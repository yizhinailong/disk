#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Verify the PostgreSQL exclusion protocol between Blob GC and content references."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
import uuid
from pathlib import Path
from typing import Any

import psycopg
from psycopg import sql
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config


REPO_ROOT = Path(__file__).resolve().parents[2]
INIT_SQL = REPO_ROOT / "sql" / "init.sql"

REARM_SQL = """
INSERT INTO storage_jobs
    (job_type, aggregate_id, dedupe_key, payload, max_attempts)
VALUES ('blob_gc', %s, %s, %s::jsonb, 8)
ON CONFLICT (dedupe_key) DO UPDATE SET
    job_type = EXCLUDED.job_type,
    aggregate_id = EXCLUDED.aggregate_id,
    payload = EXCLUDED.payload,
    status = 0,
    attempts = 0,
    max_attempts = EXCLUDED.max_attempts,
    available_at = NOW(),
    locked_by = NULL,
    locked_until = NULL,
    last_error = NULL,
    completed_at = NULL,
    updated_at = NOW()
WHERE storage_jobs.status = 3
  AND storage_jobs.job_type = EXCLUDED.job_type
  AND storage_jobs.aggregate_id = EXCLUDED.aggregate_id
RETURNING id
"""


def admin_config() -> dict[str, Any]:
    config = database_config()
    config["dbname"] = os.environ.get("PGMAINTENANCE_DB", "postgres")
    return config


def database_env(database_name: str) -> dict[str, str]:
    config = database_config()
    env = os.environ.copy()
    env.update(
        {
            "PGHOST": str(config["host"]),
            "PGPORT": str(config["port"]),
            "PGDATABASE": database_name,
            "PGUSER": str(config["user"]),
            "PGPASSWORD": str(config["password"]),
        }
    )
    return env


def connect(
    database_name: str,
    *,
    autocommit: bool = True,
) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, autocommit=autocommit, row_factory=dict_row)


def create_database(database_name: str) -> None:
    with psycopg.connect(**admin_config(), autocommit=True) as connection:
        connection.execute(sql.SQL("CREATE DATABASE {}").format(sql.Identifier(database_name)))


def drop_database(database_name: str) -> None:
    with psycopg.connect(**admin_config(), autocommit=True) as connection:
        connection.execute(
            "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
            "WHERE datname = %s AND pid <> pg_backend_pid()",
            (database_name,),
        )
        connection.execute(sql.SQL("DROP DATABASE IF EXISTS {}").format(sql.Identifier(database_name)))


def initialize_database(database_name: str) -> None:
    result = subprocess.run(
        ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
        cwd=REPO_ROOT,
        env=database_env(database_name),
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"init.sql failed ({result.returncode})\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def insert_content(
    connection: psycopg.Connection[dict[str, Any]],
    seed: int,
    ref_count: int,
) -> dict[str, Any]:
    return connection.execute(
        "INSERT INTO file_contents "
        "(hash_md5, hash_sha256, size, storage_path, mime_type, ref_count) "
        "VALUES (%s, %s, 42, %s, 'application/octet-stream', %s) "
        "RETURNING id, storage_path",
        (
            f"{seed:032x}"[-32:],
            f"{seed:064x}"[-64:],
            f"blobs/sha256/{seed:02x}/blob",
            ref_count,
        ),
    ).fetchone()


def rearm_blob_gc(
    connection: psycopg.Connection[dict[str, Any]],
    content_id: int,
    storage_path: str,
) -> dict[str, Any] | None:
    payload = json.dumps({"content_id": content_id, "storage_path": storage_path})
    return connection.execute(
        REARM_SQL,
        (str(content_id), f"blob-gc:{content_id}", payload),
    ).fetchone()


def gate_status(
    connection: psycopg.Connection[dict[str, Any]],
    content_id: int,
) -> int | None:
    row = connection.execute(
        "SELECT status FROM storage_jobs WHERE dedupe_key = %s FOR SHARE",
        (f"blob-gc:{content_id}",),
    ).fetchone()
    return None if row is None else int(row["status"])


def verify_rearm_and_gate(database_name: str) -> None:
    with connect(database_name) as connection:
        content = insert_content(connection, 1, 0)
        content_id = int(content["id"])
        path = str(content["storage_path"])
        inserted = rearm_blob_gc(connection, content_id, path)
        assert inserted is not None
        job_id = int(inserted["id"])
        assert rearm_blob_gc(connection, content_id, path) is None

        for status in (2, 4):
            connection.execute(
                "UPDATE storage_jobs SET status = %s, locked_by = NULL, locked_until = NULL "
                "WHERE id = %s",
                (status, job_id),
            )
            assert rearm_blob_gc(connection, content_id, path) is None

        connection.execute(
            "UPDATE storage_jobs SET status = 1, locked_by = 'worker-a', "
            "locked_until = NOW() + INTERVAL '1 minute' WHERE id = %s",
            (job_id,),
        )
        assert rearm_blob_gc(connection, content_id, path) is None

        connection.execute(
            "UPDATE storage_jobs SET status = 3, attempts = 7, locked_by = NULL, "
            "locked_until = NULL, last_error = 'old error', completed_at = NOW() "
            "WHERE id = %s",
            (job_id,),
        )
        assert rearm_blob_gc(connection, content_id, path) == {"id": job_id}
        rearmed = connection.execute(
            "SELECT status, attempts, locked_by, locked_until, last_error, completed_at "
            "FROM storage_jobs WHERE id = %s",
            (job_id,),
        ).fetchone()
        assert rearmed == {
            "status": 0,
            "attempts": 0,
            "locked_by": None,
            "locked_until": None,
            "last_error": None,
            "completed_at": None,
        }

        for status in (0, 1, 2, 4):
            if status == 1:
                connection.execute(
                    "UPDATE storage_jobs SET status = 1, locked_by = 'worker-gate', "
                    "locked_until = NOW() + INTERVAL '1 minute' WHERE id = %s",
                    (job_id,),
                )
            else:
                connection.execute(
                    "UPDATE storage_jobs SET status = %s, locked_by = NULL, locked_until = NULL "
                    "WHERE id = %s",
                    (status, job_id),
                )
            assert gate_status(connection, content_id) == status
            assert status != 3

        connection.execute(
            "UPDATE storage_jobs SET status = 3, locked_by = NULL, locked_until = NULL WHERE id = %s",
            (job_id,),
        )
        assert gate_status(connection, content_id) == 3

        no_job_content = insert_content(connection, 2, 1)
        assert gate_status(connection, int(no_job_content["id"])) is None
        connection.execute("DELETE FROM file_contents WHERE id = %s", (no_job_content["id"],))
        stale = connection.execute(
            "SELECT id FROM file_contents WHERE id = %s FOR UPDATE",
            (no_job_content["id"],),
        ).fetchone()
        assert stale is None


def verify_content_lock_exclusion(database_name: str) -> None:
    with connect(database_name) as connection:
        content = insert_content(connection, 3, 0)
        content_id = int(content["id"])

    locked = threading.Event()
    release = threading.Event()

    def hold_gc_lock() -> None:
        with connect(database_name, autocommit=False) as worker:
            worker.execute(
                "SELECT id FROM file_contents WHERE id = %s FOR UPDATE",
                (content_id,),
            ).fetchone()
            locked.set()
            assert release.wait(timeout=10)
            worker.commit()

    thread = threading.Thread(target=hold_gc_lock)
    thread.start()
    assert locked.wait(timeout=10)
    try:
        with connect(database_name, autocommit=False) as reference:
            reference.execute("SET LOCAL lock_timeout = '200ms'")
            try:
                reference.execute(
                    "SELECT id FROM file_contents WHERE id = %s FOR UPDATE",
                    (content_id,),
                ).fetchone()
                raise AssertionError("reference acquisition unexpectedly bypassed the GC row lock")
            except psycopg.errors.LockNotAvailable:
                reference.rollback()
    finally:
        release.set()
        thread.join(timeout=10)
        assert not thread.is_alive()


def verify_gc_completion_and_rearm(database_name: str) -> None:
    with connect(database_name) as connection:
        content = insert_content(connection, 4, 0)
        content_id = int(content["id"])
        path = str(content["storage_path"])
        job = rearm_blob_gc(connection, content_id, path)
        assert job is not None
        job_id = int(job["id"])
        connection.execute(
            "UPDATE storage_jobs SET status = 1, attempts = 1, locked_by = 'worker-delete', "
            "locked_until = NOW() + INTERVAL '1 minute' WHERE id = %s",
            (job_id,),
        )

    with connect(database_name, autocommit=False) as worker:
        candidate = worker.execute(
            "SELECT content.storage_path, content.ref_count, "
            "EXISTS (SELECT 1 FROM files WHERE content_id = content.id) AS has_file_ref, "
            "EXISTS (SELECT 1 FROM trash WHERE content_id = content.id) AS has_trash_ref "
            "FROM file_contents AS content WHERE content.id = %s FOR UPDATE OF content",
            (content_id,),
        ).fetchone()
        assert candidate == {
            "storage_path": path,
            "ref_count": 0,
            "has_file_ref": False,
            "has_trash_ref": False,
        }
        deleted = worker.execute(
            "DELETE FROM file_contents AS content "
            "WHERE content.id = %s AND content.ref_count = 0 "
            "AND NOT EXISTS (SELECT 1 FROM files WHERE content_id = content.id) "
            "AND NOT EXISTS (SELECT 1 FROM trash WHERE content_id = content.id) "
            "RETURNING content.id",
            (content_id,),
        ).fetchone()
        assert deleted == {"id": content_id}
        completed = worker.execute(
            "UPDATE storage_jobs SET status = 3, locked_by = NULL, locked_until = NULL, "
            "last_error = NULL, completed_at = NOW(), updated_at = NOW() "
            "WHERE id = %s AND status = 1 AND locked_by = 'worker-delete' RETURNING id",
            (job_id,),
        ).fetchone()
        assert completed == {"id": job_id}
        worker.commit()

    with connect(database_name) as connection:
        assert connection.execute(
            "SELECT id FROM file_contents WHERE id = %s",
            (content_id,),
        ).fetchone() is None

        restored = insert_content(connection, 5, 1)
        restored_id = int(restored["id"])
        restored_path = str(restored["storage_path"])
        restored_job = rearm_blob_gc(connection, restored_id, restored_path)
        assert restored_job is not None
        restored_job_id = int(restored_job["id"])
        connection.execute(
            "UPDATE storage_jobs SET status = 1, attempts = 1, locked_by = 'worker-noop', "
            "locked_until = NOW() + INTERVAL '1 minute' WHERE id = %s",
            (restored_job_id,),
        )

    with connect(database_name, autocommit=False) as worker:
        assert worker.execute(
            "SELECT ref_count FROM file_contents WHERE id = %s FOR UPDATE",
            (restored_id,),
        ).fetchone() == {"ref_count": 1}
        completed = worker.execute(
            "UPDATE storage_jobs SET status = 3, locked_by = NULL, locked_until = NULL, "
            "completed_at = NOW() WHERE id = %s AND status = 1 "
            "AND locked_by = 'worker-noop' RETURNING id",
            (restored_job_id,),
        ).fetchone()
        assert completed == {"id": restored_job_id}
        worker.commit()

    with connect(database_name) as connection:
        connection.execute(
            "UPDATE file_contents SET ref_count = 0 WHERE id = %s",
            (restored_id,),
        )
        assert rearm_blob_gc(connection, restored_id, restored_path) == {"id": restored_job_id}
        assert gate_status(connection, restored_id) == 0


def main() -> int:
    database_name = f"disk_blob_gc_{uuid.uuid4().hex[:12]}"
    create_database(database_name)
    try:
        initialize_database(database_name)
        verify_rearm_and_gate(database_name)
        verify_content_lock_exclusion(database_name)
        verify_gc_completion_and_rearm(database_name)
        print("Blob GC PostgreSQL protocol integration: PASS")
        return 0
    finally:
        drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Exercise request-owned multipart recovery leases against temporary PostgreSQL."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
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
LEASE_SECONDS = 30

TRACK_SQL = """
INSERT INTO storage_jobs
    (job_type, aggregate_id, dedupe_key, payload, status, attempts,
     max_attempts, available_at, locked_by, locked_until)
VALUES ('multipart_abort', %s, %s, %s::jsonb, 1, 0, 8, NOW(), %s,
        NOW() + (%s * INTERVAL '1 second'))
ON CONFLICT (dedupe_key) DO NOTHING
RETURNING id
"""

RENEW_SQL = """
UPDATE storage_jobs SET
    locked_until = NOW() + (%s * INTERVAL '1 second'),
    updated_at = NOW()
WHERE job_type = 'multipart_abort' AND aggregate_id = %s AND dedupe_key = %s
  AND status = 1 AND locked_by = %s AND locked_until > NOW()
RETURNING id
"""

RESOLVE_SQL = """
UPDATE storage_jobs SET
    status = 3,
    locked_by = NULL,
    locked_until = NULL,
    last_error = NULL,
    completed_at = NOW(),
    updated_at = NOW()
WHERE job_type = 'multipart_abort' AND aggregate_id = %s AND dedupe_key = %s
  AND status = 1 AND locked_by = %s
RETURNING id
"""

RELEASE_SQL = """
UPDATE storage_jobs SET
    status = 2,
    available_at = NOW(),
    locked_by = NULL,
    locked_until = NULL,
    last_error = %s,
    updated_at = NOW()
WHERE job_type = 'multipart_abort' AND aggregate_id = %s AND dedupe_key = %s
  AND status = 1 AND locked_by = %s
RETURNING id
"""

CLAIM_SQL = """
WITH candidate AS (
    SELECT id FROM storage_jobs
    WHERE dedupe_key = %s AND attempts < max_attempts
      AND ((status IN (0, 2) AND available_at <= NOW())
           OR (status = 1 AND locked_until <= NOW()))
    FOR UPDATE SKIP LOCKED
)
UPDATE storage_jobs AS job SET
    status = 1,
    attempts = job.attempts + 1,
    locked_by = %s,
    locked_until = NOW() + (%s * INTERVAL '1 second'),
    updated_at = NOW()
FROM candidate
WHERE job.id = candidate.id
RETURNING job.id, job.attempts, job.locked_by
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


def connect(database_name: str) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, autocommit=True, row_factory=dict_row)


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
        connection.execute(
            sql.SQL("DROP DATABASE IF EXISTS {}").format(sql.Identifier(database_name))
        )


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


def recovery_identity(key: str, upload_id: str) -> tuple[str, str]:
    aggregate_id = hashlib.sha256(f"{key}\n{upload_id}".encode()).hexdigest()
    return aggregate_id, f"multipart-abort:{aggregate_id}"


def request_owner(instance_id: str, aggregate_id: str) -> str:
    return f"{instance_id[:59]}:mp:{aggregate_id}"


def track(
    connection: psycopg.Connection[dict[str, Any]],
    key: str,
    upload_id: str,
    instance_id: str,
) -> tuple[dict[str, Any] | None, str, str, str]:
    aggregate_id, dedupe_key = recovery_identity(key, upload_id)
    owner = request_owner(instance_id, aggregate_id)
    payload = json.dumps({"backend": "s3", "key": key, "upload_id": upload_id})
    row = connection.execute(
        TRACK_SQL,
        (aggregate_id, dedupe_key, payload, owner, LEASE_SECONDS),
    ).fetchone()
    return row, aggregate_id, dedupe_key, owner


def verify_request_and_worker_ownership(database_name: str) -> None:
    with connect(database_name) as connection:
        tracked, aggregate_id, dedupe_key, owner = track(
            connection,
            "staging/upload-a/assembled/7.bin",
            "remote-upload-a",
            "api-instance-a",
        )
        assert tracked is not None
        job_id = int(tracked["id"])

        persisted = connection.execute(
            "SELECT job_type, aggregate_id, dedupe_key, payload, status, attempts, "
            "locked_by, locked_until > NOW() AS lease_active "
            "FROM storage_jobs WHERE id = %s",
            (job_id,),
        ).fetchone()
        assert persisted == {
            "job_type": "multipart_abort",
            "aggregate_id": aggregate_id,
            "dedupe_key": dedupe_key,
            "payload": {
                "backend": "s3",
                "key": "staging/upload-a/assembled/7.bin",
                "upload_id": "remote-upload-a",
            },
            "status": 1,
            "attempts": 0,
            "locked_by": owner,
            "lease_active": True,
        }

        assert connection.execute(
            CLAIM_SQL,
            (dedupe_key, "worker-early", LEASE_SECONDS),
        ).fetchone() is None
        assert connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, aggregate_id, dedupe_key, owner),
        ).fetchone() == {"id": job_id}

        released = connection.execute(
            RELEASE_SQL,
            ("abort temporarily unavailable", aggregate_id, dedupe_key, owner),
        ).fetchone()
        assert released == {"id": job_id}

        claimed = connection.execute(
            CLAIM_SQL,
            (dedupe_key, "worker-retry", LEASE_SECONDS),
        ).fetchone()
        assert claimed == {"id": job_id, "attempts": 1, "locked_by": "worker-retry"}
        assert connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, aggregate_id, dedupe_key, owner),
        ).fetchone() is None
        assert connection.execute(
            RESOLVE_SQL,
            (aggregate_id, dedupe_key, owner),
        ).fetchone() is None
        assert connection.execute(
            RESOLVE_SQL,
            (aggregate_id, dedupe_key, "worker-retry"),
        ).fetchone() == {"id": job_id}


def verify_crash_takeover_and_request_resolution(database_name: str) -> None:
    with connect(database_name) as connection:
        crashed, aggregate_id, dedupe_key, owner = track(
            connection,
            "objects/sha256/aa/aaaaaaaa.bin",
            "remote-upload-crashed",
            "api-instance-crashed",
        )
        assert crashed is not None
        job_id = int(crashed["id"])
        connection.execute(
            "UPDATE storage_jobs SET locked_until = NOW() - INTERVAL '1 second' WHERE id = %s",
            (job_id,),
        )
        takeover = connection.execute(
            CLAIM_SQL,
            (dedupe_key, "worker-takeover", LEASE_SECONDS),
        ).fetchone()
        assert takeover == {"id": job_id, "attempts": 1, "locked_by": "worker-takeover"}
        assert connection.execute(
            RESOLVE_SQL,
            (aggregate_id, dedupe_key, owner),
        ).fetchone() is None
        assert connection.execute(
            RESOLVE_SQL,
            (aggregate_id, dedupe_key, "worker-takeover"),
        ).fetchone() == {"id": job_id}

        completed, complete_id, complete_key, complete_owner = track(
            connection,
            "staging/upload-complete/assembled/9.bin",
            "remote-upload-complete",
            "api-instance-complete",
        )
        assert completed is not None
        completed_job_id = int(completed["id"])
        assert connection.execute(
            RESOLVE_SQL,
            (complete_id, complete_key, complete_owner),
        ).fetchone() == {"id": completed_job_id}
        duplicate, *_ = track(
            connection,
            "staging/upload-complete/assembled/9.bin",
            "remote-upload-complete",
            "api-instance-complete",
        )
        assert duplicate is None
        final = connection.execute(
            "SELECT status, attempts, locked_by, locked_until, completed_at IS NOT NULL AS done "
            "FROM storage_jobs WHERE id = %s",
            (completed_job_id,),
        ).fetchone()
        assert final == {
            "status": 3,
            "attempts": 0,
            "locked_by": None,
            "locked_until": None,
            "done": True,
        }


def main() -> int:
    database_name = f"disk_multipart_recovery_{uuid.uuid4().hex[:12]}"
    create_database(database_name)
    try:
        initialize_database(database_name)
        verify_request_and_worker_ownership(database_name)
        verify_crash_takeover_and_request_resolution(database_name)
        print("Multipart recovery PostgreSQL integration: PASS")
        return 0
    finally:
        drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

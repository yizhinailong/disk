#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Exercise upload finalization CAS semantics against a temporary PostgreSQL database."""

from __future__ import annotations

import os
import subprocess
import sys
import threading
import uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

import psycopg
from psycopg import sql
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config


REPO_ROOT = Path(__file__).resolve().parents[2]
INIT_SQL = REPO_ROOT / "sql" / "init.sql"
UPLOAD_ID = "state-machine-upload"
LEASE_SECONDS = 30
CONCURRENT_CLAIMERS = 24

CLAIM_SQL = """
UPDATE upload_tasks AS task SET
    status = 4,
    lease_owner = %s,
    lease_expires_at = NOW() + (%s * INTERVAL '1 second'),
    state_version = state_version + 1,
    finalize_attempts = finalize_attempts + 1,
    last_error_code = NULL,
    last_error_at = NULL
WHERE task.id = %s
  AND task.user_id = %s
  AND (
      (
          task.status = 0
          AND task.expires_at >= NOW()
          AND (
              SELECT COUNT(*) = task.total_chunks
                 AND COALESCE(MAX(chunk.chunk_index), -1) = task.total_chunks - 1
              FROM upload_task_chunks AS chunk
              WHERE chunk.task_id = task.id
          )
      )
      OR (task.status = 4 AND task.lease_expires_at <= NOW())
  )
RETURNING state_version, finalize_attempts
"""

RENEW_SQL = """
UPDATE upload_tasks SET
    lease_expires_at = NOW() + (%s * INTERVAL '1 second'),
    state_version = state_version + 1
WHERE id = %s
  AND user_id = %s
  AND status = 4
  AND lease_owner = %s
  AND state_version = %s
  AND lease_expires_at > NOW()
RETURNING state_version
"""

COMPLETE_SQL = """
UPDATE upload_tasks SET
    status = 1,
    completed_file_id = %s,
    finalized_at = NOW(),
    lease_owner = NULL,
    lease_expires_at = NULL,
    state_version = state_version + 1
WHERE id = %s
  AND user_id = %s
  AND status = 4
  AND lease_owner = %s
  AND state_version = %s
  AND lease_expires_at > NOW()
RETURNING state_version
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


def seed_upload(database_name: str) -> int:
    with connect(database_name) as connection:
        user_id = connection.execute(
            "SELECT id FROM users WHERE username = 'admin'"
        ).fetchone()
        assert user_id is not None
        resolved_user_id = int(user_id["id"])
        connection.execute(
            "UPDATE users SET storage_reserved = 6 WHERE id = %s",
            (resolved_user_id,),
        )
        connection.execute(
            "INSERT INTO upload_tasks "
            "(id, user_id, filename, file_size, file_hash, chunk_size, "
            "total_chunks, reserved_bytes, temp_path, staging_prefix, expires_at) "
            "VALUES (%s, %s, 'state-machine.bin', 6, "
            "'e80b5017098950fc58aad83c8c14978e', 2, 3, 6, %s, %s, "
            "NOW() + INTERVAL '1 day')",
            (UPLOAD_ID, resolved_user_id, UPLOAD_ID, f"staging/{UPLOAD_ID}"),
        )
        return resolved_user_id


def claim(
    database_name: str,
    user_id: int,
    owner: str,
    barrier: threading.Barrier | None = None,
) -> dict[str, Any] | None:
    with connect(database_name) as connection:
        if barrier is not None:
            barrier.wait(timeout=10)
        return connection.execute(
            CLAIM_SQL,
            (owner, LEASE_SECONDS, UPLOAD_ID, user_id),
        ).fetchone()


def concurrent_claims(
    database_name: str,
    user_id: int,
    owner_prefix: str,
) -> list[tuple[str, dict[str, Any]]]:
    barrier = threading.Barrier(CONCURRENT_CLAIMERS)

    def attempt(index: int) -> tuple[str, dict[str, Any] | None]:
        owner = f"{owner_prefix}-{index}"
        return owner, claim(database_name, user_id, owner, barrier)

    with ThreadPoolExecutor(max_workers=CONCURRENT_CLAIMERS) as executor:
        attempts = list(executor.map(attempt, range(CONCURRENT_CLAIMERS)))
    return [(owner, row) for owner, row in attempts if row is not None]


def insert_completed_file(database_name: str, user_id: int) -> int:
    with connect(database_name) as connection:
        content = connection.execute(
            "INSERT INTO file_contents "
            "(hash_md5, hash_sha256, size, storage_path, mime_type) "
            "VALUES ('e80b5017098950fc58aad83c8c14978e', "
            "'bef57ec7f53a6d40beb640a780a639c83bc29ac8a9816f1fc6c5c6dcd93c4721', "
            "6, 'objects/be/bef57e', 'application/octet-stream') RETURNING id"
        ).fetchone()
        assert content is not None
        file_row = connection.execute(
            "INSERT INTO files (user_id, content_id, name, size, mime_type, path) "
            "VALUES (%s, %s, 'state-machine.bin', 6, "
            "'application/octet-stream', '/state-machine.bin') RETURNING id",
            (user_id, content["id"]),
        ).fetchone()
        assert file_row is not None
        return int(file_row["id"])


def verify_state_machine(database_name: str) -> None:
    user_id = seed_upload(database_name)

    with connect(database_name) as connection:
        expired_upload_id = "expired-state-machine-upload"
        connection.execute(
            "INSERT INTO upload_tasks "
            "(id, user_id, filename, file_size, file_hash, chunk_size, "
            "total_chunks, reserved_bytes, temp_path, staging_prefix, expires_at) "
            "VALUES (%s, %s, 'expired.bin', 2, "
            "'187ef4436122d1cc2f40dc2b92f0eba0', 2, 1, 2, %s, %s, "
            "NOW() - INTERVAL '1 second')",
            (
                expired_upload_id,
                user_id,
                expired_upload_id,
                f"staging/{expired_upload_id}",
            ),
        )
        connection.execute(
            "INSERT INTO upload_task_chunks "
            "(task_id, chunk_index, size_bytes, hash_md5, object_key) "
            "VALUES (%s, 0, 2, '187ef4436122d1cc2f40dc2b92f0eba0', %s)",
            (expired_upload_id, f"staging/{expired_upload_id}/0"),
        )
        expired_claim = connection.execute(
            CLAIM_SQL,
            ("expired-owner", LEASE_SECONDS, expired_upload_id, user_id),
        ).fetchone()
        assert expired_claim is None
        expired_status = connection.execute(
            "SELECT status, state_version FROM upload_tasks WHERE id = %s",
            (expired_upload_id,),
        ).fetchone()
        assert expired_status == {"status": 0, "state_version": 0}

    assert claim(database_name, user_id, "incomplete-owner") is None
    with connect(database_name) as connection:
        task = connection.execute(
            "SELECT status, state_version, finalize_attempts FROM upload_tasks WHERE id = %s",
            (UPLOAD_ID,),
        ).fetchone()
        assert task == {"status": 0, "state_version": 0, "finalize_attempts": 0}
        with connection.cursor() as cursor:
            cursor.executemany(
                "INSERT INTO upload_task_chunks "
                "(task_id, chunk_index, size_bytes, hash_md5, object_key, etag) "
                "VALUES (%s, %s, 2, '187ef4436122d1cc2f40dc2b92f0eba0', %s, %s)",
                [
                    (UPLOAD_ID, index, f"staging/{UPLOAD_ID}/{index}", f"etag-{index}")
                    for index in range(3)
                ],
            )

    first_winners = concurrent_claims(database_name, user_id, "first")
    assert len(first_winners) == 1, first_winners
    first_owner, first_claim = first_winners[0]
    assert first_claim == {"state_version": 1, "finalize_attempts": 1}
    assert claim(database_name, user_id, "active-lease-owner") is None

    with connect(database_name) as connection:
        lease = connection.execute(
            "SELECT lease_owner, state_version, finalize_attempts, "
            "EXTRACT(EPOCH FROM (lease_expires_at - NOW())) AS remaining_seconds "
            "FROM upload_tasks WHERE id = %s",
            (UPLOAD_ID,),
        ).fetchone()
        assert lease is not None
        assert lease["lease_owner"] == first_owner
        assert lease["state_version"] == 1
        assert lease["finalize_attempts"] == 1
        assert 20 < float(lease["remaining_seconds"]) <= LEASE_SECONDS

        assert connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, UPLOAD_ID, user_id, "wrong-owner", 1),
        ).fetchone() is None
        renewed = connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, UPLOAD_ID, user_id, first_owner, 1),
        ).fetchone()
        assert renewed == {"state_version": 2}
        assert connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, UPLOAD_ID, user_id, first_owner, 1),
        ).fetchone() is None
        connection.execute(
            "UPDATE upload_tasks SET lease_expires_at = NOW() - INTERVAL '1 second' "
            "WHERE id = %s",
            (UPLOAD_ID,),
        )
        assert connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, UPLOAD_ID, user_id, first_owner, 2),
        ).fetchone() is None

    takeover_winners = concurrent_claims(database_name, user_id, "takeover")
    assert len(takeover_winners) == 1, takeover_winners
    takeover_owner, takeover_claim = takeover_winners[0]
    assert takeover_claim == {"state_version": 3, "finalize_attempts": 2}

    completed_file_id = insert_completed_file(database_name, user_id)
    with connect(database_name) as connection:
        assert connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, UPLOAD_ID, user_id, first_owner, 2),
        ).fetchone() is None
        assert connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, UPLOAD_ID, user_id, takeover_owner, 2),
        ).fetchone() is None
        assert connection.execute(
            COMPLETE_SQL,
            (completed_file_id, UPLOAD_ID, user_id, first_owner, 2),
        ).fetchone() is None
        assert connection.execute(
            COMPLETE_SQL,
            (completed_file_id, UPLOAD_ID, user_id, takeover_owner, 2),
        ).fetchone() is None

        completed = connection.execute(
            COMPLETE_SQL,
            (completed_file_id, UPLOAD_ID, user_id, takeover_owner, 3),
        ).fetchone()
        assert completed == {"state_version": 4}

    assert claim(database_name, user_id, "post-complete-owner") is None
    with connect(database_name) as connection:
        final_task = connection.execute(
            "SELECT status, state_version, finalize_attempts, completed_file_id, "
            "lease_owner, lease_expires_at FROM upload_tasks WHERE id = %s",
            (UPLOAD_ID,),
        ).fetchone()
        assert final_task == {
            "status": 1,
            "state_version": 4,
            "finalize_attempts": 2,
            "completed_file_id": completed_file_id,
            "lease_owner": None,
            "lease_expires_at": None,
        }
        replay = connection.execute(
            "SELECT completed_file_id FROM upload_tasks "
            "WHERE id = %s AND user_id = %s AND status = 1",
            (UPLOAD_ID, user_id),
        ).fetchone()
        assert replay == {"completed_file_id": completed_file_id}


def main() -> int:
    database_name = f"disk_upload_state_{os.getpid()}_{uuid.uuid4().hex[:8]}"
    try:
        create_database(database_name)
        initialize_database(database_name)
        verify_state_machine(database_name)
    finally:
        drop_database(database_name)

    print("Upload state machine PostgreSQL integration: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

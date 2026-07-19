#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Exercise durable storage-job queue ownership against temporary PostgreSQL."""

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
WORKER_COUNT = 24
JOB_COUNT = 48
LEASE_SECONDS = 30

ENQUEUE_SQL = """
INSERT INTO storage_jobs
    (job_type, aggregate_id, dedupe_key, payload, max_attempts)
VALUES (%s, %s, %s, %s::jsonb, %s)
ON CONFLICT (dedupe_key) DO NOTHING
RETURNING id
"""

REAP_SQL = """
UPDATE storage_jobs SET
    status = 4,
    locked_by = NULL,
    locked_until = NULL,
    last_error = COALESCE(last_error, 'Worker lease expired after final attempt'),
    updated_at = NOW()
WHERE attempts >= max_attempts
  AND (status IN (0, 2) OR (status = 1 AND locked_until <= NOW()))
RETURNING id
"""

CLAIM_SQL = """
WITH candidates AS (
    SELECT id FROM storage_jobs
    WHERE attempts < max_attempts
      AND ((status IN (0, 2) AND available_at <= NOW())
           OR (status = 1 AND locked_until <= NOW()))
    ORDER BY COALESCE(locked_until, available_at), id
    FOR UPDATE SKIP LOCKED
    LIMIT %s
)
UPDATE storage_jobs AS job SET
    status = 1,
    attempts = job.attempts + 1,
    locked_by = %s,
    locked_until = NOW() + (%s * INTERVAL '1 second'),
    updated_at = NOW()
FROM candidates
WHERE job.id = candidates.id
RETURNING job.id, job.attempts, job.locked_by
"""

RENEW_SQL = """
UPDATE storage_jobs SET
    locked_until = NOW() + (%s * INTERVAL '1 second'),
    updated_at = NOW()
WHERE id = %s AND status = 1 AND locked_by = %s AND locked_until > NOW()
RETURNING id
"""

SUCCESS_SQL = """
UPDATE storage_jobs SET
    status = 3,
    locked_by = NULL,
    locked_until = NULL,
    last_error = NULL,
    completed_at = NOW(),
    updated_at = NOW()
WHERE id = %s AND status = 1 AND locked_by = %s
RETURNING id
"""

FAIL_SQL = """
UPDATE storage_jobs SET
    status = CASE WHEN %s::boolean AND attempts < max_attempts THEN 2 ELSE 4 END,
    available_at = CASE
        WHEN %s::boolean AND attempts < max_attempts
        THEN NOW() + (%s * INTERVAL '1 second')
        ELSE available_at
    END,
    locked_by = NULL,
    locked_until = NULL,
    last_error = %s,
    updated_at = NOW()
WHERE id = %s AND status = 1 AND locked_by = %s
RETURNING status
"""

REPLAY_SQL = """
UPDATE storage_jobs SET
    status = 0,
    attempts = 0,
    available_at = NOW(),
    locked_by = NULL,
    locked_until = NULL,
    last_error = NULL,
    completed_at = NULL,
    updated_at = NOW()
WHERE id = %s AND status = 4
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


def enqueue(
    connection: psycopg.Connection[dict[str, Any]],
    dedupe_key: str,
    max_attempts: int = 3,
) -> dict[str, Any] | None:
    return connection.execute(
        ENQUEUE_SQL,
        (
            "staging_cleanup",
            dedupe_key.removeprefix("staging-cleanup:"),
            dedupe_key,
            '{"backend":"s3","prefix":"uploads/test"}',
            max_attempts,
        ),
    ).fetchone()


def claim(
    database_name: str,
    owner: str,
    limit: int,
) -> list[dict[str, Any]]:
    with connect(database_name) as connection:
        connection.execute(REAP_SQL)
        return connection.execute(CLAIM_SQL, (limit, owner, LEASE_SECONDS)).fetchall()


def verify_queue_protocol(database_name: str) -> None:
    with connect(database_name) as connection:
        first = enqueue(connection, "staging-cleanup:dedupe")
        duplicate = enqueue(connection, "staging-cleanup:dedupe")
        assert first is not None
        assert duplicate is None
        connection.execute(
            "UPDATE storage_jobs SET available_at = NOW() + INTERVAL '1 day' WHERE id = %s",
            (first["id"],),
        )

        for index in range(JOB_COUNT):
            assert enqueue(connection, f"staging-cleanup:claim-{index}") is not None

    barrier = threading.Barrier(WORKER_COUNT)

    def concurrent_claim(index: int) -> list[dict[str, Any]]:
        barrier.wait(timeout=10)
        return claim(database_name, f"worker-{index}", 2)

    with ThreadPoolExecutor(max_workers=WORKER_COUNT) as executor:
        batches = list(executor.map(concurrent_claim, range(WORKER_COUNT)))

    claimed = [row for batch in batches for row in batch]
    claimed_ids = [int(row["id"]) for row in claimed]
    assert len(claimed_ids) == JOB_COUNT
    assert len(set(claimed_ids)) == JOB_COUNT
    assert all(int(row["attempts"]) == 1 for row in claimed)

    takeover_job = claimed[0]
    takeover_id = int(takeover_job["id"])
    old_owner = str(takeover_job["locked_by"])
    with connect(database_name) as connection:
        connection.execute(
            "UPDATE storage_jobs SET locked_until = NOW() - INTERVAL '1 second' WHERE id = %s",
            (takeover_id,),
        )

    takeover = claim(database_name, "worker-takeover", 1)
    assert takeover == [{"id": takeover_id, "attempts": 2, "locked_by": "worker-takeover"}]

    with connect(database_name) as connection:
        assert connection.execute(RENEW_SQL, (LEASE_SECONDS, takeover_id, old_owner)).fetchone() is None
        assert connection.execute(SUCCESS_SQL, (takeover_id, old_owner)).fetchone() is None
        assert connection.execute(
            RENEW_SQL,
            (LEASE_SECONDS, takeover_id, "worker-takeover"),
        ).fetchone() == {"id": takeover_id}

        retry = connection.execute(
            FAIL_SQL,
            (True, True, 0, "temporary outage", takeover_id, "worker-takeover"),
        ).fetchone()
        assert retry == {"status": 2}

    final_attempt = claim(database_name, "worker-final", 1)
    assert final_attempt == [{"id": takeover_id, "attempts": 3, "locked_by": "worker-final"}]
    with connect(database_name) as connection:
        dead = connection.execute(
            FAIL_SQL,
            (True, True, 0, "still unavailable", takeover_id, "worker-final"),
        ).fetchone()
        assert dead == {"status": 4}
        assert connection.execute(REPLAY_SQL, (takeover_id,)).fetchone() == {"id": takeover_id}

        crash_job = enqueue(connection, "staging-cleanup:crash-final", max_attempts=1)
        assert crash_job is not None
        crash_id = int(crash_job["id"])

    crash_claim = claim(database_name, "worker-crash", 1)
    assert crash_claim[0]["id"] in {takeover_id, crash_id}
    if int(crash_claim[0]["id"]) == takeover_id:
        with connect(database_name) as connection:
            assert connection.execute(SUCCESS_SQL, (takeover_id, "worker-crash")).fetchone() == {
                "id": takeover_id
            }
        crash_claim = claim(database_name, "worker-crash", 1)
    assert crash_claim == [{"id": crash_id, "attempts": 1, "locked_by": "worker-crash"}]

    with connect(database_name) as connection:
        connection.execute(
            "UPDATE storage_jobs SET locked_until = NOW() - INTERVAL '1 second' WHERE id = %s",
            (crash_id,),
        )
        reaped = connection.execute(REAP_SQL).fetchall()
        assert {int(row["id"]) for row in reaped} == {crash_id}
        crash_status = connection.execute(
            "SELECT status, locked_by, locked_until FROM storage_jobs WHERE id = %s",
            (crash_id,),
        ).fetchone()
        assert crash_status == {"status": 4, "locked_by": None, "locked_until": None}

        success_candidate = next(row for row in claimed if int(row["id"]) != takeover_id)
        success = connection.execute(
            SUCCESS_SQL,
            (success_candidate["id"], success_candidate["locked_by"]),
        ).fetchone()
        assert success == {"id": success_candidate["id"]}


def main() -> int:
    database_name = f"disk_storage_jobs_{uuid.uuid4().hex[:12]}"
    create_database(database_name)
    try:
        initialize_database(database_name)
        verify_queue_protocol(database_name)
        print("Storage job queue PostgreSQL integration: PASS")
        return 0
    finally:
        drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

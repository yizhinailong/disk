#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify Worker failures persist PostgreSQL Retry and DeadLetter states."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py import cleanup, ensure_server, execute, query_one


JOB_IDS: list[int] = []


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def create_job(
    job_type: str,
    aggregate_id: str,
    dedupe_key: str,
    payload: dict[str, str],
    max_attempts: int,
) -> int:
    row = query_one(
        """
        INSERT INTO storage_jobs
            (job_type, aggregate_id, dedupe_key, payload, max_attempts)
        VALUES (%s, %s, %s, %s::jsonb, %s)
        RETURNING id
        """,
        (job_type, aggregate_id, dedupe_key, json.dumps(payload), max_attempts),
    )
    require(row is not None, "failed to create storage job fixture")
    job_id = int(row["id"])
    JOB_IDS.append(job_id)
    return job_id


def wait_for_status(job_id: int, status: int, timeout_seconds: float) -> dict:
    deadline = time.monotonic() + timeout_seconds
    last_row = None
    while time.monotonic() < deadline:
        last_row = query_one(
            """
            SELECT status, attempts, max_attempts, locked_by, locked_until, last_error
            FROM storage_jobs WHERE id = %s
            """,
            (job_id,),
        )
        if last_row is not None and int(last_row["status"]) == status:
            return last_row
        time.sleep(0.05)
    raise AssertionError(
        f"storage job {job_id} did not reach status {status}: last_row={last_row}"
    )


def cleanup_fixtures() -> None:
    if JOB_IDS:
        execute("DELETE FROM storage_jobs WHERE id = ANY(%s)", (JOB_IDS,))
        JOB_IDS.clear()


def test_failure_persistence() -> None:
    fixture_id = uuid.uuid4().hex
    permanent_id = create_job(
        "staging_cleanup",
        f"invalid-{fixture_id}",
        f"failure-persistence:permanent:{fixture_id}",
        {},
        8,
    )

    object_key = f"staging/{fixture_id}/assembled/1.bin"
    remote_upload_id = f"remote-{fixture_id}"
    digest = hashlib.sha256(
        f"{object_key}\n{remote_upload_id}".encode()
    ).hexdigest()
    retry_id = create_job(
        "multipart_abort",
        digest,
        f"multipart-abort:{digest}",
        {
            "backend": "s3",
            "key": object_key,
            "upload_id": remote_upload_id,
        },
        2,
    )

    permanent = wait_for_status(permanent_id, 4, 10)
    require(permanent["attempts"] == 1, "permanent failure retried unexpectedly")
    require(permanent["locked_by"] is None, "dead-letter retained an owner")
    require(
        "payload" in str(permanent["last_error"]).lower(),
        "dead-letter did not preserve the validation error",
    )

    retry = wait_for_status(retry_id, 2, 10)
    require(retry["attempts"] == 1, "retry state has an unexpected attempt count")
    require(retry["locked_by"] is None, "retry state retained an owner")
    require(
        "not configured" in str(retry["last_error"]).lower(),
        "retry state did not preserve the dependency error",
    )

    exhausted = wait_for_status(retry_id, 4, 15)
    require(exhausted["attempts"] == 2, "retry did not stop at max_attempts")
    require(exhausted["locked_until"] is None, "exhausted retry retained a lease")


def main() -> int:
    os.environ["DISK_PROCESS_ROLE"] = "all"
    os.environ["DISK_INSTANCE_ID"] = "failure-persistence-worker"
    ensure_server()
    try:
        test_failure_persistence()
        print("Storage job failure persistence integration: PASS")
        return 0
    finally:
        cleanup_fixtures()
        cleanup()


if __name__ == "__main__":
    raise SystemExit(main())

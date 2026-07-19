#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Exercise audited dead-letter operations and internal Prometheus metrics."""

from __future__ import annotations

import json
import os
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py import (
    cleanup,
    do_login,
    ensure_server,
    execute,
    fetch,
    log_info,
    log_pass,
    query_one,
    scalar,
)


JOB_ID: int | None = None


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def cleanup_fixture() -> None:
    global JOB_ID
    if JOB_ID is None:
        return
    execute(
        "DELETE FROM operation_logs WHERE action = %s AND target_type = %s AND target_id = %s",
        ("admin.storage_job.replay", "storage_job", JOB_ID),
    )
    execute("DELETE FROM storage_jobs WHERE id = %s", (JOB_ID,))
    JOB_ID = None


def create_dead_letter() -> int:
    aggregate_id = f"ops-{uuid.uuid4().hex}"
    row = query_one(
        """
        INSERT INTO storage_jobs
            (job_type, aggregate_id, dedupe_key, payload, status, attempts,
             max_attempts, last_error, completed_at)
        VALUES
            ('blob_gc', %s, %s, %s::jsonb, 4, 3, 3, 'fixture failure', NOW())
        RETURNING id
        """,
        (
            aggregate_id,
            f"ops-dead-letter:{aggregate_id}",
            json.dumps({"content_id": "999999999"}),
        ),
    )
    require(row is not None, "failed to create dead-letter fixture")
    return int(row["id"])


def response_json(response) -> dict:
    try:
        return json.loads(response.text)
    except json.JSONDecodeError as error:
        raise AssertionError(f"response is not JSON: {response.text}") from error


def test_metrics() -> None:
    log_info("Checking unauthenticated internal metrics contract")
    response = fetch("/metrics")
    require(response.status_code == 200, f"metrics returned HTTP {response.status_code}")
    require(
        response.headers.get("content-type", "").startswith("text/plain"),
        "metrics content type is not text/plain",
    )
    for metric in (
        "disk_http_requests_total",
        "disk_storage_jobs{status=\"dead_letter\"}",
        "disk_upload_tasks{status=\"finalizing\"}",
        "disk_reconciliation_findings_unresolved",
        "disk_metrics_snapshot_success ",
        "disk_process_info",
    ):
        require(metric in response.text, f"metrics output is missing {metric}")
    snapshot_lines = [
        line
        for line in response.text.splitlines()
        if line.startswith("disk_metrics_snapshot_success ")
    ]
    require(
        snapshot_lines in (["disk_metrics_snapshot_success 0"], ["disk_metrics_snapshot_success 1"]),
        "metrics snapshot status is not a Prometheus boolean",
    )
    require(response.headers.get("x-request-id", ""), "metrics response lacks request ID")
    require(
        response.headers.get("x-disk-instance-id", "") == "ops-api",
        "metrics response lacks the expected instance ID",
    )

    exact_only = fetch("/metrics/extra")
    require(exact_only.status_code == 401, "metrics JWT exemption is not exact")
    log_pass("Internal metrics are scrapeable and use an exact public-path exemption")


def test_dead_letter_operations(token: str) -> None:
    global JOB_ID
    JOB_ID = create_dead_letter()
    headers = {"Authorization": f"Bearer {token}"}

    unauthenticated = fetch("/api/admin/storage-jobs")
    require(unauthenticated.status_code == 401, "storage job list is not admin protected")

    listing = fetch("/api/admin/storage-jobs?page_size=100", headers=headers)
    require(listing.status_code == 200, f"storage job list returned HTTP {listing.status_code}")
    items = response_json(listing)["data"]["items"]
    fixture = next((item for item in items if int(item["id"]) == JOB_ID), None)
    require(fixture is not None, "default dead-letter list omitted fixture")
    require("payload" not in fixture, "storage job list leaked payload")

    detail = fetch(f"/api/admin/storage-jobs/{JOB_ID}", headers=headers)
    require(detail.status_code == 200, f"storage job detail returned HTTP {detail.status_code}")
    detail_data = response_json(detail)["data"]
    require(detail_data["payload"]["content_id"] == "999999999", "detail payload drifted")

    dry_run = fetch(
        f"/api/admin/storage-jobs/{JOB_ID}/replay",
        method="POST",
        headers=headers,
        json_body={},
    )
    require(dry_run.status_code == 200, f"dry-run returned HTTP {dry_run.status_code}")
    dry_data = response_json(dry_run)["data"]
    require(dry_data["dry_run"] is True, "dry-run flag is false")
    require(dry_data["eligible"] is True, "dead-letter was not eligible")
    require(dry_data["replayed"] is False, "dry-run mutated the task")
    require(
        scalar("SELECT status FROM storage_jobs WHERE id = %s", (JOB_ID,)) == 4,
        "dry-run changed storage job state",
    )
    require(
        scalar(
            "SELECT COUNT(*) FROM operation_logs WHERE action = %s AND target_id = %s",
            ("admin.storage_job.replay", JOB_ID),
        )
        == 0,
        "dry-run wrote an audit record",
    )

    replay = fetch(
        f"/api/admin/storage-jobs/{JOB_ID}/replay",
        method="POST",
        headers=headers,
        json_body={
            "dry_run": False,
            "confirm_job_id": JOB_ID,
            "reason": "  dependency recovered  ",
        },
    )
    require(replay.status_code == 200, f"replay returned HTTP {replay.status_code}: {replay.text}")
    replay_data = response_json(replay)["data"]
    require(replay_data["replayed"] is True, "replay response did not confirm mutation")
    require(replay_data["job"]["status"] == "pending", "replay did not return pending state")

    job = query_one(
        """
        SELECT status, attempts, locked_by, locked_until, last_error, completed_at
        FROM storage_jobs WHERE id = %s
        """,
        (JOB_ID,),
    )
    require(job is not None, "replayed job disappeared")
    require(job["status"] == 0 and job["attempts"] == 0, "replay state was not reset")
    require(
        all(job[field] is None for field in ("locked_by", "locked_until", "last_error", "completed_at")),
        "replay retained lease, error, or completion fields",
    )

    audit = query_one(
        """
        SELECT user_id, details
        FROM operation_logs
        WHERE action = 'admin.storage_job.replay'
          AND target_type = 'storage_job'
          AND target_id = %s
        """,
        (JOB_ID,),
    )
    require(audit is not None and audit["user_id"] is not None, "replay audit is missing operator")
    require(audit["details"]["reason"] == "dependency recovered", "audit reason was not normalized")
    require(audit["details"]["previous_status"] == "dead_letter", "audit lost prior state")

    duplicate = fetch(
        f"/api/admin/storage-jobs/{JOB_ID}/replay",
        method="POST",
        headers=headers,
        json_body={
            "dry_run": False,
            "confirm_job_id": JOB_ID,
            "reason": "duplicate",
        },
    )
    require(duplicate.status_code == 409, "second replay did not report a state conflict")
    require(
        scalar(
            "SELECT COUNT(*) FROM operation_logs WHERE action = %s AND target_id = %s",
            ("admin.storage_job.replay", JOB_ID),
        )
        == 1,
        "conflicting replay wrote a second audit record",
    )
    log_pass("Dead-letter list, detail, dry-run, replay, conflict, and audit contracts hold")


def main() -> int:
    os.environ["DISK_PROCESS_ROLE"] = "api"
    os.environ["DISK_INSTANCE_ID"] = "ops-api"
    ensure_server()
    try:
        token = do_login()
        require(token is not None, "admin login failed")
        test_metrics()
        test_dead_letter_operations(token)
        return 0
    finally:
        cleanup_fixture()
        cleanup()


if __name__ == "__main__":
    raise SystemExit(main())

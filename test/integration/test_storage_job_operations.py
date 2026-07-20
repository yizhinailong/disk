#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Exercise audited dead-letter operations and internal Prometheus metrics."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py import (
    cleanup,
    db_connection,
    do_login,
    ensure_server,
    execute,
    fetch,
    log_info,
    log_pass,
    query_all,
    query_one,
    scalar,
    upload_temp_dir,
)


JOB_ID: int | None = None
DIAGNOSTIC_UPLOAD_ID: str | None = None
DIAGNOSTIC_SCAN_ID: str | None = None
DIAGNOSTIC_JOB_IDS: list[int] = []


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def cleanup_fixture() -> None:
    global JOB_ID, DIAGNOSTIC_UPLOAD_ID, DIAGNOSTIC_SCAN_ID, DIAGNOSTIC_JOB_IDS
    if DIAGNOSTIC_UPLOAD_ID is not None:
        with db_connection() as connection:
            task = connection.execute(
                "SELECT user_id, reserved_bytes FROM upload_tasks WHERE id = %s",
                (DIAGNOSTIC_UPLOAD_ID,),
            ).fetchone()
            connection.execute(
                "DELETE FROM storage_reconciliation_findings "
                "WHERE finding_type = 'upload_staging_mismatch' AND resource_id = %s",
                (DIAGNOSTIC_UPLOAD_ID,),
            )
            if DIAGNOSTIC_JOB_IDS:
                connection.execute(
                    "DELETE FROM storage_jobs WHERE id = ANY(%s)",
                    (DIAGNOSTIC_JOB_IDS,),
                )
            connection.execute(
                "DELETE FROM upload_tasks WHERE id = %s",
                (DIAGNOSTIC_UPLOAD_ID,),
            )
            if task is not None:
                connection.execute(
                    "UPDATE users SET storage_reserved = "
                    "GREATEST(storage_reserved - %s, 0) WHERE id = %s",
                    (task["reserved_bytes"], task["user_id"]),
                )
        shutil.rmtree(upload_temp_dir(DIAGNOSTIC_UPLOAD_ID), ignore_errors=True)
        DIAGNOSTIC_UPLOAD_ID = None
        DIAGNOSTIC_SCAN_ID = None
        DIAGNOSTIC_JOB_IDS = []

    if JOB_ID is not None:
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


def create_upload_diagnostic_fixture(token: str) -> tuple[str, Path]:
    global DIAGNOSTIC_UPLOAD_ID, DIAGNOSTIC_SCAN_ID, DIAGNOSTIC_JOB_IDS
    payload = f"upload-diagnostic-{uuid.uuid4().hex}".encode()
    file_hash = hashlib.md5(payload).hexdigest()
    headers = {"Authorization": f"Bearer {token}"}

    initialized = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=headers,
        json_body={
            "filename": f"diagnostic-{uuid.uuid4().hex}.bin",
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    require(initialized.status_code == 200, f"diagnostic upload init failed: {initialized.text}")
    initialized_data = response_json(initialized)["data"]
    require(initialized_data["instant_upload"] is False, "diagnostic fixture was instant upload")
    DIAGNOSTIC_UPLOAD_ID = str(initialized_data["upload_id"])

    uploaded = fetch(
        f"/api/file/upload/chunk?upload_id={DIAGNOSTIC_UPLOAD_ID}"
        f"&chunk_index=0&chunk_hash={file_hash}",
        method="POST",
        headers={**headers, "Content-Type": "application/octet-stream"},
        data=payload,
    )
    require(uploaded.status_code == 200, f"diagnostic chunk upload failed: {uploaded.text}")

    chunk = query_one(
        "SELECT object_key FROM upload_task_chunks WHERE task_id = %s AND chunk_index = 0",
        (DIAGNOSTIC_UPLOAD_ID,),
    )
    require(chunk is not None and chunk["object_key"], "diagnostic chunk descriptor is missing")

    DIAGNOSTIC_SCAN_ID = f"diagnostic-scan-{uuid.uuid4().hex}"
    with db_connection() as connection:
        updated = connection.execute(
            """
            UPDATE upload_tasks
            SET status = 4,
                state_version = 3,
                lease_owner = 'ops-diagnostic-owner',
                lease_expires_at = NOW() + INTERVAL '10 minutes',
                finalize_attempts = 2,
                last_error_code = 10006,
                last_error_at = NOW()
            WHERE id = %s AND status = 0
            RETURNING staging_prefix
            """,
            (DIAGNOSTIC_UPLOAD_ID,),
        ).fetchone()
        require(updated is not None, "failed to move diagnostic upload into finalizing state")

        cleanup_job = connection.execute(
            """
            INSERT INTO storage_jobs
                (job_type, aggregate_id, dedupe_key, payload, last_error)
            VALUES ('staging_cleanup', %s, %s, %s::jsonb, 'raw dependency failure')
            RETURNING id
            """,
            (
                DIAGNOSTIC_UPLOAD_ID,
                f"diagnostic-cleanup:{DIAGNOSTIC_UPLOAD_ID}",
                json.dumps(
                    {
                        "upload_id": DIAGNOSTIC_UPLOAD_ID,
                        "backend": "local",
                        "prefix": DIAGNOSTIC_UPLOAD_ID,
                    }
                ),
            ),
        ).fetchone()
        reconcile_job = connection.execute(
            """
            INSERT INTO storage_jobs (job_type, aggregate_id, dedupe_key, payload)
            VALUES ('storage_reconcile', %s, %s, %s::jsonb)
            RETURNING id
            """,
            (
                DIAGNOSTIC_SCAN_ID,
                f"diagnostic-reconcile:{DIAGNOSTIC_SCAN_ID}",
                json.dumps(
                    {
                        "scan_id": DIAGNOSTIC_SCAN_ID,
                        "scope": "staging",
                        "cursor": "",
                        "page_size": 100,
                        "object_page_size": 100,
                    }
                ),
            ),
        ).fetchone()
        connection.execute(
            """
            INSERT INTO storage_reconciliation_findings
                (finding_type, resource_id, resource_locator, severity,
                 resolution_strategy, details)
            VALUES ('upload_staging_mismatch', %s, %s, 1, 'manual', %s::jsonb)
            """,
            (
                DIAGNOSTIC_UPLOAD_ID,
                DIAGNOSTIC_UPLOAD_ID,
                json.dumps({"scan_id": DIAGNOSTIC_SCAN_ID}),
            ),
        )
        require(cleanup_job is not None and reconcile_job is not None, "failed to create related jobs")
        DIAGNOSTIC_JOB_IDS = [int(cleanup_job["id"]), int(reconcile_job["id"])]

    object_path = upload_temp_dir(DIAGNOSTIC_UPLOAD_ID).parent / str(chunk["object_key"])
    require(object_path.is_file(), f"diagnostic chunk object does not exist: {object_path}")
    return DIAGNOSTIC_UPLOAD_ID, object_path


def diagnostic_database_snapshot(upload_id: str) -> dict[str, object]:
    return {
        "task": query_one(
            """
            SELECT status, state_version, lease_owner, lease_expires_at,
                   finalize_attempts, last_error_code, last_error_at, updated_at
            FROM upload_tasks WHERE id = %s
            """,
            (upload_id,),
        ),
        "chunks": query_all(
            """
            SELECT chunk_index, size_bytes, hash_md5, object_key, etag, uploaded_at
            FROM upload_task_chunks WHERE task_id = %s ORDER BY chunk_index
            """,
            (upload_id,),
        ),
        "jobs": query_all(
            """
            SELECT id, status, attempts, available_at, locked_by, locked_until,
                   last_error, completed_at, updated_at
            FROM storage_jobs WHERE id = ANY(%s) ORDER BY id
            """,
            (DIAGNOSTIC_JOB_IDS,),
        ),
    }


def test_upload_diagnostics(token: str) -> None:
    upload_id, object_path = create_upload_diagnostic_fixture(token)
    headers = {"Authorization": f"Bearer {token}"}
    path = (
        f"/api/admin/uploads/{upload_id}/diagnostics"
        "?chunk_page=1&chunk_page_size=1&job_page=1&job_page_size=100"
    )

    unauthenticated = fetch(path)
    require(unauthenticated.status_code == 401, "upload diagnostics are not admin protected")
    invalid_page = fetch(
        f"/api/admin/uploads/{upload_id}/diagnostics?chunk_page_size=101",
        headers=headers,
    )
    require(invalid_page.status_code == 400, "upload diagnostics accepted oversized pagination")
    missing = fetch(
        f"/api/admin/uploads/missing-{uuid.uuid4().hex}/diagnostics",
        headers=headers,
    )
    require(missing.status_code == 404, "unknown diagnostic upload did not return HTTP 404")

    before = diagnostic_database_snapshot(upload_id)
    response = fetch(path, headers=headers)
    require(response.status_code == 200, f"upload diagnostics failed: {response.text}")
    data = response_json(response)["data"]
    task = data["task"]
    require(task["upload_id"] == upload_id, "diagnostic task ID drifted")
    require(task["status"] == "finalizing", "diagnostic task status drifted")
    require(task["state_version"] == 3, "diagnostic state version drifted")
    require(task["finalize_attempts"] == 2, "diagnostic finalize attempts drifted")
    require(task["last_error_code"] == 10006, "diagnostic last error code drifted")
    require(task["lease"]["owner"] == "ops-diagnostic-owner", "diagnostic lease owner drifted")
    require(task["lease"]["expired"] is False, "fresh diagnostic lease appears expired")

    require(data["chunk_pagination"]["total"] == 1, "diagnostic chunk total drifted")
    require(len(data["chunks"]) == 1, "diagnostic chunk page drifted")
    chunk = data["chunks"][0]
    require(chunk["size_bytes"] == object_path.stat().st_size, "diagnostic chunk size drifted")
    require(chunk["object_head"]["status"] == "present", "present chunk was not found")
    require(chunk["object_head"]["matches_record"] is True, "present chunk did not match DB")
    require(chunk["object_head"]["error_code"] is None, "successful HEAD returned an error")

    related = data["related_jobs"]
    require(related["pagination"]["total"] == 2, "related job total drifted")
    require(
        {item["job_type"] for item in related["items"]}
        == {"staging_cleanup", "storage_reconcile"},
        "diagnostics omitted or over-selected related jobs",
    )
    require(
        all("payload" not in item for item in related["items"]),
        "related diagnostic jobs leaked payload",
    )
    require(
        all(item["last_error"] is None for item in related["items"]),
        "related diagnostic jobs leaked underlying error text",
    )

    object_path.unlink()
    missing_object = fetch(path, headers=headers)
    require(missing_object.status_code == 200, "missing object made diagnostics fail")
    missing_head = response_json(missing_object)["data"]["chunks"][0]["object_head"]
    require(missing_head["status"] == "missing", "missing object was not reported")
    require(missing_head["matches_record"] is False, "missing object matched DB metadata")
    require(
        diagnostic_database_snapshot(upload_id) == before,
        "read-only upload diagnostics changed task, chunk, or job state",
    )

    execute(
        "UPDATE upload_task_chunks SET object_key = %s "
        "WHERE task_id = %s AND chunk_index = 0",
        (f"another-upload/chunks/0-{'0' * 32}.part", upload_id),
    )
    invalid_descriptor_before = diagnostic_database_snapshot(upload_id)
    invalid_descriptor = fetch(path, headers=headers)
    require(invalid_descriptor.status_code == 200, "invalid descriptor made diagnostics fail")
    invalid_head = response_json(invalid_descriptor)["data"]["chunks"][0]["object_head"]
    require(invalid_head["status"] == "error", "invalid descriptor was not isolated")
    require(invalid_head["error_code"] == 50009, "invalid descriptor error code drifted")
    require(
        diagnostic_database_snapshot(upload_id) == invalid_descriptor_before,
        "invalid-descriptor diagnostics changed task, chunk, or job state",
    )
    log_pass("Upload diagnostics expose task, lease, chunk HEAD, and jobs without writes")


def main() -> int:
    os.environ["DISK_PROCESS_ROLE"] = "api"
    os.environ["DISK_INSTANCE_ID"] = "ops-api"
    ensure_server()
    try:
        token = do_login()
        require(token is not None, "admin login failed")
        test_metrics()
        test_dead_letter_operations(token)
        test_upload_diagnostics(token)
        return 0
    finally:
        cleanup_fixture()
        cleanup()


if __name__ == "__main__":
    raise SystemExit(main())

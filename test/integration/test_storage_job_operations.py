#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Exercise audited storage operations and internal Prometheus metrics."""

from __future__ import annotations

import base64
import hashlib
import json
import os
import shutil
import sys
import time
import uuid
from pathlib import Path

EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", ".sisyphus/evidence"))
SERVER_LOG_PATH = EVIDENCE_ROOT / "storage-job-operations-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py import (
    cleanup,
    db_connection,
    do_login,
    ensure_server,
    execute,
    fetch,
    header_value,
    log_info,
    log_pass,
    query_all,
    query_one,
    redis_delete_pattern,
    redis_set_value,
    scalar,
    upload_temp_dir,
)


JOB_ID: int | None = None
DIAGNOSTIC_UPLOAD_ID: str | None = None
DIAGNOSTIC_SCAN_ID: str | None = None
DIAGNOSTIC_JOB_IDS: list[int] = []
RECOVERY_SCAN_IDS: list[str] = []
RECOVERY_JOB_IDS: list[int] = []


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def configured_admin_rate_value(key: str, fallback: int) -> int:
    """Read one positive administrator rate-limit value from active config."""
    repo_root = Path(__file__).resolve().parents[2]
    config_path = Path(os.environ.get("DISK_CONFIG_FILE", repo_root / "config.json"))
    if not config_path.is_absolute():
        config_path = repo_root / config_path
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
        value = int(config.get("custom_config", {}).get("disk", {}).get(key, fallback))
        return value if value > 0 else fallback
    except Exception:
        return fallback


def access_token_subject(token: str) -> int:
    """Read the trusted local access token subject for test-key ownership."""
    parts = token.split(".")
    if len(parts) != 3:
        raise ValueError("access token is not a compact JWT")
    payload_segment = parts[1] + "=" * (-len(parts[1]) % 4)
    payload = json.loads(base64.urlsafe_b64decode(payload_segment).decode("utf-8"))
    if payload.get("iss") != "disk" or payload.get("type") != "access":
        raise ValueError("token payload is not a disk access token")
    subject = str(payload.get("sub", ""))
    if not subject.isdigit() or int(subject) <= 0:
        raise ValueError("access token subject is not a positive user ID")
    return int(subject)


def cleanup_fixture() -> None:
    global JOB_ID, DIAGNOSTIC_UPLOAD_ID, DIAGNOSTIC_SCAN_ID, DIAGNOSTIC_JOB_IDS
    global RECOVERY_SCAN_IDS, RECOVERY_JOB_IDS
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
            connection.execute(
                "DELETE FROM operation_logs "
                "WHERE action IN ('admin.upload.lease_release', "
                "'admin.upload.cleanup_rebuild') "
                "AND target_type = 'upload' AND target_name = %s",
                (DIAGNOSTIC_UPLOAD_ID,),
            )
            if RECOVERY_SCAN_IDS:
                connection.execute(
                    "DELETE FROM operation_logs "
                    "WHERE action = 'admin.storage.reconcile' "
                    "AND target_type = 'reconciliation' AND target_name = ANY(%s)",
                    (RECOVERY_SCAN_IDS,),
                )
                connection.execute(
                    "DELETE FROM storage_jobs "
                    "WHERE job_type = 'storage_reconcile' AND aggregate_id = ANY(%s)",
                    (RECOVERY_SCAN_IDS,),
                )
            all_job_ids = DIAGNOSTIC_JOB_IDS + RECOVERY_JOB_IDS
            if all_job_ids:
                connection.execute(
                    "DELETE FROM storage_jobs WHERE id = ANY(%s)",
                    (all_job_ids,),
                )
            connection.execute(
                "DELETE FROM storage_jobs "
                "WHERE job_type = 'staging_cleanup' AND aggregate_id = %s",
                (DIAGNOSTIC_UPLOAD_ID,),
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
        RECOVERY_SCAN_IDS = []
        RECOVERY_JOB_IDS = []

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


def audit_count(action: str, target_name: str) -> int:
    return int(
        scalar(
            "SELECT COUNT(*) FROM operation_logs "
            "WHERE action = %s AND target_name = %s",
            (action, target_name),
        )
    )


def require_response_context(response, request_id: str) -> str:
    require(
        response.headers.get("x-request-id", "") == request_id,
        f"response did not preserve request ID {request_id}",
    )
    instance_id = response.headers.get("x-disk-instance-id", "")
    require(instance_id == "ops-api", "response did not identify the storage operations API")
    return instance_id


def wait_for_admin_log(
    *,
    request_id: str,
    instance_id: str,
    message_marker: str,
    upload_id: str | None = None,
    job_id: int | None = None,
    lease_owner: str | None = None,
    state_version: int | None = None,
) -> dict[str, object]:
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if SERVER_LOG_PATH.is_file():
            for line in SERVER_LOG_PATH.read_text(
                encoding="utf-8",
                errors="replace",
            ).splitlines():
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if (
                    record.get("schema_version") == 1
                    and record.get("source") == "application"
                    and record.get("request_id") == request_id
                    and record.get("instance_id") == instance_id
                    and record.get("operation") == "admin"
                    and record.get("upload_id") == upload_id
                    and record.get("job_id") == job_id
                    and record.get("lease_owner") == lease_owner
                    and record.get("state_version") == state_version
                    and message_marker in str(record.get("message", ""))
                ):
                    return record
        time.sleep(0.05)
    raise AssertionError(
        f"admin log missing request_id={request_id}, upload_id={upload_id}, "
        f"job_id={job_id}, lease_owner={lease_owner}, state_version={state_version}, "
        f"message={message_marker}"
    )


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

    list_request_id = f"ops-storage-job-list-{uuid.uuid4().hex}"
    listing = fetch(
        "/api/admin/storage-jobs?page_size=100",
        headers={**headers, "X-Request-Id": list_request_id},
    )
    require(listing.status_code == 200, f"storage job list returned HTTP {listing.status_code}")
    list_instance_id = require_response_context(listing, list_request_id)
    items = response_json(listing)["data"]["items"]
    fixture = next((item for item in items if int(item["id"]) == JOB_ID), None)
    require(fixture is not None, "default dead-letter list omitted fixture")
    require("payload" not in fixture, "storage job list leaked payload")
    wait_for_admin_log(
        request_id=list_request_id,
        instance_id=list_instance_id,
        job_id=None,
        message_marker="Storage job admin list successful",
    )

    detail_request_id = f"ops-storage-job-detail-{uuid.uuid4().hex}"
    detail = fetch(
        f"/api/admin/storage-jobs/{JOB_ID}",
        headers={**headers, "X-Request-Id": detail_request_id},
    )
    require(detail.status_code == 200, f"storage job detail returned HTTP {detail.status_code}")
    detail_instance_id = require_response_context(detail, detail_request_id)
    detail_data = response_json(detail)["data"]
    require(detail_data["payload"]["content_id"] == "999999999", "detail payload drifted")
    wait_for_admin_log(
        request_id=detail_request_id,
        instance_id=detail_instance_id,
        job_id=JOB_ID,
        message_marker="Storage job admin detail successful",
    )

    dry_run_request_id = f"ops-storage-job-dry-run-{uuid.uuid4().hex}"
    dry_run = fetch(
        f"/api/admin/storage-jobs/{JOB_ID}/replay",
        method="POST",
        headers={**headers, "X-Request-Id": dry_run_request_id},
        json_body={},
    )
    require(dry_run.status_code == 200, f"dry-run returned HTTP {dry_run.status_code}")
    dry_run_instance_id = require_response_context(dry_run, dry_run_request_id)
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
    wait_for_admin_log(
        request_id=dry_run_request_id,
        instance_id=dry_run_instance_id,
        job_id=JOB_ID,
        message_marker="Storage job replay successful: dry_run=true",
    )

    replay_request_id = f"ops-storage-job-replay-{uuid.uuid4().hex}"
    replay = fetch(
        f"/api/admin/storage-jobs/{JOB_ID}/replay",
        method="POST",
        headers={**headers, "X-Request-Id": replay_request_id},
        json_body={
            "dry_run": False,
            "confirm_job_id": JOB_ID,
            "reason": "  dependency recovered  ",
        },
    )
    require(replay.status_code == 200, f"replay returned HTTP {replay.status_code}: {replay.text}")
    replay_instance_id = require_response_context(replay, replay_request_id)
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
    require(audit["details"]["request_id"] == replay_request_id, "audit lost request ID")
    require(audit["details"]["operation"] == "admin", "audit operation drifted")
    require(audit["details"]["job_id"] == JOB_ID, "audit job ID drifted")
    require("payload" not in audit["details"], "replay audit stored the task payload")
    wait_for_admin_log(
        request_id=replay_request_id,
        instance_id=replay_instance_id,
        job_id=JOB_ID,
        message_marker="Storage job dead-letter replayed",
    )

    conflict_request_id = f"ops-storage-job-conflict-{uuid.uuid4().hex}"
    duplicate = fetch(
        f"/api/admin/storage-jobs/{JOB_ID}/replay",
        method="POST",
        headers={**headers, "X-Request-Id": conflict_request_id},
        json_body={
            "dry_run": False,
            "confirm_job_id": JOB_ID,
            "reason": "duplicate",
        },
    )
    require(duplicate.status_code == 409, "second replay did not report a state conflict")
    conflict_instance_id = require_response_context(duplicate, conflict_request_id)
    require(
        scalar(
            "SELECT COUNT(*) FROM operation_logs WHERE action = %s AND target_id = %s",
            ("admin.storage_job.replay", JOB_ID),
        )
        == 1,
        "conflicting replay wrote a second audit record",
    )
    wait_for_admin_log(
        request_id=conflict_request_id,
        instance_id=conflict_instance_id,
        job_id=JOB_ID,
        message_marker="Storage job replay failed",
    )
    log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
    require(token not in log_text, "storage job logs exposed the administrator JWT")
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
    diagnostic_request_id = f"ops-upload-diagnostic-{uuid.uuid4().hex}"
    response = fetch(
        path,
        headers={**headers, "X-Request-Id": diagnostic_request_id},
    )
    require(response.status_code == 200, f"upload diagnostics failed: {response.text}")
    diagnostic_instance_id = require_response_context(response, diagnostic_request_id)
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
    received_log = wait_for_admin_log(
        request_id=diagnostic_request_id,
        instance_id=diagnostic_instance_id,
        message_marker="Received upload diagnostic request",
        upload_id=upload_id,
    )
    completed_log = wait_for_admin_log(
        request_id=diagnostic_request_id,
        instance_id=diagnostic_instance_id,
        message_marker="Upload diagnostic completed",
        upload_id=upload_id,
        lease_owner="ops-diagnostic-owner",
        state_version=3,
    )
    require(
        received_log.get("message") == "Received upload diagnostic request",
        "upload diagnostic request log exposed domain values",
    )
    require(
        completed_log.get("message") == "Upload diagnostic completed",
        "upload diagnostic completion log exposed domain values",
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


def test_upload_lease_release(token: str) -> int:
    upload_id = DIAGNOSTIC_UPLOAD_ID
    require(upload_id is not None, "lease recovery fixture is missing")
    headers = {"Authorization": f"Bearer {token}"}
    path = f"/api/admin/uploads/{upload_id}/lease/release"
    before = query_one(
        """
        SELECT status, state_version, lease_owner, lease_expires_at, updated_at
        FROM upload_tasks WHERE id = %s
        """,
        (upload_id,),
    )
    require(before is not None, "lease recovery upload disappeared")
    original_version = int(before["state_version"])
    original_owner = str(before["lease_owner"])

    unauthenticated = fetch(path, method="POST", json_body={})
    require(unauthenticated.status_code == 401, "lease release is not admin protected")

    dry_run_request_id = f"ops-lease-dry-run-{uuid.uuid4().hex}"
    dry_run = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": dry_run_request_id},
        json_body={
            "expected_state_version": original_version,
            "expected_lease_owner": original_owner,
        },
    )
    require(dry_run.status_code == 200, f"lease dry-run failed: {dry_run.text}")
    dry_run_instance_id = require_response_context(dry_run, dry_run_request_id)
    dry_data = response_json(dry_run)["data"]
    require(dry_data["dry_run"] is True, "lease dry-run flag drifted")
    require(dry_data["eligible"] is True, "live matching lease was not eligible")
    require(dry_data["released"] is False, "lease dry-run reported a mutation")
    require(
        query_one(
            "SELECT status, state_version, lease_owner, lease_expires_at, updated_at "
            "FROM upload_tasks WHERE id = %s",
            (upload_id,),
        )
        == before,
        "lease dry-run changed the upload task",
    )
    require(
        audit_count("admin.upload.lease_release", upload_id) == 0,
        "lease dry-run wrote an audit record",
    )
    wait_for_admin_log(
        request_id=dry_run_request_id,
        instance_id=dry_run_instance_id,
        message_marker="Upload lease release successful: dry_run=true",
        upload_id=upload_id,
        lease_owner=original_owner,
        state_version=original_version,
    )

    stale_owner_request_id = f"ops-lease-stale-owner-{uuid.uuid4().hex}"
    stale_owner = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": stale_owner_request_id},
        json_body={
            "dry_run": False,
            "confirm_upload_id": upload_id,
            "expected_state_version": original_version,
            "expected_lease_owner": "terminated-owner-with-stale-observation",
            "reason": "release stale owner",
        },
    )
    require(stale_owner.status_code == 409, "owner mismatch did not return HTTP 409")
    stale_owner_instance_id = require_response_context(stale_owner, stale_owner_request_id)
    require(
        scalar(
            "SELECT state_version FROM upload_tasks WHERE id = %s",
            (upload_id,),
        )
        == original_version,
        "owner mismatch changed the upload task",
    )
    require(
        audit_count("admin.upload.lease_release", upload_id) == 0,
        "owner mismatch wrote an audit record",
    )
    wait_for_admin_log(
        request_id=stale_owner_request_id,
        instance_id=stale_owner_instance_id,
        message_marker="Upload lease release failed",
        upload_id=upload_id,
    )

    released_request_id = f"ops-lease-release-{uuid.uuid4().hex}"
    released = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": released_request_id},
        json_body={
            "dry_run": False,
            "confirm_upload_id": upload_id,
            "expected_state_version": original_version,
            "expected_lease_owner": original_owner,
            "reason": "  owning instance terminated  ",
        },
    )
    require(released.status_code == 200, f"lease release failed: {released.text}")
    released_instance_id = require_response_context(released, released_request_id)
    released_data = response_json(released)["data"]
    released_version = original_version + 1
    require(released_data["released"] is True, "lease release did not report mutation")
    require(released_data["status"] == "finalizing", "lease release reopened the upload")
    require(released_data["state_version"] == released_version, "lease version did not advance")
    require(released_data["lease_owner"] == original_owner, "lease owner was not retained")
    require(released_data["lease_expired"] is True, "released lease is not expired")

    current = query_one(
        """
        SELECT status, state_version, lease_owner,
               lease_expires_at <= NOW() AS lease_expired
        FROM upload_tasks WHERE id = %s
        """,
        (upload_id,),
    )
    require(current is not None, "released upload disappeared")
    require(current["status"] == 4, "lease release changed Finalizing status")
    require(current["state_version"] == released_version, "database version did not advance")
    require(current["lease_owner"] == original_owner, "database owner was cleared")
    require(current["lease_expired"] is True, "database lease deadline is still live")
    require(
        scalar(
            """
            SELECT COUNT(*) FROM upload_tasks
            WHERE id = %s AND status = 4 AND lease_owner = %s
              AND state_version = %s AND lease_expires_at > NOW()
            """,
            (upload_id, original_owner, original_version),
        )
        == 0,
        "the old owner can still satisfy the final commit fence",
    )

    audit = query_one(
        """
        SELECT user_id, target_id, target_name, details
        FROM operation_logs
        WHERE action = 'admin.upload.lease_release' AND target_name = %s
        """,
        (upload_id,),
    )
    require(audit is not None and audit["user_id"] is not None, "lease audit lacks operator")
    require(audit["target_id"] is None, "string upload ID was written to numeric target_id")
    require(audit["target_name"] == upload_id, "lease audit target drifted")
    require(audit["details"]["reason"] == "owning instance terminated", "lease reason drifted")
    require(
        audit["details"]["previous_state_version"] == original_version
        and audit["details"]["new_state_version"] == released_version,
        "lease audit lost CAS versions",
    )
    require(audit["details"]["request_id"] == released_request_id, "lease audit lost request ID")
    require(audit["details"]["operation"] == "admin", "lease audit operation drifted")
    wait_for_admin_log(
        request_id=released_request_id,
        instance_id=released_instance_id,
        message_marker="Upload lease release successful: dry_run=false",
        upload_id=upload_id,
        lease_owner=original_owner,
        state_version=released_version,
    )

    repeated_request_id = f"ops-lease-repeat-{uuid.uuid4().hex}"
    repeated = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": repeated_request_id},
        json_body={
            "dry_run": False,
            "confirm_upload_id": upload_id,
            "expected_state_version": released_version,
            "expected_lease_owner": original_owner,
            "reason": "repeat an already released lease",
        },
    )
    require(repeated.status_code == 409, "expired lease accepted a repeated release")
    repeated_instance_id = require_response_context(repeated, repeated_request_id)

    require(
        audit_count("admin.upload.lease_release", upload_id) == 1,
        "lease release wrote an unexpected number of audit records",
    )
    wait_for_admin_log(
        request_id=repeated_request_id,
        instance_id=repeated_instance_id,
        message_marker="Upload lease release failed",
        upload_id=upload_id,
    )
    log_pass("Lease release enforces dry-run, owner/version CAS, fencing, and atomic audit")
    return released_version


def test_upload_cleanup_rebuild(token: str, released_version: int) -> None:
    global RECOVERY_JOB_IDS
    upload_id = DIAGNOSTIC_UPLOAD_ID
    require(upload_id is not None, "cleanup recovery fixture is missing")
    headers = {"Authorization": f"Bearer {token}"}
    path = f"/api/admin/uploads/{upload_id}/cleanup/rebuild"

    with db_connection() as connection:
        terminal = connection.execute(
            """
            UPDATE upload_tasks
            SET status = 5,
                state_version = state_version + 1,
                lease_owner = NULL,
                lease_expires_at = NULL,
                finalized_at = NOW(),
                fail_reason = 'integration recovery fixture',
                updated_at = NOW()
            WHERE id = %s AND status = 4 AND state_version = %s
            RETURNING state_version, staging_backend,
                      COALESCE(staging_prefix, temp_path) AS staging_prefix
            """,
            (upload_id, released_version),
        ).fetchone()
    require(terminal is not None, "failed to make cleanup fixture terminal")
    terminal_version = int(terminal["state_version"])
    execute(
        "DELETE FROM storage_jobs WHERE dedupe_key = %s",
        (f"staging-cleanup:{upload_id}",),
    )

    dry_run_request_id = f"ops-cleanup-dry-run-{uuid.uuid4().hex}"
    dry_run = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": dry_run_request_id},
        json_body={"expected_state_version": terminal_version},
    )
    require(dry_run.status_code == 200, f"cleanup dry-run failed: {dry_run.text}")
    dry_run_instance_id = require_response_context(dry_run, dry_run_request_id)
    dry_data = response_json(dry_run)["data"]
    require(dry_data["eligible"] is True, "missing terminal cleanup was not eligible")
    require(dry_data["planned_action"] == "create", "missing cleanup plan did not select create")
    require(dry_data["rebuilt"] is False, "cleanup dry-run reported mutation")
    require(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{upload_id}",),
        )
        == 0,
        "cleanup dry-run created a job",
    )
    require(
        audit_count("admin.upload.cleanup_rebuild", upload_id) == 0,
        "cleanup dry-run wrote an audit record",
    )
    wait_for_admin_log(
        request_id=dry_run_request_id,
        instance_id=dry_run_instance_id,
        message_marker="Upload cleanup rebuild successful: dry_run=true",
        upload_id=upload_id,
        state_version=terminal_version,
    )

    created_request_id = f"ops-cleanup-create-{uuid.uuid4().hex}"
    created = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": created_request_id},
        json_body={
            "dry_run": False,
            "confirm_upload_id": upload_id,
            "expected_state_version": terminal_version,
            "reason": "  terminal upload retained staging  ",
        },
    )
    require(created.status_code == 200, f"cleanup rebuild failed: {created.text}")
    created_instance_id = require_response_context(created, created_request_id)
    created_data = response_json(created)["data"]
    require(created_data["rebuilt"] is True, "cleanup creation did not report mutation")
    require(created_data["planned_action"] == "create", "cleanup creation action drifted")
    require(created_data["job_status"] == "pending", "new cleanup is not pending")
    cleanup_job_id = int(created_data["job_id"])
    RECOVERY_JOB_IDS.append(cleanup_job_id)

    cleanup_job = query_one(
        """
        SELECT job_type, aggregate_id, dedupe_key, payload, status
        FROM storage_jobs WHERE id = %s
        """,
        (cleanup_job_id,),
    )
    require(cleanup_job is not None, "rebuilt cleanup job disappeared")
    require(cleanup_job["job_type"] == "staging_cleanup", "cleanup job type drifted")
    require(cleanup_job["aggregate_id"] == upload_id, "cleanup aggregate drifted")
    require(cleanup_job["dedupe_key"] == f"staging-cleanup:{upload_id}", "cleanup dedupe drifted")
    require(
        cleanup_job["payload"]
        == {
            "upload_id": upload_id,
            "backend": terminal["staging_backend"],
            "prefix": terminal["staging_prefix"],
        },
        "cleanup payload was not derived from the persisted staging session",
    )
    require(cleanup_job["status"] == 0, "new cleanup database status is not Pending")

    first_audit = query_one(
        """
        SELECT user_id, details FROM operation_logs
        WHERE action = 'admin.upload.cleanup_rebuild' AND target_name = %s
        ORDER BY id LIMIT 1
        """,
        (upload_id,),
    )
    require(first_audit is not None and first_audit["user_id"] is not None, "cleanup audit missing")
    require(first_audit["details"]["planned_action"] == "create", "cleanup audit action drifted")
    require(
        first_audit["details"]["reason"] == "terminal upload retained staging",
        "cleanup audit reason drifted",
    )
    require(
        first_audit["details"]["request_id"] == created_request_id,
        "cleanup create audit lost request ID",
    )
    require(first_audit["details"]["operation"] == "admin", "cleanup audit operation drifted")
    wait_for_admin_log(
        request_id=created_request_id,
        instance_id=created_instance_id,
        message_marker="Upload cleanup rebuild successful: dry_run=false",
        upload_id=upload_id,
        job_id=cleanup_job_id,
        state_version=terminal_version,
    )

    execute(
        """
        UPDATE storage_jobs
        SET status = 3, attempts = 2, last_error = 'old cleanup result',
            completed_at = NOW(), updated_at = NOW()
        WHERE id = %s
        """,
        (cleanup_job_id,),
    )
    rearmed_request_id = f"ops-cleanup-rearm-{uuid.uuid4().hex}"
    rearmed = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": rearmed_request_id},
        json_body={
            "dry_run": False,
            "confirm_upload_id": upload_id,
            "expected_state_version": terminal_version,
            "reason": "cleanup object still present",
        },
    )
    require(rearmed.status_code == 200, f"cleanup rearm failed: {rearmed.text}")
    rearmed_instance_id = require_response_context(rearmed, rearmed_request_id)
    rearmed_data = response_json(rearmed)["data"]
    require(rearmed_data["job_id"] == cleanup_job_id, "cleanup rearm replaced job history")
    require(rearmed_data["job_status"] == "pending", "rearmed cleanup is not pending")
    rearmed_job = query_one(
        """
        SELECT status, attempts, locked_by, locked_until, last_error, completed_at
        FROM storage_jobs WHERE id = %s
        """,
        (cleanup_job_id,),
    )
    require(rearmed_job is not None, "rearmed cleanup disappeared")
    require(rearmed_job["status"] == 0 and rearmed_job["attempts"] == 0, "rearm did not reset state")
    require(
        all(
            rearmed_job[field] is None
            for field in ("locked_by", "locked_until", "last_error", "completed_at")
        ),
        "rearm retained lease, error, or completion state",
    )
    require(
        audit_count("admin.upload.cleanup_rebuild", upload_id) == 2,
        "cleanup create and rearm did not each write one audit",
    )
    rearm_audit = query_one(
        """
        SELECT details FROM operation_logs
        WHERE action = 'admin.upload.cleanup_rebuild' AND target_name = %s
        ORDER BY id DESC LIMIT 1
        """,
        (upload_id,),
    )
    require(rearm_audit is not None, "cleanup rearm audit is missing")
    require(
        rearm_audit["details"]["request_id"] == rearmed_request_id,
        "cleanup rearm audit lost request ID",
    )
    require(rearm_audit["details"]["operation"] == "admin", "cleanup rearm operation drifted")
    wait_for_admin_log(
        request_id=rearmed_request_id,
        instance_id=rearmed_instance_id,
        message_marker="Upload cleanup rebuild successful: dry_run=false",
        upload_id=upload_id,
        job_id=cleanup_job_id,
        state_version=terminal_version,
    )

    execute(
        """
        UPDATE storage_jobs
        SET status = 4, attempts = max_attempts,
            last_error = 'dependency still unavailable', completed_at = NOW(), updated_at = NOW()
        WHERE id = %s
        """,
        (cleanup_job_id,),
    )
    dead_letter_dry_run_request_id = f"ops-cleanup-dead-letter-dry-run-{uuid.uuid4().hex}"
    dead_letter_dry_run = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": dead_letter_dry_run_request_id},
        json_body={"expected_state_version": terminal_version},
    )
    require(dead_letter_dry_run.status_code == 200, "DeadLetter cleanup dry-run failed")
    dead_letter_dry_run_instance_id = require_response_context(
        dead_letter_dry_run,
        dead_letter_dry_run_request_id,
    )
    dead_letter_data = response_json(dead_letter_dry_run)["data"]
    require(dead_letter_data["eligible"] is False, "DeadLetter cleanup was rebuildable")
    require(dead_letter_data["planned_action"] == "none", "DeadLetter cleanup selected an action")
    require(dead_letter_data["job_status"] == "dead_letter", "DeadLetter status drifted")
    wait_for_admin_log(
        request_id=dead_letter_dry_run_request_id,
        instance_id=dead_letter_dry_run_instance_id,
        message_marker="Upload cleanup rebuild successful: dry_run=true",
        upload_id=upload_id,
        job_id=cleanup_job_id,
        state_version=terminal_version,
    )

    dead_letter_rebuild_request_id = f"ops-cleanup-dead-letter-conflict-{uuid.uuid4().hex}"
    dead_letter_rebuild = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": dead_letter_rebuild_request_id},
        json_body={
            "dry_run": False,
            "confirm_upload_id": upload_id,
            "expected_state_version": terminal_version,
            "reason": "incorrect recovery command",
        },
    )
    require(dead_letter_rebuild.status_code == 409, "DeadLetter cleanup rebuild did not conflict")
    dead_letter_rebuild_instance_id = require_response_context(
        dead_letter_rebuild,
        dead_letter_rebuild_request_id,
    )
    require(
        audit_count("admin.upload.cleanup_rebuild", upload_id) == 2,
        "rejected DeadLetter cleanup rebuild wrote an audit",
    )
    wait_for_admin_log(
        request_id=dead_letter_rebuild_request_id,
        instance_id=dead_letter_rebuild_instance_id,
        message_marker="Upload cleanup rebuild failed",
        upload_id=upload_id,
    )
    log_pass("Cleanup rebuild creates canonical jobs, rearms Succeeded, and rejects DeadLetter")


def test_storage_reconciliation_enqueue(token: str) -> None:
    global RECOVERY_SCAN_IDS, RECOVERY_JOB_IDS
    headers = {"Authorization": f"Bearer {token}"}
    scan_id = f"ops-{uuid.uuid4().hex}"
    RECOVERY_SCAN_IDS.append(scan_id)
    path = f"/api/admin/storage-reconciliation/{scan_id}/enqueue"

    unauthenticated = fetch(path, method="POST", json_body={"scope": "contents"})
    require(unauthenticated.status_code == 401, "reconciliation enqueue is not admin protected")
    invalid_scope = fetch(
        path,
        method="POST",
        headers=headers,
        json_body={"scope": "all"},
    )
    require(invalid_scope.status_code == 400, "reconciliation accepted an arbitrary scope")
    custom_cursor = fetch(
        path,
        method="POST",
        headers=headers,
        json_body={"scope": "staging", "continuation_token": "operator-controlled"},
    )
    require(custom_cursor.status_code == 400, "reconciliation accepted a custom cursor")
    scope_limits = {
        "contents": 500,
        "users": 500,
        "staging": 1000,
        "final": 1000,
    }
    jobs_by_scope: dict[str, int] = {}
    for scope, page_size in scope_limits.items():
        dry_run_request_id = f"ops-reconcile-{scope}-dry-run-{uuid.uuid4().hex}"
        dry_run = fetch(
            path,
            method="POST",
            headers={**headers, "X-Request-Id": dry_run_request_id},
            json_body={"scope": scope},
        )
        require(dry_run.status_code == 200, f"{scope} reconciliation dry-run failed: {dry_run.text}")
        dry_run_instance_id = require_response_context(dry_run, dry_run_request_id)
        dry_data = response_json(dry_run)["data"]
        require(dry_data["eligible"] is True, f"new {scope} scan was not eligible")
        require(dry_data["enqueued"] is False, f"{scope} dry-run reported enqueue")
        require(dry_data["page_size"] == page_size, f"{scope} page size drifted")
        require(dry_data["job_id"] is None, f"{scope} dry-run returned a job ID")
        require(
            scalar(
                "SELECT COUNT(*) FROM storage_jobs "
                "WHERE job_type = 'storage_reconcile' AND aggregate_id = %s",
                (scan_id,),
            )
            == len(jobs_by_scope),
            f"{scope} dry-run created a job",
        )
        wait_for_admin_log(
            request_id=dry_run_request_id,
            instance_id=dry_run_instance_id,
            message_marker="Storage reconciliation enqueue successful: dry_run=true",
        )

        enqueue_request_id = f"ops-reconcile-{scope}-enqueue-{uuid.uuid4().hex}"
        enqueued = fetch(
            path,
            method="POST",
            headers={**headers, "X-Request-Id": enqueue_request_id},
            json_body={
                "scope": scope,
                "dry_run": False,
                "confirm_scan_id": scan_id,
                "reason": f"  verify {scope} after storage incident  ",
            },
        )
        require(enqueued.status_code == 200, f"{scope} reconciliation enqueue failed: {enqueued.text}")
        enqueue_instance_id = require_response_context(enqueued, enqueue_request_id)
        enqueued_data = response_json(enqueued)["data"]
        require(enqueued_data["enqueued"] is True, f"{scope} enqueue did not report mutation")
        require(enqueued_data["job_status"] == "pending", f"{scope} job is not Pending")
        job_id = int(enqueued_data["job_id"])
        jobs_by_scope[scope] = job_id
        RECOVERY_JOB_IDS.append(job_id)

        job = query_one(
            """
            SELECT aggregate_id, dedupe_key, payload, status
            FROM storage_jobs WHERE id = %s
            """,
            (job_id,),
        )
        require(job is not None, f"{scope} reconciliation job disappeared")
        require(job["aggregate_id"] == scan_id, f"{scope} aggregate ID drifted")
        require(job["dedupe_key"] == enqueued_data["dedupe_key"], f"{scope} dedupe key drifted")
        require(
            job["payload"]
            == {
                "scan_id": scan_id,
                "scope": scope,
                "after_id": 0,
                "continuation_token": "",
                "limit": page_size,
            },
            f"{scope} first-page payload drifted",
        )
        require(job["status"] == 0, f"{scope} database status is not Pending")

        audit = query_one(
            """
            SELECT user_id, details FROM operation_logs
            WHERE action = 'admin.storage.reconcile'
              AND target_type = 'reconciliation'
              AND target_name = %s
              AND details->>'scope' = %s
            """,
            (scan_id, scope),
        )
        require(audit is not None and audit["user_id"] is not None, f"{scope} audit is missing")
        require(audit["details"]["job_id"] == job_id, f"{scope} audit job ID drifted")
        require(audit["details"]["page_size"] == page_size, f"{scope} audit page size drifted")
        require(
            audit["details"]["reason"] == f"verify {scope} after storage incident",
            f"{scope} audit reason was not normalized",
        )
        require(
            audit["details"]["request_id"] == enqueue_request_id,
            f"{scope} audit lost request ID",
        )
        require(audit["details"]["operation"] == "admin", f"{scope} audit operation drifted")
        wait_for_admin_log(
            request_id=enqueue_request_id,
            instance_id=enqueue_instance_id,
            message_marker="Storage reconciliation enqueue successful: dry_run=false",
            job_id=job_id,
        )

    duplicate_request_id = f"ops-reconcile-duplicate-{uuid.uuid4().hex}"
    duplicate = fetch(
        path,
        method="POST",
        headers={**headers, "X-Request-Id": duplicate_request_id},
        json_body={
            "scope": "staging",
            "dry_run": False,
            "confirm_scan_id": scan_id,
            "reason": "do not reset existing history",
        },
    )
    require(duplicate.status_code == 409, "duplicate scan scope did not return HTTP 409")
    duplicate_instance_id = require_response_context(duplicate, duplicate_request_id)
    require(
        audit_count("admin.storage.reconcile", scan_id) == len(scope_limits),
        "duplicate scan scope wrote an extra audit",
    )
    require(
        scalar(
            "SELECT status FROM storage_jobs WHERE id = %s",
            (jobs_by_scope["staging"],),
        )
        == 0,
        "duplicate scan scope reset existing history",
    )
    wait_for_admin_log(
        request_id=duplicate_request_id,
        instance_id=duplicate_instance_id,
        message_marker="Storage reconciliation enqueue failed",
    )
    log_pass("All fixed reconciliation scopes enqueue bounded first pages with atomic audits")


def test_recovery_commands(token: str) -> None:
    released_version = test_upload_lease_release(token)
    test_upload_cleanup_rebuild(token, released_version)
    test_storage_reconciliation_enqueue(token)


def test_admin_rate_limit_correlation(token: str) -> None:
    log_info("Checking administrator rate-limit rejection correlation")

    user_id = access_token_subject(token)
    limit = configured_admin_rate_value("admin_rate_limit_per_minute", 30)
    window_seconds = configured_admin_rate_value("admin_rate_limit_window_seconds", 60)
    now = time.time()
    window_start = (int(now) // window_seconds) * window_seconds
    seconds_until_reset = window_start + window_seconds - now
    if seconds_until_reset < 2:
        time.sleep(seconds_until_reset + 0.05)
        now = time.time()
        window_start = (int(now) // window_seconds) * window_seconds

    rate_key = f"rate:admin:{user_id}:{window_start}"
    request_id = f"ops-admin-rate-limit-{uuid.uuid4().hex}"
    storage_job_count = int(scalar("SELECT COUNT(*) FROM storage_jobs"))
    operation_log_count = int(scalar("SELECT COUNT(*) FROM operation_logs"))

    try:
        redis_set_value(
            rate_key,
            str(limit),
            max(1, window_start + window_seconds - int(time.time())),
        )
        response = fetch(
            "/api/admin/storage-jobs",
            headers={
                "Authorization": f"Bearer {token}",
                "X-Request-Id": request_id,
            },
        )

        require(response.status_code == 429, f"admin rate limit returned {response.status_code}")
        require(
            str(response_json(response).get("code")) == "10005",
            "admin rate limit business code drifted",
        )
        expected_headers = {
            "X-RateLimit-Limit": str(limit),
            "X-RateLimit-Remaining": "0",
            "X-Request-Id": request_id,
        }
        for name, expected in expected_headers.items():
            require(
                header_value(response.headers, name) == expected,
                f"admin rate limit {name} drifted",
            )
        for name in ("X-RateLimit-Reset", "Retry-After"):
            require(header_value(response.headers, name) != "", f"admin rate limit {name} is missing")

        instance_id = require_response_context(response, request_id)
        warning = wait_for_admin_log(
            request_id=request_id,
            instance_id=instance_id,
            message_marker="Admin rate limit:",
        )
        require(warning.get("level") == "warning", "admin rate limit log level drifted")
        require(
            int(scalar("SELECT COUNT(*) FROM storage_jobs")) == storage_job_count,
            "rejected admin list request changed storage jobs",
        )
        require(
            int(scalar("SELECT COUNT(*) FROM operation_logs")) == operation_log_count,
            "rejected admin list request changed operation logs",
        )
    finally:
        redis_delete_pattern(f"rate:admin:{user_id}:*")

    log_pass("Administrator 429 preserves correlation without database side effects")


def main() -> int:
    os.environ["DISK_PROCESS_ROLE"] = "api"
    os.environ["DISK_INSTANCE_ID"] = "ops-api"
    SERVER_LOG_PATH.unlink(missing_ok=True)
    ensure_server()
    try:
        token = do_login()
        require(token is not None, "admin login failed")
        test_metrics()
        test_dead_letter_operations(token)
        test_upload_diagnostics(token)
        test_recovery_commands(token)
        test_admin_rate_limit_correlation(token)
        log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
        require(token not in log_text, "storage administration logs exposed the administrator JWT")
        return 0
    finally:
        cleanup_fixture()
        cleanup()


if __name__ == "__main__":
    raise SystemExit(main())

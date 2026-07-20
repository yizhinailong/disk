#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""
Safety-net integration tests for upload lifecycle invariants.

These tests intentionally assert DB and filesystem side effects, not only API
responses. They enforce the distributed upload recovery contract.
"""

from __future__ import annotations

import atexit
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from pathlib import Path
from urllib.parse import urlencode

EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", ".sisyphus/evidence"))
SERVER_LOG_PATH = EVIDENCE_ROOT / "safety-upload-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    assert_db_row_absent,
    assert_equal,
    assert_numeric_delta,
    assert_path_absent,
    assert_path_exists,
    assert_storage_job_succeeded,
    ensure_server,
    cleanup,
    configured_chunk_size,
    do_login,
    execute,
    fetch,
    final_blob_path,
    header_value,
    json_field,
    local_blob_path,
    log_fail,
    log_info,
    log_pass,
    log_section,
    log_step,
    md5_bytes,
    print_summary,
    query_one,
    redis_delete_pattern,
    save_evidence,
    scalar,
    sha256_bytes,
    unique_name,
    upload_temp_dir,
)

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
EVIDENCE_PREFIX = "safety-upload"

TOKEN = ""
USER_ID = 0
REPO_ROOT = Path(__file__).resolve().parents[2]


@contextmanager
def peer_api_instance(
    *,
    purpose: str = "peer",
    upload_finalize_lease_seconds: int | None = None,
    pause_after_claim_upload_id: str | None = None,
    pause_after_assembly_upload_id: str | None = None,
) -> Iterator[tuple[str, str, subprocess.Popen[bytes]]]:
    """Run a second API process that shares the primary instance's dependencies."""
    pause_targets = [
        upload_id
        for upload_id in (
            pause_after_claim_upload_id,
            pause_after_assembly_upload_id,
        )
        if upload_id is not None
    ]
    if len(pause_targets) > 1:
        raise ValueError("peer API accepts only one upload fault pause stage")

    server_bin = Path(
        os.environ.get("SERVER_BIN", REPO_ROOT / "build/linux-debug-clang/src/disk")
    ).resolve()
    if not server_bin.is_file() or not os.access(server_bin, os.X_OK):
        raise RuntimeError(f"peer API binary is unavailable: {server_bin}")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        peer_port = int(probe.getsockname()[1])

    instance_id = f"safety-upload-{purpose}-{os.getpid()}"
    peer_url = f"http://127.0.0.1:{peer_port}"
    peer_log_path = EVIDENCE_ROOT / f"safety-upload-{purpose}.log"
    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    source_config_path = Path(os.environ.get("DISK_CONFIG_FILE", REPO_ROOT / "config.json"))
    if not source_config_path.is_absolute():
        source_config_path = REPO_ROOT / source_config_path

    with tempfile.TemporaryDirectory(prefix=f"disk-upload-{purpose}-") as temp_dir_raw:
        temp_dir = Path(temp_dir_raw)
        config = json.loads(source_config_path.read_text(encoding="utf-8"))
        config["listeners"] = [{"address": "127.0.0.1", "port": peer_port}]
        disk_config = config["custom_config"]["disk"]
        disk_config["process_role"] = "api"
        disk_config["instance_id"] = instance_id
        if upload_finalize_lease_seconds is not None:
            disk_config["upload_finalize_lease_seconds"] = upload_finalize_lease_seconds
        config_path = temp_dir / "config.json"
        config_path.write_text(json.dumps(config, indent=2), encoding="utf-8")

        peer_env = os.environ.copy()
        peer_env.pop("DISK_TEST_FAULT_INJECTION", None)
        peer_env.pop("DISK_TEST_PAUSE_AFTER_FINALIZE_CLAIM_UPLOAD_ID", None)
        peer_env.pop("DISK_TEST_PAUSE_AFTER_ASSEMBLY_UPLOAD_ID", None)
        peer_env.update(
            {
                "JWT_SECRET": peer_env.get(
                    "JWT_SECRET",
                    "dev-only-jwt-secret-key-change-in-production-2024",
                ),
                "DISK_CONFIG_FILE": str(config_path),
                "DISK_LISTEN_ADDRESS": "127.0.0.1",
                "DISK_LISTEN_PORT": str(peer_port),
                "DISK_PROCESS_ROLE": "api",
                "DISK_INSTANCE_ID": instance_id,
            }
        )
        if pause_after_claim_upload_id is not None:
            peer_env.update(
                {
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_AFTER_FINALIZE_CLAIM_UPLOAD_ID": str(
                        uuid.UUID(pause_after_claim_upload_id)
                    ),
                }
            )
        if pause_after_assembly_upload_id is not None:
            peer_env.update(
                {
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_AFTER_ASSEMBLY_UPLOAD_ID": str(
                        uuid.UUID(pause_after_assembly_upload_id)
                    ),
                }
            )

        with peer_log_path.open("wb") as log_handle:
            process = subprocess.Popen(
                [str(server_bin)],
                cwd=REPO_ROOT,
                env=peer_env,
                stdout=log_handle,
                stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(
                            f"peer API exited with code {process.returncode}; see {peer_log_path}"
                        )
                    try:
                        ready = fetch(f"{peer_url}/api/health/ready", timeout=2)
                        if (
                            ready.status_code == 200
                            and json_field(ready.text, "code") == "0"
                            and json_field(ready.text, "data.instance_id") == instance_id
                        ):
                            yield peer_url, instance_id, process
                            return
                    except Exception:  # noqa: BLE001 - readiness is expected to fail during startup
                        pass
                    time.sleep(0.1)
                raise RuntimeError(f"peer API did not become ready; see {peer_log_path}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


@contextmanager
def reject_chunk_metadata_insert(upload_id: str) -> Iterator[None]:
    """Reject one upload's chunk metadata after its immutable object is written."""
    normalized_upload_id = str(uuid.UUID(upload_id))
    trigger_name = "safety_chunk_insert_fail"
    function_name = "fail_safety_chunk_insert"
    execute(f"DROP TRIGGER IF EXISTS {trigger_name} ON upload_task_chunks")
    execute(f"DROP FUNCTION IF EXISTS {function_name}()")

    try:
        execute(
            f"""
            CREATE FUNCTION {function_name}() RETURNS trigger AS $$
            BEGIN
                RAISE EXCEPTION 'intentional safety chunk metadata failure';
            END;
            $$ LANGUAGE plpgsql
            """
        )
        execute(
            f"CREATE TRIGGER {trigger_name} BEFORE INSERT ON upload_task_chunks "
            f"FOR EACH ROW WHEN (NEW.task_id = '{normalized_upload_id}') "
            f"EXECUTE FUNCTION {function_name}()"
        )
        yield
    finally:
        execute(f"DROP TRIGGER IF EXISTS {trigger_name} ON upload_task_chunks")
        execute(f"DROP FUNCTION IF EXISTS {function_name}()")


def auth_headers(token: str, content_type: str = "application/json") -> dict[str, str]:
    """Return authorization headers for a test request."""
    return {"Authorization": f"Bearer {token}", "Content-Type": content_type}


def current_user_id() -> int:
    """Return the authenticated test user's database id."""
    user_id = scalar("SELECT id FROM users WHERE username = %s OR email = %s LIMIT 1", (TEST_USER, TEST_USER))
    if user_id is None:
        log_fail(f"Could not resolve test user id for {TEST_USER}")
        print_summary()
    return int(user_id)


def user_quota() -> dict[str, int]:
    """Return current storage quota counters for the test user."""
    row = query_one(
        "SELECT storage_used, storage_reserved, storage_quota FROM users WHERE id = %s",
        (USER_ID,),
    )
    if row is None:
        log_fail(f"Could not load user quota for user_id={USER_ID}")
        print_summary()
    return {key: int(row[key]) for key in ("storage_used", "storage_reserved", "storage_quota")}


def init_upload(filename: str, payload: bytes) -> tuple[str, str]:
    """Initialize a non-dedup chunked upload and return upload_id and file hash."""
    file_hash = md5_bytes(payload)
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers(TOKEN),
        json_body={
            "filename": filename,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{filename}-init.json", resp.text)

    if json_field(resp.text, "data.instant_upload") == "true":
        log_fail(f"{filename}: expected chunked upload but got instant_upload=true")
        print(resp.text)
        print_summary()

    upload_id = json_field(resp.text, "data.upload_id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not upload_id:
        log_fail(f"{filename}: init upload failed")
        print(resp.text)
        print_summary()

    return upload_id, file_hash


def upload_single_chunk_raw(upload_id: str, payload: bytes, evidence_suffix: str = "chunk"):
    """Upload a single chunk and return its response without asserting success."""
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={md5_bytes(payload)}",
        method="POST",
        headers=auth_headers(TOKEN, "application/octet-stream"),
        data=payload,
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-{evidence_suffix}.json", resp.text)
    return resp


def upload_single_chunk(upload_id: str, payload: bytes) -> None:
    """Upload a single chunk with the payload's MD5 hash."""
    resp = upload_single_chunk_raw(upload_id, payload)
    if resp.status_code != 200 or json_field(resp.text, "data.uploaded") != "true":
        log_fail(f"{upload_id}: chunk upload failed")
        print(resp.text)
        print_summary()


def complete_upload(upload_id: str) -> str:
    """Complete an upload and return the created file id."""
    resp = complete_upload_raw(upload_id)
    file_id = json_field(resp.text, "data.file.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not file_id:
        log_fail(f"{upload_id}: complete upload failed")
        print(resp.text)
        print_summary()
    return file_id


def complete_upload_raw(upload_id: str, request_id: str | None = None):
    """Call complete upload and return the raw response without asserting success."""
    headers = auth_headers(TOKEN)
    if request_id is not None:
        headers["X-Request-Id"] = request_id
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=headers,
        json_body={"upload_id": upload_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-complete.json", resp.text)
    return resp


def wait_for_request_log(request_id: str, instance_id: str, status: int) -> str:
    """Return the exact completion log line for one failed request."""
    markers = (
        f"request_id={request_id}",
        f"instance_id={instance_id}",
        "operation=upload_complete",
        f"status={status}",
    )
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if SERVER_LOG_PATH.is_file():
            log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
            for line in log_text.splitlines():
                if all(marker in line for marker in markers):
                    return line
        time.sleep(0.05)

    log_fail(f"request completion log contains correlation tuple for {request_id}")
    print_summary()
    raise AssertionError("unreachable")


def run_expired_cleanup() -> dict[str, int]:
    """Run the deterministic admin/manual cleanup seam and return cleanup counts."""
    resp = fetch(
        "/api/admin/maintenance/cleanup/expired",
        method="POST",
        headers=auth_headers(TOKEN),
    )
    save_evidence(f"{EVIDENCE_PREFIX}-cleanup-expired.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail("deterministic expired cleanup trigger failed")
        print(resp.text)
        print_summary()
    return {
        "expired_trash_deleted": int(json_field(resp.text, "data.expired_trash_deleted") or 0),
        "expired_upload_tasks_cleaned": int(json_field(resp.text, "data.expired_upload_tasks_cleaned") or 0),
    }


def cancel_upload_raw(upload_id: str):
    """Call cancel upload and return the raw response."""
    resp = fetch(
        f"/api/file/upload/{upload_id}",
        method="DELETE",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-cancel.json", resp.text)
    return resp


def cancel_upload(upload_id: str) -> None:
    """Cancel an upload task."""
    resp = cancel_upload_raw(upload_id)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"{upload_id}: cancel upload failed")
        print(resp.text)
        print_summary()


def race_complete_cancel_expire(upload_id: str):
    """Start the three terminal contenders from one synchronization point."""
    barrier = threading.Barrier(3)

    def complete():
        barrier.wait(timeout=10)
        return complete_upload_raw(upload_id, f"terminal-race-complete-{unique_name()}")

    def cancel():
        barrier.wait(timeout=10)
        return cancel_upload_raw(upload_id)

    def expire() -> dict[str, int]:
        barrier.wait(timeout=10)
        return run_expired_cleanup()

    with ThreadPoolExecutor(max_workers=3) as executor:
        complete_future = executor.submit(complete)
        cancel_future = executor.submit(cancel)
        expire_future = executor.submit(expire)
        return complete_future.result(), cancel_future.result(), expire_future.result()


def assert_upload_task(upload_id: str, expected_status: int) -> dict[str, object]:
    """Assert upload task status and return the row."""
    row = query_one("SELECT * FROM upload_tasks WHERE id = %s", (upload_id,))
    if row is None:
        log_fail(f"upload task {upload_id} exists")
        print_summary()
    assert_equal(f"upload task {upload_id} status={expected_status}", int(row["status"]), expected_status)
    return row


def assert_chunk_row_count(upload_id: str, expected_count: int) -> None:
    """Assert the number of chunk rows tracked for an upload task."""
    count = int(scalar("SELECT COUNT(*) FROM upload_task_chunks WHERE task_id = %s", (upload_id,)) or 0)
    assert_equal(f"upload task {upload_id} chunk row count={expected_count}", count, expected_count)


def test_successful_chunked_upload_invariants() -> None:
    """Verify successful upload DB and filesystem invariants."""
    log_section("Successful Chunked Upload Invariants")
    payload = (f"safety-success-{unique_name()}".encode() + b"-payload")
    filename = f"safety_success_{unique_name()}.bin"
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    quota_after_init = user_quota()
    assert_numeric_delta("upload init reserves storage", quota_before["storage_reserved"], quota_after_init["storage_reserved"], len(payload))

    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    file_id = complete_upload(upload_id)
    assert_chunk_row_count(upload_id, 0)
    quota_after_complete = user_quota()

    task = assert_upload_task(upload_id, 1)
    assert_equal("completed task reserved_bytes equals file size", int(task["reserved_bytes"]), len(payload))
    assert_numeric_delta(
        "complete releases reserved storage",
        quota_after_init["storage_reserved"],
        quota_after_complete["storage_reserved"],
        -len(payload),
    )
    assert_numeric_delta(
        "complete increases used storage by file size",
        quota_before["storage_used"],
        quota_after_complete["storage_used"],
        len(payload),
    )

    file_row = query_one("SELECT * FROM files WHERE id = %s AND user_id = %s", (int(file_id), USER_ID))
    if file_row is None:
        log_fail("files row created after upload complete")
        print_summary()
    log_pass("files row created after upload complete")
    assert_equal("files row has expected name", file_row["name"], filename)
    assert_equal("files row has expected size", int(file_row["size"]), len(payload))

    content_row = query_one("SELECT * FROM file_contents WHERE id = %s", (file_row["content_id"],))
    if content_row is None:
        log_fail("file_contents row exists for uploaded file")
        print_summary()
    log_pass("file_contents row exists for uploaded file")
    assert_equal("file_contents md5 matches payload", content_row["hash_md5"], file_hash)
    assert_storage_job_succeeded(
        "successful upload cleanup job converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("temp upload directory cleaned after success", upload_temp_dir(upload_id))
    assert_path_absent("assembled temp artifact cleaned after success", upload_temp_dir(upload_id).parent / f"{upload_id}.tmp")
    assert_equal(
        "final blob path exists after success",
        local_blob_path(str(content_row["storage_path"])).exists(),
        True,
    )


def test_chunk_metadata_failure_retry_and_orphan_cleanup_invariants() -> None:
    """Verify retry reuse and session cleanup after chunk metadata persistence fails."""
    log_section("Chunk Object Write / Metadata Failure Recovery")

    def fail_after_object_write(upload_id: str, payload: bytes, scenario: str) -> Path:
        quota_before_failure = user_quota()
        with reject_chunk_metadata_insert(upload_id):
            response = upload_single_chunk_raw(
                upload_id,
                payload,
                f"{scenario}-chunk-db-failure",
            )

        assert_equal(f"{scenario} metadata failure returns HTTP 500", response.status_code, 500)
        assert_equal(
            f"{scenario} metadata failure returns a business error",
            json_field(response.text, "code") != "0",
            True,
        )
        chunk_path = (
            upload_temp_dir(upload_id)
            / "chunks"
            / f"0-{md5_bytes(payload)}.part"
        )
        if not chunk_path.is_file():
            log_fail(f"{scenario} immutable chunk object exists after metadata failure")
            print_summary()
        log_pass(f"{scenario} immutable chunk object exists after metadata failure")
        assert_equal(f"{scenario} immutable chunk bytes match", chunk_path.read_bytes(), payload)
        assert_chunk_row_count(upload_id, 0)
        assert_upload_task(upload_id, 0)

        quota_after_failure = user_quota()
        assert_equal(
            f"{scenario} metadata failure preserves reserved storage",
            quota_after_failure["storage_reserved"],
            quota_before_failure["storage_reserved"],
        )
        assert_equal(
            f"{scenario} metadata failure preserves used storage",
            quota_after_failure["storage_used"],
            quota_before_failure["storage_used"],
        )
        return chunk_path

    retry_payload = f"safety-chunk-db-retry-{unique_name()}".encode()
    retry_filename = f"safety_chunk_db_retry_{unique_name()}.bin"
    retry_quota_before = user_quota()
    retry_upload_id, retry_md5 = init_upload(retry_filename, retry_payload)
    retry_quota_after_init = user_quota()
    assert_numeric_delta(
        "retry fixture reserves storage once",
        retry_quota_before["storage_reserved"],
        retry_quota_after_init["storage_reserved"],
        len(retry_payload),
    )
    retry_chunk_path = fail_after_object_write(
        retry_upload_id,
        retry_payload,
        "retry",
    )
    original_stat = retry_chunk_path.stat()
    time.sleep(0.01)
    upload_single_chunk(retry_upload_id, retry_payload)
    reused_stat = retry_chunk_path.stat()
    assert_equal("retry reuses the immutable chunk inode", reused_stat.st_ino, original_stat.st_ino)
    assert_equal(
        "retry does not rewrite the immutable chunk object",
        reused_stat.st_mtime_ns,
        original_stat.st_mtime_ns,
    )
    assert_chunk_row_count(retry_upload_id, 1)

    retry_file_id = int(complete_upload(retry_upload_id))
    assert_upload_task(retry_upload_id, 1)
    assert_chunk_row_count(retry_upload_id, 0)
    retry_quota_after_complete = user_quota()
    assert_equal(
        "retry completion releases reserved storage once",
        retry_quota_after_complete["storage_reserved"],
        retry_quota_before["storage_reserved"],
    )
    assert_numeric_delta(
        "retry completion increases used storage once",
        retry_quota_before["storage_used"],
        retry_quota_after_complete["storage_used"],
        len(retry_payload),
    )

    retry_file_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
            (USER_ID, retry_filename),
        )
        or 0
    )
    assert_equal("retry completion creates one file row", retry_file_count, 1)
    retry_file = query_one(
        "SELECT file.id, content.ref_count FROM files AS file "
        "JOIN file_contents AS content ON content.id = file.content_id "
        "WHERE file.user_id = %s AND file.name = %s",
        (USER_ID, retry_filename),
    )
    if retry_file is None:
        log_fail("retry completion creates its file/content reference")
        print_summary()
    log_pass("retry completion creates its file/content reference")
    assert_equal("retry completion returns the persisted file", int(retry_file["id"]), retry_file_id)
    assert_equal("retry completion increments ref_count once", int(retry_file["ref_count"]), 1)
    retry_content_count = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (retry_md5, sha256_bytes(retry_payload)),
        )
        or 0
    )
    assert_equal("retry completion creates one content row", retry_content_count, 1)
    retry_cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{retry_upload_id}",),
        )
        or 0
    )
    assert_equal("retry completion creates one cleanup job", retry_cleanup_count, 1)
    assert_storage_job_succeeded(
        "retry completion cleanup converges",
        f"staging-cleanup:{retry_upload_id}",
    )
    assert_path_absent("retry completion removes the staging session", upload_temp_dir(retry_upload_id))
    assert_path_exists(
        "retry completion preserves one final blob",
        final_blob_path(sha256_bytes(retry_payload)),
    )

    orphan_payload = f"safety-chunk-db-orphan-{unique_name()}".encode()
    orphan_filename = f"safety_chunk_db_orphan_{unique_name()}.bin"
    orphan_quota_before = user_quota()
    orphan_upload_id, orphan_md5 = init_upload(orphan_filename, orphan_payload)
    orphan_quota_after_init = user_quota()
    assert_numeric_delta(
        "orphan fixture reserves storage once",
        orphan_quota_before["storage_reserved"],
        orphan_quota_after_init["storage_reserved"],
        len(orphan_payload),
    )
    orphan_chunk_path = fail_after_object_write(
        orphan_upload_id,
        orphan_payload,
        "orphan",
    )
    cancel_upload(orphan_upload_id)
    assert_upload_task(orphan_upload_id, 2)
    assert_chunk_row_count(orphan_upload_id, 0)

    orphan_quota_after_cancel = user_quota()
    assert_equal(
        "orphan cancellation releases reserved storage once",
        orphan_quota_after_cancel["storage_reserved"],
        orphan_quota_before["storage_reserved"],
    )
    assert_equal(
        "orphan cancellation preserves used storage",
        orphan_quota_after_cancel["storage_used"],
        orphan_quota_before["storage_used"],
    )
    assert_db_row_absent(
        "orphan cancellation creates no file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, orphan_filename),
    )
    orphan_content_count = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (orphan_md5, sha256_bytes(orphan_payload)),
        )
        or 0
    )
    assert_equal("orphan cancellation creates no content row", orphan_content_count, 0)
    orphan_cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{orphan_upload_id}",),
        )
        or 0
    )
    assert_equal("orphan cancellation creates one cleanup job", orphan_cleanup_count, 1)
    assert_storage_job_succeeded(
        "orphan cancellation cleanup converges",
        f"staging-cleanup:{orphan_upload_id}",
    )
    assert_path_absent("orphan cleanup removes the unregistered chunk", orphan_chunk_path)
    assert_path_absent("orphan cleanup removes the staging session", upload_temp_dir(orphan_upload_id))
    assert_path_absent(
        "orphan cancellation creates no final blob",
        final_blob_path(sha256_bytes(orphan_payload)),
    )

    save_evidence(
        f"{EVIDENCE_PREFIX}-chunk-metadata-failure-recovery.json",
        json.dumps(
            {
                "retry": {
                    "upload_id": retry_upload_id,
                    "status": "completed",
                    "file_id": retry_file_id,
                },
                "orphan": {
                    "upload_id": orphan_upload_id,
                    "status": "cancelled",
                },
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )


def test_hundred_concurrent_complete_invariants() -> None:
    """Verify 100 complete calls across two processes settle all effects once."""
    log_section("100 Concurrent Complete Requests Across API Instances")

    try:
        with peer_api_instance() as (peer_url, peer_instance_id, _):
            redis_delete_pattern(f"rate:upload:{USER_ID}:*")
            primary_ready = fetch(f"{BASE_URL}/api/health/ready")
            primary_instance_id = json_field(primary_ready.text, "data.instance_id")
            if primary_ready.status_code != 200 or not primary_instance_id:
                log_fail("primary API readiness exposes an instance ID")
                print_summary()
            log_pass("primary API readiness exposes an instance ID")
            assert_equal(
                "concurrent complete uses two distinct API instances",
                primary_instance_id != peer_instance_id,
                True,
            )

            payload = f"safety-hundred-complete-{unique_name()}".encode()
            filename = f"safety_hundred_complete_{unique_name()}.bin"
            quota_before = user_quota()
            upload_id, file_hash = init_upload(filename, payload)
            upload_single_chunk(upload_id, payload)
            quota_after_init = user_quota()
            assert_numeric_delta(
                "100 complete fixture reserves storage once",
                quota_before["storage_reserved"],
                quota_after_init["storage_reserved"],
                len(payload),
            )

            request_count = 100
            barrier = threading.Barrier(request_count)

            def invoke_complete(index: int):
                target_url = BASE_URL if index % 2 == 0 else peer_url
                expected_instance = primary_instance_id if index % 2 == 0 else peer_instance_id
                barrier.wait(timeout=30)
                response = fetch(
                    f"{target_url}/api/file/upload/complete",
                    method="POST",
                    headers={
                        **auth_headers(TOKEN),
                        "X-Request-Id": f"hundred-complete-{os.getpid()}-{index}",
                    },
                    json_body={"upload_id": upload_id},
                    timeout=180,
                )
                return index, target_url, expected_instance, response

            with ThreadPoolExecutor(max_workers=request_count) as executor:
                results = list(executor.map(invoke_complete, range(request_count)))

            instance_mismatches: list[int] = []
            unexpected_results: list[dict[str, object]] = []
            completed_ids: list[int] = []
            conflicts: list[tuple[int, str]] = []
            for index, target_url, expected_instance, response in results:
                response_instance = header_value(response.headers, "X-Disk-Instance-Id")
                if response_instance != expected_instance:
                    instance_mismatches.append(index)

                code = json_field(response.text, "code")
                response_file_id = json_field(response.text, "data.file.id")
                if response.status_code == 200 and code == "0" and response_file_id:
                    completed_ids.append(int(response_file_id))
                elif response.status_code == 409 and code == "10004":
                    conflicts.append((index, target_url))
                else:
                    unexpected_results.append(
                        {
                            "index": index,
                            "http_status": response.status_code,
                            "code": code,
                        }
                    )

            if instance_mismatches:
                log_fail(
                    f"100 complete requests stayed on their selected instances: {instance_mismatches}"
                )
                print_summary()
            log_pass("100 complete requests stayed on their selected instances")

            if unexpected_results:
                log_fail(f"100 complete initial responses follow the retry contract: {unexpected_results}")
                print_summary()
            log_pass("100 complete initial responses are success or documented conflict")

            if not completed_ids:
                log_fail("100 concurrent complete requests produce a winner")
                print_summary()
            log_pass("100 concurrent complete requests produce a winner")

            replay_failures: list[dict[str, object]] = []
            for index, target_url in conflicts:
                replay = fetch(
                    f"{target_url}/api/file/upload/complete",
                    method="POST",
                    headers=auth_headers(TOKEN),
                    json_body={"upload_id": upload_id},
                    timeout=30,
                )
                replay_id = json_field(replay.text, "data.file.id")
                if replay.status_code == 200 and json_field(replay.text, "code") == "0" and replay_id:
                    completed_ids.append(int(replay_id))
                else:
                    replay_failures.append(
                        {
                            "index": index,
                            "http_status": replay.status_code,
                            "code": json_field(replay.text, "code"),
                        }
                    )

            if replay_failures:
                log_fail(f"conflicting complete requests converge on replay: {replay_failures}")
                print_summary()
            log_pass("conflicting complete requests converge on replay")
            assert_equal("all 100 complete calls return a file after replay", len(completed_ids), request_count)
            assert_equal("all 100 complete calls return one file ID", len(set(completed_ids)), 1)
            completed_file_id = completed_ids[0]

            task = query_one(
                "SELECT status, completed_file_id FROM upload_tasks "
                "WHERE id = %s AND user_id = %s",
                (upload_id, USER_ID),
            )
            if task is None:
                log_fail("100 complete fixture preserves its upload task")
                print_summary()
            assert_equal("100 complete fixture reaches Completed", int(task["status"]), 1)
            assert_equal(
                "100 complete fixture records the converged file ID",
                int(task["completed_file_id"]),
                completed_file_id,
            )

            file_count = int(
                scalar(
                    "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
                    (USER_ID, filename),
                )
                or 0
            )
            assert_equal("100 complete calls create one file row", file_count, 1)
            file_row = query_one(
                "SELECT file.id, content.ref_count FROM files AS file "
                "JOIN file_contents AS content ON content.id = file.content_id "
                "WHERE file.id = %s AND file.user_id = %s",
                (completed_file_id, USER_ID),
            )
            if file_row is None:
                log_fail("100 complete calls create the converged file/content row")
                print_summary()
            assert_equal(
                "100 complete calls return the persisted file",
                int(file_row["id"]),
                completed_file_id,
            )
            content_count = int(
                scalar(
                    "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
                    (file_hash, sha256_bytes(payload)),
                )
                or 0
            )
            assert_equal("100 complete calls create one content row", content_count, 1)
            assert_equal("100 complete calls increment ref_count once", int(file_row["ref_count"]), 1)

            quota_after_complete = user_quota()
            assert_equal(
                "100 complete calls release reserved storage once",
                quota_after_complete["storage_reserved"],
                quota_before["storage_reserved"],
            )
            assert_numeric_delta(
                "100 complete calls increase used storage once",
                quota_before["storage_used"],
                quota_after_complete["storage_used"],
                len(payload),
            )
            assert_chunk_row_count(upload_id, 0)
            cleanup_job_count = int(
                scalar(
                    "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
                    (f"staging-cleanup:{upload_id}",),
                )
                or 0
            )
            assert_equal("100 complete calls create one cleanup job", cleanup_job_count, 1)
            assert_storage_job_succeeded(
                "100 complete cleanup converges",
                f"staging-cleanup:{upload_id}",
            )
            assert_path_exists(
                "100 complete calls preserve one final blob",
                final_blob_path(sha256_bytes(payload)),
            )
            save_evidence(
                f"{EVIDENCE_PREFIX}-{upload_id}-hundred-complete.json",
                json.dumps(
                    {
                        "upload_id": upload_id,
                        "primary_instance_id": primary_instance_id,
                        "peer_instance_id": peer_instance_id,
                        "request_count": request_count,
                        "initial_success_count": request_count - len(conflicts),
                        "initial_conflict_count": len(conflicts),
                        "completed_file_id": completed_file_id,
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
            )
    except Exception as error:
        log_fail(f"100 complete dual-process fixture failed: {error}")
        print_summary()
    finally:
        redis_delete_pattern(f"rate:upload:{USER_ID}:*")


def test_finalize_claim_process_death_takeover_invariants() -> None:
    """Kill a real API after its committed claim and verify lease-based takeover."""
    log_section("Finalize Claim Process Death And Lease Takeover")
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")

    payload = f"safety-finalize-claim-death-{unique_name()}".encode()
    filename = f"safety_finalize_claim_death_{unique_name()}.bin"
    payload_md5 = md5_bytes(payload)
    payload_sha256 = sha256_bytes(payload)
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    assembled_path = upload_temp_dir(upload_id).parent / f"{upload_id}.tmp"
    blob_path = final_blob_path(payload_sha256)
    assert_equal("claim-death fixture MD5 matches init contract", file_hash, payload_md5)
    assert_path_absent("claim-death fixture starts without an assembled object", assembled_path)
    assert_path_absent("claim-death fixture starts without a final blob", blob_path)

    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    quota_after_init = user_quota()
    assert_numeric_delta(
        "claim-death fixture reserves storage once",
        quota_before["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )

    primary_ready = fetch(f"{BASE_URL}/api/health/ready")
    primary_instance_id = json_field(primary_ready.text, "data.instance_id")
    if primary_ready.status_code != 200 or not primary_instance_id:
        log_fail("claim-death fixture resolves the primary API instance")
        print_summary()
    log_pass("claim-death fixture resolves the primary API instance")

    killed_pid = 0
    killed_instance_id = ""
    killed_state_version = 0
    killed_lease_expires_at = None
    dropped_request_error = ""

    try:
        with peer_api_instance(
            purpose="claim-crash",
            upload_finalize_lease_seconds=30,
            pause_after_claim_upload_id=upload_id,
        ) as (crash_url, crash_instance_id, crash_process):
            assert_equal(
                "claim-death fixture uses two distinct API instances",
                crash_instance_id != primary_instance_id,
                True,
            )
            killed_pid = crash_process.pid
            killed_instance_id = crash_instance_id

            with ThreadPoolExecutor(max_workers=1) as executor:
                complete_future = executor.submit(
                    fetch,
                    f"{crash_url}/api/file/upload/complete",
                    method="POST",
                    headers={
                        **auth_headers(TOKEN),
                        "X-Request-Id": f"claim-death-{upload_id}",
                    },
                    json_body={"upload_id": upload_id},
                    timeout=120,
                )

                claimed_task = None
                claim_deadline = time.monotonic() + 10
                while time.monotonic() < claim_deadline:
                    candidate = query_one(
                        "SELECT status, lease_owner, lease_expires_at, state_version, "
                        "finalize_attempts, lease_expires_at > NOW() AS lease_live "
                        "FROM upload_tasks WHERE id = %s AND user_id = %s",
                        (upload_id, USER_ID),
                    )
                    if (
                        candidate is not None
                        and int(candidate["status"]) == 4
                        and candidate["lease_owner"] == crash_instance_id
                        and bool(candidate["lease_live"])
                    ):
                        claimed_task = candidate
                        break
                    time.sleep(0.05)

                if claimed_task is None:
                    log_fail("crash API commits its Finalizing claim before assembly")
                    print_summary()
                log_pass("crash API commits its Finalizing claim before assembly")
                killed_state_version = int(claimed_task["state_version"])
                killed_lease_expires_at = claimed_task["lease_expires_at"]
                assert_equal(
                    "first Finalizing claim increments finalize_attempts once",
                    int(claimed_task["finalize_attempts"]),
                    1,
                )

                quota_after_claim = user_quota()
                assert_equal(
                    "claim pause preserves reserved storage",
                    quota_after_claim["storage_reserved"],
                    quota_after_init["storage_reserved"],
                )
                assert_equal(
                    "claim pause preserves used storage",
                    quota_after_claim["storage_used"],
                    quota_after_init["storage_used"],
                )
                assert_chunk_row_count(upload_id, 1)
                assert_path_absent("claim pause creates no assembled object", assembled_path)
                assert_path_absent("claim pause creates no final blob", blob_path)
                assert_db_row_absent(
                    "claim pause creates no logical file",
                    "SELECT id FROM files WHERE user_id = %s AND name = %s",
                    (USER_ID, filename),
                )
                assert_db_row_absent(
                    "claim pause creates no content row",
                    "SELECT id FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
                    (payload_md5, payload_sha256),
                )

                assert_equal("crash API is alive at the injected pause", crash_process.poll(), None)
                crash_process.kill()
                crash_process.wait(timeout=5)
                assert_equal(
                    "crash API is terminated by a non-zero signal exit",
                    crash_process.returncode != 0,
                    True,
                )

                try:
                    crash_response = complete_future.result(timeout=10)
                except Exception as error:  # noqa: BLE001 - process death intentionally breaks HTTP
                    dropped_request_error = type(error).__name__
                    log_pass("killed API does not return a successful complete response")
                else:
                    log_fail(
                        "killed API unexpectedly returned a complete response: "
                        f"HTTP {crash_response.status_code}, body={crash_response.text}"
                    )
                    print_summary()
    except Exception as error:
        log_fail(f"claim-death process fixture failed: {error}")
        print_summary()

    task_after_kill = query_one(
        "SELECT status, lease_owner, lease_expires_at, state_version, finalize_attempts, "
        "lease_expires_at > NOW() AS lease_live "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_kill is None:
        log_fail("claim-death task remains durable after the API process dies")
        print_summary()
    log_pass("claim-death task remains durable after the API process dies")
    assert_equal("killed claim remains Finalizing", int(task_after_kill["status"]), 4)
    assert_equal("killed claim retains its owner", task_after_kill["lease_owner"], killed_instance_id)
    assert_equal(
        "process death does not mutate the claim version",
        int(task_after_kill["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "process death does not add a finalize attempt",
        int(task_after_kill["finalize_attempts"]),
        1,
    )
    assert_equal("killed claim lease is still live", bool(task_after_kill["lease_live"]), True)
    assert_path_absent("process death leaves no assembled object", assembled_path)
    assert_path_absent("process death leaves no final blob", blob_path)

    conflict_response = complete_upload_raw(
        upload_id,
        f"claim-death-live-lease-{upload_id}",
    )
    assert_equal("live lease rejects takeover with HTTP 409", conflict_response.status_code, 409)
    assert_equal(
        "live lease rejects takeover with UploadStateConflict",
        json_field(conflict_response.text, "code"),
        "10004",
    )

    task_after_conflict = query_one(
        "SELECT status, lease_owner, state_version, finalize_attempts, "
        "lease_expires_at > NOW() AS lease_live "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_conflict is None:
        log_fail("live-lease conflict preserves the upload task")
        print_summary()
    assert_equal("live-lease conflict preserves Finalizing", int(task_after_conflict["status"]), 4)
    assert_equal(
        "live-lease conflict preserves the killed owner",
        task_after_conflict["lease_owner"],
        killed_instance_id,
    )
    assert_equal(
        "live-lease conflict preserves the claim version",
        int(task_after_conflict["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "live-lease conflict does not add an attempt",
        int(task_after_conflict["finalize_attempts"]),
        1,
    )
    assert_equal("live-lease conflict occurs before expiry", bool(task_after_conflict["lease_live"]), True)

    expired_task = None
    expiry_deadline = time.monotonic() + 40
    while time.monotonic() < expiry_deadline:
        candidate = query_one(
            "SELECT status, lease_owner, state_version, finalize_attempts, "
            "lease_expires_at <= NOW() AS lease_expired "
            "FROM upload_tasks WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if candidate is not None and bool(candidate["lease_expired"]):
            expired_task = candidate
            break
        time.sleep(0.1)

    if expired_task is None:
        log_fail("killed claim expires according to PostgreSQL time")
        print_summary()
    log_pass("killed claim expires according to PostgreSQL time without a DB rewrite")
    assert_equal("expired claim remains Finalizing before takeover", int(expired_task["status"]), 4)
    assert_equal("expired claim retains the killed owner", expired_task["lease_owner"], killed_instance_id)
    assert_equal(
        "natural expiry preserves the claim version",
        int(expired_task["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "natural expiry preserves one finalize attempt",
        int(expired_task["finalize_attempts"]),
        1,
    )

    takeover_response = complete_upload_raw(
        upload_id,
        f"claim-death-takeover-{upload_id}",
    )
    takeover_file_id = json_field(takeover_response.text, "data.file.id")
    if (
        takeover_response.status_code != 200
        or json_field(takeover_response.text, "code") != "0"
        or not takeover_file_id
    ):
        log_fail("primary API completes the upload after natural lease expiry")
        print(takeover_response.text)
        print_summary()
    log_pass("primary API completes the upload after natural lease expiry")
    assert_equal(
        "takeover response comes from the primary API",
        header_value(takeover_response.headers, "X-Disk-Instance-Id"),
        primary_instance_id,
    )
    completed_file_id = int(takeover_file_id)

    completed_task = query_one(
        "SELECT status, completed_file_id, lease_owner, lease_expires_at, "
        "state_version, finalize_attempts "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if completed_task is None:
        log_fail("takeover preserves the completed upload task")
        print_summary()
    assert_equal("takeover reaches Completed", int(completed_task["status"]), 1)
    assert_equal(
        "takeover records the returned file ID",
        int(completed_task["completed_file_id"]),
        completed_file_id,
    )
    assert_equal("takeover clears lease_owner", completed_task["lease_owner"], None)
    assert_equal("takeover clears lease_expires_at", completed_task["lease_expires_at"], None)
    assert_equal(
        "takeover advances the killed claim version",
        int(completed_task["state_version"]) > killed_state_version,
        True,
    )
    assert_equal(
        "claim and takeover produce exactly two finalize attempts",
        int(completed_task["finalize_attempts"]),
        2,
    )

    assert_chunk_row_count(upload_id, 0)
    completed_quota = user_quota()
    assert_equal(
        "takeover releases reserved storage once",
        completed_quota["storage_reserved"],
        quota_before["storage_reserved"],
    )
    assert_numeric_delta(
        "takeover increases used storage once",
        quota_before["storage_used"],
        completed_quota["storage_used"],
        len(payload),
    )

    file_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
            (USER_ID, filename),
        )
        or 0
    )
    assert_equal("claim death and takeover create one file row", file_count, 1)
    file_row = query_one(
        "SELECT file.id, content.ref_count, content.hash_md5, content.hash_sha256 "
        "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
        "WHERE file.id = %s AND file.user_id = %s",
        (completed_file_id, USER_ID),
    )
    if file_row is None:
        log_fail("claim death and takeover create one file/content reference")
        print_summary()
    assert_equal("takeover returns the persisted file", int(file_row["id"]), completed_file_id)
    assert_equal("takeover increments content ref_count once", int(file_row["ref_count"]), 1)
    assert_equal("takeover content MD5 matches", file_row["hash_md5"], payload_md5)
    assert_equal("takeover content SHA-256 matches", file_row["hash_sha256"], payload_sha256)
    content_count = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (payload_md5, payload_sha256),
        )
        or 0
    )
    assert_equal("claim death and takeover create one content row", content_count, 1)

    cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{upload_id}",),
        )
        or 0
    )
    assert_equal("claim death and takeover create one cleanup job", cleanup_count, 1)
    assert_storage_job_succeeded(
        "claim-death takeover cleanup converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("takeover removes the staging session", upload_temp_dir(upload_id))
    assert_path_absent("takeover leaves no assembled object", assembled_path)
    assert_path_exists("takeover preserves one final blob", blob_path)

    save_evidence(
        f"{EVIDENCE_PREFIX}-{upload_id}-claim-death-takeover.json",
        json.dumps(
            {
                "upload_id": upload_id,
                "killed_pid": killed_pid,
                "killed_instance_id": killed_instance_id,
                "killed_state_version": killed_state_version,
                "killed_lease_expires_at": killed_lease_expires_at.isoformat()
                if killed_lease_expires_at is not None
                else None,
                "dropped_request_error": dropped_request_error,
                "live_lease_conflict_code": json_field(conflict_response.text, "code"),
                "takeover_instance_id": primary_instance_id,
                "final_state_version": int(completed_task["state_version"]),
                "finalize_attempts": int(completed_task["finalize_attempts"]),
                "completed_file_id": completed_file_id,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")


def test_assembled_object_process_death_takeover_invariants() -> None:
    """Kill an API after assembly and verify a new owner safely rebuilds it."""
    log_section("Assembled Object Process Death And Lease Takeover")
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")

    payload = f"safety-assembled-death-{unique_name()}".encode()
    filename = f"safety_assembled_death_{unique_name()}.bin"
    payload_md5 = md5_bytes(payload)
    payload_sha256 = sha256_bytes(payload)
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    assembled_path = upload_temp_dir(upload_id).parent / f"{upload_id}.tmp"
    blob_path = final_blob_path(payload_sha256)
    assert_equal("assembly-death fixture MD5 matches init contract", file_hash, payload_md5)
    assert_path_absent("assembly-death fixture starts without an assembled object", assembled_path)
    assert_path_absent("assembly-death fixture starts without a final blob", blob_path)

    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    quota_after_init = user_quota()
    assert_numeric_delta(
        "assembly-death fixture reserves storage once",
        quota_before["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )

    primary_ready = fetch(f"{BASE_URL}/api/health/ready")
    primary_instance_id = json_field(primary_ready.text, "data.instance_id")
    if primary_ready.status_code != 200 or not primary_instance_id:
        log_fail("assembly-death fixture resolves the primary API instance")
        print_summary()
    log_pass("assembly-death fixture resolves the primary API instance")

    killed_pid = 0
    killed_instance_id = ""
    killed_state_version = 0
    killed_lease_expires_at = None
    assembled_inode = 0
    assembled_mtime_ns = 0
    dropped_request_error = ""
    crash_log_path = EVIDENCE_ROOT / "safety-upload-assembly-crash.log"

    try:
        with peer_api_instance(
            purpose="assembly-crash",
            upload_finalize_lease_seconds=30,
            pause_after_assembly_upload_id=upload_id,
        ) as (crash_url, crash_instance_id, crash_process):
            assert_equal(
                "assembly-death fixture uses two distinct API instances",
                crash_instance_id != primary_instance_id,
                True,
            )
            killed_pid = crash_process.pid
            killed_instance_id = crash_instance_id

            with ThreadPoolExecutor(max_workers=1) as executor:
                complete_future = executor.submit(
                    fetch,
                    f"{crash_url}/api/file/upload/complete",
                    method="POST",
                    headers={
                        **auth_headers(TOKEN),
                        "X-Request-Id": f"assembly-death-{upload_id}",
                    },
                    json_body={"upload_id": upload_id},
                    timeout=120,
                )

                paused_task = None
                pause_marker = (
                    "Test fault injection paused upload after assembly: "
                    f"upload_id={upload_id}"
                )
                pause_deadline = time.monotonic() + 10
                while time.monotonic() < pause_deadline:
                    candidate = query_one(
                        "SELECT status, lease_owner, lease_expires_at, state_version, "
                        "finalize_attempts, lease_expires_at > NOW() AS lease_live "
                        "FROM upload_tasks WHERE id = %s AND user_id = %s",
                        (upload_id, USER_ID),
                    )
                    log_text = (
                        crash_log_path.read_text(encoding="utf-8", errors="replace")
                        if crash_log_path.is_file()
                        else ""
                    )
                    if (
                        pause_marker in log_text
                        and candidate is not None
                        and int(candidate["status"]) == 4
                        and candidate["lease_owner"] == crash_instance_id
                        and bool(candidate["lease_live"])
                        and assembled_path.is_file()
                        and assembled_path.stat().st_size == len(payload)
                    ):
                        paused_task = candidate
                        break
                    time.sleep(0.05)

                if paused_task is None:
                    log_fail("crash API pauses after a complete assembled object is durable")
                    print_summary()
                log_pass("crash API pauses after a complete assembled object is durable")
                killed_state_version = int(paused_task["state_version"])
                killed_lease_expires_at = paused_task["lease_expires_at"]
                assembled_stat = assembled_path.stat()
                assembled_inode = assembled_stat.st_ino
                assembled_mtime_ns = assembled_stat.st_mtime_ns
                assert_equal(
                    "assembly pause retains one finalize attempt",
                    int(paused_task["finalize_attempts"]),
                    1,
                )
                assert_equal("assembled object contains the complete payload", assembled_path.read_bytes(), payload)

                quota_after_assembly = user_quota()
                assert_equal(
                    "assembly pause preserves reserved storage",
                    quota_after_assembly["storage_reserved"],
                    quota_after_init["storage_reserved"],
                )
                assert_equal(
                    "assembly pause preserves used storage",
                    quota_after_assembly["storage_used"],
                    quota_after_init["storage_used"],
                )
                assert_chunk_row_count(upload_id, 1)
                assert_path_absent("assembly pause creates no final blob", blob_path)
                assert_db_row_absent(
                    "assembly pause creates no logical file",
                    "SELECT id FROM files WHERE user_id = %s AND name = %s",
                    (USER_ID, filename),
                )
                assert_db_row_absent(
                    "assembly pause creates no content row",
                    "SELECT id FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
                    (payload_md5, payload_sha256),
                )

                assert_equal("assembly owner is alive at the injected pause", crash_process.poll(), None)
                crash_process.kill()
                crash_process.wait(timeout=5)
                assert_equal(
                    "assembly owner is terminated by a non-zero signal exit",
                    crash_process.returncode != 0,
                    True,
                )

                try:
                    crash_response = complete_future.result(timeout=10)
                except Exception as error:  # noqa: BLE001 - process death intentionally breaks HTTP
                    dropped_request_error = type(error).__name__
                    log_pass("killed assembly owner returns no successful complete response")
                else:
                    log_fail(
                        "killed assembly owner unexpectedly returned a response: "
                        f"HTTP {crash_response.status_code}, body={crash_response.text}"
                    )
                    print_summary()
    except Exception as error:
        log_fail(f"assembly-death process fixture failed: {error}")
        print_summary()

    task_after_kill = query_one(
        "SELECT status, lease_owner, lease_expires_at, state_version, finalize_attempts, "
        "lease_expires_at > NOW() AS lease_live "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_kill is None:
        log_fail("assembly-death task remains durable after the API process dies")
        print_summary()
    log_pass("assembly-death task remains durable after the API process dies")
    assert_equal("killed assembly task remains Finalizing", int(task_after_kill["status"]), 4)
    assert_equal(
        "killed assembly task retains its owner",
        task_after_kill["lease_owner"],
        killed_instance_id,
    )
    assert_equal(
        "assembly owner death preserves the claim version",
        int(task_after_kill["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "assembly owner death preserves one finalize attempt",
        int(task_after_kill["finalize_attempts"]),
        1,
    )
    assert_equal("killed assembly lease is still live", bool(task_after_kill["lease_live"]), True)
    assert_path_exists("process death preserves the assembled object", assembled_path)
    assert_equal("preserved assembled object retains its inode", assembled_path.stat().st_ino, assembled_inode)
    assert_equal(
        "preserved assembled object is not rewritten before takeover",
        assembled_path.stat().st_mtime_ns,
        assembled_mtime_ns,
    )
    assert_equal("preserved assembled bytes remain complete", assembled_path.read_bytes(), payload)
    assert_path_absent("assembly owner death leaves no final blob", blob_path)

    conflict_response = complete_upload_raw(
        upload_id,
        f"assembly-death-live-lease-{upload_id}",
    )
    assert_equal("live assembly lease rejects takeover with HTTP 409", conflict_response.status_code, 409)
    assert_equal(
        "live assembly lease rejects takeover with UploadStateConflict",
        json_field(conflict_response.text, "code"),
        "10004",
    )
    task_after_conflict = query_one(
        "SELECT status, lease_owner, state_version, finalize_attempts, "
        "lease_expires_at > NOW() AS lease_live "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_conflict is None:
        log_fail("live assembly lease conflict preserves the task")
        print_summary()
    assert_equal("live assembly conflict preserves Finalizing", int(task_after_conflict["status"]), 4)
    assert_equal(
        "live assembly conflict preserves the killed owner",
        task_after_conflict["lease_owner"],
        killed_instance_id,
    )
    assert_equal(
        "live assembly conflict preserves the claim version",
        int(task_after_conflict["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "live assembly conflict does not add an attempt",
        int(task_after_conflict["finalize_attempts"]),
        1,
    )
    assert_equal(
        "live assembly conflict occurs before lease expiry",
        bool(task_after_conflict["lease_live"]),
        True,
    )
    assert_equal("live-lease conflict leaves assembled bytes unchanged", assembled_path.read_bytes(), payload)

    expired_task = None
    expiry_deadline = time.monotonic() + 40
    while time.monotonic() < expiry_deadline:
        candidate = query_one(
            "SELECT status, lease_owner, state_version, finalize_attempts, "
            "lease_expires_at <= NOW() AS lease_expired "
            "FROM upload_tasks WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if candidate is not None and bool(candidate["lease_expired"]):
            expired_task = candidate
            break
        time.sleep(0.1)

    if expired_task is None:
        log_fail("killed assembly lease expires according to PostgreSQL time")
        print_summary()
    log_pass("killed assembly lease expires according to PostgreSQL time without a DB rewrite")
    assert_equal("expired assembly task remains Finalizing", int(expired_task["status"]), 4)
    assert_equal("expired assembly task retains the killed owner", expired_task["lease_owner"], killed_instance_id)
    assert_equal(
        "natural assembly lease expiry preserves the claim version",
        int(expired_task["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "natural assembly lease expiry preserves one attempt",
        int(expired_task["finalize_attempts"]),
        1,
    )

    takeover_response = complete_upload_raw(
        upload_id,
        f"assembly-death-takeover-{upload_id}",
    )
    takeover_file_id = json_field(takeover_response.text, "data.file.id")
    if (
        takeover_response.status_code != 200
        or json_field(takeover_response.text, "code") != "0"
        or not takeover_file_id
    ):
        log_fail("primary API safely rebuilds and completes after assembly owner death")
        print(takeover_response.text)
        print_summary()
    log_pass("primary API safely rebuilds and completes after assembly owner death")
    assert_equal(
        "assembly takeover response comes from the primary API",
        header_value(takeover_response.headers, "X-Disk-Instance-Id"),
        primary_instance_id,
    )
    completed_file_id = int(takeover_file_id)

    completed_task = query_one(
        "SELECT status, completed_file_id, lease_owner, lease_expires_at, "
        "state_version, finalize_attempts "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if completed_task is None:
        log_fail("assembly takeover preserves the completed task")
        print_summary()
    assert_equal("assembly takeover reaches Completed", int(completed_task["status"]), 1)
    assert_equal(
        "assembly takeover records the returned file ID",
        int(completed_task["completed_file_id"]),
        completed_file_id,
    )
    assert_equal("assembly takeover clears lease_owner", completed_task["lease_owner"], None)
    assert_equal("assembly takeover clears lease_expires_at", completed_task["lease_expires_at"], None)
    assert_equal(
        "assembly takeover advances the killed state version",
        int(completed_task["state_version"]) > killed_state_version,
        True,
    )
    assert_equal(
        "assembly owner and takeover produce exactly two finalize attempts",
        int(completed_task["finalize_attempts"]),
        2,
    )

    assert_chunk_row_count(upload_id, 0)
    completed_quota = user_quota()
    assert_equal(
        "assembly takeover releases reserved storage once",
        completed_quota["storage_reserved"],
        quota_before["storage_reserved"],
    )
    assert_numeric_delta(
        "assembly takeover increases used storage once",
        quota_before["storage_used"],
        completed_quota["storage_used"],
        len(payload),
    )

    file_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
            (USER_ID, filename),
        )
        or 0
    )
    assert_equal("assembly death and takeover create one file row", file_count, 1)
    file_row = query_one(
        "SELECT file.id, content.ref_count, content.hash_md5, content.hash_sha256 "
        "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
        "WHERE file.id = %s AND file.user_id = %s",
        (completed_file_id, USER_ID),
    )
    if file_row is None:
        log_fail("assembly death and takeover create one file/content reference")
        print_summary()
    assert_equal("assembly takeover returns the persisted file", int(file_row["id"]), completed_file_id)
    assert_equal("assembly takeover increments ref_count once", int(file_row["ref_count"]), 1)
    assert_equal("assembly takeover content MD5 matches", file_row["hash_md5"], payload_md5)
    assert_equal("assembly takeover content SHA-256 matches", file_row["hash_sha256"], payload_sha256)
    content_count = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (payload_md5, payload_sha256),
        )
        or 0
    )
    assert_equal("assembly death and takeover create one content row", content_count, 1)

    cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{upload_id}",),
        )
        or 0
    )
    assert_equal("assembly death and takeover create one cleanup job", cleanup_count, 1)
    assert_storage_job_succeeded(
        "assembly-death takeover cleanup converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("assembly takeover removes the staging session", upload_temp_dir(upload_id))
    assert_path_absent("assembly takeover removes the rebuilt assembled object", assembled_path)
    assert_path_exists("assembly takeover preserves one final blob", blob_path)
    assert_equal("assembly takeover final blob contains the payload", blob_path.read_bytes(), payload)

    save_evidence(
        f"{EVIDENCE_PREFIX}-{upload_id}-assembly-death-takeover.json",
        json.dumps(
            {
                "upload_id": upload_id,
                "killed_pid": killed_pid,
                "killed_instance_id": killed_instance_id,
                "killed_state_version": killed_state_version,
                "killed_lease_expires_at": killed_lease_expires_at.isoformat()
                if killed_lease_expires_at is not None
                else None,
                "assembled_inode_before_kill": assembled_inode,
                "assembled_mtime_ns_before_kill": assembled_mtime_ns,
                "dropped_request_error": dropped_request_error,
                "live_lease_conflict_code": json_field(conflict_response.text, "code"),
                "takeover_instance_id": primary_instance_id,
                "final_state_version": int(completed_task["state_version"]),
                "finalize_attempts": int(completed_task["finalize_attempts"]),
                "completed_file_id": completed_file_id,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")


def assert_failed_finalize_recoverable(upload_id: str, filename: str, file_hash: str, quota_before_complete: dict[str, int]) -> None:
    """Assert failed finalization retains a leased task for retry or takeover."""
    assert_db_row_absent(
        "failed finalize creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_db_row_absent(
        "failed finalize creates no content row",
        "SELECT id FROM file_contents WHERE hash_md5 = %s",
        (file_hash,),
    )
    task = assert_upload_task(upload_id, 4)
    assert_equal("failed finalize leaves finalized_at unset", task["finalized_at"], None)
    assert_equal("failed finalize keeps lease owner", bool(task["lease_owner"]), True)
    assert_equal("failed finalize records an error code", task["last_error_code"] is not None, True)
    assert_chunk_row_count(upload_id, 1)
    quota_after_complete = user_quota()
    assert_equal(
        "failed finalize preserves reserved storage",
        quota_after_complete["storage_reserved"],
        quota_before_complete["storage_reserved"],
    )
    assert_equal(
        "failed finalize preserves used storage",
        quota_after_complete["storage_used"],
        quota_before_complete["storage_used"],
    )


def cleanup_failed_finalize_fixture(upload_id: str, blob_path) -> None:
    """Remove one deliberately failed finalization without using business APIs."""
    execute(
        """
        WITH removed AS (
            DELETE FROM upload_tasks
            WHERE id = %s AND user_id = %s AND status = 4
            RETURNING reserved_bytes
        )
        UPDATE users
        SET storage_reserved = storage_reserved - COALESCE((SELECT reserved_bytes FROM removed), 0)
        WHERE id = %s
        """,
        (upload_id, USER_ID, USER_ID),
    )
    blob_path.unlink(missing_ok=True)
    shutil.rmtree(upload_temp_dir(upload_id), ignore_errors=True)
    (upload_temp_dir(upload_id).parent / f"{upload_id}.tmp").unlink(missing_ok=True)


def test_db_failure_after_blob_promotion_retains_created_blob() -> None:
    """Verify a transaction failure retains a recognizable final candidate."""
    log_section("Finalize DB Failure Retains Created Blob")
    payload = f"safety-db-failure-created-{unique_name()}".encode()
    filename = f"safety_db_failure_created_{unique_name()}.bin"

    upload_id, file_hash = init_upload(filename, payload)
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    quota_before_complete = user_quota()
    blob_path = final_blob_path(sha256_bytes(payload))
    assert_path_absent("new-blob failure fixture starts without final blob", blob_path)

    affected = execute(
        "UPDATE upload_tasks SET folder_id = %s WHERE id = %s AND status = 0",
        (9_223_372_036_854_000_000, upload_id),
    )
    assert_equal("new-blob failure fixture corrupts target folder", affected, 1)

    resp = complete_upload_raw(upload_id)
    assert_equal("new-blob finalize failure returns non-success code", json_field(resp.text, "code") != "0", True)
    assert_failed_finalize_recoverable(upload_id, filename, file_hash, quota_before_complete)
    assert_path_exists("created final blob retained after transaction failure", blob_path)
    cleanup_failed_finalize_fixture(upload_id, blob_path)


def test_db_failure_after_promotion_preserves_preexisting_blob() -> None:
    """Verify compensation does not delete a final blob that existed before promotion."""
    log_section("Finalize DB Failure Preserves Preexisting Blob")
    payload = f"safety-db-failure-reused-{unique_name()}".encode()
    filename = f"safety_db_failure_reused_{unique_name()}.bin"
    file_hash = md5_bytes(payload)
    blob_path = final_blob_path(sha256_bytes(payload))

    upload_id, _ = init_upload(filename, payload)
    blob_path.parent.mkdir(parents=True, exist_ok=True)
    blob_path.write_bytes(payload)
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    quota_before_complete = user_quota()
    assert_path_exists("pre-existing final blob fixture is present", blob_path)

    affected = execute(
        "UPDATE upload_tasks SET folder_id = %s WHERE id = %s AND status = 0",
        (9_223_372_036_854_000_001, upload_id),
    )
    assert_equal("pre-existing-blob failure fixture corrupts target folder", affected, 1)

    resp = complete_upload_raw(upload_id)
    assert_equal("pre-existing-blob finalize failure returns non-success code", json_field(resp.text, "code") != "0", True)
    assert_failed_finalize_recoverable(upload_id, filename, file_hash, quota_before_complete)
    assert_path_exists("pre-existing final blob survives transaction failure", blob_path)
    assert_equal("pre-existing final blob bytes unchanged", blob_path.read_bytes(), payload)
    cleanup_failed_finalize_fixture(upload_id, blob_path)


def test_missing_staging_object_records_reconciliation() -> None:
    """Verify operators can diagnose and safely retry without database writes."""
    log_section("Missing Staging Object Reconciliation")
    payload = f"safety-missing-staging-{unique_name()}".encode()
    filename = f"safety_missing_staging_{unique_name()}.bin"
    upload_id, file_hash = init_upload(filename, payload)
    scan_id: str | None = None
    recovered_file_id: int | None = None

    try:
        upload_single_chunk(upload_id, payload)
        chunk = query_one(
            "SELECT object_key FROM upload_task_chunks WHERE task_id = %s AND chunk_index = 0",
            (upload_id,),
        )
        if chunk is None or not chunk["object_key"]:
            log_fail("missing-staging fixture has a persisted object key")
            print_summary()
        chunk_path = upload_temp_dir(upload_id).parent / str(chunk["object_key"])
        assert_path_exists("missing-staging fixture object exists before fault injection", chunk_path)
        chunk_path.unlink()
        assert_path_absent("missing-staging fixture removes only the object", chunk_path)

        quota_before_complete = user_quota()
        request_id = f"safety-complete-{unique_name()}"
        resp = complete_upload_raw(upload_id, request_id)
        assert_equal("missing staging object returns HTTP 400", resp.status_code, 400)
        assert_equal("missing staging object returns ChunkVerifyFailed", json_field(resp.text, "code"), "50009")
        response_request_id = header_value(resp.headers, "X-Request-Id")
        instance_id = header_value(resp.headers, "X-Disk-Instance-Id")
        assert_equal("failed response preserves caller request ID", response_request_id, request_id)
        assert_equal("failed response identifies the handling instance", bool(instance_id), True)
        request_log = wait_for_request_log(request_id, instance_id, resp.status_code)
        log_pass("failed request maps to one instance and completion log")
        assert_failed_finalize_recoverable(
            upload_id,
            filename,
            file_hash,
            quota_before_complete,
        )

        finding = query_one(
            "SELECT severity, resolution_strategy, resource_locator, details "
            "FROM storage_reconciliation_findings "
            "WHERE finding_type = 'upload_staging_mismatch' AND resource_id = %s",
            (upload_id,),
        )
        if finding is None:
            log_fail("missing staging object persists upload_staging_mismatch")
            print_summary()
        log_pass("missing staging object persists upload_staging_mismatch")
        assert_equal("staging mismatch is critical", int(finding["severity"]), 2)
        assert_equal("staging mismatch requires manual resolution", finding["resolution_strategy"], "manual")
        details = finding["details"]
        scan_id = str(details["scan_id"])
        assert_equal("staging mismatch records bounded scan id", scan_id.startswith("upload-integrity-"), True)
        assert_equal("staging mismatch records state version", int(details["state_version"]) >= 1, True)

        job = query_one(
            "SELECT id, job_type, aggregate_id FROM storage_jobs "
            "WHERE job_type = 'storage_reconcile' AND aggregate_id = %s",
            (scan_id,),
        )
        if job is None:
            log_fail("missing staging object enqueues a durable reconciliation job")
            print_summary()
        log_pass("missing staging object enqueues a durable reconciliation job")

        diagnostic = fetch(
            f"/api/admin/uploads/{upload_id}/diagnostics"
            "?chunk_page=1&chunk_page_size=20&job_page=1&job_page_size=100",
            headers=auth_headers(TOKEN),
        )
        if diagnostic.status_code != 200 or json_field(diagnostic.text, "code") != "0":
            log_fail("failed upload can be inspected through the read-only diagnostic endpoint")
            print(diagnostic.text)
            print_summary()
        diagnostic_data = json.loads(diagnostic.text)["data"]
        diagnostic_task = diagnostic_data["task"]
        diagnostic_chunks = diagnostic_data["chunks"]
        diagnostic_jobs = diagnostic_data["related_jobs"]["items"]
        assert_equal("diagnostic reports Finalizing database state", diagnostic_task["status"], "finalizing")
        assert_equal("diagnostic reports the finalize error code", diagnostic_task["last_error_code"], 50009)
        assert_equal("diagnostic returns the persisted chunk", len(diagnostic_chunks), 1)
        assert_equal("diagnostic reports the missing staging object", diagnostic_chunks[0]["object_head"]["status"], "missing")
        assert_equal("missing object cannot match the DB descriptor", diagnostic_chunks[0]["object_head"]["matches_record"], False)
        matching_jobs = [
            item
            for item in diagnostic_jobs
            if item["job_type"] == "storage_reconcile"
            and item["aggregate_id"] == scan_id
        ]
        assert_equal("diagnostic links the recovery task created by the failure", bool(matching_jobs), True)

        save_evidence(
            f"{EVIDENCE_PREFIX}-{upload_id}-failure-correlation.json",
            json.dumps(
                {
                    "request_id": request_id,
                    "instance_id": instance_id,
                    "upload_id": upload_id,
                    "request_log": request_log,
                    "database": {
                        "status": diagnostic_task["status"],
                        "state_version": diagnostic_task["state_version"],
                        "last_error_code": diagnostic_task["last_error_code"],
                    },
                    "object_head": {
                        "status": diagnostic_chunks[0]["object_head"]["status"],
                        "matches_record": diagnostic_chunks[0]["object_head"]["matches_record"],
                    },
                    "recovery_job": {
                        "id": matching_jobs[0]["id"],
                        "job_type": matching_jobs[0]["job_type"],
                        "status": matching_jobs[0]["status"],
                    },
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
        log_pass("one failed request locates instance, database state, object, and recovery task")

        log_section("API-only Upload Recovery")
        lease = diagnostic_task["lease"]
        if not isinstance(lease, dict) or not lease.get("owner"):
            log_fail("failed finalization exposes a live lease for recovery")
            print_summary()
        assert_equal("failed finalization lease is still live", lease["expired"], False)
        observed_version = int(diagnostic_task["state_version"])
        observed_owner = str(lease["owner"])

        # Repair the failed dependency at its owning storage boundary. From this
        # point onward every upload state transition and audit check uses HTTP.
        chunk_path.parent.mkdir(parents=True, exist_ok=True)
        chunk_path.write_bytes(payload)
        assert_path_exists("operator restores the exact staging object", chunk_path)

        diagnostic_path = (
            f"/api/admin/uploads/{upload_id}/diagnostics"
            "?chunk_page=1&chunk_page_size=20&job_page=1&job_page_size=100"
        )
        restored_diagnostic = fetch(diagnostic_path, headers=auth_headers(TOKEN))
        if restored_diagnostic.status_code != 200 or json_field(restored_diagnostic.text, "code") != "0":
            log_fail("restored staging object can be verified through diagnostics")
            print(restored_diagnostic.text)
            print_summary()
        restored_task = json.loads(restored_diagnostic.text)["data"]["task"]
        restored_chunk = json.loads(restored_diagnostic.text)["data"]["chunks"][0]
        assert_equal("storage repair does not change upload version", int(restored_task["state_version"]), observed_version)
        assert_equal("restored staging object is present", restored_chunk["object_head"]["status"], "present")
        assert_equal("restored staging object matches its descriptor", restored_chunk["object_head"]["matches_record"], True)

        release_path = f"/api/admin/uploads/{upload_id}/lease/release"
        dry_run = fetch(
            release_path,
            method="POST",
            headers=auth_headers(TOKEN),
            json_body={
                "expected_state_version": observed_version,
                "expected_lease_owner": observed_owner,
            },
        )
        if dry_run.status_code != 200 or json_field(dry_run.text, "code") != "0":
            log_fail("lease release dry-run succeeds with diagnostic owner and version")
            print(dry_run.text)
            print_summary()
        dry_run_data = json.loads(dry_run.text)["data"]
        assert_equal("lease release defaults to dry-run", dry_run_data["dry_run"], True)
        assert_equal("matching live lease is eligible", dry_run_data["eligible"], True)
        assert_equal("dry-run reports no mutation", dry_run_data["released"], False)
        assert_equal("dry-run preserves upload version", int(dry_run_data["state_version"]), observed_version)

        release = fetch(
            release_path,
            method="POST",
            headers=auth_headers(TOKEN),
            json_body={
                "dry_run": False,
                "confirm_upload_id": upload_id,
                "expected_state_version": observed_version,
                "expected_lease_owner": observed_owner,
                "reason": "staging dependency restored; retry completion",
            },
        )
        if release.status_code != 200 or json_field(release.text, "code") != "0":
            log_fail("confirmed lease release succeeds")
            print(release.text)
            print_summary()
        release_data = json.loads(release.text)["data"]
        released_version = observed_version + 1
        assert_equal("confirmed command releases the lease", release_data["released"], True)
        assert_equal("lease release keeps Finalizing state", release_data["status"], "finalizing")
        assert_equal("lease release increments the version", int(release_data["state_version"]), released_version)
        assert_equal("released lease is immediately expired", release_data["lease_expired"], True)

        audit_query = urlencode(
            {
                "action": "admin.upload.lease_release",
                "target_type": "upload",
                "target_name": upload_id,
                "page_size": 20,
            }
        )
        audit_response = fetch(
            f"/api/admin/logs?{audit_query}",
            headers=auth_headers(TOKEN),
        )
        if audit_response.status_code != 200 or json_field(audit_response.text, "code") != "0":
            log_fail("recovery audit is queryable through the admin API")
            print(audit_response.text)
            print_summary()
        audit_items = json.loads(audit_response.text)["data"]["items"]
        if len(audit_items) != 1:
            log_fail(f"exact audit filters returned {len(audit_items)} actions instead of one")
            print(audit_response.text)
            print_summary()
        log_pass("exact audit filters return one recovery action")
        audit_item = audit_items[0]
        assert_equal("recovery audit returns string target", audit_item["target_name"], upload_id)
        assert_equal("recovery audit uses upload target type", audit_item["target_type"], "upload")
        assert_equal("string target does not misuse numeric target ID", audit_item["target_id"], None)
        audit_details = audit_item["details"]
        if isinstance(audit_details, str):
            audit_details = json.loads(audit_details)
        assert_equal("recovery audit records previous version", int(audit_details["previous_state_version"]), observed_version)
        assert_equal("recovery audit records released version", int(audit_details["new_state_version"]), released_version)

        retry = complete_upload_raw(upload_id, f"safety-retry-{unique_name()}")
        recovered_file_id_text = json_field(retry.text, "data.file.id")
        if retry.status_code != 200 or json_field(retry.text, "code") != "0" or not recovered_file_id_text:
            log_fail("normal complete retry succeeds after audited lease release")
            print(retry.text)
            print_summary()
        recovered_file_id = int(recovered_file_id_text)

        deadline = time.monotonic() + 20.0
        final_diagnostic_data: dict[str, object] | None = None
        cleanup_status: str | None = None
        while time.monotonic() < deadline:
            final_diagnostic = fetch(diagnostic_path, headers=auth_headers(TOKEN))
            if final_diagnostic.status_code == 200 and json_field(final_diagnostic.text, "code") == "0":
                final_diagnostic_data = json.loads(final_diagnostic.text)["data"]
                cleanup_jobs = [
                    item
                    for item in final_diagnostic_data["related_jobs"]["items"]
                    if item["job_type"] == "staging_cleanup"
                    and item["aggregate_id"] == upload_id
                ]
                if cleanup_jobs:
                    cleanup_status = cleanup_jobs[0]["status"]
                    if cleanup_status in ("succeeded", "dead_letter"):
                        break
            time.sleep(0.05)

        if final_diagnostic_data is None:
            log_fail("completed upload remains available through diagnostics")
            print_summary()
        final_task = final_diagnostic_data["task"]
        assert_equal("safe retry reaches Completed", final_task["status"], "completed")
        assert_equal("safe retry records the completed file", int(final_task["completed_file_id"]), recovered_file_id)
        assert_equal("completed upload clears its lease", final_task["lease"], None)
        assert_equal("successful retry clears the prior error", final_task["last_error_code"], None)
        assert_equal("staging cleanup converges through the worker", cleanup_status, "succeeded")
        assert_path_absent("safe retry cleanup removes staging directory", upload_temp_dir(upload_id))

        save_evidence(
            f"{EVIDENCE_PREFIX}-{upload_id}-operator-recovery.json",
            json.dumps(
                {
                    "upload_id": upload_id,
                    "observed_state_version": observed_version,
                    "released_state_version": released_version,
                    "audit_id": audit_item["id"],
                    "completed_file_id": recovered_file_id,
                    "final_status": final_task["status"],
                    "cleanup_status": cleanup_status,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
        log_pass("operator diagnosis, audited lease release, and safe retry use APIs only")
    finally:
        if scan_id is not None:
            execute(
                "DELETE FROM storage_jobs WHERE job_type = 'storage_reconcile' AND aggregate_id = %s",
                (scan_id,),
            )
        execute(
            "DELETE FROM storage_reconciliation_findings "
            "WHERE finding_type = 'upload_staging_mismatch' AND resource_id = %s",
            (upload_id,),
        )
        execute(
            "DELETE FROM operation_logs "
            "WHERE action = 'admin.upload.lease_release' "
            "AND target_type = 'upload' AND target_name = %s",
            (upload_id,),
        )
        remaining_status = scalar(
            "SELECT status FROM upload_tasks WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if remaining_status == 4:
            cleanup_failed_finalize_fixture(upload_id, final_blob_path(sha256_bytes(payload)))


def test_complete_cancel_expire_race_invariants() -> None:
    """Verify concurrent terminal contenders settle quota and metadata exactly once."""
    log_section("Complete/Cancel/Expire Race Invariants")
    run_expired_cleanup()

    for expired in (False, True):
        scenario = "expired" if expired else "active"
        payload = f"safety-terminal-race-{scenario}-{unique_name()}".encode()
        filename = f"safety_terminal_race_{scenario}_{unique_name()}.bin"
        quota_before = user_quota()
        upload_id, file_hash = init_upload(filename, payload)
        upload_single_chunk(upload_id, payload)
        quota_after_init = user_quota()
        assert_numeric_delta(
            f"{scenario} race init reserves storage",
            quota_before["storage_reserved"],
            quota_after_init["storage_reserved"],
            len(payload),
        )

        if expired:
            affected = execute(
                "UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' "
                "WHERE id = %s AND user_id = %s AND status = 0",
                (upload_id, USER_ID),
            )
            assert_equal("expired race fixture crosses the database deadline", affected, 1)

        complete_response, cancel_response, cleanup_counts = race_complete_cancel_expire(upload_id)
        task = query_one(
            "SELECT status, completed_file_id, lease_owner, lease_expires_at "
            "FROM upload_tasks WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if task is None:
            log_fail(f"{scenario} terminal race preserves its upload task")
            print_summary()

        status = int(task["status"])
        allowed_statuses = {2, 3} if expired else {1, 2}
        if status not in allowed_statuses:
            log_fail(f"{scenario} terminal race reached illegal status {status}")
            print_summary()
        log_pass(f"{scenario} terminal race reaches one legal terminal state")

        quota_after_race = user_quota()
        assert_equal(
            f"{scenario} terminal race releases reserved storage exactly once",
            quota_after_race["storage_reserved"],
            quota_before["storage_reserved"],
        )
        assert_chunk_row_count(upload_id, 0)
        cleanup_job_count = int(
            scalar(
                "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
                (f"staging-cleanup:{upload_id}",),
            )
            or 0
        )
        assert_equal(f"{scenario} terminal race creates one cleanup job", cleanup_job_count, 1)

        file_row = query_one(
            "SELECT file.id, content.ref_count "
            "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
            "WHERE file.user_id = %s AND file.name = %s",
            (USER_ID, filename),
        )
        content_count = int(
            scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0
        )
        complete_code = json_field(complete_response.text, "code")
        cancel_code = json_field(cancel_response.text, "code")

        if status == 1:
            if file_row is None:
                log_fail("completed race creates one logical file")
                print_summary()
            log_pass("completed race creates one logical file")
            assert_equal("completed race records the winning file", int(task["completed_file_id"]), int(file_row["id"]))
            assert_equal("completed race creates one content row", content_count, 1)
            assert_equal("completed race increments content reference once", int(file_row["ref_count"]), 1)
            assert_numeric_delta(
                "completed race converts reserved storage to used once",
                quota_before["storage_used"],
                quota_after_race["storage_used"],
                len(payload),
            )
            assert_equal("completed race returns the winning complete response", complete_code, "0")
            assert_equal("completed race rejects cancellation", cancel_code == "0", False)
        else:
            assert_equal(f"{scenario} non-completed race creates no file", file_row, None)
            assert_equal(f"{scenario} non-completed race creates no content row", content_count, 0)
            assert_equal(
                f"{scenario} non-completed race preserves used storage",
                quota_after_race["storage_used"],
                quota_before["storage_used"],
            )
            assert_equal(f"{scenario} non-completed race rejects completion", complete_code == "0", False)
            if status == 2:
                assert_equal(f"{scenario} cancelled race returns success", cancel_code, "0")
            else:
                assert_equal("expired winner rejects cancellation", cancel_code == "0", False)

        assert_equal(f"{scenario} terminal race clears lease owner", task["lease_owner"], None)
        assert_equal(f"{scenario} terminal race clears lease deadline", task["lease_expires_at"], None)
        assert_storage_job_succeeded(
            f"{scenario} terminal race cleanup converges",
            f"staging-cleanup:{upload_id}",
        )
        save_evidence(
            f"{EVIDENCE_PREFIX}-{upload_id}-terminal-race.json",
            json.dumps(
                {
                    "scenario": scenario,
                    "upload_id": upload_id,
                    "status": status,
                    "complete_code": complete_code,
                    "cancel_code": cancel_code,
                    "expired_upload_tasks_cleaned": cleanup_counts[
                        "expired_upload_tasks_cleaned"
                    ],
                    "storage_used_before": quota_before["storage_used"],
                    "storage_used_after": quota_after_race["storage_used"],
                    "storage_reserved_before": quota_before["storage_reserved"],
                    "storage_reserved_after": quota_after_race["storage_reserved"],
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )


def test_cancel_upload_invariants() -> None:
    """Verify cancel releases reservations and removes temp artifacts."""
    log_section("Canceled Upload Invariants")
    payload = f"safety-cancel-{unique_name()}".encode()
    filename = f"safety_cancel_{unique_name()}.bin"
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    quota_after_init = user_quota()
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    cancel_upload(upload_id)
    assert_chunk_row_count(upload_id, 0)
    quota_after_cancel = user_quota()

    assert_upload_task(upload_id, 2)
    assert_numeric_delta(
        "cancel releases reserved storage",
        quota_after_init["storage_reserved"],
        quota_after_cancel["storage_reserved"],
        -len(payload),
    )
    assert_equal("cancel preserves used storage", quota_after_cancel["storage_used"], quota_before["storage_used"])
    assert_db_row_absent(
        "cancel creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_storage_job_succeeded(
        "cancel cleanup job converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("temp upload directory cleaned after cancel", upload_temp_dir(upload_id))
    assert_path_absent("final blob absent after cancel", final_blob_path(sha256_bytes(payload)))


def test_expired_upload_cleanup_invariants() -> None:
    """Verify deterministic expired-upload cleanup releases reservations and temp artifacts."""
    log_section("Expired Upload Cleanup Invariants")
    run_expired_cleanup()
    payload = f"safety-expire-{unique_name()}".encode()
    filename = f"safety_expire_{unique_name()}.bin"
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    quota_after_init = user_quota()
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    assert_numeric_delta(
        "expire fixture reserves storage",
        quota_before["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )

    affected = execute(
        "UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' WHERE id = %s AND status = 0",
        (upload_id,),
    )
    assert_equal("expire fixture marks upload task expired in DB", affected, 1)

    cleanup_counts = run_expired_cleanup()
    quota_after_cleanup = user_quota()

    assert_equal("cleanup reports at least one expired upload", cleanup_counts["expired_upload_tasks_cleaned"] >= 1, True)
    assert_chunk_row_count(upload_id, 0)
    task = assert_upload_task(upload_id, 3)
    assert_equal("expired task fail_reason documents expiry", task["fail_reason"], "任务过期")
    assert_numeric_delta(
        "expired upload cleanup releases reserved storage",
        quota_after_init["storage_reserved"],
        quota_after_cleanup["storage_reserved"],
        -len(payload),
    )
    assert_equal("expired upload cleanup preserves used storage", quota_after_cleanup["storage_used"], quota_before["storage_used"])
    assert_db_row_absent(
        "expired upload cleanup creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_storage_job_succeeded(
        "expired upload cleanup job converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("temp upload directory cleaned after expiry", upload_temp_dir(upload_id))
    assert_path_absent("assembled temp artifact absent after expiry", upload_temp_dir(upload_id).parent / f"{upload_id}.tmp")
    assert_path_absent("final blob absent after expiry", final_blob_path(sha256_bytes(payload)))


def test_init_upload_expires_existing_task_invariants() -> None:
    """Verify upload init cleanup releases expired task quota before reserving replacement."""
    log_section("Upload Init Inline Expired Cleanup Invariants")
    payload = f"safety-init-expire-{unique_name()}".encode()
    filename = f"safety_init_expire_{unique_name()}.bin"
    quota_before = user_quota()

    old_upload_id, file_hash = init_upload(filename, payload)
    quota_after_first_init = user_quota()
    upload_single_chunk(old_upload_id, payload)
    assert_numeric_delta(
        "inline expiry fixture reserves storage",
        quota_before["storage_reserved"],
        quota_after_first_init["storage_reserved"],
        len(payload),
    )

    affected = execute(
        "UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' WHERE id = %s AND status = 0",
        (old_upload_id,),
    )
    assert_equal("inline expiry fixture marks upload task expired in DB", affected, 1)

    new_upload_id, _ = init_upload(filename, payload)
    quota_after_second_init = user_quota()

    assert_equal("inline expiry init creates replacement upload id", new_upload_id != old_upload_id, True)
    old_task = assert_upload_task(old_upload_id, 3)
    assert_equal("inline expired task fail_reason documents expiry", old_task["fail_reason"], "任务过期")
    new_task = assert_upload_task(new_upload_id, 0)
    assert_equal("replacement task reserved_bytes equals file size", int(new_task["reserved_bytes"]), len(payload))
    assert_numeric_delta(
        "inline expired init does not double-reserve storage",
        quota_before["storage_reserved"],
        quota_after_second_init["storage_reserved"],
        len(payload),
    )
    assert_equal("inline expired init preserves used storage", quota_after_second_init["storage_used"], quota_before["storage_used"])
    assert_db_row_absent(
        "inline expired init creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_storage_job_succeeded(
        "inline expiry cleanup job converges",
        f"staging-cleanup:{old_upload_id}",
    )
    assert_path_absent("old temp upload directory cleaned by inline expiry", upload_temp_dir(old_upload_id))
    assert_path_absent(
        "old assembled temp artifact absent after inline expiry",
        upload_temp_dir(old_upload_id).parent / f"{old_upload_id}.tmp",
    )
    assert_path_absent("final blob absent after inline expiry", final_blob_path(sha256_bytes(payload)))


def test_complete_upload_db_failure_after_promotion_retains_final_blob() -> None:
    """Verify promoted blob remains identifiable when DB finalization fails."""
    log_section("Complete Upload Promotion Recovery Invariants")
    payload = f"safety-db-failure-{unique_name()}".encode()
    filename = f"safety_db_failure_{unique_name()}.bin"

    upload_id, file_hash = init_upload(filename, payload)
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)

    execute(
        """
        CREATE OR REPLACE FUNCTION fail_safety_upload_file_insert()
        RETURNS trigger AS $$
        BEGIN
            IF NEW.name LIKE 'safety_db_failure_%' THEN
                RAISE EXCEPTION 'intentional safety upload finalization failure';
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute("DROP TRIGGER IF EXISTS safety_upload_file_insert_fail ON files")
    execute(
        """
        CREATE TRIGGER safety_upload_file_insert_fail
        BEFORE INSERT ON files
        FOR EACH ROW EXECUTE FUNCTION fail_safety_upload_file_insert()
        """
    )

    try:
        resp = fetch(
            "/api/file/upload/complete",
            method="POST",
            headers=auth_headers(TOKEN),
            json_body={"upload_id": upload_id},
        )
        save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-complete-db-failure.json", resp.text)
        if resp.status_code == 200 and json_field(resp.text, "code") == "0":
            log_fail("complete upload should fail after trigger rejects files insert")
            print(resp.text)
            print_summary()

        log_pass("complete upload failed after files insert trigger")
    finally:
        execute("DROP TRIGGER IF EXISTS safety_upload_file_insert_fail ON files")
        execute("DROP FUNCTION IF EXISTS fail_safety_upload_file_insert()")

    task = assert_upload_task(upload_id, 4)
    assert_equal("failed finalization keeps reserved_bytes", int(task["reserved_bytes"]), len(payload))
    assert_chunk_row_count(upload_id, 1)
    assert_db_row_absent(
        "failed finalization creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_db_row_absent(
        "failed finalization rolls back file content row",
        "SELECT id FROM file_contents WHERE hash_md5 = %s",
        (file_hash,),
    )
    blob_path = final_blob_path(sha256_bytes(payload))
    assert_path_exists("promoted final blob retained after DB failure", blob_path)
    assert_equal("temp upload directory remains for retry", upload_temp_dir(upload_id).exists(), True)
    cleanup_failed_finalize_fixture(upload_id, blob_path)


def main() -> None:
    """Run upload safety-net tests."""
    print("==========================================")
    print("Upload Safety-Net Integration Tests")
    print("==========================================")
    print()

    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    SERVER_LOG_PATH.unlink(missing_ok=True)
    os.environ.setdefault("DISK_INSTANCE_ID", "safety-upload-api")
    ensure_server()

    global TOKEN, USER_ID
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)
    USER_ID = current_user_id()
    log_info(f"Using user_id={USER_ID}, chunk_size={configured_chunk_size()}, base_url={BASE_URL}")

    test_successful_chunked_upload_invariants()
    test_hundred_concurrent_complete_invariants()
    test_chunk_metadata_failure_retry_and_orphan_cleanup_invariants()
    test_finalize_claim_process_death_takeover_invariants()
    test_assembled_object_process_death_takeover_invariants()
    test_db_failure_after_blob_promotion_retains_created_blob()
    test_db_failure_after_promotion_preserves_preexisting_blob()
    test_missing_staging_object_records_reconciliation()
    test_complete_cancel_expire_race_invariants()
    test_cancel_upload_invariants()
    test_expired_upload_cleanup_invariants()
    test_init_upload_expires_existing_task_invariants()
    test_complete_upload_db_failure_after_promotion_retains_final_blob()

    print_summary()


if __name__ == "__main__":
    main()

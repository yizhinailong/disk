#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""
Safety-net integration tests for upload lifecycle invariants.

These tests intentionally assert DB and filesystem side effects, not only API
responses. They characterize current backend behavior before refactors.
"""

from __future__ import annotations

import atexit
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    assert_db_row_absent,
    assert_equal,
    assert_numeric_delta,
    assert_path_absent,
    check_server,
    cleanup,
    configured_chunk_size,
    do_login,
    execute,
    fetch,
    final_blob_path,
    json_field,
    log_fail,
    log_info,
    log_pass,
    log_section,
    log_step,
    md5_bytes,
    print_summary,
    query_one,
    save_evidence,
    scalar,
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


def upload_single_chunk(upload_id: str, payload: bytes) -> None:
    """Upload a single chunk with the payload's MD5 hash."""
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={md5_bytes(payload)}",
        method="POST",
        headers=auth_headers(TOKEN, "application/octet-stream"),
        data=payload,
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-chunk.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "data.uploaded") != "true":
        log_fail(f"{upload_id}: chunk upload failed")
        print(resp.text)
        print_summary()


def complete_upload(upload_id: str) -> str:
    """Complete an upload and return the created file id."""
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=auth_headers(TOKEN),
        json_body={"upload_id": upload_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-complete.json", resp.text)
    file_id = json_field(resp.text, "data.file.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not file_id:
        log_fail(f"{upload_id}: complete upload failed")
        print(resp.text)
        print_summary()
    return file_id


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


def cancel_upload(upload_id: str) -> None:
    """Cancel an upload task."""
    resp = fetch(
        f"/api/file/upload/{upload_id}",
        method="DELETE",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-cancel.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"{upload_id}: cancel upload failed")
        print(resp.text)
        print_summary()


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
    assert_path_absent("temp upload directory cleaned after success", upload_temp_dir(upload_id))
    assert_path_absent("assembled temp artifact cleaned after success", upload_temp_dir(upload_id).parent / f"{upload_id}.tmp")
    assert_equal("final blob path exists after success", final_blob_path(file_hash).exists(), True)


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
    assert_path_absent("temp upload directory cleaned after cancel", upload_temp_dir(upload_id))
    assert_path_absent("final blob absent after cancel", final_blob_path(file_hash))


def test_expired_upload_cleanup_invariants() -> None:
    """Verify deterministic expired-upload cleanup releases reservations and temp artifacts."""
    log_section("Expired Upload Cleanup Invariants")
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
    assert_path_absent("temp upload directory cleaned after expiry", upload_temp_dir(upload_id))
    assert_path_absent("assembled temp artifact absent after expiry", upload_temp_dir(upload_id).parent / f"{upload_id}.tmp")
    assert_path_absent("final blob absent after expiry", final_blob_path(file_hash))


def main() -> None:
    """Run upload safety-net tests."""
    print("==========================================")
    print("Upload Safety-Net Integration Tests")
    print("==========================================")
    print()

    if not check_server():
        sys.exit(1)

    global TOKEN, USER_ID
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)
    USER_ID = current_user_id()
    log_info(f"Using user_id={USER_ID}, chunk_size={configured_chunk_size()}, base_url={BASE_URL}")

    test_successful_chunked_upload_invariants()
    test_cancel_upload_invariants()
    test_expired_upload_cleanup_invariants()

    print_summary()


if __name__ == "__main__":
    main()

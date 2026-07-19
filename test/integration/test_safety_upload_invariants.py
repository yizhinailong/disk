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
import os
import shutil
import sys

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
    resp = complete_upload_raw(upload_id)
    file_id = json_field(resp.text, "data.file.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not file_id:
        log_fail(f"{upload_id}: complete upload failed")
        print(resp.text)
        print_summary()
    return file_id


def complete_upload_raw(upload_id: str):
    """Call complete upload and return the raw response without asserting success."""
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=auth_headers(TOKEN),
        json_body={"upload_id": upload_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-complete.json", resp.text)
    return resp


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

    ensure_server()

    global TOKEN, USER_ID
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)
    USER_ID = current_user_id()
    log_info(f"Using user_id={USER_ID}, chunk_size={configured_chunk_size()}, base_url={BASE_URL}")

    test_successful_chunked_upload_invariants()
    test_db_failure_after_blob_promotion_retains_created_blob()
    test_db_failure_after_promotion_preserves_preexisting_blob()
    test_cancel_upload_invariants()
    test_expired_upload_cleanup_invariants()
    test_init_upload_expires_existing_task_invariants()
    test_complete_upload_db_failure_after_promotion_retains_final_blob()

    print_summary()


if __name__ == "__main__":
    main()

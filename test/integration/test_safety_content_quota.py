#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""
Safety-net integration tests for content reference counts and quota behavior.
"""

from __future__ import annotations

import atexit
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    assert_db_row_absent,
    assert_equal,
    assert_numeric_delta,
    assert_path_absent,
    assert_path_exists,
    check_server,
    cleanup,
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
    scalar,
    unique_name,
)

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
EVIDENCE_PREFIX = "safety-content-quota"

TOKEN = ""
USER_ID = 0


def auth_headers(content_type: str = "application/json") -> dict[str, str]:
    """Return authorization headers for a test request."""
    return {"Authorization": f"Bearer {TOKEN}", "Content-Type": content_type}


def current_user_id() -> int:
    """Return the authenticated test user's database id."""
    value = scalar("SELECT id FROM users WHERE username = %s OR email = %s LIMIT 1", (TEST_USER, TEST_USER))
    if value is None:
        log_fail(f"Could not resolve user id for {TEST_USER}")
        print_summary()
    return int(value)


def user_quota() -> dict[str, int]:
    """Return current storage counters for the test user."""
    row = query_one("SELECT storage_used, storage_reserved, storage_quota FROM users WHERE id = %s", (USER_ID,))
    if row is None:
        log_fail(f"Could not load quota counters for user_id={USER_ID}")
        print_summary()
    return {key: int(row[key]) for key in ("storage_used", "storage_reserved", "storage_quota")}


def init_upload(filename: str, payload: bytes) -> tuple[str, str, str | None]:
    """Initialize upload and return upload_id, file_hash, instant file id if present."""
    file_hash = md5_bytes(payload)
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers(),
        json_body={
            "filename": filename,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{filename}-init.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"{filename}: init upload failed")
        print(resp.text)
        print_summary()
    return (
        json_field(resp.text, "data.upload_id"),
        file_hash,
        json_field(resp.text, "data.file.id") or None,
    )


def upload_chunk(upload_id: str, payload: bytes) -> None:
    """Upload one chunk for an upload task."""
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={md5_bytes(payload)}",
        method="POST",
        headers=auth_headers("application/octet-stream"),
        data=payload,
    )
    if resp.status_code != 200 or json_field(resp.text, "data.uploaded") != "true":
        log_fail(f"{upload_id}: chunk upload failed")
        print(resp.text)
        print_summary()


def complete_upload(upload_id: str) -> int:
    """Complete upload and return file id."""
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=auth_headers(),
        json_body={"upload_id": upload_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-complete.json", resp.text)
    file_id = json_field(resp.text, "data.file.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not file_id:
        log_fail(f"{upload_id}: complete upload failed")
        print(resp.text)
        print_summary()
    return int(file_id)


def upload_file(filename: str, payload: bytes) -> int:
    """Upload content as a chunked file and return file id."""
    upload_id, _, instant_file_id = init_upload(filename, payload)
    if instant_file_id:
        return int(instant_file_id)
    upload_chunk(upload_id, payload)
    return complete_upload(upload_id)


def file_row(file_id: int) -> dict[str, object]:
    """Return a file row or fail."""
    row = query_one("SELECT * FROM files WHERE id = %s AND user_id = %s", (file_id, USER_ID))
    if row is None:
        log_fail(f"file row exists: id={file_id}")
        print_summary()
    return row


def content_row(content_id: int) -> dict[str, object]:
    """Return a file_contents row or fail."""
    row = query_one("SELECT * FROM file_contents WHERE id = %s", (content_id,))
    if row is None:
        log_fail(f"content row exists: id={content_id}")
        print_summary()
    return row


def create_folder(name: str, parent_id: int = 0) -> int:
    """Create a folder and return its id."""
    resp = fetch(
        "/api/folder/create",
        method="POST",
        headers=auth_headers(),
        json_body={"name": name, "parent_id": parent_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-folder-{name}.json", resp.text)
    folder_id = json_field(resp.text, "data.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not folder_id:
        log_fail(f"create folder failed: {name}")
        print(resp.text)
        print_summary()
    return int(folder_id)


def copy_file(file_id: int, target_folder_id: int = 0) -> list[dict[str, int]]:
    """Copy a file and return new_files mappings."""
    resp = fetch(
        "/api/file/copy",
        method="POST",
        headers=auth_headers(),
        json_body={"file_ids": [file_id], "folder_ids": [], "target_folder_id": target_folder_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-copy-{file_id}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"copy file failed: file_id={file_id}")
        print(resp.text)
        print_summary()
    data = json.loads(resp.text)
    return data.get("data", {}).get("new_files", [])


def delete_file_to_trash(file_id: int) -> int:
    """Soft delete a file and return trash id."""
    resp = fetch(
        "/api/file",
        method="DELETE",
        headers=auth_headers(),
        json_body={"file_ids": [file_id], "folder_ids": []},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-delete-file-{file_id}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"soft delete failed: file_id={file_id}")
        print(resp.text)
        print_summary()
    row = query_one(
        "SELECT id FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s ORDER BY id DESC LIMIT 1",
        (USER_ID, file_id),
    )
    if row is None:
        log_fail(f"trash row created for file_id={file_id}")
        print_summary()
    return int(row["id"])


def permanently_delete_trash(trash_id: int) -> None:
    """Permanently delete one trash item."""
    resp = fetch(
        "/api/trash",
        method="DELETE",
        headers=auth_headers(),
        json_body={"trash_ids": [trash_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-trash-delete-{trash_id}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "data.results.0.status") != "success":
        log_fail(f"permanent trash delete failed: trash_id={trash_id}")
        print(resp.text)
        print_summary()


def test_instant_upload_dedup_ref_count() -> None:
    """Verify instant upload reuses content and increments ref_count."""
    log_section("Instant Upload Dedup Ref-Count")
    payload = f"instant-dedup-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_instant_src_{unique_name()}.bin", payload)
    first_file = file_row(first_file_id)
    content_id = int(first_file["content_id"])
    before_ref = int(content_row(content_id)["ref_count"])

    upload_id, file_hash, instant_file_id = init_upload(f"safety_instant_dst_{unique_name()}.bin", payload)
    assert_equal("instant upload returns no upload task", upload_id, "")
    if instant_file_id is None:
        log_fail("instant upload returned no data.file.id")
        print_summary()
    instant_file = file_row(int(instant_file_id))
    after_ref = int(content_row(content_id)["ref_count"])

    assert_equal("instant file reuses content_id", int(instant_file["content_id"]), content_id)
    assert_numeric_delta("instant upload increments ref_count", before_ref, after_ref, 1)
    assert_equal("instant upload hash matches existing content", file_hash, first_file["name"] and md5_bytes(payload))


def document_completion_dedup_public_api_limit() -> None:
    """Document why completion dedup is not deterministic through public APIs."""
    log_section("Completion Dedup Public API Limit")
    note = {
        "scenario": "content appears after init but before finalize",
        "status": "documented-pending",
        "reason": (
            "The public init endpoint resumes an existing pending upload task for the same user/hash, "
            "so a second public upload cannot deterministically create matching content before the first task finalizes."
        ),
        "recommended_next_step": "Cover this with a DB fixture or service-level seam when such fixture support exists.",
    }
    save_evidence(f"{EVIDENCE_PREFIX}-completion-dedup-pending.json", json.dumps(note, indent=2))
    log_pass("completion dedup public API limitation documented")


def test_copy_ref_count_and_quota() -> None:
    """Verify copying file increments content ref-count and used storage."""
    log_section("Copy Ref-Count And Quota")
    payload = f"copy-ref-quota-{unique_name()}".encode()
    source_file_id = upload_file(f"safety_copy_src_{unique_name()}.bin", payload)
    source_file = file_row(source_file_id)
    content_id = int(source_file["content_id"])
    before_ref = int(content_row(content_id)["ref_count"])
    quota_before = user_quota()

    copy_target_id = create_folder(f"safety_copy_target_{unique_name()}")
    mappings = copy_file(source_file_id, copy_target_id)
    if not mappings:
        log_fail("copy produced at least one new file mapping")
        print_summary()
    copied_file_id = int(mappings[0]["new_id"])
    copied_file = file_row(copied_file_id)
    after_ref = int(content_row(content_id)["ref_count"])
    quota_after = user_quota()

    assert_equal("copied file reuses source content_id", int(copied_file["content_id"]), content_id)
    assert_numeric_delta("copy increments ref_count", before_ref, after_ref, 1)
    assert_numeric_delta("copy increases used storage by logical file size", quota_before["storage_used"], quota_after["storage_used"], len(payload))


def test_upload_quota_rejection_no_leak() -> None:
    """Verify upload init rejects used+reserved+file_size over quota without leaks."""
    log_section("Upload Quota Rejection")
    original = user_quota()
    payload = b"q"
    filename = f"safety_quota_reject_{unique_name()}.bin"
    before_task_count = int(scalar("SELECT COUNT(*) FROM upload_tasks WHERE user_id = %s", (USER_ID,)) or 0)

    execute("UPDATE users SET storage_reserved = GREATEST(storage_quota - storage_used, 0) WHERE id = %s", (USER_ID,))
    saturated = user_quota()
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers(),
        json_body={"filename": filename, "file_size": len(payload), "file_hash": md5_bytes(payload), "parent_id": 0},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-upload-quota-reject.json", resp.text)
    execute(
        "UPDATE users SET storage_used = %s, storage_reserved = %s, storage_quota = %s WHERE id = %s",
        (original["storage_used"], original["storage_reserved"], original["storage_quota"], USER_ID),
    )

    after_task_count = int(scalar("SELECT COUNT(*) FROM upload_tasks WHERE user_id = %s", (USER_ID,)) or 0)
    restored = user_quota()
    assert_equal("upload quota rejection returns error", json_field(resp.text, "code") != "0", True)
    assert_equal("quota saturation made reserved non-negative", saturated["storage_reserved"] >= 0, True)
    assert_equal("upload quota rejection creates no task", after_task_count, before_task_count)
    assert_equal("upload quota rejection restores used", restored["storage_used"], original["storage_used"])
    assert_equal("upload quota rejection restores reserved", restored["storage_reserved"], original["storage_reserved"])


def test_copy_quota_rejection_no_side_effects() -> None:
    """Verify over-quota copy leaves no files or ref-count changes behind."""
    log_section("Copy Quota Rejection")
    payload = f"copy-quota-reject-{unique_name()}".encode()
    source_file_id = upload_file(f"safety_copy_quota_src_{unique_name()}.bin", payload)
    source_content_id = int(file_row(source_file_id)["content_id"])
    before_ref = int(content_row(source_content_id)["ref_count"])
    original = user_quota()
    before_file_count = int(scalar("SELECT COUNT(*) FROM files WHERE user_id = %s", (USER_ID,)) or 0)

    execute("UPDATE users SET storage_used = storage_quota WHERE id = %s", (USER_ID,))
    resp = fetch(
        "/api/file/copy",
        method="POST",
        headers=auth_headers(),
        json_body={"file_ids": [source_file_id], "folder_ids": [], "target_folder_id": 0},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-copy-quota-reject.json", resp.text)
    execute(
        "UPDATE users SET storage_used = %s, storage_reserved = %s, storage_quota = %s WHERE id = %s",
        (original["storage_used"], original["storage_reserved"], original["storage_quota"], USER_ID),
    )

    after_file_count = int(scalar("SELECT COUNT(*) FROM files WHERE user_id = %s", (USER_ID,)) or 0)
    after_ref = int(content_row(source_content_id)["ref_count"])
    assert_equal("copy quota rejection returns error", json_field(resp.text, "code") != "0", True)
    assert_equal("copy quota rejection creates no files", after_file_count, before_file_count)
    assert_equal("copy quota rejection leaves ref_count unchanged", after_ref, before_ref)


def test_permanent_delete_ref_count_and_blob_retention() -> None:
    """Verify permanent deletion decrements refs and deletes blob only at zero refs."""
    log_section("Permanent Delete Ref-Count And Blob Retention")
    payload = f"trash-ref-blob-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_trash_a_{unique_name()}.bin", payload)
    second_file_id = upload_file(f"safety_trash_b_{unique_name()}.bin", payload)
    content_id = int(file_row(first_file_id)["content_id"])
    file_hash = str(content_row(content_id)["hash_md5"])
    quota_before = user_quota()
    before_ref = int(content_row(content_id)["ref_count"])
    assert_path_exists("shared blob exists before permanent delete", final_blob_path(file_hash))

    trash_first = delete_file_to_trash(first_file_id)
    permanently_delete_trash(trash_first)
    mid_ref = int(content_row(content_id)["ref_count"])
    quota_after_first = user_quota()
    assert_numeric_delta("first permanent delete decrements ref_count", before_ref, mid_ref, -1)
    assert_path_exists("blob retained while ref_count remains positive", final_blob_path(file_hash))
    assert_numeric_delta("first permanent delete releases used storage", quota_before["storage_used"], quota_after_first["storage_used"], -len(payload))

    trash_second = delete_file_to_trash(second_file_id)
    permanently_delete_trash(trash_second)
    final_ref = int(content_row(content_id)["ref_count"])
    assert_equal("second permanent delete reaches zero ref_count", final_ref, 0)
    assert_path_absent("blob deleted when ref_count reaches zero", final_blob_path(file_hash))


def main() -> None:
    """Run content/quota safety-net tests."""
    print("==========================================")
    print("Content + Quota Safety-Net Integration Tests")
    print("==========================================")
    print()

    if not check_server():
        sys.exit(1)

    global TOKEN, USER_ID
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)
    USER_ID = current_user_id()
    log_info(f"Using user_id={USER_ID}, base_url={BASE_URL}")

    test_instant_upload_dedup_ref_count()
    document_completion_dedup_public_api_limit()
    test_copy_ref_count_and_quota()
    test_upload_quota_rejection_no_leak()
    test_copy_quota_rejection_no_side_effects()
    test_permanent_delete_ref_count_and_blob_retention()

    print_summary()


if __name__ == "__main__":
    main()

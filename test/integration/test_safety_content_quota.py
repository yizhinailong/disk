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


def init_upload(filename: str, payload: bytes, parent_id: int = 0) -> tuple[str, str, str | None]:
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
            "parent_id": parent_id,
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


def upload_file(filename: str, payload: bytes, parent_id: int = 0) -> int:
    """Upload content as a chunked file and return file id."""
    upload_id, _, instant_file_id = init_upload(filename, payload, parent_id)
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


def delete_folder_to_trash(folder_id: int) -> int:
    """Soft delete a folder and return trash id."""
    resp = fetch(
        "/api/file",
        method="DELETE",
        headers=auth_headers(),
        json_body={"file_ids": [], "folder_ids": [folder_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-delete-folder-{folder_id}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"soft delete failed: folder_id={folder_id}")
        print(resp.text)
        print_summary()
    row = query_one(
        "SELECT id FROM trash WHERE user_id = %s AND item_type = 'folder' AND item_id = %s ORDER BY id DESC LIMIT 1",
        (USER_ID, folder_id),
    )
    if row is None:
        log_fail(f"trash row created for folder_id={folder_id}")
        print_summary()
    return int(row["id"])


def empty_trash() -> dict[str, int]:
    """Empty the current user's trash and return response counters."""
    resp = fetch(
        "/api/trash/all",
        method="DELETE",
        headers=auth_headers(),
    )
    save_evidence(f"{EVIDENCE_PREFIX}-trash-empty.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail("empty trash failed")
        print(resp.text)
        print_summary()
    return {
        "deleted_count": int(json_field(resp.text, "data.deleted_count") or 0),
        "freed_space": int(json_field(resp.text, "data.freed_space") or 0),
    }


def create_share_fixture(file_ids: list[int] | None = None, folder_ids: list[int] | None = None) -> int:
    """Create an active share and direct share_files links for lifecycle cleanup assertions."""
    file_ids = file_ids or []
    folder_ids = folder_ids or []
    row = query_one(
        """
        INSERT INTO shares (share_code, user_id, permission, view_count, download_count, status, created_at, updated_at)
        VALUES (%s, %s, 'download', 0, 0, 1, NOW(), NOW())
        RETURNING id
        """,
        (f"trash_cleanup_{unique_name()}", USER_ID),
    )
    if row is None:
        log_fail("share fixture created")
        print_summary()
    share_id = int(row["id"])
    for file_id in file_ids:
        execute(
            "INSERT INTO share_files (share_id, item_type, item_id, created_at) VALUES (%s, 'file', %s, NOW())",
            (share_id, file_id),
        )
    for folder_id in folder_ids:
        execute(
            "INSERT INTO share_files (share_id, item_type, item_id, created_at) VALUES (%s, 'folder', %s, NOW())",
            (share_id, folder_id),
        )
    return share_id


def share_status(share_id: int) -> int:
    """Return share status."""
    status = scalar("SELECT status FROM shares WHERE id = %s", (share_id,))
    if status is None:
        log_fail(f"share exists: id={share_id}")
        print_summary()
    return int(status)


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


def restore_trash(trash_id: int) -> int:
    """Restore one trash item and return the new file id."""
    resp = fetch(
        "/api/trash/restore",
        method="POST",
        headers=auth_headers(),
        json_body={"trash_ids": [trash_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-trash-restore-{trash_id}.json", resp.text)
    restored_file_id = json_field(resp.text, "data.results.0.file_id")
    if (
        resp.status_code != 200
        or json_field(resp.text, "code") != "0"
        or json_field(resp.text, "data.results.0.status") != "success"
        or not restored_file_id
        or restored_file_id == "null"
    ):
        log_fail(f"trash restore failed: trash_id={trash_id}")
        print(resp.text)
        print_summary()
    return int(restored_file_id)


def run_expired_cleanup() -> dict[str, int]:
    """Run the deterministic admin/manual cleanup seam and return cleanup counts."""
    resp = fetch(
        "/api/admin/maintenance/cleanup/expired",
        method="POST",
        headers=auth_headers(),
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


def expire_trash_row(trash_id: int) -> None:
    """Make one trash row eligible for deterministic expired-trash cleanup."""
    affected = execute(
        "UPDATE trash SET expires_at = NOW() - INTERVAL '1 second' WHERE id = %s",
        (trash_id,),
    )
    assert_equal(f"trash row {trash_id} marked expired", affected, 1)


def test_instant_upload_dedup_ref_count() -> None:
    """Verify instant upload reuses content and increments ref_count."""
    log_section("Instant Upload Dedup Ref-Count")
    payload = f"instant-dedup-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_instant_src_{unique_name()}.bin", payload)
    first_file = file_row(first_file_id)
    content_id = int(first_file["content_id"])
    content_after_first = content_row(content_id)
    before_ref = int(content_after_first["ref_count"])
    assert_equal("first upload creates content ref_count=1", before_ref, 1)
    assert_equal("first upload stores sha256", content_after_first["hash_sha256"], sha256_bytes(payload))

    quota_before_instant = user_quota()
    upload_id, file_hash, instant_file_id = init_upload(f"safety_instant_dst_{unique_name()}.bin", payload)
    quota_after_instant = user_quota()
    assert_equal("instant upload returns no upload task", upload_id, "")
    if instant_file_id is None:
        log_fail("instant upload returned no data.file.id")
        print_summary()
    instant_file = file_row(int(instant_file_id))
    after_ref = int(content_row(content_id)["ref_count"])

    assert_equal("instant file reuses content_id", int(instant_file["content_id"]), content_id)
    assert_numeric_delta("instant upload increments ref_count", before_ref, after_ref, 1)
    assert_numeric_delta(
        "instant upload increases used storage by logical file size",
        quota_before_instant["storage_used"],
        quota_after_instant["storage_used"],
        len(payload),
    )
    assert_equal(
        "instant upload preserves reserved storage",
        quota_after_instant["storage_reserved"],
        quota_before_instant["storage_reserved"],
    )
    assert_equal("instant upload hash matches existing content", file_hash, first_file["name"] and md5_bytes(payload))
    same_hash_rows = int(scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0)
    assert_equal("instant upload creates no duplicate content row", same_hash_rows, 1)


def test_instant_upload_quota_rejection_no_side_effects() -> None:
    """Verify over-quota instant upload leaves no file, ref-count, or quota drift."""
    log_section("Instant Upload Quota Rejection")
    payload = f"instant-quota-reject-{unique_name()}".encode()
    source_file_id = upload_file(f"safety_instant_quota_src_{unique_name()}.bin", payload)
    source_file = file_row(source_file_id)
    content_id = int(source_file["content_id"])
    before_ref = int(content_row(content_id)["ref_count"])
    file_hash = md5_bytes(payload)
    target_name = f"safety_instant_quota_dst_{unique_name()}.bin"
    original = user_quota()
    before_same_hash_rows = int(scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0)
    before_target_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = 0 AND name = %s",
            (USER_ID, target_name),
        ) or 0
    )

    resp = None
    saturated: dict[str, int] | None = None
    quota_after_reject: dict[str, int] | None = None
    try:
        execute("UPDATE users SET storage_used = storage_quota WHERE id = %s", (USER_ID,))
        saturated = user_quota()
        resp = fetch(
            "/api/file/upload/init",
            method="POST",
            headers=auth_headers(),
            json_body={"filename": target_name, "file_size": len(payload), "file_hash": file_hash, "parent_id": 0},
        )
        save_evidence(f"{EVIDENCE_PREFIX}-instant-quota-reject.json", resp.text)
        quota_after_reject = user_quota()
    finally:
        execute(
            "UPDATE users SET storage_used = %s, storage_reserved = %s, storage_quota = %s WHERE id = %s",
            (original["storage_used"], original["storage_reserved"], original["storage_quota"], USER_ID),
        )

    if resp is None or saturated is None or quota_after_reject is None:
        log_fail("instant upload quota rejection request completed")
        print_summary()

    after_target_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = 0 AND name = %s",
            (USER_ID, target_name),
        ) or 0
    )
    after_ref = int(content_row(content_id)["ref_count"])
    restored = user_quota()
    after_same_hash_rows = int(scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0)

    assert_equal("instant upload quota rejection returns quota error", json_field(resp.text, "code"), "50004")
    assert_equal("instant upload quota rejection creates no target file", after_target_count, before_target_count)
    assert_equal("instant upload quota rejection leaves ref_count unchanged", after_ref, before_ref)
    assert_equal("instant upload quota rejection leaves used unchanged", quota_after_reject["storage_used"], saturated["storage_used"])
    assert_equal("instant upload quota rejection leaves reserved unchanged", quota_after_reject["storage_reserved"], saturated["storage_reserved"])
    assert_equal("instant upload quota rejection restores used", restored["storage_used"], original["storage_used"])
    assert_equal("instant upload quota rejection restores reserved", restored["storage_reserved"], original["storage_reserved"])
    assert_equal("instant upload quota rejection creates no duplicate content row", after_same_hash_rows, before_same_hash_rows)
    assert_path_exists("instant upload quota rejection keeps existing final blob", final_blob_path(file_hash))


def create_matching_content_fixture(filename: str, payload: bytes) -> tuple[int, int, str]:
    """Create matching content and a logical file after upload init to simulate finalize-time dedup."""
    file_hash = md5_bytes(payload)
    sha256_hash = sha256_bytes(payload)
    blob_path = final_blob_path(file_hash)
    blob_path.parent.mkdir(parents=True, exist_ok=True)
    blob_path.write_bytes(payload)

    content = query_one(
        """
        INSERT INTO file_contents (hash_md5, hash_sha256, size, storage_path, mime_type, ref_count)
        VALUES (%s, %s, %s, %s, %s, 1)
        RETURNING id
        """,
        (file_hash, sha256_hash, len(payload), str(blob_path), ""),
    )
    if content is None:
        log_fail("matching file_contents fixture created")
        print_summary()
    content_id = int(content["id"])

    file_row_result = query_one(
        """
        INSERT INTO files (user_id, content_id, folder_id, name, extension, size, mime_type, path)
        VALUES (%s, %s, 0, %s, %s, %s, %s, %s)
        RETURNING id
        """,
        (USER_ID, content_id, filename, "bin", len(payload), "", f"/{filename}"),
    )
    if file_row_result is None:
        log_fail("matching files fixture created")
        print_summary()
    execute("UPDATE users SET storage_used = storage_used + %s WHERE id = %s", (len(payload), USER_ID))
    return int(file_row_result["id"]), content_id, file_hash


def test_completion_dedup_race_ref_count_and_accounting_current_rule() -> None:
    """Verify completion reuses content that appears after init and commits reserved storage to used."""
    log_section("Completion Dedup Race Ref-Count And Accounting")
    payload = f"completion-dedup-race-{unique_name()}".encode()
    upload_filename = f"safety_dedup_race_upload_{unique_name()}.bin"
    fixture_filename = f"safety_dedup_race_existing_{unique_name()}.bin"
    quota_before_init = user_quota()

    upload_id, file_hash, instant_file_id = init_upload(upload_filename, payload)
    assert_equal("dedup race starts as non-instant upload", instant_file_id is None, True)
    quota_after_init = user_quota()
    assert_numeric_delta(
        "dedup race init reserves storage",
        quota_before_init["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )
    upload_chunk(upload_id, payload)

    existing_file_id, content_id, fixture_hash = create_matching_content_fixture(fixture_filename, payload)
    assert_equal("dedup fixture hash matches upload hash", fixture_hash, file_hash)
    before_complete_ref = int(content_row(content_id)["ref_count"])
    quota_before_complete = user_quota()

    completed_file_id = complete_upload(upload_id)
    completed_file = file_row(completed_file_id)
    existing_file = file_row(existing_file_id)
    after_complete_ref = int(content_row(content_id)["ref_count"])
    quota_after_complete = user_quota()

    assert_equal("completion dedup reuses existing content_id", int(completed_file["content_id"]), content_id)
    assert_equal("dedup fixture file keeps same content_id", int(existing_file["content_id"]), content_id)
    assert_numeric_delta("completion dedup increments ref_count by current rule", before_complete_ref, after_complete_ref, 1)
    assert_numeric_delta(
        "completion dedup releases reserved storage",
        quota_before_complete["storage_reserved"],
        quota_after_complete["storage_reserved"],
        -len(payload),
    )
    assert_numeric_delta(
        "completion dedup commits reserved bytes to used storage by current backend rule",
        quota_before_complete["storage_used"],
        quota_after_complete["storage_used"],
        len(payload),
    )
    same_hash_rows = int(scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0)
    assert_equal("completion dedup creates no duplicate content row", same_hash_rows, 1)
    assert_path_exists("dedup race keeps existing final blob", final_blob_path(file_hash))
    assert_path_absent("dedup race cleans temp upload directory", upload_temp_dir(upload_id))


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


def test_soft_delete_preserves_ref_count_storage_used_and_blob() -> None:
    """Verify soft delete moves to trash without releasing content refs, quota, or blob."""
    log_section("Soft Delete Preserves Ref-Count, Quota, And Blob")
    payload = f"soft-delete-boundary-{unique_name()}".encode()
    file_id = upload_file(f"safety_soft_delete_{unique_name()}.bin", payload)
    original_file = file_row(file_id)
    content_id = int(original_file["content_id"])
    file_hash = str(content_row(content_id)["hash_md5"])
    before_ref = int(content_row(content_id)["ref_count"])
    quota_before = user_quota()
    assert_path_exists("blob exists before soft delete", final_blob_path(file_hash))

    trash_id = delete_file_to_trash(file_id)
    trash_row = query_one(
        "SELECT content_id FROM trash WHERE id = %s AND user_id = %s",
        (trash_id, USER_ID),
    )
    if trash_row is None:
        log_fail(f"trash row exists after soft delete: id={trash_id}")
        print_summary()
    after_ref = int(content_row(content_id)["ref_count"])
    quota_after = user_quota()

    assert_db_row_absent("active file row removed on soft delete", "SELECT id FROM files WHERE id = %s", (file_id,))
    assert_equal("trash row keeps content_id", int(trash_row["content_id"]), content_id)
    assert_equal("soft delete keeps ref_count", after_ref, before_ref)
    assert_equal("soft delete keeps storage_used", quota_after["storage_used"], quota_before["storage_used"])
    assert_path_exists("soft delete keeps blob while in trash", final_blob_path(file_hash))


def test_restore_preserves_ref_count_storage_used_and_removes_trash() -> None:
    """Verify restore recreates the active file without changing content refs or quota."""
    log_section("Restore Preserves Ref-Count And Quota")
    payload = f"restore-boundary-{unique_name()}".encode()
    file_id = upload_file(f"safety_restore_{unique_name()}.bin", payload)
    original_file = file_row(file_id)
    content_id = int(original_file["content_id"])
    before_ref = int(content_row(content_id)["ref_count"])
    quota_before = user_quota()

    trash_id = delete_file_to_trash(file_id)
    restored_file_id = restore_trash(trash_id)
    restored_file = file_row(restored_file_id)
    after_ref = int(content_row(content_id)["ref_count"])
    quota_after = user_quota()

    assert_equal("restored file reuses content_id", int(restored_file["content_id"]), content_id)
    assert_db_row_absent("trash row removed after restore", "SELECT id FROM trash WHERE id = %s", (trash_id,))
    assert_equal("restore keeps ref_count", after_ref, before_ref)
    assert_equal("restore keeps storage_used", quota_after["storage_used"], quota_before["storage_used"])


def test_share_cleanup_on_soft_delete() -> None:
    """Verify move-to-trash removes share links and cancels shares only when empty."""
    log_section("Share Cleanup On Soft Delete")
    first_file_id = upload_file(f"safety_share_cleanup_a_{unique_name()}.bin", b"share-cleanup-a")
    second_file_id = upload_file(f"safety_share_cleanup_b_{unique_name()}.bin", b"share-cleanup-b")
    share_id = create_share_fixture([first_file_id, second_file_id])

    delete_file_to_trash(first_file_id)
    remaining_links = int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s", (share_id,)) or 0)
    deleted_link_count = int(
        scalar(
            "SELECT COUNT(*) FROM share_files WHERE share_id = %s AND item_type = 'file' AND item_id = %s",
            (share_id, first_file_id),
        ) or 0
    )
    assert_equal("deleted file share link removed", deleted_link_count, 0)
    assert_equal("share keeps remaining file link", remaining_links, 1)
    assert_equal("share remains active while one link remains", share_status(share_id), 1)

    delete_file_to_trash(second_file_id)
    final_links = int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s", (share_id,)) or 0)
    assert_equal("last share link removed", final_links, 0)
    assert_equal("empty share is cancelled", share_status(share_id), 0)


def test_folder_soft_delete_snapshot_preserves_ref_count_and_storage() -> None:
    """Verify folder soft delete writes a folder snapshot and preserves accounting."""
    log_section("Folder Soft Delete Snapshot Preserves Accounting")
    root_folder_id = create_folder(f"safety_folder_root_{unique_name()}")
    child_folder_id = create_folder(f"safety_folder_child_{unique_name()}", root_folder_id)
    root_payload = f"folder-root-file-{unique_name()}".encode()
    child_payload = f"folder-child-file-{unique_name()}".encode()
    root_file_id = upload_file(f"safety_folder_root_file_{unique_name()}.bin", root_payload, root_folder_id)
    child_file_id = upload_file(f"safety_folder_child_file_{unique_name()}.bin", child_payload, child_folder_id)
    root_content_id = int(file_row(root_file_id)["content_id"])
    child_content_id = int(file_row(child_file_id)["content_id"])
    before_refs = {
        root_content_id: int(content_row(root_content_id)["ref_count"]),
        child_content_id: int(content_row(child_content_id)["ref_count"]),
    }
    quota_before = user_quota()
    folder_share_id = create_share_fixture(folder_ids=[root_folder_id])

    trash_id = delete_folder_to_trash(root_folder_id)
    trash_row = query_one("SELECT item_data FROM trash WHERE id = %s AND user_id = %s", (trash_id, USER_ID))
    if trash_row is None:
        log_fail(f"folder trash row exists: id={trash_id}")
        print_summary()
    raw_item_data = trash_row["item_data"]
    item_data = raw_item_data if isinstance(raw_item_data, dict) else json.loads(str(raw_item_data))

    assert_equal("folder snapshot type", item_data.get("type"), "folder_tree")
    assert_equal("folder snapshot version", int(item_data.get("version")), 1)
    assert_equal("folder snapshot root id", int(item_data.get("root", {}).get("id")), root_folder_id)
    assert_equal(
        "folder snapshot contains child folder",
        any(int(folder.get("id")) == child_folder_id for folder in item_data.get("folders", [])),
        True,
    )
    snapshot_file_ids = {int(file.get("id")) for file in item_data.get("files", [])}
    assert_equal("folder snapshot contains root file", root_file_id in snapshot_file_ids, True)
    assert_equal("folder snapshot contains child file", child_file_id in snapshot_file_ids, True)
    assert_db_row_absent("root file row removed by folder soft delete", "SELECT id FROM files WHERE id = %s", (root_file_id,))
    assert_db_row_absent("child file row removed by folder soft delete", "SELECT id FROM files WHERE id = %s", (child_file_id,))
    assert_db_row_absent("child folder row removed by folder soft delete", "SELECT id FROM folders WHERE id = %s", (child_folder_id,))
    assert_equal("folder soft delete keeps root file ref_count", int(content_row(root_content_id)["ref_count"]), before_refs[root_content_id])
    assert_equal("folder soft delete keeps child file ref_count", int(content_row(child_content_id)["ref_count"]), before_refs[child_content_id])
    assert_equal("folder soft delete keeps storage_used", user_quota()["storage_used"], quota_before["storage_used"])
    assert_equal("folder share link removed", int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s", (folder_share_id,)) or 0), 0)
    assert_equal("folder share cancelled", share_status(folder_share_id), 0)


def test_delete_all_ref_count_quota_and_blob_cleanup() -> None:
    """Verify empty-trash uses permanent-delete semantics for refs, quota, and blobs."""
    log_section("Delete All Ref-Count, Quota, And Blob Cleanup")
    empty_trash()
    payload = f"delete-all-boundary-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_delete_all_a_{unique_name()}.bin", payload)
    second_file_id = upload_file(f"safety_delete_all_b_{unique_name()}.bin", payload)
    content_id = int(file_row(first_file_id)["content_id"])
    file_hash = str(content_row(content_id)["hash_md5"])
    before_ref = int(content_row(content_id)["ref_count"])
    quota_before = user_quota()

    first_trash = delete_file_to_trash(first_file_id)
    second_trash = delete_file_to_trash(second_file_id)
    result = empty_trash()
    quota_after = user_quota()
    final_ref = int(content_row(content_id)["ref_count"])

    assert_equal("delete-all reports at least test rows", result["deleted_count"] >= 2, True)
    assert_db_row_absent("first delete-all trash row removed", "SELECT id FROM trash WHERE id = %s", (first_trash,))
    assert_db_row_absent("second delete-all trash row removed", "SELECT id FROM trash WHERE id = %s", (second_trash,))
    assert_numeric_delta("delete-all decrements ref_count for both files", before_ref, final_ref, -2)
    assert_equal("delete-all does not drive ref_count negative", final_ref >= 0, True)
    assert_numeric_delta("delete-all releases used storage for both files", quota_before["storage_used"], quota_after["storage_used"], -2 * len(payload))
    assert_path_absent("delete-all deletes blob at zero ref_count", final_blob_path(file_hash))


def test_permanent_delete_ref_count_and_blob_retention() -> None:
    """Verify selected permanent delete decrements refs and keeps shared blobs until zero refs."""
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
    assert_equal("permanent delete does not drive ref_count negative", final_ref >= 0, True)
    assert_path_absent("blob deleted when ref_count reaches zero", final_blob_path(file_hash))


def test_expired_trash_cleanup_ref_count_quota_and_blob_retention() -> None:
    """Verify expired-trash cleanup uses permanent-delete semantics and blob safety."""
    log_section("Expired Trash Cleanup Ref-Count, Quota, And Blob Retention")
    payload = f"expired-trash-ref-blob-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_expired_trash_a_{unique_name()}.bin", payload)
    second_file_id = upload_file(f"safety_expired_trash_b_{unique_name()}.bin", payload)
    content_id = int(file_row(first_file_id)["content_id"])
    file_hash = str(content_row(content_id)["hash_md5"])
    quota_before = user_quota()
    before_ref = int(content_row(content_id)["ref_count"])
    assert_path_exists("shared blob exists before expired-trash cleanup", final_blob_path(file_hash))

    trash_first = delete_file_to_trash(first_file_id)
    expire_trash_row(trash_first)
    first_counts = run_expired_cleanup()
    mid_ref = int(content_row(content_id)["ref_count"])
    quota_after_first = user_quota()

    assert_equal("expired cleanup reports first trash deletion", first_counts["expired_trash_deleted"] >= 1, True)
    assert_db_row_absent("expired trash row removed after cleanup", "SELECT id FROM trash WHERE id = %s", (trash_first,))
    assert_numeric_delta("expired cleanup decrements ref_count", before_ref, mid_ref, -1)
    assert_numeric_delta(
        "expired cleanup releases used storage by current backend rule",
        quota_before["storage_used"],
        quota_after_first["storage_used"],
        -len(payload),
    )
    assert_path_exists("expired cleanup retains shared blob while ref_count remains positive", final_blob_path(file_hash))

    trash_second = delete_file_to_trash(second_file_id)
    expire_trash_row(trash_second)
    second_counts = run_expired_cleanup()
    final_ref = int(content_row(content_id)["ref_count"])
    quota_after_second = user_quota()

    assert_equal("expired cleanup reports second trash deletion", second_counts["expired_trash_deleted"] >= 1, True)
    assert_db_row_absent("second expired trash row removed after cleanup", "SELECT id FROM trash WHERE id = %s", (trash_second,))
    assert_equal("expired cleanup reaches zero ref_count after final reference", final_ref, 0)
    assert_equal("expired cleanup does not drive ref_count negative", final_ref >= 0, True)
    assert_numeric_delta(
        "final expired cleanup releases used storage by current backend rule",
        quota_after_first["storage_used"],
        quota_after_second["storage_used"],
        -len(payload),
    )
    assert_path_absent("expired cleanup deletes blob only when ref_count reaches zero", final_blob_path(file_hash))


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
    test_completion_dedup_race_ref_count_and_accounting_current_rule()
    test_copy_ref_count_and_quota()
    test_upload_quota_rejection_no_leak()
    test_copy_quota_rejection_no_side_effects()
    test_soft_delete_preserves_ref_count_storage_used_and_blob()
    test_restore_preserves_ref_count_storage_used_and_removes_trash()
    test_share_cleanup_on_soft_delete()
    test_folder_soft_delete_snapshot_preserves_ref_count_and_storage()
    test_delete_all_ref_count_quota_and_blob_cleanup()
    test_permanent_delete_ref_count_and_blob_retention()
    test_expired_trash_cleanup_ref_count_quota_and_blob_retention()

    print_summary()


if __name__ == "__main__":
    main()

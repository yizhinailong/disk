#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""
Backend domain extraction characterization tests.

These tests assert DB and filesystem invariants that the ContentService,
QuotaService, UploadLifecycleService, and TrashService extractions must
preserve. They intentionally verify side effects, not only API responses.
"""

from __future__ import annotations

import atexit
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    assert_db_row_absent,
    assert_equal,
    assert_numeric_delta,
    assert_path_absent,
    assert_path_exists,
    ensure_server,
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
EVIDENCE_PREFIX = "domain-extraction"

TOKEN = ""
USER_ID = 0


def auth_headers(content_type: str = "application/json") -> dict[str, str]:
    return {"Authorization": f"Bearer {TOKEN}", "Content-Type": content_type}


def current_user_id() -> int:
    user_id = scalar("SELECT id FROM users WHERE username = %s OR email = %s LIMIT 1", (TEST_USER, TEST_USER))
    if user_id is None:
        log_fail(f"Could not resolve test user id for {TEST_USER}")
        print_summary()
    return int(user_id)


def user_quota() -> dict[str, int]:
    row = query_one(
        "SELECT storage_used, storage_reserved, storage_quota FROM users WHERE id = %s",
        (USER_ID,),
    )
    if row is None:
        log_fail(f"Could not load user quota for user_id={USER_ID}")
        print_summary()
    return {key: int(row[key]) for key in ("storage_used", "storage_reserved", "storage_quota")}


def upload_init(filename: str, payload: bytes) -> tuple[str, str, str]:
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
        log_fail(f"{filename}: upload init failed")
        print(resp.text)
        print_summary()

    return json_field(resp.text, "data.upload_id"), file_hash, resp.text


def init_chunked_upload(filename: str, payload: bytes) -> tuple[str, str]:
    upload_id, file_hash, body = upload_init(filename, payload)
    if json_field(body, "data.instant_upload") == "true":
        log_fail(f"{filename}: expected chunked upload but got instant_upload=true")
        print(body)
        print_summary()
    if not upload_id:
        log_fail(f"{filename}: chunked upload did not return upload_id")
        print(body)
        print_summary()
    return upload_id, file_hash


def upload_single_chunk(upload_id: str, payload: bytes) -> None:
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={md5_bytes(payload)}",
        method="POST",
        headers=auth_headers("application/octet-stream"),
        data=payload,
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-chunk.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "data.uploaded") != "true":
        log_fail(f"{upload_id}: chunk upload failed")
        print(resp.text)
        print_summary()


def complete_upload(upload_id: str) -> int:
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


def upload_file(filename: str, payload: bytes) -> tuple[int, str]:
    upload_id, file_hash = init_chunked_upload(filename, payload)
    upload_single_chunk(upload_id, payload)
    return complete_upload(upload_id), file_hash


def file_row(file_id: int) -> dict[str, object]:
    row = query_one("SELECT * FROM files WHERE id = %s AND user_id = %s", (file_id, USER_ID))
    if row is None:
        log_fail(f"files row exists: file_id={file_id}")
        print_summary()
    return row


def content_row(content_id: int) -> dict[str, object]:
    row = query_one("SELECT * FROM file_contents WHERE id = %s", (content_id,))
    if row is None:
        log_fail(f"file_contents row exists: content_id={content_id}")
        print_summary()
    return row


def create_folder(name: str, parent_id: int = 0) -> int:
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


def copy_items(file_ids: list[int], folder_ids: list[int], target_folder_id: int) -> dict[str, object]:
    resp = fetch(
        "/api/file/copy",
        method="POST",
        headers=auth_headers(),
        json_body={"file_ids": file_ids, "folder_ids": folder_ids, "target_folder_id": target_folder_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-copy-{unique_name()}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail("copy request failed")
        print(resp.text)
        print_summary()
    return json.loads(resp.text)["data"]


def soft_delete_file(file_id: int) -> None:
    resp = fetch(
        "/api/file",
        method="DELETE",
        headers=auth_headers(),
        json_body={"file_ids": [file_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-soft-delete-{file_id}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"soft delete failed: file_id={file_id}")
        print(resp.text)
        print_summary()


def trash_row_for_original(file_id: int) -> dict[str, object]:
    row = query_one("SELECT * FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s", (USER_ID, file_id))
    if row is None:
        log_fail(f"trash row exists for file_id={file_id}")
        print_summary()
    return row


def permanent_delete_trash(trash_id: int) -> None:
    resp = fetch(
        "/api/trash",
        method="DELETE",
        headers=auth_headers(),
        json_body={"trash_ids": [trash_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-permanent-delete-{trash_id}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or json_field(resp.text, "data.results.0.status") != "success":
        log_fail(f"permanent delete failed: trash_id={trash_id}")
        print(resp.text)
        print_summary()


def test_successful_upload_invariants() -> None:
    log_section("Successful Upload Invariants")
    payload = f"domain-upload-{unique_name()}".encode()
    filename = f"domain_success_{unique_name()}.bin"
    quota_before = user_quota()

    upload_id, file_hash = init_chunked_upload(filename, payload)
    quota_after_init = user_quota()
    assert_numeric_delta("upload init reserves storage", quota_before["storage_reserved"], quota_after_init["storage_reserved"], len(payload))

    upload_single_chunk(upload_id, payload)
    file_id = complete_upload(upload_id)
    quota_after_complete = user_quota()

    task = query_one("SELECT * FROM upload_tasks WHERE id = %s", (upload_id,))
    if task is None:
        log_fail("upload task row remains for completed upload")
        print_summary()
    assert_equal("upload task status is completed", int(task["status"]), 1)
    assert_equal("upload task reserved_bytes equals file size", int(task["reserved_bytes"]), len(payload))
    assert_numeric_delta("complete releases reserved storage", quota_after_init["storage_reserved"], quota_after_complete["storage_reserved"], -len(payload))
    assert_numeric_delta("complete increases used storage", quota_before["storage_used"], quota_after_complete["storage_used"], len(payload))

    uploaded_file = file_row(file_id)
    assert_equal("files row has expected name", uploaded_file["name"], filename)
    assert_equal("files row has expected size", int(uploaded_file["size"]), len(payload))

    content = content_row(int(uploaded_file["content_id"]))
    assert_equal("file_contents md5 matches payload", content["hash_md5"], file_hash)
    assert_equal("new content ref_count starts at 1", int(content["ref_count"]), 1)
    assert_path_absent("temp upload directory cleaned after success", upload_temp_dir(upload_id))
    assert_path_absent("assembled temp artifact cleaned after success", upload_temp_dir(upload_id).parent / f"{upload_id}.tmp")
    assert_path_exists("final blob exists after success", final_blob_path(file_hash))


def test_cancel_upload_invariants() -> None:
    log_section("Cancel Upload Invariants")
    payload = f"domain-cancel-{unique_name()}".encode()
    filename = f"domain_cancel_{unique_name()}.bin"
    quota_before = user_quota()

    upload_id, file_hash = init_chunked_upload(filename, payload)
    quota_after_init = user_quota()
    upload_single_chunk(upload_id, payload)

    resp = fetch(
        f"/api/file/upload/{upload_id}",
        method="DELETE",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-cancel.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"cancel upload failed: upload_id={upload_id}")
        print(resp.text)
        print_summary()

    quota_after_cancel = user_quota()
    task = query_one("SELECT * FROM upload_tasks WHERE id = %s", (upload_id,))
    if task is None:
        log_fail("cancelled upload task row remains")
        print_summary()
    assert_equal("upload task status is cancelled", int(task["status"]), 2)
    assert_numeric_delta("cancel releases reserved storage", quota_after_init["storage_reserved"], quota_after_cancel["storage_reserved"], -len(payload))
    assert_equal("cancel preserves used storage", quota_after_cancel["storage_used"], quota_before["storage_used"])
    assert_db_row_absent("cancel creates no file row", "SELECT id FROM files WHERE user_id = %s AND name = %s", (USER_ID, filename))
    assert_path_absent("temp upload directory cleaned after cancel", upload_temp_dir(upload_id))
    assert_path_absent("final blob absent after cancel", final_blob_path(file_hash))


def test_instant_upload_ref_count_and_accounting() -> None:
    log_section("Instant Upload Ref Count And Accounting")
    payload = f"domain-instant-{unique_name()}".encode()
    first_name = f"domain_instant_first_{unique_name()}.bin"
    second_name = f"domain_instant_second_{unique_name()}.bin"

    quota_before_first = user_quota()
    first_file_id, file_hash = upload_file(first_name, payload)
    first_file = file_row(first_file_id)
    content_id = int(first_file["content_id"])
    content_after_first = content_row(content_id)
    quota_after_first = user_quota()
    assert_equal("first upload creates content ref_count=1", int(content_after_first["ref_count"]), 1)
    assert_numeric_delta("first upload increases used storage", quota_before_first["storage_used"], quota_after_first["storage_used"], len(payload))

    quota_before_instant = user_quota()
    upload_id, _, body = upload_init(second_name, payload)
    quota_after_instant = user_quota()
    if json_field(body, "data.instant_upload") != "true":
        log_fail("second upload should use instant upload")
        print(body)
        print_summary()
    assert_equal("instant upload returns no upload_id", upload_id, "")

    second_file_id = json_field(body, "data.file.id")
    if not second_file_id:
        log_fail("instant upload returns file item id")
        print(body)
        print_summary()
    second_file = file_row(int(second_file_id))
    assert_equal("instant upload reuses content_id", int(second_file["content_id"]), content_id)

    content_after_instant = content_row(content_id)
    assert_equal("instant upload increments ref_count", int(content_after_instant["ref_count"]), 2)
    assert_numeric_delta(
        "instant upload increases used storage by logical file size",
        quota_before_instant["storage_used"],
        quota_after_instant["storage_used"],
        len(payload),
    )
    assert_equal("instant upload preserves storage_reserved", quota_after_instant["storage_reserved"], quota_before_instant["storage_reserved"])
    assert_path_exists("dedup final blob still exists", final_blob_path(file_hash))


def test_copy_file_and_folder_ref_counts_and_quota() -> None:
    log_section("Copy Ref Count And Quota Invariants")
    payload = f"domain-copy-{unique_name()}".encode()
    source_name = f"domain_copy_source_{unique_name()}.bin"
    source_file_id, _ = upload_file(source_name, payload)
    source_file = file_row(source_file_id)
    content_id = int(source_file["content_id"])
    content_before = content_row(content_id)
    quota_before_file_copy = user_quota()

    file_target_folder_id = create_folder(f"domain_copy_target_{unique_name()}")
    copy_data = copy_items([source_file_id], [], file_target_folder_id)
    for key in ("copied_count", "copied_file_count", "copied_folder_count", "new_files", "new_folders"):
        assert_equal(f"file copy response includes {key}", key in copy_data, True)
    assert_equal("file copy reports one copied file", int(copy_data["copied_file_count"]), 1)
    assert_equal("file copy reports zero copied folders", int(copy_data["copied_folder_count"]), 0)
    new_file_id = int(copy_data["new_files"][0]["new_id"])
    copied_file = file_row(new_file_id)
    quota_after_file_copy = user_quota()
    content_after_file_copy = content_row(content_id)

    assert_equal("file copy reuses content_id", int(copied_file["content_id"]), content_id)
    assert_equal("file copy increments ref_count", int(content_after_file_copy["ref_count"]), int(content_before["ref_count"]) + 1)
    assert_numeric_delta("file copy increases used storage by copied bytes", quota_before_file_copy["storage_used"], quota_after_file_copy["storage_used"], len(payload))

    parent_folder_id = create_folder(f"domain_copy_parent_{unique_name()}")
    child_payload = f"domain-folder-copy-{unique_name()}".encode()
    child_name = f"domain_folder_child_{unique_name()}.bin"
    child_file_id = upload_file(child_name, child_payload)[0]
    move_resp = fetch(
        "/api/file/move",
        method="PUT",
        headers=auth_headers(),
        json_body={"file_ids": [child_file_id], "folder_ids": [], "target_folder_id": parent_folder_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-move-child-for-folder-copy.json", move_resp.text)
    if move_resp.status_code != 200 or json_field(move_resp.text, "code") != "0":
        log_fail("move child file into folder failed")
        print(move_resp.text)
        print_summary()

    child_file = file_row(child_file_id)
    child_content_id = int(child_file["content_id"])
    child_content_before = content_row(child_content_id)
    quota_before_folder_copy = user_quota()
    folder_target_id = create_folder(f"domain_folder_copy_target_{unique_name()}")
    folder_copy_data = copy_items([], [parent_folder_id], folder_target_id)
    for key in ("copied_count", "copied_file_count", "copied_folder_count", "new_files", "new_folders"):
        assert_equal(f"folder copy response includes {key}", key in folder_copy_data, True)
    quota_after_folder_copy = user_quota()
    child_content_after = content_row(child_content_id)

    assert_equal("folder copy reports one copied folder", int(folder_copy_data["copied_folder_count"]), 1)
    assert_equal("folder copy reports one copied file", int(folder_copy_data["copied_file_count"]), 1)
    assert_equal("folder copy increments contained file ref_count", int(child_content_after["ref_count"]), int(child_content_before["ref_count"]) + 1)
    assert_numeric_delta("folder copy increases used storage by contained bytes", quota_before_folder_copy["storage_used"], quota_after_folder_copy["storage_used"], len(child_payload))


def test_trash_permanent_delete_invariants() -> None:
    log_section("Trash Permanent Delete Invariants")
    payload = f"domain-trash-{unique_name()}".encode()
    filename = f"domain_trash_{unique_name()}.bin"
    file_id, file_hash = upload_file(filename, payload)
    uploaded_file = file_row(file_id)
    content_id = int(uploaded_file["content_id"])
    content_before_delete = content_row(content_id)
    quota_before_soft_delete = user_quota()

    soft_delete_file(file_id)
    quota_after_soft_delete = user_quota()
    trash = trash_row_for_original(file_id)
    content_after_soft_delete = content_row(content_id)
    assert_equal("soft delete keeps storage_used", quota_after_soft_delete["storage_used"], quota_before_soft_delete["storage_used"])
    assert_equal("soft delete keeps content ref_count", int(content_after_soft_delete["ref_count"]), int(content_before_delete["ref_count"]))
    assert_path_exists("blob remains while item is in trash", final_blob_path(file_hash))

    permanent_delete_trash(int(trash["id"]))
    quota_after_permanent_delete = user_quota()
    content_after_permanent_delete = content_row(content_id)
    assert_numeric_delta("permanent delete releases used storage", quota_after_soft_delete["storage_used"], quota_after_permanent_delete["storage_used"], -len(payload))
    assert_equal("permanent delete decrements ref_count to zero", int(content_after_permanent_delete["ref_count"]), 0)
    assert_path_absent("zero-ref blob deleted after permanent delete", final_blob_path(file_hash))
    assert_db_row_absent("trash row removed after permanent delete", "SELECT id FROM trash WHERE id = %s", (int(trash["id"]),))


def test_expired_upload_cleanup_pending_trigger_documented() -> None:
    log_section("Expired Upload Cleanup Trigger Documentation")
    payload = f"domain-expire-{unique_name()}".encode()
    filename = f"domain_expire_{unique_name()}.bin"
    quota_before = user_quota()
    upload_id, _ = init_chunked_upload(filename, payload)
    quota_after_init = user_quota()
    assert_numeric_delta("expire fixture reserves storage", quota_before["storage_reserved"], quota_after_init["storage_reserved"], len(payload))

    affected = execute("UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' WHERE id = %s AND status = 0", (upload_id,))
    assert_equal("expire fixture marks upload task expired in DB", affected, 1)

    note = {
        "scenario": "expired upload cleanup",
        "status": "pending public/manual trigger",
        "upload_id": upload_id,
        "expected_invariants_when_trigger_exists": [
            "upload_tasks.status becomes 3",
            "users.storage_reserved decreases by reserved_bytes",
            "no files row is created",
            "temporary upload artifacts are removed",
        ],
    }
    save_evidence(f"{EVIDENCE_PREFIX}-expired-upload-trigger-pending.json", json.dumps(note, indent=2))
    log_pass("expired upload cleanup trigger gap documented with executable expired-task fixture")


def test_content_boundary_source_invariant() -> None:
    log_section("Content Boundary Source Invariant")
    project_root = Path(__file__).resolve().parents[2]
    content_service_source = project_root / "src" / "services" / "ContentService.cpp"
    forbidden_ref_count_hits: list[str] = []
    forbidden_lifecycle_lookup_hits: list[str] = []

    lifecycle_lookup_markers = [
        "SELECT id, mime_type " + "FROM file_contents " + "WHERE hash_md5",
        "FROM file_contents " + "WHERE hash_md5",
    ]

    for source_path in (project_root / "src").rglob("*.cpp"):
        text = source_path.read_text(encoding="utf-8")
        relative_path = str(source_path.relative_to(project_root))

        if "UPDATE file_contents SET ref_count" in text and source_path != content_service_source:
            forbidden_ref_count_hits.append(relative_path)

        if source_path != content_service_source and any(marker in text for marker in lifecycle_lookup_markers):
            forbidden_lifecycle_lookup_hits.append(relative_path)

    if forbidden_ref_count_hits:
        log_fail("file_contents.ref_count direct SQL exists outside ContentService: " + ", ".join(forbidden_ref_count_hits))
        print_summary()

    if forbidden_lifecycle_lookup_hits:
        log_fail("file_contents lifecycle md5 lookup SQL exists outside ContentService: " + ", ".join(forbidden_lifecycle_lookup_hits))
        print_summary()

    removed_repository_files = [
        project_root / "src" / "services" / "ContentRepository.hpp",
        project_root / "src" / "services" / "ContentRepository.cpp",
    ]
    existing_repository_files = [str(path.relative_to(project_root)) for path in removed_repository_files if path.exists()]
    if existing_repository_files:
        log_fail("ContentRepository duplicate primitive still exists: " + ", ".join(existing_repository_files))
        print_summary()

    trash_source = project_root / "src" / "services" / "TrashService.cpp"
    trash_text = trash_source.read_text(encoding="utf-8")
    cleanup_start = trash_text.find("auto TrashService::CleanupVerifiedZeroRefBlobs")
    verify_index = trash_text.find("VerifyZeroRefContents", cleanup_start)
    storage_path_index = trash_text.find("content.storage_path", cleanup_start)
    delete_index = trash_text.find("ParallelDeletePaths", cleanup_start)
    if cleanup_start == -1 or verify_index == -1 or storage_path_index == -1 or delete_index == -1:
        log_fail("Trash blob cleanup no longer exposes explicit zero-ref verification and deletion steps")
        print_summary()
    if not (cleanup_start < verify_index < storage_path_index < delete_index):
        log_fail("Trash blob cleanup must verify zero refs before collecting storage paths and deleting blobs")
        print_summary()

    log_pass("Content lifecycle SQL and zero-ref blob deletion checks remain centralized")


def main() -> None:
    print("==========================================")
    print("Backend Domain Extraction Invariant Tests")
    print("==========================================")
    print()

    ensure_server()

    global TOKEN, USER_ID
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)
    USER_ID = current_user_id()
    log_info(f"Using user_id={USER_ID}, chunk_size={configured_chunk_size()}, base_url={BASE_URL}")

    test_content_boundary_source_invariant()
    test_successful_upload_invariants()
    test_cancel_upload_invariants()
    test_instant_upload_ref_count_and_accounting()
    test_copy_file_and_folder_ref_counts_and_quota()
    test_trash_permanent_delete_invariants()
    test_expired_upload_cleanup_pending_trigger_documented()

    print_summary()


if __name__ == "__main__":
    main()

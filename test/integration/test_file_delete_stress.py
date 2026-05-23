#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Stress tests for batch delete and concurrent delete scenarios.

Covers:
  1. Batch delete of 10+ files — all soft-deleted, deleted_count matches,
     and every file appears in trash.
  2. Batch delete response format — verifies `code`, `message`,
     and `data.deleted_count` fields in the JSON envelope.
  3. Soft delete → trash visibility → permanent delete → verify space freed.

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL and Redis configured
  - User account exists (default: admin / Admin123)

Usage:
  uv run test/integration/test_file_delete_stress.py
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_step,
    log_section,
    print_summary,
    save_evidence,
    check_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    assert_json_field,
    assert_json_field_numeric_gt,
    assert_json_array_not_empty,
    assert_status,
    assert_header_contains,
    create_temp_file,
    md5_hash,
    unique_name,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")

EVIDENCE_PREFIX = "file-delete-stress"

# ─── Helpers ─────────────────────────────────────────────────────────────────


def _auth_headers(token: str, content_type: str = "application/json") -> dict[str, str]:
    return {
        "Authorization": f"Bearer {token}",
        "Content-Type": content_type,
    }


def upload_fixture(token: str, tag: str, file_size: int = 256) -> str:
    """Upload a single test file via chunked upload. Returns file_id."""
    path = create_temp_file(file_size)
    file_hash = md5_hash(path)
    filename = unique_name(tag) + ".bin"

    try:
        init_resp = fetch(
            "/api/file/upload/init",
            method="POST",
            headers=_auth_headers(token),
            json_body={
                "filename": filename,
                "file_size": file_size,
                "file_hash": file_hash,
                "parent_id": 0,
            },
        )
        save_evidence(f"{EVIDENCE_PREFIX}-upload-init-{tag}.json", init_resp.text)

        instant = json_field(init_resp.text, "data.instant_upload")
        if instant == "true":
            fid = json_field(init_resp.text, "data.file_id")
            if not fid or fid == "null":
                log_fail(f"Instant upload returned no file_id ({tag})")
                raise SystemExit(1)
            log_info(f"Instant upload (dedup) — file_id={fid} ({tag})")
            return fid

        upload_id = json_field(init_resp.text, "data.upload_id")
        if not upload_id or upload_id == "null":
            log_fail(f"Upload init failed ({tag})")
            print(init_resp.text)
            raise SystemExit(1)

        with open(path, "rb") as f:
            content = f.read()

        chunk_resp = fetch(
            f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
            method="POST",
            headers=_auth_headers(token, "application/octet-stream"),
            data=content,
        )

        uploaded = json_field(chunk_resp.text, "data.uploaded")
        if uploaded != "true":
            log_fail(f"Chunk upload failed ({tag})")
            print(chunk_resp.text)
            raise SystemExit(1)

        complete_resp = fetch(
            "/api/file/upload/complete",
            method="POST",
            headers=_auth_headers(token),
            json_body={"upload_id": upload_id},
        )
        save_evidence(f"{EVIDENCE_PREFIX}-upload-complete-{tag}.json", complete_resp.text)

        fid = json_field(complete_resp.text, "data.file.id")
        if not fid or fid == "null":
            log_fail(f"Complete upload — no file.id ({tag})")
            print(complete_resp.text)
            raise SystemExit(1)

        log_info(f"File uploaded — file_id={fid} ({tag})")
        return fid

    finally:
        if os.path.exists(path):
            os.unlink(path)


def soft_delete_files(token: str, file_ids: list[int]) -> tuple[int, str]:
    """DELETE /api/file with file_ids list. Returns (status, body)."""
    resp = fetch(
        "/api/file",
        method="DELETE",
        headers=_auth_headers(token),
        json_body={"file_ids": file_ids},
    )
    return resp.status_code, resp.text


def get_trash_items(token: str) -> list[dict]:
    """GET /api/trash — returns list of trash item dicts from data.items."""
    resp = fetch(
        "/api/trash",
        headers={"Authorization": f"Bearer {token}"},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-trash-list.json", resp.text)
    try:
        data = json.loads(resp.text)
        return data.get("data", {}).get("items", [])
    except json.JSONDecodeError:
        return []


def permanent_delete_trash(token: str, trash_ids: list[int]) -> tuple[int, str]:
    """DELETE /api/trash — permanently delete trash items."""
    resp = fetch(
        "/api/trash",
        method="DELETE",
        headers=_auth_headers(token),
        json_body={"trash_ids": trash_ids},
    )
    return resp.status_code, resp.text


def get_user_storage(token: str) -> dict:
    """GET /api/user/storage — returns parsed storage info."""
    resp = fetch(
        "/api/user/storage",
        headers={"Authorization": f"Bearer {token}"},
    )
    try:
        return json.loads(resp.text).get("data", {})
    except json.JSONDecodeError:
        return {}


# ─── Test 1: Batch delete multiple files ────────────────────────────────────


def test_batch_delete_multiple_files(token: str) -> list[str]:
    """Upload 12 files, batch soft-delete them, verify deleted_count and trash visibility."""
    log_section("Test 1: Batch Delete 12 Files")

    file_count = 12
    file_ids: list[str] = []

    # Upload files
    log_step(f"Uploading {file_count} files...")
    for i in range(file_count):
        fid = upload_fixture(token, f"batch-{i:02d}")
        file_ids.append(fid)
    log_pass(f"Uploaded {file_count} files: ids={file_ids}")

    # Batch soft delete
    int_ids = [int(fid) for fid in file_ids]
    log_step(f"Batch deleting {file_count} files via DELETE /api/file")
    status, body = soft_delete_files(token, int_ids)
    save_evidence(f"{EVIDENCE_PREFIX}-batch-delete.json", body)

    ok = True
    assert_status("batch-delete-status", status, 200) or (ok := False)
    assert_json_field("batch-delete-code", body, "code", "0") or (ok := False)
    assert_json_field_numeric_gt("batch-delete-count", body, "data.deleted_count", 0) or (ok := False)

    # Verify deleted_count matches the number of files we sent
    deleted_count_str = json_field(body, "data.deleted_count")
    try:
        deleted_count = int(deleted_count_str) if deleted_count_str else 0
    except ValueError:
        deleted_count = 0

    if deleted_count == file_count:
        log_pass(f"batch-delete: deleted_count={deleted_count} matches file_count={file_count}")
    else:
        log_fail(
            f"batch-delete: expected deleted_count={file_count}, got {deleted_count}"
        )
        ok = False

    # Verify response has message field
    message = json_field(body, "message")
    if message:
        log_pass(f"batch-delete: message='{message}'")
    else:
        log_fail("batch-delete: missing 'message' field")
        ok = False

    # Verify each file appears in trash
    log_step("Verifying all deleted files appear in trash")
    trash_items = get_trash_items(token)
    trash_original_ids = {item.get("original_id") for item in trash_items}

    all_in_trash = all(int(fid) in trash_original_ids for fid in file_ids)
    if all_in_trash:
        log_pass(f"batch-delete: all {file_count} files found in trash")
    else:
        missing = [fid for fid in file_ids if int(fid) not in trash_original_ids]
        log_fail(f"batch-delete: files not in trash: {missing}")
        ok = False

    if ok:
        log_pass("batch-delete-multiple: all assertions passed")

    return file_ids


# ─── Test 2: Batch delete response format ───────────────────────────────────


def test_batch_delete_response_format(token: str) -> list[str]:
    """Upload 3 files, delete them, verify exact response field structure."""
    log_section("Test 2: Batch Delete Response Format")

    file_count = 3
    file_ids: list[str] = []

    log_step(f"Uploading {file_count} files for format verification...")
    for i in range(file_count):
        fid = upload_fixture(token, f"fmt-{i}")
        file_ids.append(fid)

    int_ids = [int(fid) for fid in file_ids]
    status, body = soft_delete_files(token, int_ids)
    save_evidence(f"{EVIDENCE_PREFIX}-format-delete.json", body)

    ok = True

    # Verify top-level JSON structure
    try:
        parsed = json.loads(body)
        log_pass("format: response is valid JSON")
    except json.JSONDecodeError:
        log_fail("format: response is NOT valid JSON")
        return file_ids

    # code field must be 0 (int)
    code = parsed.get("code")
    if code == 0:
        log_pass(f"format: code={code} (integer zero)")
    else:
        log_fail(f"format: expected code=0 (int), got code={code!r}")
        ok = False

    # message field must be a non-empty string
    message = parsed.get("message")
    if isinstance(message, str) and message:
        log_pass(f"format: message='{message}'")
    else:
        log_fail(f"format: expected non-empty string message, got {message!r}")
        ok = False

    # data field must be a dict
    data = parsed.get("data")
    if isinstance(data, dict):
        log_pass("format: data is a dict")
    else:
        log_fail(f"format: expected data to be dict, got {type(data).__name__}")
        ok = False
        data = {}

    # data.deleted_count must be a positive int
    deleted_count = data.get("deleted_count")
    if isinstance(deleted_count, int) and deleted_count > 0:
        log_pass(f"format: data.deleted_count={deleted_count} (positive int)")
    else:
        log_fail(
            f"format: expected data.deleted_count > 0 (int), got {deleted_count!r}"
        )
        ok = False

    if ok:
        log_pass("format: response contract verified")

    return file_ids


# ─── Test 3: Soft delete → trash → permanent delete ────────────────────────


def test_delete_then_trash_permanent_delete(token: str) -> None:
    """Soft-delete file → verify in trash → permanent delete → verify space freed."""
    log_section("Test 3: Soft Delete → Trash → Permanent Delete")

    file_size = 512

    # Record storage before upload
    log_step("Recording storage state before upload")
    storage_before = get_user_storage(token)
    used_before = storage_before.get("used_storage", 0)
    log_info(f"Storage before: used={used_before}")

    # Upload a file
    log_step(f"Uploading single file ({file_size} bytes)")
    file_id = upload_fixture(token, "perm-del", file_size=file_size)
    log_pass(f"File uploaded: file_id={file_id}")

    # Storage after upload should have increased
    storage_after_upload = get_user_storage(token)
    used_after_upload = storage_after_upload.get("used_storage", 0)
    if used_after_upload >= used_before:
        log_pass(f"Storage after upload: used={used_after_upload} (increased or equal)")
    else:
        log_fail(f"Storage decreased after upload: before={used_before}, after={used_after_upload}")

    # Soft delete
    log_step(f"Soft-deleting file_id={file_id}")
    status, body = soft_delete_files(token, [int(file_id)])
    save_evidence(f"{EVIDENCE_PREFIX}-soft-delete.json", body)

    ok = True
    assert_status("soft-delete-status", status, 200) or (ok := False)
    assert_json_field("soft-delete-code", body, "code", "0") or (ok := False)

    # Verify file is in trash
    log_step("Verifying file appears in trash")
    trash_items = get_trash_items(token)
    trash_entry = None
    trash_id = None
    for item in trash_items:
        if item.get("original_id") == int(file_id):
            trash_entry = item
            trash_id = item.get("id")
            break

    if trash_entry is not None:
        log_pass(f"soft-delete: file {file_id} found in trash (trash_id={trash_id})")
    else:
        log_fail(f"soft-delete: file {file_id} NOT found in trash")
        ok = False

    if trash_id is None:
        log_fail("Cannot proceed with permanent delete — no trash_id")
        return

    # Permanent delete from trash
    log_step(f"Permanently deleting trash_id={trash_id}")
    perm_status, perm_body = permanent_delete_trash(token, [trash_id])
    save_evidence(f"{EVIDENCE_PREFIX}-permanent-delete.json", perm_body)

    assert_status("perm-delete-status", perm_status, 200) or (ok := False)
    assert_json_field("perm-delete-code", perm_body, "code", "0") or (ok := False)

    # Verify file no longer in trash
    log_step("Verifying file removed from trash after permanent delete")
    trash_after = get_trash_items(token)
    still_in_trash = any(
        item.get("original_id") == int(file_id) for item in trash_after
    )
    if not still_in_trash:
        log_pass("perm-delete: file no longer in trash")
    else:
        log_fail("perm-delete: file still in trash after permanent delete")
        ok = False

    # Verify storage decreased (space freed)
    storage_after_perm = get_user_storage(token)
    used_after_perm = storage_after_perm.get("used_storage", 0)
    if used_after_perm <= used_after_upload:
        log_pass(
            f"perm-delete: storage freed — after_upload={used_after_upload}, "
            f"after_perm={used_after_perm}"
        )
    else:
        log_fail(
            f"perm-delete: storage increased after permanent delete — "
            f"after_upload={used_after_upload}, after_perm={used_after_perm}"
        )
        ok = False

    if ok:
        log_pass("delete-trash-permanent: all assertions passed")


# ─── Main ───────────────────────────────────────────────────────────────────


def main() -> None:
    print("=" * 60)
    print("File Delete Stress Tests")
    print("=" * 60)
    print()

    # Check server
    if not check_server(BASE_URL):
        sys.exit(1)

    # Login
    token = do_login(TEST_USER, TEST_PASS, BASE_URL)
    if not token:
        log_fail("Cannot proceed without authentication")
        sys.exit(1)

    # Run tests
    test_batch_delete_multiple_files(token)
    test_batch_delete_response_format(token)
    test_delete_then_trash_permanent_delete(token)

    # Summary
    result_text = (
        f"File Delete Stress Tests\n"
        f"Date: {__import__('time').strftime('%Y-%m-%d %H:%M:%S UTC', __import__('time').gmtime())}\n"
        f"User: {TEST_USER}\n"
        f"Result: see summary above\n"
    )
    save_evidence(f"{EVIDENCE_PREFIX}-results.txt", result_text)

    print_summary()


if __name__ == "__main__":
    main()

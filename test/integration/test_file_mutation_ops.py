#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for file mutation operations: rename, move, upload cancel.

Covers:
  1. Upload a unique file (with instant_upload / dedup handling)
  2. Rename file — PUT /api/file/{file_id}/rename
  3. Verify rename — GET /api/file/{file_id}
  4. Create folder for move — POST /api/folder/create
  5. Move file — PUT /api/file/move
  6. Verify move — GET /api/file/{file_id}
  7. Upload cancel — DELETE /api/file/upload/{upload_id}
  8. Cancel prevents completion — POST /api/file/upload/complete with canceled upload_id

Prerequisites:
  - Server running on localhost:8080
  - MySQL database configured
  - Redis configured
  - User account exists (default: admin / Admin123)

Usage:
  uv run test/integration/test_file_mutation_ops.py
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_step,
    print_summary,
    save_evidence,
    check_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    assert_status,
    assert_json_field,
    assert_json_field_numeric_gt,
    create_temp_file,
    md5_hash,
    unique_name,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")

TOKEN = ""
FILE_ID = ""
FOLDER_ID = ""
MOVE_FOLDER_NAME = ""
CANCELED_UPLOAD_ID = ""


# ─── Helpers ─────────────────────────────────────────────────────────────────


def upload_fixture(token: str, file_size: int = 256) -> str:
    """Upload a test fixture file. Returns file_id."""
    log_step("Uploading test fixture file...")

    path = create_temp_file(file_size)
    file_hash = md5_hash(path)
    file_name = os.path.basename(path)

    # Init upload
    init_resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": file_name,
            "file_size": file_size,
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    save_evidence("mutation-ops-upload-init.json", init_resp.text)

    instant = json_field(init_resp.text, "data.instant_upload")

    if instant == "true":
        fid = json_field(init_resp.text, "data.file_id")
        if not fid or fid == "null":
            log_fail("Instant upload but no file_id returned")
            print(init_resp.text)
            os.unlink(path)
            raise SystemExit(1)
        log_info(f"Instant upload (dedup) — file_id={fid}")
        os.unlink(path)
        return fid

    upload_id = json_field(init_resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init upload failed — no upload_id")
        print(init_resp.text)
        os.unlink(path)
        raise SystemExit(1)

    # Upload chunk (single chunk = whole file)
    with open(path, "rb") as f:
        content = f.read()

    chunk_resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/octet-stream",
        },
        data=content,
    )

    uploaded = json_field(chunk_resp.text, "data.uploaded")
    if uploaded != "true":
        log_fail("Upload chunk failed")
        print(chunk_resp.text)
        os.unlink(path)
        raise SystemExit(1)
    save_evidence("mutation-ops-upload-chunk.json", chunk_resp.text)

    # Complete upload
    complete_resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )

    fid = json_field(complete_resp.text, "data.file.id")
    if not fid or fid == "null":
        log_fail("Complete upload — no file.id")
        print(complete_resp.text)
        os.unlink(path)
        raise SystemExit(1)
    save_evidence("mutation-ops-upload-complete.json", complete_resp.text)

    os.unlink(path)
    log_pass(f"File uploaded — file_id={fid}")
    return fid


# ─── Test 1: Rename file ────────────────────────────────────────────────────


def test_rename(file_id: str, token: str) -> None:
    global FILE_ID
    log_step("Test 1: Rename file")

    new_name = f"renamed_{os.getpid()}.txt"
    resp = fetch(
        f"/api/file/{file_id}/rename",
        method="PUT",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"new_name": new_name},
    )

    save_evidence("mutation-ops-rename.json", resp.text)

    ok = True
    assert_status("rename", resp.status_code, 200) or (ok := False)
    assert_json_field("rename", resp.text, "code", "0") or (ok := False)

    if ok:
        FILE_ID = file_id
        log_pass(
            f"rename: file renamed to '{new_name}' (HTTP {resp.status_code}, code=0)"
        )


# ─── Test 2: Verify rename ──────────────────────────────────────────────────


def test_verify_rename(file_id: str, token: str) -> None:
    log_step("Test 2: Verify rename via GET /api/file/{file_id}")

    resp = fetch(
        f"/api/file/{file_id}",
        headers={"Authorization": f"Bearer {token}"},
    )

    save_evidence("mutation-ops-verify-rename.json", resp.text)

    actual_name = json_field(resp.text, "data.name")
    expected_name = f"renamed_{os.getpid()}.txt"

    if actual_name == expected_name:
        log_pass(f"verify-rename: name = '{actual_name}'")
    else:
        log_fail(f"verify-rename: expected name='{expected_name}', got='{actual_name}'")


# ─── Test 3: Create folder for move ─────────────────────────────────────────


def test_create_folder(token: str) -> str:
    global FOLDER_ID, MOVE_FOLDER_NAME
    log_step("Test 3: Create folder for move target")

    folder_name = f"move_target_{unique_name()}"
    MOVE_FOLDER_NAME = folder_name
    resp = fetch(
        "/api/folder/create",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"name": folder_name, "parent_id": 0},
    )

    save_evidence("mutation-ops-create-folder.json", resp.text)

    folder_id = json_field(resp.text, "data.id")
    if not folder_id or folder_id == "null":
        log_fail("create-folder: no folder id returned")
        print(resp.text)
        raise SystemExit(1)

    ok = True
    assert_status("create-folder", resp.status_code, 200) or (ok := False)
    assert_json_field("create-folder", resp.text, "code", "0") or (ok := False)

    if ok:
        FOLDER_ID = folder_id
        log_pass(f"create-folder: folder_id={folder_id}, name='{folder_name}'")

    return folder_id


# ─── Test 4: Move file ──────────────────────────────────────────────────────


def test_move(file_id: str, folder_id: str, token: str) -> None:
    log_step("Test 4: Move file to folder")

    resp = fetch(
        "/api/file/move",
        method="PUT",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={
            "file_ids": [int(file_id)],
            "folder_ids": [],
            "target_folder_id": int(folder_id),
        },
    )

    save_evidence("mutation-ops-move.json", resp.text)

    ok = True
    assert_status("move", resp.status_code, 200) or (ok := False)
    assert_json_field("move", resp.text, "code", "0") or (ok := False)
    assert_json_field_numeric_gt("move", resp.text, "data.moved_count", 0) or (
        ok := False
    )

    if ok:
        log_pass(f"move: file moved to folder_id={folder_id}")


# ─── Test 5: Verify move ────────────────────────────────────────────────────


def test_verify_move(file_id: str, folder_id: str, token: str) -> None:
    log_step("Test 5: Verify move via GET /api/file/{file_id}")

    resp = fetch(
        f"/api/file/{file_id}",
        headers={"Authorization": f"Bearer {token}"},
    )

    save_evidence("mutation-ops-verify-move.json", resp.text)

    actual_parent = json_field(resp.text, "data.parent_id")
    actual_name = json_field(resp.text, "data.name")
    actual_path = json_field(resp.text, "data.path")
    expected_path = f"/{MOVE_FOLDER_NAME}/{actual_name}"

    ok = True
    if int(actual_parent) == int(folder_id):
        log_pass(
            f"verify-move: parent_id = {actual_parent} (matches folder_id={folder_id})"
        )
    else:
        ok = False
        log_fail(f"verify-move: expected parent_id={folder_id}, got='{actual_parent}'")

    if actual_path == expected_path:
        log_pass(f"verify-move: path = '{actual_path}'")
    else:
        ok = False
        log_fail(f"verify-move: expected path='{expected_path}', got='{actual_path}'")

    if not ok:
        print(resp.text)


# ─── Test 6: Upload cancel ──────────────────────────────────────────────────


def test_upload_cancel(token: str) -> str:
    """Init a new upload, upload a chunk, then cancel. Returns canceled upload_id or ''."""
    global CANCELED_UPLOAD_ID
    log_step("Test 6: Init a new upload, then cancel it")

    # Use a different size to avoid instant upload (dedup)
    path = create_temp_file(128)
    file_hash = md5_hash(path)
    file_name = os.path.basename(path)

    # Init upload
    init_resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": file_name,
            "file_size": 128,
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    save_evidence("mutation-ops-cancel-init.json", init_resp.text)

    instant = json_field(init_resp.text, "data.instant_upload")
    if instant == "true":
        log_info("cancel: instant_upload=true (dedup hit), skipping cancel test")
        os.unlink(path)
        return ""

    upload_id = json_field(init_resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("cancel: failed to get upload_id for cancel test")
        print(init_resp.text)
        os.unlink(path)
        raise SystemExit(1)

    CANCELED_UPLOAD_ID = upload_id
    log_info(f"cancel: upload_id={upload_id}")

    # Upload a chunk so the upload session is active
    with open(path, "rb") as f:
        content = f.read()

    chunk_resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/octet-stream",
        },
        data=content,
    )

    uploaded = json_field(chunk_resp.text, "data.uploaded")
    if uploaded != "true":
        log_fail("cancel: chunk upload failed")
        print(chunk_resp.text)
        os.unlink(path)
        raise SystemExit(1)

    os.unlink(path)

    # Now cancel the upload
    cancel_resp = fetch(
        f"/api/file/upload/{upload_id}",
        method="DELETE",
        headers={"Authorization": f"Bearer {token}"},
    )

    save_evidence("mutation-ops-cancel-delete.json", cancel_resp.text)

    ok = True
    assert_status("cancel", cancel_resp.status_code, 200) or (ok := False)
    assert_json_field("cancel", cancel_resp.text, "code", "0") or (ok := False)

    if ok:
        log_pass(f"cancel: upload canceled (upload_id={upload_id})")

    return upload_id


# ─── Test 7: Cancel prevents completion ──────────────────────────────────────


def test_cancel_prevents_completion(canceled_upload_id: str, token: str) -> None:
    log_step("Test 7: Verify canceled upload cannot be completed")

    if not canceled_upload_id:
        log_info("cancel-prevents-completion: skipped (no canceled upload_id)")
        return

    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": canceled_upload_id},
    )

    save_evidence("mutation-ops-cancel-complete-attempt.json", resp.text)

    # Expect: HTTP non-200 OR code != 0
    code = json_field(resp.text, "code")

    if resp.status_code != 200 or code != "0":
        log_pass(
            f"cancel-prevents-completion: complete rejected (HTTP={resp.status_code}, code={code})"
        )
    else:
        log_fail(
            f"cancel-prevents-completion: expected rejection, got HTTP={resp.status_code}, code={code}"
        )
        print(resp.text)


# ─── Main ───────────────────────────────────────────────────────────────────


def main() -> None:
    print("==========================================")
    print("File Mutation Ops Integration Tests")
    print("==========================================")
    print()

    check_server() or sys.exit(1)

    # Setup
    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        sys.exit(1)

    file_id = upload_fixture(token)

    print()
    print("==========================================")
    print("Running Mutation Tests")
    print("==========================================")
    print()

    # Test 1: Rename file
    test_rename(file_id, token)

    # Test 2: Verify rename
    test_verify_rename(file_id, token)

    # Test 3: Create folder for move target
    folder_id = test_create_folder(token)

    # Test 4: Move file into folder
    test_move(file_id, folder_id, token)

    # Test 5: Verify move
    test_verify_move(file_id, folder_id, token)

    # Test 6: Upload cancel
    canceled_id = test_upload_cancel(token)

    # Test 7: Cancel prevents completion
    test_cancel_prevents_completion(canceled_id, token)

    # Summary
    print()
    print_summary()


if __name__ == "__main__":
    main()

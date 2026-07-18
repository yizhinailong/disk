#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for copy/delete/trash atomicity at API boundary.

Covers:
  - Happy-path copy: upload → copy to dedicated folder → verify copied_count > 0 and new_files present
  - Happy-path delete: delete original → verify deleted_count > 0
  - Trash visibility: GET /api/trash → verify deleted file appears in trash list
  - Copy non-existent file_ids: POST /api/file/copy with invalid IDs → verify copied_count=0 (vacuous success)
  - Delete non-existent file_ids: DELETE /api/file with invalid IDs → verify code=50005 (FileNotFound error)
  - Copy then delete copied file: independent operations succeed

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured
  - Redis configured
  - User account exists (default: admin / Admin123)

Usage:
  uv run test/integration/test_copy_delete_atomicity.py
"""

import json
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
    ensure_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    assert_json_field,
    assert_json_field_numeric_gt,
    assert_json_array_not_empty,
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
FILE_SIZE = 0
FILE_HASH = ""
COPIED_FILE_ID = ""
DEST_FOLDER_ID = ""

EVIDENCE_PREFIX = "copy-delete-atomicity"


# ─── Helpers ─────────────────────────────────────────────────────────────────


def upload_fixture(
    token: str, file_size: int = 256, filename: str | None = None
) -> str:
    """Upload a test file via chunked upload flow. Returns file_id."""
    if filename is None:
        filename = f"{unique_name('atomicity')}.bin"

    path = create_temp_file(file_size)
    file_hash = md5_hash(path)

    # Init upload
    init_resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": filename,
            "file_size": file_size,
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )

    instant = json_field(init_resp.text, "data.instant_upload")

    if instant == "true":
        fid = json_field(init_resp.text, "data.file_id")
        if not fid or fid == "null":
            log_fail("Instant upload but no file_id returned")
            print(init_resp.text)
            os.unlink(path)
            raise SystemExit(1)
        log_info(f"Instant upload (dedup) — file_id={fid}")
        save_evidence(f"{EVIDENCE_PREFIX}-upload-init.json", init_resp.text)
        os.unlink(path)
        return fid

    upload_id = json_field(init_resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init upload failed")
        print(init_resp.text)
        os.unlink(path)
        raise SystemExit(1)
    save_evidence(f"{EVIDENCE_PREFIX}-upload-init.json", init_resp.text)

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
    save_evidence(f"{EVIDENCE_PREFIX}-upload-chunk.json", chunk_resp.text)

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
    save_evidence(f"{EVIDENCE_PREFIX}-upload-complete.json", complete_resp.text)

    os.unlink(path)
    log_pass(f"File uploaded — file_id={fid}, size={file_size}")
    return fid


# ─── Test 1: Happy-path copy ────────────────────────────────────────────────


def test_happy_copy() -> None:
    global COPIED_FILE_ID

    log_step("Test: Copy file to dedicated destination folder")

    resp = fetch(
        "/api/file/copy",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"file_ids": [int(FILE_ID)], "target_folder_id": int(DEST_FOLDER_ID)},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-copy-happy.json", resp.text)

    ok = True
    assert_json_field("copy-happy", resp.text, "code", "0") or (ok := False)
    assert_json_field_numeric_gt("copy-happy", resp.text, "data.copied_count", 0) or (
        ok := False
    )
    assert_json_array_not_empty("copy-happy", resp.text, "data.new_files") or (
        ok := False
    )

    # Extract the new file ID for later tests
    COPIED_FILE_ID = json_field(resp.text, "data.new_files.0.new_id")
    if COPIED_FILE_ID and COPIED_FILE_ID != "null":
        log_info(f"Copied file ID: {COPIED_FILE_ID}")
    else:
        log_fail("copy-happy: failed to extract new_file_id")
        ok = False

    if ok:
        log_pass("copy-happy: copy operation successful")


# ─── Test 2: Happy-path delete ──────────────────────────────────────────────


def test_happy_delete() -> None:
    log_step("Test: Delete original file")

    resp = fetch(
        "/api/file",
        method="DELETE",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"file_ids": [int(FILE_ID)]},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-delete-happy.json", resp.text)

    ok = True
    assert_json_field("delete-happy", resp.text, "code", "0") or (ok := False)
    assert_json_field_numeric_gt(
        "delete-happy", resp.text, "data.deleted_count", 0
    ) or (ok := False)

    if ok:
        log_pass("delete-happy: delete operation successful")


# ─── Test 3: Trash visibility ───────────────────────────────────────────────


def test_trash_visibility() -> None:
    log_step("Test: GET /api/trash → verify deleted file appears")

    resp = fetch(
        "/api/trash",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-trash-list.json", resp.text)

    ok = True
    assert_json_field("trash-visibility", resp.text, "code", "0") or (ok := False)

    # Verify the deleted file appears in trash items
    found_in_trash = False
    try:
        data = json.loads(resp.text)
        items = data.get("data", {}).get("items", [])
        file_id_int = int(FILE_ID)
        for item in items:
            if item.get("original_id") == file_id_int:
                found_in_trash = True
                break
    except Exception:
        pass

    if found_in_trash:
        log_pass(f"trash-visibility: deleted file (id={FILE_ID}) found in trash")
    else:
        log_fail(f"trash-visibility: deleted file (id={FILE_ID}) NOT found in trash")
        ok = False

    total = json_field(resp.text, "data.total")
    log_info(f"Trash total items: {total}")

    if ok:
        log_pass("trash-visibility: trash list verification successful")


# ─── Test 4: Copy non-existent file IDs ─────────────────────────────────────


def test_copy_nonexistent() -> None:
    log_step("Test: Copy non-existent file_ids → verify zero-copy success (copied_count=0)")

    resp = fetch(
        "/api/file/copy",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"file_ids": [99999999], "target_folder_id": int(DEST_FOLDER_ID)},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-copy-nonexistent.json", resp.text)

    ok = True
    # Copy API returns success (code=0) with copied_count=0 for non-existent IDs;
    # unlike delete, the copy endpoint treats zero matches as a vacuous success.
    assert_json_field("copy-nonexistent", resp.text, "code", "0") or (ok := False)
    assert_json_field("copy-nonexistent", resp.text, "data.copied_count", "0") or (
        ok := False
    )

    if ok:
        log_pass("copy-nonexistent: vacuous success with copied_count=0")


# ─── Test 5: Delete non-existent file IDs ───────────────────────────────────


def test_delete_nonexistent() -> None:
    log_step("Test: Delete non-existent file_ids → verify FileNotFound error (code=50005)")

    resp = fetch(
        "/api/file",
        method="DELETE",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"file_ids": [99999999]},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-delete-nonexistent.json", resp.text)

    ok = True
    # After backend hardening (Task 3), zero-deletion returns a structured
    # FileNotFound error (code=50005) instead of fake success with deleted_count=0.
    assert_json_field("delete-nonexistent", resp.text, "code", "50005") or (
        ok := False
    )

    if ok:
        log_pass("delete-nonexistent: FileNotFound error returned as expected")


# ─── Test 6: Copy then delete copied file ───────────────────────────────────


def test_copy_then_delete() -> None:
    log_step("Test: Copy another file, then delete the copy")

    # Upload a second file for this test
    file_id2 = upload_fixture(
        TOKEN,
        file_size=128,
        filename=f"{unique_name('atomicity_test2')}.bin",
    )

    if not file_id2:
        log_fail("copy-then-delete: failed to upload second file")
        return

    log_info(f"Second file uploaded: file_id={file_id2}")

    # Copy the second file
    copy_resp = fetch(
        "/api/file/copy",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "file_ids": [int(file_id2)],
            "target_folder_id": int(DEST_FOLDER_ID),
        },
    )

    save_evidence(f"{EVIDENCE_PREFIX}-copy-then-delete-copy.json", copy_resp.text)

    ok = True
    assert_json_field("copy-then-delete-copy", copy_resp.text, "code", "0") or (
        ok := False
    )
    assert_json_field_numeric_gt(
        "copy-then-delete-copy", copy_resp.text, "data.copied_count", 0
    ) or (ok := False)

    copied_file_id2 = json_field(copy_resp.text, "data.new_files.0.new_id")
    if not copied_file_id2 or copied_file_id2 == "null":
        log_fail("copy-then-delete: failed to get copied file ID")
        return

    log_info(f"Copied second file: copied_id={copied_file_id2}")

    # Delete the copied file
    delete_resp = fetch(
        "/api/file",
        method="DELETE",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"file_ids": [int(copied_file_id2)]},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-copy-then-delete-delete.json", delete_resp.text)

    assert_json_field("copy-then-delete-delete", delete_resp.text, "code", "0") or (
        ok := False
    )
    assert_json_field_numeric_gt(
        "copy-then-delete-delete", delete_resp.text, "data.deleted_count", 0
    ) or (ok := False)

    if ok:
        log_pass(
            "copy-then-delete: both copy and delete operations succeeded independently"
        )


# ─── Main ───────────────────────────────────────────────────────────────────


def main() -> None:
    global TOKEN, FILE_ID, DEST_FOLDER_ID

    print("==========================================")
    print("Copy/Delete Atomicity Integration Tests")
    print("==========================================")
    print()

    ensure_server()

    # Setup
    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        sys.exit(1)
    TOKEN = token

    FILE_ID = upload_fixture(TOKEN)

    # Create a dedicated destination folder for copy operations
    dest_folder_name = f"copy_dest_{unique_name()}"
    folder_resp = fetch(
        "/api/folder/create",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"name": dest_folder_name, "parent_id": 0},
    )
    DEST_FOLDER_ID = json_field(folder_resp.text, "data.id")
    if not DEST_FOLDER_ID or DEST_FOLDER_ID == "null":
        log_fail(f"Failed to create destination folder: {folder_resp.text}")
        sys.exit(1)
    log_pass(
        f"Created destination folder: id={DEST_FOLDER_ID}, name='{dest_folder_name}'"
    )

    print()
    print("==========================================")
    print("Running Atomicity Tests")
    print("==========================================")
    print()

    # Test 1: Happy-path copy
    test_happy_copy()

    # Test 2: Happy-path delete
    test_happy_delete()

    # Test 3: Trash visibility
    test_trash_visibility()

    # Test 4: Copy non-existent file IDs
    test_copy_nonexistent()

    # Test 5: Delete non-existent file IDs
    test_delete_nonexistent()

    # Test 6: Copy then delete copied file
    test_copy_then_delete()

    # Summary
    print()
    print_summary()


if __name__ == "__main__":
    main()

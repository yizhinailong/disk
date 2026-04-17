#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for file upload flow APIs (4.1-4.4).

Prerequisites:
  - Server running on localhost:8080
  - MySQL database configured
  - Redis configured
  - User account for testing

Usage:
  uv run test/integration/test_upload_flow.py
"""

import hashlib
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    print_summary,
    save_evidence,
    check_server,
    cleanup,
    do_login,
    json_field,
    fetch,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")

TOKEN = ""
HAPPY_FILENAME = f"happy_path_test_{os.getpid()}.bin"
MISSING_FILENAME = f"missing_chunk_test_{os.getpid()}.bin"


def _configured_chunk_size() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    config_path = os.path.join(repo_root, "config.json")
    try:
        with open(config_path, encoding="utf-8") as f:
            config = json.load(f)
        return int(
            config.get("custom_config", {})
            .get("disk", {})
            .get("chunk_size", 5 * 1024 * 1024)
        )
    except Exception:
        return 5 * 1024 * 1024


CHUNK_SIZE = _configured_chunk_size()


# ─── Helpers ─────────────────────────────────────────────────────────────────


def _md5_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


# ─── Test 1: Happy path upload ──────────────────────────────────────────────


def test_happy_path_upload():
    log_info("Testing Happy Path Upload Flow...")

    chunk_0_data = b"A" * CHUNK_SIZE
    chunk_1_data = b"B" * 17
    full_data = chunk_0_data + chunk_1_data

    chunk_0_hash = _md5_bytes(chunk_0_data)
    chunk_1_hash = _md5_bytes(chunk_1_data)
    file_hash = _md5_bytes(full_data)
    file_size = len(full_data)

    log_info(f"File hash: {file_hash} (size: {file_size})")
    log_info(f"Chunk 0 hash: {chunk_0_hash}")
    log_info(f"Chunk 1 hash: {chunk_1_hash}")

    # Init upload
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": HAPPY_FILENAME,
            "file_size": file_size,
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )

    upload_id = json_field(resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init Upload (happy path) - failed to get upload_id")
        print(resp.text)
        return

    log_pass(f"Init Upload (happy path) - upload_id: {upload_id}")
    save_evidence("init-upload-normal.json", resp.text)

    # Upload chunk 0
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_0_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/octet-stream",
        },
        data=chunk_0_data,
    )

    uploaded_0 = json_field(resp.text, "data.uploaded")
    if uploaded_0 != "true":
        log_fail("Upload Chunk 0 (happy path)")
        print(resp.text)
        return

    log_pass("Upload Chunk 0 (happy path)")
    save_evidence("upload-chunk-0.json", resp.text)

    # Upload chunk 1
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=1&chunk_hash={chunk_1_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/octet-stream",
        },
        data=chunk_1_data,
    )

    uploaded_1 = json_field(resp.text, "data.uploaded")
    if uploaded_1 != "true":
        log_fail("Upload Chunk 1 (happy path)")
        print(resp.text)
        return

    log_pass("Upload Chunk 1 (happy path)")
    save_evidence("upload-chunk-1.json", resp.text)

    # Complete upload
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )

    file_id = json_field(resp.text, "data.file.id")
    file_name = json_field(resp.text, "data.file.name")
    returned_hash = json_field(resp.text, "data.file.hash")

    if not file_id or file_id == "null":
        log_fail("Complete Upload (happy path) - no file_id returned")
        print(resp.text)
        return

    if returned_hash != file_hash:
        log_fail(
            f"Complete Upload (happy path) - hash mismatch: expected {file_hash}, got {returned_hash}"
        )
        print(resp.text)
        return

    if file_name != HAPPY_FILENAME:
        log_fail(
            f"Complete Upload (happy path) - filename mismatch: expected {HAPPY_FILENAME}, got {file_name}"
        )
        print(resp.text)
        return

    log_pass(f"Complete Upload (happy path) - file_id: {file_id}, hash verified")
    save_evidence("complete-upload-success.json", resp.text)


# ─── Test 2: Missing chunk upload ───────────────────────────────────────────


def test_missing_chunk_upload():
    log_info("Testing Missing Chunk Upload (failure path)...")

    chunk_0_data = b"M" * CHUNK_SIZE
    chunk_1_data = b"N" * CHUNK_SIZE
    chunk_2_data = b"O" * 17
    full_data = chunk_0_data + chunk_1_data + chunk_2_data

    chunk_0_hash = _md5_bytes(chunk_0_data)
    chunk_1_hash = _md5_bytes(chunk_1_data)
    file_hash = _md5_bytes(full_data)
    file_size = len(full_data)

    log_info(f"File hash: {file_hash} (size: {file_size})")

    # Init upload
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": MISSING_FILENAME,
            "file_size": file_size,
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )

    upload_id = json_field(resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init Upload (missing chunk) - failed to get upload_id")
        print(resp.text)
        return

    log_pass(f"Init Upload (missing chunk) - upload_id: {upload_id}")
    save_evidence("init-upload-missing.json", resp.text)

    # Upload chunk 0 only
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_0_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/octet-stream",
        },
        data=chunk_0_data,
    )

    uploaded_0 = json_field(resp.text, "data.uploaded")
    if uploaded_0 != "true":
        log_fail("Upload Chunk 0 (missing chunk)")
        print(resp.text)
        return

    log_pass("Upload Chunk 0 (missing chunk) - deliberately not uploading chunk 1")
    save_evidence("upload-chunk-missing-0.json", resp.text)

    # Attempt complete — should fail
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )

    code = json_field(resp.text, "code")
    message = json_field(resp.text, "message")

    if code == "0" or not code or code == "null":
        log_fail(
            f"Complete Upload (missing chunk) - expected non-zero code, got: {code}"
        )
        print(resp.text)
        return

    if "Not all chunks uploaded" not in message:
        log_fail(
            f"Complete Upload (missing chunk) - expected 'Not all chunks uploaded' in message, got: {message}"
        )
        print(resp.text)
        return

    log_pass(
        f"Complete Upload (missing chunk) - correctly failed with code {code} and message: {message}"
    )
    save_evidence("complete-upload-missing-chunk.json", resp.text)


# ─── Test 3: Quota exceeded ─────────────────────────────────────────────────


def test_init_upload_quota():
    log_info("Testing Init Upload (quota exceeded)...")

    # Use a valid 32-char MD5 hash for quota test
    quota_hash = "a1c7e6486f5811f4e23e6c696c0d6363"

    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": "huge_file.pdf",
            "file_size": 999999999999,
            "file_hash": quota_hash,
            "parent_id": 0,
        },
    )

    code = json_field(resp.text, "code")

    if code == "50004":
        log_pass(f"Init Upload (quota exceeded) - code: {code}")
        save_evidence("init-upload-quota.json", resp.text)
    else:
        log_fail(f"Init Upload (quota exceeded) - expected code 50004, got: {code}")
        print(resp.text)


# ─── Main ───────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("File Upload Flow Integration Tests")
    print("==========================================\n")

    if not check_server():
        sys.exit(1)

    global TOKEN
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)

    test_happy_path_upload()
    test_missing_chunk_upload()
    test_init_upload_quota()

    print_summary()


if __name__ == "__main__":
    main()

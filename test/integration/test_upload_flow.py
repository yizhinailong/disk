#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""
Integration tests for file upload flow APIs (4.1-4.4).

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured
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
    ensure_server,
    cleanup,
    do_login,
    execute,
    json_field,
    fetch,
    scalar,
    unique_name,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")

TOKEN = ""
RUN_NONCE = unique_name("upload")
HAPPY_FILENAME = f"happy_path_{RUN_NONCE}.bin"
MISSING_FILENAME = f"missing_chunk_{RUN_NONCE}.bin"
MISMATCH_FILENAME = f"chunk_size_mismatch_{RUN_NONCE}.bin"
RESUME_FILENAME = f"resume_upload_{RUN_NONCE}.bin"
DUPLICATE_CHUNK_FILENAME = f"duplicate_chunk_{RUN_NONCE}.bin"
INSTANT_SOURCE_FILENAME = f"instant_source_{RUN_NONCE}.bin"
INSTANT_COPY_FILENAME = f"instant_copy_{RUN_NONCE}.bin"


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


def _configured_max_file_size() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    config_path = os.path.join(repo_root, "config.json")
    try:
        with open(config_path, encoding="utf-8") as f:
            config = json.load(f)
        return int(
            config.get("custom_config", {})
            .get("disk", {})
            .get("max_file_size", 10 * 1024 * 1024 * 1024)
        )
    except Exception:
        return 10 * 1024 * 1024 * 1024


CHUNK_SIZE = _configured_chunk_size()
MAX_FILE_SIZE = _configured_max_file_size()


# ─── Helpers ─────────────────────────────────────────────────────────────────


def _md5_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def _full_chunk(label: str, fill: bytes) -> bytes:
    prefix = f"{RUN_NONCE}:{label}:".encode()
    return prefix + fill * (CHUNK_SIZE - len(prefix))


# ─── Test 1: Happy path upload ──────────────────────────────────────────────


def test_happy_path_upload():
    log_info("Testing Happy Path Upload Flow...")

    chunk_0_data = _full_chunk("happy", b"A")
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

    persisted_chunk_metadata = int(
        scalar(
            "SELECT COUNT(*) FROM upload_task_chunks "
            "WHERE task_id = %s AND size_bytes IS NOT NULL AND hash_md5 IS NOT NULL",
            (upload_id,),
        )
        or 0
    )
    if persisted_chunk_metadata != 2:
        log_fail(
            "Chunk metadata persistence - expected size/hash metadata for both chunks, "
            f"got {persisted_chunk_metadata}"
        )
        return
    log_pass("Chunk metadata persistence - size and hash stored for both chunks")

    # Simulate a different instance holding an active finalize lease.
    execute(
        "UPDATE upload_tasks SET status = 4, lease_owner = %s, "
        "lease_expires_at = NOW() + INTERVAL '1 minute', "
        "state_version = state_version + 1, finalize_attempts = finalize_attempts + 1 "
        "WHERE id = %s AND status = 0",
        ("integration-old-owner", upload_id),
    )

    chunk_rows_before_late_write = int(
        scalar("SELECT COUNT(*) FROM upload_task_chunks WHERE task_id = %s", (upload_id,)) or 0
    )
    late_chunk_resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=1&chunk_hash={chunk_1_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/octet-stream",
        },
        data=chunk_1_data,
    )
    late_chunk_code = json_field(late_chunk_resp.text, "code")
    chunk_rows_after_late_write = int(
        scalar("SELECT COUNT(*) FROM upload_task_chunks WHERE task_id = %s", (upload_id,)) or 0
    )
    if late_chunk_resp.status_code != 409 or late_chunk_code != "10004":
        log_fail(
            "Late Chunk Active Lease - expected HTTP 409 / code 10004, "
            f"got HTTP {late_chunk_resp.status_code} / code {late_chunk_code}"
        )
        print(late_chunk_resp.text)
        return
    if chunk_rows_after_late_write != chunk_rows_before_late_write:
        log_fail(
            "Late Chunk Active Lease - terminal-state request changed authoritative progress"
        )
        return
    log_pass("Late Chunk Active Lease - stale cache could not change authoritative progress")
    save_evidence("upload-chunk-active-lease.json", late_chunk_resp.text)

    conflict_resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )
    conflict_code = json_field(conflict_resp.text, "code")
    if conflict_resp.status_code != 409 or conflict_code != "10004":
        log_fail(
            "Complete Upload Active Lease - expected HTTP 409 / code 10004, "
            f"got HTTP {conflict_resp.status_code} / code {conflict_code}"
        )
        print(conflict_resp.text)
        return
    log_pass("Complete Upload Active Lease - returned retryable conflict")
    save_evidence("complete-upload-active-lease.json", conflict_resp.text)

    # Expire the old lease using database time; the normal completion below must take it over.
    execute(
        "UPDATE upload_tasks SET lease_expires_at = NOW() - INTERVAL '1 second' "
        "WHERE id = %s AND status = 4 AND lease_owner = %s",
        (upload_id, "integration-old-owner"),
    )

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

    # Retry the same completion request to verify durable idempotent replay.
    replay_resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )
    replay_file_id = json_field(replay_resp.text, "data.file.id")
    replay_file_name = json_field(replay_resp.text, "data.file.name")
    replay_hash = json_field(replay_resp.text, "data.file.hash")
    if replay_resp.status_code != 200:
        log_fail(f"Complete Upload Replay - expected HTTP 200, got {replay_resp.status_code}")
        print(replay_resp.text)
        return
    if (replay_file_id, replay_file_name, replay_hash) != (file_id, file_name, returned_hash):
        log_fail("Complete Upload Replay - response differs from first completion")
        print(replay_resp.text)
        return

    log_pass(f"Complete Upload Replay - returned original file_id: {replay_file_id}")
    save_evidence("complete-upload-replay.json", replay_resp.text)


# ─── Test 2: Missing chunk upload ───────────────────────────────────────────


def test_missing_chunk_upload():
    log_info("Testing Missing Chunk Upload (failure path)...")

    chunk_0_data = _full_chunk("missing", b"M")
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


# ─── Test 3: File size policy ─────────────────────────────────────────────────


def test_init_upload_max_file_size():
    log_info("Testing Init Upload (max file size exceeded)...")

    oversized_hash = "a1c7e6486f5811f4e23e6c696c0d6363"

    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": "oversized_file.bin",
            "file_size": MAX_FILE_SIZE + 1,
            "file_hash": oversized_hash,
            "parent_id": 0,
        },
    )

    code = json_field(resp.text, "code")
    message = json_field(resp.text, "message")

    if code == "10002" and "maximum" in message.lower():
        log_pass(f"Init Upload (max file size exceeded) - code: {code}")
        save_evidence("init-upload-max-size.json", resp.text)
    else:
        log_fail(
            f"Init Upload (max file size exceeded) - expected code 10002 maximum-size error, got: {code} {message}"
        )
        print(resp.text)


# ─── Test 4: Chunk size mismatch ──────────────────────────────────────────────


def test_chunk_size_mismatch():
    log_info("Testing Upload Chunk (size mismatch)...")

    valid_chunk = _full_chunk("mismatch", b"C")
    invalid_chunk = valid_chunk[:-1]
    full_data = valid_chunk + b"D"
    file_hash = _md5_bytes(full_data)
    invalid_hash = _md5_bytes(invalid_chunk)

    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": MISMATCH_FILENAME,
            "file_size": len(full_data),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )

    upload_id = json_field(resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init Upload (chunk size mismatch) - failed to get upload_id")
        print(resp.text)
        return

    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={invalid_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/octet-stream",
        },
        data=invalid_chunk,
    )

    code = json_field(resp.text, "code")
    message = json_field(resp.text, "message")
    if code == "10002" and "chunk size" in message.lower():
        log_pass(f"Upload Chunk (size mismatch) - code: {code}")
        save_evidence("upload-chunk-size-mismatch.json", resp.text)
    else:
        log_fail(
            f"Upload Chunk (size mismatch) - expected code 10002 chunk-size error, got: {code} {message}"
        )
        print(resp.text)


# ─── Test 5: Resume upload returns uploaded chunks ─────────────────────────────


def test_resume_upload_returns_uploaded_chunks():
    log_info("Testing Resume Upload (uploaded chunks returned)...")

    chunk_0_data = _full_chunk("resume", b"R")
    chunk_1_data = f"{RUN_NONCE}:resume-tail".encode()
    full_data = chunk_0_data + chunk_1_data
    chunk_0_hash = _md5_bytes(chunk_0_data)
    chunk_1_hash = _md5_bytes(chunk_1_data)
    file_hash = _md5_bytes(full_data)

    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
        json_body={
            "filename": RESUME_FILENAME,
            "file_size": len(full_data),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    upload_id = json_field(resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init Upload (resume) - failed to get upload_id")
        print(resp.text)
        return

    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_0_hash}",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/octet-stream"},
        data=chunk_0_data,
    )
    if json_field(resp.text, "data.uploaded") != "true":
        log_fail("Upload Chunk 0 (resume)")
        print(resp.text)
        return

    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
        json_body={
            "filename": RESUME_FILENAME,
            "file_size": len(full_data),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    resumed_upload_id = json_field(resp.text, "data.upload_id")
    uploaded_chunk = json_field(resp.text, "data.uploaded_chunks.0")
    if resumed_upload_id != upload_id or uploaded_chunk != "0":
        log_fail(
            f"Resume Upload - expected upload_id={upload_id} and uploaded_chunks[0]=0, "
            f"got upload_id={resumed_upload_id}, uploaded_chunks[0]={uploaded_chunk}"
        )
        print(resp.text)
        return

    log_pass("Resume Upload - returned same upload_id and uploaded chunk index")
    save_evidence("init-upload-resume.json", resp.text)

    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=1&chunk_hash={chunk_1_hash}",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/octet-stream"},
        data=chunk_1_data,
    )
    if json_field(resp.text, "data.uploaded") != "true":
        log_fail("Upload Chunk 1 (resume)")
        print(resp.text)
        return

    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
        json_body={"upload_id": upload_id},
    )
    if not json_field(resp.text, "data.file.id"):
        log_fail("Complete Upload (resume)")
        print(resp.text)
        return
    log_pass("Complete Upload (resume)")
    save_evidence("complete-upload-resume.json", resp.text)


# ─── Test 6: Duplicate chunk idempotency ──────────────────────────────────────


def test_duplicate_chunk_upload_is_idempotent():
    log_info("Testing Duplicate Chunk Upload (idempotency)...")

    payload = f"{RUN_NONCE}:duplicate-chunk-payload".encode()
    file_hash = _md5_bytes(payload)
    chunk_hash = _md5_bytes(payload)

    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
        json_body={
            "filename": DUPLICATE_CHUNK_FILENAME,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    upload_id = json_field(resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init Upload (duplicate chunk) - failed to get upload_id")
        print(resp.text)
        return

    for attempt in (1, 2):
        resp = fetch(
            f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_hash}",
            method="POST",
            headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/octet-stream"},
            data=payload,
        )
        if json_field(resp.text, "data.uploaded") != "true":
            log_fail(f"Upload duplicate chunk attempt {attempt}")
            print(resp.text)
            return

    log_pass("Duplicate Chunk Upload - both attempts accepted idempotently")
    save_evidence("upload-chunk-duplicate.json", resp.text)

    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
        json_body={"upload_id": upload_id},
    )
    if json_field(resp.text, "data.file.hash") != file_hash:
        log_fail("Complete Upload (duplicate chunk) - hash mismatch or failure")
        print(resp.text)
        return
    log_pass("Complete Upload (duplicate chunk)")
    save_evidence("complete-upload-duplicate-chunk.json", resp.text)


def test_instant_upload_reuses_existing_content():
    log_info("Testing Instant Upload (existing content reuse)...")

    payload = f"{RUN_NONCE}:instant-upload-payload".encode()
    file_hash = _md5_bytes(payload)
    chunk_hash = _md5_bytes(payload)

    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
        json_body={
            "filename": INSTANT_SOURCE_FILENAME,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    upload_id = json_field(resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init Upload (instant source) - failed to get upload_id")
        print(resp.text)
        return

    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_hash}",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/octet-stream"},
        data=payload,
    )
    if json_field(resp.text, "data.uploaded") != "true":
        log_fail("Upload Chunk (instant source)")
        print(resp.text)
        return

    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
        json_body={"upload_id": upload_id},
    )
    source_file_id = json_field(resp.text, "data.file.id")
    if not source_file_id or json_field(resp.text, "data.file.hash") != file_hash:
        log_fail("Complete Upload (instant source)")
        print(resp.text)
        return

    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"},
        json_body={
            "filename": INSTANT_COPY_FILENAME,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    instant_upload = json_field(resp.text, "data.instant_upload")
    instant_upload_id = json_field(resp.text, "data.upload_id")
    instant_file_id = json_field(resp.text, "data.file.id")
    instant_file_name = json_field(resp.text, "data.file.name")
    instant_file_hash = json_field(resp.text, "data.file.hash")

    if instant_upload != "true":
        log_fail(f"Instant Upload - expected instant_upload=true, got {instant_upload}")
        print(resp.text)
        return
    if instant_upload_id not in ("", "null", None):
        log_fail(f"Instant Upload - expected empty upload_id, got {instant_upload_id}")
        print(resp.text)
        return
    if not instant_file_id or instant_file_name != INSTANT_COPY_FILENAME or instant_file_hash != file_hash:
        log_fail("Instant Upload - file payload mismatch")
        print(resp.text)
        return

    log_pass("Instant Upload - reused existing content and returned file payload")
    save_evidence("init-upload-instant.json", resp.text)


# ─── Main ───────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("File Upload Flow Integration Tests")
    print("==========================================\n")

    ensure_server()

    global TOKEN
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)

    test_happy_path_upload()
    test_missing_chunk_upload()
    test_init_upload_max_file_size()
    test_chunk_size_mismatch()
    test_resume_upload_returns_uploaded_chunks()
    test_duplicate_chunk_upload_is_idempotent()
    test_instant_upload_reuses_existing_content()

    print_summary()


if __name__ == "__main__":
    main()

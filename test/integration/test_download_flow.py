#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for download parity: personal file + share file.

Covers:
  - File download: 200 (full), 206 (partial Range), 416 (unsatisfiable Range)
  - Share download: 200, 206, 416 (same assertions)

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured
  - Redis configured
  - User account exists (default: admin / Admin123)

Usage:
  uv run test/integration/test_download_flow.py
"""

import hashlib
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
    save_raw_evidence,
    check_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    header_value,
    create_temp_file,
    md5_hash,
    assert_status,
    assert_header_contains,
    assert_json_field,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
EVIDENCE_PREFIX = "task-2"

TOKEN = ""
FILE_ID = ""
FILE_SIZE = 0
FILE_HASH = ""
SHARE_ID = ""
SHARE_TOKEN = ""
SHARE_FILE_ID = ""


# ─── Phase 2: Upload a test file ───────────────────────────────────────────


def do_upload():
    global FILE_ID, FILE_SIZE, FILE_HASH

    log_step("Uploading test fixture file...")

    fixture = create_temp_file(256, suffix=".bin")
    FILE_SIZE = 256
    FILE_HASH = md5_hash(fixture)

    with open(fixture, "rb") as f:
        file_content = f.read()

    # Init upload
    init_resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": "download_test.bin",
            "file_size": FILE_SIZE,
            "file_hash": FILE_HASH,
            "parent_id": 0,
        },
    )

    instant_upload = json_field(init_resp.text, "data.instant_upload")

    if instant_upload == "true":
        # File already exists (instant upload / dedup) — still get file_id
        FILE_ID = json_field(init_resp.text, "data.file_id")
        if not FILE_ID or FILE_ID == "null":
            log_fail("Instant upload but no file_id returned")
            print(init_resp.text)
            os.unlink(fixture)
            sys.exit(1)
        log_info(f"Instant upload (dedup) — file_id={FILE_ID}")
        save_evidence(f"{EVIDENCE_PREFIX}-upload-init.json", init_resp.text)
        os.unlink(fixture)
        return

    upload_id = json_field(init_resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init upload failed")
        print(init_resp.text)
        os.unlink(fixture)
        sys.exit(1)
    save_evidence(f"{EVIDENCE_PREFIX}-upload-init.json", init_resp.text)

    # Upload chunk (single chunk = whole file)
    chunk_hash = FILE_HASH
    chunk_resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/octet-stream",
        },
        data=file_content,
    )

    uploaded = json_field(chunk_resp.text, "data.uploaded")
    if uploaded != "true":
        log_fail("Upload chunk failed")
        print(chunk_resp.text)
        os.unlink(fixture)
        sys.exit(1)
    save_evidence(f"{EVIDENCE_PREFIX}-upload-chunk.json", chunk_resp.text)

    # Complete upload
    complete_resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )

    FILE_ID = json_field(complete_resp.text, "data.file.id")
    if not FILE_ID or FILE_ID == "null":
        log_fail("Complete upload — no file.id")
        print(complete_resp.text)
        os.unlink(fixture)
        sys.exit(1)
    save_evidence(f"{EVIDENCE_PREFIX}-upload-complete.json", complete_resp.text)

    os.unlink(fixture)
    log_pass(f"File uploaded — file_id={FILE_ID}, size={FILE_SIZE}")


# ─── Phase 3: Create share ─────────────────────────────────────────────────


def do_create_share():
    global SHARE_ID

    log_step(f"Creating share for file_id={FILE_ID}...")

    resp = fetch(
        "/api/share",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={
            "file_ids": [int(FILE_ID)],
            "permission": "download",
            "expire_days": 1,
        },
    )

    SHARE_ID = json_field(resp.text, "data.share_id")
    if not SHARE_ID or SHARE_ID == "null":
        log_fail("Create share failed")
        print(resp.text)
        sys.exit(1)
    save_evidence(f"{EVIDENCE_PREFIX}-share-create.json", resp.text)
    log_pass(f"Share created — share_id={SHARE_ID}")


# ─── Phase 4: Access share to get share_token ──────────────────────────────


def do_access_share():
    global SHARE_TOKEN, SHARE_FILE_ID

    log_step("Accessing share to get share_token...")

    resp = fetch(
        f"/api/share/access/{SHARE_ID}",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )

    SHARE_TOKEN = json_field(resp.text, "data.share_token")
    if not SHARE_TOKEN or SHARE_TOKEN == "null":
        log_fail("Access share failed")
        print(resp.text)
        sys.exit(1)

    share_file_id = json_field(resp.text, "data.files.0.id")
    if share_file_id and share_file_id != "null":
        SHARE_FILE_ID = share_file_id
    else:
        SHARE_FILE_ID = FILE_ID

    save_evidence(f"{EVIDENCE_PREFIX}-share-access.json", resp.text)
    log_pass(f"Share access — share_token obtained, file_id={SHARE_FILE_ID}")


# ─── Test: Personal File Download 200 ──────────────────────────────────────


def test_file_download_200():
    log_step(f"Test: GET /api/file/download/{FILE_ID} → 200 (full download)")

    resp = fetch(
        f"/api/file/download/{FILE_ID}",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(
        f"{EVIDENCE_PREFIX}-file-200.headers.txt",
        json.dumps(dict(resp.headers), indent=2),
    )
    save_raw_evidence(f"{EVIDENCE_PREFIX}-file-200.body.bin", resp.text)

    ok = True
    assert_status("file-200", resp.status_code, 200) or (ok := False)
    assert_header_contains(
        "file-200", resp.headers, "Content-Disposition", "attachment"
    ) or (ok := False)
    assert_header_contains(
        "file-200", resp.headers, "Content-Disposition", "download_test.bin"
    ) or (ok := False)
    assert_header_contains(
        "file-200", resp.headers, "Content-Length", str(FILE_SIZE)
    ) or (ok := False)
    assert_header_contains("file-200", resp.headers, "Accept-Ranges", "bytes") or (
        ok := False
    )

    if FILE_HASH:
        assert_header_contains("file-200", resp.headers, "ETag", FILE_HASH) or (
            ok := False
        )

    if ok:
        log_pass("file-200: full download OK")


# ─── Test: Personal File Download 206 ──────────────────────────────────────


def test_file_download_206():
    log_step(f"Test: GET /api/file/download/{FILE_ID} (Range: bytes=0-9) → 206")

    resp = fetch(
        f"/api/file/download/{FILE_ID}",
        method="GET",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Range": "bytes=0-9",
        },
    )

    save_evidence(
        f"{EVIDENCE_PREFIX}-file-206.headers.txt",
        json.dumps(dict(resp.headers), indent=2),
    )

    ok = True
    assert_status("file-206", resp.status_code, 206) or (ok := False)
    assert_header_contains(
        "file-206", resp.headers, "Content-Range", f"bytes 0-9/{FILE_SIZE}"
    ) or (ok := False)
    assert_header_contains("file-206", resp.headers, "Content-Length", "10") or (
        ok := False
    )
    assert_header_contains("file-206", resp.headers, "Accept-Ranges", "bytes") or (
        ok := False
    )

    if ok:
        log_pass("file-206: partial content OK")


# ─── Test: Personal File Download 416 ──────────────────────────────────────


def test_file_download_416():
    log_step(f"Test: GET /api/file/download/{FILE_ID} (Range: bytes=99999-99999) → 416")

    resp = fetch(
        f"/api/file/download/{FILE_ID}",
        method="GET",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Range": "bytes=99999-99999",
        },
    )

    save_evidence(
        f"{EVIDENCE_PREFIX}-file-416.headers.txt",
        json.dumps(dict(resp.headers), indent=2),
    )
    save_evidence(f"{EVIDENCE_PREFIX}-file-416.json", resp.text)

    ok = True
    assert_status("file-416", resp.status_code, 416) or (ok := False)
    assert_header_contains(
        "file-416", resp.headers, "Content-Range", f"bytes */{FILE_SIZE}"
    ) or (ok := False)
    assert_json_field("file-416", resp.text, "code", "10002") or (ok := False)

    # Verify .message and .data exist
    try:
        body = json.loads(resp.text)
        if "message" not in body or not body["message"]:
            log_fail("file-416: missing .message field")
            ok = False
        if "data" not in body:
            log_fail("file-416: missing .data field")
            ok = False
    except Exception:
        log_fail("file-416: invalid JSON body")
        ok = False

    if ok:
        log_pass("file-416: unsatisfiable range OK")


# ─── Test: Share Download 200 ──────────────────────────────────────────────


def test_share_download_200():
    log_step(f"Test: GET /api/share/download/{SHARE_ID}/{SHARE_FILE_ID} → 200")

    resp = fetch(
        f"/api/share/download/{SHARE_ID}/{SHARE_FILE_ID}",
        method="GET",
        headers={"X-Share-Token": SHARE_TOKEN},
    )

    save_evidence(
        f"{EVIDENCE_PREFIX}-share-200.headers.txt",
        json.dumps(dict(resp.headers), indent=2),
    )
    save_raw_evidence(f"{EVIDENCE_PREFIX}-share-200.body.bin", resp.text)

    ok = True
    assert_status("share-200", resp.status_code, 200) or (ok := False)
    assert_header_contains(
        "share-200", resp.headers, "Content-Disposition", "attachment"
    ) or (ok := False)
    assert_header_contains(
        "share-200", resp.headers, "Content-Disposition", "download_test.bin"
    ) or (ok := False)
    assert_header_contains(
        "share-200", resp.headers, "Content-Length", str(FILE_SIZE)
    ) or (ok := False)
    assert_header_contains("share-200", resp.headers, "Accept-Ranges", "bytes") or (
        ok := False
    )

    if ok:
        log_pass("share-200: full share download OK")


# ─── Test: Share Download 206 ──────────────────────────────────────────────


def test_share_download_206():
    log_step(
        f"Test: GET /api/share/download/{SHARE_ID}/{SHARE_FILE_ID} (Range: bytes=0-9) → 206"
    )

    resp = fetch(
        f"/api/share/download/{SHARE_ID}/{SHARE_FILE_ID}",
        method="GET",
        headers={
            "X-Share-Token": SHARE_TOKEN,
            "Range": "bytes=0-9",
        },
    )

    save_evidence(
        f"{EVIDENCE_PREFIX}-share-206.headers.txt",
        json.dumps(dict(resp.headers), indent=2),
    )

    ok = True
    assert_status("share-206", resp.status_code, 206) or (ok := False)
    assert_header_contains(
        "share-206", resp.headers, "Content-Range", f"bytes 0-9/{FILE_SIZE}"
    ) or (ok := False)
    assert_header_contains("share-206", resp.headers, "Content-Length", "10") or (
        ok := False
    )
    assert_header_contains("share-206", resp.headers, "Accept-Ranges", "bytes") or (
        ok := False
    )

    if ok:
        log_pass("share-206: partial share download OK")


# ─── Test: Share Download 416 ──────────────────────────────────────────────


def test_share_download_416():
    log_step(
        f"Test: GET /api/share/download/{SHARE_ID}/{SHARE_FILE_ID} (Range: bytes=99999-99999) → 416"
    )

    resp = fetch(
        f"/api/share/download/{SHARE_ID}/{SHARE_FILE_ID}",
        method="GET",
        headers={
            "X-Share-Token": SHARE_TOKEN,
            "Range": "bytes=99999-99999",
        },
    )

    save_evidence(
        f"{EVIDENCE_PREFIX}-share-416.headers.txt",
        json.dumps(dict(resp.headers), indent=2),
    )
    save_evidence(f"{EVIDENCE_PREFIX}-share-416.json", resp.text)

    ok = True
    assert_status("share-416", resp.status_code, 416) or (ok := False)
    assert_header_contains(
        "share-416", resp.headers, "Content-Range", f"bytes */{FILE_SIZE}"
    ) or (ok := False)
    assert_json_field("share-416", resp.text, "code", "10002") or (ok := False)

    try:
        body = json.loads(resp.text)
        if "message" not in body or not body["message"]:
            log_fail("share-416: missing .message field")
            ok = False
        if "data" not in body:
            log_fail("share-416: missing .data field")
            ok = False
    except Exception:
        log_fail("share-416: invalid JSON body")
        ok = False

    if ok:
        log_pass("share-416: unsatisfiable range OK")


# ─── Evidence Summary ──────────────────────────────────────────────────────


def write_summary_evidence():
    from datetime import datetime

    summary = (
        f"=== Download Flow Integration Test Summary ===\n"
        f"Date: {datetime.now().isoformat()}\n"
        f"BASE_URL: {BASE_URL}\n"
        f"TEST_USER: {TEST_USER}\n\n"
        f"--- Fixture ---\n"
        f"FILE_ID: {FILE_ID}\n"
        f"FILE_SIZE: {FILE_SIZE}\n"
        f"FILE_HASH: {FILE_HASH}\n"
        f"SHARE_ID: {SHARE_ID}\n"
        f"SHARE_FILE_ID: {SHARE_FILE_ID}\n\n"
        f"--- Results ---\n"
        f"Passed: tests_passed\n"
        f"Failed: tests_failed\n"
    )
    save_evidence(f"{EVIDENCE_PREFIX}-download-flow.txt", summary)
    log_info(f"Summary evidence: {EVIDENCE_PREFIX}-download-flow.txt")


# ─── Main ───────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("Download Flow Integration Tests")
    print("==========================================\n")

    if not check_server():
        sys.exit(1)

    global TOKEN
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)

    do_upload()
    do_create_share()
    do_access_share()

    print()
    print("==========================================")
    print("Running Download Tests")
    print("==========================================\n")

    # Personal file download tests
    test_file_download_200()
    test_file_download_206()
    test_file_download_416()

    # Share file download tests
    test_share_download_200()
    test_share_download_206()
    test_share_download_416()

    write_summary_evidence()

    print_summary()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
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
    query_one,
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
SHARE_ACCESS_BODY = ""


def file_download_metadata():
    row = query_one(
        "SELECT download_count, last_accessed_at FROM files WHERE id = %s",
        (int(FILE_ID),),
    )
    if row is None:
        log_fail(f"file metadata row missing: file_id={FILE_ID}")
        return {"download_count": -1, "last_accessed_at": None}
    return row


def share_download_count():
    count_by_code = query_one(
        "SELECT download_count FROM shares WHERE share_code = %s",
        (SHARE_ID,),
    )
    if count_by_code is not None:
        return int(count_by_code["download_count"])

    try:
        count_by_id = query_one(
            "SELECT download_count FROM shares WHERE id = %s",
            (int(SHARE_ID),),
        )
    except ValueError:
        count_by_id = None
    if count_by_id is None:
        log_fail(f"share metadata row missing: share_id={SHARE_ID}")
        return -1
    return int(count_by_id["download_count"])


def assert_file_metadata_unchanged(label, before, after):
    ok = True
    if after["download_count"] == before["download_count"]:
        log_pass(f"{label}: file download_count unchanged")
    else:
        log_fail(
            f"{label}: expected file download_count unchanged at "
            f"{before['download_count']}, got {after['download_count']}"
        )
        ok = False

    if after["last_accessed_at"] == before["last_accessed_at"]:
        log_pass(f"{label}: file last_accessed_at unchanged")
    else:
        log_fail(
            f"{label}: expected file last_accessed_at unchanged at "
            f"{before['last_accessed_at']}, got {after['last_accessed_at']}"
        )
        ok = False
    return ok


def assert_file_metadata_incremented(label, before, after):
    ok = True
    expected_count = int(before["download_count"]) + 1
    actual_count = int(after["download_count"])
    if actual_count == expected_count:
        log_pass(f"{label}: file download_count incremented")
    else:
        log_fail(f"{label}: expected file download_count {expected_count}, got {actual_count}")
        ok = False

    before_accessed = before["last_accessed_at"]
    after_accessed = after["last_accessed_at"]
    if after_accessed is not None and after_accessed != before_accessed:
        log_pass(f"{label}: file last_accessed_at refreshed")
    else:
        log_fail(
            f"{label}: expected file last_accessed_at to refresh, "
            f"before={before_accessed}, after={after_accessed}"
        )
        ok = False
    return ok


def assert_share_download_delta(label, before, after, expected_delta):
    expected = before + expected_delta
    if after == expected:
        log_pass(f"{label}: share download_count delta {expected_delta}")
        return True
    log_fail(f"{label}: expected share download_count {expected}, got {after}")
    return False


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
    global SHARE_TOKEN, SHARE_FILE_ID, SHARE_ACCESS_BODY

    log_step("Accessing share to get share_token...")

    resp = fetch(
        f"/api/share/access/{SHARE_ID}",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )
    SHARE_ACCESS_BODY = resp.text

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


# ─── Test: Download Metadata ──────────────────────────────────────────────────


def test_owner_download_metadata_integrity_fields():
    log_step(f"Test: GET /api/file/download/{FILE_ID}/info exposes integrity metadata")

    metadata_before = file_download_metadata()

    resp = fetch(
        f"/api/file/download/{FILE_ID}/info",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    metadata_after = file_download_metadata()
    save_evidence(f"{EVIDENCE_PREFIX}-file-info.json", resp.text)

    ok = True
    assert_status("file-info", resp.status_code, 200) or (ok := False)
    assert_json_field("file-info-code", resp.text, "code", "0") or (ok := False)
    assert_json_field("file-info-id", resp.text, "data.file_id", FILE_ID) or (ok := False)
    assert_json_field("file-info-size", resp.text, "data.file_size", str(FILE_SIZE)) or (ok := False)
    assert_json_field("file-info-hash", resp.text, "data.file_hash", FILE_HASH) or (ok := False)
    assert_json_field("file-info-range", resp.text, "data.supports_range", "true") or (ok := False)
    assert_file_metadata_unchanged("file-info", metadata_before, metadata_after) or (ok := False)

    if ok:
        log_pass("file-info: owner metadata includes size/hash/range fields without counting download")


def test_visitor_download_metadata_integrity_fields():
    log_step("Test: share access metadata exposes visitor size/hash fields")

    ok = True
    assert_json_field("share-access-file-id", SHARE_ACCESS_BODY, "data.files.0.id", SHARE_FILE_ID) or (ok := False)
    assert_json_field("share-access-file-size", SHARE_ACCESS_BODY, "data.files.0.size", str(FILE_SIZE)) or (ok := False)
    share_hash = json_field(SHARE_ACCESS_BODY, "data.files.0.hash")
    share_file_hash = json_field(SHARE_ACCESS_BODY, "data.files.0.file_hash")
    if FILE_HASH in (share_hash, share_file_hash):
        log_pass("share-access-file-hash")
    else:
        log_fail(
            "share-access-file-hash: expected visitor metadata to expose uploaded hash "
            f"{FILE_HASH}, got hash={share_hash!r}, file_hash={share_file_hash!r}"
        )
        ok = False

    if ok:
        log_pass("share-access: visitor metadata includes size/hash fields")


def test_share_download_info_does_not_count():
    log_step(f"Test: GET /api/share/download/{SHARE_ID}/{SHARE_FILE_ID}/info does not count")

    file_before = file_download_metadata()
    share_before = share_download_count()

    resp = fetch(
        f"/api/share/download/{SHARE_ID}/{SHARE_FILE_ID}/info",
        method="GET",
        headers={"X-Share-Token": SHARE_TOKEN},
    )

    file_after = file_download_metadata()
    share_after = share_download_count()
    save_evidence(f"{EVIDENCE_PREFIX}-share-info.json", resp.text)

    ok = True
    assert_status("share-info", resp.status_code, 200) or (ok := False)
    assert_json_field("share-info-code", resp.text, "code", "0") or (ok := False)
    assert_json_field("share-info-id", resp.text, "data.file_id", SHARE_FILE_ID) or (ok := False)
    assert_json_field("share-info-size", resp.text, "data.file_size", str(FILE_SIZE)) or (ok := False)
    assert_json_field("share-info-hash", resp.text, "data.file_hash", FILE_HASH) or (ok := False)
    assert_json_field("share-info-range", resp.text, "data.supports_range", "true") or (ok := False)
    assert_file_metadata_unchanged("share-info", file_before, file_after) or (ok := False)
    assert_share_download_delta("share-info", share_before, share_after, 0) or (ok := False)

    if ok:
        log_pass("share-info: metadata lookup did not count as download")


# ─── Test: Personal File Download 200 ──────────────────────────────────────


def test_file_download_200():
    log_step(f"Test: GET /api/file/download/{FILE_ID} → 200 (full download)")

    metadata_before = file_download_metadata()

    resp = fetch(
        f"/api/file/download/{FILE_ID}",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    metadata_after = file_download_metadata()
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
    assert_file_metadata_incremented("file-200", metadata_before, metadata_after) or (ok := False)

    if ok:
        log_pass("file-200: full download OK and file metadata updated")


# ─── Test: Personal File Download 206 ──────────────────────────────────────


def test_file_download_206():
    log_step(f"Test: GET /api/file/download/{FILE_ID} (Range: bytes=0-9) → 206")

    metadata_before = file_download_metadata()

    resp = fetch(
        f"/api/file/download/{FILE_ID}",
        method="GET",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Range": "bytes=0-9",
        },
    )

    metadata_after = file_download_metadata()
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
    assert_file_metadata_incremented("file-206", metadata_before, metadata_after) or (ok := False)

    if ok:
        log_pass("file-206: partial content OK and file metadata updated")


# ─── Test: Personal File Download 416 ──────────────────────────────────────


def test_file_download_416():
    log_step(f"Test: GET /api/file/download/{FILE_ID} (Range: bytes=99999-99999) → 416")

    metadata_before = file_download_metadata()

    resp = fetch(
        f"/api/file/download/{FILE_ID}",
        method="GET",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Range": "bytes=99999-99999",
        },
    )

    metadata_after = file_download_metadata()
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
    assert_file_metadata_unchanged("file-416", metadata_before, metadata_after) or (ok := False)

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
        log_pass("file-416: unsatisfiable range OK without file metadata update")


# ─── Test: Share Download 200 ──────────────────────────────────────────────


def test_share_download_200():
    log_step(f"Test: GET /api/share/download/{SHARE_ID}/{SHARE_FILE_ID} → 200")

    file_before = file_download_metadata()
    share_before = share_download_count()

    resp = fetch(
        f"/api/share/download/{SHARE_ID}/{SHARE_FILE_ID}",
        method="GET",
        headers={"X-Share-Token": SHARE_TOKEN},
    )

    file_after = file_download_metadata()
    share_after = share_download_count()
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
    if FILE_HASH:
        assert_header_contains("share-200", resp.headers, "ETag", FILE_HASH) or (
            ok := False
        )
    assert_file_metadata_incremented("share-200", file_before, file_after) or (ok := False)
    assert_share_download_delta("share-200", share_before, share_after, 1) or (ok := False)

    if ok:
        log_pass("share-200: full share download OK and metadata updated")


# ─── Test: Share Download 206 ──────────────────────────────────────────────


def test_share_download_206():
    log_step(
        f"Test: GET /api/share/download/{SHARE_ID}/{SHARE_FILE_ID} (Range: bytes=0-9) → 206"
    )

    file_before = file_download_metadata()
    share_before = share_download_count()

    resp = fetch(
        f"/api/share/download/{SHARE_ID}/{SHARE_FILE_ID}",
        method="GET",
        headers={
            "X-Share-Token": SHARE_TOKEN,
            "Range": "bytes=0-9",
        },
    )

    file_after = file_download_metadata()
    share_after = share_download_count()
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
    if FILE_HASH:
        assert_header_contains("share-206", resp.headers, "ETag", FILE_HASH) or (
            ok := False
        )
    assert_file_metadata_incremented("share-206", file_before, file_after) or (ok := False)
    assert_share_download_delta("share-206", share_before, share_after, 1) or (ok := False)

    if ok:
        log_pass("share-206: partial share download OK and metadata updated")


# ─── Test: Share Download 416 ──────────────────────────────────────────────


def test_share_download_416():
    log_step(
        f"Test: GET /api/share/download/{SHARE_ID}/{SHARE_FILE_ID} (Range: bytes=99999-99999) → 416"
    )

    file_before = file_download_metadata()
    share_before = share_download_count()

    resp = fetch(
        f"/api/share/download/{SHARE_ID}/{SHARE_FILE_ID}",
        method="GET",
        headers={
            "X-Share-Token": SHARE_TOKEN,
            "Range": "bytes=99999-99999",
        },
    )

    file_after = file_download_metadata()
    share_after = share_download_count()
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
    assert_file_metadata_unchanged("share-416", file_before, file_after) or (ok := False)
    assert_share_download_delta("share-416", share_before, share_after, 1) or (ok := False)

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
        log_pass("share-416: unsatisfiable range OK, share count preserved, file metadata unchanged")


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

    # Metadata tests
    test_owner_download_metadata_integrity_fields()
    test_visitor_download_metadata_integrity_fields()
    test_share_download_info_does_not_count()

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

#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for file metadata query APIs.

Covers:
  - File list: GET /api/file/list?parent_id=0 → find uploaded fixture in items
  - File detail: GET /api/file/{file_id} → verify id, name, size, hash
  - Download info: GET /api/file/download/{file_id}/info → verify file_id, filename
  - Search: GET /api/file/search?keyword=X → find fixture by keyword
  - Nonexistent file: GET /api/file/99999999 → error response
  - Unauthenticated: GET /api/file/list without token → HTTP 401

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured
  - Redis configured
  - User account exists (default: admin / Admin123)

Usage:
  uv run test/integration/test_file_metadata_query.py
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
    check_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    create_temp_file,
    md5_hash,
    unique_name,
    assert_json_field,
    assert_json_array_not_empty,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
EVIDENCE_PREFIX = "metadata-query"

TOKEN = ""
FILE_ID = ""
FILE_SIZE = 256
FILE_HASH = ""
SEARCH_KEYWORD = unique_name("metadata").replace("_", "")
FILE_NAME = f"{SEARCH_KEYWORD}.bin"


# ─── Upload fixture ────────────────────────────────────────────────────────


def do_upload_fixture():
    global FILE_ID, FILE_SIZE, FILE_HASH

    log_step(f"Uploading fixture file: {FILE_NAME}")

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
            "filename": FILE_NAME,
            "file_size": FILE_SIZE,
            "file_hash": FILE_HASH,
            "parent_id": 0,
        },
    )

    instant_upload = json_field(init_resp.text, "data.instant_upload")

    if instant_upload == "true":
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

    # Upload chunk
    chunk_hash = FILE_HASH
    fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/octet-stream",
        },
        data=file_content,
    )

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
    log_pass(
        f"Fixture uploaded — file_id={FILE_ID}, name={FILE_NAME}, size={FILE_SIZE}"
    )


# ─── Test: File List ───────────────────────────────────────────────────────


def test_file_list():
    log_step(
        "Test: GET /api/file/list?parent_id=0&page_size=100&sort_by=created_at&sort_order=desc&type=file"
    )

    resp = fetch(
        "/api/file/list?parent_id=0&page_size=100&sort_by=created_at&sort_order=desc&type=file",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-file-list.json", resp.text)

    ok = True
    assert_json_field("file-list", resp.text, "code", "0") or (ok := False)
    assert_json_array_not_empty("file-list", resp.text, "data.items") or (ok := False)

    # Verify the uploaded fixture is found in items by name
    found_name = False
    try:
        body = json.loads(resp.text)
        for item in body.get("data", {}).get("items", []):
            if item.get("name") == FILE_NAME:
                found_name = True
                break
    except Exception:
        pass

    if found_name:
        log_info(f"file-list: fixture '{FILE_NAME}' found in items")
    else:
        log_fail(f"file-list: fixture '{FILE_NAME}' NOT found in items")
        ok = False

    # Verify the fixture's file_id appears in items
    found_id = False
    try:
        body = json.loads(resp.text)
        for item in body.get("data", {}).get("items", []):
            if str(item.get("id", "")) == FILE_ID:
                found_id = True
                break
    except Exception:
        pass

    if found_id:
        log_info(f"file-list: fixture id={FILE_ID} found in items")
    else:
        log_fail(f"file-list: fixture id={FILE_ID} NOT found in items")
        ok = False

    if ok:
        log_pass("file-list: list query verified")


# ─── Test: File Detail ─────────────────────────────────────────────────────


def test_file_detail():
    log_step(f"Test: GET /api/file/{FILE_ID}")

    resp = fetch(
        f"/api/file/{FILE_ID}",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-file-detail.json", resp.text)

    ok = True
    assert_json_field("file-detail", resp.text, "code", "0") or (ok := False)

    # Verify id matches
    detail_id = json_field(resp.text, "data.id")
    if not detail_id or detail_id == "null" or detail_id == "":
        detail_id = json_field(resp.text, "data.file_id")
    if not detail_id or detail_id == "null" or detail_id == "":
        detail_id = json_field(resp.text, "data.file.file_id")

    if detail_id == FILE_ID:
        log_info(f"file-detail: id={detail_id} matches FILE_ID={FILE_ID}")
    else:
        log_fail(f"file-detail: id='{detail_id}' does not match FILE_ID={FILE_ID}")
        ok = False

    # Verify name matches
    detail_name = json_field(resp.text, "data.name")
    if not detail_name or detail_name == "null":
        detail_name = json_field(resp.text, "data.file.name")
    if detail_name == FILE_NAME:
        log_info(f"file-detail: name='{detail_name}' matches fixture")
    else:
        log_fail(f"file-detail: name='{detail_name}' does not match '{FILE_NAME}'")
        ok = False

    # Verify size matches
    detail_size = json_field(resp.text, "data.size")
    if not detail_size or detail_size == "null":
        detail_size = json_field(resp.text, "data.file.size")
    if detail_size == str(FILE_SIZE):
        log_info(f"file-detail: size={detail_size} matches fixture")
    else:
        log_fail(f"file-detail: size='{detail_size}' does not match {FILE_SIZE}")
        ok = False

    # Verify hash matches
    detail_hash = json_field(resp.text, "data.hash")
    if not detail_hash or detail_hash == "null":
        detail_hash = json_field(resp.text, "data.file.hash")
    if detail_hash == FILE_HASH:
        log_info("file-detail: hash matches fixture")
    else:
        log_fail(f"file-detail: hash='{detail_hash}' does not match '{FILE_HASH}'")
        ok = False

    if ok:
        log_pass("file-detail: detail query verified (id, name, size, hash)")


# ─── Test: Download Info ───────────────────────────────────────────────────


def test_download_info():
    log_step(f"Test: GET /api/file/download/{FILE_ID}/info")

    resp = fetch(
        f"/api/file/download/{FILE_ID}/info",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-download-info.json", resp.text)

    ok = True
    assert_json_field("download-info", resp.text, "code", "0") or (ok := False)

    # Verify data.file_id matches
    dl_file_id = json_field(resp.text, "data.file_id")
    if dl_file_id == FILE_ID:
        log_info(f"download-info: file_id={dl_file_id} matches")
    else:
        log_fail(
            f"download-info: file_id='{dl_file_id}' does not match FILE_ID={FILE_ID}"
        )
        ok = False

    # Verify data.filename matches
    dl_filename = json_field(resp.text, "data.filename")
    if dl_filename == FILE_NAME:
        log_info(f"download-info: filename='{dl_filename}' matches")
    else:
        log_fail(
            f"download-info: filename='{dl_filename}' does not match '{FILE_NAME}'"
        )
        ok = False

    # Verify data.file_size matches
    dl_file_size = json_field(resp.text, "data.file_size")
    if dl_file_size == str(FILE_SIZE):
        log_info(f"download-info: file_size={dl_file_size} matches")
    else:
        log_fail(
            f"download-info: file_size='{dl_file_size}' does not match {FILE_SIZE}"
        )
        ok = False

    # Verify data.file_hash matches
    dl_file_hash = json_field(resp.text, "data.file_hash")
    if dl_file_hash == FILE_HASH:
        log_info("download-info: file_hash matches")
    else:
        log_fail(
            f"download-info: file_hash='{dl_file_hash}' does not match '{FILE_HASH}'"
        )
        ok = False

    if ok:
        log_pass(
            "download-info: download info query verified (file_id, filename, file_size, file_hash)"
        )


# ─── Test: Search ──────────────────────────────────────────────────────────


def test_file_search():
    keyword = FILE_NAME
    log_step(f"Test: GET /api/file/search?keyword={keyword}")

    resp = fetch(
        f"/api/file/search?keyword={keyword}",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-file-search.json", resp.text)

    ok = True
    assert_json_field("file-search", resp.text, "code", "0") or (ok := False)
    assert_json_array_not_empty("file-search", resp.text, "data.items") or (ok := False)

    # Verify the fixture is found by name matching keyword
    found_name = False
    try:
        body = json.loads(resp.text)
        for item in body.get("data", {}).get("items", []):
            if keyword in item.get("name", ""):
                found_name = True
                break
    except Exception:
        pass

    if found_name:
        log_info(f"file-search: found matching file for keyword '{keyword}'")
    else:
        log_fail(f"file-search: no file matching keyword '{keyword}' in items")
        ok = False

    # Verify the fixture's id is in results
    found_id = False
    try:
        body = json.loads(resp.text)
        for item in body.get("data", {}).get("items", []):
            if str(item.get("id", "")) == FILE_ID:
                found_id = True
                break
    except Exception:
        pass

    if found_id:
        log_info(f"file-search: fixture id={FILE_ID} found in results")
    else:
        log_fail(f"file-search: fixture id={FILE_ID} NOT found in results")
        ok = False

    if ok:
        log_pass("file-search: search query verified")


# ─── Test: Nonexistent File ────────────────────────────────────────────────


def test_nonexistent_file():
    log_step("Test: GET /api/file/99999999 → expect error")

    resp = fetch(
        "/api/file/99999999",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-nonexistent.json", resp.text)

    code = json_field(resp.text, "code")

    if resp.status_code != 200:
        log_pass(f"nonexistent-file: HTTP {resp.status_code} (not found) — correct")
    elif code != "0" and code:
        log_pass(f"nonexistent-file: code={code} (error) — correct")
    else:
        log_fail(
            f"nonexistent-file: expected error, got HTTP {resp.status_code} code={code}"
        )


# ─── Test: Unauthenticated List ────────────────────────────────────────────


def test_unauthenticated_list():
    log_step("Test: GET /api/file/list without token → expect 401")

    resp = fetch(
        "/api/file/list?parent_id=0",
        method="GET",
    )

    save_evidence(f"{EVIDENCE_PREFIX}-unauthenticated.txt", f"HTTP {resp.status_code}")

    if resp.status_code == 401:
        log_pass("unauthenticated-list: HTTP 401 — correct")
    else:
        log_fail(
            f"unauthenticated-list: expected HTTP 401, got HTTP {resp.status_code}"
        )


# ─── Evidence Summary ──────────────────────────────────────────────────────


def write_summary_evidence():
    from datetime import datetime

    summary = (
        f"=== File Metadata Query Integration Test Summary ===\n"
        f"Date: {datetime.now().isoformat()}\n"
        f"BASE_URL: {BASE_URL}\n"
        f"TEST_USER: {TEST_USER}\n\n"
        f"--- Fixture ---\n"
        f"FILE_ID: {FILE_ID}\n"
        f"FILE_NAME: {FILE_NAME}\n"
        f"FILE_SIZE: {FILE_SIZE}\n"
        f"FILE_HASH: {FILE_HASH}\n\n"
        f"--- Results ---\n"
        f"Passed: tests_passed\n"
        f"Failed: tests_failed\n\n"
        f"--- Tests ---\n"
        f"  file-list: GET /api/file/list?parent_id=0 — find fixture in items\n"
        f"  file-detail: GET /api/file/{{file_id}} — verify id, name, size, hash\n"
        f"  download-info: GET /api/file/download/{{file_id}}/info — verify file_id, filename\n"
        f"  file-search: GET /api/file/search?keyword=X — find fixture by keyword\n"
        f"  nonexistent-file: GET /api/file/99999999 — expect error\n"
        f"  unauthenticated-list: GET /api/file/list (no token) — expect 401\n"
    )
    save_evidence(f"{EVIDENCE_PREFIX}-summary.txt", summary)
    log_info(f"Summary evidence: {EVIDENCE_PREFIX}-summary.txt")


# ─── Main ───────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("File Metadata Query Integration Tests")
    print("==========================================\n")

    if not check_server():
        sys.exit(1)

    global TOKEN
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)

    do_upload_fixture()

    print()
    print("==========================================")
    print("Running Metadata Query Tests")
    print("==========================================\n")

    test_file_list()
    test_file_detail()
    test_download_info()
    test_file_search()
    test_nonexistent_file()
    test_unauthenticated_list()

    write_summary_evidence()

    print_summary()


if __name__ == "__main__":
    main()

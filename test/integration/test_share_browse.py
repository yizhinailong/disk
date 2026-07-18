#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for share create + access + browse flow.

Verifies:
  1. List shares returns 200 + code 0
  2. Create share without auth returns 401
  3. Upload fixture file, create share with file_ids -> assert success
  4. Access share -> get share_token
  5. Browse share with valid share_token -> 200 + code 0
  6. Browse share without share_token -> error

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured with seed data
  - Redis configured

Usage:
  uv run test/integration/test_share_browse.py
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
CREATED_SHARE_ID = ""
SHARE_TOKEN = ""

EVIDENCE_PREFIX = "share-browse"


# ─── Helpers ─────────────────────────────────────────────────────────────────


def upload_fixture(token: str, file_size: int = 256) -> str:
    """Upload a test file via chunked upload flow. Returns file_id."""
    path = create_temp_file(file_size)
    file_hash = md5_hash(path)
    filename = unique_name("share_browse_fixture") + ".bin"

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
    log_pass(f"Fixture uploaded — file_id={fid}")
    return fid


# ─── Test 1: List shares ────────────────────────────────────────────────────


def test_list_shares() -> None:
    global TOKEN

    log_info("Testing GET /api/share (list shares)...")

    resp = fetch(
        "/api/share",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        log_pass("GET /api/share returns 200 + code 0")
    else:
        log_fail(f"GET /api/share failed: HTTP {resp.status_code}, code={code}")
        print(resp.text)
        raise SystemExit(1)


# ─── Test 2: Create share requires auth ──────────────────────────────────────


def test_share_requires_auth() -> None:
    log_info("Testing share endpoints require authentication...")

    resp = fetch(
        "/api/share",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={"file_ids": [1], "permission": "download"},
    )

    if resp.status_code == 401:
        log_pass("POST /api/share without token returns 401")
    else:
        log_fail(
            f"POST /api/share without token: expected 401, got HTTP {resp.status_code}"
        )
        print(resp.text)
        raise SystemExit(1)


# ─── Test 3: Create share with fixture file ──────────────────────────────────


def test_create_share() -> None:
    global CREATED_SHARE_ID

    log_info("Testing POST /api/share (create share)...")

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
            "expire_days": 7,
        },
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-create.json", resp.text)

    if resp.status_code != 200:
        log_fail(f"Create share failed: HTTP {resp.status_code}")
        print(resp.text)
        raise SystemExit(1)

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"Create share failed: code={code}")
        print(resp.text)
        raise SystemExit(1)

    share_id = json_field(resp.text, "data.share_id")
    if not share_id or share_id == "null":
        log_fail("Create share returned 200 but no share_id")
        print(resp.text)
        raise SystemExit(1)

    CREATED_SHARE_ID = share_id
    log_pass(f"Create share succeeds: share_id={CREATED_SHARE_ID}")


# ─── Test 4: Access share -> get share_token ─────────────────────────────────


def test_access_share() -> None:
    global SHARE_TOKEN

    log_info(f"Testing POST /api/share/access/{CREATED_SHARE_ID}...")

    resp = fetch(
        f"/api/share/access/{CREATED_SHARE_ID}",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-access.json", resp.text)

    if resp.status_code != 200:
        log_fail(f"Access share failed: HTTP {resp.status_code}")
        print(resp.text)
        raise SystemExit(1)

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"Access share failed: code={code}")
        print(resp.text)
        raise SystemExit(1)

    share_token = json_field(resp.text, "data.share_token")
    if not share_token or share_token == "null":
        log_fail("Access share returned 200 but no share_token")
        print(resp.text)
        raise SystemExit(1)

    SHARE_TOKEN = share_token
    log_pass("Access share returns share_token")


# ─── Test 5: Browse share with valid token -> 200 + code 0 ──────────────────


def test_browse_share_valid_token() -> None:
    log_info(
        f"Testing GET /api/share/browse/{CREATED_SHARE_ID} with valid share_token..."
    )

    resp = fetch(
        f"/api/share/browse/{CREATED_SHARE_ID}",
        headers={"X-Share-Token": SHARE_TOKEN},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-browse-valid.json", resp.text)

    code = json_field(resp.text, "code")
    if resp.status_code == 200 and code == "0":
        log_pass("Browse share with valid token returns 200 + code 0")
    else:
        log_fail(
            f"Browse share with valid token: expected 200 + code 0, got HTTP {resp.status_code}, code={code}"
        )
        print(resp.text)
        raise SystemExit(1)


# ─── Test 6: Browse share without token -> error ────────────────────────────


def test_browse_share_no_token() -> None:
    log_info(f"Testing GET /api/share/browse/{CREATED_SHARE_ID} without share_token...")

    resp = fetch(f"/api/share/browse/{CREATED_SHARE_ID}")

    save_evidence(f"{EVIDENCE_PREFIX}-share-browse-notoken.json", resp.text)

    code = json_field(resp.text, "code")

    if resp.status_code == 401:
        log_pass("Browse without token returns 401")
    elif code != "0":
        log_pass(f"Browse without token returns error code={code}")
    else:
        log_fail(
            f"Browse without token: expected error, got HTTP {resp.status_code} + code=0"
        )
        print(resp.text)
        raise SystemExit(1)


# ─── Main ────────────────────────────────────────────────────────────────────


def main() -> None:
    global TOKEN, FILE_ID

    print("==========================================")
    print("Share Browse Integration Tests")
    print("==========================================")
    print()

    ensure_server()

    # Login for test_list_shares
    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        sys.exit(1)
    TOKEN = token

    test_list_shares()
    test_share_requires_auth()

    # Upload fixture so share creation always has a real file
    FILE_ID = upload_fixture(TOKEN)

    test_create_share()
    test_access_share()
    test_browse_share_valid_token()
    test_browse_share_no_token()

    print()
    print_summary()


if __name__ == "__main__":
    main()

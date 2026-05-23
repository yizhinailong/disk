#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for share management: detail, update, batch cancel.

Verifies:
  1. Upload fixture + create share -> share_id
  2. GET /api/share/{share_id} -> 200 + code 0, data.share_id matches
  3. PUT /api/share/{share_id} permission=view -> 200 + code 0
  4. Verify update: GET again -> data.permission == "view"
  5. Obtain share_token before cancel
  6. DELETE /api/share batch cancel -> 200 + summary.succeeded == 1
  7. Detail after cancel -> error (code != 0)
  8. Access after cancel -> error (code != 0)
  9. Browse with existing share_token after cancel -> error (code != 0)

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured with seed data
  - Redis configured

Usage:
  uv run test/integration/test_share_management.py
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
SHARE_ID = ""
SHARE_TOKEN = ""

EVIDENCE_PREFIX = "share-mgmt"


# ─── Helpers ─────────────────────────────────────────────────────────────────


def upload_fixture(token: str, file_size: int = 256) -> str:
    """Upload a test file via chunked upload flow. Returns file_id."""
    path = create_temp_file(file_size)
    file_hash = md5_hash(path)
    filename = unique_name("share_mgmt_fixture") + ".bin"

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
    log_pass(f"Fixture uploaded — file_id={fid}")
    return fid


# ─── Create share ────────────────────────────────────────────────────────────


def create_share() -> None:
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
            "expire_days": 7,
        },
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-create.json", resp.text)

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"Create share failed: code={code}")
        print(resp.text)
        raise SystemExit(1)

    SHARE_ID = json_field(resp.text, "data.share_id")

    if not SHARE_ID or SHARE_ID == "null":
        log_fail("Create share returned success but no share_id")
        print(resp.text)
        raise SystemExit(1)

    log_pass(f"Share created — share_id={SHARE_ID}")


# ─── Test 1: GET share detail ───────────────────────────────────────────────


def test_get_share_detail() -> None:
    log_info(f"Test: GET /api/share/{SHARE_ID} -> 200 + code 0")

    resp = fetch(
        f"/api/share/{SHARE_ID}",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-detail.json", resp.text)

    if resp.status_code != 200:
        log_fail(f"GET share detail: expected HTTP 200, got {resp.status_code}")
        print(resp.text)
        raise SystemExit(1)

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"GET share detail: expected code 0, got code={code}")
        print(resp.text)
        raise SystemExit(1)

    data_share_id = json_field(resp.text, "data.share_id")
    if data_share_id != SHARE_ID:
        log_fail(
            f"GET share detail: data.share_id mismatch — expected {SHARE_ID}, got {data_share_id}"
        )
        raise SystemExit(1)

    log_pass(f"GET share detail returns correct share_id={data_share_id}")


# ─── Test 2: PUT update share permission ─────────────────────────────────────


def test_update_share() -> None:
    log_info(f"Test: PUT /api/share/{SHARE_ID} with permission=view")

    resp = fetch(
        f"/api/share/{SHARE_ID}",
        method="PUT",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"permission": "view"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-update.json", resp.text)

    if resp.status_code != 200:
        log_fail(f"PUT share update: expected HTTP 200, got {resp.status_code}")
        print(resp.text)
        raise SystemExit(1)

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"PUT share update: expected code 0, got code={code}")
        print(resp.text)
        raise SystemExit(1)

    permission = json_field(resp.text, "data.permission")
    if permission != "view":
        log_fail(f"PUT share update: expected data.permission=view, got '{permission}'")
        raise SystemExit(1)

    log_pass("PUT share update: permission changed to view")


# ─── Test 3: Verify update via GET ───────────────────────────────────────────


def test_verify_update() -> None:
    log_info(f"Test: GET /api/share/{SHARE_ID} again -> permission=view")

    resp = fetch(
        f"/api/share/{SHARE_ID}",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    if resp.status_code != 200:
        log_fail(f"Verify update GET: expected HTTP 200, got {resp.status_code}")
        print(resp.text)
        raise SystemExit(1)

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"Verify update GET: expected code 0, got code={code}")
        print(resp.text)
        raise SystemExit(1)

    permission = json_field(resp.text, "data.permission")
    if permission != "view":
        log_fail(f"Verify update: expected data.permission=view, got '{permission}'")
        raise SystemExit(1)

    log_pass("Verify update: permission persists as view")


# ─── Test 4: Access before cancel ─────────────────────────────────────────────


def test_access_before_cancel() -> None:
    global SHARE_TOKEN

    log_info(f"Test: POST /api/share/access/{SHARE_ID} before cancel -> token")

    resp = fetch(
        f"/api/share/access/{SHARE_ID}",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-access-before-cancel.json", resp.text)

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"Access before cancel: expected code 0, got code={code}")
        print(resp.text)
        raise SystemExit(1)

    SHARE_TOKEN = json_field(resp.text, "data.share_token")
    if not SHARE_TOKEN or SHARE_TOKEN == "null":
        log_fail("Access before cancel: no share_token returned")
        print(resp.text)
        raise SystemExit(1)

    log_pass("Access before cancel returned share_token")


# ─── Test 5: DELETE batch cancel share ───────────────────────────────────────


def test_cancel_share_batch() -> None:
    log_info(f"Test: DELETE /api/share batch cancel share_ids=[{SHARE_ID}]")

    resp = fetch(
        "/api/share",
        method="DELETE",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"share_ids": [SHARE_ID]},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-cancel.json", resp.text)

    if resp.status_code != 200:
        log_fail(f"DELETE batch cancel: expected HTTP 200, got {resp.status_code}")
        print(resp.text)
        raise SystemExit(1)

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"DELETE batch cancel: expected code 0, got code={code}")
        print(resp.text)
        raise SystemExit(1)

    succeeded = json_field(resp.text, "data.summary.succeeded")
    if succeeded != "1":
        log_fail(
            f"DELETE batch cancel: expected summary.succeeded=1, got '{succeeded}'"
        )
        print(resp.text)
        raise SystemExit(1)

    log_pass("DELETE batch cancel: summary.succeeded=1")


# ─── Test 6: Detail after cancel -> error ────────────────────────────────────


def test_detail_after_cancel() -> None:
    log_info(f"Test: GET /api/share/{SHARE_ID} after cancel -> error")

    resp = fetch(
        f"/api/share/{SHARE_ID}",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-detail-after-cancel.json", resp.text)

    code = json_field(resp.text, "code")
    if code == "0":
        log_fail("Detail after cancel: expected error code, got code=0")
        print(resp.text)
        raise SystemExit(1)

    if code == "60001":
        log_pass("Detail after cancel: code=60001 (ShareNotFound)")
    else:
        log_pass(f"Detail after cancel: error code={code} (expected != 0)")


# ─── Test 7: Access after cancel -> error ────────────────────────────────────


def test_access_after_cancel() -> None:
    log_info(f"Test: POST /api/share/access/{SHARE_ID} after cancel -> error")

    resp = fetch(
        f"/api/share/access/{SHARE_ID}",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-access-after-cancel.json", resp.text)

    code = json_field(resp.text, "code")
    if code == "0":
        log_fail("Access after cancel: expected error code, got code=0")
        print(resp.text)
        raise SystemExit(1)

    if code == "60001":
        log_pass("Access after cancel: code=60001 (ShareNotFound)")
    else:
        log_pass(f"Access after cancel: error code={code} (expected != 0)")


# ─── Test 8: Browse with old token after cancel -> error ──────────────────────


def test_browse_with_old_token_after_cancel() -> None:
    log_info(f"Test: GET /api/share/browse/{SHARE_ID} with old token after cancel -> error")

    resp = fetch(
        f"/api/share/browse/{SHARE_ID}",
        headers={"X-Share-Token": SHARE_TOKEN},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-share-browse-old-token-after-cancel.json", resp.text)

    code = json_field(resp.text, "code")
    if code == "0":
        log_fail("Browse with old token after cancel: expected error code, got code=0")
        print(resp.text)
        raise SystemExit(1)

    if code == "60002":
        log_pass("Browse with old token after cancel: code=60002 (ShareExpired)")
    else:
        log_pass(f"Browse with old token after cancel: error code={code} (expected != 0)")


# ─── Main ────────────────────────────────────────────────────────────────────


def main() -> None:
    global TOKEN, FILE_ID

    print("==========================================")
    print("Share Management Integration Tests")
    print("==========================================")
    print()

    check_server() or sys.exit(1)

    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        sys.exit(1)
    TOKEN = token

    FILE_ID = upload_fixture(TOKEN)
    create_share()

    print()
    print("==========================================")
    print("Running Share Management Tests")
    print("==========================================")
    print()

    test_get_share_detail()
    test_update_share()
    test_verify_update()
    test_access_before_cancel()
    test_cancel_share_batch()
    test_detail_after_cancel()
    test_access_after_cancel()
    test_browse_with_old_token_after_cancel()

    print()
    print_summary()


if __name__ == "__main__":
    main()

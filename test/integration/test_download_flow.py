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
  - Missing/size-mismatched final Blob: 500/50011 plus reconciliation finding
  - Owner download rate limit: 429/10005 plus response/log correlation

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
import tempfile
import time
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", ".sisyphus/evidence"))
SERVER_LOG_PATH = EVIDENCE_ROOT / "download-flow-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_step,
    print_summary,
    save_evidence,
    save_raw_evidence,
    ensure_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    header_value,
    md5_hash,
    assert_status,
    assert_header_contains,
    assert_json_field,
    query_one,
    local_blob_path,
    redis_delete_pattern,
    redis_set_value,
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
FILE_CONTENT = b""
FILE_NAME = ""
SHARE_ID = ""
SHARE_TOKEN = ""
SHARE_FILE_ID = ""
SHARE_ACCESS_BODY = ""


def configured_download_rate_value(key, fallback):
    """Read one positive owner-download rate-limit value from active config."""
    config_path = Path(os.environ.get("DISK_CONFIG_FILE", REPO_ROOT / "config.json"))
    if not config_path.is_absolute():
        config_path = REPO_ROOT / config_path
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
        value = int(config.get("custom_config", {}).get("disk", {}).get(key, fallback))
        return value if value > 0 else fallback
    except Exception:
        return fallback


def file_download_metadata():
    row = query_one(
        "SELECT download_count, last_accessed_at FROM files WHERE id = %s",
        (int(FILE_ID),),
    )
    if row is None:
        log_fail(f"file metadata row missing: file_id={FILE_ID}")
        return {"download_count": -1, "last_accessed_at": None}
    return row


def file_blob_path():
    row = query_one(
        "SELECT fc.storage_path FROM files f JOIN file_contents fc ON fc.id = f.content_id WHERE f.id = %s",
        (int(FILE_ID),),
    )
    if row is None or not row["storage_path"]:
        log_fail(f"file storage_path missing: file_id={FILE_ID}")
        print_summary()
    return local_blob_path(str(row["storage_path"]))


def file_content_id():
    row = query_one(
        "SELECT content_id FROM files WHERE id = %s",
        (int(FILE_ID),),
    )
    if row is None:
        log_fail(f"file content_id missing: file_id={FILE_ID}")
        return 0
    return int(row["content_id"])


def reconciliation_finding(finding_type):
    return query_one(
        "SELECT finding_type, resource_id, resource_locator, details, resolved_at "
        "FROM storage_reconciliation_findings "
        "WHERE finding_type = %s AND resource_id = %s",
        (finding_type, str(file_content_id())),
    )


def latest_share_download_audit():
    return query_one(
        "SELECT details FROM operation_logs "
        "WHERE action = 'share_download' AND details->>'share_code' = %s "
        "ORDER BY id DESC LIMIT 1",
        (SHARE_ID,),
    )


def correlation_details(value):
    if isinstance(value, str):
        return json.loads(value)
    return value if isinstance(value, dict) else {}


def assert_correlation_details(label, value, request_id):
    details = correlation_details(value)
    ok = True
    if details.get("request_id") == request_id:
        log_pass(f"{label}: request_id persisted")
    else:
        log_fail(
            f"{label}: expected request_id {request_id}, "
            f"got {details.get('request_id')!r}"
        )
        ok = False
    if details.get("operation") == "download":
        log_pass(f"{label}: bounded download operation persisted")
    else:
        log_fail(
            f"{label}: expected operation download, "
            f"got {details.get('operation')!r}"
        )
        ok = False
    return ok


def wait_for_correlated_download_log(request_id, instance_id, message_marker):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if SERVER_LOG_PATH.is_file():
            for line in SERVER_LOG_PATH.read_text(
                encoding="utf-8",
                errors="replace",
            ).splitlines():
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if (
                    isinstance(record, dict)
                    and record.get("schema_version") == 1
                    and record.get("source") == "application"
                    and record.get("request_id") == request_id
                    and record.get("instance_id") == instance_id
                    and record.get("operation") == "download"
                    and record.get("upload_id") is None
                    and record.get("job_id") is None
                    and record.get("lease_owner") is None
                    and record.get("state_version") is None
                    and message_marker in str(record.get("message", ""))
                ):
                    return record
        time.sleep(0.05)

    log_fail(
        f"download log preserves typed correlation for request_id={request_id}"
    )
    print_summary()
    raise AssertionError("unreachable")


def assert_unresolved_finding(label, finding_type):
    row = reconciliation_finding(finding_type)
    if row is None:
        log_fail(f"{label}: reconciliation finding {finding_type} missing")
        return False
    if row["resolved_at"] is not None:
        log_fail(f"{label}: reconciliation finding unexpectedly resolved")
        return False
    if str(row["resource_id"]) != str(file_content_id()):
        log_fail(f"{label}: reconciliation resource_id mismatch")
        return False
    log_pass(f"{label}: unresolved {finding_type} finding recorded")
    return True


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


def make_ascii_content(size):
    pattern = f"download-flow-{os.getpid()}-".encode("ascii")
    repeats = (size // len(pattern)) + 1
    return (pattern * repeats)[:size]


def write_fixture(content, suffix=".bin"):
    fd, path = tempfile.mkstemp(suffix=suffix, prefix="download_flow_")
    with os.fdopen(fd, "wb") as f:
        f.write(content)
    return path


def assert_response_body(label, resp, expected):
    actual = resp.text.encode("utf-8")
    if actual == expected:
        log_pass(f"{label}: response body bytes match fixture")
        return True
    log_fail(f"{label}: expected body length {len(expected)}, got {len(actual)}")
    return False


# ─── Phase 2: Upload a test file ───────────────────────────────────────────


def do_upload():
    global FILE_ID, FILE_SIZE, FILE_HASH, FILE_CONTENT, FILE_NAME

    log_step("Uploading test fixture file...")

    FILE_NAME = f"download_test_{os.getpid()}.bin"
    FILE_CONTENT = make_ascii_content(256)
    fixture = write_fixture(FILE_CONTENT, suffix=".bin")
    FILE_SIZE = len(FILE_CONTENT)
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
        "file-200", resp.headers, "Content-Disposition", FILE_NAME
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
    assert_response_body("file-200", resp, FILE_CONTENT) or (ok := False)

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
    assert_response_body("file-206", resp, FILE_CONTENT[:10]) or (ok := False)

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


def test_file_download_not_found():
    missing_id = 999999999
    log_step(f"Test: GET /api/file/download/{missing_id} → 404")

    resp = fetch(
        f"/api/file/download/{missing_id}",
        method="GET",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-file-404.json", resp.text)

    ok = True
    assert_status("file-404", resp.status_code, 404) or (ok := False)
    try:
        body = json.loads(resp.text)
        if "code" not in body or "message" not in body:
            log_fail("file-404: missing error envelope fields")
            ok = False
    except Exception:
        log_fail("file-404: invalid JSON body")
        ok = False

    if ok:
        log_pass("file-404: missing private download maps to error envelope")


def test_owner_download_rate_limit_correlation():
    log_step("Test: owner download rate-limit 429 preserves request correlation")

    user = query_one(
        "SELECT id FROM users WHERE username = %s OR email = %s ORDER BY id LIMIT 1",
        (TEST_USER, TEST_USER),
    )
    if user is None:
        log_fail("owner-rate-limit: authenticated user row is missing")
        return

    limit = configured_download_rate_value("download_rate_limit_per_minute", 60)
    window_seconds = configured_download_rate_value(
        "download_rate_limit_window_seconds",
        60,
    )
    now = time.time()
    window_start = (int(now) // window_seconds) * window_seconds
    seconds_until_reset = window_start + window_seconds - now
    if seconds_until_reset < 2:
        time.sleep(seconds_until_reset + 0.05)
        now = time.time()
        window_start = (int(now) // window_seconds) * window_seconds

    user_id = int(user["id"])
    rate_key = f"rate:download:{user_id}:{window_start}"
    request_id = f"download-rate-limit-{uuid.uuid4()}"
    raw_range = "bytes=17-23"
    metadata_before = file_download_metadata()

    try:
        redis_set_value(
            rate_key,
            str(limit),
            max(1, window_start + window_seconds - int(time.time())),
        )
        resp = fetch(
            f"/api/file/download/{FILE_ID}",
            method="GET",
            headers={
                "Authorization": f"Bearer {TOKEN}",
                "Range": raw_range,
                "X-Request-Id": request_id,
            },
        )
        metadata_after = file_download_metadata()
        save_evidence(f"{EVIDENCE_PREFIX}-owner-rate-limit.json", resp.text)

        ok = True
        assert_status("owner-rate-limit", resp.status_code, 429) or (ok := False)
        assert_json_field(
            "owner-rate-limit-code",
            resp.text,
            "code",
            "10005",
        ) or (ok := False)
        expected_headers = {
            "X-RateLimit-Limit": str(limit),
            "X-RateLimit-Remaining": "0",
            "X-Request-Id": request_id,
        }
        for header_name, expected_value in expected_headers.items():
            actual_value = header_value(resp.headers, header_name)
            if actual_value == expected_value:
                log_pass(f"owner-rate-limit: {header_name} is exact")
            else:
                log_fail(
                    f"owner-rate-limit: expected {header_name}={expected_value!r}, "
                    f"got {actual_value!r}"
                )
                ok = False
        for header_name in ("X-RateLimit-Reset", "Retry-After"):
            if header_value(resp.headers, header_name):
                log_pass(f"owner-rate-limit: {header_name} is present")
            else:
                log_fail(f"owner-rate-limit: {header_name} is missing")
                ok = False
        assert_file_metadata_unchanged(
            "owner-rate-limit",
            metadata_before,
            metadata_after,
        ) or (ok := False)

        instance_id = header_value(resp.headers, "X-Disk-Instance-Id")
        if instance_id:
            rate_log = wait_for_correlated_download_log(
                request_id,
                instance_id,
                "Download rate limit:",
            )
            if rate_log.get("level") == "warning":
                log_pass("owner-rate-limit: warning preserves bounded correlation")
            else:
                log_fail("owner-rate-limit: rejection log is not warning level")
                ok = False
        else:
            log_fail("owner-rate-limit: response lacks handling instance")
            ok = False

        log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
        if any(value and value in log_text for value in (TEST_PASS, TOKEN, raw_range)):
            log_fail("owner-rate-limit: managed log contains credentials or Range")
            ok = False
        else:
            log_pass("owner-rate-limit: managed log excludes credentials and Range")

        if ok:
            log_pass("owner-rate-limit: 429 correlation and side-effect contract preserved")
    finally:
        redis_delete_pattern(f"rate:download:{user_id}:*")


def test_missing_final_blob_error_mapping_and_side_effects():
    log_step("Test: missing final blob maps to 50011 and records reconciliation")

    blob_path = file_blob_path()
    backup_path = blob_path.with_suffix(blob_path.suffix + ".missing-download-test")
    if not blob_path.exists():
        log_fail(f"missing-blob setup: final blob does not exist: {blob_path}")
        return

    os.replace(blob_path, backup_path)
    try:
        owner_request_id = f"download-owner-missing-{os.getpid()}"
        file_before = file_download_metadata()
        private_resp = fetch(
            f"/api/file/download/{FILE_ID}",
            method="GET",
            headers={
                "Authorization": f"Bearer {TOKEN}",
                "X-Request-Id": owner_request_id,
            },
        )
        file_after = file_download_metadata()
        save_evidence(f"{EVIDENCE_PREFIX}-file-missing-blob.json", private_resp.text)

        ok = True
        assert_status("file-missing-blob", private_resp.status_code, 500) or (ok := False)
        assert_json_field("file-missing-blob", private_resp.text, "code", "50011") or (ok := False)
        assert_header_contains(
            "file-missing-blob",
            private_resp.headers,
            "X-Request-Id",
            owner_request_id,
        ) or (ok := False)
        assert_file_metadata_unchanged("file-missing-blob", file_before, file_after) or (ok := False)
        assert_unresolved_finding("file-missing-blob", "missing_final_blob") or (ok := False)
        owner_instance_id = header_value(
            private_resp.headers,
            "X-Disk-Instance-Id",
        )
        if owner_instance_id:
            wait_for_correlated_download_log(
                owner_request_id,
                owner_instance_id,
                "Download Blob preflight rejected content",
            )
            log_pass("file-missing-blob: responder log preserves request/instance")
        else:
            log_fail("file-missing-blob: response lacks handling instance")
            ok = False
        owner_finding = reconciliation_finding("missing_final_blob")
        if owner_finding is None:
            ok = False
        else:
            assert_correlation_details(
                "file-missing-blob finding",
                owner_finding["details"],
                owner_request_id,
            ) or (ok := False)

        share_request_id = f"download-share-missing-{os.getpid()}"
        share_file_before = file_download_metadata()
        share_before = share_download_count()
        share_resp = fetch(
            f"/api/share/download/{SHARE_ID}/{SHARE_FILE_ID}",
            method="GET",
            headers={
                "X-Share-Token": SHARE_TOKEN,
                "X-Request-Id": share_request_id,
            },
        )
        share_file_after = file_download_metadata()
        share_after = share_download_count()
        save_evidence(f"{EVIDENCE_PREFIX}-share-missing-blob.json", share_resp.text)

        assert_status("share-missing-blob", share_resp.status_code, 500) or (ok := False)
        assert_json_field("share-missing-blob", share_resp.text, "code", "50011") or (ok := False)
        assert_header_contains(
            "share-missing-blob",
            share_resp.headers,
            "X-Request-Id",
            share_request_id,
        ) or (ok := False)
        assert_file_metadata_unchanged("share-missing-blob", share_file_before, share_file_after) or (ok := False)
        assert_share_download_delta("share-missing-blob", share_before, share_after, 1) or (ok := False)
        assert_unresolved_finding("share-missing-blob", "missing_final_blob") or (ok := False)
        share_instance_id = header_value(
            share_resp.headers,
            "X-Disk-Instance-Id",
        )
        if share_instance_id:
            wait_for_correlated_download_log(
                share_request_id,
                share_instance_id,
                "Download Blob preflight rejected content",
            )
            log_pass("share-missing-blob: responder log preserves request/instance")
        else:
            log_fail("share-missing-blob: response lacks handling instance")
            ok = False
        share_finding = reconciliation_finding("missing_final_blob")
        if share_finding is None:
            ok = False
        else:
            assert_correlation_details(
                "share-missing-blob finding",
                share_finding["details"],
                share_request_id,
            ) or (ok := False)
        share_audit = latest_share_download_audit()
        if share_audit is None:
            log_fail("share-missing-blob: share_download audit row missing")
            ok = False
        else:
            assert_correlation_details(
                "share-missing-blob audit",
                share_audit["details"],
                share_request_id,
            ) or (ok := False)

        if ok:
            log_pass("missing-blob: private/share 50011 and reconciliation contract preserved")
    finally:
        os.replace(backup_path, blob_path)


def test_final_blob_size_mismatch_records_reconciliation():
    log_step("Test: final blob size mismatch maps to 50011 and records reconciliation")

    blob_path = file_blob_path()
    if not blob_path.exists():
        log_fail(f"size-mismatch setup: final blob does not exist: {blob_path}")
        return

    with open(blob_path, "ab") as blob:
        blob.write(b"x")

    try:
        metadata_before = file_download_metadata()
        resp = fetch(
            f"/api/file/download/{FILE_ID}",
            method="GET",
            headers={"Authorization": f"Bearer {TOKEN}"},
        )
        metadata_after = file_download_metadata()
        save_evidence(f"{EVIDENCE_PREFIX}-file-size-mismatch.json", resp.text)

        ok = True
        assert_status("file-size-mismatch", resp.status_code, 500) or (ok := False)
        assert_json_field("file-size-mismatch", resp.text, "code", "50011") or (ok := False)
        assert_file_metadata_unchanged("file-size-mismatch", metadata_before, metadata_after) or (ok := False)
        assert_unresolved_finding(
            "file-size-mismatch", "final_blob_size_mismatch"
        ) or (ok := False)

        if ok:
            log_pass("size-mismatch: 50011 and reconciliation contract preserved")
    finally:
        with open(blob_path, "r+b") as blob:
            blob.truncate(FILE_SIZE)


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
        "share-200", resp.headers, "Content-Disposition", FILE_NAME
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
    assert_response_body("share-200", resp, FILE_CONTENT) or (ok := False)

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
    assert_response_body("share-206", resp, FILE_CONTENT[:10]) or (ok := False)

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
        f"FILE_NAME: {FILE_NAME}\n"
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

    SERVER_LOG_PATH.unlink(missing_ok=True)
    ensure_server()

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
    test_file_download_not_found()
    test_owner_download_rate_limit_correlation()
    test_missing_final_blob_error_mapping_and_side_effects()
    test_final_blob_size_mismatch_records_reconciliation()

    # Share file download tests
    test_share_download_200()
    test_share_download_206()
    test_share_download_416()

    write_summary_evidence()

    print_summary()


if __name__ == "__main__":
    main()

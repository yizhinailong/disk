#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for trash lifecycle operations (restore, permanent delete, empty all).

Covers:
  - Upload file A -> soft-delete -> restore -> verify file is active again
  - Upload file B -> soft-delete -> permanent delete -> verify file is gone
  - File appears in trash after soft-delete
  - Restore operation returns success status (with new file_id — restore creates a new active row)
  - Permanent delete operation returns success status
  - Empty all trash clears remaining test data
  - Verify restored file is NOT in trash after restore
  - Verify deleted file is NOT accessible after permanent delete

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured
  - Redis configured
  - User account exists (default: admin / Admin123)

Usage:
  uv run test/integration/test_trash_lifecycle.py
"""

import base64
import json
import os
import sys
import time
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", REPO_ROOT / ".sisyphus/evidence"))
SERVER_LOG_PATH = EVIDENCE_ROOT / "trash-lifecycle-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_step,
    log_section,
    print_summary,
    save_evidence,
    ensure_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    header_value,
    redis_delete_pattern,
    redis_set_value,
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
FILE_A_ID = ""
FILE_B_ID = ""
TRASH_A_ID = ""
TRASH_B_ID = ""
RESTORED_FILE_A_ID = ""

EVIDENCE_PREFIX = "task-6-trash-lifecycle"
API_RATE_LIMIT = 100
API_RATE_WINDOW_SECONDS = 60


# ─── Helpers ─────────────────────────────────────────────────────────────────


def access_token_subject(token: str) -> int:
    """Read the trusted local access token subject for test-key ownership."""
    parts = token.split(".")
    if len(parts) != 3:
        raise ValueError("access token is not a compact JWT")
    payload_segment = parts[1] + "=" * (-len(parts[1]) % 4)
    payload = json.loads(base64.urlsafe_b64decode(payload_segment).decode("utf-8"))
    if payload.get("iss") != "disk" or payload.get("type") != "access":
        raise ValueError("token payload is not a disk access token")
    subject = str(payload.get("sub", ""))
    if not subject.isdigit() or int(subject) <= 0:
        raise ValueError("access token subject is not a positive user ID")
    return int(subject)


def wait_for_trash_rate_logs(request_id: str, instance_id: str) -> list[dict]:
    """Return the correlated rejection and failure-duration records."""
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        records: list[dict] = []
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
                    and record.get("operation") == "trash"
                    and record.get("upload_id") is None
                    and record.get("job_id") is None
                    and record.get("lease_owner") is None
                    and record.get("state_version") is None
                ):
                    records.append(record)
        messages = [str(record.get("message", "")) for record in records]
        if (
            any("API rate limit:" in message for message in messages)
            and any("outcome=failure" in message for message in messages)
        ):
            return records
        time.sleep(0.05)
    return []


def trash_response_contains_id(body: str, trash_id: str) -> bool:
    """Return whether a trash list response contains one exact item ID."""
    data = json.loads(body)
    return any(
        str(item.get("id")) == trash_id
        for item in data.get("data", {}).get("items", [])
    )


def upload_fixture(token: str, suffix: str, file_size: int = 256) -> str:
    """Upload a test file via chunked upload flow. Returns file_id."""
    path = create_temp_file(file_size)
    file_hash = md5_hash(path)
    filename = unique_name(suffix) + ".bin"

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
        save_evidence(f"{EVIDENCE_PREFIX}-upload-{suffix}-init.json", init_resp.text)
        os.unlink(path)
        return fid

    upload_id = json_field(init_resp.text, "data.upload_id")
    if not upload_id or upload_id == "null":
        log_fail("Init upload failed")
        print(init_resp.text)
        os.unlink(path)
        raise SystemExit(1)
    save_evidence(f"{EVIDENCE_PREFIX}-upload-{suffix}-init.json", init_resp.text)

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
    save_evidence(f"{EVIDENCE_PREFIX}-upload-{suffix}-chunk.json", chunk_resp.text)

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
    save_evidence(
        f"{EVIDENCE_PREFIX}-upload-{suffix}-complete.json", complete_resp.text
    )

    os.unlink(path)
    log_pass(f"File uploaded — file_id={fid}")
    return fid


def soft_delete_file(file_id: str, label: str) -> None:
    log_step(f"Soft-delete file ({label}): file_id={file_id}")

    resp = fetch(
        "/api/file",
        method="DELETE",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"file_ids": [int(file_id)]},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-delete-{label}.json", resp.text)

    ok = True
    assert_json_field(f"delete-{label}", resp.text, "code", "0") or (ok := False)
    assert_json_field_numeric_gt(
        f"delete-{label}", resp.text, "data.deleted_count", 0
    ) or (ok := False)

    if ok:
        log_pass(f"delete-{label}: file moved to trash successfully")


def get_trash_id(file_id: str, label: str) -> str:
    log_step(f"Get trash_id for file ({label}): file_id={file_id}")

    resp = fetch(
        "/api/trash",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-trash-list-{label}.json", resp.text)

    trash_id = ""
    try:
        data = json.loads(resp.text)
        for item in data.get("data", {}).get("items", []):
            if item.get("original_id") == int(file_id):
                trash_id = str(item["id"])
                break
    except Exception:
        pass

    if trash_id:
        log_pass(f"trash_id found for file {file_id}: {trash_id}")
    else:
        log_fail(f"trash_id not found for file {file_id}")

    return trash_id


# ─── Test 1: Upload fixtures ────────────────────────────────────────────────


def test_upload_fixtures() -> None:
    global FILE_A_ID, FILE_B_ID

    log_section("Upload Fixtures")

    FILE_A_ID = upload_fixture(TOKEN, "restore_test")
    log_info(f"File A uploaded: {FILE_A_ID}")

    FILE_B_ID = upload_fixture(TOKEN, "delete_test")
    log_info(f"File B uploaded: {FILE_B_ID}")


# ─── Test 2: Soft delete both files ─────────────────────────────────────────


def test_soft_delete() -> None:
    log_section("Soft Delete Files")

    soft_delete_file(FILE_A_ID, "file-a")
    soft_delete_file(FILE_B_ID, "file-b")


# ─── Test 3: Verify files in trash ──────────────────────────────────────────


def test_verify_in_trash() -> None:
    log_section("Verify Files in Trash")

    resp = fetch(
        "/api/trash",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-trash-list-verify.json", resp.text)

    ok = True
    assert_json_field("trash-list-verify", resp.text, "code", "0") or (ok := False)

    # Verify file A is in trash
    found_a = False
    try:
        data = json.loads(resp.text)
        for item in data.get("data", {}).get("items", []):
            if item.get("original_id") == int(FILE_A_ID):
                found_a = True
                break
    except Exception:
        pass

    if found_a:
        log_pass(f"File A ({FILE_A_ID}) found in trash")
    else:
        log_fail(f"File A ({FILE_A_ID}) NOT found in trash")
        ok = False

    # Verify file B is in trash
    found_b = False
    try:
        data = json.loads(resp.text)
        for item in data.get("data", {}).get("items", []):
            if item.get("original_id") == int(FILE_B_ID):
                found_b = True
                break
    except Exception:
        pass

    if found_b:
        log_pass(f"File B ({FILE_B_ID}) found in trash")
    else:
        log_fail(f"File B ({FILE_B_ID}) NOT found in trash")
        ok = False

    if ok:
        log_pass("trash-verify: both files confirmed in trash")


# ─── Test 4: Save trash IDs ─────────────────────────────────────────────────


def test_save_trash_ids() -> None:
    global TRASH_A_ID, TRASH_B_ID

    log_section("Save Trash IDs")

    TRASH_A_ID = get_trash_id(FILE_A_ID, "file-a")
    TRASH_B_ID = get_trash_id(FILE_B_ID, "file-b")

    if not TRASH_A_ID or not TRASH_B_ID:
        log_fail("Failed to retrieve trash IDs")
        raise SystemExit(1)

    log_info(f"Trash IDs saved: A={TRASH_A_ID}, B={TRASH_B_ID}")


# ─── Test 5: Restore file A ─────────────────────────────────────────────────


def test_restore_file() -> None:
    global RESTORED_FILE_A_ID

    log_section("Restore File A")

    log_step(f"Restore file A: trash_id={TRASH_A_ID}")

    resp = fetch(
        "/api/trash/restore",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"trash_ids": [int(TRASH_A_ID)]},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-restore-file-a.json", resp.text)

    ok = True
    assert_json_field("restore-file-a", resp.text, "code", "0") or (ok := False)

    status = json_field(resp.text, "data.results.0.status")
    if status == "success":
        log_pass("restore-file-a: status = success")
    else:
        log_fail(f"restore-file-a: expected status=success, got '{status}'")
        ok = False

    # Restore creates a new active file row; the returned file_id is the new ID,
    # not the original FILE_A_ID. Capture it for the subsequent active-file check.
    restored_file_id = json_field(resp.text, "data.results.0.file_id")
    if restored_file_id and restored_file_id != "null":
        RESTORED_FILE_A_ID = restored_file_id
        log_pass(f"restore-file-a: new file_id = {restored_file_id}")
    else:
        log_fail("restore-file-a: no file_id in restore response")
        ok = False

    if ok:
        log_pass("restore-file-a: restore operation successful")


# ─── Test 6: Verify file A is active ────────────────────────────────────────


def test_verify_active_after_restore() -> None:
    log_section("Verify File A is Active After Restore")

    if not RESTORED_FILE_A_ID:
        log_fail("file-a-active: no restored file ID available (restore failed?)")
        raise SystemExit(1)

    log_step(f"Get restored file details: file_id={RESTORED_FILE_A_ID}")

    resp = fetch(
        f"/api/file/{RESTORED_FILE_A_ID}",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-file-a-active.json", resp.text)

    ok = True
    assert_json_field("file-a-active", resp.text, "code", "0") or (ok := False)

    file_id_in_resp = json_field(resp.text, "data.id")
    if not file_id_in_resp or file_id_in_resp == "null":
        file_id_in_resp = json_field(resp.text, "data.file.id")
    if file_id_in_resp == RESTORED_FILE_A_ID:
        log_pass("file-a-active: restored file is accessible with correct id")
    else:
        log_fail(
            f"file-a-active: expected file id={RESTORED_FILE_A_ID}, got '{file_id_in_resp}'"
        )
        ok = False

    if ok:
        log_pass("file-a-active: file A is active and accessible")


# ─── Test 7: Verify file A NOT in trash ─────────────────────────────────────


def test_verify_not_in_trash_after_restore() -> None:
    log_section("Verify File A NOT in Trash After Restore")

    log_step(f"Check trash for file A: file_id={FILE_A_ID}")

    resp = fetch(
        "/api/trash",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-trash-list-after-restore.json", resp.text)

    found = False
    try:
        data = json.loads(resp.text)
        for item in data.get("data", {}).get("items", []):
            if item.get("original_id") == int(FILE_A_ID):
                found = True
                break
    except Exception:
        pass

    if not found:
        log_pass("file-a-not-in-trash: file A is NOT in trash (correct)")
    else:
        log_fail("file-a-not-in-trash: file A is still in trash (should not be)")
        raise SystemExit(1)


# ─── Test 8: Permanent delete file B ────────────────────────────────────────


def test_permanent_delete_file() -> None:
    log_section("Permanent Delete File B")

    log_step(f"Permanently delete file B: trash_id={TRASH_B_ID}")

    resp = fetch(
        "/api/trash",
        method="DELETE",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "Content-Type": "application/json",
        },
        json_body={"trash_ids": [int(TRASH_B_ID)]},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-permanent-delete-file-b.json", resp.text)

    ok = True
    assert_json_field("permanent-delete-file-b", resp.text, "code", "0") or (
        ok := False
    )

    # Verify results[0].status == "success"
    status = json_field(resp.text, "data.results.0.status")
    if status == "success":
        log_pass("permanent-delete-file-b: status = success")
    else:
        log_fail(f"permanent-delete-file-b: expected status=success, got '{status}'")
        ok = False

    # Verify freed_space is present
    freed_space = json_field(resp.text, "data.results.0.freed_space")
    if freed_space and freed_space != "null":
        log_pass(f"permanent-delete-file-b: freed_space = {freed_space}")
    else:
        log_fail("permanent-delete-file-b: freed_space not found")
        ok = False

    if ok:
        log_pass("permanent-delete-file-b: permanent delete successful")


# ─── Test 9: Verify file B is gone ──────────────────────────────────────────


def test_verify_file_b_gone() -> None:
    log_section("Verify File B is Gone After Permanent Delete")

    log_step(f"Try to get file B details: file_id={FILE_B_ID}")

    resp = fetch(
        f"/api/file/{FILE_B_ID}",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-file-b-gone.json", resp.text)

    code = json_field(resp.text, "code") or "-1"

    if code != "0":
        log_pass(f"file-b-gone: file B is NOT accessible (code={code})")
    else:
        log_fail("file-b-gone: file B is still accessible (should be deleted)")
        raise SystemExit(1)


# ─── Test 10: Trash rate-limit rejection ───────────────────────────────────


def test_trash_rate_limit_correlation() -> None:
    log_section("Trash Rate-Limit Rejection")

    probe_file_id = upload_fixture(TOKEN, "rate_limit_probe")
    soft_delete_file(probe_file_id, "rate-limit-probe")
    probe_trash_id = get_trash_id(probe_file_id, "rate-limit-probe")
    if not probe_trash_id:
        raise SystemExit(1)

    try:
        user_id = access_token_subject(TOKEN)
    except (ValueError, json.JSONDecodeError) as error:
        log_fail(f"Trash rate-limit token subject is unavailable: {error}")
        raise SystemExit(1) from error

    now = time.time()
    window_start = (int(now) // API_RATE_WINDOW_SECONDS) * API_RATE_WINDOW_SECONDS
    seconds_until_reset = window_start + API_RATE_WINDOW_SECONDS - now
    if seconds_until_reset < 2:
        time.sleep(seconds_until_reset + 0.05)
        now = time.time()
        window_start = (int(now) // API_RATE_WINDOW_SECONDS) * API_RATE_WINDOW_SECONDS

    rate_key = f"rate:api:{user_id}:{window_start}"
    request_id = f"trash-rate-limit-{uuid.uuid4()}"
    ok = True
    try:
        redis_set_value(
            rate_key,
            str(API_RATE_LIMIT),
            max(1, window_start + API_RATE_WINDOW_SECONDS - int(time.time())),
        )
        response = fetch(
            "/api/trash/all",
            method="DELETE",
            headers={
                "Authorization": f"Bearer {TOKEN}",
                "X-Request-Id": request_id,
            },
        )
        save_evidence(f"{EVIDENCE_PREFIX}-rate-limit.json", response.text)

        if response.status_code == 429 and json_field(response.text, "code") == "10005":
            log_pass("trash-rate-limit: returned HTTP 429 and code 10005")
        else:
            log_fail(
                "trash-rate-limit: response drifted: "
                f"HTTP {response.status_code}, code={json_field(response.text, 'code')}"
            )
            ok = False

        expected_headers = {
            "X-RateLimit-Limit": str(API_RATE_LIMIT),
            "X-RateLimit-Remaining": "0",
            "X-Request-Id": request_id,
        }
        if all(
            header_value(response.headers, name) == expected
            for name, expected in expected_headers.items()
        ) and header_value(response.headers, "X-RateLimit-Reset"):
            log_pass("trash-rate-limit: response headers preserve the fixed-window contract")
        else:
            log_fail("trash-rate-limit: response headers drifted")
            ok = False
        if header_value(response.headers, "Retry-After"):
            log_fail("trash-rate-limit: Retry-After must remain absent")
            ok = False
        else:
            log_pass("trash-rate-limit: Retry-After remains absent")

        instance_id = header_value(response.headers, "X-Disk-Instance-Id")
        records = wait_for_trash_rate_logs(request_id, instance_id) if instance_id else []
        if records:
            log_pass("trash-rate-limit: warning and failure duration preserve correlation")
        else:
            log_fail("trash-rate-limit: correlated rejection logs are missing")
            ok = False

        serialized_records = "\n".join(
            json.dumps(record, ensure_ascii=False, sort_keys=True)
            for record in records
        )
        if any(value and value in serialized_records for value in (TEST_PASS, TOKEN, probe_trash_id)):
            log_fail("trash-rate-limit: correlated logs contain credentials or trash IDs")
            ok = False
        else:
            log_pass("trash-rate-limit: correlated logs exclude credentials and trash IDs")
    finally:
        redis_delete_pattern(f"rate:api:{user_id}:*")

    list_response = fetch(
        "/api/trash",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )
    if list_response.status_code == 200 and trash_response_contains_id(
        list_response.text,
        probe_trash_id,
    ):
        log_pass("trash-rate-limit: rejected delete-all preserved the probe item")
    else:
        log_fail("trash-rate-limit: rejected delete-all removed the probe item")
        ok = False

    if not ok:
        raise SystemExit(1)


# ─── Test 11: Empty all trash ───────────────────────────────────────────────


def test_empty_all_trash() -> None:
    log_section("Empty All Trash")

    log_step("Empty all trash items")

    resp = fetch(
        "/api/trash/all",
        method="DELETE",
        headers={"Authorization": f"Bearer {TOKEN}"},
    )

    save_evidence(f"{EVIDENCE_PREFIX}-empty-all-trash.json", resp.text)

    ok = True
    assert_json_field("empty-all-trash", resp.text, "code", "0") or (ok := False)

    # Verify deleted_count is present
    deleted_count = json_field(resp.text, "data.deleted_count")
    if deleted_count and deleted_count != "null":
        log_pass(f"empty-all-trash: deleted_count = {deleted_count}")
    else:
        log_fail("empty-all-trash: deleted_count not found")
        ok = False

    if ok:
        log_pass("empty-all-trash: operation successful")


# ─── Main ────────────────────────────────────────────────────────────────────


def main() -> None:
    global TOKEN

    print("==========================================")
    print("Trash Lifecycle Integration Tests")
    print("==========================================")
    print()

    SERVER_LOG_PATH.unlink(missing_ok=True)
    ensure_server()

    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        sys.exit(1)
    TOKEN = token

    print()
    print("==========================================")
    print("Running Trash Lifecycle Tests")
    print("==========================================")
    print()

    test_upload_fixtures()
    test_soft_delete()
    test_verify_in_trash()
    test_save_trash_ids()
    test_restore_file()
    test_verify_active_after_restore()
    test_verify_not_in_trash_after_restore()
    test_permanent_delete_file()
    test_verify_file_b_gone()
    test_trash_rate_limit_correlation()
    test_empty_all_trash()

    print()
    print_summary()


if __name__ == "__main__":
    main()

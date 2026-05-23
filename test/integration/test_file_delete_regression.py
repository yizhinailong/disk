#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""
Regression tests for DELETE /api/file contract (Task 5).

Covers the two critical post-fix scenarios:
  1. Valid file soft-delete succeeds, file appears in trash.
  2. Invalid (folder-style / non-existent) delete returns structured
     JSON error without dropping the connection.

Uses ONLY Python stdlib (urllib) — no httpx dependency.

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL and Redis configured

Usage:
  python3 test/integration/test_file_delete_regression.py
  uv run test/integration/test_file_delete_regression.py
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
import time
import urllib.error
import urllib.request
from typing import Any

# ─── Configuration ──────────────────────────────────────────────────────────

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
EVIDENCE_DIR = os.environ.get(
    "EVIDENCE_DIR", ".sisyphus/evidence/task-5-backend-tests"
)

# ─── Test counters ──────────────────────────────────────────────────────────

_passed = 0
_failed = 0

RED = "\033[0;31m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[0;36m"
NC = "\033[0m"


def log_info(msg: str) -> None:
    print(f"{YELLOW}[INFO]{NC} {msg}")


def log_pass(msg: str) -> None:
    global _passed
    _passed += 1
    print(f"{GREEN}[PASS]{NC} {msg}")


def log_fail(msg: str) -> None:
    global _failed
    _failed += 1
    print(f"{RED}[FAIL]{NC} {msg}")


def log_step(msg: str) -> None:
    print(f"{CYAN}[STEP]{NC} {msg}")


def log_section(title: str) -> None:
    print()
    print(f"{CYAN}━━━ {title} ━━━{NC}")


# ─── HTTP helpers (stdlib only) ─────────────────────────────────────────────


def http_request(
    path: str,
    *,
    method: str = "GET",
    token: str | None = None,
    json_body: Any = None,
    raw_body: bytes | None = None,
    content_type: str | None = None,
    timeout: int = 30,
) -> tuple[int, str, dict[str, str]]:
    """Make HTTP request using urllib. Returns (status, body_text, headers)."""
    url = BASE_URL + path if path.startswith("/") else path
    headers: dict[str, str] = {}

    if token:
        headers["Authorization"] = f"Bearer {token}"

    body_data: bytes | None = None
    if json_body is not None:
        body_data = json.dumps(json_body).encode()
        headers["Content-Type"] = "application/json"
    elif raw_body is not None:
        body_data = raw_body
        if content_type:
            headers["Content-Type"] = content_type

    req = urllib.request.Request(url, data=body_data, headers=headers, method=method)

    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            status = resp.status
            body = resp.read().decode("utf-8", errors="replace")
            resp_headers = {k: v for k, v in resp.headers.items()}
    except urllib.error.HTTPError as exc:
        status = exc.code
        try:
            body = exc.read().decode("utf-8", errors="replace")
        except Exception:
            body = ""
        resp_headers = {k: v for k, v in exc.headers.items()} if exc.headers else {}
    except urllib.error.URLError as exc:
        log_fail(f"Connection error for {method} {path}: {exc.reason}")
        raise

    return status, body, resp_headers


def json_field(json_str: str, path: str) -> str:
    """Navigate dot-separated path in JSON string. Returns '' if not found."""
    try:
        value: Any = json.loads(json_str)
    except Exception:
        return ""

    for part in path.split("."):
        if isinstance(value, dict) and part in value:
            value = value[part]
        elif isinstance(value, list):
            try:
                value = value[int(part)]
            except (ValueError, IndexError):
                return ""
        else:
            return ""

    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return ""
    return str(value)


# ─── Evidence helpers ───────────────────────────────────────────────────────


def save_evidence(name: str, data: str) -> None:
    os.makedirs(EVIDENCE_DIR, exist_ok=True)
    path = os.path.join(EVIDENCE_DIR, name)
    with open(path, "w") as f:
        f.write(data)
    log_info(f"Evidence saved: {name}")


# ─── Fixture helpers ────────────────────────────────────────────────────────


def _unique_name(prefix: str = "del_reg") -> str:
    return f"{prefix}_{os.getpid()}_{int(time.time() * 1000)}"


def _temp_file(size: int = 256) -> tuple[str, str]:
    """Create temp file. Returns (path, md5_hex)."""
    fd, path = tempfile.mkstemp(suffix=".bin", prefix="del_reg_")
    with os.fdopen(fd, "wb") as f:
        data = os.urandom(size)
        f.write(data)
    h = hashlib.md5(data).hexdigest()
    return path, h


def register_test_user() -> tuple[str, str, str]:
    """Register a fresh user. Returns (username, password, token)."""
    tag = _unique_name("tdel")
    username = f"task5_{tag}"
    password = "Delreg123"
    email = f"{tag}@test.internal"

    status, body, _ = http_request(
        "/api/auth/register",
        method="POST",
        json_body={
            "username": username,
            "password": password,
            "email": email,
        },
    )
    save_evidence("register-user.json", body)

    if status != 200 and status != 201:
        code = json_field(body, "code")
        if code == "0":
            pass
        else:
            log_fail(f"Register failed (HTTP {status}): {body}")
            raise SystemExit(1)

    log_info(f"Registered user: {username}")

    login_status, login_body, _ = http_request(
        "/api/auth/login",
        method="POST",
        json_body={"account": username, "password": password},
    )
    token = json_field(login_body, "data.access_token")
    if not token or token == "null":
        log_fail(f"Login failed for fresh user: {login_body}")
        raise SystemExit(1)
    log_pass(f"Fresh user logged in: {username}")
    return username, password, token


def create_folder(token: str) -> int:
    """Create a folder. Returns folder ID from the folders table."""
    name = _unique_name("folder_target")
    status, body, _ = http_request(
        "/api/folder/create",
        method="POST",
        token=token,
        json_body={"name": name, "parent_id": 0},
    )
    save_evidence("create-folder.json", body)
    fid = json_field(body, "data.id")
    if not fid or fid == "null":
        log_fail(f"Failed to create folder: {body}")
        raise SystemExit(1)
    log_info(f"Created folder: id={fid}, name={name}")
    return int(fid)


def upload_file(token: str) -> str:
    """Upload a small file via chunked upload. Returns file_id."""
    path, file_hash = _temp_file(256)
    filename = _unique_name() + ".bin"

    try:
        status, body, _ = http_request(
            "/api/file/upload/init",
            method="POST",
            token=token,
            json_body={
                "filename": filename,
                "file_size": 256,
                "file_hash": file_hash,
                "parent_id": 0,
            },
        )
        save_evidence("upload-init.json", body)

        instant = json_field(body, "data.instant_upload")
        if instant == "true":
            fid = json_field(body, "data.file_id")
            if fid and fid != "null":
                log_info(f"Instant upload (dedup) — file_id={fid}")
                return fid
            log_fail("Instant upload but no file_id")
            print(body)
            raise SystemExit(1)

        upload_id = json_field(body, "data.upload_id")
        if not upload_id or upload_id == "null":
            log_fail("Init upload failed — no upload_id")
            print(body)
            raise SystemExit(1)

        with open(path, "rb") as f:
            content = f.read()

        status, body, _ = http_request(
            f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
            method="POST",
            token=token,
            raw_body=content,
            content_type="application/octet-stream",
        )
        save_evidence("upload-chunk.json", body)

        uploaded = json_field(body, "data.uploaded")
        if uploaded != "true":
            log_fail("Upload chunk failed")
            print(body)
            raise SystemExit(1)

        status, body, _ = http_request(
            "/api/file/upload/complete",
            method="POST",
            token=token,
            json_body={"upload_id": upload_id},
        )
        save_evidence("upload-complete.json", body)

        fid = json_field(body, "data.file.id")
        if not fid or fid == "null":
            log_fail("Complete upload — no file.id")
            print(body)
            raise SystemExit(1)

        log_info(f"File uploaded — file_id={fid}")
        return fid
    finally:
        if os.path.exists(path):
            os.unlink(path)


# ─── Tests ──────────────────────────────────────────────────────────────────


def test_folder_style_delete(token: str, folder_id: int) -> None:
    """Scenario: folder ID sent to DELETE /api/file returns structured error.

    The folders table has independent auto-increment from the files table.
    With a fresh user who has zero files, the folder ID cannot collide with
    any file row owned by this user, so DELETE /api/file must reject it.
    """
    log_section("Test 1: Folder-Style Delete → Structured Error")

    log_step(f"DELETE /api/file with folder ID {folder_id} (fresh user, no files)")
    status, body, headers = http_request(
        "/api/file",
        method="DELETE",
        token=token,
        json_body={"file_ids": [folder_id]},
    )
    save_evidence("invalid-delete-folder.json", body)
    # Also save as the canonical invalid-delete evidence
    save_evidence("invalid-delete.json", body)

    ok = True

    # Core regression: structured JSON response, no connection drop
    ct = headers.get("Content-Type", headers.get("content-type", ""))
    if "json" in ct.lower():
        log_pass("folder-delete: Content-Type is JSON (no crash)")
    else:
        log_fail(f"folder-delete: expected JSON Content-Type, got '{ct}'")
        ok = False

    try:
        parsed = json.loads(body)
        log_pass("folder-delete: body is valid JSON")
    except json.JSONDecodeError:
        log_fail("folder-delete: body is NOT valid JSON")
        ok = False
        parsed = {}

    # Must be an error — folder ID is NOT in the files table for this user
    code = parsed.get("code")
    if code is not None and code != 0:
        log_pass(f"folder-delete: code={code} (error as expected)")
    else:
        log_fail(f"folder-delete: expected error code, got code={code}")
        ok = False

    if "message" in parsed:
        log_pass(f"folder-delete: message='{parsed['message']}'")
    else:
        log_fail("folder-delete: missing 'message' field")
        ok = False

    if ok:
        log_pass("folder-delete: folder-style delete returns structured error")


def test_valid_file_delete(token: str) -> str:
    """Scenario: valid file soft-delete → file moves to trash."""
    log_section("Test 2: Valid File Soft Delete")

    file_id = upload_file(token)
    log_step(f"Uploaded file_id={file_id}")

    log_step(f"DELETE /api/file with file_ids=[{file_id}]")
    status, body, headers = http_request(
        "/api/file",
        method="DELETE",
        token=token,
        json_body={"file_ids": [int(file_id)]},
    )
    save_evidence("valid-delete.json", body)

    ok = True

    if status == 200:
        log_pass("valid-delete: HTTP 200")
    else:
        log_fail(f"valid-delete: expected HTTP 200, got HTTP {status}")
        ok = False

    code = json_field(body, "code")
    if code == "0":
        log_pass("valid-delete: code=0 (success)")
    else:
        log_fail(f"valid-delete: expected code=0, got code={code}")
        ok = False

    deleted_count_str = json_field(body, "data.deleted_count")
    try:
        deleted_count = int(deleted_count_str) if deleted_count_str else 0
    except ValueError:
        deleted_count = 0
    if deleted_count > 0:
        log_pass(f"valid-delete: deleted_count={deleted_count} > 0")
    else:
        log_fail(f"valid-delete: expected deleted_count > 0, got {deleted_count_str}")
        ok = False

    ct = headers.get("Content-Type", headers.get("content-type", ""))
    if "json" in ct.lower():
        log_pass("valid-delete: Content-Type is JSON")
    else:
        log_fail(f"valid-delete: expected JSON Content-Type, got '{ct}'")
        ok = False

    log_step("Verify deleted file appears in trash")
    trash_status, trash_body, _ = http_request(
        "/api/trash",
        token=token,
    )
    save_evidence("valid-delete-trash-check.json", trash_body)

    found_in_trash = False
    try:
        data = json.loads(trash_body)
        for item in data.get("data", {}).get("items", []):
            if item.get("original_id") == int(file_id):
                found_in_trash = True
                break
    except Exception:
        pass

    if found_in_trash:
        log_pass(f"valid-delete: file {file_id} found in trash")
    else:
        log_fail(f"valid-delete: file {file_id} NOT found in trash")
        ok = False

    if ok:
        log_pass("valid-delete: all checks passed")

    return file_id


def test_already_deleted_and_nonexistent(token: str, file_id: str) -> None:
    """Scenario: already-deleted and non-existent IDs return structured error."""
    log_section("Test 3: Already-Deleted and Non-Existent IDs")

    ok = True

    # Already-deleted file
    log_step(f"DELETE /api/file with already-deleted file_id={file_id}")
    status, body, headers = http_request(
        "/api/file",
        method="DELETE",
        token=token,
        json_body={"file_ids": [int(file_id)]},
    )
    save_evidence("invalid-delete-redeleted.json", body)

    ct = headers.get("Content-Type", headers.get("content-type", ""))
    if "json" in ct.lower():
        log_pass("redelete: Content-Type is JSON (no crash)")
    else:
        log_fail(f"redelete: expected JSON Content-Type, got '{ct}'")
        ok = False

    code = json_field(body, "code")
    if code and code != "0":
        log_pass(f"redelete: code={code} (error as expected)")
    else:
        log_fail(f"redelete: expected error code, got code={code}")
        ok = False

    # Non-existent ID
    log_step("DELETE /api/file with non-existent file_ids=[99999999]")
    status3, body3, headers3 = http_request(
        "/api/file",
        method="DELETE",
        token=token,
        json_body={"file_ids": [99999999]},
    )
    save_evidence("invalid-delete-nonexistent.json", body3)

    ct3 = headers3.get("Content-Type", headers3.get("content-type", ""))
    if "json" in ct3.lower():
        log_pass("nonexistent: Content-Type is JSON (no crash)")
    else:
        log_fail(f"nonexistent: expected JSON Content-Type, got '{ct3}'")
        ok = False

    code3 = json_field(body3, "code")
    if code3 and code3 != "0":
        log_pass(f"nonexistent: code={code3} (error as expected)")
    else:
        log_fail(f"nonexistent: expected error code, got code={code3}")
        ok = False

    if ok:
        log_pass("invalid-ids: all error-contract checks passed")


def test_server_survives() -> None:
    """Verify server is still alive after all delete operations."""
    log_section("Test 4: Server Survives")

    status, body, _ = http_request(
        "/api/auth/login",
        method="GET",
    )
    if status == 405:
        log_pass("server-survives: server responds normally (HTTP 405 on GET /login)")
    elif status in (400, 401):
        log_pass(f"server-survives: server responds (HTTP {status})")
    else:
        log_fail(f"server-survives: unexpected status {status}")


# ─── Main ───────────────────────────────────────────────────────────────────


def main() -> None:
    print("=" * 50)
    print("File Delete Regression Tests (Task 5)")
    print("=" * 50)
    print()

    log_info(f"Checking server at {BASE_URL}...")
    try:
        status, _, _ = http_request("/api/auth/login", method="GET")
        if status in (400, 401, 405):
            log_pass("Server is running")
        else:
            log_fail(f"Unexpected server status: {status}")
            sys.exit(1)
    except Exception as exc:
        log_fail(f"Server not reachable: {exc}")
        sys.exit(1)

    # Fresh user to avoid ID collisions between folders and files tables
    username, password, token = register_test_user()

    # Create a folder BEFORE any files — its ID lives in the folders table,
    # not files. With a fresh user, no files exist yet, so the folder ID
    # cannot match any file row for this user.
    folder_id = create_folder(token)

    # Run tests in order
    test_folder_style_delete(token, folder_id)
    file_id = test_valid_file_delete(token)
    test_already_deleted_and_nonexistent(token, file_id)
    test_server_survives()

    print()
    print("=" * 50)
    print("Test Summary")
    print("=" * 50)
    print(f"Passed: {GREEN}{_passed}{NC}")
    print(f"Failed: {RED}{_failed}{NC}")

    result_text = (
        f"File Delete Regression Tests (Task 5)\n"
        f"Date: {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime())}\n"
        f"User: {username}\n"
        f"Passed: {_passed}\n"
        f"Failed: {_failed}\n"
        f"Result: {'PASS' if _failed == 0 else 'FAIL'}\n"
    )
    save_evidence("test-results.txt", result_text)

    if _failed == 0:
        print(f"{GREEN}All tests passed!{NC}")
        sys.exit(0)

    print(f"{RED}Some tests failed.{NC}")
    sys.exit(1)


if __name__ == "__main__":
    main()

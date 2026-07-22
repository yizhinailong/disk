#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for folder lifecycle: create, tree, breadcrumb, edge cases.

Covers:
  1. Create parent folder with unique name
  2. Create child folder under parent
  3. Get folder tree — verify hierarchy contains both
  4. Get breadcrumb — verify path order (root → parent → child)
  5. Non-existent folder ID — expect rejection (404)
  6. Invalid folder name — expect rejection
  7. Folder create rate limit — expect correlated 429 without mutation

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured
  - Redis configured

Usage:
  uv run test/integration/test_folder_lifecycle.py
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
SERVER_LOG_PATH = EVIDENCE_ROOT / "folder-lifecycle-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

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
    json_field,
    fetch,
    header_value,
    redis_delete_pattern,
    redis_set_value,
    unique_name,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")

TOKEN = ""
PARENT_FOLDER_ID = ""
CHILD_FOLDER_ID = ""
PARENT_FOLDER_NAME = f"TstParent_{unique_name()}"
CHILD_FOLDER_NAME = f"TstChild_{unique_name()}"
RENAMED_PARENT_FOLDER_NAME = f"TstParentRenamed_{unique_name()}"
MOVE_TARGET_FOLDER_ID = ""
MOVE_TARGET_FOLDER_NAME = f"TstMoveTarget_{unique_name()}"


# ─── Helpers ─────────────────────────────────────────────────────────────────


def configured_folder_rate_value(key: str, fallback: int) -> int:
    """Read one positive folder rate-limit value from the active config."""
    config_path = Path(os.environ.get("DISK_CONFIG_FILE", REPO_ROOT / "config.json"))
    if not config_path.is_absolute():
        config_path = REPO_ROOT / config_path
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
        value = int(config.get("custom_config", {}).get("disk", {}).get(key, fallback))
        return value if value > 0 else fallback
    except Exception:
        return fallback


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


def wait_for_folder_rate_log(request_id: str, instance_id: str):
    """Return the correlated folder mutation warning from managed stdout."""
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
                    and record.get("level") == "warning"
                    and record.get("request_id") == request_id
                    and record.get("instance_id") == instance_id
                    and record.get("operation") == "folder_mutation"
                    and record.get("upload_id") is None
                    and record.get("job_id") is None
                    and record.get("lease_owner") is None
                    and record.get("state_version") is None
                    and "Folder rate limit:" in str(record.get("message", ""))
                ):
                    return record
        time.sleep(0.05)
    return None


def folder_tree_contains_name(body: str, expected_name: str) -> bool:
    """Search a folder tree response recursively for one exact name."""
    data = json.loads(body)
    pending = list(data.get("data", {}).get("children", []))
    while pending:
        node = pending.pop()
        if node.get("name") == expected_name:
            return True
        pending.extend(node.get("children", []))
    return False


def create_folder(token: str, name: str, parent_id: int) -> tuple[int, str]:
    """POST /api/folder/create. Returns (status_code, body)."""
    resp = fetch(
        "/api/folder/create",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"name": name, "parent_id": parent_id},
    )
    return resp.status_code, resp.text


def get_tree(token: str, parent_id: int = 0) -> tuple[int, str]:
    """GET /api/folder/tree. Returns (status_code, body)."""
    resp = fetch(
        f"/api/folder/tree?parent_id={parent_id}",
        headers={"Authorization": f"Bearer {token}"},
    )
    return resp.status_code, resp.text


def get_breadcrumb(token: str, folder_id: int) -> tuple[int, str]:
    """GET /api/folder/{folder_id}/breadcrumb. Returns (status_code, body)."""
    resp = fetch(
        f"/api/folder/{folder_id}/breadcrumb",
        headers={"Authorization": f"Bearer {token}"},
    )
    return resp.status_code, resp.text


def rename_folder(token: str, folder_id: int, new_name: str) -> tuple[int, str]:
    resp = fetch(
        f"/api/folder/{folder_id}/rename",
        method="PUT",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"new_name": new_name},
    )
    return resp.status_code, resp.text


def move_folders(token: str, folder_ids: list[int], target_folder_id: int) -> tuple[int, str]:
    resp = fetch(
        "/api/file/move",
        method="PUT",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={
            "file_ids": [],
            "folder_ids": folder_ids,
            "target_folder_id": target_folder_id,
        },
    )
    return resp.status_code, resp.text


def delete_folders(token: str, folder_ids: list[int]) -> tuple[int, str]:
    resp = fetch(
        "/api/file/delete",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"file_ids": [], "folder_ids": folder_ids},
    )
    return resp.status_code, resp.text


# ─── Test 1: Create parent folder ───────────────────────────────────────────


def test_create_parent_folder() -> None:
    global TOKEN, PARENT_FOLDER_ID

    log_info("Testing parent folder creation...")

    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        log_fail("Login failed for parent folder test")
        sys.exit(1)

    http_code, body = create_folder(TOKEN, PARENT_FOLDER_NAME, 0)
    code = json_field(body, "code")

    if http_code == 200 and code == "0":
        PARENT_FOLDER_ID = json_field(body, "data.id")
        name = json_field(body, "data.name")

        if (
            PARENT_FOLDER_ID
            and PARENT_FOLDER_ID != "null"
            and name == PARENT_FOLDER_NAME
        ):
            log_pass(
                f"Parent folder created: id={PARENT_FOLDER_ID}, name={PARENT_FOLDER_NAME}"
            )
            save_evidence("create_parent_folder_response.json", body)
        else:
            log_fail("Parent folder creation failed: missing id or name mismatch")
            print(body)
            sys.exit(1)
    else:
        log_fail(f"Parent folder creation failed: HTTP {http_code}, code={code}")
        print(body)
        sys.exit(1)


# ─── Test 2: Create child folder ────────────────────────────────────────────


def test_create_child_folder() -> None:
    global CHILD_FOLDER_ID

    log_info("Testing child folder creation...")

    if not PARENT_FOLDER_ID:
        log_fail("Parent folder ID not available for child creation")
        sys.exit(1)

    http_code, body = create_folder(TOKEN, CHILD_FOLDER_NAME, int(PARENT_FOLDER_ID))
    code = json_field(body, "code")

    if http_code == 200 and code == "0":
        CHILD_FOLDER_ID = json_field(body, "data.id")
        name = json_field(body, "data.name")
        parent_id = json_field(body, "data.parent_id")

        if (
            CHILD_FOLDER_ID
            and CHILD_FOLDER_ID != "null"
            and name == CHILD_FOLDER_NAME
            and parent_id == PARENT_FOLDER_ID
        ):
            log_pass(
                f"Child folder created: id={CHILD_FOLDER_ID}, "
                f"name={CHILD_FOLDER_NAME}, parent_id={PARENT_FOLDER_ID}"
            )
            save_evidence("create_child_folder_response.json", body)
        else:
            log_fail(
                "Child folder creation failed: missing id, name mismatch, or parent_id mismatch"
            )
            print(body)
            sys.exit(1)
    else:
        log_fail(f"Child folder creation failed: HTTP {http_code}, code={code}")
        print(body)
        sys.exit(1)


# ─── Test 3: Tree contains both folders ──────────────────────────────────────


def test_tree_contains_both_folders() -> None:
    log_info("Testing folder tree contains both created folders...")

    http_code, body = get_tree(TOKEN, 0)
    code = json_field(body, "code")

    if http_code != 200 or code != "0":
        log_fail(f"Tree request failed: HTTP {http_code}, code={code}")
        print(body)
        sys.exit(1)

    # Verify tree contains both folder names (nested structure)
    try:
        data = json.loads(body)
        children = data.get("data", {}).get("children", [])
        names: list[str] = []

        def collect_names(nodes: list) -> None:
            for n in nodes:
                names.append(n.get("name", ""))
                collect_names(n.get("children", []))

        collect_names(children)

        if PARENT_FOLDER_NAME not in names:
            log_fail("Tree does not contain parent folder name")
            print(body)
            sys.exit(1)
        if CHILD_FOLDER_NAME not in names:
            log_fail("Tree does not contain child folder name")
            print(body)
            sys.exit(1)
    except Exception as e:
        log_fail(f"Tree parsing failed: {e}")
        print(body)
        sys.exit(1)

    log_pass("Tree contains both parent and child folders")
    save_evidence("folder_tree_response.json", body)


# ─── Test 4: Breadcrumb order ───────────────────────────────────────────────


def test_breadcrumb_order() -> None:
    log_info("Testing breadcrumb navigation order...")

    if not CHILD_FOLDER_ID:
        log_fail("Child folder ID not available for breadcrumb test")
        sys.exit(1)

    http_code, body = get_breadcrumb(TOKEN, int(CHILD_FOLDER_ID))
    code = json_field(body, "code")

    if http_code != 200 or code != "0":
        log_fail(f"Breadcrumb request failed: HTTP {http_code}, code={code}")
        print(body)
        sys.exit(1)

    # Verify path is an array and has entries
    try:
        data = json.loads(body)
        path = data.get("data", {}).get("path", [])

        if not path:
            log_fail("Breadcrumb path is missing or empty")
            print(body)
            sys.exit(1)

        # Verify order: last entry should be child folder
        if path[-1].get("name") != CHILD_FOLDER_NAME:
            log_fail("Breadcrumb path last entry is not child folder")
            print(body)
            sys.exit(1)

        # Verify parent appears in path before child
        parent_found = any(item.get("name") == PARENT_FOLDER_NAME for item in path)
        if not parent_found:
            log_fail("Parent folder not found in breadcrumb path")
            print(body)
            sys.exit(1)
    except Exception as e:
        log_fail(f"Breadcrumb parsing failed: {e}")
        print(body)
        sys.exit(1)

    log_pass("Breadcrumb navigation order is correct")
    save_evidence("breadcrumb_response.json", body)


# ─── Test 5: Non-existent folder breadcrumb ──────────────────────────────────


def test_nonexistent_folder_breadcrumb() -> None:
    log_info("Testing breadcrumb for nonexistent folder is rejected...")

    nonexistent_id = 99999999
    http_code, body = get_breadcrumb(TOKEN, nonexistent_id)
    code = json_field(body, "code")

    # Should return either non-200 HTTP or non-zero code (404 + 50006)
    if http_code != 200 or code != "0":
        log_pass(
            f"Nonexistent folder breadcrumb correctly rejected: HTTP {http_code}, code={code}"
        )
        save_evidence("nonexistent_folder_breadcrumb_response.json", body)
    else:
        log_fail("Nonexistent folder breadcrumb should be rejected but succeeded")
        print(body)
        sys.exit(1)


# ─── Test 6: Invalid folder name ────────────────────────────────────────────


def test_invalid_folder_name() -> None:
    log_info("Testing invalid folder name is rejected...")

    invalid_name = "invalid/name"
    http_code, body = create_folder(TOKEN, invalid_name, 0)
    code = json_field(body, "code")

    # Should return either non-200 HTTP or non-zero code (400 + 50001)
    if http_code != 200 or code != "0":
        log_pass(
            f"Invalid folder name correctly rejected: HTTP {http_code}, code={code}"
        )
        save_evidence("invalid_folder_name_response.json", body)
    else:
        log_fail("Invalid folder name should be rejected but succeeded")
        print(body)
        sys.exit(1)


# ─── Test 7: Folder rate-limit rejection ────────────────────────────────────


def test_folder_rate_limit_correlation() -> None:
    log_info("Testing folder rate-limit rejection correlation...")

    try:
        user_id = access_token_subject(TOKEN)
    except (ValueError, json.JSONDecodeError) as error:
        log_fail(f"Folder rate-limit token subject is unavailable: {error}")
        sys.exit(1)

    limit = configured_folder_rate_value("folder_rate_limit_per_minute", 100)
    window_seconds = configured_folder_rate_value(
        "folder_rate_limit_window_seconds",
        60,
    )
    now = time.time()
    window_start = (int(now) // window_seconds) * window_seconds
    seconds_until_reset = window_start + window_seconds - now
    if seconds_until_reset < 2:
        time.sleep(seconds_until_reset + 0.05)
        now = time.time()
        window_start = (int(now) // window_seconds) * window_seconds

    rate_key = f"rate:folder:{user_id}:{window_start}"
    request_id = f"folder-rate-limit-{uuid.uuid4()}"
    probe_name = f"FolderRateProbe_{uuid.uuid4().hex}"
    ok = True
    try:
        redis_set_value(
            rate_key,
            str(limit),
            max(1, window_start + window_seconds - int(time.time())),
        )
        response = fetch(
            "/api/folder/create",
            method="POST",
            headers={
                "Authorization": f"Bearer {TOKEN}",
                "Content-Type": "application/json",
                "X-Request-Id": request_id,
            },
            json_body={"name": probe_name, "parent_id": 0},
        )
        save_evidence("folder_rate_limit_response.json", response.text)

        if response.status_code == 429 and json_field(response.text, "code") == "10005":
            log_pass("Folder rate-limit returned HTTP 429 and code 10005")
        else:
            log_fail(
                "Folder rate-limit response drifted: "
                f"HTTP {response.status_code}, code={json_field(response.text, 'code')}"
            )
            ok = False

        expected_headers = {
            "X-RateLimit-Limit": str(limit),
            "X-RateLimit-Remaining": "0",
            "X-Request-Id": request_id,
        }
        for header_name, expected_value in expected_headers.items():
            actual_value = header_value(response.headers, header_name)
            if actual_value == expected_value:
                log_pass(f"Folder rate-limit {header_name} is exact")
            else:
                log_fail(
                    f"Folder rate-limit expected {header_name}={expected_value!r}, "
                    f"got {actual_value!r}"
                )
                ok = False
        for header_name in ("X-RateLimit-Reset", "Retry-After"):
            if header_value(response.headers, header_name):
                log_pass(f"Folder rate-limit {header_name} is present")
            else:
                log_fail(f"Folder rate-limit {header_name} is missing")
                ok = False

        instance_id = header_value(response.headers, "X-Disk-Instance-Id")
        if instance_id and wait_for_folder_rate_log(request_id, instance_id) is not None:
            log_pass("Folder rate-limit warning preserves bounded correlation")
        else:
            log_fail("Folder rate-limit warning did not preserve bounded correlation")
            ok = False

        log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
        if any(value and value in log_text for value in (TEST_PASS, TOKEN, probe_name)):
            log_fail("Folder rate-limit log contains credentials or request body data")
            ok = False
        else:
            log_pass("Folder rate-limit log excludes credentials and request body data")
    finally:
        redis_delete_pattern(f"rate:folder:{user_id}:*")

    tree_status, tree_body = get_tree(TOKEN, 0)
    if tree_status == 200 and not folder_tree_contains_name(tree_body, probe_name):
        log_pass("Folder rate-limit rejection created no folder")
    else:
        log_fail("Folder rate-limit rejection changed the folder tree")
        ok = False

    if ok:
        log_pass("Folder rate-limit correlation and side-effect contract preserved")
    else:
        sys.exit(1)


# ─── Test 8: Rename folder updates tree and breadcrumb ───────────────────────


def test_rename_parent_folder() -> None:
    global PARENT_FOLDER_NAME

    log_info("Testing parent folder rename...")

    http_code, body = rename_folder(TOKEN, int(PARENT_FOLDER_ID), RENAMED_PARENT_FOLDER_NAME)
    code = json_field(body, "code")

    if http_code != 200 or code != "0":
        log_fail(f"Folder rename failed: HTTP {http_code}, code={code}")
        print(body)
        sys.exit(1)

    renamed_path = json_field(body, "data.path")
    expected_path = f"/{RENAMED_PARENT_FOLDER_NAME}/"
    if renamed_path != expected_path:
        log_fail(f"Folder rename path mismatch: expected '{expected_path}', got '{renamed_path}'")
        print(body)
        sys.exit(1)

    PARENT_FOLDER_NAME = RENAMED_PARENT_FOLDER_NAME
    log_pass(f"Parent folder renamed to {PARENT_FOLDER_NAME}")
    save_evidence("rename_parent_folder_response.json", body)


def test_renamed_folder_breadcrumb() -> None:
    log_info("Testing child breadcrumb after parent rename...")

    http_code, body = get_breadcrumb(TOKEN, int(CHILD_FOLDER_ID))
    code = json_field(body, "code")

    if http_code != 200 or code != "0":
        log_fail(f"Breadcrumb after rename failed: HTTP {http_code}, code={code}")
        print(body)
        sys.exit(1)

    data = json.loads(body)
    path = data.get("data", {}).get("path", [])
    names = [item.get("name") for item in path]
    if PARENT_FOLDER_NAME not in names or CHILD_FOLDER_NAME not in names:
        log_fail("Breadcrumb after rename does not include renamed parent and child")
        print(body)
        sys.exit(1)

    log_pass("Breadcrumb reflects renamed parent folder")
    save_evidence("renamed_folder_breadcrumb_response.json", body)


# ─── Test 9: Move folder subtree ──────────────────────────────────────────────


def test_create_move_target_folder() -> None:
    global MOVE_TARGET_FOLDER_ID

    log_info("Testing move target folder creation...")

    http_code, body = create_folder(TOKEN, MOVE_TARGET_FOLDER_NAME, 0)
    code = json_field(body, "code")

    if http_code == 200 and code == "0":
        MOVE_TARGET_FOLDER_ID = json_field(body, "data.id")
        if MOVE_TARGET_FOLDER_ID and MOVE_TARGET_FOLDER_ID != "null":
            log_pass(f"Move target created: id={MOVE_TARGET_FOLDER_ID}")
            save_evidence("create_move_target_folder_response.json", body)
            return

    log_fail(f"Move target folder creation failed: HTTP {http_code}, code={code}")
    print(body)
    sys.exit(1)


def test_reject_move_folder_into_child() -> None:
    log_info("Testing folder move into child is rejected...")

    http_code, body = move_folders(TOKEN, [int(PARENT_FOLDER_ID)], int(CHILD_FOLDER_ID))
    code = json_field(body, "code")

    if http_code != 200 or code != "0":
        log_pass(f"Move into child correctly rejected: HTTP {http_code}, code={code}")
        save_evidence("move_folder_into_child_rejected_response.json", body)
        return

    log_fail("Moving a folder into its child should be rejected but succeeded")
    print(body)
    sys.exit(1)


def test_move_parent_folder_to_target() -> None:
    log_info("Testing parent folder move to another folder...")

    http_code, body = move_folders(TOKEN, [int(PARENT_FOLDER_ID)], int(MOVE_TARGET_FOLDER_ID))
    code = json_field(body, "code")

    if http_code != 200 or code != "0":
        log_fail(f"Folder move failed: HTTP {http_code}, code={code}")
        print(body)
        sys.exit(1)

    moved_count = json_field(body, "data.moved_folder_count")
    if int(moved_count) < 1:
        log_fail(f"Folder move did not report moved folders: moved_folder_count={moved_count}")
        print(body)
        sys.exit(1)

    log_pass(f"Parent folder moved under target id={MOVE_TARGET_FOLDER_ID}")
    save_evidence("move_parent_folder_response.json", body)


def test_moved_folder_breadcrumb() -> None:
    log_info("Testing child breadcrumb after parent move...")

    http_code, body = get_breadcrumb(TOKEN, int(CHILD_FOLDER_ID))
    code = json_field(body, "code")

    if http_code != 200 or code != "0":
        log_fail(f"Breadcrumb after move failed: HTTP {http_code}, code={code}")
        print(body)
        sys.exit(1)

    data = json.loads(body)
    path = data.get("data", {}).get("path", [])
    names = [item.get("name") for item in path]
    expected_order = [MOVE_TARGET_FOLDER_NAME, PARENT_FOLDER_NAME, CHILD_FOLDER_NAME]
    positions = [names.index(name) if name in names else -1 for name in expected_order]

    if any(pos < 0 for pos in positions) or positions != sorted(positions):
        log_fail("Breadcrumb after move does not preserve target → parent → child order")
        print(body)
        sys.exit(1)

    log_pass("Breadcrumb reflects moved folder subtree")
    save_evidence("moved_folder_breadcrumb_response.json", body)


def test_delete_moved_folder_to_trash() -> None:
    log_info("Testing moved folder can be deleted via mixed delete endpoint...")

    http_code, body = delete_folders(TOKEN, [int(PARENT_FOLDER_ID)])
    code = json_field(body, "code")

    if http_code == 200 and code == "0":
        deleted_count = json_field(body, "data.deleted_count")
        if deleted_count and int(deleted_count) >= 1:
            log_pass("Moved folder deleted to trash")
            save_evidence("delete_moved_folder_response.json", body)
            return

    log_fail(f"Moved folder delete failed: HTTP {http_code}, code={code}")
    print(body)
    sys.exit(1)


# ─── Main ───────────────────────────────────────────────────────────────────


def main() -> None:
    print("==========================================")
    print("Folder Lifecycle Integration Tests")
    print("==========================================")
    print()

    SERVER_LOG_PATH.unlink(missing_ok=True)
    ensure_server()

    log_info(f"Parent: {PARENT_FOLDER_NAME}")
    log_info(f"Child: {CHILD_FOLDER_NAME}")
    print()

    test_create_parent_folder()
    test_create_child_folder()
    test_tree_contains_both_folders()
    test_breadcrumb_order()
    test_nonexistent_folder_breadcrumb()
    test_invalid_folder_name()
    test_folder_rate_limit_correlation()
    test_rename_parent_folder()
    test_renamed_folder_breadcrumb()
    test_create_move_target_folder()
    test_reject_move_folder_into_child()
    test_move_parent_folder_to_target()
    test_moved_folder_breadcrumb()
    test_delete_moved_folder_to_trash()

    log_info(
        f"Created folders — Parent: {PARENT_FOLDER_NAME} (ID: {PARENT_FOLDER_ID}), "
        f"Child: {CHILD_FOLDER_NAME} (ID: {CHILD_FOLDER_ID})"
    )

    print()
    print_summary()


if __name__ == "__main__":
    main()

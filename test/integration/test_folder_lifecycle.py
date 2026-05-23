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

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured
  - Redis configured

Usage:
  uv run test/integration/test_folder_lifecycle.py
"""

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    print_summary,
    save_evidence,
    check_server,
    cleanup,
    do_login,
    json_field,
    fetch,
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


# ─── Test 7: Rename folder updates tree and breadcrumb ───────────────────────


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
    expected_path = f"/{RENAMED_PARENT_FOLDER_NAME}"
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


# ─── Test 8: Move folder subtree ──────────────────────────────────────────────


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

    check_server() or sys.exit(1)

    log_info(f"Parent: {PARENT_FOLDER_NAME}")
    log_info(f"Child: {CHILD_FOLDER_NAME}")
    print()

    test_create_parent_folder()
    test_create_child_folder()
    test_tree_contains_both_folders()
    test_breadcrumb_order()
    test_nonexistent_folder_breadcrumb()
    test_invalid_folder_name()
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

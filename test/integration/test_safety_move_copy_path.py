#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""
Safety-net integration tests for file/folder namespace and path invariants.
"""

from __future__ import annotations

import atexit
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    assert_equal,
    check_server,
    cleanup,
    do_login,
    fetch,
    json_field,
    log_fail,
    log_info,
    log_section,
    md5_bytes,
    print_summary,
    query_one,
    scalar,
    unique_name,
)

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
EVIDENCE_PREFIX = "safety-namespace"

TOKEN = ""
USER_ID = 0


def auth_headers(content_type: str = "application/json") -> dict[str, str]:
    """Return authorization headers for a test request."""
    return {"Authorization": f"Bearer {TOKEN}", "Content-Type": content_type}


def current_user_id() -> int:
    """Return the authenticated test user's database id."""
    value = scalar("SELECT id FROM users WHERE username = %s OR email = %s LIMIT 1", (TEST_USER, TEST_USER))
    if value is None:
        log_fail(f"Could not resolve user id for {TEST_USER}")
        print_summary()
    return int(value)


def create_folder(name: str, parent_id: int = 0) -> int:
    """Create a folder and return its id."""
    resp = fetch(
        "/api/folder/create",
        method="POST",
        headers=auth_headers(),
        json_body={"name": name, "parent_id": parent_id},
    )
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"create folder failed: {name}")
        print(resp.text)
        print_summary()
    return int(json_field(resp.text, "data.id"))


def upload_file(filename: str, payload: bytes, parent_id: int = 0) -> int:
    """Upload a file and return its id."""
    file_hash = md5_bytes(payload)
    init_resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers(),
        json_body={"filename": filename, "file_size": len(payload), "file_hash": file_hash, "parent_id": parent_id},
    )
    if init_resp.status_code != 200 or json_field(init_resp.text, "code") != "0":
        log_fail(f"init upload failed: {filename}")
        print(init_resp.text)
        print_summary()
    instant_id = json_field(init_resp.text, "data.file.id")
    if instant_id:
        return int(instant_id)

    upload_id = json_field(init_resp.text, "data.upload_id")
    chunk_resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
        method="POST",
        headers=auth_headers("application/octet-stream"),
        data=payload,
    )
    if chunk_resp.status_code != 200 or json_field(chunk_resp.text, "data.uploaded") != "true":
        log_fail(f"chunk upload failed: {filename}")
        print(chunk_resp.text)
        print_summary()

    complete_resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=auth_headers(),
        json_body={"upload_id": upload_id},
    )
    file_id = json_field(complete_resp.text, "data.file.id")
    if complete_resp.status_code != 200 or json_field(complete_resp.text, "code") != "0" or not file_id:
        log_fail(f"complete upload failed: {filename}")
        print(complete_resp.text)
        print_summary()
    return int(file_id)


def move_items(file_ids: list[int], folder_ids: list[int], target_folder_id: int) -> str:
    """Move files/folders and return response body."""
    resp = fetch(
        "/api/file/move",
        method="PUT",
        headers=auth_headers(),
        json_body={"file_ids": file_ids, "folder_ids": folder_ids, "target_folder_id": target_folder_id},
    )
    return resp.text


def copy_items(file_ids: list[int], folder_ids: list[int], target_folder_id: int) -> str:
    """Copy files/folders and return response body."""
    resp = fetch(
        "/api/file/copy",
        method="POST",
        headers=auth_headers(),
        json_body={"file_ids": file_ids, "folder_ids": folder_ids, "target_folder_id": target_folder_id},
    )
    return resp.text


def folder_row(folder_id: int) -> dict[str, object]:
    """Return a folder row or fail."""
    row = query_one("SELECT * FROM folders WHERE id = %s AND user_id = %s", (folder_id, USER_ID))
    if row is None:
        log_fail(f"folder row exists: id={folder_id}")
        print_summary()
    return row


def file_row(file_id: int) -> dict[str, object]:
    """Return a file row or fail."""
    row = query_one("SELECT * FROM files WHERE id = %s AND user_id = %s", (file_id, USER_ID))
    if row is None:
        log_fail(f"file row exists: id={file_id}")
        print_summary()
    return row


def test_move_file_updates_parent_path_and_counts() -> None:
    """Verify moving a file updates folder_id/path and item counts."""
    log_section("Move File Parent/Path/Counts")
    source_folder = create_folder(f"safety_move_src_{unique_name()}")
    target_folder = create_folder(f"safety_move_dst_{unique_name()}")
    target_name = str(folder_row(target_folder)["name"])
    file_name = f"safety_move_file_{unique_name()}.bin"
    file_id = upload_file(file_name, f"move-file-{unique_name()}".encode(), source_folder)
    source_before = int(folder_row(source_folder)["item_count"])
    target_before = int(folder_row(target_folder)["item_count"])
    assert_equal("upload increments source folder item_count", source_before, 1)

    body = move_items([file_id], [], target_folder)
    if json_field(body, "code") != "0":
        log_fail("move file returned success")
        print(body)
        print_summary()

    moved_file = file_row(file_id)
    assert_equal("moved file folder_id updated", int(moved_file["folder_id"]), target_folder)
    assert_equal("moved file path updated", moved_file["path"], f"/{target_name}/{file_name}")
    assert_equal("source folder item_count decremented", int(folder_row(source_folder)["item_count"]), source_before - 1)
    assert_equal("target folder item_count incremented", int(folder_row(target_folder)["item_count"]), target_before + 1)


def test_move_folder_subtree_paths_and_rejections() -> tuple[int, int, int, int]:
    """Verify folder subtree move updates descendants and invalid moves are rejected."""
    log_section("Move Folder Subtree Paths")
    target_folder = create_folder(f"safety_subtree_target_{unique_name()}")
    root_folder = create_folder(f"safety_subtree_root_{unique_name()}")
    child_folder = create_folder(f"safety_subtree_child_{unique_name()}", root_folder)
    file_name = f"safety_subtree_file_{unique_name()}.bin"
    file_id = upload_file(file_name, f"subtree-file-{unique_name()}".encode(), child_folder)

    root_before = dict(folder_row(root_folder))
    child_before = dict(folder_row(child_folder))
    file_before = dict(file_row(file_id))

    reject_self = move_items([], [root_folder], root_folder)
    assert_equal("move folder into itself rejected", json_field(reject_self, "code") != "0", True)
    reject_child = move_items([], [root_folder], child_folder)
    assert_equal("move folder into child rejected", json_field(reject_child, "code") != "0", True)
    assert_equal("root path unchanged after rejected moves", folder_row(root_folder)["path"], root_before["path"])
    assert_equal("child path unchanged after rejected moves", folder_row(child_folder)["path"], child_before["path"])
    assert_equal("file path unchanged after rejected moves", file_row(file_id)["path"], file_before["path"])

    body = move_items([], [root_folder], target_folder)
    if json_field(body, "code") != "0":
        log_fail("move folder subtree returned success")
        print(body)
        print_summary()

    target_name = str(folder_row(target_folder)["name"])
    root_name = str(folder_row(root_folder)["name"])
    child_name = str(folder_row(child_folder)["name"])
    assert_equal("moved root parent updated", int(folder_row(root_folder)["parent_id"]), target_folder)
    assert_equal("moved root path updated", folder_row(root_folder)["path"], f"/{target_name}/{root_name}/")
    assert_equal("moved child path updated", folder_row(child_folder)["path"], f"/{target_name}/{root_name}/{child_name}/")
    assert_equal("descendant file path updated", file_row(file_id)["path"], f"/{target_name}/{root_name}/{child_name}/{file_name}")
    return target_folder, root_folder, child_folder, file_id


def test_copy_folder_preserves_tree_and_content_refs() -> None:
    """Verify folder copy preserves shape and increments content refs."""
    log_section("Copy Folder Tree Shape And Content References")
    destination = create_folder(f"safety_copy_tree_dest_{unique_name()}")
    root = create_folder(f"safety_copy_tree_root_{unique_name()}")
    child = create_folder(f"safety_copy_tree_child_{unique_name()}", root)
    file_name = f"safety_copy_tree_file_{unique_name()}.bin"
    file_id = upload_file(file_name, f"copy-tree-file-{unique_name()}".encode(), child)
    original_file = file_row(file_id)
    content_id = int(original_file["content_id"])
    ref_before = int(query_one("SELECT ref_count FROM file_contents WHERE id = %s", (content_id,))["ref_count"])

    body = copy_items([], [root], destination)
    if json_field(body, "code") != "0":
        log_fail("copy folder returned success")
        print(body)
        print_summary()

    copied_root = query_one(
        "SELECT * FROM folders WHERE user_id = %s AND parent_id = %s AND name = %s",
        (USER_ID, destination, folder_row(root)["name"]),
    )
    if copied_root is None:
        log_fail("copied root folder exists")
        print_summary()
    copied_child = query_one(
        "SELECT * FROM folders WHERE user_id = %s AND parent_id = %s AND name = %s",
        (USER_ID, copied_root["id"], folder_row(child)["name"]),
    )
    if copied_child is None:
        log_fail("copied child folder exists")
        print_summary()
    copied_file = query_one(
        "SELECT * FROM files WHERE user_id = %s AND folder_id = %s AND name = %s",
        (USER_ID, copied_child["id"], file_name),
    )
    if copied_file is None:
        log_fail("copied descendant file exists")
        print_summary()

    ref_after = int(query_one("SELECT ref_count FROM file_contents WHERE id = %s", (content_id,))["ref_count"])
    destination_name = str(folder_row(destination)["name"])
    assert_equal("copied root path preserves shape", copied_root["path"], f"/{destination_name}/{copied_root['name']}/")
    assert_equal("copied child path preserves shape", copied_child["path"], f"/{destination_name}/{copied_root['name']}/{copied_child['name']}/")
    assert_equal("copied file path preserves shape", copied_file["path"], f"/{destination_name}/{copied_root['name']}/{copied_child['name']}/{file_name}")
    assert_equal("copied file reuses content_id", int(copied_file["content_id"]), content_id)
    assert_equal("folder copy increments content ref_count", ref_after, ref_before + 1)


def test_instant_upload_updates_parent_count() -> None:
    """Verify instant upload creates one direct child in the target folder."""
    log_section("Instant Upload Parent Count")
    target = create_folder(f"safety_instant_target_{unique_name()}")
    payload = f"instant-parent-count-{unique_name()}".encode()
    upload_file(f"safety_instant_source_{unique_name()}.bin", payload)

    target_before = int(folder_row(target)["item_count"])
    instant_name = f"safety_instant_child_{unique_name()}.bin"
    instant_file_id = upload_file(instant_name, payload, target)

    assert_equal("instant upload starts with empty target folder", target_before, 0)
    assert_equal("instant upload increments target folder item_count", int(folder_row(target)["item_count"]), 1)
    assert_equal("instant upload stores target folder id", int(file_row(instant_file_id)["folder_id"]), target)


def main() -> None:
    """Run namespace safety-net tests."""
    print("==========================================")
    print("Namespace Safety-Net Integration Tests")
    print("==========================================")
    print()

    if not check_server():
        sys.exit(1)

    global TOKEN, USER_ID
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)
    USER_ID = current_user_id()
    log_info(f"Using user_id={USER_ID}, base_url={BASE_URL}")

    test_move_file_updates_parent_path_and_counts()
    test_move_folder_subtree_paths_and_rejections()
    test_copy_folder_preserves_tree_and_content_refs()
    test_instant_upload_updates_parent_count()

    print_summary()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""
Safety-net integration tests for content reference counts and quota behavior.
"""

from __future__ import annotations

import atexit
import json
import os
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", ".sisyphus/evidence"))
SERVER_LOG_PATH = EVIDENCE_ROOT / "safety-content-quota-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    assert_db_row_absent,
    assert_equal,
    assert_numeric_delta,
    assert_path_absent,
    assert_path_exists,
    assert_storage_job_succeeded,
    ensure_server,
    cleanup,
    do_login,
    execute,
    fetch,
    final_blob_path,
    header_value,
    json_field,
    local_blob_path,
    log_fail,
    log_info,
    log_pass,
    log_section,
    log_step,
    md5_bytes,
    print_summary,
    query_one,
    save_evidence,
    scalar,
    sha256_bytes,
    unique_name,
    upload_temp_dir,
)

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
EVIDENCE_PREFIX = "safety-content-quota"

TOKEN = ""
USER_ID = 0


def auth_headers(
    content_type: str = "application/json",
    request_id: str | None = None,
) -> dict[str, str]:
    """Return authorization headers for a test request."""
    headers = {"Authorization": f"Bearer {TOKEN}", "Content-Type": content_type}
    if request_id is not None:
        headers["X-Request-Id"] = request_id
    return headers


def wait_for_persistence_failure_log(
    *,
    response,
    request_id: str,
    expected_message: str,
    expected_operation: str = "file_mutation",
) -> dict[str, object]:
    """Return one exact shared-persistence event with request-owned correlation."""
    assert_equal(
        f"{expected_message} response preserves request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal(f"{expected_message} response identifies instance", bool(instance_id), True)

    deadline = time.monotonic() + 5
    records: list[dict[str, object]] = []
    while time.monotonic() < deadline:
        records.clear()
        if SERVER_LOG_PATH.is_file():
            for line in SERVER_LOG_PATH.read_text(
                encoding="utf-8",
                errors="replace",
            ).splitlines():
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(record, dict):
                    records.append(record)

        matches = [
            record
            for record in records
            if record.get("schema_version") == 1
            and record.get("source") == "application"
            and record.get("request_id") == request_id
            and record.get("instance_id") == instance_id
            and record.get("operation") == expected_operation
            and record.get("upload_id") is None
            and record.get("job_id") is None
            and record.get("lease_owner") is None
            and record.get("state_version") is None
            and record.get("message") == expected_message
        ]
        if matches:
            unscoped_duplicates = [
                record
                for record in records
                if record.get("source") == "application"
                and record.get("request_id") is None
                and record.get("message") == expected_message
            ]
            assert_equal(
                f"{expected_message} emits no unscoped duplicate",
                len(unscoped_duplicates),
                0,
            )
            return matches[-1]
        time.sleep(0.05)

    log_fail(f"{expected_message} keeps structured request correlation")
    print_summary()
    raise AssertionError("unreachable")


def assert_application_log_excludes(value: str, assertion: str) -> None:
    """Assert no application-owned structured message contains a detail."""
    messages: list[str] = []
    if SERVER_LOG_PATH.is_file():
        for line in SERVER_LOG_PATH.read_text(
            encoding="utf-8",
            errors="replace",
        ).splitlines():
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(record, dict) and record.get("source") == "application":
                messages.append(str(record.get("message", "")))
    assert_equal(assertion, any(value in message for message in messages), False)


def current_user_id() -> int:
    """Return the authenticated test user's database id."""
    value = scalar("SELECT id FROM users WHERE username = %s OR email = %s LIMIT 1", (TEST_USER, TEST_USER))
    if value is None:
        log_fail(f"Could not resolve user id for {TEST_USER}")
        print_summary()
    return int(value)


def user_quota() -> dict[str, int]:
    """Return current storage counters for the test user."""
    row = query_one("SELECT storage_used, storage_reserved, storage_quota FROM users WHERE id = %s", (USER_ID,))
    if row is None:
        log_fail(f"Could not load quota counters for user_id={USER_ID}")
        print_summary()
    return {key: int(row[key]) for key in ("storage_used", "storage_reserved", "storage_quota")}


def unexplained_reserved_bytes() -> int:
    """Return reserved bytes not explained by in-progress upload tasks."""
    row = query_one(
        """
        SELECT u.storage_reserved - COALESCE(SUM(t.reserved_bytes), 0) AS unexplained_reserved
        FROM users u
        LEFT JOIN upload_tasks t ON t.user_id = u.id AND t.status = 0
        WHERE u.id = %s
        GROUP BY u.id, u.storage_reserved
        """,
        (USER_ID,),
    )
    if row is None:
        log_fail(f"Could not load unexplained reservations for user_id={USER_ID}")
        print_summary()
    return int(row["unexplained_reserved"])


def init_upload(filename: str, payload: bytes, parent_id: int = 0) -> tuple[str, str, str | None]:
    """Initialize upload and return upload_id, file_hash, instant file id if present."""
    file_hash = md5_bytes(payload)
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers(),
        json_body={
            "filename": filename,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": parent_id,
        },
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{filename}-init.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"{filename}: init upload failed")
        print(resp.text)
        print_summary()
    return (
        json_field(resp.text, "data.upload_id"),
        file_hash,
        json_field(resp.text, "data.file.id") or None,
    )


def upload_chunk(upload_id: str, payload: bytes) -> None:
    """Upload one chunk for an upload task."""
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={md5_bytes(payload)}",
        method="POST",
        headers=auth_headers("application/octet-stream"),
        data=payload,
    )
    if resp.status_code != 200 or json_field(resp.text, "data.uploaded") != "true":
        log_fail(f"{upload_id}: chunk upload failed")
        print(resp.text)
        print_summary()


def complete_upload(upload_id: str) -> int:
    """Complete upload and return file id."""
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=auth_headers(),
        json_body={"upload_id": upload_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-complete.json", resp.text)
    file_id = json_field(resp.text, "data.file.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not file_id:
        log_fail(f"{upload_id}: complete upload failed")
        print(resp.text)
        print_summary()
    return int(file_id)


def cancel_upload(upload_id: str) -> None:
    """Cancel one in-progress upload task."""
    resp = fetch(
        f"/api/file/upload/{upload_id}",
        method="DELETE",
        headers=auth_headers(),
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-cancel.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"{upload_id}: cancel upload failed")
        print(resp.text)
        print_summary()


def upload_file(filename: str, payload: bytes, parent_id: int = 0) -> int:
    """Upload content as a chunked file and return file id."""
    upload_id, _, instant_file_id = init_upload(filename, payload, parent_id)
    if instant_file_id:
        return int(instant_file_id)
    upload_chunk(upload_id, payload)
    return complete_upload(upload_id)


def file_row(file_id: int) -> dict[str, object]:
    """Return a file row or fail."""
    row = query_one("SELECT * FROM files WHERE id = %s AND user_id = %s", (file_id, USER_ID))
    if row is None:
        log_fail(f"file row exists: id={file_id}")
        print_summary()
    return row


def content_row(content_id: int) -> dict[str, object]:
    """Return a file_contents row or fail."""
    row = query_one("SELECT * FROM file_contents WHERE id = %s", (content_id,))
    if row is None:
        log_fail(f"content row exists: id={content_id}")
        print_summary()
    return row


def create_folder(name: str, parent_id: int = 0) -> int:
    """Create a folder and return its id."""
    resp = fetch(
        "/api/folder/create",
        method="POST",
        headers=auth_headers(),
        json_body={"name": name, "parent_id": parent_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-folder-{name}.json", resp.text)
    folder_id = json_field(resp.text, "data.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not folder_id:
        log_fail(f"create folder failed: {name}")
        print(resp.text)
        print_summary()
    return int(folder_id)


def copy_file(file_id: int, target_folder_id: int = 0) -> list[dict[str, int]]:
    """Copy a file and return new_files mappings."""
    data = copy_items([file_id], [], target_folder_id)
    return data.get("new_files", [])


def copy_items(file_ids: list[int], folder_ids: list[int], target_folder_id: int = 0) -> dict[str, object]:
    """Copy files/folders and return response data."""
    resp = copy_items_response(file_ids, folder_ids, target_folder_id)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"copy items failed: file_ids={file_ids}, folder_ids={folder_ids}")
        print(resp.text)
        print_summary()
    return json.loads(resp.text).get("data", {})


def copy_items_response(file_ids: list[int], folder_ids: list[int], target_folder_id: int = 0):
    """Copy files/folders and return the raw HTTP response."""
    resp = fetch(
        "/api/file/copy",
        method="POST",
        headers=auth_headers(),
        json_body={"file_ids": file_ids, "folder_ids": folder_ids, "target_folder_id": target_folder_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-copy-{unique_name()}.json", resp.text)
    return resp


def delete_file_to_trash(file_id: int) -> int:
    """Soft delete a file and return trash id."""
    resp = fetch(
        "/api/file",
        method="DELETE",
        headers=auth_headers(),
        json_body={"file_ids": [file_id], "folder_ids": []},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-delete-file-{file_id}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"soft delete failed: file_id={file_id}")
        print(resp.text)
        print_summary()
    row = query_one(
        "SELECT id FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s ORDER BY id DESC LIMIT 1",
        (USER_ID, file_id),
    )
    if row is None:
        log_fail(f"trash row created for file_id={file_id}")
        print_summary()
    return int(row["id"])


def delete_folder_to_trash(folder_id: int) -> int:
    """Soft delete a folder and return trash id."""
    resp = fetch(
        "/api/file",
        method="DELETE",
        headers=auth_headers(),
        json_body={"file_ids": [], "folder_ids": [folder_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-delete-folder-{folder_id}.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"soft delete failed: folder_id={folder_id}")
        print(resp.text)
        print_summary()
    row = query_one(
        "SELECT id FROM trash WHERE user_id = %s AND item_type = 'folder' AND item_id = %s ORDER BY id DESC LIMIT 1",
        (USER_ID, folder_id),
    )
    if row is None:
        log_fail(f"trash row created for folder_id={folder_id}")
        print_summary()
    return int(row["id"])


def delete_file_to_trash_response(file_id: int, request_id: str | None = None):
    """Soft delete a file and return the raw HTTP response."""
    resp = fetch(
        "/api/file",
        method="DELETE",
        headers=auth_headers(request_id=request_id),
        json_body={"file_ids": [file_id], "folder_ids": []},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-delete-file-raw-{file_id}.json", resp.text)
    return resp


def delete_folder_to_trash_response(folder_id: int, request_id: str | None = None):
    """Soft delete a folder and return the raw HTTP response."""
    resp = fetch(
        "/api/file",
        method="DELETE",
        headers=auth_headers(request_id=request_id),
        json_body={"file_ids": [], "folder_ids": [folder_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-delete-folder-raw-{folder_id}.json", resp.text)
    return resp


def safe_test_identifier(prefix: str) -> str:
    """Return a PostgreSQL-safe identifier for temporary failure-injection objects."""
    return "".join(ch if ch.isalnum() or ch == "_" else "_" for ch in unique_name(prefix))[:60]


def drop_failure_trigger(table_name: str, trigger_name: str, function_name: str) -> None:
    """Drop a temporary failure-injection trigger and function."""
    execute(f'DROP TRIGGER IF EXISTS "{trigger_name}" ON {table_name}')
    execute(f'DROP FUNCTION IF EXISTS "{function_name}"()')


def install_trash_insert_failure_trigger(item_name: str):
    """Install a trigger that fails trash insertion for one item name."""
    function_name = safe_test_identifier("fail_trash_insert_fn")
    trigger_name = safe_test_identifier("fail_trash_insert_trg")
    escaped_item_name = item_name.replace("'", "''")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.item_name = '{escaped_item_name}' THEN
                RAISE EXCEPTION 'injected trash insert failure for %', NEW.item_name;
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE INSERT ON trash '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("trash", trigger_name, function_name)


def install_file_delete_failure_trigger(file_id: int):
    """Install a trigger that fails active file row deletion for one file."""
    function_name = safe_test_identifier("fail_file_delete_fn")
    trigger_name = safe_test_identifier("fail_file_delete_trg")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF OLD.id = {int(file_id)} THEN
                RAISE EXCEPTION 'injected file delete failure for %', OLD.id;
            END IF;
            RETURN OLD;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE DELETE ON files '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("files", trigger_name, function_name)


def install_folder_delete_failure_trigger(folder_id: int):
    """Install a trigger that fails active folder row deletion for one folder."""
    function_name = safe_test_identifier("fail_folder_delete_fn")
    trigger_name = safe_test_identifier("fail_folder_delete_trg")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF OLD.id = {int(folder_id)} THEN
                RAISE EXCEPTION 'injected folder delete failure for %', OLD.id;
            END IF;
            RETURN OLD;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE DELETE ON folders '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("folders", trigger_name, function_name)


def install_content_ref_increment_failure_trigger(content_id: int):
    """Install a trigger that fails reference increments for one content row."""
    function_name = safe_test_identifier("fail_content_ref_increment_fn")
    trigger_name = safe_test_identifier("fail_content_ref_increment_trg")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.id = {int(content_id)} AND NEW.ref_count > OLD.ref_count THEN
                RAISE EXCEPTION 'injected content ref increment failure for %', NEW.id;
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE UPDATE ON file_contents '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("file_contents", trigger_name, function_name)


def install_copy_file_insert_failure_trigger(target_folder_id: int, file_name: str):
    """Install a trigger that fails copied file insertion for one target/name."""
    function_name = safe_test_identifier("fail_copy_file_insert_fn")
    trigger_name = safe_test_identifier("fail_copy_file_insert_trg")
    escaped_file_name = file_name.replace("'", "''")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.folder_id = {int(target_folder_id)} AND NEW.name = '{escaped_file_name}' THEN
                RAISE EXCEPTION 'injected copy file insert failure for %', NEW.name;
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE INSERT ON files '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("files", trigger_name, function_name)


def install_reserved_release_failure_trigger(user_id: int):
    """Install a trigger that fails reservation releases but not reserve or commit updates."""
    function_name = safe_test_identifier("fail_reserved_release_fn")
    trigger_name = safe_test_identifier("fail_reserved_release_trg")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.id = {int(user_id)}
               AND NEW.storage_reserved < OLD.storage_reserved
               AND NEW.storage_used = OLD.storage_used THEN
                RAISE EXCEPTION 'injected reserved release failure for user %', NEW.id;
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE UPDATE ON users '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("users", trigger_name, function_name)


def install_used_storage_decrement_failure_trigger(user_id: int):
    """Install a trigger that rejects used-storage decrements for one user."""
    function_name = safe_test_identifier("fail_used_decrement_fn")
    trigger_name = safe_test_identifier("fail_used_decrement_trg")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.id = {int(user_id)}
               AND NEW.storage_used < OLD.storage_used THEN
                RAISE EXCEPTION 'injected used storage decrement failure for user %', NEW.id;
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE UPDATE ON users '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("users", trigger_name, function_name)


def install_upload_task_insert_failure_trigger(user_id: int, filename: str):
    """Install a trigger that rejects one user's named upload task insert."""
    function_name = safe_test_identifier("fail_upload_task_insert_fn")
    trigger_name = safe_test_identifier("fail_upload_task_insert_trg")
    escaped_filename = filename.replace("'", "''")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.user_id = {int(user_id)} AND NEW.filename = '{escaped_filename}' THEN
                RAISE EXCEPTION 'injected upload task insert failure for user %', NEW.user_id;
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE INSERT ON upload_tasks '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("upload_tasks", trigger_name, function_name)


def install_parent_item_count_failure_trigger(parent_id: int):
    """Install a trigger that rejects item-count changes for one parent folder."""
    function_name = safe_test_identifier("fail_parent_item_count_fn")
    trigger_name = safe_test_identifier("fail_parent_item_count_trg")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF OLD.id = {parent_id} AND NEW.item_count <> OLD.item_count THEN
                RAISE EXCEPTION 'injected parent item count failure';
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE UPDATE ON folders '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("folders", trigger_name, function_name)


def install_folder_rename_delay_trigger(user_id: int, parent_id: int, target_name: str):
    """Delay matching folder renames so concurrent prechecks overlap."""
    function_name = safe_test_identifier("delay_folder_rename_fn")
    trigger_name = safe_test_identifier("delay_folder_rename_trg")
    escaped_target = target_name.replace("'", "''")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.user_id = {int(user_id)}
               AND NEW.parent_id = {int(parent_id)}
               AND NEW.name = '{escaped_target}'
               AND OLD.name <> NEW.name THEN
                PERFORM pg_sleep(0.5);
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE UPDATE ON folders '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("folders", trigger_name, function_name)


def install_file_rename_delay_trigger(user_id: int, folder_id: int, target_name: str):
    """Delay matching file renames so concurrent prechecks overlap."""
    function_name = safe_test_identifier("delay_file_rename_fn")
    trigger_name = safe_test_identifier("delay_file_rename_trg")
    escaped_target = target_name.replace("'", "''")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.user_id = {int(user_id)}
               AND NEW.folder_id = {int(folder_id)}
               AND NEW.name = '{escaped_target}'
               AND OLD.name <> NEW.name THEN
                PERFORM pg_sleep(0.5);
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE UPDATE ON files '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("files", trigger_name, function_name)


def install_file_move_delay_trigger(user_id: int, folder_id: int, target_name: str):
    """Delay matching file moves so concurrent prechecks overlap."""
    function_name = safe_test_identifier("delay_file_move_fn")
    trigger_name = safe_test_identifier("delay_file_move_trg")
    escaped_target = target_name.replace("'", "''")
    execute(
        f"""
        CREATE OR REPLACE FUNCTION "{function_name}"() RETURNS trigger AS $$
        BEGIN
            IF NEW.user_id = {int(user_id)}
               AND NEW.folder_id = {int(folder_id)}
               AND NEW.name = '{escaped_target}'
               AND OLD.folder_id <> NEW.folder_id THEN
                PERFORM pg_sleep(0.5);
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute(
        f'CREATE TRIGGER "{trigger_name}" BEFORE UPDATE ON files '
        f'FOR EACH ROW EXECUTE FUNCTION "{function_name}"()'
    )
    return lambda: drop_failure_trigger("files", trigger_name, function_name)


def assert_delete_transaction_error(label: str, resp) -> None:
    """Assert delete transaction failures keep the stable public error contract."""
    assert_equal(f"{label} returns HTTP 500", resp.status_code, 500)
    assert_equal(f"{label} returns InternalError code", json_field(resp.text, "code"), "10006")
    assert_equal(f"{label} returns stable message", json_field(resp.text, "message"), "Failed to delete items")


def empty_trash() -> dict[str, int]:
    """Empty the current user's trash and return response counters."""
    resp = fetch(
        "/api/trash/all",
        method="DELETE",
        headers=auth_headers(),
    )
    save_evidence(f"{EVIDENCE_PREFIX}-trash-empty.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail("empty trash failed")
        print(resp.text)
        print_summary()
    return {
        "deleted_count": int(json_field(resp.text, "data.deleted_count") or 0),
        "freed_space": int(json_field(resp.text, "data.freed_space") or 0),
    }


def create_share_fixture(file_ids: list[int] | None = None, folder_ids: list[int] | None = None) -> int:
    """Create an active share and direct share_files links for lifecycle cleanup assertions."""
    file_ids = file_ids or []
    folder_ids = folder_ids or []
    row = query_one(
        """
        INSERT INTO shares (share_code, user_id, permission, view_count, download_count, status, created_at, updated_at)
        VALUES (%s, %s, 'download', 0, 0, 1, NOW(), NOW())
        RETURNING id
        """,
        (unique_name("trash"), USER_ID),
    )
    if row is None:
        log_fail("share fixture created")
        print_summary()
    share_id = int(row["id"])
    for file_id in file_ids:
        execute(
            "INSERT INTO share_files (share_id, item_type, item_id, created_at) VALUES (%s, 'file', %s, NOW())",
            (share_id, file_id),
        )
    for folder_id in folder_ids:
        execute(
            "INSERT INTO share_files (share_id, item_type, item_id, created_at) VALUES (%s, 'folder', %s, NOW())",
            (share_id, folder_id),
        )
    return share_id


def share_status(share_id: int) -> int:
    """Return share status."""
    status = scalar("SELECT status FROM shares WHERE id = %s", (share_id,))
    if status is None:
        log_fail(f"share exists: id={share_id}")
        print_summary()
    return int(status)


def permanently_delete_trash_response(trash_id: int, request_id: str | None = None):
    """Permanently delete one trash item and return the raw HTTP response."""
    resp = fetch(
        "/api/trash",
        method="DELETE",
        headers=auth_headers(request_id=request_id),
        json_body={"trash_ids": [trash_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-trash-delete-{trash_id}.json", resp.text)
    return resp


def permanently_delete_trash(trash_id: int) -> None:
    """Permanently delete one trash item."""
    resp = permanently_delete_trash_response(trash_id)
    if resp.status_code != 200 or json_field(resp.text, "data.results.0.status") != "success":
        log_fail(f"permanent trash delete failed: trash_id={trash_id}")
        print(resp.text)
        print_summary()


def restore_trash(trash_id: int) -> int:
    """Restore one trash item and return the new file id."""
    resp = fetch(
        "/api/trash/restore",
        method="POST",
        headers=auth_headers(),
        json_body={"trash_ids": [trash_id]},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-trash-restore-{trash_id}.json", resp.text)
    restored_file_id = json_field(resp.text, "data.results.0.file_id")
    if (
        resp.status_code != 200
        or json_field(resp.text, "code") != "0"
        or json_field(resp.text, "data.results.0.status") != "success"
        or not restored_file_id
        or restored_file_id == "null"
    ):
        log_fail(f"trash restore failed: trash_id={trash_id}")
        print(resp.text)
        print_summary()
    return int(restored_file_id)


def run_expired_cleanup() -> dict[str, int]:
    """Run the deterministic admin/manual cleanup seam and return cleanup counts."""
    resp = fetch(
        "/api/admin/maintenance/cleanup/expired",
        method="POST",
        headers=auth_headers(),
    )
    save_evidence(f"{EVIDENCE_PREFIX}-cleanup-expired.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail("deterministic expired cleanup trigger failed")
        print(resp.text)
        print_summary()
    return {
        "expired_trash_deleted": int(json_field(resp.text, "data.expired_trash_deleted") or 0),
        "expired_upload_tasks_cleaned": int(json_field(resp.text, "data.expired_upload_tasks_cleaned") or 0),
    }


def expire_trash_row(trash_id: int) -> None:
    """Make one trash row eligible for deterministic expired-trash cleanup."""
    affected = execute(
        "UPDATE trash SET expires_at = NOW() - INTERVAL '1 second' WHERE id = %s",
        (trash_id,),
    )
    assert_equal(f"trash row {trash_id} marked expired", affected, 1)


def test_instant_upload_dedup_ref_count() -> None:
    """Verify instant upload reuses content and increments ref_count."""
    log_section("Instant Upload Dedup Ref-Count")
    payload = f"instant-dedup-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_instant_src_{unique_name()}.bin", payload)
    first_file = file_row(first_file_id)
    content_id = int(first_file["content_id"])
    content_after_first = content_row(content_id)
    before_ref = int(content_after_first["ref_count"])
    assert_equal("first upload creates content ref_count=1", before_ref, 1)
    assert_equal("first upload stores sha256", content_after_first["hash_sha256"], sha256_bytes(payload))

    quota_before_instant = user_quota()
    upload_id, file_hash, instant_file_id = init_upload(f"safety_instant_dst_{unique_name()}.bin", payload)
    quota_after_instant = user_quota()
    assert_equal("instant upload returns no upload task", upload_id, "")
    if instant_file_id is None:
        log_fail("instant upload returned no data.file.id")
        print_summary()
    instant_file = file_row(int(instant_file_id))
    after_ref = int(content_row(content_id)["ref_count"])

    assert_equal("instant file reuses content_id", int(instant_file["content_id"]), content_id)
    assert_numeric_delta("instant upload increments ref_count", before_ref, after_ref, 1)
    assert_numeric_delta(
        "instant upload increases used storage by logical file size",
        quota_before_instant["storage_used"],
        quota_after_instant["storage_used"],
        len(payload),
    )
    assert_equal(
        "instant upload preserves reserved storage",
        quota_after_instant["storage_reserved"],
        quota_before_instant["storage_reserved"],
    )
    assert_equal("instant upload hash matches existing content", file_hash, first_file["name"] and md5_bytes(payload))
    same_hash_rows = int(scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0)
    assert_equal("instant upload creates no duplicate content row", same_hash_rows, 1)


def test_instant_upload_content_ref_increment_failure_rolls_back() -> None:
    """Verify content reference update failure rolls back an instant upload."""
    log_section("Instant Upload Content Ref Increment Failure Rolls Back")
    payload = f"instant-ref-increment-failure-{unique_name()}".encode()
    source_file_id = upload_file(f"safety_instant_ref_source_{unique_name()}.bin", payload)
    source_file = file_row(source_file_id)
    content_id = int(source_file["content_id"])
    content_before = content_row(content_id)
    blob_path = local_blob_path(str(content_before["storage_path"]))
    before_ref = int(content_before["ref_count"])
    quota_before = user_quota()
    file_hash = md5_bytes(payload)
    target_name = f"safety_instant_ref_target_{unique_name()}.bin"
    request_id = f"safety-content-ref-increment-{unique_name()}"
    before_target_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = 0 AND name = %s",
            (USER_ID, target_name),
        ) or 0
    )
    before_task_count = int(
        scalar(
            "SELECT COUNT(*) FROM upload_tasks WHERE user_id = %s AND file_hash = %s",
            (USER_ID, file_hash),
        ) or 0
    )
    before_content_count = int(
        scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0
    )
    cleanup_trigger = install_content_ref_increment_failure_trigger(content_id)

    try:
        resp = fetch(
            "/api/file/upload/init",
            method="POST",
            headers=auth_headers(request_id=request_id),
            json_body={
                "filename": target_name,
                "file_size": len(payload),
                "file_hash": file_hash,
                "parent_id": 0,
            },
        )
        save_evidence(f"{EVIDENCE_PREFIX}-instant-ref-increment-failure.json", resp.text)
    finally:
        cleanup_trigger()

    assert_equal("content ref increment failure returns HTTP 500", resp.status_code, 500)
    assert_equal(
        "content ref increment failure returns InternalError code",
        json_field(resp.text, "code"),
        "10006",
    )
    assert_equal(
        "content ref increment failure returns stable message",
        json_field(resp.text, "message"),
        "Failed to update file content reference count",
    )
    wait_for_persistence_failure_log(
        response=resp,
        request_id=request_id,
        expected_message="File content reference increment failed",
        expected_operation="upload_init",
    )
    assert_application_log_excludes(
        "injected content ref increment failure",
        "content reference log excludes database exception text",
    )

    after_target_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = 0 AND name = %s",
            (USER_ID, target_name),
        ) or 0
    )
    after_task_count = int(
        scalar(
            "SELECT COUNT(*) FROM upload_tasks WHERE user_id = %s AND file_hash = %s",
            (USER_ID, file_hash),
        ) or 0
    )
    after_content_count = int(
        scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0
    )
    quota_after = user_quota()

    assert_equal("content ref increment fixture starts without target file", before_target_count, 0)
    assert_equal("content ref increment failure creates no target file", after_target_count, 0)
    assert_equal(
        "content ref increment failure creates no upload task",
        after_task_count,
        before_task_count,
    )
    assert_equal(
        "content ref increment failure keeps ref_count",
        int(content_row(content_id)["ref_count"]),
        before_ref,
    )
    assert_equal(
        "content ref increment failure keeps storage_used",
        quota_after["storage_used"],
        quota_before["storage_used"],
    )
    assert_equal(
        "content ref increment failure keeps storage_reserved",
        quota_after["storage_reserved"],
        quota_before["storage_reserved"],
    )
    assert_equal(
        "content ref increment failure creates no duplicate content row",
        after_content_count,
        before_content_count,
    )
    assert_path_exists("content ref increment failure keeps existing blob", blob_path)


def test_instant_upload_quota_rejection_no_side_effects() -> None:
    """Verify over-quota instant upload leaves no file, ref-count, or quota drift."""
    log_section("Instant Upload Quota Rejection")
    payload = f"instant-quota-reject-{unique_name()}".encode()
    source_file_id = upload_file(f"safety_instant_quota_src_{unique_name()}.bin", payload)
    source_file = file_row(source_file_id)
    content_id = int(source_file["content_id"])
    before_ref = int(content_row(content_id)["ref_count"])
    file_hash = md5_bytes(payload)
    target_name = f"safety_instant_quota_dst_{unique_name()}.bin"
    original = user_quota()
    before_same_hash_rows = int(scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0)
    before_target_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = 0 AND name = %s",
            (USER_ID, target_name),
        ) or 0
    )

    resp = None
    saturated: dict[str, int] | None = None
    quota_after_reject: dict[str, int] | None = None
    try:
        execute("UPDATE users SET storage_used = storage_quota WHERE id = %s", (USER_ID,))
        saturated = user_quota()
        resp = fetch(
            "/api/file/upload/init",
            method="POST",
            headers=auth_headers(),
            json_body={"filename": target_name, "file_size": len(payload), "file_hash": file_hash, "parent_id": 0},
        )
        save_evidence(f"{EVIDENCE_PREFIX}-instant-quota-reject.json", resp.text)
        quota_after_reject = user_quota()
    finally:
        execute(
            "UPDATE users SET storage_used = %s, storage_reserved = %s, storage_quota = %s WHERE id = %s",
            (original["storage_used"], original["storage_reserved"], original["storage_quota"], USER_ID),
        )

    if resp is None or saturated is None or quota_after_reject is None:
        log_fail("instant upload quota rejection request completed")
        print_summary()

    after_target_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = 0 AND name = %s",
            (USER_ID, target_name),
        ) or 0
    )
    after_ref = int(content_row(content_id)["ref_count"])
    restored = user_quota()
    after_same_hash_rows = int(scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0)

    assert_equal("instant upload quota rejection returns quota error", json_field(resp.text, "code"), "50004")
    assert_equal("instant upload quota rejection creates no target file", after_target_count, before_target_count)
    assert_equal("instant upload quota rejection leaves ref_count unchanged", after_ref, before_ref)
    assert_equal("instant upload quota rejection leaves used unchanged", quota_after_reject["storage_used"], saturated["storage_used"])
    assert_equal("instant upload quota rejection leaves reserved unchanged", quota_after_reject["storage_reserved"], saturated["storage_reserved"])
    assert_equal("instant upload quota rejection restores used", restored["storage_used"], original["storage_used"])
    assert_equal("instant upload quota rejection restores reserved", restored["storage_reserved"], original["storage_reserved"])
    assert_equal("instant upload quota rejection creates no duplicate content row", after_same_hash_rows, before_same_hash_rows)
    assert_path_exists(
        "instant upload quota rejection keeps existing final blob",
        local_blob_path(str(content_row(content_id)["storage_path"])),
    )


def create_matching_content_fixture(filename: str, payload: bytes) -> tuple[int, int, str]:
    """Create matching content and a logical file after upload init to simulate finalize-time dedup."""
    file_hash = md5_bytes(payload)
    sha256_hash = sha256_bytes(payload)
    blob_path = final_blob_path(sha256_hash)
    blob_path.parent.mkdir(parents=True, exist_ok=True)
    blob_path.write_bytes(payload)

    content = query_one(
        """
        INSERT INTO file_contents (hash_md5, hash_sha256, size, storage_path, mime_type, ref_count)
        VALUES (%s, %s, %s, %s, %s, 1)
        RETURNING id
        """,
        (file_hash, sha256_hash, len(payload), str(blob_path), ""),
    )
    if content is None:
        log_fail("matching file_contents fixture created")
        print_summary()
    content_id = int(content["id"])

    file_row_result = query_one(
        """
        INSERT INTO files (user_id, content_id, folder_id, name, extension, size, mime_type, path)
        VALUES (%s, %s, 0, %s, %s, %s, %s, %s)
        RETURNING id
        """,
        (USER_ID, content_id, filename, "bin", len(payload), "", f"/{filename}"),
    )
    if file_row_result is None:
        log_fail("matching files fixture created")
        print_summary()
    execute("UPDATE users SET storage_used = storage_used + %s WHERE id = %s", (len(payload), USER_ID))
    return int(file_row_result["id"]), content_id, file_hash


def test_completion_dedup_race_ref_count_and_accounting_current_rule() -> None:
    """Verify completion reuses content that appears after init and commits reserved storage to used."""
    log_section("Completion Dedup Race Ref-Count And Accounting")
    payload = f"completion-dedup-race-{unique_name()}".encode()
    upload_filename = f"safety_dedup_race_upload_{unique_name()}.bin"
    fixture_filename = f"safety_dedup_race_existing_{unique_name()}.bin"
    quota_before_init = user_quota()

    upload_id, file_hash, instant_file_id = init_upload(upload_filename, payload)
    assert_equal("dedup race starts as non-instant upload", instant_file_id is None, True)
    quota_after_init = user_quota()
    assert_numeric_delta(
        "dedup race init reserves storage",
        quota_before_init["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )
    upload_chunk(upload_id, payload)

    existing_file_id, content_id, fixture_hash = create_matching_content_fixture(fixture_filename, payload)
    assert_equal("dedup fixture hash matches upload hash", fixture_hash, file_hash)
    before_complete_ref = int(content_row(content_id)["ref_count"])
    quota_before_complete = user_quota()

    completed_file_id = complete_upload(upload_id)
    completed_file = file_row(completed_file_id)
    existing_file = file_row(existing_file_id)
    after_complete_ref = int(content_row(content_id)["ref_count"])
    quota_after_complete = user_quota()

    assert_equal("completion dedup reuses existing content_id", int(completed_file["content_id"]), content_id)
    assert_equal("dedup fixture file keeps same content_id", int(existing_file["content_id"]), content_id)
    assert_numeric_delta("completion dedup increments ref_count by current rule", before_complete_ref, after_complete_ref, 1)
    assert_numeric_delta(
        "completion dedup releases reserved storage",
        quota_before_complete["storage_reserved"],
        quota_after_complete["storage_reserved"],
        -len(payload),
    )
    assert_numeric_delta(
        "completion dedup commits reserved bytes to used storage by current backend rule",
        quota_before_complete["storage_used"],
        quota_after_complete["storage_used"],
        len(payload),
    )
    same_hash_rows = int(scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0)
    assert_equal("completion dedup creates no duplicate content row", same_hash_rows, 1)
    assert_path_exists(
        "dedup race keeps existing final blob",
        local_blob_path(str(content_row(content_id)["storage_path"])),
    )
    assert_storage_job_succeeded(
        "dedup race cleanup job converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("dedup race cleans temp upload directory", upload_temp_dir(upload_id))


def test_copy_ref_count_and_quota() -> None:
    """Verify copying file increments content ref-count and used storage."""
    log_section("Copy Ref-Count And Quota")
    payload = f"copy-ref-quota-{unique_name()}".encode()
    source_file_id = upload_file(f"safety_copy_src_{unique_name()}.bin", payload)
    source_file = file_row(source_file_id)
    content_id = int(source_file["content_id"])
    before_ref = int(content_row(content_id)["ref_count"])
    quota_before = user_quota()

    copy_target_id = create_folder(f"safety_copy_target_{unique_name()}")
    mappings = copy_file(source_file_id, copy_target_id)
    if not mappings:
        log_fail("copy produced at least one new file mapping")
        print_summary()
    copied_file_id = int(mappings[0]["new_id"])
    copied_file = file_row(copied_file_id)
    after_ref = int(content_row(content_id)["ref_count"])
    quota_after = user_quota()

    assert_equal("copied file reuses source content_id", int(copied_file["content_id"]), content_id)
    assert_numeric_delta("copy increments ref_count", before_ref, after_ref, 1)
    assert_numeric_delta("copy increases used storage by logical file size", quota_before["storage_used"], quota_after["storage_used"], len(payload))
    assert_equal("copy commits and clears reservation", quota_after["storage_reserved"], quota_before["storage_reserved"])


def test_commit_under_reservation_retains_recovery_artifacts() -> None:
    """Verify a reserved-to-used underflow leaves a recoverable finalizing task."""
    log_section("Reserved-To-Used Under-Reservation Rolls Back")
    payload = f"quota-under-reservation-{unique_name()}".encode()
    filename = f"safety_quota_under_reserved_{unique_name()}.bin"
    quota_before = user_quota()
    upload_id, file_hash, instant_file_id = init_upload(filename, payload)
    assert_equal("under-reservation fixture is not instant upload", instant_file_id is None, True)
    upload_chunk(upload_id, payload)

    quota_after_init = user_quota()
    forced_under_reserved = len(payload) - 1
    assert_equal(
        "upload init reserves fixture bytes",
        quota_after_init["storage_reserved"],
        quota_before["storage_reserved"] + len(payload),
    )
    affected = execute(
        "UPDATE users SET storage_reserved = %s WHERE id = %s",
        (forced_under_reserved, USER_ID),
    )
    assert_equal("under-reservation fixture corrupts one quota row", affected, 1)

    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=auth_headers(),
        json_body={"upload_id": upload_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-under-reserved-complete.json", resp.text)

    quota_after_failure = user_quota()
    task_after_failure = query_one(
        "SELECT status, reserved_bytes FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    assert_equal("under-reservation completion returns HTTP 500", resp.status_code, 500)
    assert_equal("under-reservation completion preserves error envelope", json_field(resp.text, "code"), "10006")
    assert_equal("under-reservation failure does not increase used", quota_after_failure["storage_used"], quota_before["storage_used"])
    assert_equal(
        "under-reservation failure does not clamp reserved",
        quota_after_failure["storage_reserved"],
        forced_under_reserved,
    )
    assert_db_row_absent(
        "under-reservation failure creates no file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_db_row_absent(
        "under-reservation failure rolls back content row",
        "SELECT id FROM file_contents WHERE hash_md5 = %s",
        (file_hash,),
    )
    blob_path = final_blob_path(sha256_bytes(payload))
    assert_path_exists("under-reservation retains promoted blob for reconciliation", blob_path)
    assert_equal("under-reservation leaves upload task retryable", task_after_failure is not None, True)
    if task_after_failure is not None:
        assert_equal("under-reservation leaves task finalizing", int(task_after_failure["status"]), 4)
        assert_equal("under-reservation preserves task reservation", int(task_after_failure["reserved_bytes"]), len(payload))

    cancel_resp = fetch(
        f"/api/file/upload/{upload_id}",
        method="DELETE",
        headers=auth_headers(),
    )
    assert_equal("finalizing task cancel returns conflict", cancel_resp.status_code, 409)
    assert_equal("finalizing task cancel returns ResourceConflict", json_field(cancel_resp.text, "code"), "10004")

    execute(
        "UPDATE users SET storage_reserved = %s WHERE id = %s",
        (quota_after_init["storage_reserved"], USER_ID),
    )
    execute(
        """
        UPDATE upload_tasks
        SET status = 0, lease_owner = NULL, lease_expires_at = NULL
        WHERE id = %s AND user_id = %s AND status = 4
        """,
        (upload_id, USER_ID),
    )
    blob_path.unlink(missing_ok=True)
    cancel_upload(upload_id)
    assert_equal("under-reservation cleanup restores reserved quota", user_quota()["storage_reserved"], quota_before["storage_reserved"])


def test_upload_quota_rejection_no_leak() -> None:
    """Verify upload init rejects used+reserved+file_size over quota without leaks."""
    log_section("Upload Quota Rejection")
    original = user_quota()
    payload = b"q"
    filename = f"safety_quota_reject_{unique_name()}.bin"
    before_task_count = int(scalar("SELECT COUNT(*) FROM upload_tasks WHERE user_id = %s", (USER_ID,)) or 0)

    execute("UPDATE users SET storage_reserved = GREATEST(storage_quota - storage_used, 0) WHERE id = %s", (USER_ID,))
    saturated = user_quota()
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers(),
        json_body={"filename": filename, "file_size": len(payload), "file_hash": md5_bytes(payload), "parent_id": 0},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-upload-quota-reject.json", resp.text)
    execute(
        "UPDATE users SET storage_used = %s, storage_reserved = %s, storage_quota = %s WHERE id = %s",
        (original["storage_used"], original["storage_reserved"], original["storage_quota"], USER_ID),
    )

    after_task_count = int(scalar("SELECT COUNT(*) FROM upload_tasks WHERE user_id = %s", (USER_ID,)) or 0)
    restored = user_quota()
    assert_equal("upload quota rejection returns error", json_field(resp.text, "code") != "0", True)
    assert_equal("quota saturation made reserved non-negative", saturated["storage_reserved"] >= 0, True)
    assert_equal("upload quota rejection creates no task", after_task_count, before_task_count)
    assert_equal("upload quota rejection restores used", restored["storage_used"], original["storage_used"])
    assert_equal("upload quota rejection restores reserved", restored["storage_reserved"], original["storage_reserved"])


def test_upload_task_insert_failure_rolls_back_reservation_and_retries() -> None:
    """Verify task insertion and quota reservation share one transaction."""
    log_section("Upload Task Insert Failure Atomic Rollback And Retry")
    payload = f"upload-task-insert-rollback-{unique_name()}".encode()
    filename = f"safety_upload_task_insert_{unique_name()}.bin"
    request_body = {
        "filename": filename,
        "file_size": len(payload),
        "file_hash": md5_bytes(payload),
        "parent_id": 0,
    }
    quota_before = user_quota()
    unexplained_before = unexplained_reserved_bytes()
    task_count_before = int(
        scalar(
            "SELECT COUNT(*) FROM upload_tasks WHERE user_id = %s AND filename = %s",
            (USER_ID, filename),
        )
        or 0
    )
    cleanup_trigger = install_upload_task_insert_failure_trigger(USER_ID, filename)

    try:
        response = fetch(
            "/api/file/upload/init",
            method="POST",
            headers=auth_headers(),
            json_body=request_body,
        )
        save_evidence(f"{EVIDENCE_PREFIX}-upload-task-insert-failure.json", response.text)
    finally:
        cleanup_trigger()

    assert_equal("task insert failure returns HTTP 500", response.status_code, 500)
    assert_equal("task insert failure returns InternalError", json_field(response.text, "code"), "10006")
    assert_equal(
        "task insert failure returns stable message",
        json_field(response.text, "message"),
        "Failed to create upload task",
    )
    assert_equal(
        "task insert failure creates no task",
        int(
            scalar(
                "SELECT COUNT(*) FROM upload_tasks WHERE user_id = %s AND filename = %s",
                (USER_ID, filename),
            )
            or 0
        ),
        task_count_before,
    )
    assert_equal("task insert failure rolls back quota", user_quota(), quota_before)
    assert_equal(
        "task insert failure creates no unexplained reservation",
        unexplained_reserved_bytes(),
        unexplained_before,
    )

    retry = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers(),
        json_body=request_body,
    )
    save_evidence(f"{EVIDENCE_PREFIX}-upload-task-insert-retry.json", retry.text)
    assert_equal("task insert retry returns HTTP 200", retry.status_code, 200)
    assert_equal("task insert retry succeeds", json_field(retry.text, "code"), "0")
    upload_id = json_field(retry.text, "data.upload_id")
    assert_equal("task insert retry creates an upload task", bool(upload_id), True)
    assert_numeric_delta(
        "task insert retry reserves exact bytes",
        quota_before["storage_reserved"],
        user_quota()["storage_reserved"],
        len(payload),
    )
    cancel_upload(str(upload_id))
    assert_equal("task insert retry cancellation restores quota", user_quota(), quota_before)


def test_concurrent_upload_init_reuses_one_task_and_reservation() -> None:
    """Verify concurrent identical init requests share one task and reservation."""
    log_section("Concurrent Upload Init Single Winner")
    concurrency = 8
    payload = f"concurrent-upload-init-{unique_name()}".encode()
    filename = f"safety_concurrent_upload_init_{unique_name()}.bin"
    file_hash = md5_bytes(payload)
    request_body = {
        "filename": filename,
        "file_size": len(payload),
        "file_hash": file_hash,
        "parent_id": 0,
    }
    quota_before = user_quota()
    unexplained_before = unexplained_reserved_bytes()
    active_before = int(
        scalar(
            "SELECT COUNT(*) FROM upload_tasks "
            "WHERE user_id = %s AND file_hash = %s AND status IN (0, 4)",
            (USER_ID, file_hash),
        )
        or 0
    )
    barrier = threading.Barrier(concurrency)

    def initialize(index: int):
        barrier.wait(timeout=10)
        return fetch(
            "/api/file/upload/init",
            method="POST",
            headers=auth_headers(request_id=f"safety-concurrent-init-{index}-{unique_name()}"),
            json_body=request_body,
        )

    with ThreadPoolExecutor(max_workers=concurrency) as executor:
        responses = list(executor.map(initialize, range(concurrency)))

    for index, response in enumerate(responses):
        save_evidence(f"{EVIDENCE_PREFIX}-concurrent-init-{index}.json", response.text)

    upload_ids = {
        str(upload_id)
        for response in responses
        if (upload_id := json_field(response.text, "data.upload_id"))
    }
    quota_during = user_quota()
    unexplained_during = unexplained_reserved_bytes()
    active_during = int(
        scalar(
            "SELECT COUNT(*) FROM upload_tasks "
            "WHERE user_id = %s AND file_hash = %s AND status IN (0, 4)",
            (USER_ID, file_hash),
        )
        or 0
    )

    for upload_id in upload_ids:
        cancel_upload(upload_id)
    quota_after_cancel = user_quota()

    assert_equal("concurrent init returns every response", len(responses), concurrency)
    assert_equal("concurrent init returns HTTP 200", {response.status_code for response in responses}, {200})
    assert_equal("concurrent init returns success code", {json_field(response.text, "code") for response in responses}, {"0"})
    assert_equal("concurrent init returns non-empty upload ids", len(upload_ids) > 0, True)
    assert_equal("concurrent init returns one upload id", len(upload_ids), 1)
    assert_numeric_delta("concurrent init creates one active task", active_before, active_during, 1)
    assert_numeric_delta(
        "concurrent init reserves file bytes once",
        quota_before["storage_reserved"],
        quota_during["storage_reserved"],
        len(payload),
    )
    assert_equal("concurrent init preserves used quota", quota_during["storage_used"], quota_before["storage_used"])
    assert_equal("concurrent init creates no unexplained reservation", unexplained_during, unexplained_before)
    assert_equal("concurrent init cancellation restores quota", quota_after_cancel, quota_before)


def test_folder_create_is_atomic_and_concurrent_conflicts_are_stable() -> None:
    """Verify nested folder creation is atomic and unique conflicts are stable."""
    log_section("Folder Create Atomicity And Concurrent Conflict")
    parent_id = create_folder(f"safety_folder_create_parent_{unique_name()}")
    child_name = f"safety_folder_create_child_{unique_name()}"
    parent_before = query_one(
        "SELECT item_count FROM folders WHERE id = %s AND user_id = %s",
        (parent_id, USER_ID),
    )
    if parent_before is None:
        log_fail("folder create atomicity parent exists")
        print_summary()
    count_before = int(parent_before["item_count"])
    cleanup_trigger = install_parent_item_count_failure_trigger(parent_id)

    try:
        failed_response = fetch(
            "/api/folder/create",
            method="POST",
            headers=auth_headers(),
            json_body={"name": child_name, "parent_id": parent_id},
        )
        save_evidence(f"{EVIDENCE_PREFIX}-folder-create-parent-count-failure.json", failed_response.text)
    finally:
        cleanup_trigger()

    failed_child_count = int(
        scalar(
            "SELECT COUNT(*) FROM folders WHERE user_id = %s AND parent_id = %s AND name = %s",
            (USER_ID, parent_id, child_name),
        )
        or 0
    )
    count_after_failure = int(
        scalar("SELECT item_count FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or 0
    )

    if failed_child_count > 0:
        execute(
            "DELETE FROM folders WHERE user_id = %s AND parent_id = %s AND name = %s",
            (USER_ID, parent_id, child_name),
        )

    retry_response = fetch(
        "/api/folder/create",
        method="POST",
        headers=auth_headers(),
        json_body={"name": child_name, "parent_id": parent_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-folder-create-parent-count-retry.json", retry_response.text)
    count_after_retry = int(
        scalar("SELECT item_count FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or 0
    )

    concurrent_name = f"safety_folder_create_concurrent_{unique_name()}"
    concurrency = 8
    barrier = threading.Barrier(concurrency)

    def create_same_folder(index: int):
        barrier.wait(timeout=10)
        return fetch(
            "/api/folder/create",
            method="POST",
            headers=auth_headers(request_id=f"safety-folder-create-{index}-{unique_name()}"),
            json_body={"name": concurrent_name, "parent_id": parent_id},
        )

    with ThreadPoolExecutor(max_workers=concurrency) as executor:
        concurrent_responses = list(executor.map(create_same_folder, range(concurrency)))

    for index, response in enumerate(concurrent_responses):
        save_evidence(f"{EVIDENCE_PREFIX}-folder-create-concurrent-{index}.json", response.text)

    success_responses = [response for response in concurrent_responses if response.status_code == 200]
    conflict_responses = [response for response in concurrent_responses if response.status_code == 409]
    concurrent_row_count = int(
        scalar(
            "SELECT COUNT(*) FROM folders WHERE user_id = %s AND parent_id = %s AND name = %s",
            (USER_ID, parent_id, concurrent_name),
        )
        or 0
    )
    count_after_concurrent = int(
        scalar("SELECT item_count FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or 0
    )

    assert_equal("parent count failure returns HTTP 500", failed_response.status_code, 500)
    assert_equal("parent count failure returns InternalError", json_field(failed_response.text, "code"), "10006")
    assert_equal(
        "parent count failure returns stable message",
        json_field(failed_response.text, "message"),
        "Failed to create folder, please try again later",
    )
    assert_equal("parent count failure rolls back child folder", failed_child_count, 0)
    assert_equal("parent count failure preserves parent count", count_after_failure, count_before)
    assert_equal("folder create retry returns HTTP 200", retry_response.status_code, 200)
    assert_equal("folder create retry succeeds", json_field(retry_response.text, "code"), "0")
    assert_numeric_delta("folder create retry increments parent once", count_before, count_after_retry, 1)
    assert_equal("concurrent folder create has one success", len(success_responses), 1)
    assert_equal("concurrent folder create has seven conflicts", len(conflict_responses), concurrency - 1)
    assert_equal(
        "concurrent folder conflicts use FolderAlreadyExists",
        {json_field(response.text, "code") for response in conflict_responses},
        {"50010"},
    )
    assert_equal("concurrent folder create inserts one row", concurrent_row_count, 1)
    assert_numeric_delta("concurrent folder create increments parent once", count_after_retry, count_after_concurrent, 1)


def test_concurrent_folder_rename_has_one_winner_and_stable_conflicts() -> None:
    """Verify concurrent sibling renames serialize before conflict checks."""
    log_section("Concurrent Folder Rename Single Winner")
    parent_id = create_folder(f"safety_folder_rename_parent_{unique_name()}")
    concurrency = 8
    original_names = [f"safety_folder_rename_source_{index}_{unique_name()}" for index in range(concurrency)]
    folder_ids = [create_folder(name, parent_id) for name in original_names]
    target_name = f"safety_folder_rename_target_{unique_name()}"
    parent_count_before = int(
        scalar("SELECT item_count FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or 0
    )
    barrier = threading.Barrier(concurrency)
    cleanup_trigger = install_folder_rename_delay_trigger(USER_ID, parent_id, target_name)

    def rename_to_same_target(index: int):
        barrier.wait(timeout=10)
        return fetch(
            f"/api/folder/{folder_ids[index]}/rename",
            method="PUT",
            headers=auth_headers(request_id=f"safety-folder-rename-{index}-{unique_name()}"),
            json_body={"new_name": target_name},
        )

    try:
        with ThreadPoolExecutor(max_workers=concurrency) as executor:
            responses = list(executor.map(rename_to_same_target, range(concurrency)))
    finally:
        cleanup_trigger()

    for index, response in enumerate(responses):
        save_evidence(f"{EVIDENCE_PREFIX}-folder-rename-concurrent-{index}.json", response.text)

    success_responses = [response for response in responses if response.status_code == 200]
    conflict_responses = [response for response in responses if response.status_code == 409]
    parent_path = str(
        scalar("SELECT path FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or ""
    )
    target_rows = int(
        scalar(
            "SELECT COUNT(*) FROM folders "
            "WHERE user_id = %s AND parent_id = %s AND name = %s AND path = %s",
            (USER_ID, parent_id, target_name, f"{parent_path}{target_name}/"),
        )
        or 0
    )
    original_rows = sum(
        int(
            scalar(
                "SELECT COUNT(*) FROM folders "
                "WHERE user_id = %s AND parent_id = %s AND name = %s AND path = %s",
                (USER_ID, parent_id, name, f"{parent_path}{name}/"),
            )
            or 0
        )
        for name in original_names
    )
    parent_count_after = int(
        scalar("SELECT item_count FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or 0
    )

    assert_equal("concurrent folder rename returns every response", len(responses), concurrency)
    assert_equal("concurrent folder rename has one success", len(success_responses), 1)
    assert_equal("concurrent folder rename has seven conflicts", len(conflict_responses), concurrency - 1)
    assert_equal(
        "concurrent folder rename conflicts use FolderAlreadyExists",
        {json_field(response.text, "code") for response in conflict_responses},
        {"50010"},
    )
    assert_equal("concurrent folder rename stores one target name and path", target_rows, 1)
    assert_equal("concurrent folder rename preserves loser names and paths", original_rows, concurrency - 1)
    assert_equal("concurrent folder rename preserves parent count", parent_count_after, parent_count_before)


def test_concurrent_file_rename_has_one_winner_and_stable_conflicts() -> None:
    """Verify concurrent sibling file renames serialize before conflict checks."""
    log_section("Concurrent File Rename Single Winner")
    parent_id = create_folder(f"safety_file_rename_parent_{unique_name()}")
    concurrency = 8
    original_names = [f"safety_file_rename_source_{index}_{unique_name()}.bin" for index in range(concurrency)]
    file_ids = [
        upload_file(name, f"file-rename-payload-{index}-{unique_name()}".encode(), parent_id)
        for index, name in enumerate(original_names)
    ]
    target_name = f"safety_file_rename_target_{unique_name()}.bin"
    parent_count_before = int(
        scalar("SELECT item_count FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or 0
    )
    barrier = threading.Barrier(concurrency)
    cleanup_trigger = install_file_rename_delay_trigger(USER_ID, parent_id, target_name)

    def rename_to_same_target(index: int):
        barrier.wait(timeout=10)
        return fetch(
            f"/api/file/{file_ids[index]}/rename",
            method="PUT",
            headers=auth_headers(request_id=f"safety-file-rename-{index}-{unique_name()}"),
            json_body={"new_name": target_name},
        )

    try:
        with ThreadPoolExecutor(max_workers=concurrency) as executor:
            responses = list(executor.map(rename_to_same_target, range(concurrency)))
    finally:
        cleanup_trigger()

    for index, response in enumerate(responses):
        save_evidence(f"{EVIDENCE_PREFIX}-file-rename-concurrent-{index}.json", response.text)

    success_responses = [response for response in responses if response.status_code == 200]
    conflict_responses = [response for response in responses if response.status_code == 409]
    parent_path = str(
        scalar("SELECT path FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or ""
    )
    target_rows = int(
        scalar(
            "SELECT COUNT(*) FROM files "
            "WHERE user_id = %s AND folder_id = %s AND name = %s AND path = %s",
            (USER_ID, parent_id, target_name, f"{parent_path}{target_name}"),
        )
        or 0
    )
    original_rows = sum(
        int(
            scalar(
                "SELECT COUNT(*) FROM files "
                "WHERE user_id = %s AND folder_id = %s AND name = %s AND path = %s",
                (USER_ID, parent_id, name, f"{parent_path}{name}"),
            )
            or 0
        )
        for name in original_names
    )
    parent_count_after = int(
        scalar("SELECT item_count FROM folders WHERE id = %s AND user_id = %s", (parent_id, USER_ID))
        or 0
    )
    server_log = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")

    assert_equal("concurrent file rename returns every response", len(responses), concurrency)
    assert_equal("concurrent file rename has one success", len(success_responses), 1)
    assert_equal("concurrent file rename has seven conflicts", len(conflict_responses), concurrency - 1)
    assert_equal(
        "concurrent file rename conflicts use FileAlreadyExists",
        {json_field(response.text, "code") for response in conflict_responses},
        {"50007"},
    )
    assert_equal("concurrent file rename stores one target name and path", target_rows, 1)
    assert_equal("concurrent file rename preserves loser names and paths", original_rows, concurrency - 1)
    assert_equal("concurrent file rename preserves parent count", parent_count_after, parent_count_before)
    assert_equal(
        "concurrent file rename does not expose the uniqueness constraint",
        "uk_files_user_folder_name" in server_log,
        False,
    )


def test_concurrent_same_name_file_moves_skip_conflicts() -> None:
    """Verify concurrent same-name moves serialize and preserve partial success."""
    log_section("Concurrent Same-Name File Move Single Winner")
    target_id = create_folder(f"safety_file_move_target_{unique_name()}")
    concurrency = 8
    source_ids = [
        create_folder(f"safety_file_move_source_{index}_{unique_name()}")
        for index in range(concurrency)
    ]
    filename = f"safety_file_move_collision_{unique_name()}.bin"
    file_ids = [
        upload_file(filename, f"file-move-payload-{index}-{unique_name()}".encode(), source_ids[index])
        for index in range(concurrency)
    ]
    source_paths = [
        str(scalar("SELECT path FROM folders WHERE id = %s AND user_id = %s", (source_id, USER_ID)) or "")
        for source_id in source_ids
    ]
    target_path = str(
        scalar("SELECT path FROM folders WHERE id = %s AND user_id = %s", (target_id, USER_ID)) or ""
    )
    barrier = threading.Barrier(concurrency)
    cleanup_trigger = install_file_move_delay_trigger(USER_ID, target_id, filename)

    def move_to_same_target(index: int):
        barrier.wait(timeout=10)
        return fetch(
            "/api/file/move",
            method="PUT",
            headers=auth_headers(request_id=f"safety-file-move-{index}-{unique_name()}"),
            json_body={"file_ids": [file_ids[index]], "folder_ids": [], "target_folder_id": target_id},
        )

    try:
        with ThreadPoolExecutor(max_workers=concurrency) as executor:
            responses = list(executor.map(move_to_same_target, range(concurrency)))
    finally:
        cleanup_trigger()

    for index, response in enumerate(responses):
        save_evidence(f"{EVIDENCE_PREFIX}-file-move-concurrent-{index}.json", response.text)

    successful_responses = [
        response
        for response in responses
        if response.status_code == 200 and json_field(response.text, "code") == "0"
    ]
    moved_counts = [int(json_field(response.text, "data.moved_file_count") or 0) for response in responses]
    target_rows = int(
        scalar(
            "SELECT COUNT(*) FROM files "
            "WHERE user_id = %s AND folder_id = %s AND name = %s AND path = %s",
            (USER_ID, target_id, filename, f"{target_path}{filename}"),
        )
        or 0
    )
    source_rows = sum(
        int(
            scalar(
                "SELECT COUNT(*) FROM files "
                "WHERE id = %s AND user_id = %s AND folder_id = %s AND path = %s",
                (file_ids[index], USER_ID, source_ids[index], f"{source_paths[index]}{filename}"),
            )
            or 0
        )
        for index in range(concurrency)
    )
    folder_ids = [target_id, *source_ids]
    mismatched_counts = int(
        scalar(
            "SELECT COUNT(*) FROM folders f "
            "WHERE f.id = ANY(%s) AND f.user_id = %s "
            "AND f.item_count <> ("
            "SELECT COUNT(*) FROM files child_file "
            "WHERE child_file.user_id = f.user_id AND child_file.folder_id = f.id"
            ") + ("
            "SELECT COUNT(*) FROM folders child_folder "
            "WHERE child_folder.user_id = f.user_id AND child_folder.parent_id = f.id"
            ")",
            (folder_ids, USER_ID),
        )
        or 0
    )
    server_log = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")

    assert_equal("concurrent file move returns every response", len(responses), concurrency)
    assert_equal("concurrent file move keeps every batch response successful", len(successful_responses), concurrency)
    assert_equal("concurrent file move counts exactly one winner", sum(moved_counts), 1)
    assert_equal("concurrent file move stores one target row and path", target_rows, 1)
    assert_equal("concurrent file move preserves seven source rows and paths", source_rows, concurrency - 1)
    assert_equal("concurrent file move keeps all parent counts exact", mismatched_counts, 0)
    assert_equal(
        "concurrent file move does not expose the uniqueness constraint",
        "uk_files_user_folder_name" in server_log,
        False,
    )


def test_copy_quota_rejection_no_side_effects() -> None:
    """Verify over-quota copy leaves no files or ref-count changes behind."""
    log_section("Copy Quota Rejection")
    payload = f"copy-quota-reject-{unique_name()}".encode()
    source_file_id = upload_file(f"safety_copy_quota_src_{unique_name()}.bin", payload)
    source_content_id = int(file_row(source_file_id)["content_id"])
    before_ref = int(content_row(source_content_id)["ref_count"])
    original = user_quota()
    before_file_count = int(scalar("SELECT COUNT(*) FROM files WHERE user_id = %s", (USER_ID,)) or 0)

    execute("UPDATE users SET storage_used = storage_quota WHERE id = %s", (USER_ID,))
    saturated = user_quota()
    resp = fetch(
        "/api/file/copy",
        method="POST",
        headers=auth_headers(),
        json_body={"file_ids": [source_file_id], "folder_ids": [], "target_folder_id": 0},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-copy-quota-reject.json", resp.text)
    quota_after_reject = user_quota()
    execute(
        "UPDATE users SET storage_used = %s, storage_reserved = %s, storage_quota = %s WHERE id = %s",
        (original["storage_used"], original["storage_reserved"], original["storage_quota"], USER_ID),
    )

    after_file_count = int(scalar("SELECT COUNT(*) FROM files WHERE user_id = %s", (USER_ID,)) or 0)
    after_ref = int(content_row(source_content_id)["ref_count"])
    assert_equal("copy quota rejection returns error", json_field(resp.text, "code") != "0", True)
    assert_equal("copy quota rejection creates no files", after_file_count, before_file_count)
    assert_equal("copy quota rejection leaves ref_count unchanged", after_ref, before_ref)
    assert_equal("copy quota rejection leaves saturated used unchanged", quota_after_reject["storage_used"], saturated["storage_used"])
    assert_equal("copy quota rejection leaves reserved unchanged", quota_after_reject["storage_reserved"], saturated["storage_reserved"])


def test_copy_conflict_releases_quota_and_refs_only_successes() -> None:
    """Verify conflict skips release reserved copy bytes and avoids ref-count drift."""
    log_section("Copy Conflict Releases Quota")
    conflict_payload = f"copy-conflict-{unique_name()}".encode()
    copied_payload = f"copy-no-conflict-{unique_name()}".encode()
    conflict_name = f"safety_copy_conflict_{unique_name()}.bin"
    copied_name = f"safety_copy_ok_{unique_name()}.bin"
    conflict_file_id = upload_file(conflict_name, conflict_payload)
    copied_file_id = upload_file(copied_name, copied_payload)
    conflict_content_id = int(file_row(conflict_file_id)["content_id"])
    copied_content_id = int(file_row(copied_file_id)["content_id"])

    target_folder_id = create_folder(f"safety_copy_conflict_target_{unique_name()}")
    upload_file(conflict_name, f"target-conflict-{unique_name()}".encode(), target_folder_id)

    conflict_ref_before = int(content_row(conflict_content_id)["ref_count"])
    copied_ref_before = int(content_row(copied_content_id)["ref_count"])
    quota_before = user_quota()

    data = copy_items([conflict_file_id, copied_file_id], [], target_folder_id)
    quota_after = user_quota()
    conflict_ref_after = int(content_row(conflict_content_id)["ref_count"])
    copied_ref_after = int(content_row(copied_content_id)["ref_count"])

    assert_equal("copy conflict response has one new file", len(data.get("new_files", [])), 1)
    assert_equal("copy conflict reports one copied file", int(data.get("copied_file_count", 0)), 1)
    assert_equal("copy conflict leaves conflicting ref_count unchanged", conflict_ref_after, conflict_ref_before)
    assert_numeric_delta("copy conflict increments successful ref_count", copied_ref_before, copied_ref_after, 1)
    assert_numeric_delta(
        "copy conflict releases skipped quota",
        quota_before["storage_used"],
        quota_after["storage_used"],
        len(copied_payload),
    )
    assert_equal("copy conflict releases reserved bytes", quota_after["storage_reserved"], quota_before["storage_reserved"])


def test_copy_folder_skip_releases_quota_and_refs() -> None:
    """Verify folder skip after reservation does not drift quota or refs."""
    log_section("Copy Folder Skip Releases Quota")
    parent_folder_id = create_folder(f"safety_folder_skip_parent_{unique_name()}")
    child_folder_id = create_folder(f"safety_folder_skip_child_{unique_name()}", parent_folder_id)
    payload = f"folder-skip-{unique_name()}".encode()
    file_id = upload_file(f"safety_folder_skip_file_{unique_name()}.bin", payload, parent_folder_id)
    content_id = int(file_row(file_id)["content_id"])
    before_ref = int(content_row(content_id)["ref_count"])
    quota_before = user_quota()
    before_folder_count = int(scalar("SELECT COUNT(*) FROM folders WHERE user_id = %s", (USER_ID,)) or 0)
    before_file_count = int(scalar("SELECT COUNT(*) FROM files WHERE user_id = %s", (USER_ID,)) or 0)

    data = copy_items([], [parent_folder_id], child_folder_id)
    quota_after = user_quota()
    after_ref = int(content_row(content_id)["ref_count"])
    after_folder_count = int(scalar("SELECT COUNT(*) FROM folders WHERE user_id = %s", (USER_ID,)) or 0)
    after_file_count = int(scalar("SELECT COUNT(*) FROM files WHERE user_id = %s", (USER_ID,)) or 0)

    assert_equal("folder skip reports zero copied folders", int(data.get("copied_folder_count", 0)), 0)
    assert_equal("folder skip reports zero copied files", int(data.get("copied_file_count", 0)), 0)
    assert_equal("folder skip leaves ref_count unchanged", after_ref, before_ref)
    assert_equal("folder skip leaves storage_used unchanged", quota_after["storage_used"], quota_before["storage_used"])
    assert_equal("folder skip releases reserved bytes", quota_after["storage_reserved"], quota_before["storage_reserved"])
    assert_equal("folder skip creates no folders", after_folder_count, before_folder_count)
    assert_equal("folder skip creates no files", after_file_count, before_file_count)


def test_copy_file_insert_failure_rolls_back_reservation_and_retries() -> None:
    """Verify copied file insert failure rolls back refs/used commit, releases reservation, and can retry."""
    log_section("Copy File Insert Failure Releases Reservation And Retries")
    payload = f"copy-insert-failure-{unique_name()}".encode()
    source_name = f"safety_copy_insert_fail_{unique_name()}.bin"
    source_file_id = upload_file(source_name, payload)
    source_content_id = int(file_row(source_file_id)["content_id"])
    target_folder_id = create_folder(f"safety_copy_insert_fail_target_{unique_name()}")

    before_ref = int(content_row(source_content_id)["ref_count"])
    quota_before = user_quota()
    drop_trigger = install_copy_file_insert_failure_trigger(target_folder_id, source_name)
    try:
        resp = copy_items_response([source_file_id], [], target_folder_id)
    finally:
        drop_trigger()

    quota_after_failure = user_quota()
    after_failure_ref = int(content_row(source_content_id)["ref_count"])
    copied_count_after_failure = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = %s AND name = %s",
            (USER_ID, target_folder_id, source_name),
        ) or 0
    )

    assert_equal("copy insert failure keeps success response for partial-copy contract", resp.status_code, 200)
    assert_equal("copy insert failure returns public success envelope", json_field(resp.text, "code"), "0")
    assert_equal("copy insert failure reports zero copied files", json_field(resp.text, "data.copied_file_count"), "0")
    assert_equal("copy insert failure creates no copied row", copied_count_after_failure, 0)
    assert_equal("copy insert failure rolls back ref_count", after_failure_ref, before_ref)
    assert_equal("copy insert failure leaves used storage unchanged", quota_after_failure["storage_used"], quota_before["storage_used"])
    assert_equal("copy insert failure releases reserved bytes", quota_after_failure["storage_reserved"], quota_before["storage_reserved"])

    retry_mappings = copy_file(source_file_id, target_folder_id)
    quota_after_retry = user_quota()
    after_retry_ref = int(content_row(source_content_id)["ref_count"])
    copied_count_after_retry = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = %s AND name = %s",
            (USER_ID, target_folder_id, source_name),
        ) or 0
    )

    assert_equal("copy retry creates one mapping", len(retry_mappings), 1)
    assert_equal("copy retry creates exactly one copied row", copied_count_after_retry, 1)
    assert_numeric_delta("copy retry increments ref_count once", before_ref, after_retry_ref, 1)
    assert_numeric_delta(
        "copy retry commits one used-storage delta",
        quota_before["storage_used"],
        quota_after_retry["storage_used"],
        len(payload),
    )
    assert_equal("copy retry leaves no lingering reservation", quota_after_retry["storage_reserved"], quota_before["storage_reserved"])


def test_copy_reserved_release_failure_stops_and_exposes_orphan() -> None:
    """Verify reserved-release failure stops later copy work and leaves reconciliation-visible reservation."""
    log_section("Copy Reserved Release Failure Stops And Exposes Orphan")
    conflict_payload = f"copy-release-fail-conflict-{unique_name()}".encode()
    copied_payload = f"copy-release-fail-ok-{unique_name()}".encode()
    folder_payload = f"copy-release-fail-folder-{unique_name()}".encode()
    conflict_name = f"safety_copy_release_conflict_{unique_name()}.bin"
    copied_name = f"safety_copy_release_ok_{unique_name()}.bin"
    folder_name = f"safety_copy_release_folder_{unique_name()}"

    conflict_file_id = upload_file(conflict_name, conflict_payload)
    copied_file_id = upload_file(copied_name, copied_payload)
    conflict_content_id = int(file_row(conflict_file_id)["content_id"])
    copied_content_id = int(file_row(copied_file_id)["content_id"])
    source_folder_id = create_folder(folder_name)
    folder_file_id = upload_file(f"safety_copy_release_folder_file_{unique_name()}.bin", folder_payload, source_folder_id)
    folder_content_id = int(file_row(folder_file_id)["content_id"])

    target_folder_id = create_folder(f"safety_copy_release_target_{unique_name()}")
    upload_file(conflict_name, f"target-conflict-{unique_name()}".encode(), target_folder_id)

    conflict_ref_before = int(content_row(conflict_content_id)["ref_count"])
    copied_ref_before = int(content_row(copied_content_id)["ref_count"])
    folder_ref_before = int(content_row(folder_content_id)["ref_count"])
    quota_before = user_quota()
    unexplained_before = unexplained_reserved_bytes()

    drop_trigger = install_reserved_release_failure_trigger(USER_ID)
    try:
        resp = copy_items_response([conflict_file_id, copied_file_id], [source_folder_id], target_folder_id)
    finally:
        drop_trigger()

    quota_after = user_quota()
    unexplained_after = unexplained_reserved_bytes()
    conflict_ref_after = int(content_row(conflict_content_id)["ref_count"])
    copied_ref_after = int(content_row(copied_content_id)["ref_count"])
    folder_ref_after = int(content_row(folder_content_id)["ref_count"])
    copied_file_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND folder_id = %s AND name = %s",
            (USER_ID, target_folder_id, copied_name),
        ) or 0
    )
    copied_folder_count = int(
        scalar(
            "SELECT COUNT(*) FROM folders WHERE user_id = %s AND parent_id = %s AND name = %s",
            (USER_ID, target_folder_id, folder_name),
        ) or 0
    )
    expected_orphaned_bytes = len(conflict_payload) + len(folder_payload)

    assert_equal("reserved release failure returns error", json_field(resp.text, "code") != "0", True)
    assert_equal("reserved release failure keeps prior copied file visible", copied_file_count, 1)
    assert_equal("reserved release failure stops before folder copy", copied_folder_count, 0)
    assert_equal("reserved release failure leaves conflicting ref_count unchanged", conflict_ref_after, conflict_ref_before)
    assert_numeric_delta("reserved release failure commits successful ref_count", copied_ref_before, copied_ref_after, 1)
    assert_equal("reserved release failure does not copy later folder refs", folder_ref_after, folder_ref_before)
    assert_numeric_delta(
        "reserved release failure commits only successful file bytes to used",
        quota_before["storage_used"],
        quota_after["storage_used"],
        len(copied_payload),
    )
    assert_numeric_delta(
        "reserved release failure leaves unprocessed/skipped bytes reserved",
        quota_before["storage_reserved"],
        quota_after["storage_reserved"],
        expected_orphaned_bytes,
    )
    assert_numeric_delta(
        "reserved release failure is visible as unexplained reservation",
        unexplained_before,
        unexplained_after,
        expected_orphaned_bytes,
    )

    execute("UPDATE users SET storage_reserved = %s WHERE id = %s", (quota_before["storage_reserved"], USER_ID))


def test_soft_delete_preserves_ref_count_storage_used_and_blob() -> None:
    """Verify soft delete moves to trash without releasing content refs, quota, or blob."""
    log_section("Soft Delete Preserves Ref-Count, Quota, And Blob")
    payload = f"soft-delete-boundary-{unique_name()}".encode()
    file_id = upload_file(f"safety_soft_delete_{unique_name()}.bin", payload)
    original_file = file_row(file_id)
    content_id = int(original_file["content_id"])
    content = content_row(content_id)
    blob_path = local_blob_path(str(content["storage_path"]))
    before_ref = int(content["ref_count"])
    quota_before = user_quota()
    assert_path_exists("blob exists before soft delete", blob_path)

    trash_id = delete_file_to_trash(file_id)
    trash_row = query_one(
        "SELECT content_id FROM trash WHERE id = %s AND user_id = %s",
        (trash_id, USER_ID),
    )
    if trash_row is None:
        log_fail(f"trash row exists after soft delete: id={trash_id}")
        print_summary()
    after_ref = int(content_row(content_id)["ref_count"])
    quota_after = user_quota()

    assert_db_row_absent("active file row removed on soft delete", "SELECT id FROM files WHERE id = %s", (file_id,))
    assert_equal("trash row keeps content_id", int(trash_row["content_id"]), content_id)
    assert_equal("soft delete keeps ref_count", after_ref, before_ref)
    assert_equal("soft delete keeps storage_used", quota_after["storage_used"], quota_before["storage_used"])
    assert_path_exists("soft delete keeps blob while in trash", blob_path)


def test_move_to_trash_trash_insert_failure_preserves_active_state() -> None:
    """Verify trash insert failure rolls back without removing active rows or shares."""
    log_section("Move-To-Trash Trash Insert Failure Preserves Active State")
    payload = f"trash-insert-failure-{unique_name()}".encode()
    filename = f"safety_trash_insert_failure_{unique_name()}.bin"
    file_id = upload_file(filename, payload)
    original_file = file_row(file_id)
    content_id = int(original_file["content_id"])
    content = content_row(content_id)
    blob_path = local_blob_path(str(content["storage_path"]))
    before_ref = int(content["ref_count"])
    quota_before = user_quota()
    share_id = create_share_fixture([file_id])
    request_id = f"safety-persistence-trash-insert-{unique_name()}"
    cleanup_trigger = install_trash_insert_failure_trigger(str(original_file["name"]))

    try:
        resp = delete_file_to_trash_response(file_id, request_id)
    finally:
        cleanup_trigger()

    assert_delete_transaction_error("trash insert failure", resp)
    wait_for_persistence_failure_log(
        response=resp,
        request_id=request_id,
        expected_message="Trash record batch insert failed",
    )
    assert_application_log_excludes(
        "injected trash insert failure",
        "trash insert helper log excludes database exception text",
    )
    assert_equal("trash insert failure keeps active file", query_one("SELECT id FROM files WHERE id = %s", (file_id,)) is not None, True)
    assert_db_row_absent(
        "trash insert failure creates no trash row",
        "SELECT id FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s",
        (USER_ID, file_id),
    )
    assert_equal(
        "trash insert failure keeps share link",
        int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s AND item_type = 'file' AND item_id = %s", (share_id, file_id)) or 0),
        1,
    )
    assert_equal("trash insert failure keeps share active", share_status(share_id), 1)
    assert_equal("trash insert failure keeps ref_count", int(content_row(content_id)["ref_count"]), before_ref)
    assert_equal("trash insert failure keeps storage_used", user_quota()["storage_used"], quota_before["storage_used"])
    assert_path_exists("trash insert failure keeps blob", blob_path)


def test_move_to_trash_active_file_delete_failure_rolls_back_trash_and_share_cleanup() -> None:
    """Verify active file delete failure rolls back trash insert and share cleanup."""
    log_section("Move-To-Trash Active File Delete Failure Rolls Back")
    payload = f"active-file-delete-failure-{unique_name()}".encode()
    file_id = upload_file(f"safety_file_delete_failure_{unique_name()}.bin", payload)
    original_file = file_row(file_id)
    content_id = int(original_file["content_id"])
    content = content_row(content_id)
    blob_path = local_blob_path(str(content["storage_path"]))
    before_ref = int(content["ref_count"])
    quota_before = user_quota()
    share_id = create_share_fixture([file_id])
    request_id = f"safety-persistence-file-delete-{unique_name()}"
    cleanup_trigger = install_file_delete_failure_trigger(file_id)

    try:
        resp = delete_file_to_trash_response(file_id, request_id)
    finally:
        cleanup_trigger()

    assert_delete_transaction_error("active file delete failure", resp)
    wait_for_persistence_failure_log(
        response=resp,
        request_id=request_id,
        expected_message="File batch delete failed",
    )
    assert_application_log_excludes(
        "injected file delete failure",
        "file delete helper log excludes database exception text",
    )
    assert_equal("active file delete failure keeps active file", query_one("SELECT id FROM files WHERE id = %s", (file_id,)) is not None, True)
    assert_db_row_absent(
        "active file delete failure rolls back trash row",
        "SELECT id FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s",
        (USER_ID, file_id),
    )
    assert_equal(
        "active file delete failure restores share link",
        int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s AND item_type = 'file' AND item_id = %s", (share_id, file_id)) or 0),
        1,
    )
    assert_equal("active file delete failure keeps share active", share_status(share_id), 1)
    assert_equal("active file delete failure keeps ref_count", int(content_row(content_id)["ref_count"]), before_ref)
    assert_equal("active file delete failure keeps storage_used", user_quota()["storage_used"], quota_before["storage_used"])
    assert_path_exists("active file delete failure keeps blob", blob_path)


def test_restore_preserves_ref_count_storage_used_and_removes_trash() -> None:
    """Verify restore recreates the active file without changing content refs or quota."""
    log_section("Restore Preserves Ref-Count And Quota")
    payload = f"restore-boundary-{unique_name()}".encode()
    file_id = upload_file(f"safety_restore_{unique_name()}.bin", payload)
    original_file = file_row(file_id)
    content_id = int(original_file["content_id"])
    before_ref = int(content_row(content_id)["ref_count"])
    quota_before = user_quota()

    trash_id = delete_file_to_trash(file_id)
    restored_file_id = restore_trash(trash_id)
    restored_file = file_row(restored_file_id)
    after_ref = int(content_row(content_id)["ref_count"])
    quota_after = user_quota()

    assert_equal("restored file reuses content_id", int(restored_file["content_id"]), content_id)
    assert_db_row_absent("trash row removed after restore", "SELECT id FROM trash WHERE id = %s", (trash_id,))
    assert_equal("restore keeps ref_count", after_ref, before_ref)
    assert_equal("restore keeps storage_used", quota_after["storage_used"], quota_before["storage_used"])


def test_share_cleanup_on_soft_delete() -> None:
    """Verify move-to-trash removes share links and cancels shares only when empty."""
    log_section("Share Cleanup On Soft Delete")
    share_fixture_id = unique_name()
    first_file_id = upload_file(
        f"safety_share_cleanup_a_{share_fixture_id}.bin",
        f"share-cleanup-a-{share_fixture_id}".encode(),
    )
    second_file_id = upload_file(
        f"safety_share_cleanup_b_{share_fixture_id}.bin",
        f"share-cleanup-b-{share_fixture_id}".encode(),
    )
    share_id = create_share_fixture([first_file_id, second_file_id])

    delete_file_to_trash(first_file_id)
    remaining_links = int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s", (share_id,)) or 0)
    deleted_link_count = int(
        scalar(
            "SELECT COUNT(*) FROM share_files WHERE share_id = %s AND item_type = 'file' AND item_id = %s",
            (share_id, first_file_id),
        ) or 0
    )
    assert_equal("deleted file share link removed", deleted_link_count, 0)
    assert_equal("share keeps remaining file link", remaining_links, 1)
    assert_equal("share remains active while one link remains", share_status(share_id), 1)

    delete_file_to_trash(second_file_id)
    final_links = int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s", (share_id,)) or 0)
    assert_equal("last share link removed", final_links, 0)
    assert_equal("empty share is cancelled", share_status(share_id), 0)


def test_folder_soft_delete_snapshot_preserves_ref_count_and_storage() -> None:
    """Verify folder soft delete writes a folder snapshot and preserves accounting."""
    log_section("Folder Soft Delete Snapshot Preserves Accounting")
    root_folder_id = create_folder(f"safety_folder_root_{unique_name()}")
    child_folder_id = create_folder(f"safety_folder_child_{unique_name()}", root_folder_id)
    root_payload = f"folder-root-file-{unique_name()}".encode()
    child_payload = f"folder-child-file-{unique_name()}".encode()
    root_file_id = upload_file(f"safety_folder_root_file_{unique_name()}.bin", root_payload, root_folder_id)
    child_file_id = upload_file(f"safety_folder_child_file_{unique_name()}.bin", child_payload, child_folder_id)
    root_content_id = int(file_row(root_file_id)["content_id"])
    child_content_id = int(file_row(child_file_id)["content_id"])
    before_refs = {
        root_content_id: int(content_row(root_content_id)["ref_count"]),
        child_content_id: int(content_row(child_content_id)["ref_count"]),
    }
    quota_before = user_quota()
    folder_share_id = create_share_fixture(folder_ids=[root_folder_id])

    trash_id = delete_folder_to_trash(root_folder_id)
    trash_row = query_one("SELECT item_data FROM trash WHERE id = %s AND user_id = %s", (trash_id, USER_ID))
    if trash_row is None:
        log_fail(f"folder trash row exists: id={trash_id}")
        print_summary()
    raw_item_data = trash_row["item_data"]
    item_data = raw_item_data if isinstance(raw_item_data, dict) else json.loads(str(raw_item_data))

    assert_equal("folder snapshot type", item_data.get("type"), "folder_tree")
    assert_equal("folder snapshot version", int(item_data.get("version")), 1)
    assert_equal("folder snapshot root id", int(item_data.get("root", {}).get("id")), root_folder_id)
    assert_equal(
        "folder snapshot contains child folder",
        any(int(folder.get("id")) == child_folder_id for folder in item_data.get("folders", [])),
        True,
    )
    snapshot_file_ids = {int(file.get("id")) for file in item_data.get("files", [])}
    assert_equal("folder snapshot contains root file", root_file_id in snapshot_file_ids, True)
    assert_equal("folder snapshot contains child file", child_file_id in snapshot_file_ids, True)
    assert_db_row_absent("root file row removed by folder soft delete", "SELECT id FROM files WHERE id = %s", (root_file_id,))
    assert_db_row_absent("child file row removed by folder soft delete", "SELECT id FROM files WHERE id = %s", (child_file_id,))
    assert_db_row_absent("child folder row removed by folder soft delete", "SELECT id FROM folders WHERE id = %s", (child_folder_id,))
    assert_equal("folder soft delete keeps root file ref_count", int(content_row(root_content_id)["ref_count"]), before_refs[root_content_id])
    assert_equal("folder soft delete keeps child file ref_count", int(content_row(child_content_id)["ref_count"]), before_refs[child_content_id])
    assert_equal("folder soft delete keeps storage_used", user_quota()["storage_used"], quota_before["storage_used"])
    assert_equal("folder share link removed", int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s", (folder_share_id,)) or 0), 0)
    assert_equal("folder share cancelled", share_status(folder_share_id), 0)


def test_move_to_trash_active_folder_delete_failure_rolls_back_snapshot_and_share_cleanup() -> None:
    """Verify folder row delete failure rolls back the folder snapshot, subtree delete, and share cleanup."""
    log_section("Move-To-Trash Active Folder Delete Failure Rolls Back")
    root_folder_id = create_folder(f"safety_folder_delete_failure_root_{unique_name()}")
    child_folder_id = create_folder(f"safety_folder_delete_failure_child_{unique_name()}", root_folder_id)
    root_payload = f"folder-delete-failure-root-{unique_name()}".encode()
    child_payload = f"folder-delete-failure-child-{unique_name()}".encode()
    root_file_id = upload_file(f"safety_folder_delete_failure_root_file_{unique_name()}.bin", root_payload, root_folder_id)
    child_file_id = upload_file(f"safety_folder_delete_failure_child_file_{unique_name()}.bin", child_payload, child_folder_id)
    root_content_id = int(file_row(root_file_id)["content_id"])
    child_content_id = int(file_row(child_file_id)["content_id"])
    root_content = content_row(root_content_id)
    child_content = content_row(child_content_id)
    root_blob_path = local_blob_path(str(root_content["storage_path"]))
    child_blob_path = local_blob_path(str(child_content["storage_path"]))
    before_refs = {
        root_content_id: int(root_content["ref_count"]),
        child_content_id: int(child_content["ref_count"]),
    }
    quota_before = user_quota()
    folder_share_id = create_share_fixture(folder_ids=[root_folder_id])
    request_id = f"safety-persistence-folder-delete-{unique_name()}"
    cleanup_trigger = install_folder_delete_failure_trigger(child_folder_id)

    try:
        resp = delete_folder_to_trash_response(root_folder_id, request_id)
    finally:
        cleanup_trigger()

    assert_delete_transaction_error("active folder delete failure", resp)
    wait_for_persistence_failure_log(
        response=resp,
        request_id=request_id,
        expected_message="Folder batch delete failed",
    )
    assert_application_log_excludes(
        "injected folder delete failure",
        "folder delete helper log excludes database exception text",
    )
    assert_db_row_absent(
        "active folder delete failure rolls back trash snapshot",
        "SELECT id FROM trash WHERE user_id = %s AND item_type = 'folder' AND item_id = %s",
        (USER_ID, root_folder_id),
    )
    assert_equal("active folder delete failure keeps root folder", query_one("SELECT id FROM folders WHERE id = %s", (root_folder_id,)) is not None, True)
    assert_equal("active folder delete failure keeps child folder", query_one("SELECT id FROM folders WHERE id = %s", (child_folder_id,)) is not None, True)
    assert_equal("active folder delete failure keeps root file", query_one("SELECT id FROM files WHERE id = %s", (root_file_id,)) is not None, True)
    assert_equal("active folder delete failure keeps child file", query_one("SELECT id FROM files WHERE id = %s", (child_file_id,)) is not None, True)
    assert_equal(
        "active folder delete failure restores folder share link",
        int(scalar("SELECT COUNT(*) FROM share_files WHERE share_id = %s AND item_type = 'folder' AND item_id = %s", (folder_share_id, root_folder_id)) or 0),
        1,
    )
    assert_equal("active folder delete failure keeps share active", share_status(folder_share_id), 1)
    assert_equal("active folder delete failure keeps root ref_count", int(content_row(root_content_id)["ref_count"]), before_refs[root_content_id])
    assert_equal("active folder delete failure keeps child ref_count", int(content_row(child_content_id)["ref_count"]), before_refs[child_content_id])
    assert_equal("active folder delete failure keeps storage_used", user_quota()["storage_used"], quota_before["storage_used"])
    assert_path_exists("active folder delete failure keeps root blob", root_blob_path)
    assert_path_exists("active folder delete failure keeps child blob", child_blob_path)


def test_delete_all_ref_count_quota_and_blob_cleanup() -> None:
    """Verify empty-trash uses permanent-delete semantics for refs, quota, and blobs."""
    log_section("Delete All Ref-Count, Quota, And Blob Cleanup")
    empty_trash()
    payload = f"delete-all-boundary-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_delete_all_a_{unique_name()}.bin", payload)
    second_file_id = upload_file(f"safety_delete_all_b_{unique_name()}.bin", payload)
    content_id = int(file_row(first_file_id)["content_id"])
    content = content_row(content_id)
    blob_path = local_blob_path(str(content["storage_path"]))
    before_ref = int(content["ref_count"])
    quota_before = user_quota()

    first_trash = delete_file_to_trash(first_file_id)
    second_trash = delete_file_to_trash(second_file_id)
    result = empty_trash()
    quota_after = user_quota()
    final_ref = int(content_row(content_id)["ref_count"])

    assert_equal("delete-all reports at least test rows", result["deleted_count"] >= 2, True)
    assert_db_row_absent("first delete-all trash row removed", "SELECT id FROM trash WHERE id = %s", (first_trash,))
    assert_db_row_absent("second delete-all trash row removed", "SELECT id FROM trash WHERE id = %s", (second_trash,))
    assert_numeric_delta("delete-all decrements ref_count for both files", before_ref, final_ref, -2)
    assert_equal("delete-all does not drive ref_count negative", final_ref >= 0, True)
    assert_numeric_delta("delete-all releases used storage for both files", quota_before["storage_used"], quota_after["storage_used"], -2 * len(payload))
    assert_storage_job_succeeded(
        "delete-all Blob GC job converges",
        f"blob-gc:{content_id}",
    )
    assert_path_absent("delete-all deletes blob at zero ref_count", blob_path)


def test_permanent_delete_quota_failure_rolls_back_and_retries() -> None:
    """Verify a failed used-storage decrement rolls back permanent deletion."""
    log_section("Permanent Delete Quota Failure Rollback And Retry")
    empty_trash()
    payload = f"trash-quota-rollback-{unique_name()}".encode()
    file_id = upload_file(f"safety_trash_quota_{unique_name()}.bin", payload)
    content_id = int(file_row(file_id)["content_id"])
    content = content_row(content_id)
    blob_path = local_blob_path(str(content["storage_path"]))
    trash_id = delete_file_to_trash(file_id)
    quota_before = user_quota()
    ref_before = int(content_row(content_id)["ref_count"])
    job_key = f"blob-gc:{content_id}"
    jobs_before = int(
        scalar("SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s", (job_key,)) or 0
    )
    request_id = f"trash-quota-rollback-{unique_name()}"
    cleanup_trigger = install_used_storage_decrement_failure_trigger(USER_ID)

    try:
        response = permanently_delete_trash_response(trash_id, request_id)
    finally:
        cleanup_trigger()

    assert_equal("quota failure returns HTTP 200 batch response", response.status_code, 200)
    assert_equal("quota failure keeps success envelope", json_field(response.text, "code"), "0")
    assert_equal(
        "quota failure reports one failed item",
        json_field(response.text, "data.results.0.status"),
        "failed",
    )
    assert_equal(
        "quota failure reports InternalError",
        json_field(response.text, "data.results.0.error.code"),
        "10006",
    )
    assert_equal(
        "quota failure returns stable item message",
        json_field(response.text, "data.results.0.error.message"),
        "Failed to permanently delete file",
    )
    assert_equal(
        "quota failure keeps trash row",
        query_one("SELECT id FROM trash WHERE id = %s", (trash_id,)) is not None,
        True,
    )
    assert_equal(
        "quota failure rolls back content decrement",
        int(content_row(content_id)["ref_count"]),
        ref_before,
    )
    assert_equal("quota failure preserves quota", user_quota(), quota_before)
    assert_equal(
        "quota failure rolls back Blob GC enqueue",
        int(scalar("SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s", (job_key,)) or 0),
        jobs_before,
    )
    assert_path_exists("quota failure retains physical Blob", blob_path)

    permanently_delete_trash(trash_id)
    assert_db_row_absent(
        "quota retry removes trash row",
        "SELECT id FROM trash WHERE id = %s",
        (trash_id,),
    )
    assert_equal("quota retry reaches zero ref_count", int(content_row(content_id)["ref_count"]), 0)
    assert_numeric_delta(
        "quota retry releases used storage",
        quota_before["storage_used"],
        user_quota()["storage_used"],
        -len(payload),
    )
    assert_storage_job_succeeded("quota retry Blob GC job converges", job_key)
    assert_path_absent("quota retry deletes zero-reference Blob", blob_path)


def test_permanent_delete_ref_count_and_blob_retention() -> None:
    """Verify selected permanent delete decrements refs and keeps shared blobs until zero refs."""
    log_section("Permanent Delete Ref-Count And Blob Retention")
    payload = f"trash-ref-blob-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_trash_a_{unique_name()}.bin", payload)
    second_file_id = upload_file(f"safety_trash_b_{unique_name()}.bin", payload)
    content_id = int(file_row(first_file_id)["content_id"])
    content = content_row(content_id)
    blob_path = local_blob_path(str(content["storage_path"]))
    quota_before = user_quota()
    before_ref = int(content["ref_count"])
    assert_path_exists("shared blob exists before permanent delete", blob_path)

    trash_first = delete_file_to_trash(first_file_id)
    permanently_delete_trash(trash_first)
    mid_ref = int(content_row(content_id)["ref_count"])
    quota_after_first = user_quota()
    assert_numeric_delta("first permanent delete decrements ref_count", before_ref, mid_ref, -1)
    assert_path_exists("blob retained while ref_count remains positive", blob_path)
    assert_numeric_delta("first permanent delete releases used storage", quota_before["storage_used"], quota_after_first["storage_used"], -len(payload))

    trash_second = delete_file_to_trash(second_file_id)
    permanently_delete_trash(trash_second)
    final_ref = int(content_row(content_id)["ref_count"])
    assert_equal("second permanent delete reaches zero ref_count", final_ref, 0)
    assert_equal("permanent delete does not drive ref_count negative", final_ref >= 0, True)
    assert_storage_job_succeeded(
        "permanent delete Blob GC job converges",
        f"blob-gc:{content_id}",
    )
    assert_path_absent("blob deleted when ref_count reaches zero", blob_path)


def test_expired_trash_cleanup_ref_count_quota_and_blob_retention() -> None:
    """Verify expired-trash cleanup uses permanent-delete semantics and blob safety."""
    log_section("Expired Trash Cleanup Ref-Count, Quota, And Blob Retention")
    payload = f"expired-trash-ref-blob-{unique_name()}".encode()
    first_file_id = upload_file(f"safety_expired_trash_a_{unique_name()}.bin", payload)
    second_file_id = upload_file(f"safety_expired_trash_b_{unique_name()}.bin", payload)
    content_id = int(file_row(first_file_id)["content_id"])
    content = content_row(content_id)
    blob_path = local_blob_path(str(content["storage_path"]))
    quota_before = user_quota()
    before_ref = int(content["ref_count"])
    assert_path_exists("shared blob exists before expired-trash cleanup", blob_path)

    trash_first = delete_file_to_trash(first_file_id)
    expire_trash_row(trash_first)
    first_counts = run_expired_cleanup()
    mid_ref = int(content_row(content_id)["ref_count"])
    quota_after_first = user_quota()

    assert_equal("expired cleanup reports first trash deletion", first_counts["expired_trash_deleted"] >= 1, True)
    assert_db_row_absent("expired trash row removed after cleanup", "SELECT id FROM trash WHERE id = %s", (trash_first,))
    assert_numeric_delta("expired cleanup decrements ref_count", before_ref, mid_ref, -1)
    assert_numeric_delta(
        "expired cleanup releases used storage by current backend rule",
        quota_before["storage_used"],
        quota_after_first["storage_used"],
        -len(payload),
    )
    assert_path_exists("expired cleanup retains shared blob while ref_count remains positive", blob_path)

    trash_second = delete_file_to_trash(second_file_id)
    expire_trash_row(trash_second)
    second_counts = run_expired_cleanup()
    final_ref = int(content_row(content_id)["ref_count"])
    quota_after_second = user_quota()

    assert_equal("expired cleanup reports second trash deletion", second_counts["expired_trash_deleted"] >= 1, True)
    assert_db_row_absent("second expired trash row removed after cleanup", "SELECT id FROM trash WHERE id = %s", (trash_second,))
    assert_equal("expired cleanup reaches zero ref_count after final reference", final_ref, 0)
    assert_equal("expired cleanup does not drive ref_count negative", final_ref >= 0, True)
    assert_numeric_delta(
        "final expired cleanup releases used storage by current backend rule",
        quota_after_first["storage_used"],
        quota_after_second["storage_used"],
        -len(payload),
    )
    assert_storage_job_succeeded(
        "expired cleanup Blob GC job converges",
        f"blob-gc:{content_id}",
    )
    assert_path_absent("expired cleanup deletes blob only when ref_count reaches zero", blob_path)


def main() -> None:
    """Run content/quota safety-net tests."""
    print("==========================================")
    print("Content + Quota Safety-Net Integration Tests")
    print("==========================================")
    print()

    SERVER_LOG_PATH.unlink(missing_ok=True)
    ensure_server()

    global TOKEN, USER_ID
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)
    USER_ID = current_user_id()
    log_info(f"Using user_id={USER_ID}, base_url={BASE_URL}")

    test_instant_upload_dedup_ref_count()
    test_instant_upload_content_ref_increment_failure_rolls_back()
    test_completion_dedup_race_ref_count_and_accounting_current_rule()
    test_commit_under_reservation_retains_recovery_artifacts()
    test_copy_ref_count_and_quota()
    test_upload_quota_rejection_no_leak()
    test_upload_task_insert_failure_rolls_back_reservation_and_retries()
    test_concurrent_upload_init_reuses_one_task_and_reservation()
    test_folder_create_is_atomic_and_concurrent_conflicts_are_stable()
    test_concurrent_folder_rename_has_one_winner_and_stable_conflicts()
    test_concurrent_file_rename_has_one_winner_and_stable_conflicts()
    test_concurrent_same_name_file_moves_skip_conflicts()
    test_copy_quota_rejection_no_side_effects()
    test_copy_conflict_releases_quota_and_refs_only_successes()
    test_copy_folder_skip_releases_quota_and_refs()
    test_copy_file_insert_failure_rolls_back_reservation_and_retries()
    test_copy_reserved_release_failure_stops_and_exposes_orphan()
    test_soft_delete_preserves_ref_count_storage_used_and_blob()
    test_move_to_trash_trash_insert_failure_preserves_active_state()
    test_move_to_trash_active_file_delete_failure_rolls_back_trash_and_share_cleanup()
    test_restore_preserves_ref_count_storage_used_and_removes_trash()
    test_share_cleanup_on_soft_delete()
    test_folder_soft_delete_snapshot_preserves_ref_count_and_storage()
    test_move_to_trash_active_folder_delete_failure_rolls_back_snapshot_and_share_cleanup()
    test_delete_all_ref_count_quota_and_blob_cleanup()
    test_permanent_delete_quota_failure_rolls_back_and_retries()
    test_permanent_delete_ref_count_and_blob_retention()
    test_expired_trash_cleanup_ref_count_quota_and_blob_retention()

    print_summary()


if __name__ == "__main__":
    main()

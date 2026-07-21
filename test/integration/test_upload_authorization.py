#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Prove that upload identifiers never grant cross-user lifecycle access."""

from __future__ import annotations

import atexit
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    assert_storage_job_succeeded,
    cleanup,
    db_connection,
    do_login,
    ensure_server,
    fetch,
    final_blob_path,
    json_field,
    log_pass,
    query_all,
    query_one,
    redis_delete_pattern,
    save_evidence,
    scalar,
    send_login_request,
    sha256_bytes,
    unique_name,
    upload_temp_dir,
)

atexit.register(cleanup)

TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
ATTACKER_PASSWORD = "UploadAuthz123"


def require_response(response: Any, status: int, code: int, label: str) -> None:
    actual_code = json_field(response.text, "code")
    if response.status_code != status or actual_code != str(code):
        raise AssertionError(
            f"{label}: expected HTTP {status} / code {code}, "
            f"got HTTP {response.status_code} / code {actual_code}"
        )


def json_response(response: Any, label: str) -> dict[str, Any]:
    try:
        parsed = json.loads(response.text)
    except json.JSONDecodeError as error:
        raise AssertionError(f"{label}: response was not JSON") from error
    if not isinstance(parsed, dict):
        raise AssertionError(f"{label}: response was not a JSON object")
    return parsed


def register_attacker() -> tuple[int, str]:
    username = unique_name("authz")
    email = f"{username}@test.example.com"
    redis_delete_pattern("rate:register:*")
    response = fetch(
        "/api/auth/register",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={
            "username": username,
            "email": email,
            "password": ATTACKER_PASSWORD,
        },
    )
    require_response(response, 200, 0, "register attacker")
    user_id = int(json_field(response.text, "data.user.id"))

    status, body = send_login_request(username, ATTACKER_PASSWORD)
    if status != 200 or json_field(body, "code") != "0":
        raise AssertionError(f"login attacker: expected HTTP 200 / code 0, got HTTP {status}")
    token = json_field(body, "data.access_token")
    if not token:
        raise AssertionError("login attacker: access token missing")
    return user_id, token


def business_snapshot(upload_id: str, owner_id: int, attacker_id: int, file_hash: str) -> dict[str, Any]:
    return {
        "task": query_one(
            "SELECT user_id, status, state_version, lease_owner, lease_expires_at, "
            "finalize_attempts, completed_file_id, reserved_bytes, staging_backend, "
            "staging_prefix, temp_path FROM upload_tasks WHERE id = %s",
            (upload_id,),
        ),
        "chunks": query_all(
            "SELECT chunk_index, size_bytes, hash_md5, etag, object_key "
            "FROM upload_task_chunks WHERE task_id = %s ORDER BY chunk_index",
            (upload_id,),
        ),
        "users": query_all(
            "SELECT id, storage_used, storage_reserved FROM users "
            "WHERE id IN (%s, %s) ORDER BY id",
            (owner_id, attacker_id),
        ),
        "files": query_all(
            "SELECT file.id, file.user_id, file.content_id, file.size FROM files AS file "
            "JOIN file_contents AS content ON content.id = file.content_id "
            "WHERE file.user_id IN (%s, %s) AND content.hash_md5 = %s ORDER BY file.id",
            (owner_id, attacker_id, file_hash),
        ),
        "contents": query_all(
            "SELECT id, hash_md5, hash_sha256, size, storage_path, ref_count "
            "FROM file_contents WHERE hash_md5 = %s ORDER BY id",
            (file_hash,),
        ),
        "jobs": query_all(
            "SELECT job_type, aggregate_id, dedupe_key FROM storage_jobs "
            "WHERE aggregate_id = %s ORDER BY id",
            (upload_id,),
        ),
    }


def file_tree_snapshot(path: Path) -> tuple[Any, ...]:
    if not path.exists():
        return ("absent",)
    if path.is_file():
        return ("file", path.stat().st_size, hashlib.sha256(path.read_bytes()).hexdigest())

    entries: list[tuple[str, str, int, str]] = []
    for entry in sorted(path.rglob("*")):
        relative = str(entry.relative_to(path))
        if entry.is_dir():
            entries.append((relative, "directory", 0, ""))
        else:
            content = entry.read_bytes()
            entries.append((relative, "file", len(content), hashlib.sha256(content).hexdigest()))
    return tuple(entries)


def storage_snapshot(upload_id: str, payload: bytes, backend: str) -> dict[str, Any]:
    if backend != "local":
        return {"backend": backend, "local_checked": False}
    return {
        "backend": backend,
        "local_checked": True,
        "session": file_tree_snapshot(upload_temp_dir(upload_id)),
        "assembled": file_tree_snapshot(upload_temp_dir(upload_id).with_suffix(".tmp")),
        "final": file_tree_snapshot(final_blob_path(sha256_bytes(payload))),
    }


def attacker_chunk(token: str, upload_id: str, payload: bytes, chunk_hash: str) -> Any:
    return fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={chunk_hash}",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/octet-stream",
        },
        data=payload,
    )


def complete(token: str, upload_id: str) -> Any:
    return fetch(
        "/api/file/upload/complete",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"upload_id": upload_id},
    )


def cancel(token: str, upload_id: str) -> Any:
    return fetch(
        f"/api/file/upload/{upload_id}",
        method="DELETE",
        headers={"Authorization": f"Bearer {token}"},
    )


def main() -> None:
    ensure_server()

    owner_token = do_login(TEST_USER, TEST_PASS)
    if not owner_token:
        raise SystemExit(1)

    owner = query_one(
        "SELECT id, storage_used, storage_reserved FROM users "
        "WHERE username = %s OR email = %s",
        (TEST_USER, TEST_USER),
    )
    if owner is None:
        raise AssertionError("owner row was not found after login")
    owner_id = int(owner["id"])
    owner_storage_used_before = int(owner["storage_used"])
    owner_storage_reserved_before = int(owner["storage_reserved"])

    attacker_id = 0
    upload_id = ""
    file_hash = ""
    backend = ""
    final_path: Path | None = None
    final_existed_before = False
    terminal_cleanup_succeeded = False
    try:
        attacker_id, attacker_token = register_attacker()
        nonce = unique_name("upload-authz")
        payload = f"{nonce}:owner-only-payload".encode()
        file_hash = hashlib.md5(payload).hexdigest()
        chunk_hash = hashlib.md5(payload).hexdigest()
        final_path = final_blob_path(sha256_bytes(payload))
        final_existed_before = final_path.exists()

        init_response = fetch(
            "/api/file/upload/init",
            method="POST",
            headers={
                "Authorization": f"Bearer {owner_token}",
                "Content-Type": "application/json",
            },
            json_body={
                "filename": f"{nonce}.bin",
                "file_size": len(payload),
                "file_hash": file_hash,
                "parent_id": 0,
            },
        )
        require_response(init_response, 200, 0, "owner upload init")
        init_body = json_response(init_response, "owner upload init")
        upload_id = str(init_body.get("data", {}).get("upload_id", ""))
        if not upload_id or init_body.get("data", {}).get("instant_upload") is True:
            raise AssertionError("owner upload init did not create a non-instant task")

        task = query_one(
            "SELECT user_id, staging_backend, reserved_bytes FROM upload_tasks WHERE id = %s",
            (upload_id,),
        )
        if task is None:
            raise AssertionError("owner upload task was not persisted")
        if int(task["user_id"]) != owner_id:
            raise AssertionError("owner upload task was persisted for another user")
        backend = str(task["staging_backend"])
        if int(task["reserved_bytes"]) != len(payload):
            raise AssertionError("owner upload reservation does not match payload size")

        before = business_snapshot(upload_id, owner_id, attacker_id, file_hash)
        storage_before = storage_snapshot(upload_id, payload, backend)

        first_attempts = (
            ("cross-user chunk before progress", attacker_chunk(attacker_token, upload_id, payload, chunk_hash)),
            ("cross-user complete before progress", complete(attacker_token, upload_id)),
            ("cross-user cancel before progress", cancel(attacker_token, upload_id)),
        )
        for label, response in first_attempts:
            require_response(response, 400, 50008, label)
        if business_snapshot(upload_id, owner_id, attacker_id, file_hash) != before:
            raise AssertionError("cross-user requests changed business state before owner progress")
        if storage_snapshot(upload_id, payload, backend) != storage_before:
            raise AssertionError("cross-user requests changed storage before owner progress")
        log_pass("Cross-user chunk/complete/cancel rejected before owner progress")

        owner_chunk = attacker_chunk(owner_token, upload_id, payload, chunk_hash)
        require_response(owner_chunk, 200, 0, "owner chunk")
        if json_field(owner_chunk.text, "data.uploaded") != "true":
            raise AssertionError("owner chunk was not recorded")

        after_owner_chunk = business_snapshot(upload_id, owner_id, attacker_id, file_hash)
        storage_after_owner_chunk = storage_snapshot(upload_id, payload, backend)
        for label, response in (
            (
                "cross-user chunk against cached owner task",
                attacker_chunk(attacker_token, upload_id, payload, chunk_hash),
            ),
            ("cross-user complete after full progress", complete(attacker_token, upload_id)),
            ("cross-user cancel after full progress", cancel(attacker_token, upload_id)),
        ):
            require_response(response, 400, 50008, label)
        if business_snapshot(upload_id, owner_id, attacker_id, file_hash) != after_owner_chunk:
            raise AssertionError("cross-user terminal requests changed completed chunk state")
        if storage_snapshot(upload_id, payload, backend) != storage_after_owner_chunk:
            raise AssertionError("cross-user terminal requests changed staging or final storage")
        log_pass("Cross-user complete/cancel rejected after all chunks were present")

        owner_before_cancel = query_one(
            "SELECT storage_used, storage_reserved FROM users WHERE id = %s",
            (owner_id,),
        )
        if owner_before_cancel is None:
            raise AssertionError("owner row disappeared before cancellation")

        require_response(cancel(owner_token, upload_id), 200, 0, "owner cancel")
        terminal_before_replay = business_snapshot(upload_id, owner_id, attacker_id, file_hash)
        terminal_before_replay.pop("jobs")

        require_response(cancel(owner_token, upload_id), 200, 0, "owner cancel replay")
        require_response(cancel(attacker_token, upload_id), 400, 50008, "cross-user cancel replay")
        terminal_after_replay = business_snapshot(upload_id, owner_id, attacker_id, file_hash)
        terminal_after_replay.pop("jobs")
        if terminal_after_replay != terminal_before_replay:
            raise AssertionError("owner or cross-user replay changed terminal business state")

        terminal_task = terminal_after_replay["task"]
        if terminal_task is None or int(terminal_task["status"]) != 2:
            raise AssertionError("owner cancellation did not persist Cancelled status")
        owner_after_cancel = next(
            row for row in terminal_after_replay["users"] if int(row["id"]) == owner_id
        )
        if int(owner_after_cancel["storage_used"]) != int(owner_before_cancel["storage_used"]):
            raise AssertionError("cancellation changed used storage")
        if int(owner_after_cancel["storage_reserved"]) != int(owner_before_cancel["storage_reserved"]) - len(payload):
            raise AssertionError("cancellation did not release reserved storage exactly once")
        if terminal_after_replay["chunks"] or terminal_after_replay["files"] or terminal_after_replay["contents"]:
            raise AssertionError("cancellation left chunk metadata or created file/content records")
        log_pass("Owner cancel replay remained idempotent and identity-scoped")

        terminal_cleanup_succeeded = assert_storage_job_succeeded(
            "Cancelled upload staging cleanup succeeded",
            f"staging-cleanup:{upload_id}",
        )
        if not terminal_cleanup_succeeded:
            raise AssertionError("cancelled upload cleanup did not succeed")
        if backend == "local" and file_tree_snapshot(upload_temp_dir(upload_id)) != ("absent",):
            raise AssertionError("cancelled upload staging directory still exists")

        save_evidence(
            "upload-authorization-summary.json",
            json.dumps(
                {
                    "upload_id": upload_id,
                    "staging_backend": backend,
                    "cross_user_attempts": 7,
                    "not_found_contract": {"http_status": 400, "code": 50008},
                    "business_state_unchanged": True,
                    "local_storage_checked": backend == "local",
                    "owner_cancel_replay_idempotent": True,
                    "staging_cleanup_succeeded": terminal_cleanup_succeeded,
                },
                indent=2,
            ),
        )
    finally:
        if upload_id or attacker_id:
            with db_connection() as connection:
                if upload_id:
                    connection.execute(
                        "DELETE FROM storage_jobs WHERE aggregate_id = %s",
                        (upload_id,),
                    )
                    connection.execute("DELETE FROM upload_tasks WHERE id = %s", (upload_id,))
                if file_hash:
                    connection.execute(
                        "DELETE FROM files AS file USING file_contents AS content "
                        "WHERE file.content_id = content.id AND content.hash_md5 = %s "
                        "AND file.user_id IN (%s, %s)",
                        (file_hash, owner_id, attacker_id),
                    )
                    connection.execute(
                        "DELETE FROM file_contents AS content WHERE content.hash_md5 = %s "
                        "AND NOT EXISTS (SELECT 1 FROM files WHERE content_id = content.id) "
                        "AND NOT EXISTS (SELECT 1 FROM trash WHERE content_id = content.id)",
                        (file_hash,),
                    )
                connection.execute(
                    "UPDATE users SET storage_used = %s, storage_reserved = %s WHERE id = %s",
                    (owner_storage_used_before, owner_storage_reserved_before, owner_id),
                )
                if attacker_id:
                    connection.execute("DELETE FROM users WHERE id = %s", (attacker_id,))

        if upload_id:
            shutil.rmtree(upload_temp_dir(upload_id), ignore_errors=True)
            upload_temp_dir(upload_id).with_suffix(".tmp").unlink(missing_ok=True)
        if backend == "local" and final_path is not None and not final_existed_before:
            final_path.unlink(missing_ok=True)

    if scalar("SELECT COUNT(*) FROM upload_tasks WHERE id = %s", (upload_id,)) != 0:
        raise AssertionError("test upload task cleanup failed")
    print("Upload authorization integration passed")


if __name__ == "__main__":
    main()

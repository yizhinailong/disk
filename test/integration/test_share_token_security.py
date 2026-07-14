#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Share-token scope and live share-state integration coverage."""

from __future__ import annotations

import atexit
import base64
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    cleanup,
    create_temp_file,
    do_login,
    ensure_server,
    execute,
    fetch,
    json_field,
    log_fail,
    log_info,
    log_pass,
    md5_hash,
    print_summary,
    save_evidence,
    unique_name,
)

atexit.register(cleanup)

TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
EVIDENCE_PREFIX = "share-token-security"
SHARE_EXPIRED_CODE = "60002"


def require_success(response, label: str) -> dict:
    try:
        body = json.loads(response.text)
    except json.JSONDecodeError as error:
        log_fail(f"{label}: response is not JSON: {error}")
        print(response.text)
        raise SystemExit(1) from error

    if response.status_code != 200 or str(body.get("code")) != "0":
        log_fail(
            f"{label}: expected HTTP 200 and code 0, got "
            f"HTTP {response.status_code}, code={body.get('code')}"
        )
        print(response.text)
        raise SystemExit(1)
    return body


def upload_fixture(owner_token: str, file_size: int = 256) -> str:
    path = create_temp_file(file_size)
    file_hash = md5_hash(path)
    filename = unique_name("share_token_security") + ".bin"

    try:
        init_response = fetch(
            "/api/file/upload/init",
            method="POST",
            headers={
                "Authorization": f"Bearer {owner_token}",
                "Content-Type": "application/json",
            },
            json_body={
                "filename": filename,
                "file_size": file_size,
                "file_hash": file_hash,
                "parent_id": 0,
            },
        )
        init_body = require_success(init_response, "upload init")
        save_evidence(f"{EVIDENCE_PREFIX}-upload-init.json", init_response.text)

        if init_body["data"]["instant_upload"]:
            file_id = init_body["data"].get("file_id")
            if not file_id:
                log_fail("instant upload did not return file_id")
                raise SystemExit(1)
            return str(file_id)

        upload_id = init_body["data"].get("upload_id")
        if not upload_id:
            log_fail("upload init did not return upload_id")
            raise SystemExit(1)

        with open(path, "rb") as fixture:
            content = fixture.read()

        chunk_response = fetch(
            f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
            method="POST",
            headers={
                "Authorization": f"Bearer {owner_token}",
                "Content-Type": "application/octet-stream",
            },
            data=content,
        )
        require_success(chunk_response, "upload chunk")
        save_evidence(f"{EVIDENCE_PREFIX}-upload-chunk.json", chunk_response.text)

        complete_response = fetch(
            "/api/file/upload/complete",
            method="POST",
            headers={
                "Authorization": f"Bearer {owner_token}",
                "Content-Type": "application/json",
            },
            json_body={"upload_id": upload_id},
        )
        complete_body = require_success(complete_response, "upload complete")
        save_evidence(f"{EVIDENCE_PREFIX}-upload-complete.json", complete_response.text)

        file_id = complete_body["data"]["file"].get("id")
        if not file_id:
            log_fail("upload complete did not return file id")
            raise SystemExit(1)
        return str(file_id)
    finally:
        os.unlink(path)


def create_download_share(owner_token: str, file_id: str, label: str) -> str:
    response = fetch(
        "/api/share",
        method="POST",
        headers={
            "Authorization": f"Bearer {owner_token}",
            "Content-Type": "application/json",
        },
        json_body={
            "file_ids": [int(file_id)],
            "permission": "download",
            "expire_days": 7,
        },
    )
    body = require_success(response, f"create {label} share")
    save_evidence(f"{EVIDENCE_PREFIX}-{label}-create.json", response.text)

    share_id = body["data"].get("share_id")
    if not share_id:
        log_fail(f"create {label} share did not return share_id")
        raise SystemExit(1)
    return str(share_id)


def access_share(share_id: str, label: str) -> str:
    response = fetch(
        f"/api/share/access/{share_id}",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )
    body = require_success(response, f"access {label} share")
    save_evidence(f"{EVIDENCE_PREFIX}-{label}-access.json", response.text)

    share_token = body["data"].get("share_token")
    if not share_token:
        log_fail(f"access {label} share did not return share_token")
        raise SystemExit(1)

    payload_segment = share_token.split(".")[1]
    payload_segment += "=" * (-len(payload_segment) % 4)
    payload = json.loads(base64.urlsafe_b64decode(payload_segment).decode("utf-8"))
    expected_scope = {"share_id": share_id, "permission": "download"}
    if payload.get("scope") != expected_scope:
        log_fail(
            f"{label} token scope mismatch: expected {expected_scope}, "
            f"got {payload.get('scope')}"
        )
        raise SystemExit(1)

    log_pass(f"{label} token contains the expected download scope")
    return str(share_token)


def cancel_share(owner_token: str, share_id: str) -> None:
    response = fetch(
        "/api/share",
        method="DELETE",
        headers={
            "Authorization": f"Bearer {owner_token}",
            "Content-Type": "application/json",
        },
        json_body={"share_ids": [share_id]},
    )
    body = require_success(response, "cancel share")
    save_evidence(f"{EVIDENCE_PREFIX}-cancelled-cancel.json", response.text)

    if body["data"]["summary"].get("succeeded") != 1:
        log_fail("cancel share did not report one successful cancellation")
        print(response.text)
        raise SystemExit(1)


def expire_share(share_id: str) -> None:
    affected = execute(
        "UPDATE shares SET expires_at = NOW() - INTERVAL '1 second' "
        "WHERE share_code = %s AND status = 1",
        (share_id,),
    )
    if affected != 1:
        log_fail(f"expire share expected one updated row, got {affected}")
        raise SystemExit(1)


def assert_old_token_rejected_everywhere(
    owner_token: str,
    file_id: str,
    share_id: str,
    share_token: str,
    label: str,
) -> None:
    share_headers = {"X-Share-Token": share_token}
    operations = [
        (
            "browse",
            f"/api/share/browse/{share_id}",
            "GET",
            share_headers,
            None,
        ),
        (
            "download-info",
            f"/api/share/download/{share_id}/{file_id}/info",
            "GET",
            share_headers,
            None,
        ),
        (
            "download-content",
            f"/api/share/download/{share_id}/{file_id}",
            "GET",
            share_headers,
            None,
        ),
        (
            "save-to-drive",
            f"/api/share/save/{share_id}",
            "POST",
            {
                "Authorization": f"Bearer {owner_token}",
                "X-Share-Token": share_token,
                "Content-Type": "application/json",
            },
            {
                "file_ids": [int(file_id)],
                "folder_ids": [],
                "target_folder_id": 0,
            },
        ),
    ]

    for operation, path, method, headers, json_body in operations:
        response = fetch(
            path,
            method=method,
            headers=headers,
            json_body=json_body,
        )
        save_evidence(
            f"{EVIDENCE_PREFIX}-{label}-{operation}.json",
            response.text,
        )
        code = json_field(response.text, "code")
        if code != SHARE_EXPIRED_CODE:
            log_fail(
                f"{label} {operation}: expected code {SHARE_EXPIRED_CODE}, "
                f"got HTTP {response.status_code}, code={code}"
            )
            print(response.text)
            raise SystemExit(1)
        log_pass(f"{label} old token rejected by {operation} with code 60002")


def main() -> None:
    print("==========================================")
    print("Share Token Security Integration Tests")
    print("==========================================")
    print()

    ensure_server()
    owner_token = do_login(TEST_USER, TEST_PASS)
    if not owner_token:
        raise SystemExit(1)

    file_id = upload_fixture(owner_token)
    log_info(f"Uploaded security fixture file_id={file_id}")

    cancelled_share = create_download_share(owner_token, file_id, "cancelled")
    cancelled_token = access_share(cancelled_share, "cancelled")
    cancel_share(owner_token, cancelled_share)
    assert_old_token_rejected_everywhere(
        owner_token,
        file_id,
        cancelled_share,
        cancelled_token,
        "cancelled",
    )

    expired_share = create_download_share(owner_token, file_id, "expired")
    expired_token = access_share(expired_share, "expired")
    expire_share(expired_share)
    assert_old_token_rejected_everywhere(
        owner_token,
        file_id,
        expired_share,
        expired_token,
        "expired",
    )

    print()
    print_summary()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Database-backed coverage for share-domain audit events and fail-open policy."""

from __future__ import annotations

import atexit
import hashlib
import json
import os
import sys
from typing import Any
from uuid import uuid4

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    cleanup,
    do_login,
    ensure_server,
    execute,
    fetch,
    json_field,
    log_fail,
    log_pass,
    print_summary,
    query_all,
    query_one,
    redis_delete_pattern,
)

TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
PASSWORD = "Audit28"
WRONG_PASSWORD = "Wrong28"
USER_AGENT = "disk-share-audit-test/1.0 " + ("u" * 600)
TRIGGER_NAME = "trg_test_reject_share_audit"
FUNCTION_NAME = "test_reject_share_audit"

TOKEN = ""
FILE_ID = 0
FILE_CONTENT = f"share-audit-{uuid4().hex}".encode("ascii")
SHARE_CODES: list[str] = []
AUDIT_CODES: list[str] = []
TRIGGER_INSTALLED = False


def fail(message: str, body: str = "") -> None:
    log_fail(message)
    if body:
        print(body)
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)
    log_pass(message)


def drop_failure_trigger() -> None:
    global TRIGGER_INSTALLED
    try:
        execute(f"DROP TRIGGER IF EXISTS {TRIGGER_NAME} ON operation_logs")
        execute(f"DROP FUNCTION IF EXISTS {FUNCTION_NAME}()")
    finally:
        TRIGGER_INSTALLED = False


def teardown() -> None:
    try:
        drop_failure_trigger()
    except Exception as exc:
        print(f"Audit trigger cleanup failed: {exc}", file=sys.stderr)

    if AUDIT_CODES:
        try:
            execute(
                "DELETE FROM operation_logs WHERE details->>'share_code' = ANY(%s)",
                (AUDIT_CODES,),
            )
        except Exception as exc:
            print(f"Audit row cleanup failed: {exc}", file=sys.stderr)

    if SHARE_CODES:
        try:
            execute("DELETE FROM shares WHERE share_code = ANY(%s)", (SHARE_CODES,))
        except Exception as exc:
            print(f"Share cleanup failed: {exc}", file=sys.stderr)
        for share_code in SHARE_CODES:
            redis_delete_pattern(f"rate:share_password:{share_code}:*")

    cleanup()


atexit.register(teardown)


def assert_schema_contract() -> None:
    column = query_one(
        """
        SELECT is_nullable
        FROM information_schema.columns
        WHERE table_schema = 'public'
          AND table_name = 'operation_logs'
          AND column_name = 'user_id'
        """
    )
    require(column is not None and column["is_nullable"] == "YES", "audit user_id is nullable")

    foreign_key = query_one(
        """
        SELECT rc.delete_rule
        FROM information_schema.referential_constraints rc
        WHERE rc.constraint_schema = 'public'
          AND rc.constraint_name = 'fk_operation_logs_user_id'
        """
    )
    require(
        foreign_key is not None and foreign_key["delete_rule"] == "SET NULL",
        "audit user foreign key preserves rows with SET NULL",
    )


def upload_fixture() -> int:
    filename = f"share_audit_{uuid4().hex}.bin"
    file_hash = hashlib.md5(FILE_CONTENT).hexdigest()
    auth_headers = {"Authorization": f"Bearer {TOKEN}"}

    init_response = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers,
        json_body={
            "filename": filename,
            "file_size": len(FILE_CONTENT),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    if json_field(init_response.text, "code") != "0":
        fail("audit fixture upload init succeeds", init_response.text)

    if json_field(init_response.text, "data.instant_upload") == "true":
        file_id = json_field(init_response.text, "data.file_id")
        if not file_id:
            fail("instant upload returns file id", init_response.text)
        return int(file_id)

    upload_id = json_field(init_response.text, "data.upload_id")
    if not upload_id:
        fail("upload init returns upload id", init_response.text)

    chunk_response = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
        method="POST",
        headers={**auth_headers, "Content-Type": "application/octet-stream"},
        data=FILE_CONTENT,
    )
    if json_field(chunk_response.text, "code") != "0":
        fail("audit fixture chunk upload succeeds", chunk_response.text)

    complete_response = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=auth_headers,
        json_body={"upload_id": upload_id},
    )
    file_id = json_field(complete_response.text, "data.file.id")
    if json_field(complete_response.text, "code") != "0" or not file_id:
        fail("audit fixture upload completes", complete_response.text)
    return int(file_id)


def create_share() -> str:
    response = fetch(
        "/api/share",
        method="POST",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "User-Agent": USER_AGENT,
        },
        json_body={
            "file_ids": [FILE_ID],
            "password": PASSWORD,
            "permission": "download",
            "expire_days": 1,
        },
    )
    share_code = json_field(response.text, "data.share_id")
    if response.status_code != 200 or json_field(response.text, "code") != "0" or not share_code:
        fail("share creation preserves success", response.text)
    SHARE_CODES.append(share_code)
    AUDIT_CODES.append(share_code)
    return share_code


def access_share(share_code: str, password: str):
    return fetch(
        f"/api/share/access/{share_code}",
        method="POST",
        headers={"User-Agent": USER_AGENT},
        json_body={"password": password},
    )


def download_share(share_code: str, share_token: str):
    return fetch(
        f"/api/share/download/{share_code}/{FILE_ID}",
        method="GET",
        headers={
            "X-Share-Token": share_token,
            "User-Agent": USER_AGENT,
        },
    )


def cancel_shares(share_codes: list[str]):
    return fetch(
        "/api/share",
        method="DELETE",
        headers={
            "Authorization": f"Bearer {TOKEN}",
            "User-Agent": USER_AGENT,
        },
        json_body={"share_ids": share_codes},
    )


def audit_rows(share_code: str) -> list[dict[str, Any]]:
    rows = query_all(
        """
        SELECT id, user_id, action, target_type, target_id, target_name,
               details, ip_address, user_agent
        FROM operation_logs
        WHERE details->>'share_code' = %s
        ORDER BY id
        """,
        (share_code,),
    )
    for row in rows:
        if isinstance(row["details"], str):
            row["details"] = json.loads(row["details"])
    return rows


def rows_for(rows: list[dict[str, Any]], action: str) -> list[dict[str, Any]]:
    return [row for row in rows if row["action"] == action]


def assert_successful_audit_flow(owner_id: int) -> None:
    share_code = create_share()
    missing_code = f"missing{uuid4().hex[:12]}"
    AUDIT_CODES.append(missing_code)

    share_row = query_one("SELECT id FROM shares WHERE share_code = %s", (share_code,))
    if share_row is None:
        fail("created share row exists")
    internal_share_id = int(share_row["id"])

    rejected = access_share(share_code, WRONG_PASSWORD)
    require(json_field(rejected.text, "code") == "60003", "wrong password keeps public error contract")

    accepted = access_share(share_code, PASSWORD)
    share_token = json_field(accepted.text, "data.share_token")
    require(
        accepted.status_code == 200 and json_field(accepted.text, "code") == "0" and bool(share_token),
        "correct password returns share token",
    )

    downloaded = download_share(share_code, share_token)
    require(downloaded.status_code == 200, "audited share download returns HTTP 200")
    require(downloaded.text.encode("utf-8") == FILE_CONTENT, "audited share download returns fixture bytes")

    cancelled = cancel_shares([share_code, missing_code, share_code])
    cancel_body = json.loads(cancelled.text)
    require(json_field(cancelled.text, "code") == "0", "mixed batch cancel returns success envelope")
    require(cancel_body["data"]["summary"] == {"total": 3, "succeeded": 1, "failed": 2}, "batch cancel summary matches final results")

    rows = audit_rows(share_code)
    missing_rows = audit_rows(missing_code)
    require(len(rows_for(rows, "share_create")) == 1, "share_create is recorded once")
    require(len(rows_for(rows, "share_access")) == 2, "rejected and successful share_access are recorded")
    require(len(rows_for(rows, "share_pwd_fail")) == 1, "share_pwd_fail is recorded once")
    require(len(rows_for(rows, "share_download")) == 1, "share_download is recorded once")
    require(len(rows_for(rows, "share_cancel")) == 2, "duplicate share cancel inputs have per-item events")
    require(len(rows_for(missing_rows, "share_cancel")) == 1, "missing batch item has its own cancel event")

    create_row = rows_for(rows, "share_create")[0]
    require(create_row["user_id"] == owner_id, "share_create records the authenticated owner")
    require(create_row["target_id"] == internal_share_id, "share_create targets internal shares.id")
    require(create_row["details"]["file_ids"] == [FILE_ID], "share_create records shared file ids")

    access_rows = rows_for(rows, "share_access")
    require(all(row["user_id"] is None for row in access_rows), "visitor access actors remain NULL")
    require(
        [row["details"]["result"] for row in access_rows] == ["validation_failed", "success"],
        "share_access records rejected and successful results",
    )

    password_row = rows_for(rows, "share_pwd_fail")[0]
    require(password_row["user_id"] is None, "password failure actor remains NULL")
    require(password_row["details"]["attempt_count"] == 1, "password failure records Redis attempt count")
    require(password_row["details"]["counter_available"] is True, "password failure records counter availability")

    download_row = rows_for(rows, "share_download")[0]
    require(download_row["user_id"] is None, "download visitor actor remains NULL")
    require(download_row["details"]["bytes"] == len(FILE_CONTENT), "download records selected payload bytes")
    require(download_row["details"]["http_status"] == 200, "download records HTTP result")

    cancel_rows = rows_for(rows, "share_cancel") + rows_for(missing_rows, "share_cancel")
    require(all(row["user_id"] == owner_id for row in cancel_rows), "cancel events record the authenticated operator")
    require(
        sorted(row["details"]["result"] for row in cancel_rows)
        == ["already_cancelled", "share_not_found", "success"],
        "batch cancel audit preserves every final per-item result",
    )
    require(rows_for(missing_rows, "share_cancel")[0]["target_id"] is None, "unresolved cancel target remains NULL")

    all_rows = rows + missing_rows
    require(all(row["target_type"] == "share" for row in all_rows), "all share audit targets use target_type share")
    require(all(row["target_name"] == row["details"]["share_code"] for row in all_rows), "target_name matches share_code")
    require(all(row["ip_address"] == "127.0.0.1" for row in all_rows), "audit rows record normalized visitor IP")
    require(all(len(row["user_agent"]) == 512 for row in all_rows), "audit User-Agent is capped at 512 characters")

    serialized_rows = json.dumps(all_rows, default=str).lower()
    for forbidden_value in (PASSWORD.lower(), WRONG_PASSWORD.lower(), share_token.lower()):
        require(forbidden_value not in serialized_rows, "audit rows exclude passwords and raw share tokens")
    for row in all_rows:
        for forbidden_key in ("password", "password_hash", "share_token", "authorization", "x-share-token"):
            require(forbidden_key not in row["details"], f"audit details exclude {forbidden_key}")


def install_failure_trigger() -> None:
    global TRIGGER_INSTALLED
    drop_failure_trigger()
    execute(
        f"""
        CREATE FUNCTION {FUNCTION_NAME}() RETURNS trigger
        LANGUAGE plpgsql AS $$
        BEGIN
            IF NEW.action LIKE 'share_%' THEN
                RAISE EXCEPTION 'injected share audit failure';
            END IF;
            RETURN NEW;
        END
        $$
        """
    )
    execute(
        f"""
        CREATE TRIGGER {TRIGGER_NAME}
        BEFORE INSERT ON operation_logs
        FOR EACH ROW EXECUTE FUNCTION {FUNCTION_NAME}()
        """
    )
    TRIGGER_INSTALLED = True


def assert_fail_open_policy() -> None:
    install_failure_trigger()
    share_code = create_share()

    rejected = access_share(share_code, WRONG_PASSWORD)
    require(json_field(rejected.text, "code") == "60003", "audit failure does not replace rejected-access result")

    accepted = access_share(share_code, PASSWORD)
    share_token = json_field(accepted.text, "data.share_token")
    require(bool(share_token), "audit failure does not block successful access")

    downloaded = download_share(share_code, share_token)
    require(downloaded.status_code == 200, "audit failure does not block download")

    cancelled = cancel_shares([share_code])
    require(json_field(cancelled.text, "data.summary.succeeded") == "1", "audit failure does not block cancellation")

    drop_failure_trigger()
    require(audit_rows(share_code) == [], "failed audit writes are not retried or duplicated later")


def main() -> None:
    ensure_server()
    assert_schema_contract()

    global TOKEN, FILE_ID
    TOKEN = do_login(TEST_USER, TEST_PASS) or ""
    if not TOKEN:
        fail("share audit test login succeeds")

    owner = query_one("SELECT id FROM users WHERE username = %s", (TEST_USER,))
    if owner is None:
        fail("share audit owner row exists")

    FILE_ID = upload_fixture()
    assert_successful_audit_flow(int(owner["id"]))
    assert_fail_open_policy()
    print_summary()


if __name__ == "__main__":
    main()

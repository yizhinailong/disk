#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify bounded database-arbitrated retries for generated share-code collisions."""

from __future__ import annotations

import atexit
import os
import re
import sys
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
    query_one,
    save_evidence,
    scalar,
)

TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
COLLISION_CODE = "COLL1DE1"
TRIGGER_NAME = "trg_test_share_code_collision"
FUNCTION_NAME = "test_share_code_collision"
SEQUENCE_NAME = "test_share_code_collision_sequence"

FOLDER_ID = 0
RETURNED_CODE = ""


def fail(message: str, body: str = "") -> None:
    log_fail(message)
    if body:
        print(body)
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)
    log_pass(message)


def drop_collision_objects() -> None:
    execute(f"DROP TRIGGER IF EXISTS {TRIGGER_NAME} ON shares")
    execute(f"DROP FUNCTION IF EXISTS {FUNCTION_NAME}()")
    execute(f"DROP SEQUENCE IF EXISTS {SEQUENCE_NAME}")


def teardown() -> None:
    try:
        drop_collision_objects()
    except Exception as exc:
        print(f"Share collision object cleanup failed: {exc}", file=sys.stderr)

    try:
        codes = [COLLISION_CODE]
        if RETURNED_CODE:
            codes.append(RETURNED_CODE)
        execute("DELETE FROM shares WHERE share_code = ANY(%s)", (codes,))
        if FOLDER_ID > 0:
            execute("DELETE FROM folders WHERE id = %s", (FOLDER_ID,))
    except Exception as exc:
        print(f"Share collision fixture cleanup failed: {exc}", file=sys.stderr)

    cleanup()


atexit.register(teardown)


def create_fixture(owner_id: int) -> None:
    global FOLDER_ID

    folder_name = f"share_collision_{uuid4().hex}"
    folder = query_one(
        """
        INSERT INTO folders
            (user_id, parent_id, name, path, depth, item_count)
        VALUES (%s, 0, %s, %s, 1, 0)
        RETURNING id
        """,
        (owner_id, folder_name, f"/{folder_name}/"),
    )
    if folder is None:
        fail("collision test folder fixture is created")
    FOLDER_ID = int(folder["id"])

    execute(
        """
        INSERT INTO shares
            (share_code, user_id, permission, view_count, download_count, status)
        VALUES (%s, %s, 'view', 0, 0, 1)
        """,
        (COLLISION_CODE, owner_id),
    )


def install_collision_trigger(collision_count: int) -> None:
    drop_collision_objects()
    execute(f"CREATE SEQUENCE {SEQUENCE_NAME} START WITH 1")
    execute(
        f"""
        CREATE FUNCTION {FUNCTION_NAME}() RETURNS trigger
        LANGUAGE plpgsql AS $$
        BEGIN
            IF nextval('{SEQUENCE_NAME}') <= {collision_count} THEN
                NEW.share_code := '{COLLISION_CODE}';
            END IF;
            RETURN NEW;
        END
        $$
        """
    )
    execute(
        f"""
        CREATE TRIGGER {TRIGGER_NAME}
        BEFORE INSERT ON shares
        FOR EACH ROW EXECUTE FUNCTION {FUNCTION_NAME}()
        """
    )


def assert_collision_retry(token: str) -> None:
    global RETURNED_CODE

    response = fetch(
        "/api/share",
        method="POST",
        headers={"Authorization": f"Bearer {token}"},
        json_body={
            "folder_ids": [FOLDER_ID],
            "permission": "view",
            "expire_days": 0,
        },
    )
    save_evidence("share-code-collision-create.json", response.text)

    require(response.status_code == 200, "share creation hides the injected code collision")
    require(json_field(response.text, "code") == "0", "share creation succeeds after retry")

    RETURNED_CODE = json_field(response.text, "data.share_id")
    require(
        re.fullmatch(r"[A-Za-z0-9]{8}", RETURNED_CODE) is not None,
        "retried share code preserves the eight-character public format",
    )
    require(RETURNED_CODE != COLLISION_CODE, "retry returns a fresh share code")

    trigger_calls = scalar(f"SELECT last_value FROM {SEQUENCE_NAME}")
    require(trigger_calls == 2, "database observed exactly one collision and one retry")

    returned_share = query_one(
        "SELECT id, user_id, permission FROM shares WHERE share_code = %s",
        (RETURNED_CODE,),
    )
    require(returned_share is not None, "retried share row is committed")

    association_count = scalar(
        """
        SELECT COUNT(*)
        FROM share_files
        WHERE share_id = %s AND item_type = 'folder' AND item_id = %s
        """,
        (returned_share["id"], FOLDER_ID),
    )
    require(association_count == 1, "retried share has exactly one requested association")

    fixture_count = scalar(
        "SELECT COUNT(*) FROM shares WHERE share_code = ANY(%s)",
        ([COLLISION_CODE, RETURNED_CODE],),
    )
    require(fixture_count == 2, "collision retry creates no extra or orphan share row")

    original_share = query_one(
        "SELECT user_id, permission FROM shares WHERE share_code = %s",
        (COLLISION_CODE,),
    )
    require(
        original_share is not None and original_share["permission"] == "view",
        "pre-existing collision row remains unchanged",
    )


def assert_collision_exhaustion(token: str) -> None:
    install_collision_trigger(5)
    share_count_before = scalar("SELECT COUNT(*) FROM shares")
    association_count_before = scalar("SELECT COUNT(*) FROM share_files")

    response = fetch(
        "/api/share",
        method="POST",
        headers={"Authorization": f"Bearer {token}"},
        json_body={
            "folder_ids": [FOLDER_ID],
            "permission": "download",
            "expire_days": 7,
        },
    )
    save_evidence("share-code-collision-exhausted.json", response.text)

    require(response.status_code == 500, "five collisions return the existing HTTP 500")
    require(
        json_field(response.text, "code") == "10006",
        "five collisions return the existing InternalError code",
    )
    require(
        scalar(f"SELECT last_value FROM {SEQUENCE_NAME}") == 5,
        "share creation stops after exactly five conflicting candidates",
    )
    require(
        scalar("SELECT COUNT(*) FROM shares") == share_count_before,
        "exhausted retries persist no share row",
    )
    require(
        scalar("SELECT COUNT(*) FROM share_files") == association_count_before,
        "exhausted retries persist no item association",
    )


def main() -> None:
    ensure_server()
    token = do_login(TEST_USER, TEST_PASS) or ""
    if not token:
        fail("share collision test login succeeds")

    owner = query_one("SELECT id FROM users WHERE username = %s", (TEST_USER,))
    if owner is None:
        fail("share collision owner exists")

    create_fixture(int(owner["id"]))
    install_collision_trigger(1)
    assert_collision_retry(token)
    assert_collision_exhaustion(token)
    print_summary()


if __name__ == "__main__":
    main()

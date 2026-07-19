#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Integration coverage for public share password failure protection."""

from __future__ import annotations

import atexit
import json
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor
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
    redis_delete_pattern,
    redis_get_value,
    redis_ttl,
    unique_name,
)

TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
CLIENT_IP = os.environ.get("SHARE_CLIENT_IP", "127.0.0.1")
SHARE_PASSWORD = "Open27"
WRONG_PASSWORD = "Wrong27"
PASSWORD_WINDOW_SECONDS = 900
FAILURE_LIMIT = 5
CONCURRENT_FAILURES = 12

TOKEN = ""
FOLDER_ID: int | None = None
CREATED_SHARE_IDS: list[str] = []
NONEXISTENT_SHARE_IDS: list[str] = []


def fail(message: str, body: str = "") -> None:
    log_fail(message)
    if body:
        print(body)
    raise SystemExit(1)


def password_key(share_id: str) -> str:
    return f"rate:share_password:{share_id}:{CLIENT_IP}"


def clear_access_limiter() -> None:
    """Clear this scenario's access bucket without touching authenticated buckets."""
    redis_delete_pattern(f"rate:share_access:{CLIENT_IP}:*")


def reset_counter(share_id: str) -> None:
    redis_delete_pattern(f"rate:share_password:{share_id}:*")
    clear_access_limiter()


def teardown() -> None:
    for share_id in CREATED_SHARE_IDS + NONEXISTENT_SHARE_IDS:
        redis_delete_pattern(f"rate:share_password:{share_id}:*")
    clear_access_limiter()

    if CREATED_SHARE_IDS:
        try:
            execute("DELETE FROM shares WHERE share_code = ANY(%s)", (CREATED_SHARE_IDS,))
        except Exception as exc:
            print(f"Share fixture cleanup failed: {exc}", file=sys.stderr)

    if FOLDER_ID is not None:
        try:
            execute("DELETE FROM folders WHERE id = %s", (FOLDER_ID,))
        except Exception as exc:
            print(f"Folder fixture cleanup failed: {exc}", file=sys.stderr)

    cleanup()


atexit.register(teardown)


def create_folder_fixture() -> int:
    resp = fetch(
        "/api/folder/create",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}"},
        json_body={"name": unique_name("share_password_fixture"), "parent_id": 0},
    )
    folder_id = json_field(resp.text, "data.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not folder_id:
        fail("Could not create folder fixture", resp.text)
    return int(folder_id)


def create_share(password: str | None) -> str:
    body: dict[str, object] = {
        "folder_ids": [FOLDER_ID],
        "expire_days": 1,
        "permission": "download",
    }
    if password is not None:
        body["password"] = password

    resp = fetch(
        "/api/share",
        method="POST",
        headers={"Authorization": f"Bearer {TOKEN}"},
        json_body=body,
    )
    share_id = json_field(resp.text, "data.share_id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not share_id:
        fail("Could not create share fixture", resp.text)
    CREATED_SHARE_IDS.append(share_id)
    return share_id


def access_share(share_id: str, password: str | None = None):
    body = {} if password is None else {"password": password}
    return fetch(
        f"/api/share/access/{share_id}",
        method="POST",
        json_body=body,
    )


def failure_signature(resp) -> tuple[int, str, str, object]:
    try:
        data = json.loads(resp.text).get("data", "missing")
    except json.JSONDecodeError:
        data = "invalid-json"
    return (
        resp.status_code,
        json_field(resp.text, "code"),
        json_field(resp.text, "message"),
        data,
    )


def assert_validation_failure(resp, context: str) -> None:
    expected = (400, "60003", "Share access validation failed", None)
    actual = failure_signature(resp)
    if actual != expected:
        fail(f"{context}: expected {expected}, got {actual}", resp.text)


def assert_blocked(resp, context: str) -> None:
    expected = (
        429,
        "10005",
        "Too many password verification attempts, please try again later",
        None,
    )
    actual = failure_signature(resp)
    if actual != expected:
        fail(f"{context}: expected {expected}, got {actual}", resp.text)


def assert_success(resp, context: str) -> None:
    token = json_field(resp.text, "data.share_token")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not token:
        fail(f"{context}: expected successful access with share_token", resp.text)


def test_passwordless_share_regression(share_id: str) -> None:
    reset_counter(share_id)
    resp = access_share(share_id)
    assert_success(resp, "Passwordless share")
    if redis_get_value(password_key(share_id)) is not None:
        fail("Passwordless access created a password failure counter")
    log_pass("Passwordless shares remain accessible without creating a failure counter")


def test_failures_are_indistinguishable(protected_share_id: str, nonexistent_share_id: str) -> None:
    responses = []
    for share_id, password in (
        (protected_share_id, None),
        (protected_share_id, ""),
        (protected_share_id, WRONG_PASSWORD),
        (nonexistent_share_id, WRONG_PASSWORD),
    ):
        reset_counter(share_id)
        response = access_share(share_id, password)
        assert_validation_failure(response, f"Validation failure for {share_id}")
        responses.append(failure_signature(response))

    if len(set(responses)) != 1:
        fail(f"Missing, wrong, and nonexistent failures differ: {responses}")
    log_pass("Missing, wrong, and nonexistent share failures are indistinguishable")


def test_five_failures_then_sixth_blocked(share_id: str) -> None:
    reset_counter(share_id)
    for attempt in range(1, FAILURE_LIMIT + 1):
        assert_validation_failure(access_share(share_id, WRONG_PASSWORD), f"Failure {attempt}")
    assert_blocked(access_share(share_id, WRONG_PASSWORD), "Sixth failure")
    if redis_get_value(password_key(share_id)) != "6":
        fail("Six failures did not produce counter value 6")
    log_pass("Five password failures pass through and the sixth is blocked")


def test_success_does_not_change_failure_count(share_id: str) -> None:
    reset_counter(share_id)
    for attempt in range(FAILURE_LIMIT + 1):
        response = access_share(share_id, WRONG_PASSWORD)
        if attempt < FAILURE_LIMIT:
            assert_validation_failure(response, f"Pre-success failure {attempt + 1}")
        else:
            assert_blocked(response, "Pre-success sixth failure")

    key = password_key(share_id)
    before = redis_get_value(key)
    assert_success(access_share(share_id, SHARE_PASSWORD), "Correct password after failures")
    after = redis_get_value(key)
    if before != "6" or after != before:
        fail(f"Correct password changed failure counter: before={before}, after={after}")
    log_pass("Correct password succeeds after the failure limit without changing the counter")


def test_failure_ttl_is_fixed(share_id: str) -> None:
    reset_counter(share_id)
    assert_validation_failure(access_share(share_id, WRONG_PASSWORD), "Initial TTL failure")
    key = password_key(share_id)
    first_ttl = redis_ttl(key)
    if not 0 < first_ttl <= PASSWORD_WINDOW_SECONDS:
        fail(f"Initial failure TTL is outside expected range: {first_ttl}")

    time.sleep(2)
    assert_validation_failure(access_share(share_id, WRONG_PASSWORD), "Second TTL failure")
    second_ttl = redis_ttl(key)
    if not 0 < second_ttl <= first_ttl - 1:
        fail(f"Failure refreshed TTL: first={first_ttl}, second={second_ttl}")
    log_pass(f"Password failure TTL is fixed (first={first_ttl}s, second={second_ttl}s)")


def test_concurrent_failures_are_atomic(share_id: str) -> None:
    reset_counter(share_id)
    with ThreadPoolExecutor(max_workers=CONCURRENT_FAILURES) as executor:
        responses = list(
            executor.map(
                lambda _: access_share(share_id, WRONG_PASSWORD),
                range(CONCURRENT_FAILURES),
            )
        )

    validation_count = sum(failure_signature(resp)[0:2] == (400, "60003") for resp in responses)
    blocked_count = sum(failure_signature(resp)[0:2] == (429, "10005") for resp in responses)
    if validation_count != FAILURE_LIMIT or blocked_count != CONCURRENT_FAILURES - FAILURE_LIMIT:
        fail(
            "Concurrent failures lost threshold ordering: "
            f"validation={validation_count}, blocked={blocked_count}"
        )
    if redis_get_value(password_key(share_id)) != str(CONCURRENT_FAILURES):
        fail("Concurrent failures did not atomically preserve every increment")
    ttl = redis_ttl(password_key(share_id))
    if not 0 < ttl <= PASSWORD_WINDOW_SECONDS:
        fail(f"Concurrent failure counter has invalid TTL: {ttl}")
    log_pass("Concurrent wrong passwords atomically increment one fixed-TTL counter")


def test_repeated_nonexistent_share_threshold(share_id: str) -> None:
    reset_counter(share_id)
    for attempt in range(1, FAILURE_LIMIT + 1):
        assert_validation_failure(access_share(share_id, WRONG_PASSWORD), f"Nonexistent failure {attempt}")
    assert_blocked(access_share(share_id, WRONG_PASSWORD), "Sixth nonexistent failure")
    if redis_get_value(password_key(share_id)) != "6":
        fail("Repeated nonexistent share attempts did not reach counter value 6")
    log_pass("Repeated nonexistent share attempts use the same five-versus-six threshold")


def main() -> None:
    global TOKEN, FOLDER_ID

    print("==========================================")
    print("Share Password Protection Integration Test")
    print("==========================================")

    ensure_server()
    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        fail("Login failed")
    TOKEN = token

    FOLDER_ID = create_folder_fixture()
    passwordless_share_id = create_share(None)
    protected_share_id = create_share(SHARE_PASSWORD)
    nonexistent_share_id = f"missing{uuid4().hex[:12]}"
    repeated_nonexistent_id = f"absent{uuid4().hex[:12]}"
    NONEXISTENT_SHARE_IDS.extend([nonexistent_share_id, repeated_nonexistent_id])

    test_passwordless_share_regression(passwordless_share_id)
    test_failures_are_indistinguishable(protected_share_id, nonexistent_share_id)
    test_five_failures_then_sixth_blocked(protected_share_id)
    test_success_does_not_change_failure_count(protected_share_id)
    test_failure_ttl_is_fixed(protected_share_id)
    test_concurrent_failures_are_atomic(protected_share_id)
    test_repeated_nonexistent_share_threshold(repeated_nonexistent_id)

    print_summary()


if __name__ == "__main__":
    main()

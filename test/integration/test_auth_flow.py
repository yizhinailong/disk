#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for login success/failure auth flow (NOT rate limiting).

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured with seed data
  - Redis configured

Usage:
  uv run test/integration/test_auth_flow.py
"""

import os
import sys

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
    send_login_request,
    json_field,
    fetch,
    redis_delete_pattern,
)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
VALID_ACCOUNT = os.environ.get("VALID_ACCOUNT", "admin")
VALID_PASS = os.environ.get("VALID_PASS", "Admin123")

import atexit

atexit.register(cleanup)


# Test 1: Login with valid credentials returns 200 with tokens
def test_login_success():
    log_info("Testing login with valid credentials...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)

    access_token = json_field(body, "data.access_token")
    refresh_token = json_field(body, "data.refresh_token")
    code = json_field(body, "code")

    if (
        status_code == 200
        and code == "0"
        and access_token
        and access_token != "null"
        and refresh_token
        and refresh_token != "null"
    ):
        log_pass("Login success: HTTP 200, code=0, both tokens returned")
    else:
        log_fail(
            f"Login success: expected HTTP 200 + code 0 + tokens, "
            f"got HTTP {status_code} code={code}"
        )
        print(body)
        sys.exit(1)


# Test 2: Login with wrong password returns error
def test_login_wrong_password():
    log_info("Testing login with wrong password...")

    status_code, body = send_login_request(VALID_ACCOUNT, "WrongPassword123")
    code = json_field(body, "code")

    if status_code != 200 or code != "0":
        log_pass(
            f"Login wrong password: HTTP {status_code}, code={code} (rejected as expected)"
        )
    else:
        log_fail("Login wrong password: expected failure but got success")
        print(body)
        sys.exit(1)


# Test 3: Login with nonexistent user returns error
def test_login_nonexistent_user():
    log_info("Testing login with nonexistent user...")

    status_code, body = send_login_request(
        "nonexistent_user_xyz_12345", "SomePassword123"
    )
    code = json_field(body, "code")

    if status_code != 200 or code != "0":
        log_pass(
            f"Login nonexistent user: HTTP {status_code}, code={code} (rejected as expected)"
        )
    else:
        log_fail("Login nonexistent user: expected failure but got success")
        print(body)
        sys.exit(1)


# Test 4: Login with missing fields returns error
def test_login_missing_fields():
    log_info("Testing login with missing fields...")

    resp = fetch(
        "/api/auth/login",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={"account": "admin"},
    )

    code = json_field(resp.text, "code")

    if resp.status_code != 200 or code != "0":
        log_pass(
            f"Login missing password: HTTP {resp.status_code}, code={code} (rejected as expected)"
        )
    else:
        log_fail("Login missing password: expected failure but got success")
        print(resp.text)
        sys.exit(1)


# Test 5: Login with empty body returns error
def test_login_empty_body():
    log_info("Testing login with empty body...")

    resp = fetch(
        "/api/auth/login",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )

    code = json_field(resp.text, "code")

    if resp.status_code != 200 or code != "0":
        log_pass(
            f"Login empty body: HTTP {resp.status_code}, code={code} (rejected as expected)"
        )
    else:
        log_fail("Login empty body: expected failure but got success")
        print(resp.text)
        sys.exit(1)


# Test 6: Access token can authenticate protected endpoints
def test_access_token_works():
    log_info("Testing that access token authenticates protected endpoint...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Cannot get access token for auth test")
        sys.exit(1)

    resp = fetch(
        "/api/user/profile",
        method="GET",
        headers={"Authorization": f"Bearer {access_token}"},
    )

    code = json_field(resp.text, "code")

    if resp.status_code == 200 and code == "0":
        log_pass("Access token authenticates /api/user/profile")
    else:
        log_fail(
            f"Access token failed to authenticate: HTTP {resp.status_code}, code={code}"
        )
        print(resp.text)
        sys.exit(1)


# Test 7: Invalid token is rejected
def test_invalid_token_rejected():
    log_info("Testing that invalid token is rejected...")

    resp = fetch(
        "/api/user/profile",
        method="GET",
        headers={"Authorization": "Bearer invalid.token.value"},
    )

    if resp.status_code == 401:
        log_pass("Invalid token rejected with 401")
    else:
        log_fail(f"Invalid token: expected 401, got HTTP {resp.status_code}")
        print(resp.text)
        sys.exit(1)


def main():
    print("==========================================")
    print("Auth Flow Integration Tests")
    print("==========================================\n")

    ensure_server()

    redis_delete_pattern("rate:*")

    test_login_success()
    test_login_wrong_password()
    test_login_nonexistent_user()
    test_login_missing_fields()
    test_login_empty_body()
    test_access_token_works()
    test_invalid_token_rejected()

    redis_delete_pattern("rate:*")

    print_summary()


if __name__ == "__main__":
    main()

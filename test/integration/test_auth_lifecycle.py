#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for auth lifecycle: register and logout.

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured
  - Redis configured

Usage:
  uv run test/integration/test_auth_lifecycle.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_section,
    print_summary,
    save_evidence,
    ensure_server,
    send_login_request,
    json_field,
    fetch,
)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")

TIMESTAMP_PID = f"{int(time.time())}_{os.getpid()}"
TEST_USERNAME = f"testuser_{TIMESTAMP_PID}"
TEST_EMAIL = f"testuser_{TIMESTAMP_PID}@test.example.com"
TEST_PASSWORD = "TestPass123"


# Test 1: Register new user
def test_register_new_user():
    log_info("Testing registration with new unique user...")

    resp = fetch(
        "/api/auth/register",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={
            "username": TEST_USERNAME,
            "email": TEST_EMAIL,
            "password": TEST_PASSWORD,
        },
    )

    code = json_field(resp.text, "code")
    user_id = json_field(resp.text, "data.user.id")
    username = json_field(resp.text, "data.user.username")

    save_evidence("register_new_user_response.json", resp.text)

    if (
        resp.status_code == 200
        and code == "0"
        and user_id
        and user_id != "null"
        and username == TEST_USERNAME
    ):
        log_pass(
            f"Register new user: HTTP 200, code=0, user.id={user_id}, username matches"
        )
    else:
        log_fail(
            f"Register new user: expected HTTP 200 + code 0 + user data, "
            f"got HTTP {resp.status_code} code={code}"
        )
        print(resp.text)
        sys.exit(1)


# Test 2: Login with new user
def test_login_new_user():
    log_info("Testing login with newly registered user...")

    status_code, body = send_login_request(TEST_USERNAME, TEST_PASSWORD)
    access_token = json_field(body, "data.access_token")
    code = json_field(body, "code")

    save_evidence("login_new_user_response.json", body)

    if status_code == 200 and code == "0" and access_token and access_token != "null":
        log_pass("Login new user: HTTP 200, code=0, access_token present")
    else:
        log_fail(
            f"Login new user: expected HTTP 200 + code 0 + token, "
            f"got HTTP {status_code} code={code}"
        )
        print(body)
        sys.exit(1)


# Test 3: Duplicate registration rejected
def test_duplicate_registration_rejected():
    log_info("Testing duplicate registration is rejected...")

    resp = fetch(
        "/api/auth/register",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={
            "username": TEST_USERNAME,
            "email": TEST_EMAIL,
            "password": TEST_PASSWORD,
        },
    )

    code = json_field(resp.text, "code")

    save_evidence("duplicate_registration_response.json", resp.text)

    if resp.status_code != 200 or code != "0":
        log_pass(
            f"Duplicate registration: HTTP {resp.status_code}, code={code} (rejected as expected)"
        )
    else:
        log_fail("Duplicate registration: expected failure but got success")
        print(resp.text)
        sys.exit(1)


# Test 4: Logout
def test_logout():
    log_info("Testing logout with valid token...")

    status_code, body = send_login_request(TEST_USERNAME, TEST_PASSWORD)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Cannot get access token for logout test")
        sys.exit(1)

    resp = fetch(
        "/api/auth/logout",
        method="POST",
        headers={"Authorization": f"Bearer {access_token}"},
    )

    code = json_field(resp.text, "code")

    save_evidence("logout_response.json", resp.text)

    if resp.status_code == 200 and code == "0":
        log_pass("Logout: HTTP 200, code=0")
    else:
        log_fail(
            f"Logout: expected HTTP 200 + code 0, got HTTP {resp.status_code} code={code}"
        )
        print(resp.text)
        sys.exit(1)


# Test 5: Token invalidation after logout
def test_token_invalidated_after_logout():
    log_info("Testing that token is invalidated after logout...")

    status_code, body = send_login_request(TEST_USERNAME, TEST_PASSWORD)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Cannot get access token for token invalidation test")
        sys.exit(1)

    # Logout to invalidate the token
    fetch(
        "/api/auth/logout",
        method="POST",
        headers={"Authorization": f"Bearer {access_token}"},
    )

    # Try to use the token — should get 401
    resp = fetch(
        "/api/user/profile",
        method="GET",
        headers={"Authorization": f"Bearer {access_token}"},
    )

    if resp.status_code == 401:
        log_pass("Token invalidation: old token returns 401 after logout")
    else:
        log_fail(f"Token invalidation: expected 401, got HTTP {resp.status_code}")
        print(resp.text)
        sys.exit(1)


def main():
    print("==========================================")
    print("Auth Lifecycle Integration Tests")
    print("==========================================\n")

    ensure_server()

    log_section("Test User Credentials")
    log_info(f"Username: {TEST_USERNAME}")
    log_info(f"Email: {TEST_EMAIL}")
    log_info(f"Password: {TEST_PASSWORD}")
    print()

    test_register_new_user()
    test_login_new_user()
    test_duplicate_registration_rejected()
    test_logout()
    test_token_invalidated_after_logout()

    print_summary()


if __name__ == "__main__":
    main()

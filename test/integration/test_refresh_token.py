#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for refresh token rotation.

Verifies:
  1. Login returns both access_token and refresh_token
  2. Refresh token can be exchanged for new token pair
  3. Old refresh_token is invalidated after rotation (single-use)
  4. New refresh_token from rotation works
  5. Invalid refresh_token is rejected

Usage:
  uv run test/integration/test_refresh_token.py
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    print_summary,
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


def do_refresh(refresh_token):
    resp = fetch(
        "/api/auth/refresh",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={"refresh_token": refresh_token},
    )
    return resp.status_code, resp.text


# Test 1: Login returns both access_token and refresh_token
def test_login_returns_both_tokens():
    log_info("Testing login returns both tokens...")

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
        log_pass("Login returns both access_token and refresh_token")
    else:
        log_fail("Login did not return both tokens")
        print(body)
        sys.exit(1)


# Test 2: Refresh token can get new token pair
def test_refresh_gets_new_tokens():
    log_info("Testing refresh token exchange...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    refresh_token = json_field(body, "data.refresh_token")

    if not refresh_token or refresh_token == "null":
        log_fail("No refresh_token from login")
        sys.exit(1)

    refresh_status, refresh_body = do_refresh(refresh_token)
    new_access = json_field(refresh_body, "data.access_token")
    new_refresh = json_field(refresh_body, "data.refresh_token")
    code = json_field(refresh_body, "code")

    if (
        refresh_status == 200
        and code == "0"
        and new_access
        and new_access != "null"
        and new_refresh
        and new_refresh != "null"
    ):
        log_pass("Refresh token exchange returns new token pair")
    else:
        log_fail(f"Refresh token exchange failed: HTTP {refresh_status}, code={code}")
        print(refresh_body)
        sys.exit(1)


# Test 3: Old refresh_token is invalidated after rotation
def test_old_refresh_token_invalidated():
    log_info("Testing old refresh_token is invalidated after rotation...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    old_refresh = json_field(body, "data.refresh_token")

    if not old_refresh or old_refresh == "null":
        log_fail("No refresh_token from login")
        sys.exit(1)

    # First refresh — should succeed
    refresh_status, refresh_body = do_refresh(old_refresh)
    code = json_field(refresh_body, "code")

    if refresh_status != 200 or code != "0":
        log_fail("First refresh should succeed but failed")
        print(refresh_body)
        sys.exit(1)

    # Second refresh with same old token — should fail (rotation)
    refresh_status, refresh_body = do_refresh(old_refresh)
    code = json_field(refresh_body, "code")

    if refresh_status != 200 or code != "0":
        log_pass("Old refresh_token correctly rejected after rotation")
    else:
        log_fail("Old refresh_token was accepted again (rotation broken!)")
        print(refresh_body)
        sys.exit(1)


# Test 4: New refresh_token from rotation works
def test_new_refresh_token_works():
    log_info("Testing new refresh_token from rotation works...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    old_refresh = json_field(body, "data.refresh_token")

    # First refresh to get new token
    refresh_status, refresh_body = do_refresh(old_refresh)
    new_refresh = json_field(refresh_body, "data.refresh_token")

    if not new_refresh or new_refresh == "null":
        log_fail("No new refresh_token from first rotation")
        sys.exit(1)

    # Use the new refresh token — should succeed
    refresh_status, refresh_body = do_refresh(new_refresh)
    code = json_field(refresh_body, "code")

    if refresh_status == 200 and code == "0":
        log_pass("New refresh_token from rotation works correctly")
    else:
        log_fail(
            f"New refresh_token from rotation failed: HTTP {refresh_status}, code={code}"
        )
        print(refresh_body)
        sys.exit(1)


# Test 5: Invalid refresh_token is rejected
def test_invalid_refresh_token_rejected():
    log_info("Testing invalid refresh_token is rejected...")

    refresh_status, refresh_body = do_refresh(
        "invalid.refresh.token.value.that.does.not.exist"
    )
    code = json_field(refresh_body, "code")

    if refresh_status != 200 or code != "0":
        log_pass(f"Invalid refresh_token rejected: HTTP {refresh_status}, code={code}")
    else:
        log_fail("Invalid refresh_token was accepted")
        print(refresh_body)
        sys.exit(1)


def main():
    print("==========================================")
    print("Refresh Token Rotation Integration Tests")
    print("==========================================\n")

    ensure_server()

    redis_delete_pattern("rate:*")

    test_login_returns_both_tokens()
    test_refresh_gets_new_tokens()
    test_old_refresh_token_invalidated()
    test_new_refresh_token_works()
    test_invalid_refresh_token_rejected()

    redis_delete_pattern("rate:*")

    print_summary()


if __name__ == "__main__":
    main()

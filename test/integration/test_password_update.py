#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for password update flow.

Verifies:
  1. Change password with correct old password succeeds
  2. Old password no longer works after change
  3. New password works after change
  4. Change password with wrong old password fails
  5. Restore original password at end

Usage:
  uv run test/integration/test_password_update.py
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
    send_login_request,
    json_field,
    fetch,
    redis_delete_pattern,
)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
VALID_ACCOUNT = os.environ.get("VALID_ACCOUNT", "admin")
VALID_PASS = os.environ.get("VALID_PASS", "Admin123")
NEW_PASS = os.environ.get("NEW_PASS", "TestNewPass456")

import atexit

atexit.register(cleanup)


def do_change_password(token, old_pw, new_pw):
    resp = fetch(
        "/api/user/password",
        method="PUT",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"old_password": old_pw, "new_password": new_pw},
    )
    return resp.status_code, resp.text


# Test 1: Change password with correct old password
def test_change_password_success():
    log_info("Testing password change with correct old password...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Login failed for password change test")
        print(body)
        sys.exit(1)

    change_status, change_body = do_change_password(access_token, VALID_PASS, NEW_PASS)
    code = json_field(change_body, "code")

    if change_status == 200 and code == "0":
        log_pass("Password change with correct old password succeeds")
    else:
        log_fail(f"Password change failed: HTTP {change_status}, code={code}")
        print(change_body)
        sys.exit(1)


# Test 2: Old password no longer works after change
def test_old_password_fails():
    log_info("Testing old password fails after change...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    code = json_field(body, "code")

    if status_code != 200 or code != "0":
        log_pass("Old password correctly rejected after change")
    else:
        log_fail("Old password still works after change!")
        print(body)
        sys.exit(1)


# Test 3: New password works after change
def test_new_password_works():
    log_info("Testing new password works after change...")

    status_code, body = send_login_request(VALID_ACCOUNT, NEW_PASS)
    access_token = json_field(body, "data.access_token")
    code = json_field(body, "code")

    if status_code == 200 and code == "0" and access_token and access_token != "null":
        log_pass("New password works for login")
    else:
        log_fail(f"New password login failed: HTTP {status_code}, code={code}")
        print(body)
        sys.exit(1)


# Test 4: Change password with wrong old password fails
def test_change_wrong_old_password():
    log_info("Testing password change with wrong old password...")

    status_code, body = send_login_request(VALID_ACCOUNT, NEW_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Login with new password failed")
        sys.exit(1)

    change_status, change_body = do_change_password(
        access_token, "WrongOldPass999", VALID_PASS
    )
    code = json_field(change_body, "code")

    if change_status != 200 or code != "0":
        log_pass("Password change with wrong old password rejected")
    else:
        log_fail("Password change with wrong old password succeeded!")
        print(change_body)
        sys.exit(1)


# Cleanup: Restore original password
def test_restore_password():
    log_info("Restoring original password...")

    status_code, body = send_login_request(VALID_ACCOUNT, NEW_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Cannot login to restore password")
        sys.exit(1)

    change_status, change_body = do_change_password(access_token, NEW_PASS, VALID_PASS)
    code = json_field(change_body, "code")

    if change_status == 200 and code == "0":
        log_pass("Original password restored")
    else:
        log_fail(
            f"Failed to restore original password: HTTP {change_status}, code={code}"
        )
        print(change_body)
        # Don't exit here — this is cleanup


def main():
    print("==========================================")
    print("Password Update Integration Tests")
    print("==========================================\n")

    ensure_server()

    redis_delete_pattern("rate:*")

    test_change_password_success()
    test_old_password_fails()
    test_new_password_works()
    test_change_wrong_old_password()
    test_restore_password()

    redis_delete_pattern("rate:*")

    print_summary()


if __name__ == "__main__":
    main()

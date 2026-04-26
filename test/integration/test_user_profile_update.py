#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for user profile update flow.

Verifies:
  1. Get current profile and save original nickname
  2. Update nickname with unique value succeeds
  3. Verify nickname persistence via GET
  4. Invalid payload (empty body) is rejected
  5. Restore original nickname at end

Usage:
  uv run test/integration/test_user_profile_update.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    print_summary,
    save_evidence,
    check_server,
    send_login_request,
    json_field,
    fetch,
    redis_delete_pattern,
)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
VALID_ACCOUNT = os.environ.get("VALID_ACCOUNT", "admin")
VALID_PASS = os.environ.get("VALID_PASS", "Admin123")

ORIGINAL_NICKNAME = ""
NEW_NICKNAME = ""


def do_get_profile(token):
    resp = fetch(
        "/api/user/profile",
        method="GET",
        headers={"Authorization": f"Bearer {token}"},
    )
    return resp.status_code, resp.text


def do_update_profile(token, nickname):
    resp = fetch(
        "/api/user/profile",
        method="PATCH",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={"nickname": nickname},
    )
    return resp.status_code, resp.text


# Test 1: Get current profile and save original nickname
def test_get_current_profile():
    global ORIGINAL_NICKNAME

    log_info("Getting current user profile...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Login failed for profile test")
        print(body)
        sys.exit(1)

    profile_status, profile_body = do_get_profile(access_token)
    code = json_field(profile_body, "code")

    if profile_status == 200 and code == "0":
        ORIGINAL_NICKNAME = json_field(profile_body, "data.user.nickname")
        log_pass(f"Retrieved current profile, original nickname: {ORIGINAL_NICKNAME}")
        save_evidence("user-profile-original.json", profile_body)
    else:
        log_fail(f"Failed to get profile: HTTP {profile_status}, code={code}")
        print(profile_body)
        sys.exit(1)


# Test 2: Update nickname with unique value
def test_update_nickname_success():
    global NEW_NICKNAME

    log_info("Testing nickname update with unique value...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Login failed for nickname update test")
        sys.exit(1)

    NEW_NICKNAME = f"TestNick{int(time.time())}_{os.getpid()}"
    update_status, update_body = do_update_profile(access_token, NEW_NICKNAME)
    code = json_field(update_body, "code")

    if update_status == 200 and code == "0":
        log_pass(f"Nickname updated successfully to: {NEW_NICKNAME}")
        save_evidence("user-profile-update-response.json", update_body)
    else:
        log_fail(f"Nickname update failed: HTTP {update_status}, code={code}")
        print(update_body)
        sys.exit(1)


# Test 3: Verify nickname persistence via GET
def test_verify_nickname_persistence():
    log_info("Verifying nickname persistence...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Login failed for persistence verification")
        sys.exit(1)

    profile_status, profile_body = do_get_profile(access_token)
    code = json_field(profile_body, "code")
    current_nickname = json_field(profile_body, "data.user.nickname")

    if profile_status == 200 and code == "0" and current_nickname == NEW_NICKNAME:
        log_pass(f"Nickname persisted correctly: {current_nickname}")
        save_evidence("user-profile-persisted.json", profile_body)
    else:
        log_fail(
            f"Nickname persistence verification failed: HTTP {profile_status}, "
            f"code={code}, expected '{NEW_NICKNAME}', got '{current_nickname}'"
        )
        print(profile_body)
        sys.exit(1)


# Test 4: Invalid payload (empty body) should be rejected
def test_invalid_empty_body():
    log_info("Testing invalid payload (empty body)...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Login failed for invalid payload test")
        sys.exit(1)

    resp = fetch(
        "/api/user/profile",
        method="PATCH",
        headers={
            "Authorization": f"Bearer {access_token}",
            "Content-Type": "application/json",
        },
        json_body={},
    )

    code = json_field(resp.text, "code")

    # API returns 400 + code 10001 for empty body
    if resp.status_code == 400 or code != "0":
        log_pass(f"Empty body correctly rejected: HTTP {resp.status_code}, code={code}")
        save_evidence("user-profile-empty-body-rejection.json", resp.text)
    else:
        log_fail(
            f"Empty body should be rejected but succeeded: HTTP {resp.status_code}, code={code}"
        )
        print(resp.text)
        sys.exit(1)


# Test 5: Restore original nickname
def test_restore_original_nickname():
    log_info("Restoring original nickname...")

    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Cannot login to restore nickname")
        sys.exit(1)

    if not ORIGINAL_NICKNAME:
        log_fail("Original nickname not saved, cannot restore")
        sys.exit(1)

    update_status, update_body = do_update_profile(access_token, ORIGINAL_NICKNAME)
    code = json_field(update_body, "code")

    if update_status == 200 and code == "0":
        log_pass(f"Original nickname restored: {ORIGINAL_NICKNAME}")
        save_evidence("user-profile-restored.json", update_body)
    else:
        log_fail(
            f"Failed to restore original nickname: HTTP {update_status}, code={code}"
        )
        print(update_body)
        # Don't exit here — this is cleanup


def main():
    print("==========================================")
    print("User Profile Update Integration Tests")
    print("==========================================\n")

    check_server()

    redis_delete_pattern("rate:*")

    test_get_current_profile()
    test_update_nickname_success()
    test_verify_nickname_persistence()
    test_invalid_empty_body()
    test_restore_original_nickname()

    redis_delete_pattern("rate:*")

    print_summary()


if __name__ == "__main__":
    main()

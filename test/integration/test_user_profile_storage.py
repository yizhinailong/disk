#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for user profile and storage endpoints.

Verifies:
  1. GET /api/user/profile returns all expected fields
  2. GET /api/user/storage returns all expected fields
  3. GET /api/user/profile without token returns 401
  4. GET /api/user/storage with malformed token returns 401

Usage:
  uv run test/integration/test_user_profile_storage.py
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

import atexit

atexit.register(cleanup)


def send_authed_request(method, path, token):
    resp = fetch(
        path,
        method=method,
        headers={"Authorization": f"Bearer {token}"},
    )
    return resp.status_code, resp.text


# Test 1: GET /api/user/profile with valid token
def test_get_profile_with_valid_token():
    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if status_code != 200 or not access_token:
        log_fail(f"Login failed: HTTP {status_code}")
        print(body)
        sys.exit(1)

    resp_status, resp_body = send_authed_request(
        "GET", "/api/user/profile", access_token
    )
    resp_code = json_field(resp_body, "code")

    if resp_status != 200 or resp_code != "0":
        log_fail(f"GET /api/user/profile 失败: HTTP {resp_status}, code={resp_code}")
        print(resp_body)
        sys.exit(1)

    required_fields = {
        "id": json_field(resp_body, "data.user.id"),
        "username": json_field(resp_body, "data.user.username"),
        "email": json_field(resp_body, "data.user.email"),
        "file_count": json_field(resp_body, "data.user.file_count"),
        "folder_count": json_field(resp_body, "data.user.folder_count"),
        "storage_quota": json_field(resp_body, "data.user.storage_quota"),
        "storage_used": json_field(resp_body, "data.user.storage_used"),
        "nickname": json_field(resp_body, "data.user.nickname"),
        "created_at": json_field(resp_body, "data.user.created_at"),
        "updated_at": json_field(resp_body, "data.user.updated_at"),
    }

    avatar = json_field(resp_body, "data.user.avatar")

    if all(required_fields.values()):
        log_pass(
            f"GET /api/user/profile 返回所有预期字段 "
            f"(id={required_fields['id']}, username={required_fields['username']}, "
            f"files={required_fields['file_count']}, folders={required_fields['folder_count']}, "
            f"avatar={'set' if avatar else 'null/empty'})"
        )
    else:
        missing = {k: v for k, v in required_fields.items() if not v}
        log_fail(f"GET /api/user/profile 缺少字段: {missing}")
        print(resp_body)
        sys.exit(1)


# Test 2: GET /api/user/storage with valid token
def test_get_storage_with_valid_token():
    status_code, body = send_login_request(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")

    if status_code != 200 or not access_token:
        log_fail(f"Login failed: HTTP {status_code}")
        sys.exit(1)

    resp_status, resp_body = send_authed_request(
        "GET", "/api/user/storage", access_token
    )
    resp_code = json_field(resp_body, "code")

    if resp_status != 200 or resp_code != "0":
        log_fail(f"GET /api/user/storage 失败: HTTP {resp_status}, code={resp_code}")
        print(resp_body)
        sys.exit(1)

    fields = {
        "used": json_field(resp_body, "data.used"),
        "quota": json_field(resp_body, "data.quota"),
        "percentage": json_field(resp_body, "data.percentage"),
        "file_count": json_field(resp_body, "data.file_count"),
        "folder_count": json_field(resp_body, "data.folder_count"),
    }

    if all(fields.values()):
        log_pass(
            f"GET /api/user/storage 返回所有预期字段 "
            f"(used={fields['used']}, quota={fields['quota']}, "
            f"percentage={fields['percentage']}%, files={fields['file_count']}, "
            f"folders={fields['folder_count']})"
        )
    else:
        missing = {k: v for k, v in fields.items() if not v}
        log_fail(f"GET /api/user/storage 缺少字段: {missing}")
        print(resp_body)
        sys.exit(1)


# Test 3: GET /api/user/profile without token returns 401
def test_get_profile_without_token():
    resp = fetch("/api/user/profile", method="GET")

    if resp.status_code == 401:
        log_pass("GET /api/user/profile 无令牌返回 401")
    else:
        log_fail(f"GET /api/user/profile 无令牌期望 401，实际 HTTP {resp.status_code}")
        print(resp.text)
        sys.exit(1)


# Test 4: GET /api/user/storage with malformed token returns 401
def test_get_storage_with_malformed_token():
    resp = fetch(
        "/api/user/storage",
        method="GET",
        headers={"Authorization": "Bearer not.a.valid.jwt.token"},
    )

    if resp.status_code == 401:
        log_pass("GET /api/user/storage 畸形令牌返回 401")
    else:
        log_fail(
            f"GET /api/user/storage 畸形令牌期望 401，实际 HTTP {resp.status_code}"
        )
        print(resp.text)
        sys.exit(1)


def main():
    print("==========================================")
    print("User Profile/Storage Integration Test")
    print("==========================================\n")

    ensure_server()

    test_get_profile_with_valid_token()
    test_get_storage_with_valid_token()
    test_get_profile_without_token()
    test_get_storage_with_malformed_token()

    redis_delete_pattern("rate:*")

    print_summary()


if __name__ == "__main__":
    main()

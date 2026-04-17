#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for login rate limiting.

Prerequisites:
  - Server running on localhost:8080
  - MySQL database configured with seed data
  - Redis configured

Usage:
  uv run test/integration/test_login_rate_limit.py
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
    redis_delete_key,
    redis_delete_pattern,
)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
VALID_ACCOUNT = os.environ.get("VALID_ACCOUNT", "admin")
VALID_PASS = os.environ.get("VALID_PASS", "Admin123")
LOGIN_RATE_KEY = os.environ.get("LOGIN_RATE_KEY", "rate:login:127.0.0.1")

import atexit

atexit.register(cleanup)


def reset_rate_limit_counter():
    redis_delete_key(LOGIN_RATE_KEY)


def assert_user_not_found(context, status_code, body):
    code = json_field(body, "code")
    if status_code == 404 and code == "40100":
        log_pass(context)
    else:
        log_fail(f"{context} (expected 404/40100, got HTTP {status_code} code {code})")
        print(body)
        sys.exit(1)


def send_login_request_with_response(account, password):
    status_code, body = send_login_request(account, password)
    return status_code, body


# Test 1: First 5 attempts allowed through
def test_below_threshold_allows_first_five():
    account = "rate_limit_missing_user_below"
    reset_rate_limit_counter()

    for attempt in range(1, 6):
        status_code, body = send_login_request_with_response(account, "WrongPass123")
        assert_user_not_found(
            f"前 5 次尝试允许通过（第 {attempt} 次）", status_code, body
        )


# Test 2: 6th attempt returns 429
def test_above_threshold_blocks_sixth():
    account = "rate_limit_missing_user_blocked"
    reset_rate_limit_counter()

    for _ in range(5):
        status_code, body = send_login_request_with_response(account, "WrongPass123")
        assert_user_not_found("达到阈值前仍返回业务错误", status_code, body)

    status_code, body = send_login_request_with_response(account, "WrongPass123")

    code = json_field(body, "code")
    message = json_field(body, "message")

    if (
        status_code == 429
        and code == "10005"
        and message == "Too many login attempts, please try again in 5 minutes"
    ):
        log_pass("第 6 次尝试返回相同 429 行为")
    else:
        log_fail("第 6 次尝试未返回预期 429 行为")
        print(body)
        sys.exit(1)


# Test 3: Successful login clears counter
def test_success_clears_counter():
    account = "rate_limit_missing_user_reset"
    reset_rate_limit_counter()

    for _ in range(3):
        status_code, body = send_login_request_with_response(account, "WrongPass123")
        assert_user_not_found("成功登录前的失败计数可累加", status_code, body)

    status_code, body = send_login_request_with_response(VALID_ACCOUNT, VALID_PASS)
    access_token = json_field(body, "data.access_token")
    if status_code == 200 and access_token:
        log_pass("成功登录清除频率限制计数器")
    else:
        log_fail("成功登录未返回访问令牌")
        print(body)
        sys.exit(1)

    status_code, body = send_login_request_with_response(account, "WrongPass123")
    assert_user_not_found("成功登录后立即再次失败不会被 429 阻断", status_code, body)


def main():
    print("==========================================")
    print("Login Rate Limit Integration Test")
    print("==========================================\n")

    ensure_server()

    test_below_threshold_allows_first_five()
    test_above_threshold_blocks_sixth()
    test_success_clears_counter()
    reset_rate_limit_counter()

    print_summary()


if __name__ == "__main__":
    main()

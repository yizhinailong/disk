#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Smoke test: verify UploadRateLimitFilter still returns HTTP 429 / code 10005
when the configured threshold is exceeded.

Prerequisites:
  - Server running on localhost:8080
  - MySQL database configured
  - Redis configured
  - User account for testing

Usage:
  uv run test/integration/test_upload_rate_limit.py
"""

import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_step,
    print_summary,
    save_evidence,
    check_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    redis_delete_pattern,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")


def _configured_upload_rate_limit() -> int:
    """Read upload_rate_limit_per_minute from config.json."""
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    config_path = os.path.join(repo_root, "config.json")
    try:
        with open(config_path, encoding="utf-8") as f:
            config = json.load(f)
        val = int(
            config.get("custom_config", {})
            .get("disk", {})
            .get("upload_rate_limit_per_minute", 60)
        )
        return val if val > 0 else 60
    except Exception:
        return 60


def _current_window_remaining_seconds() -> int:
    """Seconds remaining in the current 60-second fixed window."""
    return 60 - (int(time.time()) % 60)


def _cleanup_upload_rate_keys(user_id_hint: str = "*") -> None:
    """Delete only upload rate-limit keys, not all rate:* keys."""
    redis_delete_pattern(f"rate:upload:{user_id_hint}:*")


def _upload_init_request(token: str, idx: int) -> tuple[int, str]:
    """Send a lightweight upload/init request. Returns (status, body)."""
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        json_body={
            "filename": f"__rate_limit_probe_{os.getpid()}_{idx}.bin",
            "file_size": 1,
            "file_hash": "00000000000000000000000000000000",
            "parent_id": 0,
        },
    )
    return resp.status_code, resp.text


# ─── Test 1: Threshold enforcement ──────────────────────────────────────────


def test_threshold_enforcement(token: str):
    limit = _configured_upload_rate_limit()
    log_info(f"Configured upload rate limit: {limit} req/min")
    log_info(f"Will send {limit + 1} requests to trigger throttling")

    remaining = _current_window_remaining_seconds()
    if remaining < 10:
        wait = remaining + 1
        log_info(f"Only {remaining}s left in current window, waiting {wait}s for fresh window")
        time.sleep(wait)

    _cleanup_upload_rate_keys()

    log_step(f"Sending {limit} requests (should all pass)")
    for i in range(1, limit + 1):
        status, body = _upload_init_request(token, i)
        if status == 429:
            log_fail(f"Request {i}/{limit} returned 429 prematurely (expected non-429)")
            print(body)
            return

    log_pass(f"All {limit} requests passed through (no 429)")

    log_step(f"Sending request #{limit + 1} (should be throttled)")
    status, body = _upload_init_request(token, limit + 1)

    code = json_field(body, "code")
    message = json_field(body, "message")

    if status == 429 and code == "10005":
        log_pass(f"Request #{limit + 1} correctly throttled: HTTP 429, code 10005")
        log_info(f"Message: {message}")
        save_evidence("upload-rate-limit-throttled.json", body)
    else:
        log_fail(
            f"Expected HTTP 429/code 10005, got HTTP {status}/code {code}"
        )
        print(body)

    _cleanup_upload_rate_keys()


# ─── Main ───────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("Upload Rate Limit Smoke Test")
    print("==========================================\n")

    if not check_server():
        sys.exit(1)

    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        sys.exit(1)

    test_threshold_enforcement(token)

    print_summary()


if __name__ == "__main__":
    main()

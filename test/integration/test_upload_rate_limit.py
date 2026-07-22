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
  - PostgreSQL database configured
  - Redis configured
  - User account for testing

Usage:
  uv run test/integration/test_upload_rate_limit.py
"""

import json
import os
import sys
import time
import uuid
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", REPO_ROOT / ".sisyphus/evidence"))
SERVER_LOG_PATH = EVIDENCE_ROOT / "upload-rate-limit-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (
    log_fail,
    log_info,
    log_pass,
    log_step,
    print_summary,
    save_evidence,
    ensure_server,
    cleanup,
    do_login,
    json_field,
    fetch,
    header_value,
    redis_delete_pattern,
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")


def _configured_upload_rate_value(key: str, fallback: int) -> int:
    """Read one positive upload rate-limit value from the active config."""
    config_path = Path(os.environ.get("DISK_CONFIG_FILE", REPO_ROOT / "config.json"))
    if not config_path.is_absolute():
        config_path = REPO_ROOT / config_path
    try:
        with config_path.open(encoding="utf-8") as f:
            config = json.load(f)
        value = int(config.get("custom_config", {}).get("disk", {}).get(key, fallback))
        return value if value > 0 else fallback
    except Exception:
        return fallback


def _current_window_remaining_seconds(window_seconds: int) -> int:
    """Seconds remaining in the configured fixed window."""
    return window_seconds - (int(time.time()) % window_seconds)


def _cleanup_upload_rate_keys(user_id_hint: str = "*") -> None:
    """Delete only upload rate-limit keys, not all rate:* keys."""
    redis_delete_pattern(f"rate:upload:{user_id_hint}:*")


def _upload_init_request(token: str, idx: int, request_id: str = "") -> Any:
    """Send a lightweight upload/init request."""
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
    }
    if request_id:
        headers["X-Request-Id"] = request_id
    return fetch(
        "/api/file/upload/init",
        method="POST",
        headers=headers,
        json_body={
            "filename": f"__rate_limit_probe_{os.getpid()}_{idx}.bin",
            "file_size": 1,
            "file_hash": "00000000000000000000000000000000",
            "parent_id": 0,
        },
    )


def _wait_for_correlated_upload_rate_log(response: Any, request_id: str) -> None:
    """Match the final 429 warning to the response correlation headers."""
    actual_request_id = header_value(response.headers, "X-Request-Id")
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    if actual_request_id != request_id or not instance_id:
        log_fail("Upload rate-limit response is missing its caller correlation")
        return
    log_pass("Upload rate-limit response preserves request and instance correlation")

    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if SERVER_LOG_PATH.is_file():
            for line in SERVER_LOG_PATH.read_text(
                encoding="utf-8",
                errors="replace",
            ).splitlines():
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if (
                    isinstance(record, dict)
                    and record.get("schema_version") == 1
                    and record.get("source") == "application"
                    and record.get("level") == "warning"
                    and record.get("request_id") == request_id
                    and record.get("instance_id") == instance_id
                    and record.get("operation") == "upload_init"
                    and record.get("upload_id") is None
                    and record.get("job_id") is None
                    and record.get("lease_owner") is None
                    and record.get("state_version") is None
                    and "Upload rate limit:" in str(record.get("message", ""))
                ):
                    log_pass("Upload rate-limit warning keeps bounded typed correlation")
                    return
        time.sleep(0.05)

    log_fail("Upload rate-limit warning did not preserve typed request correlation")


def _assert_log_excludes_request_secrets(token: str, final_probe_name: str) -> None:
    """Verify managed API logs exclude credentials and upload metadata."""
    log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
    for value in (TEST_PASS, token, final_probe_name):
        if value and value in log_text:
            log_fail("Upload rate-limit server log contains a request secret or body value")
            return
    log_pass("Upload rate-limit logs exclude password, token, and upload body values")


# ─── Test 1: Threshold enforcement ──────────────────────────────────────────


def test_threshold_enforcement(token: str):
    limit = _configured_upload_rate_value("upload_rate_limit_per_minute", 60)
    window_seconds = _configured_upload_rate_value("upload_rate_limit_window_seconds", 60)
    log_info(f"Configured upload rate limit: {limit} requests/{window_seconds}s")
    log_info(f"Will send {limit + 1} requests to trigger throttling")

    remaining = _current_window_remaining_seconds(window_seconds)
    if remaining < 10:
        wait = remaining + 1
        log_info(f"Only {remaining}s left in current window, waiting {wait}s for fresh window")
        time.sleep(wait)

    _cleanup_upload_rate_keys()

    log_step(f"Sending {limit} requests (should all pass)")
    for i in range(1, limit + 1):
        response = _upload_init_request(token, i)
        if response.status_code == 429:
            log_fail(f"Request {i}/{limit} returned 429 prematurely (expected non-429)")
            print(response.text)
            return

    log_pass(f"All {limit} requests passed through (no 429)")

    log_step(f"Sending request #{limit + 1} (should be throttled)")
    request_id = f"upload-rate-limit-{uuid.uuid4()}"
    response = _upload_init_request(token, limit + 1, request_id)
    status = response.status_code
    body = response.text

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

    rate_headers = {
        name: header_value(response.headers, name)
        for name in (
            "X-RateLimit-Limit",
            "X-RateLimit-Remaining",
            "X-RateLimit-Reset",
            "Retry-After",
        )
    }
    if (
        rate_headers["X-RateLimit-Limit"] == str(limit)
        and rate_headers["X-RateLimit-Remaining"] == "0"
        and all(rate_headers.values())
    ):
        log_pass("Upload rate-limit response preserves all standard headers")
    else:
        log_fail("Upload rate-limit response headers drifted")

    _wait_for_correlated_upload_rate_log(response, request_id)
    _assert_log_excludes_request_secrets(
        token,
        f"__rate_limit_probe_{os.getpid()}_{limit + 1}.bin",
    )

    _cleanup_upload_rate_keys()


# ─── Main ───────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("Upload Rate Limit Smoke Test")
    print("==========================================\n")

    SERVER_LOG_PATH.unlink(missing_ok=True)
    ensure_server()

    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        sys.exit(1)

    test_threshold_enforcement(token)

    print_summary()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration regression test: repeated authenticated browsing must NOT return 429.

Verifies that after removing the global RateLimitFilter, rapid-fire authenticated
requests to the four browse endpoints succeed without rate-limiting:

  1. GET /api/folder/tree   – folder tree (JwtAuthFilter only)
  2. GET /api/file/list     – file listing  (JwtAuthFilter only)
  3. GET /api/trash          – trash listing (JwtAuthFilter only)
  4. GET /api/share          – share listing (JwtAuthFilter only)

Prerequisites:
  - Server running on localhost:8080
  - PostgreSQL database configured with seed data
  - Redis configured
  - User account exists (default: admin / Admin123)

Usage:
  uv run test/integration/test_browse_burst_regression.py
"""

import os
import sys

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
)

import atexit

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")

BURST_COUNT = 20
EVIDENCE_PREFIX = "browse-burst-regression"

# The four browse endpoints to exercise.
# Each entry: (label, path_with_query)
BROWSE_ENDPOINTS = [
    ("folder-tree", "/api/folder/tree?parent_id=0"),
    ("file-list", "/api/file/list?parent_id=0&page_size=50"),
    ("trash-list", "/api/trash"),
    ("share-list", "/api/share"),
]

TOKEN = ""


def _assert_not_rate_limited(
    label: str, idx: int, status_code: int, body: str
) -> bool:
    """Assert response is not 429 and not rate-limit error code 10005.

    Returns True on pass, calls log_fail + sys.exit on failure.
    """
    code = json_field(body, "code")

    if status_code == 429:
        log_fail(
            f"{label} request #{idx}: got HTTP 429 (rate-limited) — REGRESSION"
        )
        print(body)
        sys.exit(1)

    if code == "10005":
        log_fail(
            f"{label} request #{idx}: got code 10005 (rate-limit) — REGRESSION"
        )
        print(body)
        sys.exit(1)

    return True


def _assert_success(label: str, idx: int, status_code: int, body: str) -> bool:
    """Assert response is successful (HTTP 200 + code 0)."""
    code = json_field(body, "code")

    if status_code == 200 and code == "0":
        return True

    # Non-rate-limit failure (e.g., 401 from expired token) — still a problem
    log_fail(
        f"{label} request #{idx}: expected 200/code=0, "
        f"got HTTP {status_code}/code={code}"
    )
    print(body)
    sys.exit(1)


# ─── Test 1: Single-request sanity check for each endpoint ──────────────────


def test_single_request_each_endpoint() -> None:
    """Verify each browse endpoint returns 200/code=0 with a valid token."""
    log_step("Sanity check: one request per endpoint")

    for label, path in BROWSE_ENDPOINTS:
        resp = fetch(
            path,
            headers={"Authorization": f"Bearer {TOKEN}"},
        )
        _assert_not_rate_limited(label, 1, resp.status_code, resp.text)
        _assert_success(label, 1, resp.status_code, resp.text)
        save_evidence(
            f"{EVIDENCE_PREFIX}-single-{label}.json",
            resp.text,
        )
        log_pass(f"Single request to {label}: HTTP {resp.status_code}, code=0")


# ─── Test 2: Burst requests to folder/tree ──────────────────────────────────


def test_burst_folder_tree() -> None:
    _run_burst("folder-tree", "/api/folder/tree?parent_id=0")


# ─── Test 3: Burst requests to file/list ────────────────────────────────────


def test_burst_file_list() -> None:
    _run_burst("file-list", "/api/file/list?parent_id=0&page_size=50")


# ─── Test 4: Burst requests to trash ────────────────────────────────────────


def test_burst_trash() -> None:
    _run_burst("trash-list", "/api/trash")


# ─── Test 5: Burst requests to share ────────────────────────────────────────


def test_burst_share() -> None:
    _run_burst("share-list", "/api/share")


# ─── Burst runner ────────────────────────────────────────────────────────────


def _run_burst(label: str, path: str) -> None:
    """Send BURST_COUNT rapid requests and assert none return 429/code 10005."""
    log_step(f"Burst test: {BURST_COUNT}x {label} ({path})")

    all_ok = True
    sample_body = ""

    for idx in range(1, BURST_COUNT + 1):
        resp = fetch(
            path,
            headers={"Authorization": f"Bearer {TOKEN}"},
        )

        _assert_not_rate_limited(label, idx, resp.status_code, resp.text)
        _assert_success(label, idx, resp.status_code, resp.text)

        # Capture last response for evidence
        sample_body = resp.text

    save_evidence(f"{EVIDENCE_PREFIX}-burst-{label}.json", sample_body)
    log_pass(f"Burst {label}: {BURST_COUNT}/{BURST_COUNT} requests succeeded (no 429)")


# ─── Main ────────────────────────────────────────────────────────────────────


def main() -> None:
    global TOKEN

    print("==========================================")
    print("Browse Burst Regression Integration Test")
    print("==========================================")
    print()
    print(f"Burst count per endpoint: {BURST_COUNT}")
    print()

    check_server() or sys.exit(1)

    token = do_login(TEST_USER, TEST_PASS)
    if not token:
        sys.exit(1)
    TOKEN = token

    print()
    print("==========================================")
    print("Running Browse Burst Regression Tests")
    print("==========================================")
    print()

    test_single_request_each_endpoint()
    test_burst_folder_tree()
    test_burst_file_list()
    test_burst_trash()
    test_burst_share()

    print()
    print_summary()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""
Integration tests for /api/health, /api/logs, and share-browse exemption.

Verifies:
  1. GET /api/health succeeds unauthenticated with valid payload
  2. Health payload shape has expected fields and valid enum values
  3. GET /api/logs requires authentication (401 without token)
  4. GET /api/logs returns success with authentication (200 + code 0)
  5. Share-browse exemption behavior documented (no config changes)

Prerequisites:
  - Server running on localhost:8080
  - MySQL database configured with seed data
  - Redis configured

Usage:
  uv run test/integration/test_health_and_logs.py
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
    save_raw_evidence,
    ensure_server,
    do_login,
    json_field,
    fetch,
)

import atexit

atexit.register(lambda: None)  # No specific cleanup needed


# ─── Test 1: Health check unauthenticated ──────────────────────────────────────


def test_health_unauthenticated():
    log_info("Testing GET /api/health without authentication...")

    resp = fetch("/api/health", method="GET")

    # Health endpoint returns 200 when healthy, 503 when degraded/unhealthy
    if resp.status_code not in (200, 503):
        log_fail(
            f"GET /api/health: expected HTTP 200 or 503, got HTTP {resp.status_code}"
        )
        print(resp.text)
        sys.exit(1)

    log_pass(f"GET /api/health returns HTTP {resp.status_code} (acceptable)")

    code = json_field(resp.text, "code")
    if code != "0":
        log_fail(f"Health response: expected code=0, got code={code}")
        print(resp.text)
        sys.exit(1)

    log_pass("Health response code=0")

    # Verify core fields exist
    overall_status = json_field(resp.text, "data.overall_status")
    version = json_field(resp.text, "data.version")
    components = json_field(resp.text, "data.components")

    if not overall_status or overall_status == "null":
        log_fail("data.overall_status is missing or null")
        print(resp.text)
        sys.exit(1)

    log_pass(f"data.overall_status present: {overall_status}")

    if not version or version == "null":
        log_fail("data.version is missing or null")
        print(resp.text)
        sys.exit(1)

    log_pass(f"data.version present: {version}")

    if not components or components == "null":
        log_fail("data.components is missing or null")
        print(resp.text)
        sys.exit(1)

    log_pass("data.components present")

    # Save evidence
    save_evidence("health-response.json", resp.text)


# ─── Test 2: Health payload shape validation ───────────────────────────────────


def test_health_payload_shape():
    log_info("Testing health payload shape (enum values)...")

    # Re-fetch to get a fresh response
    resp = fetch("/api/health", method="GET")

    # overall_status must be one of: healthy, degraded, unhealthy
    overall_status = json_field(resp.text, "data.overall_status")

    if overall_status not in ("healthy", "degraded", "unhealthy"):
        log_fail(
            f"data.overall_status='{overall_status}' is NOT one of healthy/degraded/unhealthy"
        )
        print(resp.text)
        sys.exit(1)

    log_pass(f"data.overall_status='{overall_status}' is a valid enum value")

    # database status must be one of: healthy, unhealthy
    db_status = json_field(resp.text, "data.components.database.status")

    if db_status not in ("healthy", "unhealthy"):
        log_fail(
            f"data.components.database.status='{db_status}' is NOT healthy/unhealthy"
        )
        print(resp.text)
        sys.exit(1)

    log_pass(f"data.components.database.status='{db_status}' is valid")

    # redis status must be one of: healthy, unhealthy
    redis_status = json_field(resp.text, "data.components.redis.status")

    if redis_status not in ("healthy", "unhealthy"):
        log_fail(
            f"data.components.redis.status='{redis_status}' is NOT healthy/unhealthy"
        )
        print(resp.text)
        sys.exit(1)

    log_pass(f"data.components.redis.status='{redis_status}' is valid")


# ─── Test 3: Logs without auth ─────────────────────────────────────────────────


def test_logs_without_auth():
    log_info("Testing GET /api/logs without authentication...")

    resp = fetch("/api/logs", method="GET")

    if resp.status_code != 401:
        log_fail(
            f"GET /api/logs without token: expected 401, got HTTP {resp.status_code}"
        )
        print(resp.text)
        sys.exit(1)

    log_pass("GET /api/logs without token returns 401")

    # Save evidence
    save_evidence("logs-no-auth-response.json", resp.text)


# ─── Test 4: Logs with auth ────────────────────────────────────────────────────


def test_logs_with_auth():
    log_info("Testing GET /api/logs with authentication...")

    resp = fetch(
        "/api/auth/login",
        method="POST",
        json_body={"account": "admin", "password": "Admin123"},
    )
    access_token = json_field(resp.text, "data.access_token")

    if not access_token or access_token == "null":
        log_fail("Login failed for logs test")
        print(resp.text)
        sys.exit(1)

    resp = fetch(
        "/api/logs",
        method="GET",
        headers={"Authorization": f"Bearer {access_token}"},
    )

    code = json_field(resp.text, "code")

    if resp.status_code != 200 or code != "0":
        log_fail(
            f"GET /api/logs with token: expected HTTP 200 + code 0, got HTTP {resp.status_code} + code={code}"
        )
        print(resp.text)
        sys.exit(1)

    log_pass("GET /api/logs with token returns 200 + code 0")

    # Verify data.items is an array (may be empty)
    items = json_field(resp.text, "data.items")

    if not items or items == "null":
        log_fail("data.items is missing or null")
        print(resp.text)
        sys.exit(1)

    log_pass("data.items is present (may be empty array)")

    # Save evidence
    save_evidence("logs-with-auth-response.json", resp.text)


# ─── Test 5: Share-browse exemption validation ─────────────────────────────────


def test_share_browse_exemption():
    log_info("Testing GET /api/share/browse/<id> without auth (exemption evidence)...")

    resp = fetch("/api/share/browse/nonexistent_share_id", method="GET")

    # Save evidence regardless of outcome — we are documenting behavior
    evidence_path = "share-browse-exemption-evidence.txt"
    evidence_content = f"""HTTP Status: {resp.status_code}

Response Body:
{resp.text}

Interpretation:
"""

    if resp.status_code == 401:
        evidence_content += (
            "The endpoint /api/share/browse/{share_id} is NOT exempted from JWT auth.\n"
            "It returned 401 without authentication, meaning the config.json exemption pattern\n"
            '"^/api/share-browse/.*" does NOT match the actual route "/api/share/browse/{share_id}".\n'
        )
        log_pass(
            f"Share-browse endpoint: HTTP {resp.status_code} (requires auth — exemption pattern may not match)"
        )
    else:
        evidence_content += (
            f"The endpoint /api/share/browse/{{share_id}} IS publicly accessible (HTTP {resp.status_code}).\n"
            "It did NOT return 401, meaning the config.json exemption pattern likely matches.\n"
        )
        log_pass(
            f"Share-browse endpoint: HTTP {resp.status_code} (publicly accessible — exemption active)"
        )

    save_raw_evidence(evidence_path, evidence_content)
    log_info(f"Evidence saved to {evidence_path}")


# ─── Main ──────────────────────────────────────────────────────────────────────


def main():
    print("==========================================")
    print("Health & Logs Integration Tests")
    print("==========================================\n")

    ensure_server()

    test_health_unauthenticated()
    test_health_payload_shape()
    test_logs_without_auth()
    test_logs_with_auth()
    test_share_browse_exemption()

    print_summary()


if __name__ == "__main__":
    main()

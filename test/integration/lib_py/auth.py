# test/integration/lib_py/auth.py
# Authentication and server management helpers.
#
# Mirrors lib/auth.sh behavior exactly.

from __future__ import annotations

import atexit
import json
import os
import subprocess
import time
from typing import Any, IO

from .common import log_fail, log_info, log_pass
from .http import BASE_URL, Response, fetch, json_field

# ─── Server state ──────────────────────────────────────────────────────────────

_server_process: subprocess.Popen | None = None
_managed_server: bool = False
_server_log_handle: IO[str] | None = None


# ─── Server readiness ──────────────────────────────────────────────────────────


def server_ready(base_url: str | None = None) -> bool:
    """Silent check if server responds. Returns True if status in {400, 401, 405}."""
    url = (base_url or BASE_URL) + "/api/auth/login"
    try:
        resp = fetch(url, method="GET", timeout=5)
        return resp.status_code in (400, 401, 405)
    except Exception:
        return False


def check_server(base_url: str | None = None) -> bool:
    """Verbose server check, logs result."""
    url = base_url or BASE_URL
    log_info(f"Checking server at {url}...")
    if server_ready(url):
        log_pass("Server is running")
        return True
    log_fail("Server not responding")
    return False


# ─── Server lifecycle ──────────────────────────────────────────────────────────


def ensure_server(
    base_url: str | None = None,
    server_bin: str | None = None,
) -> None:
    """Borrow a ready server or start and own one. Wait up to 30s."""
    global _server_process, _managed_server, _server_log_handle

    url = base_url or BASE_URL

    if server_ready(url):
        log_info(f"Using existing server at {url}")
        return

    bin_path = server_bin or os.environ.get(
        "SERVER_BIN", "./build/linux-debug-clang/src/disk"
    )
    if not os.path.isfile(bin_path) or not os.access(bin_path, os.X_OK):
        fallback = "./build/linux-debug-clang/disk"
        if os.path.isfile(fallback) and os.access(fallback, os.X_OK):
            bin_path = fallback

    if not os.path.isfile(bin_path) or not os.access(bin_path, os.X_OK):
        log_fail(f"Server binary not found: {bin_path}")
        raise SystemExit(1)

    log_dir = os.environ.get("EVIDENCE_DIR", ".sisyphus/evidence")
    server_log = os.environ.get("SERVER_LOG", os.path.join(log_dir, "server.log"))
    os.makedirs(os.path.dirname(server_log), exist_ok=True)

    log_info(f"Starting server with {bin_path}")

    env = os.environ.copy()
    env["JWT_SECRET"] = env.get(
        "JWT_SECRET", "dev-only-jwt-secret-key-change-in-production-2024"
    )

    _server_log_handle = open(server_log, "w")
    try:
        _server_process = subprocess.Popen(
            [bin_path],
            stdout=_server_log_handle,
            stderr=_server_log_handle,
            env=env,
        )
    except OSError:
        cleanup()
        raise
    _managed_server = True

    for _ in range(30):
        if server_ready(url):
            log_pass("Server started")
            return
        if _server_process.poll() is not None:
            break
        time.sleep(1)

    log_fail("Server did not become ready")
    cleanup()
    if os.path.isfile(server_log):
        with open(server_log) as f:
            print(f.read())
    raise SystemExit(1)


def cleanup() -> None:
    """Stop only the server process started by this Python process."""
    global _server_process, _managed_server, _server_log_handle

    if _managed_server and _server_process is not None:
        try:
            _server_process.terminate()
            _server_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            _server_process.kill()
            _server_process.wait(timeout=5)
        except (OSError, subprocess.SubprocessError):
            pass

    _server_process = None
    _managed_server = False
    if _server_log_handle is not None:
        _server_log_handle.close()
        _server_log_handle = None


atexit.register(cleanup)


# ─── Login helpers ─────────────────────────────────────────────────────────────


def send_login_request(
    account: str,
    password: str,
    base_url: str | None = None,
) -> tuple[int, str]:
    """POST /api/auth/login. Returns (status_code, body_text)."""
    url = (base_url or BASE_URL) + "/api/auth/login"
    resp = fetch(
        url,
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={"account": account, "password": password},
    )
    return resp.status_code, resp.text


def do_login(
    account: str = "admin",
    password: str = "Admin123",
    base_url: str | None = None,
) -> str | None:
    """Full login flow. Returns token string or None on failure."""
    log_info(f"Logging in as {account}...")
    status_code, body = send_login_request(account, password, base_url)

    token = json_field(body, "data.access_token")
    if not token or token == "null":
        log_fail("Login failed")
        print(body)
        return None

    log_pass("Login successful")
    return token

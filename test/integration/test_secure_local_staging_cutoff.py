#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Verify that a secure API cannot start with local upload staging."""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from lib_py.common import save_evidence  # noqa: E402


EXPECTED_ERROR = (
    "Secure mode API requires upload_staging_backend=s3; "
    "local staging creation is disabled"
)


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def port_is_open(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client:
        client.settimeout(0.05)
        return client.connect_ex(("127.0.0.1", port)) == 0


def stop_process(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def main() -> int:
    binary = Path(
        os.environ.get(
            "DISK_SERVER_BIN",
            REPO_ROOT / "build/linux-debug-clang/src/disk",
        )
    ).resolve()
    if not binary.is_file():
        raise FileNotFoundError(f"Disk server binary not found: {binary}")

    port = reserve_port()
    environment = os.environ.copy()
    for name in (
        "DISK_S3_ACCESS_KEY",
        "DISK_S3_SECRET_KEY",
        "DISK_S3_SESSION_TOKEN",
    ):
        environment.pop(name, None)
    environment.update(
        {
            "DISK_CONFIG_FILE": str(REPO_ROOT / "config.json"),
            "DISK_LISTEN_ADDRESS": "127.0.0.1",
            "DISK_LISTEN_PORT": str(port),
            "DISK_PROCESS_ROLE": "api",
            "DISK_INSTANCE_ID": "secure-local-cutoff-api",
            "DISK_STORAGE_BACKEND": "local",
            "DISK_UPLOAD_STAGING_BACKEND": "local",
            "DISK_SECURE_MODE": "true",
            "JWT_SECRET": "secure-local-cutoff-jwt-secret-32-chars",
            "DATABASE_PASSWORD": "secure-local-cutoff-db-password",
            "REDIS_PASSWORD": "secure-local-cutoff-redis-password",
        }
    )

    process = subprocess.Popen(
        [str(binary)],
        cwd=REPO_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    listener_opened = False
    timed_out = False
    try:
        deadline = time.monotonic() + 5
        while process.poll() is None and time.monotonic() < deadline:
            listener_opened = listener_opened or port_is_open(port)
            if listener_opened:
                break
            time.sleep(0.02)
        timed_out = process.poll() is None and not listener_opened
    finally:
        stop_process(process)

    output, _ = process.communicate()
    error_observed = EXPECTED_ERROR in output
    passed = (
        process.returncode == 1
        and error_observed
        and not listener_opened
        and not timed_out
    )
    evidence = {
        "schema_version": 1,
        "scenario": "secure_api_local_staging_cutoff",
        "exit_code": process.returncode,
        "expected_error_observed": error_observed,
        "listener_opened": listener_opened,
        "timed_out": timed_out,
        "passed": passed,
    }
    save_evidence(
        "local-staging-cutoff-summary.json",
        json.dumps(evidence, indent=2, sort_keys=True) + "\n",
    )

    if not passed:
        print(output, file=sys.stderr)
        return 1
    print("Secure API rejected local upload staging before opening its listener.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

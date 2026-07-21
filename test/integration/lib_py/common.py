# test/integration/lib_py/common.py
# Shared test infrastructure: colors, logging, counters, evidence, summary.
#
# Mirrors lib/common.sh behavior exactly.

from __future__ import annotations

import json
import os
import re
import sys
from typing import Any

# ─── Colors ────────────────────────────────────────────────────────────────────

RED = "\033[0;31m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[0;36m"
NC = "\033[0m"

# ─── Test counters ─────────────────────────────────────────────────────────────

tests_passed: int = 0
tests_failed: int = 0
EVIDENCE_DIR: str = os.environ.get("EVIDENCE_DIR", ".sisyphus/evidence")

# ─── Logging ───────────────────────────────────────────────────────────────────


def log_info(msg: str) -> None:
    print(f"{YELLOW}[INFO]{NC} {msg}")


def log_pass(msg: str) -> None:
    global tests_passed
    tests_passed += 1
    print(f"{GREEN}[PASS]{NC} {msg}")


def log_fail(msg: str) -> None:
    global tests_failed
    tests_failed += 1
    print(f"{RED}[FAIL]{NC} {msg}")


def log_step(msg: str) -> None:
    print(f"{CYAN}[STEP]{NC} {msg}")


def log_section(title: str) -> None:
    print()
    print(f"{CYAN}━━━ {title} ━━━{NC}")


# ─── Evidence ──────────────────────────────────────────────────────────────────


REDACTED = "[REDACTED]"
_SENSITIVE_KEYS = frozenset(
    {
        "access_token",
        "refresh_token",
        "share_token",
        "authorization",
        "x_share_token",
        "password",
        "password_hash",
        "passwd",
        "jwt_secret",
        "secret",
        "secret_key",
        "access_key",
        "access_key_id",
        "accesskeyid",
        "secret_access_key",
        "secretaccesskey",
        "session_token",
        "sessiontoken",
        "aws_access_key_id",
        "aws_secret_access_key",
        "aws_session_token",
        "minio_root_user",
        "minio_root_password",
        "disk_s3_access_key",
        "disk_s3_secret_key",
        "disk_s3_session_token",
        "database_password",
        "redis_password",
        "pgpassword",
    }
)
_HEADER_PATTERN = re.compile(
    r"(?im)^([ \t]*(?:authorization|x-share-token)[ \t]*:[ \t]*)([^\r\n]*)"
)
_NAMED_SECRET_PATTERN = re.compile(
    r"(?im)(\b(?:access[_-]token|refresh[_-]token|share[_-]token|password(?:[_-]hash)?|passwd|"
    r"jwt[_-]secret|secret(?:[_-]key)?|secret[_-]access[_-]key|"
    r"access[_-]key(?:[_-]id)?|session[_-]token|"
    r"aws[_-](?:access[_-]key[_-]id|secret[_-]access[_-]key|session[_-]token)|"
    r"minio[_-]root[_-](?:user|password)|"
    r"disk[_-]s3[_-](?:access[_-]key|secret[_-]key|session[_-]token)|"
    r"database[_-]password|redis[_-]password|pgpassword)\b[ \t]*[:=][ \t]*)"
    r"(?:\"[^\"\r\n]*\"|'[^'\r\n]*'|[^\s,;}\]]+)"
)
_BEARER_PATTERN = re.compile(r"(?i)\bbearer[ \t]+[A-Za-z0-9._~+/=-]+")
_JWT_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_-])eyJ[A-Za-z0-9_-]{6,}\.[A-Za-z0-9_-]{6,}\."
    r"[A-Za-z0-9_-]{6,}(?![A-Za-z0-9_-])"
)
_AWS_ACCESS_KEY_PATTERN = re.compile(r"(?<![A-Z0-9])(?:AKIA|ASIA)[A-Z0-9]{16}(?![A-Z0-9])")
_URL_PASSWORD_PATTERN = re.compile(
    r"(?i)(\b[a-z][a-z0-9+.-]*://[^\s/:@]+:)[^\s/@]+(@)"
)


def _normalized_key(key: object) -> str:
    return str(key).casefold().replace("-", "_")


def _redact_text(data: str) -> str:
    data = _HEADER_PATTERN.sub(lambda match: f"{match.group(1)}{REDACTED}", data)
    data = _NAMED_SECRET_PATTERN.sub(lambda match: f"{match.group(1)}{REDACTED}", data)
    data = _BEARER_PATTERN.sub(f"Bearer {REDACTED}", data)
    data = _JWT_PATTERN.sub(REDACTED, data)
    data = _AWS_ACCESS_KEY_PATTERN.sub(REDACTED, data)
    return _URL_PASSWORD_PATTERN.sub(
        lambda match: f"{match.group(1)}{REDACTED}{match.group(2)}",
        data,
    )


def _redact_json(value: Any) -> Any:
    if isinstance(value, dict):
        return {
            key: REDACTED if _normalized_key(key) in _SENSITIVE_KEYS else _redact_json(item)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [_redact_json(item) for item in value]
    if isinstance(value, str):
        return _redact_text(value)
    return value


def redact_sensitive_data(data: str) -> str:
    """Redact replayable credentials from structured or plain-text evidence."""
    try:
        parsed = json.loads(data)
    except (json.JSONDecodeError, TypeError):
        return _redact_text(data)

    trailing_newline = "\n" if data.endswith("\n") else ""
    return json.dumps(_redact_json(parsed), ensure_ascii=False, indent=2) + trailing_newline


def save_evidence(name: str, data: str) -> None:
    os.makedirs(EVIDENCE_DIR, exist_ok=True)
    with open(os.path.join(EVIDENCE_DIR, name), "w", encoding="utf-8") as f:
        f.write(redact_sensitive_data(data))
    log_info(f"Evidence saved: {name}")


def save_raw_evidence(name: str, command_output: str) -> None:
    os.makedirs(EVIDENCE_DIR, exist_ok=True)
    with open(os.path.join(EVIDENCE_DIR, name), "w", encoding="utf-8") as f:
        f.write(command_output)
    log_info(f"Evidence saved: {name}")


# ─── Summary ───────────────────────────────────────────────────────────────────


def print_summary() -> None:
    print()
    print("==========================================")
    print("Test Summary")
    print("==========================================")
    print(f"Passed: {GREEN}{tests_passed}{NC}")
    print(f"Failed: {RED}{tests_failed}{NC}")

    if tests_failed == 0:
        print(f"{GREEN}All tests passed!{NC}")
        sys.exit(0)

    print(f"{RED}Some tests failed.{NC}")
    sys.exit(1)

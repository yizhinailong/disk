# test/integration/lib_py/assert.py
# Test assertion helpers.
#
# Mirrors lib/assert.sh behavior exactly but uses Python json instead of jq.

from __future__ import annotations

import json
from typing import Any

from .common import log_fail, log_pass
from .http import _CaseInsensitiveDict, header_value, json_field


def assert_status(label: str, actual: int, expected: int) -> bool:
    """Compare HTTP status codes. Increments pass/fail counters."""
    if actual == expected:
        log_pass(label)
        return True
    log_fail(f"{label}: expected HTTP {expected}, got HTTP {actual}")
    return False


def assert_json_field(label: str, body: str, field: str, expected: str) -> bool:
    """Parse body JSON, check field value using dot notation.
    Supports array indexing like "data.results.0.status".
    """
    actual = json_field(body, field)
    if actual == expected:
        log_pass(label)
        return True
    log_fail(f"{label}: expected .{field} = '{expected}', got '{actual}'")
    return False


def assert_json_field_numeric_gt(
    label: str, body: str, field: str, min_value: float
) -> bool:
    """Assert JSON field value > min_value."""
    raw = json_field(body, field)
    try:
        actual = float(raw)
    except (ValueError, TypeError):
        actual = 0.0
    if actual > min_value:
        log_pass(label)
        return True
    log_fail(f"{label}: expected .{field} > {min_value}, got '{raw}'")
    return False


def assert_json_array_not_empty(label: str, body: str, field: str) -> bool:
    """Assert JSON array field has length > 0."""
    try:
        data: Any = json.loads(body)
    except Exception:
        log_fail(f"{label}: invalid JSON body")
        return False

    value = data
    for part in field.split("."):
        if isinstance(value, dict) and part in value:
            value = value[part]
        elif isinstance(value, list):
            try:
                value = value[int(part)]
            except (ValueError, IndexError):
                log_fail(f"{label}: path .{field} not found")
                return False
        else:
            log_fail(f"{label}: path .{field} not found")
            return False

    length = len(value) if isinstance(value, list) else 0
    if length > 0:
        log_pass(label)
        return True
    log_fail(f"{label}: expected .{field} array to be non-empty, got length={length}")
    return False


def assert_header_contains(
    label: str,
    headers: dict[str, str],
    header_name: str,
    expected_value: str,
) -> bool:
    """Case-insensitive header check for substring."""
    actual = header_value(headers, header_name)
    if expected_value in actual:
        log_pass(label)
        return True
    log_fail(
        f"{label}: expected {header_name} containing '{expected_value}', got '{actual}'"
    )
    return False

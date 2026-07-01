# test/integration/lib_py/invariants.py
# Assertion helpers for backend safety-net integration tests.

from __future__ import annotations

from pathlib import Path
from typing import Any

from .common import log_fail, log_pass
from .db import query_one, scalar


def assert_db_row_exists(label: str, sql: str, params: Any = None) -> dict[str, Any] | None:
    """Assert that a query returns one row and return it."""
    row = query_one(sql, params)
    if row is not None:
        log_pass(label)
        return row
    log_fail(f"{label}: expected database row to exist")
    return None


def assert_db_row_absent(label: str, sql: str, params: Any = None) -> bool:
    """Assert that a query returns no row."""
    row = query_one(sql, params)
    if row is None:
        log_pass(label)
        return True
    log_fail(f"{label}: expected no database row, got {row}")
    return False


def assert_db_scalar(label: str, sql: str, params: Any, expected: Any) -> bool:
    """Assert that a scalar query equals an expected value."""
    actual = scalar(sql, params)
    if actual == expected:
        log_pass(label)
        return True
    log_fail(f"{label}: expected {expected!r}, got {actual!r}")
    return False


def assert_numeric_delta(label: str, before: int, after: int, expected_delta: int) -> bool:
    """Assert that after - before equals expected_delta."""
    actual_delta = after - before
    if actual_delta == expected_delta:
        log_pass(label)
        return True
    log_fail(
        f"{label}: expected delta {expected_delta}, got {actual_delta} "
        f"(before={before}, after={after})"
    )
    return False


def assert_path_exists(label: str, path: Path) -> bool:
    """Assert that a filesystem path exists."""
    if path.exists():
        log_pass(label)
        return True
    log_fail(f"{label}: expected path to exist: {path}")
    return False


def assert_path_absent(label: str, path: Path) -> bool:
    """Assert that a filesystem path does not exist."""
    if not path.exists():
        log_pass(label)
        return True
    log_fail(f"{label}: expected path to be absent: {path}")
    return False


def assert_equal(label: str, actual: Any, expected: Any) -> bool:
    """Assert that two values are equal."""
    if actual == expected:
        log_pass(label)
        return True
    log_fail(f"{label}: expected {expected!r}, got {actual!r}")
    return False


def assert_true(label: str, condition: bool, detail: str = "condition was false") -> bool:
    """Assert that a condition is true."""
    if condition:
        log_pass(label)
        return True
    log_fail(f"{label}: {detail}")
    return False

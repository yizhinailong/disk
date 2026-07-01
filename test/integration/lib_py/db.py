# test/integration/lib_py/db.py
# PostgreSQL helpers for backend integration tests.

from __future__ import annotations

import json
import os
from collections.abc import Iterable, Sequence
from contextlib import contextmanager
from pathlib import Path
from typing import Any

try:
    import psycopg
    from psycopg.rows import dict_row
except ModuleNotFoundError as exc:  # pragma: no cover - exercised by integration scripts.
    psycopg = None
    dict_row = None
    _IMPORT_ERROR = exc
else:
    _IMPORT_ERROR = None


SqlParams = Sequence[Any] | dict[str, Any] | None


class DatabaseDependencyError(RuntimeError):
    """Raised when the PostgreSQL Python dependency is unavailable."""


class DatabaseQueryError(RuntimeError):
    """Raised when an invariant helper cannot find expected database state."""


def repo_root() -> Path:
    """Return the repository root for integration tests."""
    return Path(__file__).resolve().parents[3]


def load_config() -> dict[str, Any]:
    """Load the repository config.json file."""
    config_path = Path(os.environ.get("DISK_CONFIG", repo_root() / "config.json"))
    with config_path.open(encoding="utf-8") as handle:
        return json.load(handle)


def database_config() -> dict[str, Any]:
    """Resolve PostgreSQL connection settings from environment, then config.json."""
    config = load_config()
    db_clients = config.get("db_clients", [])
    default_db = db_clients[0] if db_clients else {}

    return {
        "host": os.environ.get("PGHOST") or os.environ.get("DB_HOST") or default_db.get("host", "127.0.0.1"),
        "port": int(os.environ.get("PGPORT") or os.environ.get("DB_PORT") or default_db.get("port", 5432)),
        "dbname": os.environ.get("PGDATABASE") or os.environ.get("DB_NAME") or default_db.get("dbname", "disk"),
        "user": os.environ.get("PGUSER") or os.environ.get("DB_USER") or default_db.get("user", "postgres"),
        "password": os.environ.get("PGPASSWORD") or os.environ.get("DB_PASSWORD") or default_db.get("passwd", "postgresql"),
    }


def _require_psycopg() -> None:
    if psycopg is None:
        raise DatabaseDependencyError(
            "psycopg is required for DB-backed integration tests. "
            "Run the script through uv so PEP 723 dependencies are installed."
        ) from _IMPORT_ERROR


@contextmanager
def db_connection():
    """Open a PostgreSQL connection using resolved integration-test settings."""
    _require_psycopg()
    assert psycopg is not None
    assert dict_row is not None

    with psycopg.connect(row_factory=dict_row, **database_config()) as connection:
        yield connection


def query_all(sql: str, params: SqlParams = None) -> list[dict[str, Any]]:
    """Execute a SELECT query and return all rows as dictionaries."""
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cursor.execute(sql, params)
            return [dict(row) for row in cursor.fetchall()]


def query_one(sql: str, params: SqlParams = None) -> dict[str, Any] | None:
    """Execute a SELECT query and return one row as a dictionary, if present."""
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cursor.execute(sql, params)
            row = cursor.fetchone()
            return dict(row) if row is not None else None


def scalar(sql: str, params: SqlParams = None) -> Any:
    """Execute a SELECT query and return the first column of the first row."""
    row = query_one(sql, params)
    if row is None:
        return None
    return next(iter(row.values()))


def execute(sql: str, params: SqlParams = None) -> int:
    """Execute a mutating statement and return the affected row count."""
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cursor.execute(sql, params)
            affected = cursor.rowcount
        connection.commit()
        return affected


def execute_many(sql: str, param_sets: Iterable[Sequence[Any] | dict[str, Any]]) -> None:
    """Execute a mutating statement for multiple parameter sets in one transaction."""
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cursor.executemany(sql, list(param_sets))
        connection.commit()


def require_row(sql: str, params: SqlParams = None, *, label: str = "row") -> dict[str, Any]:
    """Return one row or raise with a descriptive label."""
    row = query_one(sql, params)
    if row is None:
        raise DatabaseQueryError(f"Expected {label} to exist")
    return row


def require_scalar(sql: str, params: SqlParams = None, *, label: str = "value") -> Any:
    """Return one scalar value or raise with a descriptive label."""
    value = scalar(sql, params)
    if value is None:
        raise DatabaseQueryError(f"Expected {label} to exist")
    return value

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Validate the V004 reconciliation migration in isolated PostgreSQL."""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any

import psycopg
from psycopg import sql
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config


REPO_ROOT = Path(__file__).resolve().parents[2]
INIT_SQL = REPO_ROOT / "sql" / "init.sql"
MIGRATOR = REPO_ROOT / "scripts" / "migrate-db.sh"
REVERSAL = REPO_ROOT / "scripts" / "reverse-expand-migration.sh"
FORWARD = REPO_ROOT / "sql" / "migrations" / "V004_storage_reconciliation_forward.sql"
VERSION = "V004_storage_reconciliation"
TEST_READINESS_SHA256 = "2" * 64


def admin_config() -> dict[str, Any]:
    config = database_config()
    config["dbname"] = os.environ.get("PGMAINTENANCE_DB", "postgres")
    return config


def database_env(database_name: str) -> dict[str, str]:
    config = database_config()
    environment = os.environ.copy()
    environment.update(
        {
            "PGHOST": str(config["host"]),
            "PGPORT": str(config["port"]),
            "PGDATABASE": database_name,
            "PGUSER": str(config["user"]),
            "PGPASSWORD": str(config["password"]),
            "DISK_SCHEMA_REVERSAL_CONTEXT": "pre_activation_reversal",
            "DISK_SCHEMA_REVERSAL_APPROVED": "true",
            "DISK_SCHEMA_CHANGE_TICKET": "TEST-V004-REVERSAL",
            "DISK_SCHEMA_READINESS_SHA256": TEST_READINESS_SHA256,
        }
    )
    environment.pop("DISK_DATABASE_URL", None)
    return environment


def create_database(database_name: str) -> None:
    with psycopg.connect(**admin_config(), autocommit=True) as connection:
        connection.execute(sql.SQL("CREATE DATABASE {}").format(sql.Identifier(database_name)))


def drop_database(database_name: str) -> None:
    with psycopg.connect(**admin_config(), autocommit=True) as connection:
        connection.execute(
            "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = %s AND pid <> pg_backend_pid()",
            (database_name,),
        )
        connection.execute(sql.SQL("DROP DATABASE IF EXISTS {}").format(sql.Identifier(database_name)))


def connect(database_name: str) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, autocommit=True, row_factory=dict_row)


def run_command(command: list[str], database_name: str, *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        env=database_env(database_name),
        check=False,
        capture_output=True,
        text=True,
    )
    if check and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def run_sql_file(database_name: str, path: Path, *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return run_command(
        ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(path)],
        database_name,
        check=check,
    )


def run_migrator(database_name: str) -> subprocess.CompletedProcess[str]:
    return run_command([str(MIGRATOR)], database_name)


def scalar(connection: psycopg.Connection[dict[str, Any]], query: str) -> Any:
    row = connection.execute(query).fetchone()
    assert row is not None
    return next(iter(row.values()))


def verify_schema(database_name: str) -> None:
    expected_checksum = hashlib.sha256(FORWARD.read_bytes()).hexdigest()
    with connect(database_name) as connection:
        assert (
            scalar(
                connection,
                "SELECT to_regclass('public.storage_reconciliation_findings')",
            )
            == "storage_reconciliation_findings"
        )
        assert (
            scalar(
                connection,
                "SELECT COUNT(*) FROM pg_indexes WHERE indexname = 'idx_storage_reconciliation_unresolved'",
            )
            == 1
        )
        row = connection.execute("SELECT checksum FROM schema_migrations WHERE version = %s", (VERSION,)).fetchone()
        assert row == {"checksum": expected_checksum}

        connection.execute(
            "INSERT INTO storage_reconciliation_findings "
            "(finding_type, resource_id, severity, resolution_strategy, details) "
            "VALUES ('missing_final_blob', '7', 2, 'manual', '{\"size\": 10}') "
            "ON CONFLICT (finding_type, resource_id) DO UPDATE SET "
            "occurrences = storage_reconciliation_findings.occurrences + 1, "
            "details = EXCLUDED.details, last_seen_at = NOW(), resolved_at = NULL"
        )
        connection.execute(
            "INSERT INTO storage_reconciliation_findings "
            "(finding_type, resource_id, severity, resolution_strategy, details) "
            "VALUES ('missing_final_blob', '7', 2, 'manual', '{\"size\": 11}') "
            "ON CONFLICT (finding_type, resource_id) DO UPDATE SET "
            "occurrences = storage_reconciliation_findings.occurrences + 1, "
            "details = EXCLUDED.details, last_seen_at = NOW(), resolved_at = NULL"
        )
        finding = connection.execute(
            "SELECT occurrences, details->>'size' AS size FROM storage_reconciliation_findings"
        ).fetchone()
        assert finding == {"occurrences": 2, "size": "11"}


def verify_upgrade_and_rollback(database_name: str) -> None:
    run_sql_file(database_name, INIT_SQL)
    with connect(database_name) as connection:
        connection.execute("DROP TABLE storage_reconciliation_findings")
        connection.execute("DELETE FROM schema_migrations WHERE version = %s", (VERSION,))

    migration = run_migrator(database_name)
    assert f"applied {VERSION}" in migration.stdout
    verify_schema(database_name)

    blocked = run_command([str(REVERSAL), VERSION], database_name, check=False)
    assert blocked.returncode != 0
    assert "contains records" in blocked.stderr
    with connect(database_name) as connection:
        assert (
            scalar(
                connection,
                "SELECT to_regclass('public.storage_reconciliation_findings')",
            )
            == "storage_reconciliation_findings"
        )
        connection.execute("DELETE FROM storage_reconciliation_findings")

    run_command([str(REVERSAL), VERSION], database_name)
    with connect(database_name) as connection:
        assert (
            scalar(
                connection,
                "SELECT to_regclass('public.storage_reconciliation_findings')",
            )
            is None
        )
    run_migrator(database_name)
    verify_schema(database_name)


def verify_fresh_install(database_name: str) -> None:
    run_sql_file(database_name, INIT_SQL)
    migration = run_migrator(database_name)
    assert f"{VERSION} already applied" in migration.stdout
    verify_schema(database_name)


def main() -> int:
    suffix = f"{os.getpid()}_{uuid.uuid4().hex[:8]}"
    upgrade_database = f"disk_v004_upgrade_{suffix}"
    fresh_database = f"disk_v004_fresh_{suffix}"
    databases = [upgrade_database, fresh_database]
    try:
        for database_name in databases:
            create_database(database_name)
        verify_upgrade_and_rollback(upgrade_database)
        verify_fresh_install(fresh_database)
    finally:
        for database_name in reversed(databases):
            drop_database(database_name)

    print("V004 reconciliation migration integration: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

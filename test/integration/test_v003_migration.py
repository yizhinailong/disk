#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Validate the V003 expand migration in isolated PostgreSQL databases."""

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
MIGRATOR = REPO_ROOT / "scripts" / "migrate-db.sh"
REVERSAL = REPO_ROOT / "scripts" / "reverse-expand-migration.sh"
FORWARD = REPO_ROOT / "sql" / "migrations" / "V003_distributed_upload_forward.sql"
INIT_SQL = REPO_ROOT / "sql" / "init.sql"
MIGRATION_VERSION = "V003_distributed_upload"
TEST_READINESS_SHA256 = "1" * 64

LEGACY_SCHEMA = """
CREATE TABLE users (
    id BIGSERIAL PRIMARY KEY,
    storage_reserved BIGINT NOT NULL DEFAULT 0
);

CREATE TABLE files (
    id BIGSERIAL PRIMARY KEY
);

CREATE TABLE upload_tasks (
    id VARCHAR(64) NOT NULL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    folder_id BIGINT NOT NULL DEFAULT 0,
    filename VARCHAR(255) NOT NULL,
    file_size BIGINT NOT NULL,
    file_hash CHAR(32) NOT NULL,
    chunk_size INTEGER NOT NULL,
    total_chunks INTEGER NOT NULL,
    reserved_bytes BIGINT NOT NULL DEFAULT 0,
    temp_path VARCHAR(512) NOT NULL,
    status SMALLINT NOT NULL DEFAULT 0,
    expires_at TIMESTAMP NOT NULL,
    finalized_at TIMESTAMP DEFAULT NULL,
    fail_reason VARCHAR(512) DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_upload_tasks_status_expires ON upload_tasks (status, expires_at);
CREATE INDEX idx_upload_tasks_user_status ON upload_tasks (user_id, status);

CREATE TABLE upload_task_chunks (
    task_id VARCHAR(64) NOT NULL REFERENCES upload_tasks (id) ON DELETE CASCADE,
    chunk_index INTEGER NOT NULL,
    uploaded_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (task_id, chunk_index)
);

INSERT INTO users (id, storage_reserved) VALUES (1, 1024);
INSERT INTO upload_tasks (
    id, user_id, filename, file_size, file_hash, chunk_size, total_chunks,
    reserved_bytes, temp_path, expires_at
) VALUES (
    'legacy-upload', 1, 'legacy.bin', 1024,
    'd41d8cd98f00b204e9800998ecf8427e', 1024, 1,
    1024, 'build/temp_uploads/legacy-upload', NOW() + INTERVAL '1 day'
);
INSERT INTO upload_task_chunks (task_id, chunk_index)
VALUES ('legacy-upload', 0);
"""


def admin_config() -> dict[str, Any]:
    config = database_config()
    config["dbname"] = os.environ.get("PGMAINTENANCE_DB", "postgres")
    return config


def database_env(database_name: str) -> dict[str, str]:
    config = database_config()
    env = os.environ.copy()
    env.update(
        {
            "PGHOST": str(config["host"]),
            "PGPORT": str(config["port"]),
            "PGDATABASE": database_name,
            "PGUSER": str(config["user"]),
            "PGPASSWORD": str(config["password"]),
            "DISK_SCHEMA_REVERSAL_CONTEXT": "pre_activation_reversal",
            "DISK_SCHEMA_REVERSAL_APPROVED": "true",
            "DISK_SCHEMA_CHANGE_TICKET": "TEST-V003-REVERSAL",
            "DISK_SCHEMA_READINESS_SHA256": TEST_READINESS_SHA256,
        }
    )
    env.pop("DISK_DATABASE_URL", None)
    return env


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


def run_command(
    command: list[str],
    database_name: str,
    *,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
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


def run_migrator(database_name: str) -> subprocess.CompletedProcess[str]:
    return run_command([str(MIGRATOR)], database_name)


def scalar(connection: psycopg.Connection[dict[str, Any]], statement: str) -> Any:
    row = connection.execute(statement).fetchone()
    if row is None:
        raise AssertionError(f"query returned no row: {statement}")
    return next(iter(row.values()))


def expect_check_violation(connection: psycopg.Connection[dict[str, Any]], statement: str) -> None:
    try:
        connection.execute(statement)
    except psycopg.errors.CheckViolation:
        return
    raise AssertionError(f"statement unexpectedly bypassed a CHECK constraint: {statement}")


def expect_index_plan(connection: psycopg.Connection[dict[str, Any]], statement: str, index_name: str) -> None:
    plan_rows = connection.execute(f"EXPLAIN (COSTS OFF) {statement}").fetchall()
    plan = "\n".join(str(row["QUERY PLAN"]) for row in plan_rows)
    assert index_name in plan, f"expected {index_name} in query plan:\n{plan}"


def verify_upgrade(database_name: str) -> None:
    expected_checksum = hashlib.sha256(FORWARD.read_bytes()).hexdigest()
    with connect(database_name) as connection:
        migration = connection.execute(
            "SELECT checksum FROM schema_migrations WHERE version = %s",
            (MIGRATION_VERSION,),
        ).fetchone()
        assert migration == {"checksum": expected_checksum}

        task = connection.execute(
            "SELECT staging_backend, staging_prefix, state_version, "
            "finalize_attempts, lease_owner, completed_file_id "
            "FROM upload_tasks WHERE id = 'legacy-upload'"
        ).fetchone()
        assert task == {
            "staging_backend": "local",
            "staging_prefix": "staging/legacy-upload",
            "state_version": 0,
            "finalize_attempts": 0,
            "lease_owner": None,
            "completed_file_id": None,
        }

        chunk = connection.execute(
            "SELECT size_bytes, hash_md5, object_key, etag FROM upload_task_chunks WHERE task_id = 'legacy-upload'"
        ).fetchone()
        assert chunk == {
            "size_bytes": None,
            "hash_md5": None,
            "object_key": None,
            "etag": None,
        }

        constraints = {
            row["conname"]
            for row in connection.execute(
                "SELECT conname FROM pg_constraint "
                "WHERE conrelid IN ('upload_tasks'::regclass, "
                "'upload_task_chunks'::regclass, 'storage_jobs'::regclass)"
            ).fetchall()
        }
        assert {
            "ck_upload_tasks_status",
            "ck_upload_tasks_finalizing_lease",
            "ck_upload_tasks_completed_file",
            "ck_upload_task_chunks_size",
            "ck_upload_task_chunks_hash",
            "uk_storage_jobs_dedupe_key",
            "ck_storage_jobs_running_lease",
        } <= constraints

        indexes = {
            row["indexname"]
            for row in connection.execute("SELECT indexname FROM pg_indexes WHERE schemaname = 'public'").fetchall()
        }
        assert {
            "idx_upload_tasks_finalizing_lease",
            "idx_upload_tasks_staging_backend",
            "idx_storage_jobs_ready",
            "idx_storage_jobs_expired_lease",
            "idx_storage_jobs_type_status",
        } <= indexes

        connection.execute("SET enable_seqscan = off")
        expect_index_plan(
            connection,
            "SELECT id FROM upload_tasks "
            "WHERE status = 4 AND lease_expires_at <= NOW() "
            "ORDER BY lease_expires_at LIMIT 1",
            "idx_upload_tasks_finalizing_lease",
        )
        expect_index_plan(
            connection,
            "SELECT id FROM upload_tasks WHERE staging_backend = 's3' AND status = 0",
            "idx_upload_tasks_staging_backend",
        )
        expect_index_plan(
            connection,
            "SELECT id FROM storage_jobs "
            "WHERE status IN (0, 2) AND available_at <= NOW() "
            "ORDER BY available_at, id LIMIT 1",
            "idx_storage_jobs_ready",
        )
        expect_index_plan(
            connection,
            "SELECT id FROM storage_jobs WHERE status = 1 AND locked_until <= NOW() ORDER BY locked_until, id LIMIT 1",
            "idx_storage_jobs_expired_lease",
        )
        expect_index_plan(
            connection,
            "SELECT id FROM storage_jobs WHERE job_type = 'staging_cleanup' AND status = 4",
            "idx_storage_jobs_type_status",
        )
        connection.execute("RESET enable_seqscan")

        connection.execute(
            "INSERT INTO upload_tasks "
            "(id, user_id, filename, file_size, file_hash, chunk_size, "
            "total_chunks, reserved_bytes, temp_path, expires_at) "
            "VALUES ('old-app-write', 1, 'old.bin', 1, "
            "'d41d8cd98f00b204e9800998ecf8427e', 1, 1, 1, "
            "'build/temp_uploads/old-app-write', NOW() + INTERVAL '1 day')"
        )
        connection.execute("INSERT INTO upload_task_chunks (task_id, chunk_index) VALUES ('old-app-write', 0)")
        connection.execute("UPDATE upload_tasks SET status = 2 WHERE id = 'old-app-write'")
        connection.execute("DELETE FROM upload_tasks WHERE id = 'old-app-write'")

        expect_check_violation(
            connection,
            "UPDATE upload_tasks SET status = 6 WHERE id = 'legacy-upload'",
        )
        expect_check_violation(
            connection,
            "UPDATE upload_tasks SET status = 4 WHERE id = 'legacy-upload'",
        )
        connection.execute(
            "UPDATE upload_tasks SET status = 4, lease_owner = 'worker-a', "
            "lease_expires_at = NOW() + INTERVAL '1 minute' "
            "WHERE id = 'legacy-upload'"
        )
        connection.execute(
            "UPDATE upload_tasks SET status = 0, lease_owner = NULL, lease_expires_at = NULL WHERE id = 'legacy-upload'"
        )
        expect_check_violation(
            connection,
            "UPDATE upload_task_chunks SET size_bytes = 0 WHERE task_id = 'legacy-upload'",
        )
        expect_check_violation(
            connection,
            "INSERT INTO storage_jobs "
            "(job_type, aggregate_id, dedupe_key, status) "
            "VALUES ('staging_cleanup', 'legacy-upload', 'invalid-running', 1)",
        )


def verify_legacy_upgrade(database_name: str) -> None:
    with connect(database_name) as connection:
        connection.execute(LEGACY_SCHEMA, prepare=False)

    first = run_migrator(database_name)
    assert f"applied {MIGRATION_VERSION}" in first.stdout
    verify_upgrade(database_name)

    second = run_migrator(database_name)
    assert f"{MIGRATION_VERSION} already applied" in second.stdout
    with connect(database_name) as connection:
        assert (
            scalar(
                connection,
                f"SELECT COUNT(*) FROM schema_migrations WHERE version = '{MIGRATION_VERSION}'",
            )
            == 1
        )

        connection.execute(
            "UPDATE schema_migrations SET checksum = repeat('0', 64) WHERE version = %s",
            (MIGRATION_VERSION,),
        )

    mismatch = run_command([str(MIGRATOR)], database_name, check=False)
    assert mismatch.returncode != 0
    assert f"checksum mismatch for {MIGRATION_VERSION}" in mismatch.stderr

    expected_checksum = hashlib.sha256(FORWARD.read_bytes()).hexdigest()
    with connect(database_name) as connection:
        connection.execute(
            "UPDATE schema_migrations SET checksum = %s WHERE version = %s",
            (expected_checksum, MIGRATION_VERSION),
        )
        connection.execute(
            "INSERT INTO storage_jobs (job_type, aggregate_id, dedupe_key) "
            "VALUES ('staging_cleanup', 'legacy-upload', 'rollback-blocker')"
        )

    blocked = run_command(
        [str(REVERSAL), MIGRATION_VERSION],
        database_name,
        check=False,
    )
    assert blocked.returncode != 0
    assert "rollback blocked" in blocked.stderr

    with connect(database_name) as connection:
        connection.execute("DELETE FROM storage_jobs")

    run_command(
        [str(REVERSAL), MIGRATION_VERSION],
        database_name,
    )
    with connect(database_name) as connection:
        assert scalar(connection, "SELECT to_regclass('public.storage_jobs')") is None
        assert (
            scalar(
                connection,
                "SELECT COUNT(*) FROM information_schema.columns "
                "WHERE table_schema = 'public' AND table_name = 'upload_tasks' "
                "AND column_name = 'state_version'",
            )
            == 0
        )
        assert (
            scalar(
                connection,
                f"SELECT COUNT(*) FROM schema_migrations WHERE version = '{MIGRATION_VERSION}'",
            )
            == 0
        )

    run_migrator(database_name)
    verify_upgrade(database_name)


def verify_fresh_install(database_name: str) -> None:
    run_command(
        ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
        database_name,
    )
    migration = run_migrator(database_name)
    assert f"{MIGRATION_VERSION} already applied" in migration.stdout
    verify_upgrade_fresh(database_name)


def verify_upgrade_fresh(database_name: str) -> None:
    expected_checksum = hashlib.sha256(FORWARD.read_bytes()).hexdigest()
    with connect(database_name) as connection:
        assert scalar(connection, "SELECT to_regclass('public.storage_jobs')") == "storage_jobs"
        row = connection.execute(
            "SELECT checksum FROM schema_migrations WHERE version = %s",
            (MIGRATION_VERSION,),
        ).fetchone()
        assert row == {"checksum": expected_checksum}
        assert (
            scalar(
                connection,
                "SELECT COUNT(*) FROM pg_indexes WHERE indexname = 'idx_storage_jobs_ready'",
            )
            == 1
        )


def main() -> int:
    suffix = f"{os.getpid()}_{uuid.uuid4().hex[:8]}"
    legacy_database = f"disk_v003_legacy_{suffix}"
    fresh_database = f"disk_v003_fresh_{suffix}"
    databases = [legacy_database, fresh_database]

    try:
        for database_name in databases:
            create_database(database_name)
        verify_legacy_upgrade(legacy_database)
        verify_fresh_install(fresh_database)
    finally:
        for database_name in reversed(databases):
            drop_database(database_name)

    print("V003 migration integration: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

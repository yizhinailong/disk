#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Verify that the complete migration manifest preserves representative data."""

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
MIGRATION_DIR = REPO_ROOT / "sql" / "migrations"
MANIFEST = MIGRATION_DIR / "manifest.tsv"
MIGRATOR = REPO_ROOT / "scripts" / "migrate-db.sh"
ROLLBACKS = (
    MIGRATION_DIR / "V004_storage_reconciliation_rollback.sql",
    MIGRATION_DIR / "V003_distributed_upload_rollback.sql",
)

REPRESENTATIVE_DATA = """
TRUNCATE TABLE file_contents, users RESTART IDENTITY CASCADE;

INSERT INTO users (
    id, username, email, password_hash, nickname, avatar,
    storage_quota, storage_used, storage_reserved, status, role,
    login_attempts, locked_until, last_login_at, last_login_ip,
    created_at, updated_at
) VALUES
    (
        101, 'alice', 'alice@example.test', 'alice-password-hash',
        'Alice A', '/avatars/alice.png', 100000, 3072, 1536, 1, 0,
        2, NULL, '2026-01-10 08:30:00', '192.0.2.10',
        '2025-12-01 09:00:00', '2026-01-10 08:30:00'
    ),
    (
        202, 'bob', 'bob@example.test', 'bob-password-hash',
        'Bob B', NULL, 200000, 1024, 0, 2, 1,
        5, '2026-02-01 00:00:00', '2026-01-11 09:45:00', '2001:db8::20',
        '2025-12-02 10:00:00', '2026-01-11 09:45:00'
    );

INSERT INTO file_contents (
    id, hash_md5, hash_sha256, size, storage_path, mime_type,
    ref_count, created_at
) VALUES
    (
        301, 'd41d8cd98f00b204e9800998ecf8427e',
        'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
        1024, 'objects/e3/content-a', 'application/octet-stream', 2,
        '2026-01-01 12:00:00'
    ),
    (
        302, '9e107d9d372bb6826bd81d3542a419d6',
        'd7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592',
        2048, 'objects/d7/content-b', 'application/pdf', 1,
        '2026-01-02 12:00:00'
    );

INSERT INTO folders (
    id, user_id, parent_id, name, path, depth, item_count,
    created_at, updated_at
) VALUES
    (
        401, 101, 0, 'documents', '/documents', 1, 1,
        '2026-01-03 12:00:00', '2026-01-08 12:00:00'
    ),
    (
        402, 202, 0, 'shared-copy', '/shared-copy', 1, 1,
        '2026-01-04 12:00:00', '2026-01-09 12:00:00'
    );

INSERT INTO files (
    id, user_id, content_id, folder_id, name, extension, size,
    mime_type, path, is_favorite, download_count, last_accessed_at,
    created_at, updated_at
) VALUES
    (
        501, 101, 301, 401, 'alpha.bin', 'bin', 1024,
        'application/octet-stream', '/documents/alpha.bin', 1, 7,
        '2026-01-12 10:00:00', '2026-01-05 12:00:00', '2026-01-12 10:00:00'
    ),
    (
        502, 101, 302, 0, 'report.pdf', 'pdf', 2048,
        'application/pdf', '/report.pdf', 0, 3,
        '2026-01-13 10:00:00', '2026-01-06 12:00:00', '2026-01-13 10:00:00'
    ),
    (
        503, 202, 301, 402, 'alpha-copy.bin', 'bin', 1024,
        'application/octet-stream', '/shared-copy/alpha-copy.bin', 0, 1,
        NULL, '2026-01-07 12:00:00', '2026-01-07 12:00:00'
    );

INSERT INTO upload_tasks (
    id, user_id, folder_id, filename, file_size, file_hash,
    chunk_size, total_chunks, reserved_bytes, temp_path, status,
    expires_at, finalized_at, fail_reason, created_at, updated_at
) VALUES
    (
        'upload-in-progress', 101, 401, 'pending.iso', 1536,
        'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 512, 3, 1536,
        'build/temp_uploads/upload-in-progress', 0,
        '2027-01-20 00:00:00', NULL, NULL,
        '2026-01-14 08:00:00', '2026-01-14 08:05:00'
    ),
    (
        'upload-completed', 101, 0, 'alpha.bin', 1024,
        'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb', 1024, 1, 1024,
        'build/temp_uploads/upload-completed', 1,
        '2026-01-15 00:00:00', '2026-01-14 09:10:00', NULL,
        '2026-01-14 09:00:00', '2026-01-14 09:10:00'
    ),
    (
        'upload-cancelled', 202, 402, 'cancelled.zip', 2048,
        'cccccccccccccccccccccccccccccccc', 1024, 2, 2048,
        'build/temp_uploads/upload-cancelled', 2,
        '2026-01-16 00:00:00', '2026-01-14 10:10:00', 'cancelled by user',
        '2026-01-14 10:00:00', '2026-01-14 10:10:00'
    ),
    (
        'upload-expired', 202, 0, 'expired.tar', 512,
        'dddddddddddddddddddddddddddddddd', 512, 1, 512,
        'build/temp_uploads/upload-expired', 3,
        '2026-01-13 00:00:00', '2026-01-14 11:10:00', 'upload expired',
        '2026-01-13 11:00:00', '2026-01-14 11:10:00'
    );

INSERT INTO upload_task_chunks (task_id, chunk_index, uploaded_at) VALUES
    ('upload-in-progress', 0, '2026-01-14 08:01:00'),
    ('upload-in-progress', 1, '2026-01-14 08:02:00'),
    ('upload-completed', 0, '2026-01-14 09:05:00'),
    ('upload-cancelled', 0, '2026-01-14 10:05:00'),
    ('upload-expired', 0, '2026-01-13 11:05:00');
"""

LEGACY_UPLOAD_COLUMNS = """
    id, user_id, folder_id, filename, file_size, file_hash,
    chunk_size, total_chunks, reserved_bytes, temp_path, status,
    expires_at, finalized_at, fail_reason, created_at
"""


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
            "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
            "WHERE datname = %s AND pid <> pg_backend_pid()",
            (database_name,),
        )
        connection.execute(sql.SQL("DROP DATABASE IF EXISTS {}").format(sql.Identifier(database_name)))


def connect(database_name: str) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, autocommit=True, row_factory=dict_row)


def run_command(command: list[str], database_name: str) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        env=database_env(database_name),
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def run_sql_file(database_name: str, path: Path) -> subprocess.CompletedProcess[str]:
    return run_command(
        ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(path)],
        database_name,
    )


def run_migrator(database_name: str) -> subprocess.CompletedProcess[str]:
    return run_command([str(MIGRATOR)], database_name)


def manifest_entries() -> list[tuple[str, str, str]]:
    entries: list[tuple[str, str, str]] = []
    for raw_line in MANIFEST.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        version, filename = line.split("\t")
        checksum = hashlib.sha256((MIGRATION_DIR / filename).read_bytes()).hexdigest()
        entries.append((version, filename, checksum))
    return entries


def fetch_rows(
    connection: psycopg.Connection[dict[str, Any]], statement: str
) -> list[dict[str, Any]]:
    return [dict(row) for row in connection.execute(statement).fetchall()]


def scalar(connection: psycopg.Connection[dict[str, Any]], statement: str) -> Any:
    row = connection.execute(statement).fetchone()
    assert row is not None, f"query returned no row: {statement}"
    return next(iter(row.values()))


def business_snapshot(
    connection: psycopg.Connection[dict[str, Any]],
) -> dict[str, list[dict[str, Any]]]:
    return {
        "users": fetch_rows(connection, "SELECT * FROM users ORDER BY id"),
        "file_contents": fetch_rows(connection, "SELECT * FROM file_contents ORDER BY id"),
        "folders": fetch_rows(connection, "SELECT * FROM folders ORDER BY id"),
        "files": fetch_rows(connection, "SELECT * FROM files ORDER BY id"),
        "upload_tasks": fetch_rows(
            connection,
            f"SELECT {LEGACY_UPLOAD_COLUMNS} FROM upload_tasks ORDER BY id",
        ),
        "upload_task_chunks": fetch_rows(
            connection,
            "SELECT task_id, chunk_index, uploaded_at "
            "FROM upload_task_chunks ORDER BY task_id, chunk_index",
        ),
        "quota": fetch_rows(
            connection,
            "SELECT u.id, u.storage_quota, u.storage_used, u.storage_reserved, "
            "COALESCE((SELECT SUM(f.size) FROM files f WHERE f.user_id = u.id), 0) "
            "AS active_file_bytes, "
            "COALESCE((SELECT SUM(t.reserved_bytes) FROM upload_tasks t "
            "WHERE t.user_id = u.id AND t.status = 0), 0) AS active_reserved_bytes "
            "FROM users u ORDER BY u.id",
        ),
        "content_references": fetch_rows(
            connection,
            "SELECT c.id, c.ref_count, COUNT(f.id) AS actual_ref_count "
            "FROM file_contents c LEFT JOIN files f ON f.content_id = c.id "
            "GROUP BY c.id, c.ref_count ORDER BY c.id",
        ),
    }


def complete_snapshot(
    connection: psycopg.Connection[dict[str, Any]],
) -> dict[str, list[dict[str, Any]]]:
    tables = (
        "users",
        "file_contents",
        "folders",
        "files",
        "upload_tasks",
        "upload_task_chunks",
        "storage_jobs",
        "storage_reconciliation_findings",
        "schema_migrations",
    )
    return {
        table: fetch_rows(connection, f"SELECT * FROM {table} ORDER BY 1")
        for table in tables
    }


def verify_business_invariants(snapshot: dict[str, list[dict[str, Any]]]) -> None:
    assert len(snapshot["users"]) == 2
    assert len(snapshot["files"]) == 3
    assert len(snapshot["upload_tasks"]) == 4
    assert len(snapshot["upload_task_chunks"]) == 5
    assert snapshot["quota"] == [
        {
            "id": 101,
            "storage_quota": 100000,
            "storage_used": 3072,
            "storage_reserved": 1536,
            "active_file_bytes": 3072,
            "active_reserved_bytes": 1536,
        },
        {
            "id": 202,
            "storage_quota": 200000,
            "storage_used": 1024,
            "storage_reserved": 0,
            "active_file_bytes": 1024,
            "active_reserved_bytes": 0,
        },
    ]
    assert snapshot["content_references"] == [
        {"id": 301, "ref_count": 2, "actual_ref_count": 2},
        {"id": 302, "ref_count": 1, "actual_ref_count": 1},
    ]


def prepare_v002_baseline(database_name: str) -> None:
    run_sql_file(database_name, INIT_SQL)
    for rollback in ROLLBACKS:
        run_sql_file(database_name, rollback)

    with connect(database_name) as connection:
        assert scalar(connection, "SELECT COUNT(*) FROM schema_migrations") == 0
        connection.execute(REPRESENTATIVE_DATA, prepare=False)


def verify_complete_upgrade(database_name: str) -> None:
    prepare_v002_baseline(database_name)

    with connect(database_name) as connection:
        before = business_snapshot(connection)
        verify_business_invariants(before)
        before_updated_at = {
            row["id"]: row["updated_at"]
            for row in fetch_rows(
                connection,
                "SELECT id, updated_at FROM upload_tasks ORDER BY id",
            )
        }

    entries = manifest_entries()
    first = run_migrator(database_name)
    apply_positions = []
    for version, _, _ in entries:
        marker = f"migrate-db: applied {version}"
        assert marker in first.stdout
        apply_positions.append(first.stdout.index(marker))
    assert apply_positions == sorted(apply_positions)

    with connect(database_name) as connection:
        after = business_snapshot(connection)
        assert after == before
        verify_business_invariants(after)

        after_updated_at = {
            row["id"]: row["updated_at"]
            for row in fetch_rows(
                connection,
                "SELECT id, updated_at FROM upload_tasks ORDER BY id",
            )
        }
        assert after_updated_at.keys() == before_updated_at.keys()
        assert all(
            after_updated_at[task_id] >= before_updated_at[task_id]
            for task_id in before_updated_at
        )

        task_metadata = fetch_rows(
            connection,
            "SELECT id, staging_backend, staging_prefix, state_version, "
            "lease_owner, lease_expires_at, finalize_attempts, "
            "last_error_code, last_error_at, completed_file_id "
            "FROM upload_tasks ORDER BY id",
        )
        expected_task_metadata = [
            {
                "id": task["id"],
                "staging_backend": "local",
                "staging_prefix": f"staging/{task['id']}",
                "state_version": 0,
                "lease_owner": None,
                "lease_expires_at": None,
                "finalize_attempts": 0,
                "last_error_code": None,
                "last_error_at": None,
                "completed_file_id": None,
            }
            for task in before["upload_tasks"]
        ]
        assert task_metadata == expected_task_metadata

        chunk_metadata = fetch_rows(
            connection,
            "SELECT task_id, chunk_index, size_bytes, hash_md5, object_key, etag "
            "FROM upload_task_chunks ORDER BY task_id, chunk_index",
        )
        assert all(
            row["size_bytes"] is None
            and row["hash_md5"] is None
            and row["object_key"] is None
            and row["etag"] is None
            for row in chunk_metadata
        )
        assert scalar(connection, "SELECT COUNT(*) FROM storage_jobs") == 0
        assert scalar(
            connection,
            "SELECT COUNT(*) FROM storage_reconciliation_findings",
        ) == 0

        ledger = fetch_rows(
            connection,
            "SELECT version, checksum FROM schema_migrations ORDER BY applied_at, version",
        )
        assert ledger == [
            {"version": version, "checksum": checksum}
            for version, _, checksum in entries
        ]
        first_complete_snapshot = complete_snapshot(connection)

    second = run_migrator(database_name)
    for version, _, _ in entries:
        assert f"migrate-db: {version} already applied" in second.stdout

    with connect(database_name) as connection:
        assert complete_snapshot(connection) == first_complete_snapshot


def main() -> int:
    suffix = f"{os.getpid()}_{uuid.uuid4().hex[:8]}"
    database_name = f"disk_full_upgrade_{suffix}"
    try:
        create_database(database_name)
        verify_complete_upgrade(database_name)
    finally:
        drop_database(database_name)

    print("Full schema upgrade integration: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

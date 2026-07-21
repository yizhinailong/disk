#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Restore a coordinated backup set and run the persisted reconciliation gate."""

from __future__ import annotations

import hashlib
import json
import shutil
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_expand_mixed_version import (
    INIT_SQL,
    ManagedServer,
    allocate_ports,
    connect,
    create_database,
    drop_database,
    require,
    resolve_current_binary,
    run_database_command,
    server_config,
)


JOB_SUCCEEDED = 3
JOB_DEAD_LETTER = 4
SCOPES = ("contents", "users", "staging", "final")
PAGE_LIMIT = 1


def digest_bytes(payload: bytes, algorithm: str) -> str:
    hasher = hashlib.new(algorithm)
    hasher.update(payload)
    return hasher.hexdigest()


def digest_file(path: Path, algorithm: str) -> str:
    hasher = hashlib.new(algorithm)
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            hasher.update(block)
    return hasher.hexdigest()


def make_blob(final_root: Path, payload: bytes) -> dict[str, Any]:
    sha256 = digest_bytes(payload, "sha256")
    path = final_root / "sha256" / sha256[:2] / f"{sha256}.bin"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return {
        "payload": payload,
        "path": path,
        "size": len(payload),
        "md5": digest_bytes(payload, "md5"),
        "sha256": sha256,
    }


def seed_source_database(
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> dict[str, Any]:
    final_root.mkdir(parents=True, exist_ok=True)
    staging_root.mkdir(parents=True, exist_ok=True)
    blobs = [
        make_blob(final_root, b"backup-restore-alpha-content\n"),
        make_blob(final_root, b"backup-restore-bravo-content-with-more-bytes\n"),
        make_blob(final_root, b"backup-restore-charlie-content\n"),
    ]
    user_one_used = (2 * blobs[0]["size"]) + blobs[1]["size"]
    user_two_used = blobs[2]["size"]
    user_one_reserved = 37
    user_two_reserved = 53

    with connect(database_name) as connection, connection.transaction():
        user_one = connection.execute(
            """
            INSERT INTO users
                (username, email, password_hash, storage_used, storage_reserved)
            VALUES ('restore_user_one', 'restore-one@example.test', 'hash', %s, %s)
            RETURNING id
            """,
            (user_one_used, user_one_reserved),
        ).fetchone()
        user_two = connection.execute(
            """
            INSERT INTO users
                (username, email, password_hash, storage_used, storage_reserved)
            VALUES ('restore_user_two', 'restore-two@example.test', 'hash', %s, %s)
            RETURNING id
            """,
            (user_two_used, user_two_reserved),
        ).fetchone()
        require(
            user_one is not None and user_two is not None,
            "failed to seed restore users",
        )

        for blob, ref_count in zip(blobs, (2, 1, 1), strict=True):
            row = connection.execute(
                """
                INSERT INTO file_contents
                    (hash_md5, hash_sha256, size, storage_path, mime_type, ref_count)
                VALUES (%s, %s, %s, %s, 'application/octet-stream', %s)
                RETURNING id
                """,
                (
                    blob["md5"],
                    blob["sha256"],
                    blob["size"],
                    str(blob["path"]),
                    ref_count,
                ),
            ).fetchone()
            require(row is not None, "failed to seed restore content")
            blob["content_id"] = int(row["id"])

        file_specs = (
            (int(user_one["id"]), blobs[0], "alpha.bin"),
            (int(user_one["id"]), blobs[1], "bravo.bin"),
            (int(user_two["id"]), blobs[2], "charlie.bin"),
        )
        for user_id, blob, name in file_specs:
            row = connection.execute(
                """
                INSERT INTO files
                    (user_id, content_id, name, extension, size, mime_type, path)
                VALUES (%s, %s, %s, 'bin', %s, 'application/octet-stream', '/')
                RETURNING id
                """,
                (user_id, blob["content_id"], name, blob["size"]),
            ).fetchone()
            require(row is not None, "failed to seed restore file")
            blob["file_id"] = int(row["id"])

        connection.execute(
            """
            INSERT INTO trash
                (user_id, item_type, item_id, item_name, item_size, content_id,
                 original_path, expires_at)
            VALUES (%s, 'file', 900001, 'alpha-deleted.bin', %s, %s, '/',
                    NOW() + INTERVAL '30 days')
            """,
            (int(user_one["id"]), blobs[0]["size"], blobs[0]["content_id"]),
        )

        upload_specs = (
            ("restore-upload-one", int(user_one["id"]), user_one_reserved),
            ("restore-upload-two", int(user_two["id"]), user_two_reserved),
        )
        for upload_id, user_id, reserved_bytes in upload_specs:
            connection.execute(
                """
                INSERT INTO upload_tasks
                    (id, user_id, filename, file_size, file_hash, chunk_size,
                     total_chunks, reserved_bytes, temp_path, staging_backend,
                     staging_prefix, status, expires_at)
                VALUES (%s, %s, %s, %s, %s, 1048576, 1, %s, %s,
                        'local', %s, 0, NOW() + INTERVAL '1 day')
                """,
                (
                    upload_id,
                    user_id,
                    f"{upload_id}.bin",
                    reserved_bytes,
                    digest_bytes(upload_id.encode(), "md5"),
                    reserved_bytes,
                    str(staging_root / upload_id),
                    f"staging/{upload_id}",
                ),
            )

    return {
        "blobs": blobs,
        "user_one_id": int(user_one["id"]),
        "user_two_id": int(user_two["id"]),
        "user_one_used": user_one_used,
        "user_two_used": user_two_used,
        "user_one_reserved": user_one_reserved,
        "user_two_reserved": user_two_reserved,
    }


def business_snapshot(database_name: str) -> dict[str, list[dict[str, Any]]]:
    queries = {
        "schema_migrations": (
            "SELECT version, checksum FROM schema_migrations ORDER BY version"
        ),
        "users": (
            "SELECT id, username, email, storage_quota, storage_used, storage_reserved "
            "FROM users ORDER BY id"
        ),
        "file_contents": (
            "SELECT id, hash_md5, hash_sha256, size, storage_path, ref_count "
            "FROM file_contents ORDER BY id"
        ),
        "files": (
            "SELECT id, user_id, content_id, folder_id, name, size, path "
            "FROM files ORDER BY id"
        ),
        "trash": (
            "SELECT id, user_id, item_type, item_id, item_name, item_size, "
            "content_id, original_path FROM trash ORDER BY id"
        ),
        "upload_tasks": (
            "SELECT id, user_id, file_size, reserved_bytes, staging_backend, "
            "staging_prefix, status FROM upload_tasks ORDER BY id"
        ),
        "upload_task_chunks": (
            "SELECT task_id, chunk_index, size_bytes, hash_md5, object_key, etag "
            "FROM upload_task_chunks ORDER BY task_id, chunk_index"
        ),
    }
    with connect(database_name) as connection:
        return {
            name: [dict(row) for row in connection.execute(query).fetchall()]
            for name, query in queries.items()
        }


def create_recovery_set(
    source_database: str,
    final_root: Path,
    backup_root: Path,
) -> dict[str, Any]:
    backup_root.mkdir(parents=True, exist_ok=False)
    dump_path = backup_root / "disk.dump"
    final_snapshot = backup_root / "final"
    run_database_command(
        [
            "pg_dump",
            "--format=custom",
            "--no-owner",
            "--no-privileges",
            "--file",
            str(dump_path),
        ],
        source_database,
    )
    shutil.copytree(final_root, final_snapshot)
    manifest = {
        "recovery_set_id": f"integration-{uuid.uuid4().hex}",
        "dump_sha256": digest_file(dump_path, "sha256"),
        "storage_root": str(final_root),
        "objects": [
            {
                "locator": str(path),
                "size": path.stat().st_size,
                "sha256": digest_file(path, "sha256"),
            }
            for path in sorted(final_root.rglob("*"))
            if path.is_file()
        ],
    }
    (backup_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest


def restore_recovery_set(
    restored_database: str,
    final_root: Path,
    backup_root: Path,
    manifest: dict[str, Any],
) -> None:
    dump_path = backup_root / "disk.dump"
    require(
        digest_file(dump_path, "sha256") == manifest["dump_sha256"],
        "backup dump checksum changed before restore",
    )
    shutil.rmtree(final_root)
    shutil.copytree(backup_root / "final", final_root)
    restored_objects = [
        {
            "locator": str(path),
            "size": path.stat().st_size,
            "sha256": digest_file(path, "sha256"),
        }
        for path in sorted(final_root.rglob("*"))
        if path.is_file()
    ]
    require(
        restored_objects == manifest["objects"],
        "restored final snapshot differs from recovery-set manifest",
    )
    run_database_command(
        [
            "pg_restore",
            "--exit-on-error",
            "--no-owner",
            "--no-privileges",
            "--dbname",
            restored_database,
            str(dump_path),
        ],
        restored_database,
    )


def create_referenced_staging_inventory(
    database_name: str,
    staging_root: Path,
) -> None:
    chunks = (
        ("restore-upload-one", b"a" * 37),
        ("restore-upload-two", b"b" * 53),
    )
    rows: list[tuple[str, int, int, str, str]] = []
    for upload_id, payload in chunks:
        md5 = digest_bytes(payload, "md5")
        locator = f"{upload_id}/chunks/0-{md5}.part"
        path = staging_root / locator
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        rows.append((upload_id, 0, len(payload), md5, locator))

    with connect(database_name) as connection, connection.transaction():
        for row in rows:
            connection.execute(
                """
                INSERT INTO upload_task_chunks
                    (task_id, chunk_index, size_bytes, hash_md5, object_key)
                VALUES (%s, %s, %s, %s, %s)
                """,
                row,
            )


def verify_clean_invariants(
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> None:
    with connect(database_name) as connection:
        ref_mismatches = connection.execute(
            """
            SELECT content.id
            FROM file_contents AS content
            WHERE content.ref_count <>
                ((SELECT COUNT(*) FROM files WHERE content_id = content.id) +
                 (SELECT COUNT(*) FROM trash
                  WHERE content_id = content.id AND item_type = 'file'))
            """
        ).fetchall()
        quota_mismatches = connection.execute(
            """
            SELECT users.id
            FROM users
            WHERE users.storage_used <>
                    (COALESCE((SELECT SUM(size) FROM files
                               WHERE user_id = users.id), 0) +
                     COALESCE((SELECT SUM(item_size) FROM trash
                               WHERE user_id = users.id), 0))
               OR users.storage_reserved <>
                    COALESCE((SELECT SUM(reserved_bytes) FROM upload_tasks
                              WHERE user_id = users.id AND status IN (0, 4)), 0)
            """
        ).fetchall()
        contents = connection.execute(
            "SELECT storage_path, size, hash_md5, hash_sha256 "
            "FROM file_contents ORDER BY id"
        ).fetchall()
        file_count = int(
            connection.execute("SELECT COUNT(*) AS count FROM files").fetchone()[
                "count"
            ]
        )
        staging_rows = connection.execute(
            """
            SELECT COALESCE(
                       chunk.object_key,
                       task.id || '/' || chunk.chunk_index::text || '.chunk'
                   ) AS locator
            FROM upload_task_chunks AS chunk
            JOIN upload_tasks AS task ON task.id = chunk.task_id
            WHERE task.status IN (0, 4)
            UNION ALL
            SELECT task.id || '.tmp' AS locator
            FROM upload_tasks AS task
            WHERE task.status = 4 AND task.staging_backend = 'local'
            ORDER BY locator
            """
        ).fetchall()

    require(not ref_mismatches, f"ref_count mismatch after restore: {ref_mismatches}")
    require(not quota_mismatches, f"quota mismatch after restore: {quota_mismatches}")
    require(file_count == 3, f"restored file count changed: {file_count}")
    require(len(contents) == 3, f"restored content count changed: {len(contents)}")
    expected_locators: set[str] = set()
    for content in contents:
        path = Path(content["storage_path"])
        expected_locators.add(str(path))
        require(path.is_file(), f"restored final Blob is missing: {path}")
        require(
            path.stat().st_size == content["size"],
            f"restored Blob size changed: {path}",
        )
        require(
            digest_file(path, "md5") == content["hash_md5"],
            f"restored Blob MD5 changed: {path}",
        )
        require(
            digest_file(path, "sha256") == content["hash_sha256"],
            f"restored Blob SHA-256 changed: {path}",
        )
    inventory = {str(path) for path in final_root.rglob("*") if path.is_file()}
    require(inventory == expected_locators, "restored DB/final inventory sets differ")
    expected_staging_locators = {str(row["locator"]) for row in staging_rows}
    staging_inventory = {
        path.relative_to(staging_root).as_posix()
        for path in staging_root.rglob("*")
        if path.is_file()
    }
    require(
        staging_inventory == expected_staging_locators,
        "restored DB/staging inventory sets differ",
    )


def expected_page_counts(
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> dict[str, int]:
    with connect(database_name) as connection:
        content_count = int(
            connection.execute(
                "SELECT COUNT(*) AS count FROM file_contents"
            ).fetchone()["count"]
        )
        user_count = int(
            connection.execute("SELECT COUNT(*) AS count FROM users").fetchone()[
                "count"
            ]
        )
    staging_count = sum(1 for path in staging_root.rglob("*") if path.is_file())
    final_count = sum(1 for path in final_root.rglob("*") if path.is_file())
    return {
        "contents": (content_count // PAGE_LIMIT) + 1,
        "users": (user_count // PAGE_LIMIT) + 1,
        "staging": max(1, (staging_count + PAGE_LIMIT - 1) // PAGE_LIMIT),
        "final": max(1, (final_count + PAGE_LIMIT - 1) // PAGE_LIMIT),
    }


def enqueue_reconciliation(database_name: str, scan_id: str) -> None:
    with connect(database_name) as connection, connection.transaction():
        for scope in SCOPES:
            payload = {
                "scan_id": scan_id,
                "scope": scope,
                "after_id": 0,
                "continuation_token": "",
                "limit": PAGE_LIMIT,
            }
            cursor_digest = digest_bytes(f"{scope}\n0\n".encode(), "sha256")
            dedupe_key = f"periodic:storage-reconcile:{scan_id}:{scope}:{cursor_digest}"
            row = connection.execute(
                """
                INSERT INTO storage_jobs
                    (job_type, aggregate_id, dedupe_key, payload)
                VALUES ('storage_reconcile', %s, %s, %s::jsonb)
                RETURNING id
                """,
                (scan_id, dedupe_key, json.dumps(payload)),
            ).fetchone()
            require(
                row is not None, f"failed to enqueue {scope} restore reconciliation"
            )


def wait_for_reconciliation(
    database_name: str,
    scan_id: str,
    expected_counts: dict[str, int],
    worker: ManagedServer,
) -> list[dict[str, Any]]:
    deadline = time.monotonic() + 45
    last_rows: list[dict[str, Any]] = []
    while time.monotonic() < deadline:
        worker.require_running(f"reconciliation scan {scan_id}")
        with connect(database_name) as connection:
            last_rows = [
                dict(row)
                for row in connection.execute(
                    """
                    SELECT id, status, attempts, last_error, payload
                    FROM storage_jobs
                    WHERE job_type = 'storage_reconcile' AND aggregate_id = %s
                    ORDER BY id
                    """,
                    (scan_id,),
                ).fetchall()
            ]
        dead_letters = [row for row in last_rows if row["status"] == JOB_DEAD_LETTER]
        require(
            not dead_letters,
            f"restore reconciliation entered dead-letter: {dead_letters}",
        )
        rows_by_scope = {
            scope: [row for row in last_rows if row["payload"]["scope"] == scope]
            for scope in SCOPES
        }
        require(
            all(
                len(rows_by_scope[scope]) <= expected_counts[scope] for scope in SCOPES
            ),
            f"restore reconciliation produced unexpected pages: {rows_by_scope}",
        )
        if all(
            len(rows_by_scope[scope]) == expected_counts[scope]
            and all(row["status"] == JOB_SUCCEEDED for row in rows_by_scope[scope])
            for scope in SCOPES
        ):
            return last_rows
        time.sleep(0.2)
    raise AssertionError(
        f"restore reconciliation did not finish: scan_id={scan_id}, rows={last_rows}\n"
        f"{worker.log_tail()}"
    )


def verify_pagination(
    rows: list[dict[str, Any]], expected_counts: dict[str, int]
) -> None:
    for scope in SCOPES:
        payloads = [row["payload"] for row in rows if row["payload"]["scope"] == scope]
        require(
            len(payloads) == expected_counts[scope] and len(payloads) > 1,
            f"{scope} did not exercise all expected reconciliation pages",
        )
        require(payloads[0]["after_id"] == 0, f"{scope} first DB cursor changed")
        require(
            payloads[0]["continuation_token"] == "",
            f"{scope} first object cursor changed",
        )
        require(
            all(payload["limit"] == PAGE_LIMIT for payload in payloads),
            f"{scope} continuation changed its page limit",
        )
        if scope in ("contents", "users"):
            cursors = [int(payload["after_id"]) for payload in payloads]
            require(
                cursors == sorted(set(cursors)),
                f"{scope} database cursor did not advance monotonically: {cursors}",
            )
        else:
            cursors = [payload["continuation_token"] for payload in payloads]
            require(
                len(cursors) == len(set(cursors)) and all(cursors[1:]),
                f"{scope} object cursor did not advance: {cursors}",
            )


def unresolved_findings(database_name: str) -> list[dict[str, Any]]:
    with connect(database_name) as connection:
        return [
            dict(row)
            for row in connection.execute(
                """
                SELECT finding_type, resource_id, resource_locator, details,
                       occurrences, resolved_at
                FROM storage_reconciliation_findings
                WHERE resolved_at IS NULL
                ORDER BY finding_type, resource_id
                """
            ).fetchall()
        ]


def inject_reconciliation_failures(
    database_name: str,
    final_root: Path,
    staging_root: Path,
    fixture: dict[str, Any],
) -> tuple[Path, Path, set[tuple[str, str]]]:
    alpha, bravo, charlie = fixture["blobs"]
    with connect(database_name) as connection, connection.transaction():
        connection.execute(
            "UPDATE file_contents SET ref_count = 9 WHERE id = %s",
            (alpha["content_id"],),
        )
        connection.execute(
            "UPDATE users SET storage_used = storage_used + 11, "
            "storage_reserved = storage_reserved + 13 WHERE id = %s",
            (fixture["user_one_id"],),
        )
    Path(bravo["path"]).unlink()
    Path(charlie["path"]).write_bytes(charlie["payload"] + b"size-mismatch")
    orphan_final_path = final_root / "orphan" / "unreferenced-final.bin"
    orphan_final_path.parent.mkdir(parents=True, exist_ok=True)
    orphan_final_path.write_bytes(b"orphan-final-blob\n")
    orphan_staging_path = staging_root / "orphan" / "unreferenced-staging.chunk"
    orphan_staging_path.parent.mkdir(parents=True, exist_ok=True)
    orphan_staging_path.write_bytes(b"orphan-staging-object\n")
    orphan_staging_locator = orphan_staging_path.relative_to(staging_root).as_posix()
    expected = {
        ("content_ref_count_mismatch", str(alpha["content_id"])),
        ("quota_used_mismatch", str(fixture["user_one_id"])),
        ("quota_reserved_mismatch", str(fixture["user_one_id"])),
        ("missing_final_blob", str(bravo["content_id"])),
        ("final_blob_size_mismatch", str(charlie["content_id"])),
        (
            "orphan_staging_object",
            digest_bytes(orphan_staging_locator.encode(), "sha256"),
        ),
        (
            "orphan_final_blob",
            digest_bytes(str(orphan_final_path).encode(), "sha256"),
        ),
    }
    return orphan_final_path, orphan_staging_path, expected


def repair_reconciliation_failures(
    database_name: str,
    fixture: dict[str, Any],
    orphan_final_path: Path,
    orphan_staging_path: Path,
) -> None:
    alpha, bravo, charlie = fixture["blobs"]
    with connect(database_name) as connection, connection.transaction():
        connection.execute(
            "UPDATE file_contents SET ref_count = 2 WHERE id = %s",
            (alpha["content_id"],),
        )
        connection.execute(
            "UPDATE users SET storage_used = %s, storage_reserved = %s WHERE id = %s",
            (
                fixture["user_one_used"],
                fixture["user_one_reserved"],
                fixture["user_one_id"],
            ),
        )
    Path(bravo["path"]).write_bytes(bravo["payload"])
    Path(charlie["path"]).write_bytes(charlie["payload"])
    orphan_final_path.unlink()
    orphan_staging_path.unlink()


def verify_detected_findings(
    database_name: str,
    expected: set[tuple[str, str]],
) -> None:
    rows = unresolved_findings(database_name)
    observed = {(row["finding_type"], row["resource_id"]) for row in rows}
    require(observed == expected, f"unexpected restore findings: {rows}")
    require(
        all(row["occurrences"] >= 1 and row["resolved_at"] is None for row in rows),
        "detected restore findings have invalid lifecycle fields",
    )


def verify_resolved_history(
    database_name: str,
    expected: set[tuple[str, str]],
) -> None:
    require(
        not unresolved_findings(database_name), "repaired restore still has findings"
    )
    with connect(database_name) as connection:
        rows = connection.execute(
            """
            SELECT finding_type, resource_id, occurrences, resolved_at
            FROM storage_reconciliation_findings
            ORDER BY finding_type, resource_id
            """
        ).fetchall()
    observed = {(row["finding_type"], row["resource_id"]) for row in rows}
    require(observed == expected, f"restore finding history changed: {rows}")
    require(
        all(row["occurrences"] >= 1 and row["resolved_at"] is not None for row in rows),
        "repaired restore findings were not resolved by reconciliation",
    )


def main() -> None:
    current_binary = resolve_current_binary()
    suffix = uuid.uuid4().hex[:12]
    source_database = f"disk_backup_source_{suffix}"
    restored_database = f"disk_backup_restore_{suffix}"
    source_created = False
    restored_created = False
    worker: ManagedServer | None = None

    with tempfile.TemporaryDirectory(prefix="disk-backup-restore-") as temporary:
        temporary_root = Path(temporary)
        final_root = temporary_root / "runtime" / "final"
        staging_root = temporary_root / "runtime" / "staging"
        backup_root = temporary_root / "backup-set"
        try:
            create_database(source_database)
            source_created = True
            run_database_command(
                ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
                source_database,
            )
            fixture = seed_source_database(source_database, final_root, staging_root)
            source_snapshot = business_snapshot(source_database)
            require(
                len(source_snapshot["files"]) == 3,
                "backup source file count changed",
            )
            require(
                len(source_snapshot["file_contents"]) == 3,
                "backup source content count changed",
            )
            require(
                len(source_snapshot["users"]) == 3
                and len(source_snapshot["trash"]) == 1,
                "backup source user or trash count changed",
            )
            require(
                len(source_snapshot["upload_tasks"]) == 2
                and not source_snapshot["upload_task_chunks"],
                "backup source upload fixture changed",
            )
            manifest = create_recovery_set(source_database, final_root, backup_root)

            create_database(restored_database)
            restored_created = True
            restore_recovery_set(restored_database, final_root, backup_root, manifest)
            require(
                business_snapshot(restored_database) == source_snapshot,
                "restored business snapshot differs from backup source",
            )
            create_referenced_staging_inventory(restored_database, staging_root)
            verify_clean_invariants(restored_database, final_root, staging_root)

            clean_scan_id = f"restore-clean-{suffix}"
            clean_counts = expected_page_counts(
                restored_database,
                final_root,
                staging_root,
            )
            enqueue_reconciliation(restored_database, clean_scan_id)
            port = allocate_ports(1)[0]
            worker = ManagedServer(
                name="backup-restore-worker",
                binary=current_binary,
                run_directory=temporary_root / "worker-run",
                config=server_config(
                    restored_database,
                    port,
                    "backup-restore-worker",
                    final_root,
                    staging_root,
                    role="worker",
                ),
                database_name=restored_database,
                port=port,
                readiness_path="/api/health/ready",
                role="worker",
            )
            clean_rows = wait_for_reconciliation(
                restored_database,
                clean_scan_id,
                clean_counts,
                worker,
            )
            verify_pagination(clean_rows, clean_counts)
            require(
                not unresolved_findings(restored_database),
                "clean restored backup produced reconciliation findings",
            )

            (
                orphan_final_path,
                orphan_staging_path,
                expected_findings,
            ) = inject_reconciliation_failures(
                restored_database,
                final_root,
                staging_root,
                fixture,
            )
            failure_scan_id = f"restore-failure-{suffix}"
            failure_counts = expected_page_counts(
                restored_database,
                final_root,
                staging_root,
            )
            enqueue_reconciliation(restored_database, failure_scan_id)
            failure_rows = wait_for_reconciliation(
                restored_database,
                failure_scan_id,
                failure_counts,
                worker,
            )
            verify_pagination(failure_rows, failure_counts)
            verify_detected_findings(restored_database, expected_findings)

            repair_reconciliation_failures(
                restored_database,
                fixture,
                orphan_final_path,
                orphan_staging_path,
            )
            verify_clean_invariants(restored_database, final_root, staging_root)
            time.sleep(0.02)
            repaired_scan_id = f"restore-repaired-{suffix}"
            repaired_counts = expected_page_counts(
                restored_database,
                final_root,
                staging_root,
            )
            enqueue_reconciliation(restored_database, repaired_scan_id)
            repaired_rows = wait_for_reconciliation(
                restored_database,
                repaired_scan_id,
                repaired_counts,
                worker,
            )
            verify_pagination(repaired_rows, repaired_counts)
            verify_resolved_history(restored_database, expected_findings)
            verify_clean_invariants(restored_database, final_root, staging_root)
            worker.require_running("backup restore acceptance verification")
            print(
                "PASS: custom-format backup restore completed full file-count, quota, "
                "ref_count, and bidirectional staging/final-object reconciliation"
            )
        except BaseException:
            if worker is not None:
                print(worker.log_tail(), file=sys.stderr)
            raise
        finally:
            if worker is not None:
                worker.stop()
            if restored_created:
                drop_database(restored_database)
            if source_created:
                drop_database(source_database)


if __name__ == "__main__":
    main()

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify PostgreSQL physical backup integrity and LSN-targeted recovery."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_expand_mixed_version import (  # noqa: E402
    allocate_ports,
    require,
)
from test_postgres_failover_semantics import (  # noqa: E402
    DATABASE_NAME,
    DATABASE_USER,
    PostgresNode,
    wait_for,
)
from test_redis_session_persistence import resolve_executable  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = ROOT / ".sisyphus/evidence/postgres-pitr-recovery-summary.json"
BASELINE_USER = "pitr_baseline"
PRESERVED_USER = "pitr_preserved"
EXCLUDED_USER = "pitr_excluded"


def run_command(
    command: list[str],
    *,
    expected_success: bool,
    timeout_seconds: float = 90,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=timeout_seconds,
        check=False,
    )
    if expected_success:
        require(
            result.returncode == 0,
            f"command failed with {result.returncode}: {result.stderr.strip()[-2000:]}",
        )
    else:
        require(result.returncode != 0, "corrupted backup unexpectedly verified")
    return result


def digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def create_base_backup(node: PostgresNode, destination: Path) -> None:
    pg_basebackup = resolve_executable(("pg_basebackup",))
    run_command(
        [
            str(pg_basebackup),
            "--host=127.0.0.1",
            f"--port={node.port}",
            f"--username={DATABASE_USER}",
            f"--pgdata={destination}",
            "--format=plain",
            "--wal-method=stream",
            "--checkpoint=fast",
            "--manifest-checksums=SHA256",
            "--label=disk-pitr-integration",
            "--no-password",
        ],
        expected_success=True,
    )


def verify_backup(path: Path, *, expected_success: bool) -> None:
    pg_verifybackup = resolve_executable(("pg_verifybackup",))
    run_command(
        [str(pg_verifybackup), "--exit-on-error", str(path)],
        expected_success=expected_success,
    )


def insert_probe_user(node: PostgresNode, username: str) -> str:
    with node.connect(DATABASE_NAME) as connection:
        connection.execute(
            """
            INSERT INTO users (username, email, password_hash, nickname)
            VALUES (%s, %s, '{TEST-HASH}', %s)
            """,
            (username, f"{username}@example.invalid", username),
        )
        row = connection.execute(
            "SELECT pg_current_wal_flush_lsn()::text AS lsn"
        ).fetchone()
    require(row is not None, f"failed to record WAL position for {username}")
    return str(row["lsn"])


def user_exists(node: PostgresNode, username: str) -> bool:
    return (
        node.scalar(
            "SELECT EXISTS (SELECT 1 FROM users WHERE username = %s)",
            (username,),
            database_name=DATABASE_NAME,
        )
        is True
    )


def read_manifest(path: Path, expected_system_identifier: str) -> dict[str, Any]:
    manifest_path = path / "backup_manifest"
    require(manifest_path.is_file(), "physical backup manifest is missing")
    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(isinstance(payload, dict), "physical backup manifest is not an object")
    require(
        str(payload.get("System-Identifier")) == expected_system_identifier,
        "physical backup manifest has the wrong system identifier",
    )
    wal_ranges = payload.get("WAL-Ranges")
    require(
        isinstance(wal_ranges, list) and len(wal_ranges) >= 1,
        "physical backup manifest has no WAL range",
    )
    return payload


def write_evidence(payload: dict[str, Any]) -> None:
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=EVIDENCE_PATH.parent,
        prefix=f".{EVIDENCE_PATH.name}.",
    )
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        os.fchmod(handle.fileno(), 0o600)
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary_name, EVIDENCE_PATH)


def main() -> int:
    temporary: tempfile.TemporaryDirectory[str] | None = None
    source: PostgresNode | None = None
    restored: PostgresNode | None = None

    try:
        source_port, restore_port = allocate_ports(2)
        temporary = tempfile.TemporaryDirectory(prefix="disk-postgres-pitr-")
        temporary_root = Path(temporary.name)
        archive_root = temporary_root / "archive"
        base_backup_root = temporary_root / "base-backup"
        corrupted_backup_root = temporary_root / "corrupted-backup"
        restored_root = temporary_root / "restored"
        archive_root.mkdir()
        require(
            not any(character.isspace() for character in str(temporary_root)),
            "PITR fixture path contains unsupported whitespace",
        )

        source = PostgresNode(
            "pitr-source",
            temporary_root / "source",
            source_port,
            server_settings=(
                "archive_mode=on",
                f"archive_command=cp %p {archive_root}/%f",
                "archive_timeout=1s",
            ),
        )
        source.initialize_primary()
        source.start()
        source.create_database()
        source.load_schema()
        insert_probe_user(source, BASELINE_USER)
        source.execute("CHECKPOINT")

        source_system_identifier = source.system_identifier()
        source_timeline = source.timeline()
        create_base_backup(source, base_backup_root)
        verify_backup(base_backup_root, expected_success=True)
        manifest = read_manifest(base_backup_root, source_system_identifier)
        manifest_sha256 = digest_file(base_backup_root / "backup_manifest")

        shutil.copytree(base_backup_root, corrupted_backup_root)
        with (corrupted_backup_root / "PG_VERSION").open("ab") as handle:
            handle.write(b"\ncorrupted\n")
        verify_backup(corrupted_backup_root, expected_success=False)

        target_lsn = insert_probe_user(source, PRESERVED_USER)
        post_target_lsn = insert_probe_user(source, EXCLUDED_USER)
        require(
            source.scalar(
                "SELECT %s::pg_lsn < %s::pg_lsn",
                (target_lsn, post_target_lsn),
            )
            is True,
            "post-target transaction did not advance WAL",
        )
        require(user_exists(source, EXCLUDED_USER), "source lost the post-target row")
        post_target_wal = str(
            source.scalar(
                "SELECT pg_walfile_name(%s::pg_lsn)",
                (post_target_lsn,),
            )
        )
        source.execute("SELECT pg_switch_wal()")
        wait_for(
            "WAL archive through the post-target transaction",
            lambda: (archive_root / post_target_wal).is_file(),
        )
        wait_for(
            "successful PostgreSQL archiver status",
            lambda: source.scalar(
                "SELECT archived_count > 0 AND failed_count = 0 FROM pg_stat_archiver"
            )
            is True,
        )
        archive_files = sorted(path for path in archive_root.iterdir() if path.is_file())
        require(archive_files, "continuous WAL archive is empty")
        source.stop()

        shutil.copytree(base_backup_root, restored_root)
        (restored_root / "recovery.signal").touch()
        restored = PostgresNode(
            "pitr-restored",
            restored_root,
            restore_port,
            server_settings=(
                f"restore_command=cp {archive_root}/%f %p",
                f"recovery_target_lsn={target_lsn}",
                "recovery_target_inclusive=on",
                "recovery_target_action=promote",
            ),
        )
        recovery_started_at = time.monotonic()
        restored.start()
        wait_for(
            "LSN-targeted recovery promotion",
            lambda: restored.scalar("SELECT NOT pg_is_in_recovery()") is True,
        )
        recovery_seconds = time.monotonic() - recovery_started_at

        require(
            restored.system_identifier() == source_system_identifier,
            "PITR changed the PostgreSQL system identifier",
        )
        require(
            restored.timeline() > source_timeline,
            "PITR promotion did not create a new timeline",
        )
        require(
            restored.scalar(
                "SELECT to_regclass('public.users') IS NOT NULL",
                database_name=DATABASE_NAME,
            )
            is True,
            "PITR lost the application schema",
        )
        require(user_exists(restored, BASELINE_USER), "PITR lost the baseline row")
        require(user_exists(restored, PRESERVED_USER), "PITR lost the target row")
        require(
            not user_exists(restored, EXCLUDED_USER),
            "PITR replayed a transaction after the target LSN",
        )
        replay_lsn = str(restored.scalar("SELECT pg_last_wal_replay_lsn()::text"))
        require(
            restored.scalar(
                "SELECT %s::pg_lsn >= %s::pg_lsn",
                (replay_lsn, target_lsn),
            )
            is True,
            "PITR stopped before the requested target LSN",
        )
        verify_backup(base_backup_root, expected_success=True)

        write_evidence(
            {
                "archive_file_count": len(archive_files),
                "base_backup_manifest_sha256": manifest_sha256,
                "base_backup_reverified": True,
                "corrupted_backup_rejected": True,
                "manifest_system_identifier_matches": True,
                "manifest_wal_range_count": len(manifest["WAL-Ranges"]),
                "passed": True,
                "physical_base_backup_verified": True,
                "post_target_lsn": post_target_lsn,
                "recovery_seconds": round(recovery_seconds, 3),
                "recovery_target_inclusive": True,
                "recovery_target_kind": "lsn",
                "required_wal_archive_restored": True,
                "restored_replay_lsn": replay_lsn,
                "scenario": "postgres_physical_backup_lsn_pitr",
                "schema_version": 1,
                "separate_data_directories": True,
                "shared_database_service_touched": False,
                "source_cluster_stopped_before_restore": True,
                "system_identifier_preserved": True,
                "target_lsn": target_lsn,
                "timeline_advanced": True,
                "transaction_after_target_excluded": True,
                "transaction_before_target_preserved": True,
            }
        )

        restored.stop()
        restored = None
        source = None
        print(
            "PASS: physical backup verification and LSN-targeted PostgreSQL "
            "recovery preserved the intended transaction boundary"
        )
        return 0
    except BaseException:
        for postgres in (source, restored):
            if postgres is not None:
                print(postgres.log_tail(), file=sys.stderr)
        raise
    finally:
        for postgres in (restored, source):
            if postgres is not None:
                postgres.stop()
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())

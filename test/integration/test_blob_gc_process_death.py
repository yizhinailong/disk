#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Kill a Blob GC Worker after deletion and verify lease-expiry takeover."""

from __future__ import annotations

import hashlib
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator, IO

import psycopg
from psycopg import sql
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config


REPO_ROOT = Path(__file__).resolve().parents[2]
INIT_SQL = REPO_ROOT / "sql" / "init.sql"
EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", REPO_ROOT / ".sisyphus/evidence"))
FAULT_MARKER = "Test fault injection paused blob_gc after blob delete"


def admin_config() -> dict[str, Any]:
    config = database_config()
    config["dbname"] = os.environ.get("PGMAINTENANCE_DB", "postgres")
    return config


def connect(database_name: str) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, autocommit=True, row_factory=dict_row)


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
        connection.execute(
            sql.SQL("DROP DATABASE IF EXISTS {}").format(sql.Identifier(database_name))
        )


def initialize_database(database_name: str) -> None:
    config = database_config()
    env = os.environ.copy()
    env.update(
        {
            "PGHOST": str(config["host"]),
            "PGPORT": str(config["port"]),
            "PGDATABASE": database_name,
            "PGUSER": str(config["user"]),
            "PGPASSWORD": str(config["password"]),
        }
    )
    result = subprocess.run(
        ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"init.sql failed ({result.returncode})\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def write_worker_config(
    path: Path,
    database_name: str,
    storage_root: Path,
    staging_root: Path,
) -> None:
    config = json.loads((REPO_ROOT / "config.json").read_text(encoding="utf-8"))
    database = database_config()
    configured_database = config["db_clients"][0]
    configured_database.update(
        {
            "host": database["host"],
            "port": database["port"],
            "dbname": database_name,
            "user": database["user"],
            "passwd": database["password"],
            "connection_number": 4,
        }
    )
    config["listeners"] = [{"address": "127.0.0.1", "port": reserve_port()}]
    config["app"]["upload_path"] = str(path.parent / "drogon-upload")
    disk_config = config["custom_config"]["disk"]
    disk_config.update(
        {
            "process_role": "worker",
            "storage_backend": "local",
            "upload_staging_backend": "local",
            "storage_base_path": str(storage_root),
            "temp_upload_path": str(staging_root),
            "worker_poll_interval_ms": 100,
            "worker_claim_batch_size": 1,
            "worker_concurrency": 1,
            "worker_lease_duration_seconds": 30,
            "worker_drain_timeout_seconds": 1,
        }
    )
    path.write_text(json.dumps(config, indent=2), encoding="utf-8")


def worker_environment(
    config_path: Path,
    database_name: str,
    instance_id: str,
    port: int,
    target_job_id: int | None = None,
) -> dict[str, str]:
    database = database_config()
    env = os.environ.copy()
    for name in (
        "DISK_SECURE_MODE",
        "DISK_TEST_FAULT_INJECTION",
        "DISK_TEST_PAUSE_AFTER_FINALIZE_CLAIM_UPLOAD_ID",
        "DISK_TEST_PAUSE_AFTER_ASSEMBLY_UPLOAD_ID",
        "DISK_TEST_PAUSE_AFTER_FINALIZE_COMMIT_UPLOAD_ID",
        "DISK_TEST_PAUSE_AFTER_BLOB_DELETE_JOB_ID",
        "DATABASE_HOST",
        "DATABASE_PORT",
        "DATABASE_NAME",
        "DATABASE_USER",
        "DATABASE_PASSWORD",
    ):
        env.pop(name, None)
    env.update(
        {
            "JWT_SECRET": env.get("JWT_SECRET")
            or "dev-only-jwt-secret-key-change-in-production-2024",
            "DISK_CONFIG_FILE": str(config_path),
            "DISK_LISTEN_ADDRESS": "127.0.0.1",
            "DISK_LISTEN_PORT": str(port),
            "DISK_PROCESS_ROLE": "worker",
            "DISK_INSTANCE_ID": instance_id,
            "DISK_STORAGE_BACKEND": "local",
            "DISK_UPLOAD_STAGING_BACKEND": "local",
            "DATABASE_HOST": str(database["host"]),
            "DATABASE_PORT": str(database["port"]),
            "DATABASE_NAME": database_name,
            "DATABASE_USER": str(database["user"]),
        }
    )
    if database["password"]:
        env["DATABASE_PASSWORD"] = str(database["password"])
    if target_job_id is not None:
        env["DISK_TEST_FAULT_INJECTION"] = "1"
        env["DISK_TEST_PAUSE_AFTER_BLOB_DELETE_JOB_ID"] = str(target_job_id)
    return env


def read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


@contextmanager
def worker_process(
    *,
    config_path: Path,
    database_name: str,
    instance_id: str,
    log_path: Path,
    target_job_id: int | None = None,
) -> Iterator[subprocess.Popen[bytes]]:
    port = reserve_port()
    log_handle: IO[bytes] = log_path.open("wb")
    process = subprocess.Popen(
        [str(resolve_server_binary())],
        cwd=REPO_ROOT,
        env=worker_environment(
            config_path,
            database_name,
            instance_id,
            port,
            target_job_id,
        ),
        stdout=log_handle,
        stderr=subprocess.STDOUT,
    )
    try:
        yield process
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        log_handle.close()


def resolve_server_binary() -> Path:
    server_bin = Path(
        os.environ.get("SERVER_BIN", REPO_ROOT / "build/linux-debug-clang/src/disk")
    ).resolve()
    if not server_bin.is_file() or not os.access(server_bin, os.X_OK):
        raise RuntimeError(f"Worker binary is unavailable: {server_bin}")
    return server_bin


def wait_until(
    description: str,
    predicate,
    *,
    timeout_seconds: float,
    process: subprocess.Popen[bytes] | None = None,
    log_path: Path | None = None,
):
    deadline = time.monotonic() + timeout_seconds
    last_value = None
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            log = read_log(log_path) if log_path is not None else ""
            raise AssertionError(
                f"{description}: Worker exited with code {process.returncode}\n{log}"
            )
        last_value = predicate()
        if last_value:
            return last_value
        time.sleep(0.05)
    log = read_log(log_path) if log_path is not None else ""
    raise AssertionError(f"Timed out waiting for {description}: last={last_value}\n{log}")


def job_snapshot(database_name: str, job_id: int) -> dict[str, Any]:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT status, attempts, max_attempts, locked_by, locked_until, "
            "locked_until > NOW() AS lease_live, completed_at, last_error, "
            "NOW() AS database_now FROM storage_jobs WHERE id = %s",
            (job_id,),
        ).fetchone()
    if row is None:
        raise AssertionError(f"Blob GC job disappeared: {job_id}")
    return dict(row)


def content_snapshot(database_name: str, content_id: int) -> dict[str, Any] | None:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT id, ref_count, storage_path FROM file_contents WHERE id = %s",
            (content_id,),
        ).fetchone()
    return None if row is None else dict(row)


def finding_snapshot(database_name: str, content_id: int) -> dict[str, Any] | None:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT id, occurrences, resolved_at FROM storage_reconciliation_findings "
            "WHERE finding_type = 'zero_reference_content' AND resource_id = %s",
            (str(content_id),),
        ).fetchone()
    return None if row is None else dict(row)


def create_fixture(
    database_name: str,
    storage_root: Path,
    run_id: str,
) -> tuple[int, int, Path]:
    payload = f"blob-gc-process-death-{run_id}".encode()
    hash_md5 = hashlib.md5(payload).hexdigest()
    hash_sha256 = hashlib.sha256(payload).hexdigest()
    blob_path = storage_root / "sha256" / hash_sha256[:2] / f"{hash_sha256}.bin"
    blob_path.parent.mkdir(parents=True, exist_ok=True)
    blob_path.write_bytes(payload)

    with connect(database_name) as connection:
        content = connection.execute(
            "INSERT INTO file_contents "
            "(hash_md5, hash_sha256, size, storage_path, mime_type, ref_count) "
            "VALUES (%s, %s, %s, %s, 'application/octet-stream', 0) RETURNING id",
            (hash_md5, hash_sha256, len(payload), str(blob_path)),
        ).fetchone()
        if content is None:
            raise AssertionError("Failed to create zero-reference content fixture")
        content_id = int(content["id"])
        connection.execute(
            "INSERT INTO storage_reconciliation_findings "
            "(finding_type, resource_id, resource_locator, severity, "
            " resolution_strategy, details) "
            "VALUES ('zero_reference_content', %s, %s, 1, 'auto_gc', '{}'::jsonb)",
            (str(content_id), str(blob_path)),
        )
        job = connection.execute(
            "INSERT INTO storage_jobs "
            "(job_type, aggregate_id, dedupe_key, payload, max_attempts) "
            "VALUES ('blob_gc', %s, %s, %s::jsonb, 8) RETURNING id",
            (
                str(content_id),
                f"blob-gc:{content_id}",
                json.dumps(
                    {"content_id": content_id, "storage_path": str(blob_path)}
                ),
            ),
        ).fetchone()
    if job is None:
        raise AssertionError("Failed to create Blob GC job fixture")
    return content_id, int(job["id"]), blob_path


def assert_crash_boundary(
    database_name: str,
    content_id: int,
    job_id: int,
    instance_id: str,
    blob_path: Path,
) -> dict[str, Any] | None:
    job = job_snapshot(database_name, job_id)
    if (
        blob_path.exists()
        or int(job["status"]) != 1
        or int(job["attempts"]) != 1
        or job["locked_by"] != instance_id
    ):
        return None
    content = content_snapshot(database_name, content_id)
    finding = finding_snapshot(database_name, content_id)
    if content is None or int(content["ref_count"]) != 0:
        return None
    if finding is None or finding["resolved_at"] is not None:
        return None
    return {"job": job, "content": content, "finding": finding}


def json_ready(value: Any) -> Any:
    return json.loads(json.dumps(value, default=str))


def verify_process_death_takeover(
    database_name: str,
    config_path: Path,
    storage_root: Path,
    run_id: str,
) -> dict[str, Any]:
    content_id, job_id, blob_path = create_fixture(database_name, storage_root, run_id)
    crash_instance = f"blob-gc-crash-{run_id}"
    takeover_instance = f"blob-gc-takeover-{run_id}"
    crash_log = EVIDENCE_ROOT / f"blob-gc-crash-{run_id}.log"
    takeover_log = EVIDENCE_ROOT / f"blob-gc-takeover-{run_id}.log"

    with worker_process(
        config_path=config_path,
        database_name=database_name,
        instance_id=crash_instance,
        log_path=crash_log,
        target_job_id=job_id,
    ) as crash_worker:
        boundary = wait_until(
            "Blob deletion fault boundary",
            lambda: (
                assert_crash_boundary(
                    database_name,
                    content_id,
                    job_id,
                    crash_instance,
                    blob_path,
                )
                if FAULT_MARKER in read_log(crash_log)
                else None
            ),
            timeout_seconds=30,
            process=crash_worker,
            log_path=crash_log,
        )
        if not boundary["job"]["lease_live"]:
            raise AssertionError("Crash Worker lease expired before SIGKILL")
        crash_worker.kill()
        crash_worker.wait(timeout=5)
        if crash_worker.returncode != -signal.SIGKILL:
            raise AssertionError(
                f"Crash Worker did not exit through SIGKILL: {crash_worker.returncode}"
            )

    after_kill = wait_until(
        "database transaction rollback after SIGKILL",
        lambda: assert_crash_boundary(
            database_name,
            content_id,
            job_id,
            crash_instance,
            blob_path,
        ),
        timeout_seconds=10,
    )
    if not after_kill["job"]["lease_live"]:
        raise AssertionError("Blob GC lease was not live after the owner was killed")

    with worker_process(
        config_path=config_path,
        database_name=database_name,
        instance_id=takeover_instance,
        log_path=takeover_log,
    ) as takeover_worker:
        wait_until(
            "takeover Worker initialization",
            lambda: (
                True
                if f"Process initialization completed: instance_id={takeover_instance}, role=worker"
                in read_log(takeover_log)
                else None
            ),
            timeout_seconds=30,
            process=takeover_worker,
            log_path=takeover_log,
        )
        time.sleep(0.5)
        during_live_lease = assert_crash_boundary(
            database_name,
            content_id,
            job_id,
            crash_instance,
            blob_path,
        )
        if during_live_lease is None or not during_live_lease["job"]["lease_live"]:
            raise AssertionError("Takeover Worker changed a live lease")

        final_job = wait_until(
            "Blob GC lease-expiry takeover",
            lambda: (
                snapshot
                if int((snapshot := job_snapshot(database_name, job_id))["status"]) == 3
                else None
            ),
            timeout_seconds=45,
            process=takeover_worker,
            log_path=takeover_log,
        )

    if int(final_job["attempts"]) != 2:
        raise AssertionError(f"Blob GC attempts did not converge to 2: {final_job}")
    if final_job["locked_by"] is not None or final_job["locked_until"] is not None:
        raise AssertionError(f"Succeeded Blob GC retained lease ownership: {final_job}")
    if final_job["completed_at"] is None or final_job["last_error"] is not None:
        raise AssertionError(f"Succeeded Blob GC retained an incomplete outcome: {final_job}")
    takeover_marker = (
        f"job_id={job_id}, job_type=blob_gc, lease_owner={takeover_instance}, "
        "attempts=2, lease_takeover=1"
    )
    if takeover_marker not in read_log(takeover_log):
        raise AssertionError("Takeover Worker log does not identify the expired target lease")
    if blob_path.exists():
        raise AssertionError("Blob reappeared after takeover")
    if content_snapshot(database_name, content_id) is not None:
        raise AssertionError("Takeover did not delete the zero-reference content row")
    final_finding = finding_snapshot(database_name, content_id)
    if final_finding is None or final_finding["resolved_at"] is None:
        raise AssertionError("Takeover did not resolve the zero-reference finding")
    if final_finding["resolved_at"] != final_job["completed_at"]:
        raise AssertionError("Finding and Blob GC completion were not committed together")

    with connect(database_name) as connection:
        dedupe_count = int(
            connection.execute(
                "SELECT COUNT(*) AS count FROM storage_jobs WHERE dedupe_key = %s",
                (f"blob-gc:{content_id}",),
            ).fetchone()["count"]
        )
    if dedupe_count != 1:
        raise AssertionError(f"Blob GC dedupe key has {dedupe_count} rows")

    return json_ready(
        {
            "database": database_name,
            "content_id": content_id,
            "job_id": job_id,
            "crash_returncode": -signal.SIGKILL,
            "after_delete_before_kill": boundary,
            "after_kill": after_kill,
            "during_live_lease": during_live_lease,
            "final_job": final_job,
            "final_finding": final_finding,
            "dedupe_count": dedupe_count,
        }
    )


def main() -> int:
    resolve_server_binary()
    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    run_id = uuid.uuid4().hex[:12]
    database_name = f"disk_blob_gc_death_{run_id}"
    created = False
    try:
        create_database(database_name)
        created = True
        initialize_database(database_name)
        with tempfile.TemporaryDirectory(prefix="disk-blob-gc-death-") as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            config_path = temp_dir / "config.json"
            storage_root = temp_dir / "blobs"
            write_worker_config(
                config_path,
                database_name,
                storage_root,
                temp_dir / "staging",
            )
            evidence = verify_process_death_takeover(
                database_name,
                config_path,
                storage_root,
                run_id,
            )
        evidence_path = EVIDENCE_ROOT / f"blob-gc-process-death-{run_id}.json"
        evidence_path.write_text(json.dumps(evidence, indent=2), encoding="utf-8")
        print(f"Blob GC process-death takeover integration: PASS ({evidence_path})")
        return 0
    finally:
        if created:
            drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

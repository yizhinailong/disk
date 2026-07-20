#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify SIGTERM stops Worker claims and preserves leases for natural takeover."""

from __future__ import annotations

import hashlib
import json
import os
import signal
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any, Callable

import httpx
import psycopg
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config
from test_expand_mixed_version import (
    INIT_SQL,
    ManagedServer,
    allocate_ports,
    connect,
    create_database,
    drop_database,
    require,
    resolve_current_binary,
    run_sql_file,
    server_config,
)


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = ROOT / ".sisyphus/evidence/worker-drain-takeover-summary.json"
WORKER_A = "worker-drain-a"
WORKER_B = "worker-drain-b"
POLL_INTERVAL_MS = 100
LEASE_SECONDS = 30
DRAIN_SECONDS = 2


def wait_until(
    description: str,
    predicate: Callable[[], Any],
    *,
    timeout_seconds: float,
) -> Any:
    deadline = time.monotonic() + timeout_seconds
    last_value: Any = None
    last_error = ""
    while time.monotonic() < deadline:
        try:
            last_value = predicate()
            if last_value:
                return last_value
        except (httpx.HTTPError, psycopg.Error) as error:
            last_error = str(error)
        time.sleep(0.05)
    detail = f", last_error={last_error}" if last_error else ""
    raise AssertionError(
        f"timed out waiting for {description}: last={last_value}{detail}"
    )


def read_log(server: ManagedServer) -> str:
    if server.log_handle is not None:
        server.log_handle.flush()
    return server.log_path.read_text(encoding="utf-8", errors="replace")


def worker_config(
    database_name: str,
    port: int,
    instance_id: str,
    final_root: Path,
    staging_root: Path,
) -> dict[str, Any]:
    config = server_config(
        database_name,
        port,
        instance_id,
        final_root,
        staging_root,
        role="worker",
    )
    disk = config["custom_config"]["disk"]
    disk.update(
        {
            "worker_claiming_enabled": True,
            "worker_poll_interval_ms": POLL_INTERVAL_MS,
            "worker_claim_batch_size": 1,
            "worker_concurrency": 1,
            "worker_lease_duration_seconds": LEASE_SECONDS,
            "worker_drain_timeout_seconds": DRAIN_SECONDS,
        }
    )
    return config


def blocking_connection(database_name: str) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, row_factory=dict_row)


def create_blob_gc_fixture(
    database_name: str,
    final_root: Path,
    label: str,
) -> dict[str, Any]:
    payload = f"worker-drain-{label}".encode()
    hash_md5 = hashlib.md5(payload).hexdigest()
    hash_sha256 = hashlib.sha256(payload).hexdigest()
    blob_path = final_root / "sha256" / hash_sha256[:2] / f"{hash_sha256}.bin"
    blob_path.parent.mkdir(parents=True, exist_ok=True)
    blob_path.write_bytes(payload)

    with connect(database_name) as connection:
        content = connection.execute(
            "INSERT INTO file_contents "
            "(hash_md5, hash_sha256, size, storage_path, mime_type, ref_count) "
            "VALUES (%s, %s, %s, %s, 'application/octet-stream', 0) "
            "RETURNING id",
            (hash_md5, hash_sha256, len(payload), str(blob_path)),
        ).fetchone()
        require(content is not None, f"failed to create {label} content")
        content_id = int(content["id"])
        dedupe_key = f"worker-drain:{label}:{content_id}"
        job = connection.execute(
            "INSERT INTO storage_jobs "
            "(job_type, aggregate_id, dedupe_key, payload, max_attempts) "
            "VALUES ('blob_gc', %s, %s, %s::jsonb, 8) RETURNING id",
            (
                str(content_id),
                dedupe_key,
                json.dumps({"content_id": content_id, "storage_path": str(blob_path)}),
            ),
        ).fetchone()
        require(job is not None, f"failed to create {label} job")
    return {
        "content_id": content_id,
        "job_id": int(job["id"]),
        "dedupe_key": dedupe_key,
        "blob_path": blob_path,
    }


def job_snapshot(database_name: str, job_id: int) -> dict[str, Any]:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT id, status, attempts, max_attempts, locked_by, locked_until, "
            "locked_until > NOW() AS lease_live, completed_at, NOW() AS database_now "
            "FROM storage_jobs WHERE id = %s",
            (job_id,),
        ).fetchone()
    require(row is not None, f"storage job disappeared: {job_id}")
    return dict(row)


def content_count(database_name: str, content_id: int) -> int:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT COUNT(*) AS count FROM file_contents WHERE id = %s",
            (content_id,),
        ).fetchone()
    require(row is not None, "content count returned no row")
    return int(row["count"])


def dedupe_count(database_name: str, dedupe_key: str) -> int:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT COUNT(*) AS count FROM storage_jobs WHERE dedupe_key = %s",
            (dedupe_key,),
        ).fetchone()
    require(row is not None, "dedupe count returned no row")
    return int(row["count"])


def draining_health(server: ManagedServer) -> dict[str, Any] | None:
    response = httpx.get(server.base_url + "/api/health/ready", timeout=1)
    if response.status_code != 503:
        return None
    body = response.json()
    data = body.get("data")
    if not isinstance(data, dict):
        return None
    if (
        data.get("overall_status") != "unhealthy"
        or data.get("instance_id") != WORKER_A
        or data.get("draining") is not True
        or data.get("worker_claiming_enabled") is not True
        or data.get("worker_accepting") is not False
    ):
        return None
    return data


def metric_value(metrics: str, sample: str) -> float:
    prefix = sample + " "
    values = [
        line[len(prefix) :] for line in metrics.splitlines() if line.startswith(prefix)
    ]
    require(len(values) == 1, f"expected one metric sample for {sample}: {values}")
    return float(values[0])


def assert_draining_metrics(server: ManagedServer) -> None:
    response = httpx.get(server.base_url + "/metrics", timeout=1)
    require(response.status_code == 200, "draining Worker metrics are unavailable")
    require(
        metric_value(response.text, "disk_worker_claiming_enabled") == 1,
        "draining Worker lost its configured claiming identity",
    )
    require(
        metric_value(response.text, "disk_worker_accepting_jobs") == 0,
        "draining Worker still advertises job acceptance",
    )


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
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_worker_drain_{suffix}"
    database_created = False
    blocker: psycopg.Connection[dict[str, Any]] | None = None
    worker_a: ManagedServer | None = None
    worker_b: ManagedServer | None = None

    try:
        binary = resolve_current_binary()
        with tempfile.TemporaryDirectory(prefix="disk-worker-drain-") as temporary:
            temporary_root = Path(temporary)
            final_root = temporary_root / "final"
            staging_root = temporary_root / "staging"
            worker_a_port, worker_b_port = allocate_ports(2)

            create_database(database_name)
            database_created = True
            run_sql_file(database_name, INIT_SQL)

            target = create_blob_gc_fixture(database_name, final_root, "held")
            blocker = blocking_connection(database_name)
            blocker.execute(
                "SELECT id FROM file_contents WHERE id = %s FOR UPDATE",
                (target["content_id"],),
            )

            worker_a = ManagedServer(
                name=WORKER_A,
                binary=binary,
                run_directory=temporary_root / "worker-a",
                config=worker_config(
                    database_name,
                    worker_a_port,
                    WORKER_A,
                    final_root,
                    staging_root,
                ),
                database_name=database_name,
                port=worker_a_port,
                readiness_path="/api/health/ready",
                role="worker",
                environment_overrides={"DISK_WORKER_CLAIMING_ENABLED": "true"},
            )

            claimed = wait_until(
                "Worker A held-job claim",
                lambda: (
                    snapshot
                    if int(
                        (snapshot := job_snapshot(database_name, target["job_id"]))[
                            "status"
                        ]
                    )
                    == 1
                    and snapshot["locked_by"] == WORKER_A
                    and int(snapshot["attempts"]) == 1
                    and snapshot["lease_live"] is True
                    else None
                ),
                timeout_seconds=15,
            )
            wait_until(
                "Worker A execution log",
                lambda: (
                    True
                    if f"job_id={target['job_id']}, job_type=blob_gc"
                    in read_log(worker_a)
                    else None
                ),
                timeout_seconds=5,
            )
            wait_until(
                "Worker A initial seed cycle",
                lambda: (
                    True
                    if "Periodic storage job seed cycle completed" in read_log(worker_a)
                    else None
                ),
                timeout_seconds=5,
            )

            sentinel = create_blob_gc_fixture(database_name, final_root, "sentinel")
            log_before_signal = read_log(worker_a)
            execution_starts_before = log_before_signal.count(
                "Storage job execution started"
            )
            seed_cycles_before = log_before_signal.count(
                "Periodic storage job seed cycle completed"
            )
            require(
                execution_starts_before == 1, "Worker A claimed unexpected startup work"
            )

            require(worker_a.process is not None, "Worker A process is unavailable")
            signal_started = time.monotonic()
            worker_a.process.send_signal(signal.SIGTERM)
            health = wait_until(
                "Worker A draining readiness",
                lambda: draining_health(worker_a),
                timeout_seconds=1.5,
            )
            assert_draining_metrics(worker_a)

            worker_b = ManagedServer(
                name=WORKER_B,
                binary=binary,
                run_directory=temporary_root / "worker-b",
                config=worker_config(
                    database_name,
                    worker_b_port,
                    WORKER_B,
                    final_root,
                    staging_root,
                ),
                database_name=database_name,
                port=worker_b_port,
                readiness_path="/api/health/ready",
                role="worker",
                environment_overrides={"DISK_WORKER_CLAIMING_ENABLED": "true"},
            )

            sentinel_final = wait_until(
                "successor sentinel completion",
                lambda: (
                    snapshot
                    if int(
                        (
                            snapshot := job_snapshot(
                                database_name,
                                sentinel["job_id"],
                            )
                        )["status"]
                    )
                    == 3
                    else None
                ),
                timeout_seconds=20,
            )
            require(
                int(sentinel_final["attempts"]) == 1,
                "successor sentinel was attempted more than once",
            )

            return_code = worker_a.process.wait(timeout=DRAIN_SECONDS + 6)
            drain_elapsed = time.monotonic() - signal_started
            require(
                return_code == 0, f"Worker A SIGTERM exit code drifted: {return_code}"
            )
            require(
                drain_elapsed >= DRAIN_SECONDS - 0.25,
                f"Worker A exited before its drain deadline: {drain_elapsed:.3f}s",
            )
            require(
                drain_elapsed < DRAIN_SECONDS + 4,
                f"Worker A exceeded its bounded drain: {drain_elapsed:.3f}s",
            )

            log_after_exit = read_log(worker_a)
            require(
                log_after_exit.count("Storage job execution started")
                == execution_starts_before,
                "Worker A started another job after SIGTERM",
            )
            require(
                log_after_exit.count("Periodic storage job seed cycle completed")
                == seed_cycles_before,
                "Worker A seeded another periodic cycle after SIGTERM",
            )
            require(
                f"job_id={sentinel['job_id']}" not in log_after_exit,
                "Worker A touched the post-signal sentinel",
            )
            require(
                "Storage worker runtime draining" in log_after_exit
                and "Process drain deadline reached" in log_after_exit,
                "Worker A did not log its bounded drain deadline",
            )

            after_exit = job_snapshot(database_name, target["job_id"])
            require(
                int(after_exit["status"]) == 1
                and int(after_exit["attempts"]) == 1
                and after_exit["locked_by"] == WORKER_A
                and after_exit["lease_live"] is True,
                f"Worker A exit rewrote its persisted lease: {after_exit}",
            )
            require(
                after_exit["locked_until"] >= claimed["locked_until"],
                "Worker A exit shortened the held lease",
            )

            blocker.rollback()
            blocker.close()
            blocker = None

            final_target = wait_until(
                "Worker B natural lease takeover",
                lambda: (
                    snapshot
                    if int(
                        (snapshot := job_snapshot(database_name, target["job_id"]))[
                            "status"
                        ]
                    )
                    == 3
                    else None
                ),
                timeout_seconds=LEASE_SECONDS + 15,
            )
            require(
                int(final_target["attempts"]) == 2,
                f"held job did not converge through one takeover: {final_target}",
            )
            require(
                final_target["completed_at"] >= after_exit["locked_until"],
                "successor completed the held job before its persisted lease deadline",
            )
            require(
                final_target["locked_by"] is None
                and final_target["locked_until"] is None,
                "completed takeover retained lease ownership",
            )

            worker_b_log = read_log(worker_b)
            require(
                f"job_id={sentinel['job_id']}, job_type=blob_gc, "
                f"lease_owner={WORKER_B}, attempts=1, lease_takeover=0" in worker_b_log,
                "successor log does not identify the independent sentinel",
            )
            require(
                f"job_id={target['job_id']}, job_type=blob_gc, "
                f"lease_owner={WORKER_B}, attempts=2, lease_takeover=1" in worker_b_log,
                "successor log does not identify the expired-lease takeover",
            )
            require(
                content_count(database_name, target["content_id"]) == 0
                and content_count(database_name, sentinel["content_id"]) == 0,
                "Blob GC takeover retained zero-reference content",
            )
            require(
                not target["blob_path"].exists() and not sentinel["blob_path"].exists(),
                "Blob GC drain scenario retained a final object",
            )
            require(
                dedupe_count(database_name, target["dedupe_key"]) == 1
                and dedupe_count(database_name, sentinel["dedupe_key"]) == 1,
                "Worker drain scenario duplicated a logical job",
            )

            write_evidence(
                {
                    "schema_version": 1,
                    "scenario": "worker_sigterm_drain_and_natural_lease_takeover",
                    "readiness_status": 503,
                    "draining": health["draining"],
                    "worker_claiming_enabled": health["worker_claiming_enabled"],
                    "worker_accepting": health["worker_accepting"],
                    "claiming_metric": 1,
                    "accepting_metric": 0,
                    "drain_timeout_seconds": DRAIN_SECONDS,
                    "drain_deadline_reached": True,
                    "retiring_exit_code": return_code,
                    "new_claims_after_signal": 0,
                    "new_seed_cycles_after_signal": 0,
                    "held_lease_live_after_exit": True,
                    "held_attempts_before": 1,
                    "held_attempts_after": int(final_target["attempts"]),
                    "takeover_after_persisted_deadline": True,
                    "lease_takeover_logged": True,
                    "sentinel_attempts": int(sentinel_final["attempts"]),
                    "sentinel_completed_by_successor": True,
                    "manual_storage_job_updates": 0,
                    "logical_job_rows": 2,
                    "remaining_content_rows": 0,
                    "remaining_blob_objects": 0,
                    "passed": True,
                }
            )
            require(
                EVIDENCE_PATH.stat().st_mode & 0o777 == 0o600,
                "Worker drain evidence mode drifted",
            )

            worker_b.stop()
            worker_b = None
            worker_a.stop()
            worker_a = None
            print(
                "PASS: SIGTERM stopped Worker claims and preserved the held lease "
                "for natural successor takeover"
            )
        return 0
    except BaseException:
        for server in (worker_a, worker_b):
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
        raise
    finally:
        if blocker is not None:
            blocker.rollback()
            blocker.close()
        for server in (worker_b, worker_a):
            if server is not None:
                server.stop()
        if database_created:
            drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

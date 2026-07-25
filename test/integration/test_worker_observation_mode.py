#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify that an observation Worker reads health/metrics without mutating jobs."""

from __future__ import annotations

import json
import re
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any

import httpx

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
    run_sql_file,
    server_config,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = (
    REPO_ROOT / ".sisyphus" / "evidence" / "worker-observation-summary.json"
)
INSTANCE_ID = "worker-observer-integration"
POLL_INTERVAL_MS = 100
OBSERVATION_SECONDS = 0.6


def runtime_events(log: str, operation: str) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for line in log.splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(record, dict):
            continue
        if (
            record.get("schema_version") == 1
            and record.get("source") == "application"
            and record.get("operation") == operation
        ):
            events.append(record)
    return events


def require_unowned_runtime_context(event: dict[str, Any], operation: str) -> str:
    message = str(event.get("message", ""))
    require(event.get("instance_id") == INSTANCE_ID, f"runtime instance drifted: {event}")
    require(event.get("operation") == operation, f"runtime operation drifted: {event}")
    require(event.get("request_id") is None, f"runtime request ownership drifted: {event}")
    require(event.get("upload_id") is None, f"runtime upload ownership drifted: {event}")
    require(event.get("job_id") is None, f"runtime job ownership drifted: {event}")
    require(event.get("lease_owner") is None, f"runtime lease ownership drifted: {event}")
    require(event.get("state_version") is None, f"runtime version ownership drifted: {event}")
    require("instance_id=" not in message, f"runtime message repeats instance: {event}")
    return message


def require_storage_runtime_logs(
    log: str,
    final_root: Path,
    staging_root: Path,
) -> None:
    events = runtime_events(log, "storage_runtime")
    require(len(events) == 4, f"expected four local storage runtime events, got {events}")

    messages = [str(event.get("message", "")) for event in events]
    require(
        messages.count("Storage backend selected: backend=local") == 1,
        f"local storage selection event drifted: {messages}",
    )
    require(
        sum(
            re.fullmatch(
                r"Local file storage initialized: io_threads=\d+, assembly_threads=\d+",
                message,
            )
            is not None
            for message in messages
        )
        == 1,
        f"local file storage event drifted: {messages}",
    )
    require(
        sum(
            re.fullmatch(r"Local blob storage initialized: io_threads=\d+", message)
            is not None
            for message in messages
        )
        == 1,
        f"local blob storage event drifted: {messages}",
    )
    require(
        messages.count("Storage managers initialized") == 1,
        f"storage manager event drifted: {messages}",
    )

    for event in events:
        message = require_unowned_runtime_context(event, "storage_runtime")
        require(str(final_root) not in message, f"storage message leaked final path: {event}")
        require(str(staging_root) not in message, f"storage message leaked staging path: {event}")
        for forbidden in ("bucket=", "endpoint=", "region=", "prefix=", "object_key="):
            require(forbidden not in message, f"storage message leaked {forbidden}: {event}")


def require_process_runtime_logs(log: str) -> None:
    events = runtime_events(log, "process_runtime")
    require(len(events) == 3, f"expected three process startup events, got {events}")
    messages = [require_unowned_runtime_context(event, "process_runtime") for event in events]

    require(
        sum(
            message.startswith("Process runtime configured: framework_version=")
            and message.endswith(", role=worker")
            for message in messages
        )
        == 1,
        f"process configuration event drifted: {messages}",
    )
    require(
        messages.count(
            "Worker observation mode enabled; job claiming and scheduled task "
            "registration are disabled"
        )
        == 1,
        f"Worker observation event drifted: {messages}",
    )
    require(
        messages.count("Process initialization completed: role=worker") == 1,
        f"process completion event drifted: {messages}",
    )


def seed_ready_job(database_name: str, dedupe_key: str) -> dict[str, Any]:
    with connect(database_name) as connection:
        row = connection.execute(
            """
            INSERT INTO storage_jobs
                (job_type, aggregate_id, dedupe_key, payload, status, attempts,
                 available_at)
            VALUES ('storage_reconcile', %s, %s, '{}'::jsonb, 0, 0,
                    NOW() - INTERVAL '1 minute')
            RETURNING id, job_type, aggregate_id, dedupe_key, payload::text AS payload,
                      status, attempts, locked_by, locked_until, available_at,
                      updated_at
            """,
            (f"observation-{uuid.uuid4().hex}", dedupe_key),
        ).fetchone()
        require(row is not None, "failed to seed the observation queue job")
        return dict(row)


def queue_snapshot(database_name: str, dedupe_key: str) -> tuple[dict[str, Any], int]:
    with connect(database_name) as connection:
        row = connection.execute(
            """
            SELECT id, job_type, aggregate_id, dedupe_key, payload::text AS payload,
                   status, attempts, locked_by, locked_until, available_at,
                   updated_at
            FROM storage_jobs
            WHERE dedupe_key = %s
            """,
            (dedupe_key,),
        ).fetchone()
        count = connection.execute(
            "SELECT COUNT(*) AS count FROM storage_jobs"
        ).fetchone()
        require(row is not None, "observation queue job disappeared")
        require(count is not None, "storage job count query returned no row")
        return dict(row), int(count["count"])


def readiness(base_url: str) -> dict[str, Any]:
    response = httpx.get(base_url + "/api/health/ready", timeout=5)
    require(response.status_code == 200, f"readiness returned HTTP {response.status_code}")
    body = response.json()
    require(body.get("code") in (0, "0"), f"readiness returned failure: {body}")
    data = body.get("data")
    require(isinstance(data, dict), f"readiness data is invalid: {body}")
    require(data.get("overall_status") == "healthy", "observation Worker is not ready")
    require(data.get("role") == "worker", "observation Worker role drifted")
    require(data.get("initialized") is True, "observation Worker is not initialized")
    require(data.get("draining") is False, "observation Worker unexpectedly drains")
    require(
        data.get("worker_claiming_enabled") is False,
        "health does not report claiming disabled",
    )
    require(
        data.get("worker_accepting") is False,
        "health reports that the observation Worker accepts jobs",
    )
    components = data.get("components")
    require(isinstance(components, dict), "readiness components are missing")
    require(
        {"runtime", "database", "staging_storage", "final_storage", "storage_jobs"}
        <= set(components),
        "observation readiness skipped a required Worker dependency",
    )
    require("redis" not in components, "Worker readiness unexpectedly checks Redis")
    require(
        all(component.get("status") == "healthy" for component in components.values()),
        f"observation readiness contains an unhealthy component: {components}",
    )
    return data


def metric_value(metrics: str, sample: str) -> float:
    prefix = sample + " "
    values = [line[len(prefix) :] for line in metrics.splitlines() if line.startswith(prefix)]
    require(len(values) == 1, f"expected one metric sample for {sample}, got {values}")
    return float(values[0])


def metrics_snapshot(base_url: str) -> str:
    response = httpx.get(base_url + "/metrics", timeout=5)
    require(response.status_code == 200, f"metrics returned HTTP {response.status_code}")
    metrics = response.text
    require(
        metric_value(metrics, "disk_metrics_snapshot_success") == 1,
        "database-backed metrics snapshot failed",
    )
    require(
        metric_value(metrics, "disk_worker_claiming_enabled") == 0,
        "claiming-enabled gauge is not zero",
    )
    require(
        metric_value(metrics, "disk_worker_accepting_jobs") == 0,
        "accepting-jobs gauge is not zero",
    )
    require(
        metric_value(metrics, 'disk_storage_jobs{status="pending"}') == 1,
        "pending queue depth does not include the seeded job",
    )
    return metrics


def main() -> int:
    suffix = f"{uuid.uuid4().hex[:8]}_{int(time.time())}"
    database_name = f"disk_worker_observe_{suffix}"
    dedupe_key = f"observation:{suffix}"
    database_created = False
    worker: ManagedServer | None = None

    try:
        current_binary = resolve_current_binary()
        with tempfile.TemporaryDirectory(prefix="disk-worker-observation-") as temporary:
            temporary_root = Path(temporary)
            final_root = temporary_root / "final"
            staging_root = temporary_root / "staging"

            create_database(database_name)
            database_created = True
            run_sql_file(database_name, INIT_SQL)
            seeded = seed_ready_job(database_name, dedupe_key)

            port = allocate_ports(1)[0]
            config = server_config(
                database_name,
                port,
                INSTANCE_ID,
                final_root,
                staging_root,
                role="worker",
            )
            disk_config = config["custom_config"]["disk"]
            disk_config["worker_claiming_enabled"] = False
            disk_config["worker_poll_interval_ms"] = POLL_INTERVAL_MS

            worker = ManagedServer(
                name=INSTANCE_ID,
                binary=current_binary,
                run_directory=temporary_root / "worker-run",
                config=config,
                database_name=database_name,
                port=port,
                readiness_path="/api/health/ready",
                role="worker",
            )

            readiness(worker.base_url)
            metrics_snapshot(worker.base_url)
            before, before_count = queue_snapshot(database_name, dedupe_key)
            require(before == seeded, "queue job changed before the observation interval")
            require(before_count == 1, "unexpected periodic jobs were seeded at startup")

            time.sleep(OBSERVATION_SECONDS)

            readiness(worker.base_url)
            metrics_snapshot(worker.base_url)
            after, after_count = queue_snapshot(database_name, dedupe_key)
            require(after == before, "observation Worker mutated the ready job")
            require(after_count == before_count, "observation Worker seeded periodic jobs")
            require(after["status"] == 0, "observation Worker changed job status")
            require(after["attempts"] == 0, "observation Worker incremented attempts")
            require(after["locked_by"] is None, "observation Worker acquired the job lease")
            require(after["locked_until"] is None, "observation Worker wrote a lease deadline")
            worker.require_running("observation interval")

            log = worker.log_path.read_text(encoding="utf-8", errors="replace")
            require(
                "Worker observation mode enabled" in log,
                "startup log does not identify observation mode",
            )
            require_storage_runtime_logs(log, final_root, staging_root)
            require_process_runtime_logs(log)

            EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
            EVIDENCE_PATH.write_text(
                json.dumps(
                    {
                        "database": database_name,
                        "instance_id": INSTANCE_ID,
                        "poll_interval_ms": POLL_INTERVAL_MS,
                        "observation_seconds": OBSERVATION_SECONDS,
                        "job_id": int(after["id"]),
                        "job_status": int(after["status"]),
                        "attempts": int(after["attempts"]),
                        "job_count_before": before_count,
                        "job_count_after": after_count,
                        "readiness": "healthy",
                        "metrics_snapshot_success": 1,
                        "worker_claiming_enabled": 0,
                        "worker_accepting_jobs": 0,
                        "storage_runtime_events": 4,
                        "storage_runtime_context": True,
                        "storage_runtime_details_bounded": True,
                        "process_runtime_events": 3,
                        "process_runtime_context": True,
                        "process_runtime_details_bounded": True,
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            worker.stop()
            worker = None
            print(
                "PASS: observation Worker stayed ready and reported queue metrics "
                "without claiming or seeding jobs"
            )
        return 0
    except BaseException:
        if worker is not None:
            print(worker.log_tail(), file=sys.stderr)
        raise
    finally:
        if worker is not None:
            worker.stop()
        if database_created:
            drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

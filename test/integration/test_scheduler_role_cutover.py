#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify that only a claiming Worker owns cluster periodic scheduling."""

from __future__ import annotations

import json
import sys
import tempfile
import time
import uuid
from collections import Counter
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
    REPO_ROOT / ".sisyphus" / "evidence" / "scheduler-role-cutover-summary.json"
)
API_A_INSTANCE_ID = "scheduler-cutover-api-a"
API_B_INSTANCE_ID = "scheduler-cutover-api-b"
WORKER_INSTANCE_ID = "scheduler-cutover-worker"
WORKER_POLL_INTERVAL_MS = 100
API_SETTLE_SECONDS = 0.5
PERIODIC_JOB_TIMEOUT_SECONDS = 30
SEEDER_MARKERS = (
    "Periodic storage job seeder started",
    "Periodic storage job seed cycle completed",
)


def readiness(base_url: str, expected_role: str, expected_instance_id: str) -> dict[str, Any]:
    response = httpx.get(base_url + "/api/health/ready", timeout=5)
    require(response.status_code == 200, f"readiness returned HTTP {response.status_code}")
    body = response.json()
    require(body.get("code") in (0, "0"), f"readiness returned failure: {body}")
    data = body.get("data")
    require(isinstance(data, dict), f"readiness data is invalid: {body}")
    require(data.get("overall_status") == "healthy", f"{expected_instance_id} is not ready")
    require(data.get("role") == expected_role, f"{expected_instance_id} role drifted")
    require(
        data.get("instance_id") == expected_instance_id,
        f"{expected_instance_id} readiness reports another instance",
    )
    require(data.get("initialized") is True, f"{expected_instance_id} is not initialized")
    require(data.get("draining") is False, f"{expected_instance_id} unexpectedly drains")
    return data


def periodic_snapshot(database_name: str) -> list[dict[str, Any]]:
    with connect(database_name) as connection:
        rows = connection.execute(
            """
            SELECT id, job_type, aggregate_id, dedupe_key, payload::text AS payload,
                   status, attempts, max_attempts, available_at, locked_by,
                   locked_until, last_error, created_at, updated_at, completed_at
            FROM storage_jobs
            WHERE dedupe_key LIKE 'periodic:%'
            ORDER BY id
            """
        ).fetchall()
        return [dict(row) for row in rows]


def wait_for_periodic_jobs(database_name: str) -> list[dict[str, Any]]:
    deadline = time.monotonic() + PERIODIC_JOB_TIMEOUT_SECONDS
    last_snapshot: list[dict[str, Any]] = []
    while time.monotonic() < deadline:
        last_snapshot = periodic_snapshot(database_name)
        if len(last_snapshot) == 6 and all(int(row["status"]) == 3 for row in last_snapshot):
            return last_snapshot
        time.sleep(0.1)
    raise AssertionError(
        "claiming Worker did not complete the six periodic jobs: "
        f"{[(row['job_type'], row['status'], row['attempts']) for row in last_snapshot]}"
    )


def read_log(server: ManagedServer) -> str:
    if server.log_handle is not None:
        server.log_handle.flush()
    return server.log_path.read_text(encoding="utf-8", errors="replace")


def require_api_scheduler_disabled(server: ManagedServer, health: dict[str, Any]) -> str:
    require(
        health.get("worker_claiming_enabled") is False,
        f"{server.name} reports effective Worker claiming",
    )
    require(
        health.get("worker_accepting") is False,
        f"{server.name} reports accepting Worker jobs",
    )
    log = read_log(server)
    for marker in (*SEEDER_MARKERS, "Storage worker runtime started"):
        require(marker not in log, f"{server.name} unexpectedly logged {marker!r}")
    return log


def main() -> int:
    suffix = f"{uuid.uuid4().hex[:8]}_{int(time.time())}"
    database_name = f"disk_scheduler_cutover_{suffix}"
    database_created = False
    api_a: ManagedServer | None = None
    api_b: ManagedServer | None = None
    worker: ManagedServer | None = None

    try:
        current_binary = resolve_current_binary()
        with tempfile.TemporaryDirectory(prefix="disk-scheduler-cutover-") as temporary:
            temporary_root = Path(temporary)
            final_root = temporary_root / "final"
            staging_root = temporary_root / "staging"
            api_a_port, worker_port, api_b_port = allocate_ports(3)

            create_database(database_name)
            database_created = True
            run_sql_file(database_name, INIT_SQL)

            api_a_config = server_config(
                database_name,
                api_a_port,
                API_A_INSTANCE_ID,
                final_root,
                staging_root,
                role="api",
            )
            api_a_config["custom_config"]["disk"]["worker_claiming_enabled"] = True
            api_a = ManagedServer(
                name=API_A_INSTANCE_ID,
                binary=current_binary,
                run_directory=temporary_root / "api-a-run",
                config=api_a_config,
                database_name=database_name,
                port=api_a_port,
                readiness_path="/api/health/ready",
                role="api",
                environment_overrides={"DISK_WORKER_CLAIMING_ENABLED": "true"},
            )
            api_a_health = readiness(api_a.base_url, "api", API_A_INSTANCE_ID)
            time.sleep(API_SETTLE_SECONDS)
            require(
                periodic_snapshot(database_name) == [],
                "API A seeded periodic jobs before Worker execution was enabled",
            )
            require_api_scheduler_disabled(api_a, api_a_health)

            worker_config = server_config(
                database_name,
                worker_port,
                WORKER_INSTANCE_ID,
                final_root,
                staging_root,
                role="worker",
            )
            worker_disk_config = worker_config["custom_config"]["disk"]
            worker_disk_config["worker_claiming_enabled"] = True
            worker_disk_config["worker_poll_interval_ms"] = WORKER_POLL_INTERVAL_MS
            worker = ManagedServer(
                name=WORKER_INSTANCE_ID,
                binary=current_binary,
                run_directory=temporary_root / "worker-run",
                config=worker_config,
                database_name=database_name,
                port=worker_port,
                readiness_path="/api/health/ready",
                role="worker",
                environment_overrides={"DISK_WORKER_CLAIMING_ENABLED": "true"},
            )
            worker_health = readiness(worker.base_url, "worker", WORKER_INSTANCE_ID)
            require(
                worker_health.get("worker_claiming_enabled") is True,
                "Worker execution cutover did not enable claiming",
            )
            require(
                worker_health.get("worker_accepting") is True,
                "claiming Worker does not accept jobs",
            )

            baseline = wait_for_periodic_jobs(database_name)
            job_type_counts = Counter(str(row["job_type"]) for row in baseline)
            require(
                job_type_counts
                == Counter({"expire_uploads": 1, "expire_trash": 1, "storage_reconcile": 4}),
                f"periodic job set drifted: {job_type_counts}",
            )
            require(
                all(int(row["attempts"]) == 1 for row in baseline),
                "a periodic job did not execute exactly once",
            )
            worker_log = read_log(worker)
            require(
                worker_log.count(SEEDER_MARKERS[0]) == 1,
                "claiming Worker did not start exactly one periodic seeder",
            )
            require(
                SEEDER_MARKERS[1] in worker_log,
                "claiming Worker did not log a completed seed cycle",
            )

            api_b_config = server_config(
                database_name,
                api_b_port,
                API_B_INSTANCE_ID,
                final_root,
                staging_root,
                role="api",
            )
            api_b_config["custom_config"]["disk"]["worker_claiming_enabled"] = True
            api_b = ManagedServer(
                name=API_B_INSTANCE_ID,
                binary=current_binary,
                run_directory=temporary_root / "api-b-run",
                config=api_b_config,
                database_name=database_name,
                port=api_b_port,
                readiness_path="/api/health/ready",
                role="api",
                environment_overrides={"DISK_WORKER_CLAIMING_ENABLED": "true"},
            )
            api_b_health = readiness(api_b.base_url, "api", API_B_INSTANCE_ID)
            time.sleep(API_SETTLE_SECONDS)
            after_scale_up = periodic_snapshot(database_name)
            require(
                after_scale_up == baseline,
                "adding API B changed the complete periodic job snapshot",
            )
            api_b_log = require_api_scheduler_disabled(api_b, api_b_health)

            api_b.stop()
            api_b = None
            time.sleep(API_SETTLE_SECONDS)
            after_scale_down = periodic_snapshot(database_name)
            require(
                after_scale_down == baseline,
                "removing API B changed the complete periodic job snapshot",
            )
            api_a_log = require_api_scheduler_disabled(api_a, api_a_health)
            api_a.require_running("API scale cycle")
            worker.require_running("API scale cycle")

            EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
            EVIDENCE_PATH.write_text(
                json.dumps(
                    {
                        "database": database_name,
                        "instances": {
                            "api_a": API_A_INSTANCE_ID,
                            "api_b": API_B_INSTANCE_ID,
                            "worker": WORKER_INSTANCE_ID,
                        },
                        "shared_worker_claiming_setting": True,
                        "api_effective_worker_claiming": False,
                        "worker_effective_claiming": True,
                        "periodic_job_count": len(baseline),
                        "periodic_job_type_counts": dict(sorted(job_type_counts.items())),
                        "periodic_jobs": [
                            {
                                "id": int(row["id"]),
                                "job_type": str(row["job_type"]),
                                "dedupe_key": str(row["dedupe_key"]),
                                "status": int(row["status"]),
                                "attempts": int(row["attempts"]),
                            }
                            for row in baseline
                        ],
                        "snapshot_equal_after_api_scale_up": after_scale_up == baseline,
                        "snapshot_equal_after_api_scale_down": after_scale_down == baseline,
                        "api_a_seeder_log_entries": sum(
                            api_a_log.count(marker) for marker in SEEDER_MARKERS
                        ),
                        "api_b_seeder_log_entries": sum(
                            api_b_log.count(marker) for marker in SEEDER_MARKERS
                        ),
                        "worker_seeder_start_entries": worker_log.count(SEEDER_MARKERS[0]),
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            worker.stop()
            worker = None
            api_a.stop()
            api_a = None
            print(
                "PASS: only the claiming Worker seeded six periodic jobs, and API "
                "scale-up/scale-down left their complete rows unchanged"
            )
        return 0
    except BaseException:
        for server in (api_a, api_b, worker):
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
        raise
    finally:
        for server in (api_b, worker, api_a):
            if server is not None:
                server.stop()
        if database_created:
            drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

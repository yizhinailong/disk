#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "prometheus-client", "psycopg[binary]"]
# ///

"""Measure 1/2/N Worker backlog drain capacity under continuous API load."""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import httpx

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

from bench_api_scaling import (  # noqa: E402
    git_metadata,
    host_cpu_sample,
    loopback_bytes,
    parse_replica_counts,
    process_sample,
)
from bench_failure_under_load import (  # noqa: E402
    ContinuousLoad,
    HealthAwareRouter,
    ManagedDiskProcess,
    PhaseTracker,
    json_ready,
    login,
    metric_delta,
    normalize_times,
    read_metrics,
    resolve_binary,
    storage_job_metrics,
    topology_config,
    wait_until,
)
from bench_storage_workloads import (  # noqa: E402
    directory_bytes,
    environment_metadata,
    summarize,
)
from test_expand_mixed_version import (  # noqa: E402
    INIT_SQL,
    allocate_ports,
    connect,
    create_database,
    drop_database,
    require,
    run_database_command,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
MIB = 1024 * 1024
WORKER_POLL_INTERVAL_SECONDS = 0.1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--replicas", type=parse_replica_counts, default=[1, 2, 4])
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--backlog-jobs", type=int, default=1000)
    parser.add_argument("--job-bytes", type=int, default=4096)
    parser.add_argument("--request-rate", type=float, default=100.0)
    parser.add_argument("--load-concurrency", type=int, default=16)
    parser.add_argument("--warmup-requests", type=int, default=100)
    parser.add_argument("--steady-seconds", type=float, default=3.0)
    parser.add_argument("--recovery-seconds", type=float, default=3.0)
    parser.add_argument("--timeout-seconds", type=float, default=60.0)
    parser.add_argument("--minimum-online-rate-percent", type=float, default=90.0)
    parser.add_argument("--drain-p95-ratio", type=float, default=2.0)
    parser.add_argument("--drain-p95-floor-ms", type=float, default=20.0)
    parser.add_argument("--recovery-p95-ratio", type=float, default=1.5)
    parser.add_argument("--recovery-p95-floor-ms", type=float, default=10.0)
    parser.add_argument("--server-bin", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    positive = {
        "trials": args.trials,
        "backlog_jobs": args.backlog_jobs,
        "job_bytes": args.job_bytes,
        "request_rate": args.request_rate,
        "load_concurrency": args.load_concurrency,
        "warmup_requests": args.warmup_requests,
        "steady_seconds": args.steady_seconds,
        "recovery_seconds": args.recovery_seconds,
        "timeout_seconds": args.timeout_seconds,
        "minimum_online_rate_percent": args.minimum_online_rate_percent,
        "drain_p95_ratio": args.drain_p95_ratio,
        "drain_p95_floor_ms": args.drain_p95_floor_ms,
        "recovery_p95_ratio": args.recovery_p95_ratio,
        "recovery_p95_floor_ms": args.recovery_p95_floor_ms,
    }
    for name, value in positive.items():
        if value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.trials > 10:
        parser.error("--trials cannot exceed 10")
    if not 10 <= args.backlog_jobs <= 5000:
        parser.error("--backlog-jobs must be in range 10..5000")
    if args.job_bytes > MIB:
        parser.error("--job-bytes cannot exceed 1 MiB")
    if args.request_rate > 500:
        parser.error("--request-rate cannot exceed 500")
    if args.load_concurrency > 128:
        parser.error("--load-concurrency cannot exceed 128")
    if not 1 <= args.minimum_online_rate_percent <= 100:
        parser.error("--minimum-online-rate-percent must be in range 1..100")
    return args


@dataclass(frozen=True)
class BacklogFixture:
    dedupe_prefix: str
    upload_prefix: str
    jobs: int
    bytes_created: int
    path_count: int


def seed_backlog(
    database_name: str,
    staging_root: Path,
    batch_id: str,
    jobs: int,
    job_bytes: int,
) -> BacklogFixture:
    dedupe_prefix = f"worker-backlog:{batch_id}:"
    upload_prefix = f"worker_backlog_{batch_id}_"
    first_size = job_bytes // 2
    second_size = job_bytes - first_size

    for index in range(jobs):
        upload_id = f"{upload_prefix}{index:05d}"
        directory = staging_root / upload_id
        directory.mkdir(parents=True, exist_ok=False)
        byte = bytes((index % 251 + 1,))
        (directory / "0.chunk").write_bytes(byte * first_size)
        (staging_root / f"{upload_id}.tmp").write_bytes(byte * second_size)

    with connect(database_name) as connection:
        row = connection.execute(
            "WITH inserted AS ("
            "  INSERT INTO storage_jobs "
            "    (job_type, aggregate_id, dedupe_key, payload, max_attempts, available_at) "
            "  SELECT 'staging_cleanup', "
            "    %s || LPAD(item::text, 5, '0'), "
            "    %s || LPAD(item::text, 5, '0'), "
            "    jsonb_build_object("
            "      'upload_id', %s || LPAD(item::text, 5, '0'), "
            "      'backend', 'local', "
            "      'prefix', 'staging/' || %s || LPAD(item::text, 5, '0')"
            "    ), 8, NOW() + INTERVAL '1 hour' "
            "  FROM generate_series(0, %s) AS item "
            "  RETURNING id"
            ") SELECT COUNT(*) AS count FROM inserted",
            (
                upload_prefix,
                dedupe_prefix,
                upload_prefix,
                upload_prefix,
                jobs - 1,
            ),
        ).fetchone()
    require(row is not None and int(row["count"]) == jobs, "backlog seed count drifted")
    return BacklogFixture(
        dedupe_prefix=dedupe_prefix,
        upload_prefix=upload_prefix,
        jobs=jobs,
        bytes_created=jobs * job_bytes,
        path_count=jobs * 2,
    )


def target_snapshot(
    database_name: str,
    dedupe_prefix: str,
    connection: Any | None = None,
) -> dict[str, Any]:
    owns_connection = connection is None
    current = connection if connection is not None else connect(database_name)
    try:
        row = current.execute(
            "SELECT "
            "  COUNT(*) AS total, "
            "  COUNT(*) FILTER (WHERE status = 0) AS pending, "
            "  COUNT(*) FILTER (WHERE status = 1) AS running, "
            "  COUNT(*) FILTER (WHERE status = 2) AS retry, "
            "  COUNT(*) FILTER (WHERE status = 3) AS succeeded, "
            "  COUNT(*) FILTER (WHERE status = 4) AS dead_letter, "
            "  COUNT(*) FILTER ("
            "    WHERE status IN (0, 2) AND available_at <= NOW()"
            "  ) AS ready, "
            "  COALESCE(EXTRACT(EPOCH FROM ("
            "    NOW() - MIN(available_at) FILTER ("
            "      WHERE status IN (0, 2) AND available_at <= NOW()"
            "    )"
            "  )), 0) AS oldest_ready_age_seconds, "
            "  COUNT(*) FILTER (WHERE attempts <> 1 AND status = 3) "
            "    AS succeeded_with_non_unit_attempts, "
            "  COUNT(*) FILTER (WHERE locked_by IS NOT NULL OR locked_until IS NOT NULL) "
            "    AS leased, "
            "  MIN(created_at) AS oldest_created_at, "
            "  MAX(completed_at) AS latest_completed_at, "
            "  NOW() AS database_now, "
            "  (SELECT COUNT(*) FROM pg_stat_activity "
            "    WHERE datname = current_database() AND pid <> pg_backend_pid()"
            "  ) AS postgres_connections "
            "FROM storage_jobs WHERE dedupe_key LIKE %s",
            (dedupe_prefix + "%",),
        ).fetchone()
    finally:
        if owns_connection:
            current.close()
    require(row is not None, "backlog snapshot returned no row")
    result = dict(row)
    for key in (
        "total",
        "pending",
        "running",
        "retry",
        "succeeded",
        "dead_letter",
        "ready",
        "succeeded_with_non_unit_attempts",
        "leased",
        "postgres_connections",
    ):
        result[key] = int(result[key])
    result["oldest_ready_age_seconds"] = float(result["oldest_ready_age_seconds"])
    result["remaining"] = result["pending"] + result["running"] + result["retry"]
    return result


def release_backlog(database_name: str, fixture: BacklogFixture) -> dict[str, Any]:
    with connect(database_name) as connection:
        row = connection.execute(
            "WITH released AS ("
            "  UPDATE storage_jobs SET available_at = created_at, updated_at = NOW() "
            "  WHERE dedupe_key LIKE %s AND status = 0 "
            "  RETURNING id"
            ") SELECT COUNT(*) AS count, NOW() AS database_now FROM released",
            (fixture.dedupe_prefix + "%",),
        ).fetchone()
    require(
        row is not None and int(row["count"]) == fixture.jobs,
        "backlog release count drifted",
    )
    return dict(row)


def non_target_active_jobs(database_name: str, dedupe_prefix: str) -> int:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT COUNT(*) AS count FROM storage_jobs "
            "WHERE dedupe_key NOT LIKE %s AND status IN (0, 1, 2) "
            "AND (status = 1 OR available_at <= NOW())",
            (dedupe_prefix + "%",),
        ).fetchone()
    require(row is not None, "failed to inspect periodic job noise")
    return int(row["count"])


class BacklogMonitor:
    def __init__(
        self,
        database_name: str,
        fixture: BacklogFixture,
        interval_seconds: float = WORKER_POLL_INTERVAL_SECONDS,
    ) -> None:
        self._database_name = database_name
        self._fixture = fixture
        self._interval_seconds = interval_seconds
        self._stop_event = threading.Event()
        self._terminal_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._samples: list[dict[str, Any]] = []
        self._error: BaseException | None = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        try:
            with connect(self._database_name) as connection:
                while not self._stop_event.is_set():
                    snapshot = target_snapshot(
                        self._database_name,
                        self._fixture.dedupe_prefix,
                        connection,
                    )
                    snapshot["at"] = time.monotonic()
                    with self._lock:
                        self._samples.append(snapshot)
                    if snapshot["dead_letter"] > 0 or (
                        snapshot["succeeded"] == self._fixture.jobs
                        and snapshot["remaining"] == 0
                    ):
                        self._terminal_event.set()
                        return
                    self._stop_event.wait(self._interval_seconds)
        except BaseException as error:
            self._error = error
            self._terminal_event.set()

    def wait(
        self,
        timeout_seconds: float,
        processes: tuple[ManagedDiskProcess, ...],
    ) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            if self._terminal_event.wait(0.1):
                break
            for process in processes:
                process.require_running("backlog drain")
        else:
            raise AssertionError("timed out waiting for Worker backlog drain")

        if self._error is not None:
            raise self._error
        samples = self.samples()
        require(samples, "backlog monitor produced no samples")
        final = samples[-1]
        require(final["dead_letter"] == 0, "backlog produced a dead letter")
        require(
            final["succeeded"] == self._fixture.jobs and final["remaining"] == 0,
            "backlog did not drain completely",
        )
        return final

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=3)

    def samples(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self._samples)


class ResourceProbe:
    def __init__(self, processes: dict[str, int]) -> None:
        self._processes = processes
        self._start_samples: dict[str, dict[str, float | int]] = {}
        self._peak_rss: dict[str, int] = {}
        self._host_start: tuple[int, int] | None = None
        self._loopback_start = 0
        self._started_at = 0.0
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        for name, pid in self._processes.items():
            sample = process_sample(pid)
            require(sample is not None, f"initial process sample failed for {name}")
            self._start_samples[name] = sample
            self._peak_rss[name] = int(sample["rss_bytes"])
        self._host_start = host_cpu_sample()
        self._loopback_start = loopback_bytes()
        self._started_at = time.perf_counter()
        self._thread = threading.Thread(target=self._sample_loop, daemon=True)
        self._thread.start()

    def _sample_loop(self) -> None:
        while not self._stop_event.wait(0.02):
            for name, pid in self._processes.items():
                sample = process_sample(pid)
                if sample is not None:
                    self._peak_rss[name] = max(
                        self._peak_rss[name], int(sample["rss_bytes"])
                    )

    def stop(self) -> dict[str, Any]:
        self.cancel()
        wall_seconds = time.perf_counter() - self._started_at
        require(wall_seconds > 0, "resource probe wall time is not positive")
        require(self._host_start is not None, "resource probe was not started")

        process_results: dict[str, Any] = {}
        for name, pid in self._processes.items():
            end = process_sample(pid)
            require(end is not None, f"final process sample failed for {name}")
            cpu_seconds = max(
                0.0,
                float(end["cpu_seconds"])
                - float(self._start_samples[name]["cpu_seconds"]),
            )
            process_results[name] = {
                "cpu_seconds": round(cpu_seconds, 6),
                "cpu_cores": round(cpu_seconds / wall_seconds, 3),
                "rss_start_bytes": int(self._start_samples[name]["rss_bytes"]),
                "rss_peak_bytes": self._peak_rss[name],
                "rss_end_bytes": int(end["rss_bytes"]),
            }

        host_end = host_cpu_sample()
        total_delta = host_end[0] - self._host_start[0]
        idle_delta = host_end[1] - self._host_start[1]
        busy_percent = (
            100 * (total_delta - idle_delta) / total_delta if total_delta > 0 else 0.0
        )
        network_bytes = loopback_bytes() - self._loopback_start
        return {
            "wall_seconds": round(wall_seconds, 6),
            "processes": process_results,
            "host_cpu_busy_percent": round(busy_percent, 3),
            "network_scope": "loopback_rx_plus_tx",
            "network_bytes": network_bytes,
            "network_mib_per_second": round(network_bytes / wall_seconds / MIB, 3),
        }

    def cancel(self) -> None:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=3)


def backlog_report(
    samples: list[dict[str, Any]],
    release_at: float,
    completed_at: float,
    fixture: BacklogFixture,
) -> dict[str, Any]:
    drain_seconds = completed_at - release_at
    require(drain_seconds > 0, "backlog drain duration is not positive")
    milestones: dict[str, float] = {}
    for label, fraction in (("25", 0.25), ("50", 0.5), ("75", 0.75), ("100", 1.0)):
        threshold = fixture.jobs * fraction
        matched = next(
            (sample for sample in samples if sample["succeeded"] >= threshold), None
        )
        require(matched is not None, f"missing backlog {label}% milestone")
        milestones[f"drained_{label}_percent_seconds"] = round(
            max(0.0, float(matched["at"]) - release_at), 6
        )

    normalized_samples = []
    for sample in samples:
        normalized = dict(sample)
        normalized["at_seconds"] = round(float(normalized.pop("at")) - release_at, 6)
        normalized_samples.append(json.loads(json.dumps(normalized, default=str)))

    return {
        "jobs": fixture.jobs,
        "bytes": fixture.bytes_created,
        "paths": fixture.path_count,
        "drain_seconds": round(drain_seconds, 6),
        "jobs_per_second": round(fixture.jobs / drain_seconds, 3),
        "bytes_per_second": round(fixture.bytes_created / drain_seconds, 3),
        "mib_per_second": round(fixture.bytes_created / drain_seconds / MIB, 3),
        "maximum_oldest_ready_age_seconds": round(
            max(float(sample["oldest_ready_age_seconds"]) for sample in samples), 6
        ),
        "maximum_postgres_connections": max(
            int(sample["postgres_connections"]) for sample in samples
        ),
        "milestones": milestones,
        "samples": normalized_samples,
    }


def online_report(
    results: list[dict[str, Any]],
    phase_windows: dict[str, float],
    origin: float,
    request_rate: float,
) -> dict[str, Any]:
    measured_phases = ("steady", "drain", "recovered")
    measured_results = [item for item in results if item["phase"] in measured_phases]
    require(measured_results, "continuous API load produced no measured results")
    physical_attempts = [attempt for item in results for attempt in item["attempts"]]
    logical_failures = [item for item in results if not item["success"]]
    failed_attempts = [
        attempt for attempt in physical_attempts if attempt["outcome"] != "success"
    ]

    phases: dict[str, Any] = {}
    for phase in measured_phases:
        phase_results = [item for item in measured_results if item["phase"] == phase]
        duration = phase_windows[phase]
        require(duration > 0, f"{phase} phase duration is not positive")
        require(phase_results, f"{phase} phase produced no API samples")
        successes = [item for item in phase_results if item["success"]]
        achieved_rate = len(phase_results) / duration
        phases[phase] = {
            "duration_seconds": round(duration, 6),
            "logical_requests": len(phase_results),
            "logical_successes": len(successes),
            "logical_failures": len(phase_results) - len(successes),
            "achieved_requests_per_second": round(achieved_rate, 3),
            "target_rate_percent": round(100 * achieved_rate / request_rate, 3),
            "latency": summarize([float(item["latency_ms"]) for item in phase_results]),
        }

    return {
        "logical_requests": len(results),
        "logical_successes": len(results) - len(logical_failures),
        "logical_failures": len(logical_failures),
        "measured_logical_requests": len(measured_results),
        "physical_attempts": len(physical_attempts),
        "failed_physical_attempts": len(failed_attempts),
        "phases": phases,
        "raw_results": normalize_times(results, origin),
    }


def worker_metrics_report(
    before: dict[str, dict[Any, float]],
    after: dict[str, dict[Any, float]],
) -> dict[str, Any]:
    workers: dict[str, Any] = {}
    total_outcomes = {
        "succeeded": 0,
        "retry": 0,
        "dead_letter": 0,
        "ownership_lost": 0,
    }
    total_duration_count = 0
    total_duration_seconds = 0.0
    for name in sorted(before):
        metrics = storage_job_metrics(before[name], after[name], "staging_cleanup")
        duration_count = int(
            metric_delta(
                before[name],
                after[name],
                "disk_storage_job_duration_seconds_count",
                job_type="staging_cleanup",
            )
        )
        duration_seconds = metric_delta(
            before[name],
            after[name],
            "disk_storage_job_duration_seconds_sum",
            job_type="staging_cleanup",
        )
        metrics["duration_count"] = duration_count
        metrics["duration_seconds"] = round(duration_seconds, 6)
        metrics["mean_duration_ms"] = round(
            1000 * duration_seconds / duration_count if duration_count else 0.0, 3
        )
        workers[name] = metrics
        total_duration_count += duration_count
        total_duration_seconds += duration_seconds
        for outcome, count in metrics["outcomes"].items():
            total_outcomes[outcome] += int(count)

    return {
        "workers": workers,
        "aggregate": {
            "outcomes": total_outcomes,
            "duration_count": total_duration_count,
            "duration_seconds": round(total_duration_seconds, 6),
            "mean_duration_ms": round(
                1000 * total_duration_seconds / total_duration_count
                if total_duration_count
                else 0.0,
                3,
            ),
        },
    }


def reconciliation_report(
    database_name: str,
    staging_root: Path,
    fixture: BacklogFixture,
) -> dict[str, Any]:
    snapshot = target_snapshot(database_name, fixture.dedupe_prefix)
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT "
            "  COUNT(*) FILTER (WHERE status = 4) AS all_dead_letters, "
            "  COUNT(*) FILTER (WHERE status IN (0, 1, 2)) AS all_active_jobs "
            "FROM storage_jobs"
        ).fetchone()
    require(row is not None, "final storage job reconciliation returned no row")
    matching_entries = [
        path
        for path in staging_root.iterdir()
        if path.name.startswith(fixture.upload_prefix)
    ]
    return {
        "passed": (
            snapshot["total"] == fixture.jobs
            and snapshot["succeeded"] == fixture.jobs
            and snapshot["remaining"] == 0
            and snapshot["dead_letter"] == 0
            and snapshot["succeeded_with_non_unit_attempts"] == 0
            and snapshot["leased"] == 0
            and int(row["all_dead_letters"]) == 0
            and not matching_entries
        ),
        "target": json_ready(snapshot),
        "all_dead_letters": int(row["all_dead_letters"]),
        "all_active_jobs": int(row["all_active_jobs"]),
        "matching_staging_entries": len(matching_entries),
        "staging_bytes": directory_bytes(staging_root),
    }


def run_trial(
    args: argparse.Namespace,
    binary: Path,
    run_id: str,
    round_index: int,
    order_index: int,
    replicas: int,
    log_directory: Path,
) -> dict[str, Any]:
    origin = time.monotonic()
    trial_id = f"{run_id}-r{round_index + 1}-w{replicas}"
    database_name = f"disk_worker_backlog_{trial_id.replace('-', '_')}"
    created_database = False
    processes: list[ManagedDiskProcess] = []
    workers: list[ManagedDiskProcess] = []
    router: HealthAwareRouter | None = None
    load: ContinuousLoad | None = None
    load_stopped = False
    monitor: BacklogMonitor | None = None
    resource_probe: ResourceProbe | None = None
    resource_stopped = False

    try:
        create_database(database_name)
        created_database = True
        run_database_command(
            ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
            database_name,
        )

        with tempfile.TemporaryDirectory(
            prefix=f"disk-worker-backlog-{trial_id}-"
        ) as raw:
            temp_root = Path(raw)
            final_root = temp_root / "final"
            staging_root = temp_root / "staging"
            final_root.mkdir()
            staging_root.mkdir()
            ports = allocate_ports(replicas + 1)

            api = ManagedDiskProcess(
                name=f"backlog-api-r{round_index + 1}-w{replicas}",
                role="api",
                binary=binary,
                run_directory=temp_root / "api",
                config=topology_config(
                    database_name,
                    ports[0],
                    f"backlog-api-r{round_index + 1}-w{replicas}",
                    "api",
                    final_root,
                    staging_root,
                    temp_root / "api-upload",
                ),
                database_name=database_name,
                port=ports[0],
                log_directory=log_directory,
            )
            processes.append(api)
            token, username = login(api.base_url)

            fixture = seed_backlog(
                database_name,
                staging_root,
                trial_id,
                args.backlog_jobs,
                args.job_bytes,
            )
            seeded_snapshot = target_snapshot(database_name, fixture.dedupe_prefix)
            require(
                seeded_snapshot["pending"] == fixture.jobs
                and seeded_snapshot["ready"] == 0,
                "seeded backlog became claimable before release",
            )

            phase = PhaseTracker("warmup")
            router = HealthAwareRouter({"api": api.base_url})
            router.start()
            load = ContinuousLoad(
                router=router,
                phase=phase,
                token=token,
                expected_username=username,
                request_rate=args.request_rate,
                concurrency=args.load_concurrency,
            )
            load.start()

            for worker_index in range(replicas):
                name = (
                    f"backlog-worker-r{round_index + 1}-w{replicas}-{worker_index + 1}"
                )
                worker = ManagedDiskProcess(
                    name=name,
                    role="worker",
                    binary=binary,
                    run_directory=temp_root / f"worker-{worker_index + 1}",
                    config=topology_config(
                        database_name,
                        ports[worker_index + 1],
                        name,
                        "worker",
                        final_root,
                        staging_root,
                        temp_root / f"worker-{worker_index + 1}-upload",
                    ),
                    database_name=database_name,
                    port=ports[worker_index + 1],
                    log_directory=log_directory,
                )
                workers.append(worker)
                processes.append(worker)

            wait_until(
                "periodic Worker noise to settle",
                lambda: (
                    non_target_active_jobs(database_name, fixture.dedupe_prefix) == 0
                ),
                20,
                interval=0.1,
                processes=tuple(processes),
            )
            wait_until(
                "continuous API warmup",
                lambda: load.count() >= args.warmup_requests,
                20,
                interval=0.05,
                processes=tuple(processes),
            )

            steady_started_at = phase.set("steady")
            time.sleep(args.steady_seconds)

            metrics_before = {
                worker.name: read_metrics(worker.base_url) for worker in workers
            }
            resource_probe = ResourceProbe(
                {
                    "api": api.pid,
                    "client": os.getpid(),
                    **{worker.name: worker.pid for worker in workers},
                }
            )
            resource_probe.start()
            monitor = BacklogMonitor(database_name, fixture)
            monitor.start()
            drain_started_at = phase.set("drain")
            released_at = time.monotonic()
            release_result = release_backlog(database_name, fixture)
            release_committed_at = time.monotonic()
            monitor.wait(args.timeout_seconds, tuple(processes))
            completed_at = time.monotonic()
            phase.set("post_drain")
            monitor.stop()
            samples = monitor.samples()
            resources = resource_probe.stop()
            resource_stopped = True
            metrics_after = {
                worker.name: read_metrics(worker.base_url) for worker in workers
            }
            worker_metrics = worker_metrics_report(metrics_before, metrics_after)

            recovered_started_at = phase.set("recovered")
            time.sleep(args.recovery_seconds)
            recovered_ended_at = time.monotonic()
            load.stop()
            load_stopped = True
            load_results = load.snapshot()

            phase_windows = {
                "steady": drain_started_at - steady_started_at,
                "drain": completed_at - drain_started_at,
                "recovered": recovered_ended_at - recovered_started_at,
            }
            online = online_report(
                load_results,
                phase_windows,
                origin,
                args.request_rate,
            )
            backlog = backlog_report(samples, released_at, completed_at, fixture)
            reconciliation = reconciliation_report(database_name, staging_root, fixture)

            steady_p95 = float(online["phases"]["steady"]["latency"]["p95_ms"])
            drain_p95 = float(online["phases"]["drain"]["latency"]["p95_ms"])
            recovered_p95 = float(online["phases"]["recovered"]["latency"]["p95_ms"])
            drain_p95_limit = max(
                steady_p95 * args.drain_p95_ratio, args.drain_p95_floor_ms
            )
            recovered_p95_limit = max(
                steady_p95 * args.recovery_p95_ratio,
                args.recovery_p95_floor_ms,
            )
            drain_rate_percent = float(online["phases"]["drain"]["target_rate_percent"])
            aggregate_outcomes = worker_metrics["aggregate"]["outcomes"]
            acceptance = {
                "backlog_drained_before_timeout": backlog["drain_seconds"]
                <= args.timeout_seconds,
                "online_requests_have_zero_failures": online["logical_failures"] == 0
                and online["failed_physical_attempts"] == 0,
                "drain_online_rate_at_least_target": drain_rate_percent
                >= args.minimum_online_rate_percent,
                "drain_p95_within_limit": drain_p95 <= drain_p95_limit,
                "recovered_p95_within_limit": recovered_p95 <= recovered_p95_limit,
                "worker_metrics_match_exactly_once": aggregate_outcomes
                == {
                    "succeeded": fixture.jobs,
                    "retry": 0,
                    "dead_letter": 0,
                    "ownership_lost": 0,
                },
                "reconciliation_passed": reconciliation["passed"],
            }
            acceptance["passed"] = all(acceptance.values())
            require(acceptance["passed"], f"trial acceptance failed: {acceptance}")

            worker_cpu_cores = sum(
                float(resources["processes"][worker.name]["cpu_cores"])
                for worker in workers
            )
            worker_peak_rss_bytes = sum(
                int(resources["processes"][worker.name]["rss_peak_bytes"])
                for worker in workers
            )
            return {
                "round": round_index + 1,
                "order": order_index + 1,
                "replicas": replicas,
                "database": database_name,
                "fixture": {
                    "jobs": fixture.jobs,
                    "bytes": fixture.bytes_created,
                    "paths": fixture.path_count,
                    "seeded_snapshot": json.loads(
                        json.dumps(seeded_snapshot, default=str)
                    ),
                    "release": json.loads(json.dumps(release_result, default=str)),
                },
                "timeline": normalize_times(
                    {
                        "phase_transitions": phase.transitions,
                        "released_at": released_at,
                        "release_committed_at": release_committed_at,
                        "completed_at": completed_at,
                    },
                    origin,
                ),
                "backlog": backlog,
                "online": online,
                "worker_metrics": worker_metrics,
                "resources": resources,
                "resource_summary": {
                    "worker_cpu_cores": round(worker_cpu_cores, 3),
                    "worker_peak_rss_bytes": worker_peak_rss_bytes,
                    "api_cpu_cores": resources["processes"]["api"]["cpu_cores"],
                    "api_peak_rss_bytes": resources["processes"]["api"][
                        "rss_peak_bytes"
                    ],
                    "client_cpu_cores": resources["processes"]["client"]["cpu_cores"],
                },
                "limits": {
                    "minimum_online_rate_percent": args.minimum_online_rate_percent,
                    "drain_p95_ms": round(drain_p95_limit, 3),
                    "recovered_p95_ms": round(recovered_p95_limit, 3),
                },
                "reconciliation": reconciliation,
                "acceptance": acceptance,
            }
    except BaseException:
        for process in processes:
            print(process.log_tail(), file=sys.stderr)
        raise
    finally:
        if monitor is not None:
            monitor.stop()
        if resource_probe is not None and not resource_stopped:
            resource_probe.cancel()
        if load is not None and not load_stopped:
            try:
                load.stop()
            except RuntimeError:
                pass
        if router is not None:
            router.stop()
        for process in reversed(processes):
            process.stop()
        if created_database:
            drop_database(database_name)


def median(values: list[float]) -> float:
    require(values, "cannot calculate an empty median")
    return round(float(statistics.median(values)), 6)


def summarize_trials(
    trials: list[dict[str, Any]], replica_counts: list[int]
) -> dict[str, Any]:
    topologies: dict[str, Any] = {}
    for replicas in replica_counts:
        matching = [trial for trial in trials if trial["replicas"] == replicas]
        require(matching, f"missing trials for {replicas} Worker replicas")
        topologies[str(replicas)] = {
            "trials": len(matching),
            "drain_seconds_median": median(
                [float(trial["backlog"]["drain_seconds"]) for trial in matching]
            ),
            "jobs_per_second_median": median(
                [float(trial["backlog"]["jobs_per_second"]) for trial in matching]
            ),
            "drain_online_rate_percent_median": median(
                [
                    float(trial["online"]["phases"]["drain"]["target_rate_percent"])
                    for trial in matching
                ]
            ),
            "steady_p95_ms_median": median(
                [
                    float(trial["online"]["phases"]["steady"]["latency"]["p95_ms"])
                    for trial in matching
                ]
            ),
            "drain_p95_ms_median": median(
                [
                    float(trial["online"]["phases"]["drain"]["latency"]["p95_ms"])
                    for trial in matching
                ]
            ),
            "recovered_p95_ms_median": median(
                [
                    float(trial["online"]["phases"]["recovered"]["latency"]["p95_ms"])
                    for trial in matching
                ]
            ),
            "worker_cpu_cores_median": median(
                [
                    float(trial["resource_summary"]["worker_cpu_cores"])
                    for trial in matching
                ]
            ),
            "worker_peak_rss_bytes_median": median(
                [
                    float(trial["resource_summary"]["worker_peak_rss_bytes"])
                    for trial in matching
                ]
            ),
            "maximum_postgres_connections": max(
                int(trial["backlog"]["maximum_postgres_connections"])
                for trial in matching
            ),
            "all_trials_passed": all(
                trial["acceptance"]["passed"] for trial in matching
            ),
        }

    baseline = float(topologies["1"]["jobs_per_second_median"])
    for replicas in replica_counts:
        topology = topologies[str(replicas)]
        speedup = float(topology["jobs_per_second_median"]) / baseline
        topology["speedup_vs_1"] = round(speedup, 6)
        topology["scaling_efficiency"] = round(speedup / replicas, 6)
    return topologies


def run(args: argparse.Namespace) -> dict[str, Any]:
    binary = resolve_binary(args.server_bin)
    started_at = datetime.now(timezone.utc)
    run_id = uuid.uuid4().hex[:10]
    log_directory = REPO_ROOT / ".sisyphus" / "evidence" / f"worker-backlog-{run_id}"
    trials: list[dict[str, Any]] = []
    execution_order: list[dict[str, int]] = []

    for round_index in range(args.trials):
        order = args.replicas if round_index % 2 == 0 else list(reversed(args.replicas))
        for order_index, replicas in enumerate(order):
            execution_order.append(
                {
                    "round": round_index + 1,
                    "order": order_index + 1,
                    "replicas": replicas,
                }
            )
            trials.append(
                run_trial(
                    args,
                    binary,
                    run_id,
                    round_index,
                    order_index,
                    replicas,
                    log_directory,
                )
            )

    summary = summarize_trials(trials, args.replicas)
    completed_at = datetime.now(timezone.utc)
    acceptance = {
        "trial_count_complete": len(trials) == len(args.replicas) * args.trials,
        "all_trials_passed": all(trial["acceptance"]["passed"] for trial in trials),
        "all_topologies_passed": all(
            topology["all_trials_passed"] for topology in summary.values()
        ),
    }
    acceptance["passed"] = all(acceptance.values())
    return {
        "schema_version": 1,
        "scenario": "worker_backlog_recovery_under_online_load",
        "started_at": started_at.isoformat(),
        "completed_at": completed_at.isoformat(),
        "elapsed_seconds": round((completed_at - started_at).total_seconds(), 3),
        "git": git_metadata(),
        "environment": environment_metadata(binary),
        "parameters": {
            "replicas": args.replicas,
            "trials": args.trials,
            "backlog_jobs": args.backlog_jobs,
            "job_bytes": args.job_bytes,
            "request_rate": args.request_rate,
            "load_concurrency": args.load_concurrency,
            "warmup_requests": args.warmup_requests,
            "steady_seconds": args.steady_seconds,
            "recovery_seconds": args.recovery_seconds,
            "timeout_seconds": args.timeout_seconds,
            "minimum_online_rate_percent": args.minimum_online_rate_percent,
            "drain_p95_ratio": args.drain_p95_ratio,
            "drain_p95_floor_ms": args.drain_p95_floor_ms,
            "recovery_p95_ratio": args.recovery_p95_ratio,
            "recovery_p95_floor_ms": args.recovery_p95_floor_ms,
            "worker_concurrency": 1,
            "effective_claim_batch_size": 1,
        },
        "topology": {
            "api_instances_per_trial": 1,
            "worker_replicas": args.replicas,
            "storage_backend": "isolated local directory per trial",
            "queue": "PostgreSQL storage_jobs",
            "online_operation": "authenticated GET /api/user/profile",
        },
        "execution_order": execution_order,
        "trials": trials,
        "summary": summary,
        "acceptance": acceptance,
    }


def terminal_result(result: dict[str, Any], output: Path) -> dict[str, Any]:
    return {
        "scenario": result["scenario"],
        "output": str(output),
        "elapsed_seconds": result["elapsed_seconds"],
        "git": result["git"],
        "summary": result["summary"],
        "acceptance": result["acceptance"],
    }


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (
        AssertionError,
        OSError,
        subprocess.SubprocessError,
        httpx.HTTPError,
    ) as error:
        print(f"worker backlog benchmark failed: {error}", file=sys.stderr)
        return 1

    serialized = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8")
        print(
            json.dumps(
                terminal_result(result, args.output), ensure_ascii=False, indent=2
            )
        )
    else:
        print(serialized, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Measure dependency-free API throughput across 1/2/N real processes."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

from test_expand_mixed_version import (
    INIT_SQL,
    ManagedServer,
    allocate_ports,
    connect,
    create_database,
    current_build_directory,
    drop_database,
    require,
    resolve_current_binary,
    run_database_command,
    server_config,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
REQUEST_FILE = REPO_ROOT / "test" / "benchmark" / "requests" / "health.json"
ROUTE_PATH = "/api/health/live"
PRESS_TOTALS = re.compile(
    r"TOTALS:\s+(?P<connections>\d+) connect,\s+"
    r"(?P<requests>\d+) requests,\s+(?P<success>\d+) success,\s+"
    r"(?P<fail>\d+) fail"
)
PRESS_TIMING = re.compile(
    r"TIMING:\s+(?P<duration>[0-9.]+) seconds,\s+"
    r"(?P<rps>[0-9.]+) rps,\s+(?P<average>[0-9.]+) ms avg req time"
)
DB_POOL_SIZE = 6
REDIS_POOL_SIZE = 4


def parse_replica_counts(raw: str) -> list[int]:
    try:
        values = [int(item.strip()) for item in raw.split(",") if item.strip()]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "replicas must be comma-separated integers"
        ) from error
    if len(values) < 2 or values[0] != 1 or values != sorted(set(values)):
        raise argparse.ArgumentTypeError(
            "replicas must be unique, increasing, and begin with 1"
        )
    if 2 not in values or values[-1] > 16:
        raise argparse.ArgumentTypeError("replicas must include 2 and cannot exceed 16")
    return values


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--replicas", type=parse_replica_counts, default=[1, 2, 4])
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--requests", type=int, default=60_000)
    parser.add_argument("--concurrency", type=int, default=256)
    parser.add_argument("--client-threads", type=int, default=8)
    parser.add_argument("--api-threads", type=int, default=2)
    parser.add_argument("--warmup-requests", type=int, default=4_000)
    parser.add_argument("--warmup-concurrency", type=int, default=64)
    parser.add_argument("--target-gain-percent", type=float, default=70.0)
    parser.add_argument("--server-bin", type=Path)
    parser.add_argument("--press-bin", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--enforce-target", action="store_true")
    args = parser.parse_args()

    maximum = max(args.replicas)
    positive_values = {
        "trials": args.trials,
        "requests": args.requests,
        "concurrency": args.concurrency,
        "client_threads": args.client_threads,
        "api_threads": args.api_threads,
        "warmup_requests": args.warmup_requests,
        "warmup_concurrency": args.warmup_concurrency,
    }
    for name, value in positive_values.items():
        if value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.trials < 3:
        parser.error("--trials must be at least 3")
    if args.requests < maximum or args.warmup_requests < maximum:
        parser.error("request counts must be at least the maximum replica count")
    if args.concurrency < maximum or args.warmup_concurrency < maximum:
        parser.error("concurrency must be at least the maximum replica count")
    if args.client_threads < maximum:
        parser.error("--client-threads must be at least the maximum replica count")
    if not 0 <= args.target_gain_percent <= 1000:
        parser.error("--target-gain-percent must be in range 0..1000")
    for replicas in args.replicas:
        concurrency = split_total(args.concurrency, replicas)
        threads = split_total(args.client_threads, replicas)
        if any(
            thread_count > connection_count
            for thread_count, connection_count in zip(threads, concurrency, strict=True)
        ):
            parser.error("per-instance client threads cannot exceed concurrency")
    return args


def split_total(total: int, parts: int) -> list[int]:
    quotient, remainder = divmod(total, parts)
    return [quotient + (1 if index < remainder else 0) for index in range(parts)]


def resolve_executable(
    configured: Path | None, candidates: list[Path], label: str
) -> Path:
    paths = ([configured] if configured is not None else []) + candidates
    for candidate in paths:
        path = candidate if candidate.is_absolute() else REPO_ROOT / candidate
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
    raise RuntimeError(f"cannot locate executable for {label}")


def resolve_press_binary(current_binary: Path, configured: Path | None) -> Path:
    build_root = current_build_directory(current_binary)
    discovered = shutil.which("drogon_ctl")
    candidates = [
        build_root
        / "vcpkg_installed"
        / "x64-linux"
        / "tools"
        / "drogon"
        / "drogon_ctl",
    ]
    if discovered:
        candidates.append(Path(discovered))
    return resolve_executable(configured, candidates, "drogon_ctl press")


def file_sha256(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            hasher.update(block)
    return hasher.hexdigest()


def command_output(command: list[str]) -> str:
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else "unavailable"


def git_metadata() -> dict[str, Any]:
    commit = command_output(["git", "rev-parse", "HEAD"])
    status = command_output(["git", "status", "--porcelain"])
    return {"commit": commit, "dirty": status not in ("", "unavailable")}


def cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def physical_cpu_count() -> int | None:
    output = command_output(["lscpu", "--parse=core,socket"])
    if output == "unavailable":
        return None
    pairs: set[tuple[str, str]] = set()
    for line in output.splitlines():
        if line.startswith("#") or "," not in line:
            continue
        core, socket = line.split(",", 1)
        pairs.add((core, socket))
    return len(pairs) or None


def memory_total_bytes() -> int | None:
    try:
        for line in Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
            if line.startswith("MemTotal:"):
                return int(line.split()[1]) * 1024
    except (OSError, ValueError):
        pass
    return None


def build_type(current_binary: Path) -> str:
    cache = current_build_directory(current_binary) / "CMakeCache.txt"
    try:
        for line in cache.read_text(encoding="utf-8").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:"):
                return line.split("=", 1)[1]
    except OSError:
        pass
    return "unknown"


def environment_metadata(current_binary: Path, press_binary: Path) -> dict[str, Any]:
    load_average = os.getloadavg() if hasattr(os, "getloadavg") else None
    return {
        "kernel": platform.release(),
        "platform": platform.platform(),
        "cpu_model": cpu_model(),
        "logical_cpus": os.cpu_count(),
        "physical_cores": physical_cpu_count(),
        "memory_total_bytes": memory_total_bytes(),
        "load_average_at_report": list(load_average) if load_average else None,
        "build_type": build_type(current_binary),
        "server_binary": str(current_binary),
        "server_binary_sha256": file_sha256(current_binary),
        "press_binary": str(press_binary),
        "postgres_client": command_output(["psql", "--version"]),
        "redis_server": command_output(["redis-server", "--version"]),
    }


def process_sample(pid: int) -> dict[str, float | int] | None:
    try:
        stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        closing = stat.rfind(")")
        fields = stat[closing + 2 :].split()
        ticks = int(fields[11]) + int(fields[12])
        rss_pages = int(
            Path(f"/proc/{pid}/statm").read_text(encoding="utf-8").split()[1]
        )
        return {
            "cpu_seconds": ticks / os.sysconf("SC_CLK_TCK"),
            "rss_bytes": rss_pages * os.sysconf("SC_PAGE_SIZE"),
        }
    except (FileNotFoundError, IndexError, OSError, ValueError):
        return None


def host_cpu_sample() -> tuple[int, int]:
    values = Path("/proc/stat").read_text(encoding="utf-8").splitlines()[0].split()[1:]
    counters = [int(value) for value in values]
    idle = counters[3] + (counters[4] if len(counters) > 4 else 0)
    return sum(counters), idle


def loopback_bytes() -> int:
    for line in Path("/proc/net/dev").read_text(encoding="utf-8").splitlines():
        if ":" not in line:
            continue
        interface, counters = line.split(":", 1)
        if interface.strip() == "lo":
            values = counters.split()
            return int(values[0]) + int(values[8])
    raise RuntimeError("loopback interface is unavailable")


def postgres_connection_count(database_name: str) -> int:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT COUNT(*) AS count FROM pg_stat_activity "
            "WHERE datname = current_database() AND pid <> pg_backend_pid()"
        ).fetchone()
    require(row is not None, "failed to inspect PostgreSQL connections")
    return int(row["count"])


def parse_press_output(output: str, expected_requests: int) -> dict[str, int | float]:
    totals = PRESS_TOTALS.search(output)
    timing = PRESS_TIMING.search(output)
    if totals is None or timing is None:
        raise RuntimeError(f"unrecognized drogon_ctl press output:\n{output}")
    result: dict[str, int | float] = {
        "connections": int(totals.group("connections")),
        "requests": int(totals.group("requests")),
        "success": int(totals.group("success")),
        "fail": int(totals.group("fail")),
        "duration_seconds": float(timing.group("duration")),
        "reported_rps": float(timing.group("rps")),
        "average_request_ms": float(timing.group("average")),
    }
    if result["requests"] != expected_requests:
        raise RuntimeError(
            f"press request count changed: expected={expected_requests}, observed={result['requests']}"
        )
    if result["success"] != expected_requests or result["fail"] != 0:
        raise RuntimeError(f"press reported request failures: {result}")
    return result


def press_command(
    press_binary: Path,
    port: int,
    requests: int,
    concurrency: int,
    threads: int,
) -> list[str]:
    return [
        str(press_binary),
        "press",
        "-n",
        str(requests),
        "-c",
        str(concurrency),
        "-t",
        str(threads),
        "-f",
        str(REQUEST_FILE),
        f"http://127.0.0.1:{port}{ROUTE_PATH}",
    ]


def run_press_trial(
    *,
    database_name: str,
    servers: list[ManagedServer],
    ports: list[int],
    press_binary: Path,
    total_requests: int,
    total_concurrency: int,
    total_threads: int,
) -> dict[str, Any]:
    replicas = len(servers)
    request_counts = split_total(total_requests, replicas)
    concurrency_counts = split_total(total_concurrency, replicas)
    thread_counts = split_total(total_threads, replicas)
    api_pids = [server.pid for server in servers]
    api_start = {pid: process_sample(pid) for pid in api_pids}
    require(
        all(sample is not None for sample in api_start.values()),
        "API resource sample failed",
    )
    api_peak_rss = {pid: int(api_start[pid]["rss_bytes"]) for pid in api_pids}  # type: ignore[index]
    postgres_connections = postgres_connection_count(database_name)
    host_start = host_cpu_sample()
    loopback_start = loopback_bytes()

    processes: list[subprocess.Popen[str]] = []
    launch_times: list[float] = []
    wall_started = time.perf_counter()
    for port, requests, concurrency, threads in zip(
        ports,
        request_counts,
        concurrency_counts,
        thread_counts,
        strict=True,
    ):
        process = subprocess.Popen(
            press_command(press_binary, port, requests, concurrency, threads),
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        processes.append(process)
        launch_times.append(time.perf_counter())

    client_first_cpu: dict[int, float] = {}
    client_last_cpu: dict[int, float] = {}
    client_peak_rss: dict[int, int] = {}
    while any(process.poll() is None for process in processes):
        for pid in api_pids:
            sample = process_sample(pid)
            if sample is not None:
                api_peak_rss[pid] = max(api_peak_rss[pid], int(sample["rss_bytes"]))
        for process in processes:
            sample = process_sample(process.pid)
            if sample is None:
                continue
            cpu_seconds = float(sample["cpu_seconds"])
            client_first_cpu.setdefault(process.pid, cpu_seconds)
            client_last_cpu[process.pid] = cpu_seconds
            client_peak_rss[process.pid] = max(
                client_peak_rss.get(process.pid, 0), int(sample["rss_bytes"])
            )
        time.sleep(0.01)

    wall_seconds = time.perf_counter() - wall_started
    loopback_delta = loopback_bytes() - loopback_start
    host_end = host_cpu_sample()
    api_end = {pid: process_sample(pid) for pid in api_pids}
    require(
        all(sample is not None for sample in api_end.values()),
        "API exited during trial",
    )

    instance_results: list[dict[str, Any]] = []
    for index, (process, expected_requests) in enumerate(
        zip(processes, request_counts, strict=True)
    ):
        stdout, stderr = process.communicate()
        if process.returncode != 0:
            raise RuntimeError(
                f"press instance {index} exited with {process.returncode}:\n{stdout}\n{stderr}"
            )
        parsed = parse_press_output(stdout, expected_requests)
        parsed.update(
            {
                "instance_index": index,
                "port": ports[index],
                "assigned_concurrency": concurrency_counts[index],
                "assigned_client_threads": thread_counts[index],
            }
        )
        instance_results.append(parsed)

    for server in servers:
        server.require_running("API scaling trial")

    successful = sum(int(result["success"]) for result in instance_results)
    failed = sum(int(result["fail"]) for result in instance_results)
    api_cpu_seconds = sum(
        float(api_end[pid]["cpu_seconds"]) - float(api_start[pid]["cpu_seconds"])  # type: ignore[index]
        for pid in api_pids
    )
    client_cpu_seconds = sum(
        max(0.0, client_last_cpu.get(pid, 0.0) - client_first_cpu.get(pid, 0.0))
        for pid in client_last_cpu
    )
    host_total_delta = host_end[0] - host_start[0]
    host_idle_delta = host_end[1] - host_start[1]
    host_busy_percent = (
        100 * (host_total_delta - host_idle_delta) / host_total_delta
        if host_total_delta > 0
        else 0.0
    )
    weighted_average_ms = (
        sum(
            float(result["average_request_ms"]) * int(result["requests"])
            for result in instance_results
        )
        / total_requests
    )
    return {
        "replicas": replicas,
        "requests": total_requests,
        "success": successful,
        "fail": failed,
        "error_rate_percent": round(100 * failed / total_requests, 6),
        "wall_seconds": round(wall_seconds, 6),
        "aggregate_success_rps": round(successful / wall_seconds, 3),
        "press_reported_rps_sum": round(
            sum(float(result["reported_rps"]) for result in instance_results), 3
        ),
        "weighted_average_request_ms": round(weighted_average_ms, 3),
        "launch_skew_ms": round(1000 * (max(launch_times) - min(launch_times)), 3),
        "api_cpu_seconds": round(api_cpu_seconds, 6),
        "api_cpu_cores": round(api_cpu_seconds / wall_seconds, 3),
        "api_host_capacity_percent": round(
            100 * api_cpu_seconds / (wall_seconds * (os.cpu_count() or 1)), 3
        ),
        "client_cpu_seconds": round(client_cpu_seconds, 6),
        "client_cpu_cores": round(client_cpu_seconds / wall_seconds, 3),
        "api_peak_rss_bytes": sum(api_peak_rss.values()),
        "client_peak_rss_bytes": sum(client_peak_rss.values()),
        "host_cpu_busy_percent": round(host_busy_percent, 3),
        "loopback_bytes": loopback_delta,
        "loopback_mib_per_second": round(
            loopback_delta / wall_seconds / (1024 * 1024), 3
        ),
        "postgres_connections": postgres_connections,
        "instances": instance_results,
    }


def topology_config(
    database_name: str,
    port: int,
    instance_id: str,
    final_root: Path,
    staging_root: Path,
    args: argparse.Namespace,
) -> dict[str, Any]:
    config = server_config(
        database_name,
        port,
        instance_id,
        final_root,
        staging_root,
        role="api",
    )
    config["app"]["threads_num"] = args.api_threads
    config["db_clients"][0]["connection_number"] = DB_POOL_SIZE
    config["db_clients"][0].pop("num_connection_number", None)
    config["redis_clients"][0]["number_of_connections"] = REDIS_POOL_SIZE
    return config


def stop_servers(servers: list[ManagedServer]) -> None:
    for server in servers:
        server.stop()
        if server.log_handle is not None:
            server.log_handle.close()
            server.log_handle = None


def run_topology_trial(
    *,
    round_index: int,
    replicas: int,
    database_name: str,
    temporary_root: Path,
    current_binary: Path,
    press_binary: Path,
    args: argparse.Namespace,
) -> dict[str, Any]:
    ports = allocate_ports(replicas)
    final_root = temporary_root / "storage" / "final"
    staging_root = temporary_root / "storage" / "staging"
    servers: list[ManagedServer] = []
    try:
        for index, port in enumerate(ports):
            instance_id = f"scaling-r{round_index}-{replicas}-api-{index}"
            server = ManagedServer(
                name=instance_id,
                binary=current_binary,
                run_directory=(
                    temporary_root
                    / "runs"
                    / f"round-{round_index}"
                    / f"replicas-{replicas}"
                    / f"api-{index}"
                ),
                config=topology_config(
                    database_name,
                    port,
                    instance_id,
                    final_root,
                    staging_root,
                    args,
                ),
                database_name=database_name,
                port=port,
                readiness_path="/api/health/ready",
                role="api",
            )
            servers.append(server)

        warmup_threads = max(
            replicas, min(args.client_threads, args.warmup_concurrency)
        )
        warmup = run_press_trial(
            database_name=database_name,
            servers=servers,
            ports=ports,
            press_binary=press_binary,
            total_requests=args.warmup_requests,
            total_concurrency=args.warmup_concurrency,
            total_threads=warmup_threads,
        )
        measurement = run_press_trial(
            database_name=database_name,
            servers=servers,
            ports=ports,
            press_binary=press_binary,
            total_requests=args.requests,
            total_concurrency=args.concurrency,
            total_threads=args.client_threads,
        )
        return {
            "round": round_index,
            "replicas": replicas,
            "warmup": warmup,
            "measurement": measurement,
        }
    except BaseException:
        for server in servers:
            print(server.log_tail(), file=sys.stderr)
        raise
    finally:
        stop_servers(servers)


def median_metric(trials: list[dict[str, Any]], field: str) -> float:
    return round(
        statistics.median(float(trial["measurement"][field]) for trial in trials),
        3,
    )


def summarize_trials(
    trials: list[dict[str, Any]],
    replica_counts: list[int],
    target_gain_percent: float,
    api_threads_per_instance: int,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    by_replica = {
        replicas: sorted(
            [trial for trial in trials if trial["replicas"] == replicas],
            key=lambda trial: trial["round"],
        )
        for replicas in replica_counts
    }
    summaries: dict[str, Any] = {}
    for replicas, topology_trials in by_replica.items():
        require(len(topology_trials) >= 3, f"replica {replicas} has too few trials")
        median_api_cpu_cores = median_metric(topology_trials, "api_cpu_cores")
        configured_http_threads = replicas * api_threads_per_instance
        summaries[str(replicas)] = {
            "trial_count": len(topology_trials),
            "median_success_rps": median_metric(
                topology_trials, "aggregate_success_rps"
            ),
            "min_success_rps": round(
                min(
                    float(trial["measurement"]["aggregate_success_rps"])
                    for trial in topology_trials
                ),
                3,
            ),
            "max_success_rps": round(
                max(
                    float(trial["measurement"]["aggregate_success_rps"])
                    for trial in topology_trials
                ),
                3,
            ),
            "median_average_request_ms": median_metric(
                topology_trials, "weighted_average_request_ms"
            ),
            "median_api_cpu_cores": median_api_cpu_cores,
            "configured_http_threads": configured_http_threads,
            "median_api_process_cpu_to_http_thread_budget_percent": round(
                100 * median_api_cpu_cores / configured_http_threads, 3
            ),
            "median_api_host_capacity_percent": median_metric(
                topology_trials, "api_host_capacity_percent"
            ),
            "median_client_cpu_cores": median_metric(
                topology_trials, "client_cpu_cores"
            ),
            "median_host_cpu_busy_percent": median_metric(
                topology_trials, "host_cpu_busy_percent"
            ),
            "median_loopback_mib_per_second": median_metric(
                topology_trials, "loopback_mib_per_second"
            ),
            "median_api_peak_rss_bytes": median_metric(
                topology_trials, "api_peak_rss_bytes"
            ),
            "postgres_connections": sorted(
                {
                    int(trial["measurement"]["postgres_connections"])
                    for trial in topology_trials
                }
            ),
            "total_failures": sum(
                int(trial["measurement"]["fail"]) for trial in topology_trials
            ),
        }

    baseline = float(summaries["1"]["median_success_rps"])
    scaling: dict[str, Any] = {}
    for replicas in replica_counts:
        throughput = float(summaries[str(replicas)]["median_success_rps"])
        speedup = throughput / baseline
        scaling[str(replicas)] = {
            "speedup": round(speedup, 4),
            "efficiency_percent": round(100 * speedup / replicas, 2),
        }
    gain = 100 * (float(summaries["2"]["median_success_rps"]) / baseline - 1)
    all_successful = all(
        summary["total_failures"] == 0 for summary in summaries.values()
    )
    acceptance = {
        "target_gain_1_to_2_percent": target_gain_percent,
        "observed_gain_1_to_2_percent": round(gain, 2),
        "all_formal_requests_succeeded": all_successful,
        "passed": all_successful and gain >= target_gain_percent,
    }

    maximum = str(max(replica_counts))
    max_summary = summaries[maximum]
    api_cpu = float(max_summary["median_api_cpu_cores"])
    api_to_http_thread_budget = float(
        max_summary["median_api_process_cpu_to_http_thread_budget_percent"]
    )
    api_host_capacity = float(max_summary["median_api_host_capacity_percent"])
    client_cpu = float(max_summary["median_client_cpu_cores"])
    host_busy = float(max_summary["median_host_cpu_busy_percent"])
    if host_busy >= 85:
        classification = "shared_host_cpu"
    elif client_cpu > api_cpu * 1.25:
        classification = "load_generator_or_loopback"
    elif api_to_http_thread_budget >= 75:
        classification = "api_process_cpu_or_http_event_loops"
    else:
        classification = "not_saturated_or_mixed_local_limit"
    bottleneck = {
        "classification": classification,
        "dependency_calls_in_workload": False,
        "evidence": {
            "maximum_replicas": int(maximum),
            "median_api_cpu_cores": api_cpu,
            "configured_http_threads": int(max_summary["configured_http_threads"]),
            "median_api_process_cpu_to_http_thread_budget_percent": (
                api_to_http_thread_budget
            ),
            "median_api_host_capacity_percent": api_host_capacity,
            "median_client_cpu_cores": client_cpu,
            "median_host_cpu_busy_percent": host_busy,
            "median_loopback_mib_per_second": max_summary[
                "median_loopback_mib_per_second"
            ],
        },
        "limitation": (
            "GET /api/health/live excludes PostgreSQL, Redis, and object storage; this "
            "classification only applies to local dependency-free HTTP throughput."
        ),
    }
    return (
        summaries,
        {"by_replicas": scaling},
        {"acceptance": acceptance, "bottleneck": bottleneck},
    )


def run(args: argparse.Namespace) -> dict[str, Any]:
    current_binary = (
        resolve_executable(args.server_bin, [], "Disk server")
        if args.server_bin is not None
        else resolve_current_binary()
    )
    press_binary = resolve_press_binary(current_binary, args.press_bin)
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_api_scaling_{suffix}"
    database_created = False
    started_at = datetime.now(timezone.utc)
    trials: list[dict[str, Any]] = []

    with tempfile.TemporaryDirectory(prefix="disk-api-scaling-") as temporary:
        temporary_root = Path(temporary)
        try:
            create_database(database_name)
            database_created = True
            run_database_command(
                ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
                database_name,
            )
            for round_index in range(args.trials):
                order = (
                    args.replicas
                    if round_index % 2 == 0
                    else list(reversed(args.replicas))
                )
                for replicas in order:
                    print(
                        f"round {round_index + 1}/{args.trials}: measuring {replicas} API replica(s)",
                        file=sys.stderr,
                    )
                    trials.append(
                        run_topology_trial(
                            round_index=round_index,
                            replicas=replicas,
                            database_name=database_name,
                            temporary_root=temporary_root,
                            current_binary=current_binary,
                            press_binary=press_binary,
                            args=args,
                        )
                    )
        finally:
            if database_created:
                drop_database(database_name)

    summaries, scaling, assessment = summarize_trials(
        trials,
        args.replicas,
        args.target_gain_percent,
        args.api_threads,
    )
    completed_at = datetime.now(timezone.utc)
    return {
        "schema_version": 1,
        "scenario": "dependency_free_api_horizontal_scaling",
        "started_at": started_at.isoformat(),
        "completed_at": completed_at.isoformat(),
        "elapsed_seconds": round((completed_at - started_at).total_seconds(), 3),
        "git": git_metadata(),
        "environment": environment_metadata(current_binary, press_binary),
        "parameters": {
            "replicas": args.replicas,
            "trials": args.trials,
            "requests_per_trial": args.requests,
            "aggregate_concurrency": args.concurrency,
            "aggregate_client_threads": args.client_threads,
            "api_threads_per_instance": args.api_threads,
            "db_pool_size_per_instance": DB_POOL_SIZE,
            "redis_pool_size_per_instance": REDIS_POOL_SIZE,
            "warmup_requests": args.warmup_requests,
            "warmup_concurrency": args.warmup_concurrency,
            "route": f"GET {ROUTE_PATH}",
            "routing": "equal direct-to-instance partition",
            "storage_backend": "local",
        },
        "trials": sorted(trials, key=lambda trial: (trial["round"], trial["replicas"])),
        "summary": summaries,
        "scaling": scaling,
        **assessment,
    }


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (AssertionError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"API scaling benchmark failed: {error}", file=sys.stderr)
        return 1

    serialized = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    if args.enforce_target and not result["acceptance"]["passed"]:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

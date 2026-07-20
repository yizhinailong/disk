#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "prometheus-client", "psycopg[binary]"]
# ///

"""Measure API and Worker process-death recovery under continuous load."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, IO

import httpx
from prometheus_client.parser import text_string_to_metric_families

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

from bench_api_scaling import git_metadata  # noqa: E402
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
    resolve_current_binary,
    run_database_command,
    server_config,
    server_environment,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
MIB = 1024 * 1024
PAYLOAD_BYTES = MIB
LEASE_SECONDS = 30
API_THREADS = 4
DB_POOL_SIZE = 6
REDIS_POOL_SIZE = 4
WORKER_POLL_INTERVAL_MS = 100
WORKER_CLAIM_BATCH_SIZE = 4
WORKER_CONCURRENCY = 1
FAULT_UPLOAD_MARKER = "Test fault injection paused upload after finalize claim"
FAULT_BLOB_GC_MARKER = "Test fault injection paused blob_gc after blob delete"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request-rate", type=float, default=40.0)
    parser.add_argument("--load-concurrency", type=int, default=8)
    parser.add_argument("--worker-load-jobs", type=int, default=64)
    parser.add_argument("--warmup-requests", type=int, default=80)
    parser.add_argument("--post-rejoin-requests", type=int, default=40)
    parser.add_argument("--minimum-logical-requests", type=int, default=1000)
    parser.add_argument("--timeout-seconds", type=float, default=90.0)
    parser.add_argument("--server-bin", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    positive = {
        "request_rate": args.request_rate,
        "load_concurrency": args.load_concurrency,
        "worker_load_jobs": args.worker_load_jobs,
        "warmup_requests": args.warmup_requests,
        "post_rejoin_requests": args.post_rejoin_requests,
        "minimum_logical_requests": args.minimum_logical_requests,
        "timeout_seconds": args.timeout_seconds,
    }
    for name, value in positive.items():
        if value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.request_rate > 500:
        parser.error("--request-rate cannot exceed 500")
    if args.load_concurrency > 128:
        parser.error("--load-concurrency cannot exceed 128")
    if args.worker_load_jobs > 1000:
        parser.error("--worker-load-jobs cannot exceed 1000")
    if args.timeout_seconds < LEASE_SECONDS + 10:
        parser.error(f"--timeout-seconds must be at least {LEASE_SECONDS + 10}")
    return args


def resolve_binary(configured: Path | None) -> Path:
    if configured is None:
        return resolve_current_binary()
    path = configured if configured.is_absolute() else REPO_ROOT / configured
    require(
        path.is_file() and os.access(path, os.X_OK),
        f"server binary is unavailable: {path}",
    )
    return path.resolve()


def json_ready(value: Any) -> Any:
    return json.loads(json.dumps(value, default=str))


def response_payload(response: httpx.Response, label: str) -> dict[str, Any]:
    try:
        body = response.json()
    except ValueError as error:
        raise AssertionError(
            f"{label} returned non-JSON HTTP {response.status_code}: {response.text[:300]}"
        ) from error
    require(isinstance(body, dict), f"{label} response is not an object")
    return body


def success_data(response: httpx.Response, label: str) -> dict[str, Any]:
    body = response_payload(response, label)
    require(
        response.status_code in (200, 201) and str(body.get("code")) == "0",
        f"{label} failed: HTTP {response.status_code}, body={body}",
    )
    data = body.get("data")
    require(isinstance(data, dict), f"{label} returned invalid data")
    return data


def auth_headers(token: str, content_type: str = "application/json") -> dict[str, str]:
    return {
        "Authorization": f"Bearer {token}",
        "Content-Type": content_type,
    }


def wait_until(
    label: str,
    predicate: Callable[[], Any],
    timeout_seconds: float,
    *,
    interval: float = 0.05,
    processes: tuple["ManagedDiskProcess", ...] = (),
) -> Any:
    deadline = time.monotonic() + timeout_seconds
    last_value: Any = None
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        for process in processes:
            process.require_running(label)
        try:
            last_value = predicate()
            if last_value:
                return last_value
        except (httpx.HTTPError, OSError) as error:
            last_error = error
        time.sleep(interval)
    suffix = f", last_error={last_error}" if last_error is not None else ""
    raise AssertionError(f"timed out waiting for {label}: last={last_value}{suffix}")


def topology_config(
    database_name: str,
    port: int,
    instance_id: str,
    role: str,
    final_root: Path,
    staging_root: Path,
    process_upload_root: Path,
) -> dict[str, Any]:
    config = server_config(
        database_name,
        port,
        instance_id,
        final_root,
        staging_root,
        role=role,
    )
    config["app"]["threads_num"] = API_THREADS
    config["app"]["upload_path"] = str(process_upload_root)
    config["app"]["client_max_body_size"] = "4M"
    disk = config["custom_config"]["disk"]
    disk.update(
        {
            "chunk_size": MIB,
            "max_file_size": 4 * MIB,
            "upload_finalize_lease_seconds": LEASE_SECONDS,
            "worker_poll_interval_ms": WORKER_POLL_INTERVAL_MS,
            "worker_claim_batch_size": WORKER_CLAIM_BATCH_SIZE,
            "worker_concurrency": WORKER_CONCURRENCY,
            "worker_lease_duration_seconds": LEASE_SECONDS,
            "worker_drain_timeout_seconds": 2,
            "assembly_max_concurrent": 2,
            "assemble_buffer_size_bytes": 64 * 1024,
        }
    )
    config["db_clients"][0]["connection_number"] = DB_POOL_SIZE
    config["db_clients"][0].pop("num_connection_number", None)
    config["redis_clients"][0]["number_of_connections"] = REDIS_POOL_SIZE
    return config


class ManagedDiskProcess:
    def __init__(
        self,
        *,
        name: str,
        role: str,
        binary: Path,
        run_directory: Path,
        config: dict[str, Any],
        database_name: str,
        port: int,
        extra_environment: dict[str, str] | None = None,
        log_directory: Path | None = None,
    ) -> None:
        self.name = name
        self.role = role
        self.binary = binary
        self.run_directory = run_directory
        self.database_name = database_name
        self.port = port
        self.base_url = f"http://127.0.0.1:{port}"
        self.config_path = run_directory / "config.json"
        self.log_path = (log_directory or run_directory) / f"{name}.log"
        self.config = config
        self.extra_environment = dict(extra_environment or {})
        self.process: subprocess.Popen[bytes] | None = None
        self.log_handle: IO[bytes] | None = None
        self.start_history: list[dict[str, Any]] = []

        run_directory.mkdir(parents=True, exist_ok=False)
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.config_path.write_text(
            json.dumps(config, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        self.start()

    @property
    def pid(self) -> int:
        require(self.process is not None, f"{self.name} has not started")
        return self.process.pid

    def environment(self) -> dict[str, str]:
        environment = server_environment(
            self.config_path,
            self.database_name,
            self.port,
            self.name,
            role=self.role,
        )
        for key in (
            "DISK_TEST_FAULT_INJECTION",
            "DISK_TEST_PAUSE_AFTER_FINALIZE_CLAIM_UPLOAD_ID",
            "DISK_TEST_PAUSE_AFTER_ASSEMBLY_UPLOAD_ID",
            "DISK_TEST_PAUSE_AFTER_FINALIZE_COMMIT_UPLOAD_ID",
            "DISK_TEST_PAUSE_AFTER_BLOB_DELETE_JOB_ID",
        ):
            environment.pop(key, None)
        environment.update(self.extra_environment)
        return environment

    def start(self, extra_environment: dict[str, str] | None = None) -> None:
        if self.process is not None and self.process.poll() is None:
            raise AssertionError(f"{self.name} is already running")
        if extra_environment is not None:
            self.extra_environment = dict(extra_environment)
        self.log_handle = self.log_path.open("ab")
        try:
            self.process = subprocess.Popen(
                [str(self.binary)],
                cwd=self.run_directory,
                env=self.environment(),
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
            )
            started_at = time.monotonic()
            self.wait_ready()
            self.start_history.append(
                {
                    "pid": self.pid,
                    "started_at": started_at,
                    "ready_at": time.monotonic(),
                }
            )
        except BaseException:
            self.stop()
            raise

    def wait_ready(self) -> None:
        def probe() -> bool:
            if self.process is not None and self.process.poll() is not None:
                raise AssertionError(
                    f"{self.name} exited during startup with {self.process.returncode}\n"
                    f"{self.log_tail()}"
                )
            try:
                response = httpx.get(self.base_url + "/api/health/ready", timeout=1)
                if response.status_code != 200:
                    return False
                data = response_payload(response, f"{self.name} readiness").get(
                    "data", {}
                )
                return (
                    data.get("overall_status") == "healthy"
                    and data.get("role") == self.role
                    and data.get("instance_id") == self.name
                )
            except httpx.HTTPError:
                return False

        wait_until(f"{self.name} readiness", probe, 45, interval=0.2)

    def contains_log(self, marker: str) -> bool:
        if self.log_handle is not None:
            self.log_handle.flush()
        try:
            return marker in self.log_path.read_text(encoding="utf-8", errors="replace")
        except FileNotFoundError:
            return False

    def require_running(self, label: str) -> None:
        require(
            self.process is not None and self.process.poll() is None,
            f"{self.name} exited during {label}\n{self.log_tail()}",
        )

    def kill(self) -> int:
        self.require_running("SIGKILL injection")
        assert self.process is not None
        self.process.kill()
        self.process.wait(timeout=5)
        returncode = int(self.process.returncode)
        self._close_log()
        return returncode

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self._close_log()

    def _close_log(self) -> None:
        if self.log_handle is not None:
            self.log_handle.close()
            self.log_handle = None

    def log_tail(self, lines: int = 100) -> str:
        if self.log_handle is not None:
            self.log_handle.flush()
        try:
            content = self.log_path.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()
        except FileNotFoundError:
            return f"{self.name} log unavailable"
        return f"{self.name} log tail:\n" + "\n".join(content[-lines:])


class PhaseTracker:
    def __init__(self, initial: str) -> None:
        self._phase = initial
        self._lock = threading.Lock()
        self.transitions: list[dict[str, Any]] = [
            {"phase": initial, "at": time.monotonic()}
        ]

    def get(self) -> str:
        with self._lock:
            return self._phase

    def set(self, phase: str) -> float:
        now = time.monotonic()
        with self._lock:
            self._phase = phase
            self.transitions.append({"phase": phase, "at": now})
        return now


class HealthAwareRouter:
    def __init__(self, endpoints: dict[str, str]) -> None:
        self.endpoints = dict(endpoints)
        self._healthy = {name: True for name in endpoints}
        self._round_robin = 0
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self.transitions: list[dict[str, Any]] = []

    def start(self) -> None:
        self._thread = threading.Thread(target=self._probe_loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=3)

    def choose(self, excluded: set[str] | None = None) -> tuple[str, str]:
        blocked = excluded or set()
        with self._lock:
            candidates = sorted(
                name
                for name, healthy in self._healthy.items()
                if healthy and name not in blocked
            )
            if not candidates:
                raise RuntimeError("no healthy API endpoint is available")
            name = candidates[self._round_robin % len(candidates)]
            self._round_robin += 1
            return name, self.endpoints[name]

    def mark_unhealthy(self, name: str, reason: str) -> None:
        with self._lock:
            if not self._healthy.get(name, False):
                return
            self._healthy[name] = False
            self.transitions.append(
                {
                    "endpoint": name,
                    "healthy": False,
                    "reason": reason,
                    "at": time.monotonic(),
                }
            )

    def is_healthy(self, name: str) -> bool:
        with self._lock:
            return bool(self._healthy.get(name, False))

    def _mark_healthy(self, name: str) -> None:
        with self._lock:
            if self._healthy.get(name, False):
                return
            self._healthy[name] = True
            self.transitions.append(
                {
                    "endpoint": name,
                    "healthy": True,
                    "reason": "readiness probe recovered",
                    "at": time.monotonic(),
                }
            )

    def _probe_loop(self) -> None:
        while not self._stop.wait(0.1):
            with self._lock:
                unhealthy = [
                    (name, self.endpoints[name])
                    for name, healthy in self._healthy.items()
                    if not healthy
                ]
            for name, base_url in unhealthy:
                try:
                    response = httpx.get(
                        base_url + "/api/health/ready",
                        timeout=httpx.Timeout(0.5, connect=0.2),
                    )
                    if response.status_code == 200:
                        self._mark_healthy(name)
                except httpx.HTTPError:
                    continue


class ContinuousLoad:
    def __init__(
        self,
        *,
        router: HealthAwareRouter,
        phase: PhaseTracker,
        token: str,
        expected_username: str,
        request_rate: float,
        concurrency: int,
    ) -> None:
        self.router = router
        self.phase = phase
        self.token = token
        self.expected_username = expected_username
        self.request_rate = request_rate
        self.concurrency = concurrency
        self._stop = threading.Event()
        self._slots = threading.Semaphore(concurrency)
        self._executor = concurrent.futures.ThreadPoolExecutor(max_workers=concurrency)
        self._scheduler: threading.Thread | None = None
        self._thread_local = threading.local()
        self._results: list[dict[str, Any]] = []
        self._lock = threading.Lock()
        self._next_id = 0

    def start(self) -> None:
        self._scheduler = threading.Thread(target=self._schedule, daemon=True)
        self._scheduler.start()

    def stop(self) -> None:
        self._stop.set()
        if self._scheduler is not None:
            self._scheduler.join(timeout=5)
        self._executor.shutdown(wait=True, cancel_futures=False)

    def snapshot(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self._results)

    def count(self) -> int:
        with self._lock:
            return len(self._results)

    def successful_on(self, endpoint: str, *, after: float | None = None) -> int:
        with self._lock:
            return sum(
                1
                for result in self._results
                if result["success"]
                and result["success_endpoint"] == endpoint
                and (after is None or result["ended_at"] >= after)
            )

    def _client(self) -> httpx.Client:
        client = getattr(self._thread_local, "client", None)
        if client is None:
            client = httpx.Client(
                timeout=httpx.Timeout(2.0, connect=0.5),
                limits=httpx.Limits(max_connections=4, max_keepalive_connections=2),
            )
            self._thread_local.client = client
        return client

    def _schedule(self) -> None:
        interval = 1.0 / self.request_rate
        next_start = time.monotonic()
        while not self._stop.is_set():
            delay = next_start - time.monotonic()
            if delay > 0 and self._stop.wait(delay):
                break
            if not self._slots.acquire(timeout=0.1):
                next_start = time.monotonic() + interval
                continue
            with self._lock:
                logical_id = self._next_id
                self._next_id += 1
            self._executor.submit(self._run_and_release, logical_id)
            next_start = max(next_start + interval, time.monotonic())

    def _run_and_release(self, logical_id: int) -> None:
        try:
            result = self._run_one(logical_id)
            with self._lock:
                self._results.append(result)
        finally:
            self._slots.release()

    def _run_one(self, logical_id: int) -> dict[str, Any]:
        logical_started = time.monotonic()
        phase = self.phase.get()
        attempts: list[dict[str, Any]] = []
        excluded: set[str] = set()
        success_endpoint: str | None = None

        for attempt_number in (1, 2):
            attempt_started = time.monotonic()
            try:
                endpoint, base_url = self.router.choose(excluded)
            except RuntimeError as error:
                attempts.append(
                    {
                        "attempt": attempt_number,
                        "endpoint": None,
                        "started_at": attempt_started,
                        "ended_at": time.monotonic(),
                        "outcome": "no_endpoint",
                        "error_type": type(error).__name__,
                    }
                )
                break
            excluded.add(endpoint)
            try:
                response = self._client().get(
                    base_url + "/api/user/profile",
                    headers={"Authorization": f"Bearer {self.token}"},
                )
                ended = time.monotonic()
                try:
                    body = response.json()
                except ValueError:
                    body = {}
                user = (
                    body.get("data", {}).get("user", {})
                    if isinstance(body, dict)
                    else {}
                )
                success = (
                    response.status_code == 200
                    and isinstance(body, dict)
                    and str(body.get("code")) == "0"
                    and isinstance(user, dict)
                    and user.get("username") == self.expected_username
                )
                attempts.append(
                    {
                        "attempt": attempt_number,
                        "endpoint": endpoint,
                        "started_at": attempt_started,
                        "ended_at": ended,
                        "latency_ms": round((ended - attempt_started) * 1000, 3),
                        "outcome": "success" if success else "http_error",
                        "http_status": response.status_code,
                        "domain_code": str(body.get("code"))
                        if isinstance(body, dict) and "code" in body
                        else None,
                    }
                )
                if success:
                    success_endpoint = endpoint
                    break
            except httpx.HTTPError as error:
                ended = time.monotonic()
                attempts.append(
                    {
                        "attempt": attempt_number,
                        "endpoint": endpoint,
                        "started_at": attempt_started,
                        "ended_at": ended,
                        "latency_ms": round((ended - attempt_started) * 1000, 3),
                        "outcome": "transport_error",
                        "error_type": type(error).__name__,
                    }
                )
                self.router.mark_unhealthy(endpoint, type(error).__name__)

        logical_ended = time.monotonic()
        return {
            "logical_id": logical_id,
            "phase": phase,
            "started_at": logical_started,
            "ended_at": logical_ended,
            "latency_ms": round((logical_ended - logical_started) * 1000, 3),
            "success": success_endpoint is not None,
            "success_endpoint": success_endpoint,
            "attempts": attempts,
        }


MetricSnapshot = dict[tuple[str, tuple[tuple[str, str], ...]], float]


def read_metrics(base_url: str) -> MetricSnapshot:
    response = httpx.get(base_url + "/metrics", timeout=10)
    response.raise_for_status()
    result: MetricSnapshot = {}
    for family in text_string_to_metric_families(response.text):
        for sample in family.samples:
            labels = tuple(sorted((str(k), str(v)) for k, v in sample.labels.items()))
            result[(sample.name, labels)] = float(sample.value)
    return result


def metric_value(snapshot: MetricSnapshot, name: str, **labels: str) -> float:
    return snapshot.get((name, tuple(sorted(labels.items()))), 0.0)


def metric_delta(
    before: MetricSnapshot,
    after: MetricSnapshot,
    name: str,
    **labels: str,
) -> float:
    value = metric_value(after, name, **labels) - metric_value(before, name, **labels)
    require(value >= 0, f"metric decreased: {name}{labels}")
    return value


def storage_job_metrics(
    before: MetricSnapshot,
    after: MetricSnapshot,
    job_type: str,
) -> dict[str, Any]:
    outcomes = {
        outcome: int(
            metric_delta(
                before,
                after,
                "disk_storage_job_runs_total",
                job_type=job_type,
                outcome=outcome,
            )
        )
        for outcome in ("succeeded", "retry", "dead_letter", "ownership_lost")
    }
    return {
        "outcomes": outcomes,
        "takeovers": int(
            metric_delta(
                before,
                after,
                "disk_storage_job_takeovers_total",
                job_type=job_type,
            )
        ),
    }


def login(base_url: str) -> tuple[str, str]:
    response = httpx.post(
        base_url + "/api/auth/login",
        json={"account": "admin", "password": "Admin123"},
        timeout=30,
    )
    data = success_data(response, "admin login")
    token = data.get("access_token")
    require(isinstance(token, str) and token, "admin login returned no access token")
    return token, "admin"


def prepare_upload(base_url: str, token: str, filename: str, payload: bytes) -> str:
    init_response = httpx.post(
        base_url + "/api/file/upload/init",
        headers=auth_headers(token),
        json={
            "filename": filename,
            "file_size": len(payload),
            "file_hash": hashlib.md5(payload).hexdigest(),
            "parent_id": 0,
        },
        timeout=20,
    )
    init_data = success_data(init_response, "fault upload init")
    require(init_data.get("instant_upload") is False, "fault upload was deduplicated")
    upload_id = init_data.get("upload_id")
    require(isinstance(upload_id, str) and upload_id, "fault upload has no ID")

    chunk_response = httpx.post(
        base_url + "/api/file/upload/chunk",
        params={
            "upload_id": upload_id,
            "chunk_index": 0,
            "chunk_hash": hashlib.md5(payload).hexdigest(),
        },
        headers=auth_headers(token, "application/octet-stream"),
        content=payload,
        timeout=20,
    )
    chunk_data = success_data(chunk_response, "fault upload chunk")
    require(chunk_data.get("uploaded") is True, "fault upload chunk was not accepted")
    return upload_id


def complete_request(base_url: str, token: str, upload_id: str) -> dict[str, Any]:
    started = time.monotonic()
    try:
        response = httpx.post(
            base_url + "/api/file/upload/complete",
            headers=auth_headers(token),
            json={"upload_id": upload_id},
            timeout=120,
        )
        ended = time.monotonic()
        body = response_payload(response, "complete request")
        return {
            "started_at": started,
            "ended_at": ended,
            "latency_ms": round((ended - started) * 1000, 3),
            "outcome": "response",
            "http_status": response.status_code,
            "domain_code": str(body.get("code")),
            "body": body,
        }
    except httpx.HTTPError as error:
        ended = time.monotonic()
        return {
            "started_at": started,
            "ended_at": ended,
            "latency_ms": round((ended - started) * 1000, 3),
            "outcome": "transport_error",
            "error_type": type(error).__name__,
        }


def retry_complete_until_success(
    base_url: str,
    token: str,
    upload_id: str,
    timeout_seconds: float,
) -> tuple[int, list[dict[str, Any]], float]:
    deadline = time.monotonic() + timeout_seconds
    attempts: list[dict[str, Any]] = []
    while time.monotonic() < deadline:
        result = complete_request(base_url, token, upload_id)
        attempts.append(result)
        if (
            result.get("outcome") == "response"
            and result.get("http_status") == 200
            and result.get("domain_code") == "0"
        ):
            body = result["body"]
            file_data = body.get("data", {}).get("file", {})
            file_id = file_data.get("id")
            require(
                isinstance(file_id, int) and file_id > 0, "takeover returned no file ID"
            )
            return file_id, attempts, float(result["ended_at"])
        require(
            result.get("outcome") == "response"
            and result.get("http_status") == 409
            and result.get("domain_code") == "10004",
            f"unexpected complete retry result: {result}",
        )
        time.sleep(0.25)
    raise AssertionError(f"complete takeover did not succeed: attempts={attempts[-5:]}")


def upload_claim_snapshot(database_name: str, upload_id: str) -> dict[str, Any] | None:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT status, state_version, finalize_attempts, lease_owner, "
            "lease_expires_at, lease_expires_at > NOW() AS lease_live, NOW() AS database_now "
            "FROM upload_tasks WHERE id = %s",
            (upload_id,),
        ).fetchone()
    return dict(row) if row is not None else None


@dataclass(frozen=True)
class BlobGcFixture:
    content_id: int
    job_id: int
    path: Path
    dedupe_key: str


def create_blob_gc_fixture(
    database_name: str,
    final_root: Path,
    run_id: str,
) -> BlobGcFixture:
    payload = (f"fault-blob-gc-{run_id}:".encode() * 256)[:4096]
    md5_hash = hashlib.md5(payload).hexdigest()
    sha256_hash = hashlib.sha256(payload).hexdigest()
    path = final_root / "sha256" / sha256_hash[:2] / f"{sha256_hash}.bin"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)

    with connect(database_name) as connection:
        with connection.transaction():
            content = connection.execute(
                "INSERT INTO file_contents "
                "(hash_md5, hash_sha256, size, storage_path, mime_type, ref_count) "
                "VALUES (%s, %s, %s, %s, 'application/octet-stream', 0) RETURNING id",
                (md5_hash, sha256_hash, len(payload), str(path)),
            ).fetchone()
            require(content is not None, "failed to insert Blob GC content")
            content_id = int(content["id"])
            dedupe_key = f"blob-gc:{content_id}"
            connection.execute(
                "INSERT INTO storage_reconciliation_findings "
                "(finding_type, resource_id, resource_locator, severity, "
                "resolution_strategy, details) "
                "VALUES ('zero_reference_content', %s, %s, 1, 'auto_gc', '{}'::jsonb)",
                (str(content_id), str(path)),
            )
            job = connection.execute(
                "INSERT INTO storage_jobs "
                "(job_type, aggregate_id, dedupe_key, payload, max_attempts) "
                "VALUES ('blob_gc', %s, %s, %s::jsonb, 8) RETURNING id",
                (
                    str(content_id),
                    dedupe_key,
                    json.dumps({"content_id": content_id, "storage_path": str(path)}),
                ),
            ).fetchone()
            require(job is not None, "failed to insert Blob GC job")
    return BlobGcFixture(content_id, int(job["id"]), path, dedupe_key)


def blob_gc_snapshot(
    database_name: str,
    fixture: BlobGcFixture,
) -> dict[str, Any]:
    with connect(database_name) as connection:
        job = connection.execute(
            "SELECT status, attempts, max_attempts, locked_by, locked_until, "
            "locked_until > NOW() AS lease_live, completed_at, last_error, "
            "NOW() AS database_now FROM storage_jobs WHERE id = %s",
            (fixture.job_id,),
        ).fetchone()
        content = connection.execute(
            "SELECT id, ref_count, storage_path FROM file_contents WHERE id = %s",
            (fixture.content_id,),
        ).fetchone()
        finding = connection.execute(
            "SELECT id, occurrences, resolved_at FROM storage_reconciliation_findings "
            "WHERE finding_type = 'zero_reference_content' AND resource_id = %s",
            (str(fixture.content_id),),
        ).fetchone()
    require(job is not None, "Blob GC job disappeared")
    return {
        "job": dict(job),
        "content": dict(content) if content is not None else None,
        "finding": dict(finding) if finding is not None else None,
        "blob_exists": fixture.path.exists(),
    }


def create_worker_load_jobs(
    database_name: str,
    staging_root: Path,
    run_id: str,
    count: int,
) -> tuple[list[int], list[Path], int]:
    ids: list[int] = []
    paths: list[Path] = []
    bytes_created = 0
    with connect(database_name) as connection:
        with connection.transaction():
            for index in range(count):
                upload_id = f"fault_load_{run_id}_{index:04d}"
                directory = staging_root / upload_id
                directory.mkdir(parents=True, exist_ok=False)
                chunk_path = directory / "0.chunk"
                payload = hashlib.sha256(upload_id.encode()).digest() * 128
                chunk_path.write_bytes(payload)
                assembled_path = staging_root / f"{upload_id}.tmp"
                assembled_path.write_bytes(payload)
                bytes_created += len(payload) * 2
                paths.extend((directory, assembled_path))
                row = connection.execute(
                    "INSERT INTO storage_jobs "
                    "(job_type, aggregate_id, dedupe_key, payload, max_attempts) "
                    "VALUES ('staging_cleanup', %s, %s, %s::jsonb, 8) RETURNING id",
                    (
                        upload_id,
                        f"fault-load:{run_id}:{index:04d}",
                        json.dumps(
                            {
                                "upload_id": upload_id,
                                "backend": "local",
                                "prefix": f"staging/{upload_id}",
                            }
                        ),
                    ),
                ).fetchone()
                require(row is not None, "failed to insert Worker load job")
                ids.append(int(row["id"]))
    return ids, paths, bytes_created


def job_rows(database_name: str, ids: list[int]) -> list[dict[str, Any]]:
    with connect(database_name) as connection:
        rows = connection.execute(
            "SELECT id, status, attempts, locked_by, locked_until, completed_at, last_error "
            "FROM storage_jobs WHERE id = ANY(%s) ORDER BY id",
            (ids,),
        ).fetchall()
    return [dict(row) for row in rows]


def aggregate_load(
    results: list[dict[str, Any]],
    *,
    origin: float,
    api_killed_at: float,
) -> dict[str, Any]:
    require(results, "continuous load produced no results")
    ordered = sorted(results, key=lambda item: item["logical_id"])
    logical_successes = [item for item in ordered if item["success"]]
    logical_failures = [item for item in ordered if not item["success"]]
    attempts = [attempt for item in ordered for attempt in item["attempts"]]
    failed_attempts = [
        attempt for attempt in attempts if attempt["outcome"] != "success"
    ]
    first_attempt_failures = [
        item
        for item in ordered
        if item["attempts"] and item["attempts"][0]["outcome"] != "success"
    ]
    retried = [item for item in ordered if len(item["attempts"]) > 1]
    recovered_retries = [item for item in retried if item["success"]]
    success_ends = sorted(float(item["ended_at"]) for item in logical_successes)
    max_gap = max(
        (right - left for left, right in zip(success_ends, success_ends[1:])),
        default=0.0,
    )

    phase_results: dict[str, dict[str, Any]] = {}
    for phase in sorted({str(item["phase"]) for item in ordered}):
        samples = [item for item in ordered if item["phase"] == phase]
        phase_results[phase] = {
            "logical_requests": len(samples),
            "logical_failures": sum(not item["success"] for item in samples),
            "latency": summarize([float(item["latency_ms"]) for item in samples]),
        }

    endpoint_successes = {
        endpoint: sum(
            item["success"] and item["success_endpoint"] == endpoint for item in ordered
        )
        for endpoint in ("api-a", "api-b")
    }
    fallback = [
        item
        for item in ordered
        if item["ended_at"] >= api_killed_at
        and len(item["attempts"]) > 1
        and item["attempts"][0].get("endpoint") == "api-a"
        and item["attempts"][0]["outcome"] != "success"
        and item["success_endpoint"] == "api-b"
    ]
    first_success_after_kill = min(
        (
            float(item["ended_at"])
            for item in logical_successes
            if item["ended_at"] >= api_killed_at
        ),
        default=math.inf,
    )
    first_fallback = min(
        (float(item["ended_at"]) for item in fallback),
        default=math.inf,
    )
    failure_times = [
        float(attempt["ended_at"])
        for attempt in failed_attempts
        if attempt["ended_at"] >= api_killed_at
    ]

    logical_count = len(ordered)
    logical_failure_rate = len(logical_failures) * 100 / logical_count
    first_failure_rate = len(first_attempt_failures) * 100 / logical_count
    require(
        math.isfinite(first_success_after_kill), "load never succeeded after API kill"
    )
    require(math.isfinite(first_fallback), "no API A failure recovered through API B")

    def relative(value: float) -> float:
        return round(value - origin, 6)

    raw_results = []
    for item in ordered:
        raw = dict(item)
        raw["started_at_seconds"] = relative(float(raw.pop("started_at")))
        raw["ended_at_seconds"] = relative(float(raw.pop("ended_at")))
        raw_attempts = []
        for attempt in raw["attempts"]:
            normalized = dict(attempt)
            normalized["started_at_seconds"] = relative(
                float(normalized.pop("started_at"))
            )
            normalized["ended_at_seconds"] = relative(float(normalized.pop("ended_at")))
            raw_attempts.append(normalized)
        raw["attempts"] = raw_attempts
        raw_results.append(raw)

    return {
        "logical_requests": logical_count,
        "logical_successes": len(logical_successes),
        "logical_failures": len(logical_failures),
        "logical_failure_rate_percent": round(logical_failure_rate, 6),
        "physical_attempts": len(attempts),
        "failed_physical_attempts": len(failed_attempts),
        "first_attempt_failures": len(first_attempt_failures),
        "first_attempt_failure_rate_percent": round(first_failure_rate, 6),
        "retried_logical_requests": len(retried),
        "recovered_retries": len(recovered_retries),
        "endpoint_successes": endpoint_successes,
        "latency": summarize([float(item["latency_ms"]) for item in ordered]),
        "by_phase": phase_results,
        "max_success_completion_gap_seconds": round(max_gap, 6),
        "first_success_after_api_kill_seconds": round(
            first_success_after_kill - api_killed_at, 6
        ),
        "first_retry_recovery_after_api_kill_seconds": round(
            first_fallback - api_killed_at, 6
        ),
        "raw_failure_window_seconds": round(
            max(failure_times) - min(failure_times) if failure_times else 0.0,
            6,
        ),
        "raw_results": raw_results,
    }


def final_reconciliation(
    database_name: str,
    filename: str,
    upload_id: str,
    payload: bytes,
    file_id: int,
    fixture: BlobGcFixture,
    worker_load_ids: list[int],
    staging_root: Path,
) -> dict[str, Any]:
    with connect(database_name) as connection:
        file_row = connection.execute(
            "SELECT file.id, file.name, file.size, content.id AS content_id, "
            "content.hash_md5, content.hash_sha256, content.storage_path, content.ref_count "
            "FROM files file JOIN file_contents content ON content.id = file.content_id "
            "WHERE file.name = %s",
            (filename,),
        ).fetchone()
        counts = connection.execute(
            "SELECT (SELECT COUNT(*) FROM files) AS files, "
            "(SELECT COUNT(*) FROM file_contents) AS contents, "
            "(SELECT COUNT(*) FROM storage_jobs WHERE status = 4) AS dead_letters"
        ).fetchone()
        task = connection.execute(
            "SELECT status, completed_file_id, finalize_attempts, lease_owner, "
            "lease_expires_at, reserved_bytes FROM upload_tasks WHERE id = %s",
            (upload_id,),
        ).fetchone()
        user = connection.execute(
            "SELECT storage_used, storage_reserved FROM users WHERE username = 'admin'"
        ).fetchone()
        upload_cleanup = connection.execute(
            "SELECT status, attempts, locked_by, locked_until, last_error "
            "FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{upload_id}",),
        ).fetchone()
        target_count = connection.execute(
            "SELECT COUNT(*) AS count FROM storage_jobs WHERE dedupe_key = %s",
            (fixture.dedupe_key,),
        ).fetchone()

    require(file_row is not None, "completed file is missing")
    require(
        counts is not None and task is not None and user is not None,
        "reconciliation rows are missing",
    )
    require(upload_cleanup is not None, "upload cleanup job is missing")
    require(int(file_row["id"]) == file_id, "completed file ID changed")
    require(int(file_row["size"]) == len(payload), "completed file size changed")
    require(
        str(file_row["hash_md5"]).strip() == hashlib.md5(payload).hexdigest(),
        "completed MD5 changed",
    )
    require(
        str(file_row["hash_sha256"]).strip() == hashlib.sha256(payload).hexdigest(),
        "completed SHA-256 changed",
    )
    require(int(file_row["ref_count"]) == 1, "completed content ref_count changed")
    final_path = Path(str(file_row["storage_path"]))
    require(final_path.read_bytes() == payload, "completed Blob bytes changed")
    require(
        int(counts["files"]) == 1 and int(counts["contents"]) == 1,
        "file/content cardinality changed",
    )
    require(int(counts["dead_letters"]) == 0, "a storage job entered DeadLetter")
    require(int(task["status"]) == 1, "upload task did not reach Completed")
    require(
        int(task["completed_file_id"]) == file_id, "upload task points to another file"
    )
    require(
        int(task["finalize_attempts"]) == 2, "upload takeover attempt count changed"
    )
    require(
        task["lease_owner"] is None and task["lease_expires_at"] is None,
        "upload lease remained",
    )
    require(int(user["storage_used"]) == len(payload), "used quota changed")
    require(int(user["storage_reserved"]) == 0, "reserved quota remained")
    require(int(upload_cleanup["status"]) == 3, "upload cleanup did not succeed")
    require(
        upload_cleanup["locked_by"] is None and upload_cleanup["locked_until"] is None,
        "cleanup lease remained",
    )
    require(upload_cleanup["last_error"] is None, "upload cleanup retained an error")
    require(
        target_count is not None and int(target_count["count"]) == 1,
        "Blob GC dedupe key is not unique",
    )

    load_rows = job_rows(database_name, worker_load_ids)
    require(len(load_rows) == len(worker_load_ids), "Worker load job count changed")
    require(
        all(
            int(row["status"]) == 3
            and int(row["attempts"]) == 1
            and row["locked_by"] is None
            and row["locked_until"] is None
            and row["last_error"] is None
            for row in load_rows
        ),
        "Worker load jobs did not converge exactly once",
    )
    require(directory_bytes(staging_root) == 0, "local staging directory is not empty")
    return {
        "passed": True,
        "file_id": file_id,
        "file_bytes": len(payload),
        "content_id": int(file_row["content_id"]),
        "content_ref_count": int(file_row["ref_count"]),
        "upload_status": int(task["status"]),
        "upload_finalize_attempts": int(task["finalize_attempts"]),
        "storage_used": int(user["storage_used"]),
        "storage_reserved": int(user["storage_reserved"]),
        "upload_cleanup_attempts": int(upload_cleanup["attempts"]),
        "worker_load_jobs": len(load_rows),
        "worker_load_attempts_min": min(int(row["attempts"]) for row in load_rows),
        "worker_load_attempts_max": max(int(row["attempts"]) for row in load_rows),
        "dead_letters": int(counts["dead_letters"]),
        "staging_bytes": 0,
        "final_sha256": hashlib.sha256(payload).hexdigest(),
    }


def normalize_times(value: Any, origin: float) -> Any:
    if isinstance(value, dict):
        result: dict[str, Any] = {}
        for key, item in value.items():
            if key == "at" or key.endswith("_at"):
                if isinstance(item, (int, float)):
                    result[f"{key}_seconds"] = round(float(item) - origin, 6)
                    continue
            result[key] = normalize_times(item, origin)
        return result
    if isinstance(value, list):
        return [normalize_times(item, origin) for item in value]
    return json_ready(value)


def run(args: argparse.Namespace) -> dict[str, Any]:
    binary = resolve_binary(args.server_bin)
    started_at = datetime.now(timezone.utc)
    origin = time.monotonic()
    run_id = uuid.uuid4().hex[:12]
    database_name = f"disk_fault_load_{run_id}"
    log_directory = (
        REPO_ROOT / ".sisyphus" / "evidence" / f"failure-under-load-{run_id}"
    )
    created_database = False
    processes: list[ManagedDiskProcess] = []
    router: HealthAwareRouter | None = None
    load: ContinuousLoad | None = None
    complete_executor: concurrent.futures.ThreadPoolExecutor | None = None

    try:
        create_database(database_name)
        created_database = True
        run_database_command(
            ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
            database_name,
        )

        with tempfile.TemporaryDirectory(prefix="disk-fault-under-load-") as temp_raw:
            temp_root = Path(temp_raw)
            final_root = temp_root / "final"
            staging_root = temp_root / "staging"
            final_root.mkdir()
            staging_root.mkdir()
            api_a_port, api_b_port, worker_a_port, worker_b_port = allocate_ports(4)

            api_b = ManagedDiskProcess(
                name="fault-api-b",
                role="api",
                binary=binary,
                run_directory=temp_root / "api-b",
                config=topology_config(
                    database_name,
                    api_b_port,
                    "fault-api-b",
                    "api",
                    final_root,
                    staging_root,
                    temp_root / "api-b-upload",
                ),
                database_name=database_name,
                port=api_b_port,
                log_directory=log_directory,
            )
            processes.append(api_b)
            token, username = login(api_b.base_url)
            payload_seed = f"failure-under-load-{run_id}:".encode()
            payload = (payload_seed * (PAYLOAD_BYTES // len(payload_seed) + 1))[
                :PAYLOAD_BYTES
            ]
            filename = f"fault_under_load_{run_id}.bin"
            upload_id = prepare_upload(api_b.base_url, token, filename, payload)

            api_a = ManagedDiskProcess(
                name="fault-api-a",
                role="api",
                binary=binary,
                run_directory=temp_root / "api-a",
                config=topology_config(
                    database_name,
                    api_a_port,
                    "fault-api-a",
                    "api",
                    final_root,
                    staging_root,
                    temp_root / "api-a-upload",
                ),
                database_name=database_name,
                port=api_a_port,
                extra_environment={
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_AFTER_FINALIZE_CLAIM_UPLOAD_ID": upload_id,
                },
                log_directory=log_directory,
            )
            processes.append(api_a)

            phase = PhaseTracker("steady")
            router = HealthAwareRouter(
                {"api-a": api_a.base_url, "api-b": api_b.base_url}
            )
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
            wait_until(
                "continuous load warmup",
                lambda: (
                    load.count() >= args.warmup_requests
                    and load.successful_on("api-a") > 0
                    and load.successful_on("api-b") > 0
                ),
                30,
                processes=(api_a, api_b),
            )

            complete_executor = concurrent.futures.ThreadPoolExecutor(max_workers=1)
            paused_complete = complete_executor.submit(
                complete_request,
                api_a.base_url,
                token,
                upload_id,
            )

            def claimed() -> dict[str, Any] | None:
                snapshot = upload_claim_snapshot(database_name, upload_id)
                if (
                    snapshot is not None
                    and int(snapshot["status"]) == 4
                    and snapshot["lease_owner"] == api_a.name
                    and bool(snapshot["lease_live"])
                    and api_a.contains_log(FAULT_UPLOAD_MARKER)
                ):
                    return snapshot
                return None

            api_claim = wait_until(
                "API A committed finalize claim fault boundary",
                claimed,
                15,
                processes=(api_a, api_b),
            )
            phase.set("api_fault")
            api_killed_at = time.monotonic()
            api_a_kill_returncode = api_a.kill()
            require(
                api_a_kill_returncode == -signal.SIGKILL,
                f"API A did not exit through SIGKILL: {api_a_kill_returncode}",
            )
            paused_result = paused_complete.result(timeout=10)
            require(
                paused_result["outcome"] == "transport_error",
                f"killed API complete unexpectedly returned: {paused_result}",
            )

            wait_until(
                "load balancer removes API A",
                lambda: not router.is_healthy("api-a"),
                2,
                processes=(api_b,),
            )
            file_id, complete_retries, api_takeover_completed_at = (
                retry_complete_until_success(
                    api_b.base_url,
                    token,
                    upload_id,
                    args.timeout_seconds,
                )
            )
            api_takeover_rto = api_takeover_completed_at - api_killed_at
            require(api_takeover_rto <= 45, "API upload takeover exceeded 45 seconds")
            require(
                any(
                    attempt.get("http_status") == 409
                    and attempt.get("domain_code") == "10004"
                    for attempt in complete_retries[:-1]
                ),
                "survivor did not observe the live finalize lease",
            )
            completed_task = upload_claim_snapshot(database_name, upload_id)
            require(
                completed_task is not None
                and int(completed_task["status"]) == 1
                and int(completed_task["finalize_attempts"]) == 2,
                "API takeover did not converge to one Completed task",
            )

            api_restart_started_at = time.monotonic()
            api_a.start(extra_environment={})
            api_restart_ready_at = time.monotonic()
            phase.set("api_recovered")
            wait_until(
                "router rejoins restarted API A",
                lambda: router.is_healthy("api-a"),
                5,
                processes=(api_a, api_b),
            )
            api_router_rejoined_at = time.monotonic()
            a_successes_at_restart = load.successful_on(
                "api-a", after=api_restart_ready_at
            )
            wait_until(
                "restarted API A serves continuous load",
                lambda: (
                    load.successful_on("api-a", after=api_restart_ready_at)
                    >= a_successes_at_restart + args.post_rejoin_requests
                ),
                15,
                processes=(api_a, api_b),
            )

            blob_fixture = create_blob_gc_fixture(database_name, final_root, run_id)
            worker_a = ManagedDiskProcess(
                name="fault-worker-a",
                role="worker",
                binary=binary,
                run_directory=temp_root / "worker-a",
                config=topology_config(
                    database_name,
                    worker_a_port,
                    "fault-worker-a",
                    "worker",
                    final_root,
                    staging_root,
                    temp_root / "worker-a-upload",
                ),
                database_name=database_name,
                port=worker_a_port,
                extra_environment={
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_AFTER_BLOB_DELETE_JOB_ID": str(
                        blob_fixture.job_id
                    ),
                },
                log_directory=log_directory,
            )
            processes.append(worker_a)

            def blob_boundary() -> dict[str, Any] | None:
                snapshot = blob_gc_snapshot(database_name, blob_fixture)
                job = snapshot["job"]
                if (
                    int(job["status"]) == 1
                    and int(job["attempts"]) == 1
                    and job["locked_by"] == worker_a.name
                    and bool(job["lease_live"])
                    and not snapshot["blob_exists"]
                    and snapshot["content"] is not None
                    and snapshot["finding"] is not None
                    and snapshot["finding"]["resolved_at"] is None
                    and worker_a.contains_log(FAULT_BLOB_GC_MARKER)
                ):
                    return snapshot
                return None

            worker_boundary = wait_until(
                "Worker A Blob GC fault boundary",
                blob_boundary,
                20,
                processes=(api_a, api_b, worker_a),
            )
            worker_b = ManagedDiskProcess(
                name="fault-worker-b",
                role="worker",
                binary=binary,
                run_directory=temp_root / "worker-b",
                config=topology_config(
                    database_name,
                    worker_b_port,
                    "fault-worker-b",
                    "worker",
                    final_root,
                    staging_root,
                    temp_root / "worker-b-upload",
                ),
                database_name=database_name,
                port=worker_b_port,
                log_directory=log_directory,
            )
            processes.append(worker_b)
            worker_metrics_before = read_metrics(worker_b.base_url)

            phase.set("worker_fault")
            worker_killed_at = time.monotonic()
            worker_a_kill_returncode = worker_a.kill()
            require(
                worker_a_kill_returncode == -signal.SIGKILL,
                f"Worker A did not exit through SIGKILL: {worker_a_kill_returncode}",
            )
            worker_after_kill = wait_until(
                "Worker A transaction rollback",
                lambda: (
                    snapshot
                    if (
                        not (snapshot := blob_gc_snapshot(database_name, blob_fixture))[
                            "blob_exists"
                        ]
                        and snapshot["content"] is not None
                        and int(snapshot["job"]["status"]) == 1
                        and snapshot["job"]["locked_by"] == worker_a.name
                        and bool(snapshot["job"]["lease_live"])
                    )
                    else None
                ),
                10,
                processes=(api_a, api_b, worker_b),
            )
            worker_load_ids, worker_load_paths, worker_load_bytes = (
                create_worker_load_jobs(
                    database_name,
                    staging_root,
                    run_id,
                    args.worker_load_jobs,
                )
            )

            def load_jobs_succeeded() -> list[dict[str, Any]] | None:
                rows = job_rows(database_name, worker_load_ids)
                if len(rows) == len(worker_load_ids) and all(
                    int(row["status"]) == 3 for row in rows
                ):
                    return rows
                return None

            worker_load_rows = wait_until(
                "survivor processes Worker load jobs",
                load_jobs_succeeded,
                15,
                processes=(api_a, api_b, worker_b),
            )
            worker_load_completed_at = time.monotonic()
            target_during_load = blob_gc_snapshot(database_name, blob_fixture)
            require(
                int(target_during_load["job"]["status"]) == 1
                and bool(target_during_load["job"]["lease_live"]),
                "Worker B changed the live Blob GC lease while processing other jobs",
            )
            require(
                all(not path.exists() for path in worker_load_paths),
                "Worker load staging artifacts remained",
            )

            def blob_takeover_succeeded() -> dict[str, Any] | None:
                snapshot = blob_gc_snapshot(database_name, blob_fixture)
                job = snapshot["job"]
                if (
                    int(job["status"]) == 3
                    and int(job["attempts"]) == 2
                    and job["locked_by"] is None
                    and job["locked_until"] is None
                    and job["completed_at"] is not None
                    and job["last_error"] is None
                    and snapshot["content"] is None
                    and snapshot["finding"] is not None
                    and snapshot["finding"]["resolved_at"] == job["completed_at"]
                    and not snapshot["blob_exists"]
                ):
                    return snapshot
                return None

            try:
                worker_final = wait_until(
                    "Worker B lease-expiry Blob GC takeover",
                    blob_takeover_succeeded,
                    args.timeout_seconds,
                    processes=(api_a, api_b, worker_b),
                )
            except AssertionError as error:
                final_snapshot = blob_gc_snapshot(database_name, blob_fixture)
                raise AssertionError(
                    f"{error}; final_snapshot={json_ready(final_snapshot)}"
                ) from error
            worker_takeover_completed_at = time.monotonic()
            worker_takeover_rto = worker_takeover_completed_at - worker_killed_at
            require(worker_takeover_rto <= 45, "Worker takeover exceeded 45 seconds")
            worker_metrics_after = read_metrics(worker_b.base_url)
            worker_metric_result = {
                "staging_cleanup": storage_job_metrics(
                    worker_metrics_before,
                    worker_metrics_after,
                    "staging_cleanup",
                ),
                "blob_gc": storage_job_metrics(
                    worker_metrics_before,
                    worker_metrics_after,
                    "blob_gc",
                ),
            }
            require(
                worker_metric_result["staging_cleanup"]["outcomes"]
                == {
                    "succeeded": args.worker_load_jobs,
                    "retry": 0,
                    "dead_letter": 0,
                    "ownership_lost": 0,
                },
                "Worker load metrics changed",
            )
            require(
                worker_metric_result["blob_gc"]["outcomes"]
                == {
                    "succeeded": 1,
                    "retry": 0,
                    "dead_letter": 0,
                    "ownership_lost": 0,
                }
                and worker_metric_result["blob_gc"]["takeovers"] == 1,
                "Blob GC takeover metrics changed",
            )

            phase.set("recovered")
            time.sleep(2)
            load.stop()
            load_results = load.snapshot()
            load_report = aggregate_load(
                load_results,
                origin=origin,
                api_killed_at=api_killed_at,
            )
            require(
                load_report["logical_requests"] >= args.minimum_logical_requests,
                "continuous load did not reach the minimum request count",
            )
            require(
                load_report["logical_failure_rate_percent"] <= 0.1,
                "logical API error budget exceeded 0.1%",
            )
            require(
                load_report["first_attempt_failures"] >= 1,
                "continuous load did not observe the killed API",
            )
            require(
                load_report["first_attempt_failure_rate_percent"] <= 1.0,
                "first-attempt API error budget exceeded 1%",
            )
            require(
                load_report["retried_logical_requests"]
                == load_report["recovered_retries"],
                "a retried API operation did not recover",
            )
            require(
                load_report["first_success_after_api_kill_seconds"] <= 2,
                "continuous API success recovery exceeded 2 seconds",
            )
            require(
                load_report["first_retry_recovery_after_api_kill_seconds"] <= 2,
                "API fallback retry exceeded 2 seconds",
            )
            require(
                load_report["max_success_completion_gap_seconds"] <= 1,
                "continuous load completion gap exceeded 1 second",
            )
            require(
                all(count > 0 for count in load_report["endpoint_successes"].values()),
                "both API endpoints did not serve load",
            )

            download = httpx.get(
                f"{api_b.base_url}/api/file/download/{file_id}",
                headers={"Authorization": f"Bearer {token}"},
                timeout=20,
            )
            require(
                download.status_code == 200 and download.content == payload,
                "takeover file download changed bytes",
            )
            reconciliation = final_reconciliation(
                database_name,
                filename,
                upload_id,
                payload,
                file_id,
                blob_fixture,
                worker_load_ids,
                staging_root,
            )

            router.stop()
            api_unhealthy_transition = next(
                transition
                for transition in router.transitions
                if transition["endpoint"] == "api-a"
                and not transition["healthy"]
                and transition["at"] >= api_killed_at
            )
            api_healthy_transition = next(
                transition
                for transition in router.transitions
                if transition["endpoint"] == "api-a"
                and transition["healthy"]
                and transition["at"] >= api_restart_started_at
            )
            api_removal_seconds = api_unhealthy_transition["at"] - api_killed_at
            api_rejoin_seconds = api_healthy_transition["at"] - api_restart_started_at
            require(api_removal_seconds <= 2, "API removal exceeded 2 seconds")
            require(api_rejoin_seconds <= 5, "API rejoin exceeded 5 seconds")

            completed_at = datetime.now(timezone.utc)
            result = {
                "schema_version": 1,
                "scenario": "process_failure_under_continuous_load",
                "started_at": started_at.isoformat(),
                "completed_at": completed_at.isoformat(),
                "elapsed_seconds": round(
                    (completed_at - started_at).total_seconds(), 3
                ),
                "git": git_metadata(),
                "environment": environment_metadata(binary),
                "parameters": {
                    "request_rate": args.request_rate,
                    "load_concurrency": args.load_concurrency,
                    "worker_load_jobs": args.worker_load_jobs,
                    "warmup_requests": args.warmup_requests,
                    "post_rejoin_requests": args.post_rejoin_requests,
                    "minimum_logical_requests": args.minimum_logical_requests,
                    "payload_bytes": len(payload),
                    "api_threads": API_THREADS,
                    "db_pool_size": DB_POOL_SIZE,
                    "redis_pool_size": REDIS_POOL_SIZE,
                    "api_finalize_lease_seconds": LEASE_SECONDS,
                    "worker_lease_seconds": LEASE_SECONDS,
                    "worker_poll_interval_ms": WORKER_POLL_INTERVAL_MS,
                    "worker_claim_batch_size": WORKER_CLAIM_BATCH_SIZE,
                    "worker_concurrency": WORKER_CONCURRENCY,
                },
                "topology": {
                    "api_instances": 2,
                    "worker_instances": 2,
                    "load_balancer": "client-side health-aware round robin",
                    "storage_backend": "shared local test directory",
                    "sticky_session": False,
                },
                "timeline": normalize_times(
                    {
                        "phase_transitions": phase.transitions,
                        "router_transitions": router.transitions,
                    },
                    origin,
                ),
                "api_failure": {
                    "passed": True,
                    "fault_stage": "after committed finalize claim",
                    "kill_signal": "SIGKILL",
                    "kill_returncode": api_a_kill_returncode,
                    "claim_before_kill": json_ready(api_claim),
                    "paused_request": normalize_times(paused_result, origin),
                    "complete_retry_attempts": [
                        normalize_times(attempt, origin) for attempt in complete_retries
                    ],
                    "conflict_retries": sum(
                        attempt.get("domain_code") == "10004"
                        for attempt in complete_retries
                    ),
                    "takeover_rto_seconds": round(api_takeover_rto, 6),
                    "removal_seconds": round(api_removal_seconds, 6),
                    "restart_readiness_seconds": round(
                        api_restart_ready_at - api_restart_started_at, 6
                    ),
                    "router_rejoin_seconds": round(api_rejoin_seconds, 6),
                    "router_rejoined_at_seconds": round(
                        api_router_rejoined_at - origin, 6
                    ),
                    "completed_task": json_ready(completed_task),
                    "file_id": file_id,
                },
                "worker_failure": {
                    "passed": True,
                    "fault_stage": "after Blob delete before transaction commit",
                    "kill_signal": "SIGKILL",
                    "kill_returncode": worker_a_kill_returncode,
                    "boundary_before_kill": json_ready(worker_boundary),
                    "after_kill": json_ready(worker_after_kill),
                    "load_jobs": args.worker_load_jobs,
                    "load_job_bytes": worker_load_bytes,
                    "load_jobs_completed_while_target_lease_live": True,
                    "load_jobs_completion_seconds": round(
                        worker_load_completed_at - worker_killed_at, 6
                    ),
                    "target_during_load": json_ready(target_during_load),
                    "takeover_rto_seconds": round(worker_takeover_rto, 6),
                    "final": json_ready(worker_final),
                    "metrics": worker_metric_result,
                    "load_job_rows": json_ready(worker_load_rows),
                },
                "continuous_load": load_report,
                "reconciliation": reconciliation,
                "acceptance": {
                    "logical_failure_rate_at_most_0_1_percent": True,
                    "first_attempt_failure_rate_at_most_1_percent": True,
                    "api_fallback_at_most_2_seconds": True,
                    "max_success_gap_at_most_1_second": True,
                    "api_takeover_at_most_45_seconds": True,
                    "worker_takeover_at_most_45_seconds": True,
                    "worker_load_jobs_exactly_once": True,
                    "reconciliation_passed": True,
                    "passed": True,
                },
            }
            return result
    except BaseException:
        for process in processes:
            print(process.log_tail(), file=sys.stderr)
        raise
    finally:
        if load is not None:
            try:
                load.stop()
            except RuntimeError:
                pass
        if router is not None:
            router.stop()
        for process in reversed(processes):
            process.stop()
        if complete_executor is not None:
            complete_executor.shutdown(wait=False, cancel_futures=True)
        if created_database:
            drop_database(database_name)


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
        print(f"failure-under-load benchmark failed: {error}", file=sys.stderr)
        return 1

    serialized = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8")
        terminal_result = {
            "scenario": result["scenario"],
            "output": str(args.output),
            "elapsed_seconds": result["elapsed_seconds"],
            "api_failure": {
                "takeover_rto_seconds": result["api_failure"]["takeover_rto_seconds"],
                "removal_seconds": result["api_failure"]["removal_seconds"],
                "router_rejoin_seconds": result["api_failure"]["router_rejoin_seconds"],
            },
            "worker_failure": {
                "load_jobs": result["worker_failure"]["load_jobs"],
                "load_jobs_completion_seconds": result["worker_failure"][
                    "load_jobs_completion_seconds"
                ],
                "takeover_rto_seconds": result["worker_failure"][
                    "takeover_rto_seconds"
                ],
            },
            "continuous_load": {
                key: value
                for key, value in result["continuous_load"].items()
                if key != "raw_results"
            },
            "reconciliation": result["reconciliation"],
            "acceptance": result["acceptance"],
        }
        print(json.dumps(terminal_result, ensure_ascii=False, indent=2))
    else:
        print(serialized, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

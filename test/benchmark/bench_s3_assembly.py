#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "boto3",
#   "httpx",
#   "moto[server]==5.2.2",
#   "prometheus-client",
#   "psycopg[binary]",
# ]
# ///

"""Measure S3 streaming assembly and asynchronous staging cleanup."""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import importlib.metadata
import json
import math
import os
import shutil
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

import boto3
import httpx
from botocore.config import Config
from botocore.exceptions import BotoCoreError, ClientError
from prometheus_client.parser import text_string_to_metric_families

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

from bench_api_scaling import (  # noqa: E402
    DB_POOL_SIZE,
    REDIS_POOL_SIZE,
    git_metadata,
    host_cpu_sample,
    loopback_bytes,
    process_sample,
    resolve_executable,
)
from bench_storage_workloads import (  # noqa: E402
    CHUNK_SIZE,
    MIB,
    FileFixture,
    UploadHandle,
    complete_upload,
    directory_bytes,
    environment_metadata,
    initialize_upload,
    login,
    upload_chunks,
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


ASSEMBLE_BUFFER_BYTES = MIB
ASSEMBLY_CONCURRENCY = 1
S3_MAX_CONNECTIONS = 16
S3_IO_THREADS = 4
WORKER_POLL_INTERVAL_MS = 100
WORKER_CONCURRENCY = 1
UPLOAD_EXPIRY_SECONDS = 3600


@dataclass(frozen=True)
class PreparedUpload:
    handle: UploadHandle
    payload: bytes


@dataclass(frozen=True)
class ObjectInventory:
    count: int
    bytes: int
    chunk_count: int
    assembled_count: int
    keys: list[str]

    def to_json(self) -> dict[str, Any]:
        return {
            "count": self.count,
            "bytes": self.bytes,
            "chunk_count": self.chunk_count,
            "assembled_count": self.assembled_count,
            "keys": self.keys,
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file-mib", type=int, default=32)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--chunk-concurrency", type=int, default=4)
    parser.add_argument("--api-threads", type=int, default=4)
    parser.add_argument("--timeout-seconds", type=float, default=300.0)
    parser.add_argument("--worker-timeout-seconds", type=float, default=60.0)
    parser.add_argument("--server-bin", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    for name in (
        "file_mib",
        "trials",
        "chunk_concurrency",
        "api_threads",
        "timeout_seconds",
        "worker_timeout_seconds",
    ):
        if getattr(args, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.file_mib < 6:
        parser.error("--file-mib must be at least 6 to exercise multipart promotion")
    chunk_count = math.ceil(args.file_mib * MIB / CHUNK_SIZE)
    if args.chunk_concurrency > chunk_count:
        parser.error("--chunk-concurrency cannot exceed chunks per file")
    return args


def endpoint_configuration() -> dict[str, Any]:
    endpoint = os.environ.get("DISK_S3_ENDPOINT", "").strip()
    if not endpoint:
        return {
            "managed": True,
            "implementation": "moto",
            "version": importlib.metadata.version("moto"),
            "endpoint": "",
            "bucket": "disk-benchmark",
            "region": "us-east-1",
            "access_key": "disk",
            "secret_key": "disk-password",
        }

    required = (
        "DISK_S3_BUCKET",
        "DISK_S3_ACCESS_KEY",
        "DISK_S3_SECRET_KEY",
    )
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise RuntimeError(
            "external S3 endpoint requires environment variables: " + ", ".join(missing)
        )
    return {
        "managed": False,
        "implementation": os.environ.get("DISK_S3_IMPLEMENTATION", "external"),
        "version": os.environ.get(
            "DISK_S3_IMPLEMENTATION_VERSION", "operator-declared"
        ),
        "endpoint": endpoint,
        "bucket": os.environ["DISK_S3_BUCKET"],
        "region": os.environ.get("DISK_S3_REGION", "us-east-1"),
        "access_key": os.environ["DISK_S3_ACCESS_KEY"],
        "secret_key": os.environ["DISK_S3_SECRET_KEY"],
    }


class ManagedMoto:
    def __init__(self, run_directory: Path, port: int) -> None:
        executable = shutil.which("moto_server")
        if executable is None:
            raise RuntimeError(
                "moto_server entry point is unavailable in the uv environment"
            )
        self.endpoint = f"http://127.0.0.1:{port}"
        self.log_path = run_directory / "moto.log"
        self.log_handle: Any = None
        self.process: subprocess.Popen[bytes] | None = None
        run_directory.mkdir(parents=True, exist_ok=False)
        self.log_handle = self.log_path.open("wb")
        try:
            self.process = subprocess.Popen(
                [executable, "-H", "127.0.0.1", "-p", str(port)],
                cwd=run_directory,
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
            )
            self._wait_until_ready()
        except BaseException:
            self.stop()
            raise

    @property
    def pid(self) -> int:
        require(self.process is not None, "Moto process was not started")
        return self.process.pid

    def _wait_until_ready(self) -> None:
        deadline = time.monotonic() + 30
        last_error = "no response"
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                raise RuntimeError(
                    f"Moto exited during startup with {self.process.returncode}\n{self.log_tail()}"
                )
            try:
                response = httpx.get(self.endpoint, timeout=1.0)
                if response.status_code < 500:
                    return
                last_error = f"HTTP {response.status_code}"
            except httpx.HTTPError as error:
                last_error = str(error)
            time.sleep(0.1)
        raise RuntimeError(
            f"Moto did not become ready: {last_error}\n{self.log_tail()}"
        )

    def log_tail(self, lines: int = 80) -> str:
        if self.log_handle is not None:
            self.log_handle.flush()
        if not self.log_path.is_file():
            return "Moto log unavailable"
        content = self.log_path.read_text(
            encoding="utf-8", errors="replace"
        ).splitlines()
        return "Moto log tail:\n" + "\n".join(content[-lines:])

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process = None
        if self.log_handle is not None:
            self.log_handle.close()
            self.log_handle = None


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
        s3: dict[str, Any],
    ) -> None:
        self.name = name
        self.base_url = f"http://127.0.0.1:{port}"
        self.log_path = run_directory / f"{name}.log"
        self.config_path = run_directory / "config.json"
        self.log_handle: Any = None
        self.process: subprocess.Popen[bytes] | None = None
        run_directory.mkdir(parents=True, exist_ok=False)
        self.config_path.write_text(
            json.dumps(config, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        environment = server_environment(
            self.config_path,
            database_name,
            port,
            name,
            role=role,
        )
        environment.update(
            {
                "DISK_STORAGE_BACKEND": "s3",
                "DISK_UPLOAD_STAGING_BACKEND": "s3",
                "DISK_S3_ENDPOINT": str(s3["endpoint"]),
                "DISK_S3_BUCKET": str(s3["bucket"]),
                "DISK_S3_REGION": str(s3["region"]),
                "DISK_S3_ACCESS_KEY": str(s3["access_key"]),
                "DISK_S3_SECRET_KEY": str(s3["secret_key"]),
                "DISK_S3_USE_SSL": "true"
                if str(s3["endpoint"]).startswith("https://")
                else "false",
                "DISK_S3_FORCE_PATH_STYLE": "true",
                "DISK_S3_VERIFY_SSL": "false",
                "DISK_S3_MAX_CONNECTIONS": str(S3_MAX_CONNECTIONS),
                "DISK_S3_IO_THREADS": str(S3_IO_THREADS),
                "AWS_ACCESS_KEY_ID": str(s3["access_key"]),
                "AWS_SECRET_ACCESS_KEY": str(s3["secret_key"]),
                "AWS_DEFAULT_REGION": str(s3["region"]),
            }
        )
        session_token = os.environ.get("DISK_S3_SESSION_TOKEN")
        if session_token:
            environment["DISK_S3_SESSION_TOKEN"] = session_token
            environment["AWS_SESSION_TOKEN"] = session_token

        self.log_handle = self.log_path.open("wb")
        try:
            self.process = subprocess.Popen(
                [str(binary)],
                cwd=run_directory,
                env=environment,
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
            )
            self._wait_until_ready()
        except BaseException:
            self.stop()
            raise

    @property
    def pid(self) -> int:
        require(self.process is not None, f"{self.name} process was not started")
        return self.process.pid

    def _wait_until_ready(self) -> None:
        deadline = time.monotonic() + 60
        last_error = "no response"
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                raise RuntimeError(
                    f"{self.name} exited during startup with {self.process.returncode}\n"
                    f"{self.log_tail()}"
                )
            try:
                response = httpx.get(
                    self.base_url + "/api/health/ready",
                    timeout=1.0,
                )
                if response.status_code == 200:
                    return
                last_error = f"HTTP {response.status_code}: {response.text[:200]}"
            except httpx.HTTPError as error:
                last_error = str(error)
            time.sleep(0.2)
        raise RuntimeError(
            f"{self.name} did not become ready: {last_error}\n{self.log_tail()}"
        )

    def require_running(self, label: str) -> None:
        require(
            self.process is not None and self.process.poll() is None,
            f"{self.name} exited during {label}\n{self.log_tail()}",
        )

    def log_tail(self, lines: int = 100) -> str:
        if self.log_handle is not None:
            self.log_handle.flush()
        if not self.log_path.is_file():
            return f"{self.name} log unavailable"
        content = self.log_path.read_text(
            encoding="utf-8", errors="replace"
        ).splitlines()
        return f"{self.name} log tail:\n" + "\n".join(content[-lines:])

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process = None
        if self.log_handle is not None:
            self.log_handle.close()
            self.log_handle = None


class ProcessResourceProbe:
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
        while not self._stop_event.wait(0.01):
            for name, pid in self._processes.items():
                sample = process_sample(pid)
                if sample is not None:
                    self._peak_rss[name] = max(
                        self._peak_rss[name], int(sample["rss_bytes"])
                    )

    def stop(self) -> dict[str, Any]:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=2)
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


def s3_client(configuration: dict[str, Any]) -> Any:
    return boto3.client(
        "s3",
        endpoint_url=configuration["endpoint"],
        aws_access_key_id=configuration["access_key"],
        aws_secret_access_key=configuration["secret_key"],
        aws_session_token=os.environ.get("DISK_S3_SESSION_TOKEN"),
        region_name=configuration["region"],
        config=Config(
            s3={"addressing_style": "path"},
            retries={"max_attempts": 1, "mode": "standard"},
            connect_timeout=5,
            read_timeout=300,
        ),
    )


def inventory(client: Any, bucket: str, prefix: str) -> ObjectInventory:
    keys: list[str] = []
    total_bytes = 0
    paginator = client.get_paginator("list_objects_v2")
    for page in paginator.paginate(Bucket=bucket, Prefix=prefix):
        for item in page.get("Contents", []):
            keys.append(str(item["Key"]))
            total_bytes += int(item["Size"])
    keys.sort()
    return ObjectInventory(
        count=len(keys),
        bytes=total_bytes,
        chunk_count=sum("/chunks/" in key for key in keys),
        assembled_count=sum("/assembled/" in key for key in keys),
        keys=keys,
    )


def multipart_count(client: Any, bucket: str, prefix: str) -> int:
    count = 0
    key_marker: str | None = None
    upload_id_marker: str | None = None
    while True:
        request: dict[str, Any] = {"Bucket": bucket, "Prefix": prefix}
        if key_marker:
            request["KeyMarker"] = key_marker
        if upload_id_marker:
            request["UploadIdMarker"] = upload_id_marker
        response = client.list_multipart_uploads(**request)
        count += len(response.get("Uploads", []))
        if not response.get("IsTruncated"):
            return count
        key_marker = response.get("NextKeyMarker")
        upload_id_marker = response.get("NextUploadIdMarker")


def remove_prefix(client: Any, bucket: str, prefix: str) -> None:
    paginator = client.get_paginator("list_objects_v2")
    for page in paginator.paginate(Bucket=bucket, Prefix=prefix):
        objects = [{"Key": item["Key"]} for item in page.get("Contents", [])]
        for offset in range(0, len(objects), 1000):
            batch = objects[offset : offset + 1000]
            if batch:
                client.delete_objects(
                    Bucket=bucket,
                    Delete={"Objects": batch, "Quiet": True},
                )


def stream_sha256(client: Any, bucket: str, key: str) -> tuple[int, str]:
    response = client.get_object(Bucket=bucket, Key=key)
    body = response["Body"]
    digest = hashlib.sha256()
    size = 0
    try:
        while True:
            chunk = body.read(MIB)
            if not chunk:
                break
            digest.update(chunk)
            size += len(chunk)
    finally:
        body.close()
    return size, digest.hexdigest()


MetricSnapshot = dict[tuple[str, tuple[tuple[str, str], ...]], float]


def read_metrics(base_url: str) -> MetricSnapshot:
    response = httpx.get(base_url + "/metrics", timeout=30)
    response.raise_for_status()
    result: MetricSnapshot = {}
    for family in text_string_to_metric_families(response.text):
        for sample in family.samples:
            labels = tuple(
                sorted((str(key), str(value)) for key, value in sample.labels.items())
            )
            result[(sample.name, labels)] = float(sample.value)
    return result


def metric_value(snapshot: MetricSnapshot, name: str, **labels: str) -> float:
    expected = tuple(sorted(labels.items()))
    return snapshot.get((name, expected), 0.0)


def metric_delta(
    before: MetricSnapshot,
    after: MetricSnapshot,
    name: str,
    **labels: str,
) -> float:
    delta = metric_value(after, name, **labels) - metric_value(before, name, **labels)
    require(delta >= 0, f"metric counter decreased: {name}{labels}")
    return delta


def stage_deltas(
    before: MetricSnapshot,
    after: MetricSnapshot,
    trials: int,
) -> dict[str, dict[str, float | int]]:
    result: dict[str, dict[str, float | int]] = {}
    for stage in (
        "claim_lease",
        "load_metadata",
        "assemble",
        "dedup_lookup",
        "promote",
        "commit",
    ):
        count = int(
            metric_delta(
                before,
                after,
                "disk_upload_complete_stage_duration_seconds_count",
                stage=stage,
            )
        )
        duration = metric_delta(
            before,
            after,
            "disk_upload_complete_stage_duration_seconds_sum",
            stage=stage,
        )
        require(count == trials, f"upload complete metric count changed for {stage}")
        result[stage] = {
            "count": count,
            "sum_seconds": round(duration, 6),
            "mean_ms": round(duration * 1000 / count, 3),
        }
    return result


def s3_dependency_delta(
    before: MetricSnapshot,
    after: MetricSnapshot,
) -> dict[str, Any]:
    outcomes = {}
    total_calls = 0
    for outcome in (
        "success",
        "timeout",
        "connection",
        "conflict",
        "not_found",
        "retryable",
        "permanent",
        "protocol",
        "other",
    ):
        calls = int(
            metric_delta(
                before,
                after,
                "disk_dependency_calls_total",
                dependency="s3",
                outcome=outcome,
            )
        )
        outcomes[outcome] = calls
        total_calls += calls
    failure_calls = sum(
        outcomes[outcome]
        for outcome in (
            "timeout",
            "connection",
            "conflict",
            "retryable",
            "permanent",
            "protocol",
            "other",
        )
    )
    duration_count = int(
        metric_delta(
            before,
            after,
            "disk_dependency_call_duration_seconds_count",
            dependency="s3",
        )
    )
    duration_seconds = metric_delta(
        before,
        after,
        "disk_dependency_call_duration_seconds_sum",
        dependency="s3",
    )
    require(duration_count == total_calls, "S3 dependency count and outcomes diverged")
    require(failure_calls == 0, "S3 dependency failures occurred during benchmark")
    return {
        "calls": total_calls,
        "outcomes": outcomes,
        "failure_calls": failure_calls,
        "duration_sum_seconds": round(duration_seconds, 6),
        "duration_mean_ms": round(
            duration_seconds * 1000 / duration_count if duration_count else 0.0,
            3,
        ),
    }


def storage_job_metric_delta(
    before: MetricSnapshot,
    after: MetricSnapshot,
    trials: int,
) -> dict[str, Any]:
    outcomes = {}
    total_runs = 0
    for outcome in ("succeeded", "retry", "dead_letter", "ownership_lost"):
        count = int(
            metric_delta(
                before,
                after,
                "disk_storage_job_runs_total",
                job_type="staging_cleanup",
                outcome=outcome,
            )
        )
        outcomes[outcome] = count
        total_runs += count
    duration_count = int(
        metric_delta(
            before,
            after,
            "disk_storage_job_duration_seconds_count",
            job_type="staging_cleanup",
        )
    )
    duration_seconds = metric_delta(
        before,
        after,
        "disk_storage_job_duration_seconds_sum",
        job_type="staging_cleanup",
    )
    require(total_runs == trials, "Worker staging cleanup run count changed")
    require(duration_count == trials, "Worker staging cleanup duration count changed")
    require(outcomes["succeeded"] == trials, "Worker did not succeed every cleanup job")
    return {
        "runs": total_runs,
        "outcomes": outcomes,
        "duration_sum_seconds": round(duration_seconds, 6),
        "duration_mean_ms": round(duration_seconds * 1000 / trials, 3),
    }


def topology_config(
    database_name: str,
    port: int,
    instance_id: str,
    role: str,
    temporary_root: Path,
    s3: dict[str, Any],
    object_prefix: str,
    staging_prefix: str,
    args: argparse.Namespace,
) -> dict[str, Any]:
    config = server_config(
        database_name,
        port,
        instance_id,
        temporary_root / f"{role}-unused-final",
        temporary_root / f"{role}-unused-staging",
        role=role,
    )
    config["app"]["threads_num"] = args.api_threads
    config["app"]["client_max_body_size"] = "4M"
    disk_config = config["custom_config"]["disk"]
    disk_config.update(
        {
            "storage_backend": "s3",
            "upload_staging_backend": "s3",
            "chunk_size": CHUNK_SIZE,
            "max_file_size": args.file_mib * MIB,
            "upload_task_expiry_seconds": UPLOAD_EXPIRY_SECONDS,
            "assembly_max_concurrent": ASSEMBLY_CONCURRENCY,
            "assemble_buffer_size_bytes": ASSEMBLE_BUFFER_BYTES,
            "worker_poll_interval_ms": WORKER_POLL_INTERVAL_MS,
            "worker_claim_batch_size": WORKER_CONCURRENCY,
            "worker_concurrency": WORKER_CONCURRENCY,
            "s3": {
                "bucket": s3["bucket"],
                "region": s3["region"],
                "endpoint": s3["endpoint"],
                "use_ssl": str(s3["endpoint"]).startswith("https://"),
                "force_path_style": True,
                "verify_ssl": False,
                "object_prefix": object_prefix,
                "staging_prefix": staging_prefix,
                "max_connections": S3_MAX_CONNECTIONS,
                "io_threads": S3_IO_THREADS,
                "connect_timeout_ms": 3000,
                "request_timeout_ms": int(args.timeout_seconds * 1000),
                "max_retries": 0,
                "retry_base_delay_ms": 10,
            },
        }
    )
    config["db_clients"][0]["connection_number"] = DB_POOL_SIZE
    config["db_clients"][0].pop("num_connection_number", None)
    config["redis_clients"][0]["number_of_connections"] = REDIS_POOL_SIZE
    return config


async def prepare_uploads(
    base_url: str,
    run_prefix: str,
    args: argparse.Namespace,
) -> tuple[str, list[PreparedUpload]]:
    limits = httpx.Limits(
        max_connections=args.chunk_concurrency,
        max_keepalive_connections=args.chunk_concurrency,
    )
    async with httpx.AsyncClient(
        base_url=base_url,
        limits=limits,
        timeout=httpx.Timeout(args.timeout_seconds),
    ) as client:
        token = await login(client)
        client.headers["Authorization"] = f"Bearer {token}"
        prepared: list[PreparedUpload] = []
        for index in range(args.trials):
            payload = os.urandom(args.file_mib * MIB)
            handle, _ = await initialize_upload(
                client,
                f"{run_prefix}s3_{index:02d}.bin",
                payload,
            )
            await upload_chunks(
                client,
                handle,
                payload,
                args.chunk_concurrency,
            )
            prepared.append(PreparedUpload(handle=handle, payload=payload))
        return token, prepared


async def complete_uploads(
    base_url: str,
    token: str,
    prepared: list[PreparedUpload],
    timeout_seconds: float,
) -> tuple[list[FileFixture], list[float]]:
    fixtures: list[FileFixture] = []
    latencies: list[float] = []
    async with httpx.AsyncClient(
        base_url=base_url,
        timeout=httpx.Timeout(timeout_seconds),
        headers={"Authorization": f"Bearer {token}"},
    ) as client:
        for upload in prepared:
            fixture, latency = await complete_upload(
                client,
                upload.handle,
                upload.payload,
            )
            fixtures.append(fixture)
            latencies.append(latency)
    return fixtures, latencies


def cleanup_job_rows(database_name: str, run_prefix: str) -> list[dict[str, Any]]:
    with connect(database_name) as connection:
        rows = connection.execute(
            "SELECT job.id, job.aggregate_id, job.dedupe_key, job.status, job.attempts, "
            "job.last_error FROM storage_jobs job "
            "JOIN upload_tasks task ON task.id = job.aggregate_id "
            "WHERE job.job_type = 'staging_cleanup' AND LEFT(task.filename, %s) = %s "
            "ORDER BY job.id",
            (len(run_prefix), run_prefix),
        ).fetchall()
    return [dict(row) for row in rows]


def postpone_cleanup_jobs(database_name: str, run_prefix: str, trials: int) -> None:
    with connect(database_name) as connection:
        result = connection.execute(
            "UPDATE storage_jobs job SET available_at = NOW() + INTERVAL '1 day' "
            "FROM upload_tasks task WHERE task.id = job.aggregate_id "
            "AND job.job_type = 'staging_cleanup' AND job.status = 0 "
            "AND LEFT(task.filename, %s) = %s",
            (len(run_prefix), run_prefix),
        )
        require(result.rowcount == trials, "cleanup jobs were not all postponed")


def release_cleanup_jobs(database_name: str, run_prefix: str, trials: int) -> None:
    with connect(database_name) as connection:
        connection.execute(
            "UPDATE storage_jobs SET available_at = NOW() + INTERVAL '1 day' "
            "WHERE job_type <> 'staging_cleanup' AND status IN (0, 2)"
        )
        result = connection.execute(
            "UPDATE storage_jobs job SET available_at = NOW() "
            "FROM upload_tasks task WHERE task.id = job.aggregate_id "
            "AND job.job_type = 'staging_cleanup' AND job.status = 0 "
            "AND LEFT(task.filename, %s) = %s",
            (len(run_prefix), run_prefix),
        )
        require(result.rowcount == trials, "cleanup jobs were not all released")


def wait_for_cleanup_jobs(
    database_name: str,
    run_prefix: str,
    trials: int,
    timeout_seconds: float,
    worker: ManagedDiskProcess,
) -> list[dict[str, Any]]:
    deadline = time.monotonic() + timeout_seconds
    rows: list[dict[str, Any]] = []
    while time.monotonic() < deadline:
        worker.require_running("S3 staging cleanup")
        rows = cleanup_job_rows(database_name, run_prefix)
        if any(int(row["status"]) == 4 for row in rows):
            raise RuntimeError(f"S3 staging cleanup entered DeadLetter: {rows}")
        if len(rows) == trials and all(int(row["status"]) == 3 for row in rows):
            return rows
        time.sleep(0.05)
    raise RuntimeError(f"S3 staging cleanup did not finish: {rows}")


def verify_final_objects(
    client: Any,
    bucket: str,
    object_prefix: str,
    fixtures: list[FileFixture],
) -> list[dict[str, Any]]:
    verified = []
    for fixture in fixtures:
        sha256_hash = hashlib.sha256(fixture.payload).hexdigest()
        key = f"{object_prefix}/sha256/{sha256_hash[:2]}/{sha256_hash}.bin"
        size, observed_sha256 = stream_sha256(client, bucket, key)
        require(
            size == len(fixture.payload),
            f"final object size changed for {fixture.name}",
        )
        require(
            observed_sha256 == sha256_hash,
            f"final object SHA-256 changed for {fixture.name}",
        )
        verified.append(
            {
                "file_id": fixture.file_id,
                "name": fixture.name,
                "key": key,
                "bytes": size,
                "sha256": sha256_hash,
            }
        )
    return verified


def reconcile_database(
    database_name: str,
    run_prefix: str,
    expected_bytes: int,
    trials: int,
) -> dict[str, Any]:
    with connect(database_name) as connection:
        file_row = connection.execute(
            "SELECT COUNT(*) AS count, COALESCE(SUM(size), 0) AS bytes "
            "FROM files WHERE LEFT(name, %s) = %s",
            (len(run_prefix), run_prefix),
        ).fetchone()
        content_row = connection.execute(
            "SELECT COUNT(*) AS count, MIN(content.ref_count) AS min_ref_count, "
            "MAX(content.ref_count) AS max_ref_count "
            "FROM file_contents content JOIN files file ON file.content_id = content.id "
            "WHERE LEFT(file.name, %s) = %s",
            (len(run_prefix), run_prefix),
        ).fetchone()
        task_row = connection.execute(
            "SELECT COUNT(*) AS count, COUNT(*) FILTER (WHERE status = 1) AS completed, "
            "COUNT(*) FILTER (WHERE staging_backend = 's3') AS s3_tasks "
            "FROM upload_tasks WHERE LEFT(filename, %s) = %s",
            (len(run_prefix), run_prefix),
        ).fetchone()
        user_row = connection.execute(
            "SELECT storage_used, storage_reserved FROM users WHERE username = 'admin'"
        ).fetchone()

    require(
        file_row is not None and int(file_row["count"]) == trials, "file count changed"
    )
    require(int(file_row["bytes"]) == expected_bytes, "file bytes changed")
    require(
        content_row is not None and int(content_row["count"]) == trials,
        "content count changed",
    )
    require(
        int(content_row["min_ref_count"]) == 1
        and int(content_row["max_ref_count"]) == 1,
        "content ref_count changed",
    )
    require(
        task_row is not None and int(task_row["count"]) == trials, "task count changed"
    )
    require(int(task_row["completed"]) == trials, "tasks are not completed")
    require(
        int(task_row["s3_tasks"]) == trials, "tasks did not retain S3 staging ownership"
    )
    require(user_row is not None, "admin user disappeared")
    require(int(user_row["storage_used"]) == expected_bytes, "storage_used changed")
    require(int(user_row["storage_reserved"]) == 0, "storage_reserved is not zero")
    return {
        "passed": True,
        "file_count": int(file_row["count"]),
        "file_bytes": int(file_row["bytes"]),
        "content_count": int(content_row["count"]),
        "content_ref_count_min": int(content_row["min_ref_count"]),
        "content_ref_count_max": int(content_row["max_ref_count"]),
        "upload_task_count": int(task_row["count"]),
        "completed_upload_tasks": int(task_row["completed"]),
        "s3_upload_tasks": int(task_row["s3_tasks"]),
        "user_storage_used": int(user_row["storage_used"]),
        "user_storage_reserved": int(user_row["storage_reserved"]),
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    binary = (
        resolve_executable(args.server_bin, [], "Disk server")
        if args.server_bin is not None
        else resolve_current_binary()
    )
    s3 = endpoint_configuration()
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_s3_assembly_{suffix}"
    run_prefix = f"bench_s3_{suffix}_"
    object_prefix = f"objects/bench-{suffix}"
    staging_prefix = f"staging/bench-{suffix}"
    database_created = False
    endpoint: ManagedMoto | None = None
    api: ManagedDiskProcess | None = None
    worker: ManagedDiskProcess | None = None
    client: Any = None
    started_at = datetime.now(timezone.utc)
    assembly_result: dict[str, Any] = {}
    cleanup_result: dict[str, Any] = {}
    reconciliation: dict[str, Any] = {}
    verified_objects: list[dict[str, Any]] = []

    with tempfile.TemporaryDirectory(prefix="disk-s3-assembly-") as temporary:
        temporary_root = Path(temporary)
        ports = allocate_ports(3 if s3["managed"] else 2)
        api_port = ports[0]
        worker_port = ports[1]
        if s3["managed"]:
            endpoint = ManagedMoto(temporary_root / "moto-run", ports[2])
            s3["endpoint"] = endpoint.endpoint

        client = s3_client(s3)
        if s3["managed"]:
            client.create_bucket(Bucket=s3["bucket"])
        else:
            client.head_bucket(Bucket=s3["bucket"])

        try:
            create_database(database_name)
            database_created = True
            run_database_command(
                ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
                database_name,
            )
            api = ManagedDiskProcess(
                name=f"s3-api-{suffix}",
                role="api",
                binary=binary,
                run_directory=temporary_root / "api-run",
                config=topology_config(
                    database_name,
                    api_port,
                    f"s3-api-{suffix}",
                    "api",
                    temporary_root,
                    s3,
                    object_prefix,
                    staging_prefix,
                    args,
                ),
                database_name=database_name,
                port=api_port,
                s3=s3,
            )

            print("preparing S3-native chunks", file=sys.stderr)
            token, prepared = asyncio.run(
                prepare_uploads(api.base_url, run_prefix, args)
            )
            expected_bytes = args.trials * args.file_mib * MIB
            expected_chunks = sum(upload.handle.total_chunks for upload in prepared)
            staging_before = inventory(client, s3["bucket"], staging_prefix + "/")
            final_before = inventory(client, s3["bucket"], object_prefix + "/")
            require(
                staging_before.count == expected_chunks,
                "preloaded S3 chunk count changed",
            )
            require(
                staging_before.bytes == expected_bytes,
                "preloaded S3 chunk bytes changed",
            )
            require(
                staging_before.chunk_count == expected_chunks,
                "preload contains non-chunk objects",
            )
            require(
                final_before.count == 0, "final prefix was not empty before completion"
            )
            require(
                multipart_count(client, s3["bucket"], staging_prefix + "/") == 0,
                "preload left incomplete multipart uploads",
            )

            print("measuring S3 assembly and promotion", file=sys.stderr)
            metrics_before = read_metrics(api.base_url)
            assembly_processes = {"api": api.pid, "client": os.getpid()}
            if endpoint is not None:
                assembly_processes["s3_endpoint"] = endpoint.pid
            assembly_probe = ProcessResourceProbe(assembly_processes)
            assembly_probe.start()
            fixtures, complete_latencies = asyncio.run(
                complete_uploads(
                    api.base_url,
                    token,
                    prepared,
                    args.timeout_seconds,
                )
            )
            assembly_resources = assembly_probe.stop()
            metrics_after = read_metrics(api.base_url)
            api.require_running("S3 assembly")

            stages = stage_deltas(metrics_before, metrics_after, args.trials)
            s3_calls = s3_dependency_delta(metrics_before, metrics_after)
            assemble_seconds = float(stages["assemble"]["sum_seconds"])
            promote_seconds = float(stages["promote"]["sum_seconds"])
            require(assemble_seconds > 0, "S3 assemble duration is zero")
            require(promote_seconds > 0, "S3 promote duration is zero")

            staging_peak = inventory(client, s3["bucket"], staging_prefix + "/")
            final_peak = inventory(client, s3["bucket"], object_prefix + "/")
            require(
                staging_peak.count == expected_chunks + args.trials,
                "pre-cleanup staging object count changed",
            )
            require(
                staging_peak.bytes == expected_bytes * 2,
                "pre-cleanup staging bytes changed",
            )
            require(
                staging_peak.chunk_count == expected_chunks
                and staging_peak.assembled_count == args.trials,
                "pre-cleanup staging object classes changed",
            )
            require(final_peak.count == args.trials, "final object count changed")
            require(final_peak.bytes == expected_bytes, "final object bytes changed")
            require(
                multipart_count(client, s3["bucket"], staging_prefix + "/") == 0
                and multipart_count(client, s3["bucket"], object_prefix + "/") == 0,
                "completion left incomplete multipart uploads",
            )
            jobs_before = cleanup_job_rows(database_name, run_prefix)
            require(len(jobs_before) == args.trials, "cleanup job count changed")
            require(
                all(int(row["status"]) == 0 for row in jobs_before),
                "cleanup jobs ran before the Worker started",
            )
            require(
                all(
                    row["dedupe_key"] == f"staging-cleanup:{row['aggregate_id']}"
                    for row in jobs_before
                ),
                "cleanup job dedupe contract changed",
            )
            assembly_result = {
                "passed": True,
                "business_bytes": expected_bytes,
                "trials": args.trials,
                "complete_latency_samples_ms": complete_latencies,
                "complete_latency_mean_ms": round(
                    sum(complete_latencies) / len(complete_latencies), 3
                ),
                "stages": stages,
                "assemble_mib_per_second": round(
                    expected_bytes / assemble_seconds / MIB, 3
                ),
                "promote_mib_per_second": round(
                    expected_bytes / promote_seconds / MIB, 3
                ),
                "end_to_end_mib_per_second": round(
                    expected_bytes / float(assembly_resources["wall_seconds"]) / MIB,
                    3,
                ),
                "s3_dependency": s3_calls,
                "resources": assembly_resources,
                "inventory": {
                    "staging_before": staging_before.to_json(),
                    "final_before": final_before.to_json(),
                    "staging_pre_cleanup": staging_peak.to_json(),
                    "final_pre_cleanup": final_peak.to_json(),
                    "staging_growth_bytes": staging_peak.bytes - staging_before.bytes,
                    "endpoint_visible_growth_bytes": staging_peak.bytes
                    + final_peak.bytes
                    - staging_before.bytes,
                    "incomplete_multipart_uploads": 0,
                },
                "cleanup_jobs_before_worker": jobs_before,
            }

            print("verifying promoted S3 objects", file=sys.stderr)
            verified_objects = verify_final_objects(
                client,
                s3["bucket"],
                object_prefix,
                fixtures,
            )

            postpone_cleanup_jobs(database_name, run_prefix, args.trials)
            worker = ManagedDiskProcess(
                name=f"s3-worker-{suffix}",
                role="worker",
                binary=binary,
                run_directory=temporary_root / "worker-run",
                config=topology_config(
                    database_name,
                    worker_port,
                    f"s3-worker-{suffix}",
                    "worker",
                    temporary_root,
                    s3,
                    object_prefix,
                    staging_prefix,
                    args,
                ),
                database_name=database_name,
                port=worker_port,
                s3=s3,
            )
            worker_metrics_before = read_metrics(worker.base_url)
            cleanup_processes = {
                "api": api.pid,
                "worker": worker.pid,
                "client": os.getpid(),
            }
            if endpoint is not None:
                cleanup_processes["s3_endpoint"] = endpoint.pid
            cleanup_probe = ProcessResourceProbe(cleanup_processes)
            cleanup_probe.start()
            print("measuring Worker staging cleanup", file=sys.stderr)
            release_cleanup_jobs(database_name, run_prefix, args.trials)
            jobs_after = wait_for_cleanup_jobs(
                database_name,
                run_prefix,
                args.trials,
                args.worker_timeout_seconds,
                worker,
            )
            cleanup_resources = cleanup_probe.stop()
            worker_metrics_after = read_metrics(worker.base_url)
            worker.require_running("S3 cleanup verification")
            api.require_running("S3 cleanup verification")
            cleanup_metrics = storage_job_metric_delta(
                worker_metrics_before,
                worker_metrics_after,
                args.trials,
            )
            staging_after = inventory(client, s3["bucket"], staging_prefix + "/")
            final_after = inventory(client, s3["bucket"], object_prefix + "/")
            require(staging_after.count == 0, "Worker did not empty the staging prefix")
            require(
                final_after == final_peak,
                "Worker changed final objects during staging cleanup",
            )
            require(
                multipart_count(client, s3["bucket"], staging_prefix + "/") == 0,
                "Worker cleanup left incomplete multipart uploads",
            )
            cleanup_wall = float(cleanup_resources["wall_seconds"])
            cleanup_result = {
                "passed": True,
                "jobs": args.trials,
                "jobs_per_second": round(args.trials / cleanup_wall, 3),
                "removed_bytes": staging_peak.bytes,
                "removed_mib_per_second": round(
                    staging_peak.bytes / cleanup_wall / MIB, 3
                ),
                "metrics": cleanup_metrics,
                "resources": cleanup_resources,
                "jobs_after": jobs_after,
                "inventory": {
                    "staging_after": staging_after.to_json(),
                    "final_after": final_after.to_json(),
                    "incomplete_multipart_uploads": 0,
                },
            }

            reconciliation = reconcile_database(
                database_name,
                run_prefix,
                expected_bytes,
                args.trials,
            )
            local_staging_bytes = directory_bytes(temporary_root / "api-unused-staging")
            worker_local_staging_bytes = directory_bytes(
                temporary_root / "worker-unused-staging"
            )
            require(local_staging_bytes == 0, "API created node-local staging bytes")
            require(
                worker_local_staging_bytes == 0,
                "Worker created node-local staging bytes",
            )
            reconciliation["api_local_staging_bytes"] = local_staging_bytes
            reconciliation["worker_local_staging_bytes"] = worker_local_staging_bytes
        except BaseException:
            for process in (api, worker):
                if process is not None:
                    print(process.log_tail(), file=sys.stderr)
            if endpoint is not None:
                print(endpoint.log_tail(), file=sys.stderr)
            raise
        finally:
            if worker is not None:
                worker.stop()
            if api is not None:
                api.stop()
            if client is not None:
                for prefix in (staging_prefix, object_prefix):
                    try:
                        remove_prefix(client, s3["bucket"], prefix)
                    except (BotoCoreError, ClientError) as error:
                        print(
                            f"warning: failed to remove S3 prefix {prefix}: {error}",
                            file=sys.stderr,
                        )
            if database_created:
                drop_database(database_name)
            if endpoint is not None:
                endpoint.stop()

    completed_at = datetime.now(timezone.utc)
    endpoint_report = {
        "managed": bool(s3["managed"]),
        "implementation": s3["implementation"],
        "version": s3["version"],
        "endpoint": s3["endpoint"],
        "bucket": s3["bucket"],
        "region": s3["region"],
    }
    passed = (
        assembly_result.get("passed") is True
        and cleanup_result.get("passed") is True
        and reconciliation.get("passed") is True
        and len(verified_objects) == args.trials
    )
    return {
        "schema_version": 1,
        "scenario": "s3_streaming_assembly",
        "started_at": started_at.isoformat(),
        "completed_at": completed_at.isoformat(),
        "elapsed_seconds": round((completed_at - started_at).total_seconds(), 3),
        "git": git_metadata(),
        "environment": environment_metadata(binary),
        "endpoint": endpoint_report,
        "parameters": {
            "file_bytes": args.file_mib * MIB,
            "trials": args.trials,
            "total_business_bytes": args.trials * args.file_mib * MIB,
            "chunk_size_bytes": CHUNK_SIZE,
            "chunk_concurrency": args.chunk_concurrency,
            "api_threads": args.api_threads,
            "db_pool_size": DB_POOL_SIZE,
            "redis_pool_size": REDIS_POOL_SIZE,
            "assembly_max_concurrent": ASSEMBLY_CONCURRENCY,
            "assemble_buffer_size_bytes": ASSEMBLE_BUFFER_BYTES,
            "s3_max_connections": S3_MAX_CONNECTIONS,
            "s3_io_threads": S3_IO_THREADS,
            "worker_poll_interval_ms": WORKER_POLL_INTERVAL_MS,
            "worker_concurrency": WORKER_CONCURRENCY,
        },
        "assembly": assembly_result,
        "cleanup": cleanup_result,
        "verified_final_objects": verified_objects,
        "reconciliation": reconciliation,
        "acceptance": {
            "assembly_passed": assembly_result.get("passed") is True,
            "cleanup_passed": cleanup_result.get("passed") is True,
            "final_object_count": len(verified_objects),
            "reconciliation_passed": reconciliation.get("passed") is True,
            "passed": passed,
        },
    }


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (
        AssertionError,
        BotoCoreError,
        ClientError,
        httpx.HTTPError,
        OSError,
        RuntimeError,
        subprocess.SubprocessError,
    ) as error:
        print(f"S3 assembly benchmark failed: {error}", file=sys.stderr)
        return 1

    serialized = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if result["acceptance"]["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())

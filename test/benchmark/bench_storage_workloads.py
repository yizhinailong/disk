#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Run an isolated small/chunk/large/Range/mixed storage workload matrix."""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import math
import os
import platform
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
    DB_POOL_SIZE,
    REDIS_POOL_SIZE,
    build_type,
    command_output,
    cpu_model,
    file_sha256,
    git_metadata,
    host_cpu_sample,
    loopback_bytes,
    memory_total_bytes,
    physical_cpu_count,
    process_sample,
    resolve_executable,
)
from test_expand_mixed_version import (  # noqa: E402
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


REPO_ROOT = Path(__file__).resolve().parents[2]
KIB = 1024
MIB = 1024 * KIB
CHUNK_SIZE = MIB
MAX_FILE_SIZE = 32 * MIB
ASSEMBLY_CONCURRENCY = 8
UPLOAD_TASK_EXPIRY_SECONDS = 3600


@dataclass(frozen=True)
class UploadHandle:
    upload_id: str
    name: str
    file_hash: str
    file_size: int
    chunk_size: int
    total_chunks: int


@dataclass(frozen=True)
class FileFixture:
    file_id: int
    name: str
    payload: bytes
    file_hash: str


@dataclass(frozen=True)
class CompletedUpload:
    fixture: FileFixture
    logical_ms: float
    init_ms: float
    chunk_wall_ms: float
    chunk_latencies_ms: list[float]
    complete_ms: float

    @property
    def load_http_requests(self) -> int:
        return 2 + len(self.chunk_latencies_ms)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--small-files", type=int, default=32)
    parser.add_argument("--small-bytes", type=int, default=64 * KIB)
    parser.add_argument("--small-concurrency", type=int, default=8)
    parser.add_argument("--chunk-file-mib", type=int, default=8)
    parser.add_argument("--chunk-concurrency", type=int, default=8)
    parser.add_argument("--large-file-mib", type=int, default=12)
    parser.add_argument("--large-trials", type=int, default=3)
    parser.add_argument("--range-requests", type=int, default=128)
    parser.add_argument("--range-bytes", type=int, default=512 * KIB)
    parser.add_argument("--range-concurrency", type=int, default=16)
    parser.add_argument("--mixed-operations", type=int, default=120)
    parser.add_argument("--mixed-concurrency", type=int, default=16)
    parser.add_argument("--mixed-write-percent", type=int, default=20)
    parser.add_argument("--mixed-write-bytes", type=int, default=64 * KIB)
    parser.add_argument("--mixed-range-bytes", type=int, default=256 * KIB)
    parser.add_argument("--mixed-write-concurrency", type=int, default=8)
    parser.add_argument("--api-threads", type=int, default=4)
    parser.add_argument("--timeout-seconds", type=float, default=180.0)
    parser.add_argument("--server-bin", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    positive = {
        "small_files": args.small_files,
        "small_bytes": args.small_bytes,
        "small_concurrency": args.small_concurrency,
        "chunk_file_mib": args.chunk_file_mib,
        "chunk_concurrency": args.chunk_concurrency,
        "large_file_mib": args.large_file_mib,
        "large_trials": args.large_trials,
        "range_requests": args.range_requests,
        "range_bytes": args.range_bytes,
        "range_concurrency": args.range_concurrency,
        "mixed_operations": args.mixed_operations,
        "mixed_concurrency": args.mixed_concurrency,
        "mixed_write_bytes": args.mixed_write_bytes,
        "mixed_range_bytes": args.mixed_range_bytes,
        "mixed_write_concurrency": args.mixed_write_concurrency,
        "api_threads": args.api_threads,
        "timeout_seconds": args.timeout_seconds,
    }
    for name, value in positive.items():
        if value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")

    chunk_file_bytes = args.chunk_file_mib * MIB
    large_file_bytes = args.large_file_mib * MIB
    if (
        max(
            args.small_bytes,
            chunk_file_bytes,
            large_file_bytes,
            args.mixed_write_bytes,
        )
        > MAX_FILE_SIZE
    ):
        parser.error(f"file sizes cannot exceed {MAX_FILE_SIZE} bytes")
    if args.small_concurrency > args.small_files:
        parser.error("--small-concurrency cannot exceed --small-files")
    chunk_count = math.ceil(chunk_file_bytes / CHUNK_SIZE)
    if args.chunk_concurrency > chunk_count:
        parser.error("--chunk-concurrency cannot exceed the configured chunk count")
    if args.range_concurrency > args.range_requests:
        parser.error("--range-concurrency cannot exceed --range-requests")
    if args.range_bytes > large_file_bytes:
        parser.error("--range-bytes cannot exceed the large fixture size")
    if args.mixed_range_bytes > large_file_bytes:
        parser.error("--mixed-range-bytes cannot exceed the large fixture size")
    if args.mixed_concurrency > args.mixed_operations:
        parser.error("--mixed-concurrency cannot exceed --mixed-operations")
    if not 1 <= args.mixed_write_percent <= 99:
        parser.error("--mixed-write-percent must be in range 1..99")
    mixed_writes = mixed_write_count(args.mixed_operations, args.mixed_write_percent)
    if args.mixed_write_concurrency > mixed_writes:
        parser.error("--mixed-write-concurrency cannot exceed mixed write operations")
    return args


def mixed_write_count(operations: int, write_percent: int) -> int:
    return max(1, min(operations - 1, (operations * write_percent + 50) // 100))


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * fraction) - 1)
    return ordered[index]


def summarize(values: list[float]) -> dict[str, float | int]:
    require(values, "cannot summarize an empty sample")
    return {
        "count": len(values),
        "min_ms": round(min(values), 3),
        "p50_ms": round(percentile(values, 0.50), 3),
        "p95_ms": round(percentile(values, 0.95), 3),
        "p99_ms": round(percentile(values, 0.99), 3),
        "max_ms": round(max(values), 3),
        "mean_ms": round(statistics.fmean(values), 3),
    }


def directory_bytes(path: Path) -> int:
    total = 0
    if not path.exists():
        return total
    for candidate in path.rglob("*"):
        try:
            if candidate.is_file():
                total += candidate.stat().st_size
        except FileNotFoundError:
            continue
    return total


def postgres_connection_count(database_name: str) -> int:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT COUNT(*) AS count FROM pg_stat_activity "
            "WHERE datname = current_database() AND pid <> pg_backend_pid()"
        ).fetchone()
    require(row is not None, "failed to inspect PostgreSQL connections")
    return int(row["count"])


class ScenarioProbe:
    def __init__(
        self,
        api_pid: int,
        database_name: str,
        storage_roots: dict[str, Path],
    ) -> None:
        self._api_pid = api_pid
        self._database_name = database_name
        self._storage_roots = storage_roots
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._started_at = 0.0
        self._api_start: dict[str, float | int] | None = None
        self._client_start: dict[str, float | int] | None = None
        self._host_start: tuple[int, int] | None = None
        self._loopback_start = 0
        self._api_peak_rss = 0
        self._client_peak_rss = 0
        self._storage_before: dict[str, int] = {}
        self._postgres_connections = 0

    def start(self) -> None:
        self._api_start = process_sample(self._api_pid)
        self._client_start = process_sample(os.getpid())
        require(self._api_start is not None, "initial API process sample failed")
        require(self._client_start is not None, "initial client process sample failed")
        self._api_peak_rss = int(self._api_start["rss_bytes"])
        self._client_peak_rss = int(self._client_start["rss_bytes"])
        self._host_start = host_cpu_sample()
        self._loopback_start = loopback_bytes()
        self._storage_before = {
            name: directory_bytes(path) for name, path in self._storage_roots.items()
        }
        self._postgres_connections = postgres_connection_count(self._database_name)
        self._started_at = time.perf_counter()
        self._thread = threading.Thread(target=self._sample_loop, daemon=True)
        self._thread.start()

    def _sample_loop(self) -> None:
        while not self._stop_event.wait(0.01):
            api = process_sample(self._api_pid)
            client = process_sample(os.getpid())
            if api is not None:
                self._api_peak_rss = max(self._api_peak_rss, int(api["rss_bytes"]))
            if client is not None:
                self._client_peak_rss = max(
                    self._client_peak_rss, int(client["rss_bytes"])
                )

    def stop(self) -> dict[str, Any]:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=2)
        wall_seconds = time.perf_counter() - self._started_at
        api_end = process_sample(self._api_pid)
        client_end = process_sample(os.getpid())
        host_end = host_cpu_sample()
        require(api_end is not None, "final API process sample failed")
        require(client_end is not None, "final client process sample failed")
        require(self._api_start is not None, "API probe was not started")
        require(self._client_start is not None, "client probe was not started")
        require(self._host_start is not None, "host probe was not started")

        api_cpu_seconds = max(
            0.0,
            float(api_end["cpu_seconds"]) - float(self._api_start["cpu_seconds"]),
        )
        client_cpu_seconds = max(
            0.0,
            float(client_end["cpu_seconds"]) - float(self._client_start["cpu_seconds"]),
        )
        host_total_delta = host_end[0] - self._host_start[0]
        host_idle_delta = host_end[1] - self._host_start[1]
        host_busy_percent = (
            100 * (host_total_delta - host_idle_delta) / host_total_delta
            if host_total_delta > 0
            else 0.0
        )
        loopback_delta = loopback_bytes() - self._loopback_start
        storage_after = {
            name: directory_bytes(path) for name, path in self._storage_roots.items()
        }
        return {
            "wall_seconds": round(wall_seconds, 6),
            "api_cpu_seconds": round(api_cpu_seconds, 6),
            "api_cpu_cores": round(api_cpu_seconds / wall_seconds, 3),
            "client_cpu_seconds": round(client_cpu_seconds, 6),
            "client_cpu_cores": round(client_cpu_seconds / wall_seconds, 3),
            "api_peak_rss_bytes": self._api_peak_rss,
            "client_peak_rss_bytes": self._client_peak_rss,
            "host_cpu_busy_percent": round(host_busy_percent, 3),
            "loopback_bytes": loopback_delta,
            "loopback_mib_per_second": round(loopback_delta / wall_seconds / MIB, 3),
            "postgres_connections": self._postgres_connections,
            "storage": {
                name: {
                    "before_bytes": self._storage_before[name],
                    "after_bytes": storage_after[name],
                    "delta_bytes": storage_after[name] - self._storage_before[name],
                }
                for name in self._storage_roots
            },
        }


def response_object(response: httpx.Response, label: str) -> dict[str, Any]:
    try:
        payload = response.json()
    except ValueError as error:
        raise RuntimeError(
            f"{label} returned non-JSON HTTP {response.status_code}"
        ) from error
    if not isinstance(payload, dict):
        raise RuntimeError(f"{label} returned a non-object response")
    return payload


async def successful_json_request(
    client: httpx.AsyncClient,
    method: str,
    path: str,
    label: str,
    **kwargs: Any,
) -> tuple[dict[str, Any], float]:
    started = time.perf_counter_ns()
    response = await client.request(method, path, **kwargs)
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    payload = response_object(response, label)
    if response.status_code != 200 or payload.get("code") != 0:
        raise RuntimeError(
            f"{label} failed: http={response.status_code} "
            f"code={payload.get('code')} message={payload.get('message', '')}"
        )
    return payload, elapsed_ms


async def login(client: httpx.AsyncClient) -> str:
    payload, _ = await successful_json_request(
        client,
        "POST",
        "/api/auth/login",
        "benchmark login",
        json={"account": "admin", "password": "Admin123"},
    )
    token = payload.get("data", {}).get("access_token")
    if not isinstance(token, str) or not token:
        raise RuntimeError("benchmark login omitted data.access_token")
    return token


async def initialize_upload(
    client: httpx.AsyncClient,
    name: str,
    payload: bytes,
) -> tuple[UploadHandle, float]:
    file_hash = hashlib.md5(payload).hexdigest()
    body, elapsed_ms = await successful_json_request(
        client,
        "POST",
        "/api/file/upload/init",
        f"initialize {name}",
        json={
            "filename": name,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    data = body.get("data", {})
    if data.get("instant_upload") is True:
        raise RuntimeError(f"{name} unexpectedly used instant upload")
    upload_id = data.get("upload_id")
    chunk_size = data.get("chunk_size")
    total_chunks = data.get("total_chunks")
    if not isinstance(upload_id, str) or not upload_id:
        raise RuntimeError(f"initialize {name} omitted upload_id")
    if not isinstance(chunk_size, int) or chunk_size != CHUNK_SIZE:
        raise RuntimeError(
            f"initialize {name} returned chunk_size={chunk_size}, expected={CHUNK_SIZE}"
        )
    expected_chunks = math.ceil(len(payload) / chunk_size)
    if not isinstance(total_chunks, int) or total_chunks != expected_chunks:
        raise RuntimeError(
            f"initialize {name} returned total_chunks={total_chunks}, "
            f"expected={expected_chunks}"
        )
    return (
        UploadHandle(
            upload_id=upload_id,
            name=name,
            file_hash=file_hash,
            file_size=len(payload),
            chunk_size=chunk_size,
            total_chunks=total_chunks,
        ),
        elapsed_ms,
    )


def chunk_payloads(handle: UploadHandle, payload: bytes) -> list[bytes]:
    chunks = [
        payload[offset : offset + handle.chunk_size]
        for offset in range(0, len(payload), handle.chunk_size)
    ]
    require(len(chunks) == handle.total_chunks, "chunk split count changed")
    return chunks


async def upload_chunks(
    client: httpx.AsyncClient,
    handle: UploadHandle,
    payload: bytes,
    concurrency: int,
) -> tuple[list[float], float]:
    chunks = chunk_payloads(handle, payload)
    semaphore = asyncio.Semaphore(concurrency)

    async def upload_one(index: int, chunk: bytes) -> float:
        async with semaphore:
            body, elapsed_ms = await successful_json_request(
                client,
                "POST",
                "/api/file/upload/chunk",
                f"upload {handle.name} chunk {index}",
                params={
                    "upload_id": handle.upload_id,
                    "chunk_index": index,
                    "chunk_hash": hashlib.md5(chunk).hexdigest(),
                },
                headers={"Content-Type": "application/octet-stream"},
                content=chunk,
            )
            data = body.get("data", {})
            if data.get("uploaded") is not True:
                raise RuntimeError(
                    f"upload {handle.name} chunk {index} did not confirm uploaded=true"
                )
            returned_index = data.get("chunk_index")
            if returned_index is not None and returned_index != index:
                raise RuntimeError(
                    f"upload {handle.name} chunk index changed: {returned_index}"
                )
            return elapsed_ms

    started = time.perf_counter_ns()
    latencies = await asyncio.gather(
        *(upload_one(index, chunk) for index, chunk in enumerate(chunks))
    )
    wall_ms = (time.perf_counter_ns() - started) / 1_000_000
    return list(latencies), wall_ms


async def complete_upload(
    client: httpx.AsyncClient,
    handle: UploadHandle,
    payload: bytes,
) -> tuple[FileFixture, float]:
    body, elapsed_ms = await successful_json_request(
        client,
        "POST",
        "/api/file/upload/complete",
        f"complete {handle.name}",
        json={"upload_id": handle.upload_id},
    )
    file_data = body.get("data", {}).get("file", {})
    file_id = file_data.get("id")
    if not isinstance(file_id, int):
        raise RuntimeError(f"complete {handle.name} omitted numeric file id")
    if file_data.get("name") != handle.name:
        raise RuntimeError(f"complete {handle.name} returned a different filename")
    if file_data.get("hash") != handle.file_hash:
        raise RuntimeError(f"complete {handle.name} returned a different MD5")
    returned_size = file_data.get("size")
    if returned_size is not None and returned_size != handle.file_size:
        raise RuntimeError(f"complete {handle.name} returned size={returned_size}")
    return (
        FileFixture(
            file_id=file_id,
            name=handle.name,
            payload=payload,
            file_hash=handle.file_hash,
        ),
        elapsed_ms,
    )


async def upload_file(
    client: httpx.AsyncClient,
    name: str,
    payload: bytes,
    chunk_concurrency: int,
) -> CompletedUpload:
    started = time.perf_counter_ns()
    handle, init_ms = await initialize_upload(client, name, payload)
    chunk_latencies, chunk_wall_ms = await upload_chunks(
        client,
        handle,
        payload,
        min(chunk_concurrency, handle.total_chunks),
    )
    fixture, complete_ms = await complete_upload(client, handle, payload)
    logical_ms = (time.perf_counter_ns() - started) / 1_000_000
    return CompletedUpload(
        fixture=fixture,
        logical_ms=logical_ms,
        init_ms=init_ms,
        chunk_wall_ms=chunk_wall_ms,
        chunk_latencies_ms=chunk_latencies,
        complete_ms=complete_ms,
    )


async def verify_full_download(
    client: httpx.AsyncClient,
    fixture: FileFixture,
) -> None:
    response = await client.get(f"/api/file/download/{fixture.file_id}")
    if response.status_code != 200:
        raise RuntimeError(
            f"verify {fixture.name} failed with HTTP {response.status_code}"
        )
    if response.content != fixture.payload:
        raise RuntimeError(f"verify {fixture.name} returned different bytes")
    if hashlib.md5(response.content).hexdigest() != fixture.file_hash:
        raise RuntimeError(f"verify {fixture.name} returned different MD5")


async def timed_range_download(
    client: httpx.AsyncClient,
    fixture: FileFixture,
    start: int,
    length: int,
    label: str,
) -> float:
    end = start + length - 1
    started = time.perf_counter_ns()
    response = await client.get(
        f"/api/file/download/{fixture.file_id}",
        headers={"Range": f"bytes={start}-{end}"},
    )
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if response.status_code != 206:
        raise RuntimeError(
            f"{label} returned HTTP {response.status_code}, expected 206"
        )
    expected_content_range = f"bytes {start}-{end}/{len(fixture.payload)}"
    if response.headers.get("Content-Range") != expected_content_range:
        raise RuntimeError(
            f"{label} Content-Range changed: {response.headers.get('Content-Range')}"
        )
    expected = fixture.payload[start : end + 1]
    if response.content != expected:
        raise RuntimeError(f"{label} returned different Range bytes")
    return elapsed_ms


def new_probe(
    api_pid: int,
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> ScenarioProbe:
    return ScenarioProbe(
        api_pid,
        database_name,
        {"final": final_root, "staging": staging_root},
    )


async def run_small_files(
    client: httpx.AsyncClient,
    args: argparse.Namespace,
    run_prefix: str,
    api_pid: int,
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> tuple[dict[str, Any], list[FileFixture]]:
    inputs = [
        (f"{run_prefix}small_{index:04d}.bin", os.urandom(args.small_bytes))
        for index in range(args.small_files)
    ]
    semaphore = asyncio.Semaphore(args.small_concurrency)

    async def upload_owned(name: str, payload: bytes) -> CompletedUpload:
        async with semaphore:
            return await upload_file(client, name, payload, 1)

    probe = new_probe(api_pid, database_name, final_root, staging_root)
    probe.start()
    started = time.perf_counter_ns()
    uploads = await asyncio.gather(
        *(upload_owned(name, payload) for name, payload in inputs)
    )
    wall_seconds = (time.perf_counter_ns() - started) / 1_000_000_000
    resources = probe.stop()

    fixtures = [upload.fixture for upload in uploads]
    await asyncio.gather(
        *(verify_full_download(client, fixture) for fixture in fixtures)
    )
    total_bytes = sum(len(fixture.payload) for fixture in fixtures)
    logical_samples = [upload.logical_ms for upload in uploads]
    init_samples = [upload.init_ms for upload in uploads]
    chunk_samples = [
        latency for upload in uploads for latency in upload.chunk_latencies_ms
    ]
    complete_samples = [upload.complete_ms for upload in uploads]
    return (
        {
            "name": "small_files",
            "passed": True,
            "logical_operations": len(uploads),
            "successful_logical_operations": len(uploads),
            "failed_logical_operations": 0,
            "load_http_requests": sum(upload.load_http_requests for upload in uploads),
            "validation_http_requests": len(fixtures),
            "file_size_bytes": args.small_bytes,
            "concurrency": args.small_concurrency,
            "wall_seconds": round(wall_seconds, 6),
            "files_per_second": round(len(uploads) / wall_seconds, 3),
            "payload_mib_per_second": round(total_bytes / wall_seconds / MIB, 3),
            "latency_samples_ms": {
                "logical_upload": logical_samples,
                "init": init_samples,
                "chunk_request": chunk_samples,
                "complete": complete_samples,
            },
            "latency": {
                "logical_upload": summarize(logical_samples),
                "init": summarize(init_samples),
                "chunk_request": summarize(chunk_samples),
                "complete": summarize(complete_samples),
            },
            "resources": resources,
        },
        fixtures,
    )


async def run_concurrent_chunks(
    client: httpx.AsyncClient,
    args: argparse.Namespace,
    run_prefix: str,
    api_pid: int,
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> tuple[dict[str, Any], FileFixture]:
    payload = os.urandom(args.chunk_file_mib * MIB)
    name = f"{run_prefix}concurrent_chunks.bin"
    handle, init_ms = await initialize_upload(client, name, payload)

    probe = new_probe(api_pid, database_name, final_root, staging_root)
    probe.start()
    chunk_latencies, chunk_wall_ms = await upload_chunks(
        client,
        handle,
        payload,
        args.chunk_concurrency,
    )
    fixture, complete_ms = await complete_upload(client, handle, payload)
    resources = probe.stop()
    await verify_full_download(client, fixture)
    return (
        {
            "name": "concurrent_chunks",
            "passed": True,
            "logical_operations": 1,
            "successful_logical_operations": 1,
            "failed_logical_operations": 0,
            "load_http_requests": handle.total_chunks + 2,
            "timed_http_requests": handle.total_chunks + 1,
            "validation_http_requests": 1,
            "file_size_bytes": len(payload),
            "chunk_size_bytes": handle.chunk_size,
            "chunk_count": handle.total_chunks,
            "chunk_concurrency": args.chunk_concurrency,
            "init_ms": round(init_ms, 3),
            "chunk_wall_ms": round(chunk_wall_ms, 3),
            "chunk_payload_mib_per_second": round(
                len(payload) / (chunk_wall_ms / 1000) / MIB, 3
            ),
            "complete_ms": round(complete_ms, 3),
            "latency_samples_ms": {
                "chunk_request": chunk_latencies,
                "complete": [complete_ms],
            },
            "chunk_request_latency": summarize(chunk_latencies),
            "resources": resources,
        },
        fixture,
    )


async def run_large_complete(
    client: httpx.AsyncClient,
    args: argparse.Namespace,
    run_prefix: str,
    api_pid: int,
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> tuple[dict[str, Any], list[FileFixture]]:
    prepared: list[tuple[UploadHandle, bytes]] = []
    setup_init_latencies: list[float] = []
    setup_chunk_latencies: list[float] = []
    for index in range(args.large_trials):
        payload = os.urandom(args.large_file_mib * MIB)
        handle, init_ms = await initialize_upload(
            client,
            f"{run_prefix}large_{index:02d}.bin",
            payload,
        )
        chunk_latencies, _ = await upload_chunks(client, handle, payload, 1)
        prepared.append((handle, payload))
        setup_init_latencies.append(init_ms)
        setup_chunk_latencies.extend(chunk_latencies)

    probe = new_probe(api_pid, database_name, final_root, staging_root)
    probe.start()
    started = time.perf_counter_ns()
    completed = [
        await complete_upload(client, handle, payload) for handle, payload in prepared
    ]
    complete_wall_seconds = (time.perf_counter_ns() - started) / 1_000_000_000
    resources = probe.stop()
    fixtures = [fixture for fixture, _ in completed]
    complete_latencies = [latency for _, latency in completed]
    for fixture in fixtures:
        await verify_full_download(client, fixture)
    total_bytes = sum(len(fixture.payload) for fixture in fixtures)
    setup_http_requests = sum(1 + handle.total_chunks for handle, _ in prepared)
    return (
        {
            "name": "large_complete",
            "passed": True,
            "logical_operations": len(completed),
            "successful_logical_operations": len(completed),
            "failed_logical_operations": 0,
            "load_http_requests": setup_http_requests + len(completed),
            "setup_http_requests": setup_http_requests,
            "timed_http_requests": len(completed),
            "validation_http_requests": len(completed),
            "file_size_bytes": args.large_file_mib * MIB,
            "chunk_size_bytes": CHUNK_SIZE,
            "chunks_per_file": prepared[0][0].total_chunks,
            "trials": args.large_trials,
            "complete_wall_seconds": round(complete_wall_seconds, 6),
            "assembly_mib_per_second": round(
                total_bytes / complete_wall_seconds / MIB, 3
            ),
            "latency_samples_ms": {
                "setup_init": setup_init_latencies,
                "setup_chunk_request": setup_chunk_latencies,
                "complete": complete_latencies,
            },
            "setup_latency": {
                "init": summarize(setup_init_latencies),
                "chunk_request": summarize(setup_chunk_latencies),
            },
            "complete_latency": summarize(complete_latencies),
            "resources": resources,
        },
        fixtures,
    )


def deterministic_offsets(file_size: int, range_size: int, count: int) -> list[int]:
    maximum = file_size - range_size
    if maximum == 0:
        return [0] * count
    return [((index * 104_729) % (maximum + 1)) for index in range(count)]


async def run_range_downloads(
    client: httpx.AsyncClient,
    args: argparse.Namespace,
    fixture: FileFixture,
    api_pid: int,
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> dict[str, Any]:
    offsets = deterministic_offsets(
        len(fixture.payload), args.range_bytes, args.range_requests
    )
    semaphore = asyncio.Semaphore(args.range_concurrency)

    async def download_one(index: int, start: int) -> float:
        async with semaphore:
            return await timed_range_download(
                client,
                fixture,
                start,
                args.range_bytes,
                f"Range request {index}",
            )

    probe = new_probe(api_pid, database_name, final_root, staging_root)
    probe.start()
    started = time.perf_counter_ns()
    latencies = await asyncio.gather(
        *(download_one(index, start) for index, start in enumerate(offsets))
    )
    wall_seconds = (time.perf_counter_ns() - started) / 1_000_000_000
    resources = probe.stop()
    total_bytes = args.range_bytes * args.range_requests
    return {
        "name": "range_download",
        "passed": True,
        "logical_operations": args.range_requests,
        "successful_logical_operations": args.range_requests,
        "failed_logical_operations": 0,
        "load_http_requests": args.range_requests,
        "validation_http_requests": 0,
        "file_id": fixture.file_id,
        "file_size_bytes": len(fixture.payload),
        "range_size_bytes": args.range_bytes,
        "concurrency": args.range_concurrency,
        "wall_seconds": round(wall_seconds, 6),
        "ranges_per_second": round(args.range_requests / wall_seconds, 3),
        "payload_mib_per_second": round(total_bytes / wall_seconds / MIB, 3),
        "latency_samples_ms": list(latencies),
        "latency": summarize(list(latencies)),
        "resources": resources,
    }


def mixed_operation_types(operations: int, writes: int) -> list[str]:
    result: list[str] = []
    accumulator = 0
    for _ in range(operations):
        accumulator += writes
        if accumulator >= operations:
            result.append("write")
            accumulator -= operations
        else:
            result.append("read")
    require(result.count("write") == writes, "mixed operation allocation changed")
    return result


async def run_mixed_read_write(
    client: httpx.AsyncClient,
    args: argparse.Namespace,
    run_prefix: str,
    range_fixture: FileFixture,
    api_pid: int,
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> tuple[dict[str, Any], list[FileFixture]]:
    writes = mixed_write_count(args.mixed_operations, args.mixed_write_percent)
    reads = args.mixed_operations - writes
    operation_types = mixed_operation_types(args.mixed_operations, writes)
    write_inputs = [
        (f"{run_prefix}mixed_{index:04d}.bin", os.urandom(args.mixed_write_bytes))
        for index in range(writes)
    ]
    read_offsets = deterministic_offsets(
        len(range_fixture.payload), args.mixed_range_bytes, reads
    )
    operation_semaphore = asyncio.Semaphore(args.mixed_concurrency)
    write_semaphore = asyncio.Semaphore(args.mixed_write_concurrency)
    write_index = 0
    read_index = 0
    operations: list[tuple[str, int, str | None, bytes | None]] = []
    for operation_type in operation_types:
        if operation_type == "write":
            name, payload = write_inputs[write_index]
            operations.append(("write", write_index, name, payload))
            write_index += 1
        else:
            operations.append(("read", read_index, None, None))
            read_index += 1

    async def execute_operation(
        operation: tuple[str, int, str | None, bytes | None],
    ) -> tuple[str, float, CompletedUpload | None]:
        operation_type, index, name, payload = operation
        async with operation_semaphore:
            if operation_type == "read":
                latency = await timed_range_download(
                    client,
                    range_fixture,
                    read_offsets[index],
                    args.mixed_range_bytes,
                    f"mixed read {index}",
                )
                return operation_type, latency, None
            require(
                name is not None and payload is not None, "mixed write input missing"
            )
            async with write_semaphore:
                upload = await upload_file(client, name, payload, 1)
            return operation_type, upload.logical_ms, upload

    probe = new_probe(api_pid, database_name, final_root, staging_root)
    probe.start()
    started = time.perf_counter_ns()
    outcomes = await asyncio.gather(*(execute_operation(item) for item in operations))
    wall_seconds = (time.perf_counter_ns() - started) / 1_000_000_000
    resources = probe.stop()

    read_latencies = [
        latency for operation_type, latency, _ in outcomes if operation_type == "read"
    ]
    write_uploads = [
        upload
        for operation_type, _, upload in outcomes
        if operation_type == "write" and upload is not None
    ]
    require(len(read_latencies) == reads, "mixed read result count changed")
    require(len(write_uploads) == writes, "mixed write result count changed")
    fixtures = [upload.fixture for upload in write_uploads]
    await asyncio.gather(
        *(verify_full_download(client, fixture) for fixture in fixtures)
    )
    read_bytes = reads * args.mixed_range_bytes
    write_bytes = sum(len(fixture.payload) for fixture in fixtures)
    write_latencies = [upload.logical_ms for upload in write_uploads]
    return (
        {
            "name": "mixed_read_write",
            "passed": True,
            "logical_operations": args.mixed_operations,
            "successful_logical_operations": args.mixed_operations,
            "failed_logical_operations": 0,
            "read_operations": reads,
            "write_operations": writes,
            "load_http_requests": reads
            + sum(upload.load_http_requests for upload in write_uploads),
            "validation_http_requests": len(fixtures),
            "concurrency": args.mixed_concurrency,
            "write_concurrency": args.mixed_write_concurrency,
            "write_percent": round(100 * writes / args.mixed_operations, 3),
            "read_range_bytes": args.mixed_range_bytes,
            "write_file_bytes": args.mixed_write_bytes,
            "wall_seconds": round(wall_seconds, 6),
            "logical_operations_per_second": round(
                args.mixed_operations / wall_seconds, 3
            ),
            "read_mib_per_second": round(read_bytes / wall_seconds / MIB, 3),
            "write_mib_per_second": round(write_bytes / wall_seconds / MIB, 3),
            "latency_samples_ms": {
                "read": read_latencies,
                "write_logical_upload": write_latencies,
            },
            "latency": {
                "read": summarize(read_latencies),
                "write_logical_upload": summarize(write_latencies),
            },
            "resources": resources,
        },
        fixtures,
    )


def reconcile_database(
    database_name: str,
    run_prefix: str,
    fixtures: list[FileFixture],
) -> dict[str, Any]:
    expected_ids = sorted(fixture.file_id for fixture in fixtures)
    expected_bytes = sum(len(fixture.payload) for fixture in fixtures)
    expected_hashes = {fixture.file_hash for fixture in fixtures}
    require(
        len(expected_hashes) == len(fixtures), "fixture payload hashes are not unique"
    )
    prefix_length = len(run_prefix)
    with connect(database_name) as connection:
        file_rows = connection.execute(
            "SELECT id, content_id, size FROM files "
            "WHERE LEFT(name, %s) = %s ORDER BY id",
            (prefix_length, run_prefix),
        ).fetchall()
        content_row = connection.execute(
            "SELECT COUNT(*) AS count, MIN(fc.ref_count) AS min_ref_count, "
            "MAX(fc.ref_count) AS max_ref_count "
            "FROM file_contents fc JOIN files f ON f.content_id = fc.id "
            "WHERE LEFT(f.name, %s) = %s",
            (prefix_length, run_prefix),
        ).fetchone()
        task_row = connection.execute(
            "SELECT COUNT(*) AS count, "
            "COUNT(*) FILTER (WHERE status = 1) AS completed, "
            "COUNT(*) FILTER (WHERE status IN (0, 4)) AS nonterminal, "
            "COALESCE(SUM(reserved_bytes), 0) AS reserved_bytes_snapshot, "
            "COALESCE(SUM(reserved_bytes) FILTER (WHERE status IN (0, 4)), 0) "
            "AS active_reserved_bytes, "
            "MIN(EXTRACT(EPOCH FROM (expires_at - created_at))) AS min_ttl_seconds, "
            "MAX(EXTRACT(EPOCH FROM (expires_at - created_at))) AS max_ttl_seconds "
            "FROM upload_tasks WHERE LEFT(filename, %s) = %s",
            (prefix_length, run_prefix),
        ).fetchone()
        user_row = connection.execute(
            "SELECT storage_used, storage_reserved FROM users WHERE username = 'admin'"
        ).fetchone()

    observed_ids = sorted(int(row["id"]) for row in file_rows)
    observed_bytes = sum(int(row["size"]) for row in file_rows)
    require(
        observed_ids == expected_ids, "database file IDs differ from fixture ledger"
    )
    require(
        observed_bytes == expected_bytes,
        "database file bytes differ from fixture ledger",
    )
    require(content_row is not None, "content reconciliation returned no row")
    require(int(content_row["count"]) == len(fixtures), "content count changed")
    require(
        int(content_row["min_ref_count"]) == 1
        and int(content_row["max_ref_count"]) == 1,
        "unique benchmark content ref_count is not one",
    )
    require(task_row is not None, "upload task reconciliation returned no row")
    require(int(task_row["count"]) == len(fixtures), "upload task count changed")
    require(
        int(task_row["completed"]) == len(fixtures), "upload tasks are not completed"
    )
    require(int(task_row["nonterminal"]) == 0, "nonterminal upload tasks remain")
    require(
        int(task_row["reserved_bytes_snapshot"]) == expected_bytes,
        "upload task reserved-byte snapshots differ from fixture ledger",
    )
    require(
        int(task_row["active_reserved_bytes"]) == 0,
        "active upload task reservations remain",
    )
    minimum_ttl = float(task_row["min_ttl_seconds"])
    maximum_ttl = float(task_row["max_ttl_seconds"])
    require(
        UPLOAD_TASK_EXPIRY_SECONDS - 1 <= minimum_ttl <= UPLOAD_TASK_EXPIRY_SECONDS + 1
        and UPLOAD_TASK_EXPIRY_SECONDS - 1
        <= maximum_ttl
        <= UPLOAD_TASK_EXPIRY_SECONDS + 1,
        "upload task TTL was not created from the configured database-time interval",
    )
    require(user_row is not None, "admin user is missing")
    require(int(user_row["storage_used"]) == expected_bytes, "storage_used changed")
    require(int(user_row["storage_reserved"]) == 0, "storage_reserved is not zero")
    return {
        "passed": True,
        "file_count": len(file_rows),
        "file_bytes": observed_bytes,
        "content_count": int(content_row["count"]),
        "content_ref_count_min": int(content_row["min_ref_count"]),
        "content_ref_count_max": int(content_row["max_ref_count"]),
        "upload_task_count": int(task_row["count"]),
        "completed_upload_tasks": int(task_row["completed"]),
        "nonterminal_upload_tasks": int(task_row["nonterminal"]),
        "task_reserved_bytes_snapshot": int(task_row["reserved_bytes_snapshot"]),
        "active_task_reserved_bytes": int(task_row["active_reserved_bytes"]),
        "upload_task_ttl_seconds_min": minimum_ttl,
        "upload_task_ttl_seconds_max": maximum_ttl,
        "user_storage_used": int(user_row["storage_used"]),
        "user_storage_reserved": int(user_row["storage_reserved"]),
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
    disk_config = config["custom_config"]["disk"]
    disk_config["chunk_size"] = CHUNK_SIZE
    disk_config["max_file_size"] = MAX_FILE_SIZE
    disk_config["assembly_max_concurrent"] = ASSEMBLY_CONCURRENCY
    disk_config["upload_task_expiry_seconds"] = UPLOAD_TASK_EXPIRY_SECONDS
    config["db_clients"][0]["connection_number"] = DB_POOL_SIZE
    config["db_clients"][0].pop("num_connection_number", None)
    config["redis_clients"][0]["number_of_connections"] = REDIS_POOL_SIZE
    return config


def environment_metadata(current_binary: Path) -> dict[str, Any]:
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
        "postgres_client": command_output(["psql", "--version"]),
        "redis_server": command_output(["redis-server", "--version"]),
    }


async def execute_matrix(
    base_url: str,
    args: argparse.Namespace,
    run_prefix: str,
    api_pid: int,
    database_name: str,
    final_root: Path,
    staging_root: Path,
) -> tuple[list[dict[str, Any]], list[FileFixture]]:
    maximum_concurrency = max(
        args.small_concurrency,
        args.chunk_concurrency,
        args.range_concurrency,
        args.mixed_concurrency,
    )
    limits = httpx.Limits(
        max_connections=maximum_concurrency,
        max_keepalive_connections=maximum_concurrency,
    )
    timeout = httpx.Timeout(args.timeout_seconds)
    scenarios: list[dict[str, Any]] = []
    fixtures: list[FileFixture] = []
    async with httpx.AsyncClient(
        base_url=base_url,
        limits=limits,
        timeout=timeout,
    ) as client:
        token = await login(client)
        client.headers["Authorization"] = f"Bearer {token}"

        print("measuring small_files", file=sys.stderr)
        small_result, small_fixtures = await run_small_files(
            client,
            args,
            run_prefix,
            api_pid,
            database_name,
            final_root,
            staging_root,
        )
        scenarios.append(small_result)
        fixtures.extend(small_fixtures)

        print("measuring concurrent_chunks", file=sys.stderr)
        chunk_result, chunk_fixture = await run_concurrent_chunks(
            client,
            args,
            run_prefix,
            api_pid,
            database_name,
            final_root,
            staging_root,
        )
        scenarios.append(chunk_result)
        fixtures.append(chunk_fixture)

        print("measuring large_complete", file=sys.stderr)
        large_result, large_fixtures = await run_large_complete(
            client,
            args,
            run_prefix,
            api_pid,
            database_name,
            final_root,
            staging_root,
        )
        scenarios.append(large_result)
        fixtures.extend(large_fixtures)
        range_fixture = large_fixtures[-1]

        print("measuring range_download", file=sys.stderr)
        scenarios.append(
            await run_range_downloads(
                client,
                args,
                range_fixture,
                api_pid,
                database_name,
                final_root,
                staging_root,
            )
        )

        print("measuring mixed_read_write", file=sys.stderr)
        mixed_result, mixed_fixtures = await run_mixed_read_write(
            client,
            args,
            run_prefix,
            range_fixture,
            api_pid,
            database_name,
            final_root,
            staging_root,
        )
        scenarios.append(mixed_result)
        fixtures.extend(mixed_fixtures)
    return scenarios, fixtures


def run(args: argparse.Namespace) -> dict[str, Any]:
    current_binary = (
        resolve_executable(args.server_bin, [], "Disk server")
        if args.server_bin is not None
        else resolve_current_binary()
    )
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_storage_workloads_{suffix}"
    run_prefix = f"bench_{suffix}_"
    instance_id = f"storage-workloads-{suffix}"
    database_created = False
    server: ManagedServer | None = None
    started_at = datetime.now(timezone.utc)
    scenarios: list[dict[str, Any]] = []
    fixtures: list[FileFixture] = []
    reconciliation: dict[str, Any] = {}

    with tempfile.TemporaryDirectory(prefix="disk-storage-workloads-") as temporary:
        temporary_root = Path(temporary)
        final_root = temporary_root / "storage" / "final"
        staging_root = temporary_root / "storage" / "staging"
        port = allocate_ports(1)[0]
        try:
            create_database(database_name)
            database_created = True
            run_database_command(
                ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
                database_name,
            )
            server = ManagedServer(
                name=instance_id,
                binary=current_binary,
                run_directory=temporary_root / "run",
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
            scenarios, fixtures = asyncio.run(
                execute_matrix(
                    server.base_url,
                    args,
                    run_prefix,
                    server.pid,
                    database_name,
                    final_root,
                    staging_root,
                )
            )
            server.require_running("storage workload matrix")
            reconciliation = reconcile_database(database_name, run_prefix, fixtures)
        except BaseException:
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
            raise
        finally:
            if server is not None:
                server.stop()
                if server.log_handle is not None:
                    server.log_handle.close()
                    server.log_handle = None
            if database_created:
                drop_database(database_name)

    completed_at = datetime.now(timezone.utc)
    total_load_requests = sum(
        int(scenario["load_http_requests"]) for scenario in scenarios
    )
    return {
        "schema_version": 1,
        "scenario": "storage_workload_matrix",
        "started_at": started_at.isoformat(),
        "completed_at": completed_at.isoformat(),
        "elapsed_seconds": round((completed_at - started_at).total_seconds(), 3),
        "git": git_metadata(),
        "environment": environment_metadata(current_binary),
        "parameters": {
            "storage_backend": "local",
            "api_instances": 1,
            "api_threads": args.api_threads,
            "chunk_size_bytes": CHUNK_SIZE,
            "max_file_size_bytes": MAX_FILE_SIZE,
            "assembly_max_concurrent": ASSEMBLY_CONCURRENCY,
            "upload_task_expiry_seconds": UPLOAD_TASK_EXPIRY_SECONDS,
            "db_pool_size": DB_POOL_SIZE,
            "redis_pool_size": REDIS_POOL_SIZE,
            "small_files": args.small_files,
            "small_bytes": args.small_bytes,
            "small_concurrency": args.small_concurrency,
            "chunk_file_bytes": args.chunk_file_mib * MIB,
            "chunk_concurrency": args.chunk_concurrency,
            "large_file_bytes": args.large_file_mib * MIB,
            "large_trials": args.large_trials,
            "range_requests": args.range_requests,
            "range_bytes": args.range_bytes,
            "range_concurrency": args.range_concurrency,
            "mixed_operations": args.mixed_operations,
            "mixed_concurrency": args.mixed_concurrency,
            "mixed_write_percent_requested": args.mixed_write_percent,
            "mixed_write_bytes": args.mixed_write_bytes,
            "mixed_range_bytes": args.mixed_range_bytes,
            "mixed_write_concurrency": args.mixed_write_concurrency,
        },
        "scenarios": scenarios,
        "reconciliation": reconciliation,
        "acceptance": {
            "scenario_count": len(scenarios),
            "all_scenarios_passed": len(scenarios) == 5
            and all(bool(scenario["passed"]) for scenario in scenarios),
            "total_load_http_requests": total_load_requests,
            "total_failed_logical_operations": sum(
                int(scenario["failed_logical_operations"]) for scenario in scenarios
            ),
            "reconciliation_passed": reconciliation.get("passed") is True,
            "passed": len(scenarios) == 5
            and all(bool(scenario["passed"]) for scenario in scenarios)
            and reconciliation.get("passed") is True,
        },
    }


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (
        AssertionError,
        httpx.HTTPError,
        OSError,
        RuntimeError,
        subprocess.SubprocessError,
    ) as error:
        print(f"storage workload benchmark failed: {error}", file=sys.stderr)
        return 1

    serialized = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if result["acceptance"]["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""Measure single-instance upload lifecycle latency with owned fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import statistics
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import httpx


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base-url",
        default=os.environ.get("BENCH_HOST", "http://127.0.0.1:8080"),
    )
    parser.add_argument("--account", default=os.environ.get("BENCH_ACCOUNT", "admin"))
    parser.add_argument(
        "--password",
        default=os.environ.get("BENCH_PASSWORD", "Admin123"),
    )
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--payload-bytes", type=int, default=1024 * 1024)
    parser.add_argument("--timeout-seconds", type=float, default=60.0)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--keep-fixtures", action="store_true")
    args = parser.parse_args()
    if args.iterations <= 0:
        parser.error("--iterations must be positive")
    if args.payload_bytes < 1024:
        parser.error("--payload-bytes must be at least 1024")
    return args


def response_body(response: httpx.Response) -> dict[str, Any]:
    try:
        body = response.json()
    except ValueError as exc:
        raise RuntimeError(
            f"HTTP {response.status_code} returned a non-JSON response"
        ) from exc
    if not isinstance(body, dict):
        raise RuntimeError(f"HTTP {response.status_code} returned a non-object response")
    return body


def require_success(response: httpx.Response, expected_status: int = 200) -> dict[str, Any]:
    body = response_body(response)
    if response.status_code != expected_status or body.get("code") != 0:
        raise RuntimeError(
            "request failed: "
            f"http={response.status_code} code={body.get('code')} "
            f"message={body.get('message', '')}"
        )
    return body


def timed_request(
    client: httpx.Client,
    method: str,
    path: str,
    *,
    expected_status: int = 200,
    expect_envelope: bool = True,
    **kwargs: Any,
) -> tuple[httpx.Response, dict[str, Any], float]:
    started = time.perf_counter_ns()
    response = client.request(method, path, **kwargs)
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if expect_envelope:
        body = require_success(response, expected_status)
    else:
        if response.status_code != expected_status:
            raise RuntimeError(
                f"request failed: http={response.status_code}, expected={expected_status}"
            )
        body = {}
    return response, body, elapsed_ms


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * fraction) - 1)
    return ordered[index]


def summarize(values: list[float]) -> dict[str, float | int]:
    return {
        "count": len(values),
        "min_ms": round(min(values), 3),
        "p50_ms": round(percentile(values, 0.50), 3),
        "p95_ms": round(percentile(values, 0.95), 3),
        "p99_ms": round(percentile(values, 0.99), 3),
        "max_ms": round(max(values), 3),
        "mean_ms": round(statistics.fmean(values), 3),
    }


def login(client: httpx.Client, account: str, password: str) -> str:
    response = client.post(
        "/api/auth/login",
        json={"account": account, "password": password},
    )
    body = require_success(response)
    token = body.get("data", {}).get("access_token")
    if not isinstance(token, str) or not token:
        raise RuntimeError("login response did not contain data.access_token")
    return token


def initialize_upload(
    client: httpx.Client,
    filename: str,
    payload: bytes,
    *,
    measure: bool,
) -> tuple[str, float | None]:
    file_hash = hashlib.md5(payload).hexdigest()
    started = time.perf_counter_ns()
    response = client.post(
        "/api/file/upload/init",
        json={
            "filename": filename,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    body = require_success(response)
    data = body.get("data", {})
    if data.get("instant_upload") is True:
        raise RuntimeError("random benchmark payload unexpectedly hit instant upload")
    upload_id = data.get("upload_id")
    if not isinstance(upload_id, str) or not upload_id:
        raise RuntimeError("upload init response did not contain data.upload_id")
    return upload_id, elapsed_ms if measure else None


def upload_chunk(
    client: httpx.Client,
    upload_id: str,
    payload: bytes,
    *,
    measure: bool,
) -> float | None:
    chunk_hash = hashlib.md5(payload).hexdigest()
    started = time.perf_counter_ns()
    response = client.post(
        "/api/file/upload/chunk",
        params={
            "upload_id": upload_id,
            "chunk_index": 0,
            "chunk_hash": chunk_hash,
        },
        headers={"Content-Type": "application/octet-stream"},
        content=payload,
    )
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    body = require_success(response)
    if body.get("data", {}).get("uploaded") is not True:
        raise RuntimeError("chunk response did not confirm data.uploaded=true")
    return elapsed_ms if measure else None


def list_owned_trash_ids(client: httpx.Client, file_ids: set[int]) -> list[int]:
    owned: list[int] = []
    page = 1
    while True:
        response = client.get(
            "/api/trash",
            params={"page": page, "page_size": 100},
        )
        data = require_success(response).get("data", {})
        items = data.get("items", [])
        for item in items:
            if item.get("original_id") in file_ids and isinstance(item.get("id"), int):
                owned.append(item["id"])
        total_pages = data.get("pagination", {}).get("total_pages", 1)
        if not isinstance(total_pages, int) or page >= total_pages:
            return owned
        page += 1


def cleanup_files(client: httpx.Client, file_ids: list[int]) -> dict[str, float | int]:
    if not file_ids:
        return {"file_count": 0}
    started = time.perf_counter_ns()
    require_success(client.request("DELETE", "/api/file", json={"file_ids": file_ids}))
    soft_delete_ms = (time.perf_counter_ns() - started) / 1_000_000

    started = time.perf_counter_ns()
    trash_ids = list_owned_trash_ids(client, set(file_ids))
    trash_lookup_ms = (time.perf_counter_ns() - started) / 1_000_000
    if len(trash_ids) != len(file_ids):
        raise RuntimeError(
            f"cleanup found {len(trash_ids)} of {len(file_ids)} owned trash records"
        )

    started = time.perf_counter_ns()
    require_success(
        client.request("DELETE", "/api/trash", json={"trash_ids": trash_ids})
    )
    permanent_delete_ms = (time.perf_counter_ns() - started) / 1_000_000
    total_ms = soft_delete_ms + trash_lookup_ms + permanent_delete_ms
    return {
        "file_count": len(file_ids),
        "soft_delete_ms": round(soft_delete_ms, 3),
        "trash_lookup_ms": round(trash_lookup_ms, 3),
        "permanent_delete_ms": round(permanent_delete_ms, 3),
        "total_ms": round(total_ms, 3),
        "files_per_second": round(len(file_ids) / (total_ms / 1000), 3),
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    timings: dict[str, list[float]] = {
        "upload_init": [],
        "upload_chunk": [],
        "upload_complete": [],
        "range_download": [],
        "upload_cancel": [],
    }
    created_file_ids: list[int] = []
    active_upload_ids: set[str] = set()
    cleanup_result: dict[str, float | int] | None = None
    run_id = uuid.uuid4().hex[:12]
    started_at = datetime.now(timezone.utc)

    with httpx.Client(
        base_url=args.base_url.rstrip("/"),
        timeout=args.timeout_seconds,
    ) as client:
        token = login(client, args.account, args.password)
        client.headers["Authorization"] = f"Bearer {token}"

        try:
            for index in range(args.iterations):
                payload = os.urandom(args.payload_bytes)
                upload_id, init_ms = initialize_upload(
                    client,
                    f"dist_baseline_{run_id}_{index}.bin",
                    payload,
                    measure=True,
                )
                active_upload_ids.add(upload_id)
                timings["upload_init"].append(init_ms or 0.0)

                chunk_ms = upload_chunk(client, upload_id, payload, measure=True)
                timings["upload_chunk"].append(chunk_ms or 0.0)

                _, complete_body, complete_ms = timed_request(
                    client,
                    "POST",
                    "/api/file/upload/complete",
                    json={"upload_id": upload_id},
                )
                timings["upload_complete"].append(complete_ms)
                active_upload_ids.discard(upload_id)
                file_id = complete_body.get("data", {}).get("file", {}).get("id")
                if not isinstance(file_id, int):
                    raise RuntimeError("complete response did not contain numeric data.file.id")
                created_file_ids.append(file_id)

                range_response, _, range_ms = timed_request(
                    client,
                    "GET",
                    f"/api/file/download/{file_id}",
                    expected_status=206,
                    expect_envelope=False,
                    headers={"Range": "bytes=0-1023"},
                )
                if range_response.content != payload[:1024]:
                    raise RuntimeError("Range response bytes did not match uploaded payload")
                timings["range_download"].append(range_ms)

                cancel_payload = os.urandom(1024 + index)
                cancel_id, _ = initialize_upload(
                    client,
                    f"dist_cancel_{run_id}_{index}.bin",
                    cancel_payload,
                    measure=False,
                )
                active_upload_ids.add(cancel_id)
                upload_chunk(client, cancel_id, cancel_payload, measure=False)
                _, _, cancel_ms = timed_request(
                    client,
                    "DELETE",
                    f"/api/file/upload/{cancel_id}",
                )
                active_upload_ids.discard(cancel_id)
                timings["upload_cancel"].append(cancel_ms)
        finally:
            for upload_id in sorted(active_upload_ids):
                client.delete(f"/api/file/upload/{upload_id}")
            if created_file_ids and not args.keep_fixtures:
                cleanup_result = cleanup_files(client, created_file_ids)

    completed_at = datetime.now(timezone.utc)
    return {
        "schema_version": 1,
        "started_at": started_at.isoformat(),
        "completed_at": completed_at.isoformat(),
        "base_url": args.base_url,
        "iterations": args.iterations,
        "payload_bytes": args.payload_bytes,
        "range_bytes": 1024,
        "fixtures_kept": args.keep_fixtures,
        "elapsed_seconds": round((completed_at - started_at).total_seconds(), 3),
        "latency": {name: summarize(values) for name, values in timings.items()},
        "cleanup": cleanup_result,
    }


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (httpx.HTTPError, RuntimeError) as exc:
        print(f"benchmark failed: {exc}", file=sys.stderr)
        return 1

    output = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Gate an upload rollback on frozen ingress and a read-only task snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

import psycopg
from psycopg.rows import dict_row


SCHEMA_VERSION = 1
SCENARIO = "upload-rollback-preparation"
FROZEN_CODE = 50013
FROZEN_MESSAGE = "Upload lifecycle is temporarily frozen for rollback"
MAX_RESPONSE_BYTES = 64 * 1024
MAX_ACTIVE_TASK_SAMPLE = 100
STATUS_NAMES = {
    0: "in_progress",
    1: "completed",
    2: "cancelled",
    3: "expired",
    4: "finalizing",
    5: "failed",
}


class GateInputError(ValueError):
    """Raised when rollback gate input cannot be interpreted safely."""


def parse_positive_seconds(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number") from error
    if parsed <= 0 or parsed > 30:
        raise argparse.ArgumentTypeError("must be greater than zero and at most 30")
    return parsed


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Verify compatible APIs, frozen upload ingress, and PostgreSQL upload "
            "state before an application rollback."
        )
    )
    parser.add_argument("--mode", required=True, choices=("drain", "freeze"))
    parser.add_argument(
        "--api-url",
        required=True,
        action="append",
        help="direct compatible API base URL; repeat for every instance",
    )
    parser.add_argument("--ingress-url", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--http-timeout-seconds",
        type=parse_positive_seconds,
        default=5.0,
    )
    return parser.parse_args()


def normalize_base_url(value: str, label: str) -> str:
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise GateInputError(f"{label} must be an absolute HTTP(S) base URL")
    if parsed.username is not None or parsed.password is not None:
        raise GateInputError(f"{label} must not contain credentials")
    if parsed.path not in {"", "/"} or parsed.query or parsed.fragment:
        raise GateInputError(f"{label} must not contain a path, query, or fragment")
    return urllib.parse.urlunsplit((parsed.scheme, parsed.netloc, "", "", ""))


def request_json(url: str, method: str, timeout_seconds: float) -> dict[str, Any]:
    body = b"{}" if method == "POST" else None
    request = urllib.request.Request(
        url,
        data=body,
        method=method,
        headers={"Accept": "application/json", "Content-Type": "application/json"},
    )
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        response = opener.open(request, timeout=timeout_seconds)
    except urllib.error.HTTPError as error:
        status = error.code
        headers = {key.lower(): value for key, value in error.headers.items()}
        raw_body = error.read(MAX_RESPONSE_BYTES + 1)
    except (OSError, urllib.error.URLError) as error:
        return {
            "error": type(error).__name__,
            "headers": {},
            "http_status": None,
            "json": None,
        }
    else:
        with response:
            status = response.status
            headers = {key.lower(): value for key, value in response.headers.items()}
            raw_body = response.read(MAX_RESPONSE_BYTES + 1)

    if len(raw_body) > MAX_RESPONSE_BYTES:
        return {
            "error": "response_too_large",
            "headers": headers,
            "http_status": status,
            "json": None,
        }
    try:
        payload = json.loads(raw_body)
    except (UnicodeDecodeError, json.JSONDecodeError):
        payload = None
    return {
        "error": None if isinstance(payload, dict) else "invalid_json",
        "headers": headers,
        "http_status": status,
        "json": payload if isinstance(payload, dict) else None,
    }


def probe_api(base_url: str, index: int, timeout_seconds: float) -> dict[str, Any]:
    response = request_json(
        base_url + "/api/health/ready",
        "GET",
        timeout_seconds,
    )
    payload = response["json"]
    data = payload.get("data") if isinstance(payload, dict) else None
    if not isinstance(data, dict):
        data = {}
    return {
        "probe_index": index,
        "http_status": response["http_status"],
        "response_error": response["error"],
        "code": payload.get("code") if isinstance(payload, dict) else None,
        "overall_status": data.get("overall_status"),
        "role": data.get("role"),
        "instance_id": data.get("instance_id"),
        "initialized": data.get("initialized"),
        "draining": data.get("draining"),
        "worker_claiming_enabled": data.get("worker_claiming_enabled"),
        "upload_task_creation_enabled": data.get("upload_task_creation_enabled"),
        "business_requests_inflight": data.get("business_requests_inflight"),
        "version": data.get("version"),
    }


def probe_ingress(base_url: str, timeout_seconds: float) -> dict[str, Any]:
    response = request_json(
        base_url + "/api/file/upload/init",
        "POST",
        timeout_seconds,
    )
    payload = response["json"]
    return {
        "http_status": response["http_status"],
        "response_error": response["error"],
        "code": payload.get("code") if isinstance(payload, dict) else None,
        "message": payload.get("message") if isinstance(payload, dict) else None,
        "data_is_null": (
            isinstance(payload, dict)
            and "data" in payload
            and payload.get("data") is None
        ),
        "retry_after": response["headers"].get("retry-after"),
        "cache_control": response["headers"].get("cache-control"),
    }


def timestamp_text(value: object) -> str | None:
    if value is None:
        return None
    isoformat = getattr(value, "isoformat", None)
    return isoformat(timespec="microseconds") if callable(isoformat) else str(value)


def descriptor_for_digest(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(row["id"]),
        "status": int(row["status"]),
        "staging_backend": str(row["staging_backend"]),
        "staging_prefix": row["staging_prefix"],
        "state_version": int(row["state_version"]),
        "lease_owner": row["lease_owner"],
        "lease_expires_at": timestamp_text(row["lease_expires_at"]),
    }


def database_snapshot(database_url: str) -> dict[str, Any]:
    with psycopg.connect(
        database_url,
        connect_timeout=5,
        row_factory=dict_row,
    ) as connection:
        with connection.transaction():
            connection.execute(
                "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY"
            )
            connection.execute("SET LOCAL statement_timeout = '5s'")
            snapshot_row = connection.execute(
                "SELECT CURRENT_TIMESTAMP AS snapshot_at"
            ).fetchone()
            status_rows = connection.execute(
                "SELECT status, staging_backend, COUNT(*)::bigint AS task_count "
                "FROM upload_tasks GROUP BY status, staging_backend "
                "ORDER BY status, staging_backend"
            ).fetchall()
            active_rows = connection.execute(
                "SELECT id, status, staging_backend, staging_prefix, state_version, "
                "lease_owner, lease_expires_at, "
                "CASE WHEN status = 4 THEN lease_expires_at > CURRENT_TIMESTAMP "
                "ELSE NULL END AS lease_active "
                "FROM upload_tasks WHERE status IN (0, 4) ORDER BY id"
            ).fetchall()

    if snapshot_row is None:
        raise RuntimeError("database snapshot returned no timestamp")

    status_counts = {name: 0 for name in STATUS_NAMES.values()}
    backend_status_counts: dict[str, dict[str, int]] = {}
    for row in status_rows:
        status = int(row["status"])
        status_name = STATUS_NAMES.get(status, f"unknown_{status}")
        count = int(row["task_count"])
        status_counts[status_name] = status_counts.get(status_name, 0) + count
        backend = str(row["staging_backend"])
        backend_status_counts.setdefault(backend, {})[status_name] = count

    descriptors = [descriptor_for_digest(dict(row)) for row in active_rows]
    serialized = json.dumps(
        descriptors,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    finalizing_with_active_lease = sum(
        1
        for row in active_rows
        if int(row["status"]) == 4 and row["lease_active"] is True
    )
    finalizing_with_expired_lease = sum(
        1
        for row in active_rows
        if int(row["status"]) == 4 and row["lease_active"] is False
    )
    return {
        "snapshot_at": timestamp_text(snapshot_row["snapshot_at"]),
        "transaction": "repeatable_read_read_only",
        "status_counts": status_counts,
        "backend_status_counts": backend_status_counts,
        "active_task_count": len(descriptors),
        "finalizing_with_active_lease": finalizing_with_active_lease,
        "finalizing_with_expired_lease": finalizing_with_expired_lease,
        "active_descriptor_sha256": hashlib.sha256(serialized).hexdigest(),
        "active_tasks_sample": descriptors[:MAX_ACTIVE_TASK_SAMPLE],
        "active_tasks_truncated": len(descriptors) > MAX_ACTIVE_TASK_SAMPLE,
    }


def write_evidence(path: Path, evidence: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=f".{path.name}.",
    )
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(evidence, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_name, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except BaseException:
        try:
            os.close(descriptor)
        except OSError:
            pass
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    arguments = parse_arguments()
    errors: list[str] = []
    operational_error = False

    try:
        api_urls = [
            normalize_base_url(value, f"api-url[{index}]")
            for index, value in enumerate(arguments.api_url)
        ]
        ingress_url = normalize_base_url(arguments.ingress_url, "ingress-url")
    except GateInputError as error:
        evidence = {
            "schema_version": SCHEMA_VERSION,
            "scenario": SCENARIO,
            "requested_disposition": arguments.mode,
            "acceptance": {"errors": [str(error)], "passed": False},
        }
        write_evidence(arguments.output, evidence)
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    api_probes = [
        probe_api(url, index, arguments.http_timeout_seconds)
        for index, url in enumerate(api_urls)
    ]
    ingress_probe = probe_ingress(ingress_url, arguments.http_timeout_seconds)

    instance_ids = [
        probe["instance_id"]
        for probe in api_probes
        if isinstance(probe["instance_id"], str) and probe["instance_id"]
    ]
    api_checks = {
        "all_readiness_responses_are_healthy": all(
            probe["http_status"] == 200
            and probe["response_error"] is None
            and probe["code"] == 0
            and probe["overall_status"] == "healthy"
            and probe["initialized"] is True
            and probe["draining"] is False
            for probe in api_probes
        ),
        "all_roles_are_api_only": all(
            probe["role"] == "api" and probe["worker_claiming_enabled"] is False
            for probe in api_probes
        ),
        "instance_ids_are_present_and_unique": (
            len(instance_ids) == len(api_probes)
            and len(set(instance_ids)) == len(instance_ids)
        ),
        "new_task_creation_is_closed": all(
            probe["upload_task_creation_enabled"] is False for probe in api_probes
        ),
        "business_requests_are_drained": all(
            type(probe["business_requests_inflight"]) is int
            and probe["business_requests_inflight"] == 0
            for probe in api_probes
        ),
    }
    api_messages = {
        "all_readiness_responses_are_healthy": "every compatible API readiness probe must be healthy",
        "all_roles_are_api_only": "every compatible handler must run the api-only role",
        "instance_ids_are_present_and_unique": "compatible API instance IDs must be present and unique",
        "new_task_creation_is_closed": "every compatible API must report upload task creation disabled",
        "business_requests_are_drained": "every compatible API must report zero in-flight business requests",
    }
    for name, passed in api_checks.items():
        if not passed:
            errors.append(api_messages[name])

    ingress_frozen = (
        ingress_probe["http_status"] == 503
        and ingress_probe["response_error"] is None
        and ingress_probe["code"] == FROZEN_CODE
        and ingress_probe["message"] == FROZEN_MESSAGE
        and ingress_probe["data_is_null"] is True
        and ingress_probe["retry_after"] == "30"
        and isinstance(ingress_probe["cache_control"], str)
        and "no-store" in ingress_probe["cache_control"].lower()
    )
    if not ingress_frozen:
        errors.append(
            "public upload ingress must return the reviewed 503/50013 freeze response"
        )

    database_url = os.environ.get("DISK_DATABASE_URL", "")
    snapshot: dict[str, Any] | None = None
    if not database_url:
        errors.append("DISK_DATABASE_URL is required")
        operational_error = True
    else:
        try:
            snapshot = database_snapshot(database_url)
        except (psycopg.Error, RuntimeError):
            errors.append("PostgreSQL rollback snapshot failed")
            operational_error = True

    active_tasks_drained = (
        snapshot is not None
        and snapshot["status_counts"]["in_progress"] == 0
        and snapshot["status_counts"]["finalizing"] == 0
    )
    disposition_allowed = arguments.mode == "freeze" or active_tasks_drained
    if not disposition_allowed:
        errors.append("drain mode requires zero InProgress and zero Finalizing tasks")

    checks = {
        **api_checks,
        "public_upload_ingress_is_frozen": ingress_frozen,
        "database_snapshot_completed": snapshot is not None,
        "requested_disposition_is_allowed": disposition_allowed,
    }
    passed = not errors
    active_task_count = snapshot["active_task_count"] if snapshot is not None else None
    compatible_handlers_required = (
        arguments.mode == "freeze"
        and isinstance(active_task_count, int)
        and active_task_count > 0
    )
    evidence = {
        "schema_version": SCHEMA_VERSION,
        "scenario": SCENARIO,
        "requested_disposition": arguments.mode,
        "checks": checks,
        "api_instances": api_probes,
        "ingress": ingress_probe,
        "database": snapshot,
        "decision": {
            "active_task_disposition": arguments.mode if passed else "blocked",
            "compatible_handlers_required": compatible_handlers_required,
            "contract_migration_allowed": False,
            "old_release_upload_route_allowed": False,
            "schema_action": "preserve_expand",
            "upload_ingress": "closed" if ingress_frozen else "unverified",
        },
        "acceptance": {"errors": errors, "passed": passed},
    }
    write_evidence(arguments.output, evidence)

    if passed:
        print(
            f"PASS: upload rollback {arguments.mode} gate accepted with "
            f"{active_task_count} active task(s)"
        )
        return 0
    print("BLOCKED: upload rollback preparation gate did not pass", file=sys.stderr)
    return 2 if operational_error else 1


if __name__ == "__main__":
    raise SystemExit(main())

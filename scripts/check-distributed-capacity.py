#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Validate a distributed replica plan against measured and dependency budgets."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
SCENARIO = "distributed-capacity-plan"
MAX_MEASURED_REPLICAS = {"api": 4, "worker": 4}
MEASURED_PROFILE = {
    "assembly_concurrency_slots": 2,
    "authentication_cpu_threads": 4,
    "http_threads": 4,
    "postgresql_connections": 8,
    "redis_connections": 4,
    "s3_http_connections": 16,
    "s3_io_threads": 4,
    "worker_concurrency_slots": 1,
}
HARD_RESOURCES = (
    "postgresql_connections",
    "redis_connections",
    "s3_http_connections",
    "s3_io_threads",
)
TOPOLOGY_PATTERN = re.compile(
    r"(?P<name>[A-Za-z0-9][A-Za-z0-9_-]{0,63})=(?P<api>[0-9]+):(?P<worker>[0-9]+)"
)


class CapacityInputError(ValueError):
    """Raised when a capacity plan input cannot be evaluated safely."""


@dataclass(frozen=True)
class Topology:
    name: str
    api_replicas: int
    worker_replicas: int


def positive_integer(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check an API/Worker replica plan against the repository capacity contract."
    )
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument(
        "--topology",
        required=True,
        action="append",
        metavar="NAME=API:WORKER",
        help="named replica step; repeat for every proposed topology",
    )
    parser.add_argument(
        "--postgres-max-connections", required=True, type=positive_integer
    )
    parser.add_argument(
        "--redis-connection-budget", required=True, type=positive_integer
    )
    parser.add_argument(
        "--s3-http-connection-budget", required=True, type=positive_integer
    )
    parser.add_argument("--s3-io-thread-budget", required=True, type=positive_integer)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def require_positive_integer(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise CapacityInputError(f"{label} must be a positive integer")
    return value


def get_mapping(value: object, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise CapacityInputError(f"{label} must be an object")
    return value


def get_default_client(config: dict[str, Any], key: str) -> dict[str, Any]:
    clients = config.get(key)
    if not isinstance(clients, list):
        raise CapacityInputError(f"{key} must be an array")
    matches = [
        client
        for client in clients
        if isinstance(client, dict) and client.get("name") == "default"
    ]
    if len(matches) != 1:
        raise CapacityInputError(f"{key} must contain exactly one default client")
    return matches[0]


def read_profile(config_path: Path) -> tuple[str, dict[str, int]]:
    try:
        raw_config = config_path.read_bytes()
    except OSError as error:
        raise CapacityInputError(
            f"cannot read distributed config: {error.strerror}"
        ) from error

    config_sha256 = hashlib.sha256(raw_config).hexdigest()
    try:
        parsed = json.loads(raw_config)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CapacityInputError(
            "distributed config is not valid UTF-8 JSON"
        ) from error

    config = get_mapping(parsed, "distributed config")
    app = get_mapping(config.get("app"), "app")
    database = get_default_client(config, "db_clients")
    redis = get_default_client(config, "redis_clients")
    custom_config = get_mapping(config.get("custom_config"), "custom_config")
    disk = get_mapping(custom_config.get("disk"), "custom_config.disk")
    s3 = get_mapping(disk.get("s3"), "custom_config.disk.s3")

    profile = {
        "assembly_concurrency_slots": require_positive_integer(
            disk.get("assembly_max_concurrent"),
            "custom_config.disk.assembly_max_concurrent",
        ),
        "authentication_cpu_threads": require_positive_integer(
            disk.get("auth_cpu_pool_threads"),
            "custom_config.disk.auth_cpu_pool_threads",
        ),
        "http_threads": require_positive_integer(
            app.get("threads_num"), "app.threads_num"
        ),
        "postgresql_connections": require_positive_integer(
            database.get("connection_number"), "db_clients.default.connection_number"
        ),
        "redis_connections": require_positive_integer(
            redis.get("number_of_connections"),
            "redis_clients.default.number_of_connections",
        ),
        "s3_http_connections": require_positive_integer(
            s3.get("max_connections"), "custom_config.disk.s3.max_connections"
        ),
        "s3_io_threads": require_positive_integer(
            s3.get("io_threads"), "custom_config.disk.s3.io_threads"
        ),
        "worker_concurrency_slots": require_positive_integer(
            disk.get("worker_concurrency"), "custom_config.disk.worker_concurrency"
        ),
    }
    return config_sha256, profile


def parse_topologies(values: list[str]) -> list[Topology]:
    topologies: list[Topology] = []
    names: set[str] = set()
    for value in values:
        match = TOPOLOGY_PATTERN.fullmatch(value)
        if match is None:
            raise CapacityInputError(
                f"invalid topology {value!r}; expected NAME=API:WORKER"
            )
        name = match.group("name")
        if name in names:
            raise CapacityInputError(f"duplicate topology name: {name}")
        api_replicas = int(match.group("api"))
        worker_replicas = int(match.group("worker"))
        if api_replicas <= 0 or worker_replicas <= 0:
            raise CapacityInputError(
                f"topology {name} must use positive API and Worker replica counts"
            )
        names.add(name)
        topologies.append(Topology(name, api_replicas, worker_replicas))
    return topologies


def budget_limits(arguments: argparse.Namespace) -> dict[str, int]:
    return {
        "postgresql_connections": arguments.postgres_max_connections,
        "redis_connections": arguments.redis_connection_budget,
        "s3_http_connections": arguments.s3_http_connection_budget,
        "s3_io_threads": arguments.s3_io_thread_budget,
    }


def aggregate_profile(profile: dict[str, int], topology: Topology) -> dict[str, int]:
    process_count = topology.api_replicas + topology.worker_replicas
    return {
        "application_processes": process_count,
        "assembly_concurrency_slots": (
            profile["assembly_concurrency_slots"] * topology.api_replicas
        ),
        "authentication_cpu_threads": (
            profile["authentication_cpu_threads"] * topology.api_replicas
        ),
        "http_threads": profile["http_threads"] * process_count,
        "postgresql_connections": profile["postgresql_connections"] * process_count,
        "redis_connections": profile["redis_connections"] * process_count,
        "s3_http_connections": profile["s3_http_connections"] * process_count,
        "s3_io_threads": profile["s3_io_threads"] * process_count,
        "worker_concurrency_slots": (
            profile["worker_concurrency_slots"] * topology.worker_replicas
        ),
    }


def evaluate_topology(
    profile: dict[str, int], limits: dict[str, int], topology: Topology
) -> dict[str, Any]:
    aggregate = aggregate_profile(profile, topology)
    boundary_check = {
        "api": {
            "limit": MAX_MEASURED_REPLICAS["api"],
            "passed": topology.api_replicas <= MAX_MEASURED_REPLICAS["api"],
            "replicas": topology.api_replicas,
        },
        "worker": {
            "limit": MAX_MEASURED_REPLICAS["worker"],
            "passed": topology.worker_replicas <= MAX_MEASURED_REPLICAS["worker"],
            "replicas": topology.worker_replicas,
        },
    }
    boundary_check["passed"] = all(
        boundary_check[role]["passed"] for role in ("api", "worker")
    )

    budget_checks: dict[str, dict[str, int | bool]] = {}
    required_with_reserve: dict[str, int] = {}
    for resource in HARD_RESOURCES:
        required = aggregate[resource] + profile[resource]
        required_with_reserve[resource] = required
        limit = limits[resource]
        budget_checks[resource] = {
            "budget": limit,
            "headroom": limit - required,
            "passed": required <= limit,
            "required": required,
            "rolling_replacement_reserve": profile[resource],
        }

    passed = (
        profile == MEASURED_PROFILE
        and bool(boundary_check["passed"])
        and all(bool(check["passed"]) for check in budget_checks.values())
    )
    return {
        "aggregate": aggregate,
        "budget_checks": budget_checks,
        "measured_boundary_check": boundary_check,
        "name": topology.name,
        "passed": passed,
        "replicas": {
            "api": topology.api_replicas,
            "worker": topology.worker_replicas,
        },
        "required_with_rolling_reserve": required_with_reserve,
    }


def rejection_reasons(
    profile_matches: bool, topology_results: list[dict[str, Any]]
) -> list[str]:
    reasons: list[str] = []
    if not profile_matches:
        reasons.append(
            "distributed config differs from the measured per-process profile"
        )
    for result in topology_results:
        name = result["name"]
        boundary = result["measured_boundary_check"]
        for role in ("api", "worker"):
            check = boundary[role]
            if not check["passed"]:
                reasons.append(
                    f"topology {name} exceeds measured {role} replica limit: "
                    f"{check['replicas']} > {check['limit']}"
                )
        for resource, check in result["budget_checks"].items():
            if not check["passed"]:
                reasons.append(
                    f"topology {name} exceeds {resource} budget: "
                    f"{check['required']} > {check['budget']}"
                )
    return reasons


def build_evidence(
    *,
    config_sha256: str | None,
    profile: dict[str, int] | None,
    limits: dict[str, int],
    topology_results: list[dict[str, Any]],
    errors: list[str],
) -> dict[str, Any]:
    profile_matches = profile == MEASURED_PROFILE
    reserves = {
        resource: profile[resource] if profile is not None else None
        for resource in HARD_RESOURCES
    }
    return {
        "acceptance": {
            "errors": errors,
            "passed": not errors,
        },
        "budgets": {
            resource: {
                "limit": limits[resource],
                "rolling_replacement_reserve": reserves[resource],
            }
            for resource in HARD_RESOURCES
        },
        "capacity_contract": {
            "expected_per_process": MEASURED_PROFILE,
            "measured_replica_limits": MAX_MEASURED_REPLICAS,
            "per_process": profile,
            "profile_matches_measured_contract": profile_matches,
            "rolling_replacement_reserve_processes": 1,
        },
        "config": {"sha256": config_sha256},
        "scenario": SCENARIO,
        "schema_version": SCHEMA_VERSION,
        "topologies": topology_results,
    }


def write_evidence(output_path: Path, evidence: dict[str, Any]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(evidence, indent=2, sort_keys=True) + "\n").encode("utf-8")
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output_path.parent,
            prefix=f".{output_path.name}.",
            delete=False,
        ) as temporary:
            temporary.write(encoded)
            temporary.flush()
            os.fsync(temporary.fileno())
            temporary_path = Path(temporary.name)
        os.replace(temporary_path, output_path)
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()


def main() -> int:
    arguments = parse_arguments()
    limits = budget_limits(arguments)
    config_sha256: str | None = None
    profile: dict[str, int] | None = None

    try:
        config_sha256, profile = read_profile(arguments.config)
        topologies = parse_topologies(arguments.topology)
    except CapacityInputError as error:
        evidence = build_evidence(
            config_sha256=config_sha256,
            profile=profile,
            limits=limits,
            topology_results=[],
            errors=[str(error)],
        )
        write_evidence(arguments.output, evidence)
        print(f"capacity plan input rejected: {error}", file=sys.stderr)
        return 2

    assert profile is not None
    topology_results = [
        evaluate_topology(profile, limits, topology) for topology in topologies
    ]
    errors = rejection_reasons(profile == MEASURED_PROFILE, topology_results)
    evidence = build_evidence(
        config_sha256=config_sha256,
        profile=profile,
        limits=limits,
        topology_results=topology_results,
        errors=errors,
    )
    write_evidence(arguments.output, evidence)

    if errors:
        print(
            f"capacity plan rejected with {len(errors)} error(s); evidence: {arguments.output}",
            file=sys.stderr,
        )
        return 1
    print(
        f"capacity plan accepted for {len(topology_results)} topology step(s); "
        f"evidence: {arguments.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Contract tests for the distributed replica capacity gate."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check-distributed-capacity.py"
CONFIG = ROOT / "deploy" / "config.distributed.json"
STANDARD_BUDGETS = {
    "postgres-max-connections": 100,
    "redis-connection-budget": 36,
    "s3-http-connection-budget": 144,
    "s3-io-thread-budget": 36,
}
STANDARD_TOPOLOGIES = (
    "baseline=2:2",
    "worker-burst=2:4",
    "api-scale=4:2",
    "measured-max=4:4",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_gate(
    output: Path,
    topologies: tuple[str, ...],
    *,
    config: Path = CONFIG,
    budgets: dict[str, int] | None = None,
) -> tuple[subprocess.CompletedProcess[str], dict[str, Any]]:
    command = [
        sys.executable,
        str(SCRIPT),
        "--config",
        str(config),
    ]
    for topology in topologies:
        command.extend(("--topology", topology))
    for name, value in (budgets or STANDARD_BUDGETS).items():
        command.extend((f"--{name}", str(value)))
    command.extend(("--output", str(output)))
    result = subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    require(output.is_file(), f"capacity gate did not write evidence: {result.stderr}")
    return result, json.loads(output.read_text(encoding="utf-8"))


def expected_aggregate(api_replicas: int, worker_replicas: int) -> dict[str, int]:
    processes = api_replicas + worker_replicas
    return {
        "application_processes": processes,
        "assembly_concurrency_slots": api_replicas * 2,
        "authentication_cpu_threads": api_replicas * 4,
        "http_threads": processes * 4,
        "postgresql_connections": processes * 8,
        "redis_connections": processes * 4,
        "s3_http_connections": processes * 16,
        "s3_io_threads": processes * 4,
        "worker_concurrency_slots": worker_replicas,
    }


def test_accepted_matrix_and_deterministic_evidence(temp_root: Path) -> None:
    first_output = temp_root / "accepted-first.json"
    second_output = temp_root / "accepted-second.json"
    first, evidence = run_gate(first_output, STANDARD_TOPOLOGIES)
    second, repeated = run_gate(second_output, STANDARD_TOPOLOGIES)

    require(first.returncode == 0, first.stderr)
    require(second.returncode == 0, second.stderr)
    require(
        first_output.read_bytes() == second_output.read_bytes(),
        "evidence is not deterministic",
    )
    require(evidence == repeated, "repeated capacity evidence drifted")
    require(evidence["schema_version"] == 1, "capacity evidence schema drifted")
    require(evidence["scenario"] == "distributed-capacity-plan", "scenario drifted")
    require(evidence["acceptance"] == {"errors": [], "passed": True}, "plan rejected")
    require(
        evidence["config"]["sha256"] == hashlib.sha256(CONFIG.read_bytes()).hexdigest(),
        "config digest drifted",
    )
    require(
        evidence["capacity_contract"]["profile_matches_measured_contract"] is True,
        "distributed profile no longer matches the measured contract",
    )
    require(
        evidence["capacity_contract"]["measured_replica_limits"]
        == {"api": 4, "worker": 4},
        "measured replica boundary drifted",
    )

    expected_replicas = ((2, 2), (2, 4), (4, 2), (4, 4))
    for result, (api_replicas, worker_replicas) in zip(
        evidence["topologies"], expected_replicas, strict=True
    ):
        require(result["passed"] is True, f"topology {result['name']} was rejected")
        require(
            result["aggregate"] == expected_aggregate(api_replicas, worker_replicas),
            f"topology {result['name']} aggregate drifted",
        )
        require(
            result["required_with_rolling_reserve"]
            == {
                "postgresql_connections": (api_replicas + worker_replicas) * 8 + 8,
                "redis_connections": (api_replicas + worker_replicas) * 4 + 4,
                "s3_http_connections": (api_replicas + worker_replicas) * 16 + 16,
                "s3_io_threads": (api_replicas + worker_replicas) * 4 + 4,
            },
            f"topology {result['name']} rolling reserve drifted",
        )


def test_each_dependency_budget_is_hard(temp_root: Path) -> None:
    insufficient_values = {
        "postgres-max-connections": 71,
        "redis-connection-budget": 35,
        "s3-http-connection-budget": 143,
        "s3-io-thread-budget": 35,
    }
    resource_by_flag = {
        "postgres-max-connections": "postgresql_connections",
        "redis-connection-budget": "redis_connections",
        "s3-http-connection-budget": "s3_http_connections",
        "s3-io-thread-budget": "s3_io_threads",
    }
    for flag, insufficient_value in insufficient_values.items():
        budgets = dict(STANDARD_BUDGETS)
        budgets[flag] = insufficient_value
        result, evidence = run_gate(
            temp_root / f"rejected-{flag}.json",
            ("measured-max=4:4",),
            budgets=budgets,
        )
        resource = resource_by_flag[flag]
        check = evidence["topologies"][0]["budget_checks"][resource]
        require(result.returncode == 1, f"{flag} shortage was accepted")
        require(evidence["acceptance"]["passed"] is False, f"{flag} evidence passed")
        require(check["passed"] is False, f"{flag} did not fail its resource check")
        require(check["headroom"] == -1, f"{flag} headroom was not exact")


def test_unmeasured_and_invalid_plans_are_rejected(temp_root: Path) -> None:
    for role, topology_value in (
        ("api", "unmeasured-api=5:2"),
        ("worker", "unmeasured-worker=2:5"),
    ):
        unmeasured, evidence = run_gate(
            temp_root / f"unmeasured-{role}.json", (topology_value,)
        )
        topology = evidence["topologies"][0]
        require(unmeasured.returncode == 1, f"unmeasured {role} topology was accepted")
        require(
            topology["measured_boundary_check"][role]["passed"] is False,
            f"{role} measured boundary was not enforced",
        )
        require(
            all(check["passed"] for check in topology["budget_checks"].values()),
            "unmeasured boundary fixture unexpectedly exceeded a dependency budget",
        )

    duplicate, duplicate_evidence = run_gate(
        temp_root / "duplicate.json", ("same=2:2", "same=2:4")
    )
    require(duplicate.returncode == 2, "duplicate topology name was accepted")
    require(
        duplicate_evidence["acceptance"]["errors"] == ["duplicate topology name: same"],
        "duplicate topology error drifted",
    )

    malformed, malformed_evidence = run_gate(
        temp_root / "malformed.json", ("missing-separator",)
    )
    require(malformed.returncode == 2, "malformed topology was accepted")
    require(
        malformed_evidence["acceptance"]["passed"] is False,
        "malformed topology evidence passed",
    )


def test_profile_drift_and_sensitive_config_are_handled(temp_root: Path) -> None:
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    config["db_clients"][0]["passwd"] = "db-secret-sentinel"
    config["redis_clients"][0]["passwd"] = "redis-secret-sentinel"
    config["custom_config"]["disk"]["s3"]["endpoint"] = (
        "https://private-endpoint-sentinel.invalid"
    )
    sensitive_config = temp_root / "sensitive-config.json"
    sensitive_config.write_text(json.dumps(config), encoding="utf-8")

    accepted, evidence = run_gate(
        temp_root / "redacted.json",
        ("baseline=2:2",),
        config=sensitive_config,
    )
    rendered = json.dumps(evidence, sort_keys=True)
    require(accepted.returncode == 0, accepted.stderr)
    for sentinel in (
        "db-secret-sentinel",
        "redis-secret-sentinel",
        "private-endpoint-sentinel",
    ):
        require(sentinel not in rendered, f"capacity evidence leaked {sentinel}")

    config["db_clients"][0]["connection_number"] = 9
    drifted_config = temp_root / "drifted-config.json"
    drifted_config.write_text(json.dumps(config), encoding="utf-8")
    drifted, drifted_evidence = run_gate(
        temp_root / "drifted.json",
        ("baseline=2:2",),
        config=drifted_config,
    )
    require(drifted.returncode == 1, "unmeasured per-process profile was accepted")
    require(
        drifted_evidence["capacity_contract"]["profile_matches_measured_contract"]
        is False,
        "profile drift was not recorded",
    )
    require(
        drifted_evidence["topologies"][0]["passed"] is False,
        "profile drift did not reject the topology result",
    )

    config["db_clients"][0]["connection_number"] = 0
    invalid_config = temp_root / "invalid-config.json"
    invalid_config.write_text(json.dumps(config), encoding="utf-8")
    invalid, invalid_evidence = run_gate(
        temp_root / "invalid.json",
        ("baseline=2:2",),
        config=invalid_config,
    )
    require(invalid.returncode == 2, "non-positive per-process value was accepted")
    require(
        invalid_evidence["acceptance"]["errors"]
        == ["db_clients.default.connection_number must be a positive integer"],
        "invalid per-process value error drifted",
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="disk-capacity-budget-") as temporary:
        temp_root = Path(temporary)
        test_accepted_matrix_and_deterministic_evidence(temp_root)
        test_each_dependency_budget_is_hard(temp_root)
        test_unmeasured_and_invalid_plans_are_rejected(temp_root)
        test_profile_drift_and_sensitive_config_are_handled(temp_root)
    print("distributed capacity budget contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

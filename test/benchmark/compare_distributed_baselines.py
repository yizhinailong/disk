#!/usr/bin/env python3

"""Compare the five distributed benchmark baselines with a current run."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


SUITES = ("api", "storage", "s3", "failure", "worker")
EXPECTED_SCENARIOS = {
    "api": "dependency_free_api_horizontal_scaling",
    "storage": "storage_workload_matrix",
    "s3": "s3_streaming_assembly",
    "failure": "process_failure_under_continuous_load",
    "worker": "worker_backlog_recovery_under_online_load",
}
COMPARABLE_ENVIRONMENT_FIELDS = (
    "kernel",
    "platform",
    "cpu_model",
    "logical_cpus",
    "physical_cores",
    "memory_total_bytes",
    "build_type",
    "postgres_client",
    "redis_server",
)


class ComparisonError(RuntimeError):
    """Raised when benchmark inputs cannot support a valid comparison."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ComparisonError(message)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for suite in SUITES:
        parser.add_argument(
            f"--{suite}",
            nargs=2,
            required=True,
            metavar=("BASELINE", "CURRENT"),
            type=Path,
        )
    parser.add_argument("--material-threshold-percent", type=float, default=10.0)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def load_result(path: Path) -> dict[str, Any]:
    require(path.is_file(), f"benchmark result does not exist: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ComparisonError(
            f"cannot read benchmark result {path}: {error}"
        ) from error
    require(isinstance(value, dict), f"benchmark result is not an object: {path}")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def deep_get(value: dict[str, Any], *path: str) -> Any:
    current: Any = value
    for component in path:
        require(
            isinstance(current, dict) and component in current,
            f"missing result field: {'.'.join(path)}",
        )
        current = current[component]
    return current


def numeric(value: Any, label: str) -> float:
    require(
        isinstance(value, (int, float)) and not isinstance(value, bool),
        f"metric is not numeric: {label}",
    )
    return float(value)


def source_metadata(path: Path, result: dict[str, Any]) -> dict[str, Any]:
    environment = deep_get(result, "environment")
    git = deep_get(result, "git")
    return {
        "path": str(path),
        "sha256": sha256(path),
        "git": git,
        "server_binary_sha256": environment.get("server_binary_sha256"),
        "load_average_at_report": environment.get("load_average_at_report"),
    }


def compare_metric(
    suite: str,
    name: str,
    unit: str,
    direction: str,
    baseline: Any,
    current: Any,
    threshold_percent: float,
) -> dict[str, Any]:
    baseline_number = numeric(baseline, f"{suite}.{name}.baseline")
    current_number = numeric(current, f"{suite}.{name}.current")
    delta = current_number - baseline_number
    delta_percent = None if baseline_number == 0 else delta / baseline_number * 100
    classification = "stable"
    if delta_percent is not None:
        favorable_delta = (
            delta_percent if direction == "higher_better" else -delta_percent
        )
        if favorable_delta >= threshold_percent:
            classification = "material_improvement"
        elif favorable_delta <= -threshold_percent:
            classification = "material_regression"
    return {
        "suite": suite,
        "metric": name,
        "unit": unit,
        "direction": direction,
        "baseline": round(baseline_number, 6),
        "current": round(current_number, 6),
        "delta": round(delta, 6),
        "delta_percent": round(delta_percent, 6) if delta_percent is not None else None,
        "classification": classification,
    }


def api_metrics(
    baseline: dict[str, Any],
    current: dict[str, Any],
) -> list[tuple[str, str, str, Any, Any]]:
    metrics: list[tuple[str, str, str, Any, Any]] = []
    for replicas in ("1", "2", "4"):
        baseline_summary = deep_get(baseline, "summary", replicas)
        current_summary = deep_get(current, "summary", replicas)
        metrics.extend(
            (
                (
                    f"replicas_{replicas}_median_success_rps",
                    "requests_per_second",
                    "higher_better",
                    deep_get(baseline_summary, "median_success_rps"),
                    deep_get(current_summary, "median_success_rps"),
                ),
                (
                    f"replicas_{replicas}_median_average_request_ms",
                    "milliseconds",
                    "lower_better",
                    deep_get(baseline_summary, "median_average_request_ms"),
                    deep_get(current_summary, "median_average_request_ms"),
                ),
            )
        )
    metrics.append(
        (
            "gain_1_to_2_percent",
            "percent",
            "higher_better",
            deep_get(baseline, "acceptance", "observed_gain_1_to_2_percent"),
            deep_get(current, "acceptance", "observed_gain_1_to_2_percent"),
        )
    )
    return metrics


def scenario_by_name(result: dict[str, Any], name: str) -> dict[str, Any]:
    scenarios = deep_get(result, "scenarios")
    require(isinstance(scenarios, list), "storage scenarios is not an array")
    matches = [
        item
        for item in scenarios
        if isinstance(item, dict) and item.get("name") == name
    ]
    require(len(matches) == 1, f"expected one storage scenario named {name}")
    return matches[0]


def storage_metrics(
    baseline: dict[str, Any],
    current: dict[str, Any],
) -> list[tuple[str, str, str, Any, Any]]:
    definitions = (
        (
            "small_files",
            "files_per_second",
            "files_per_second",
            ("latency", "logical_upload", "p95_ms"),
        ),
        (
            "concurrent_chunks",
            "chunk_payload_mib_per_second",
            "mib_per_second",
            ("chunk_request_latency", "p95_ms"),
        ),
        (
            "large_complete",
            "assembly_mib_per_second",
            "mib_per_second",
            ("complete_latency", "p95_ms"),
        ),
        (
            "range_download",
            "ranges_per_second",
            "ranges_per_second",
            ("latency", "p95_ms"),
        ),
        (
            "mixed_read_write",
            "logical_operations_per_second",
            "operations_per_second",
            ("latency", "read", "p95_ms"),
        ),
    )
    metrics: list[tuple[str, str, str, Any, Any]] = []
    for scenario_name, throughput_field, unit, latency_path in definitions:
        baseline_scenario = scenario_by_name(baseline, scenario_name)
        current_scenario = scenario_by_name(current, scenario_name)
        metrics.extend(
            (
                (
                    f"{scenario_name}_throughput",
                    unit,
                    "higher_better",
                    deep_get(baseline_scenario, throughput_field),
                    deep_get(current_scenario, throughput_field),
                ),
                (
                    f"{scenario_name}_primary_p95_ms",
                    "milliseconds",
                    "lower_better",
                    deep_get(baseline_scenario, *latency_path),
                    deep_get(current_scenario, *latency_path),
                ),
            )
        )
    baseline_mixed = scenario_by_name(baseline, "mixed_read_write")
    current_mixed = scenario_by_name(current, "mixed_read_write")
    metrics.append(
        (
            "mixed_read_write_write_p95_ms",
            "milliseconds",
            "lower_better",
            deep_get(baseline_mixed, "latency", "write_logical_upload", "p95_ms"),
            deep_get(current_mixed, "latency", "write_logical_upload", "p95_ms"),
        )
    )
    return metrics


def s3_metrics(
    baseline: dict[str, Any],
    current: dict[str, Any],
) -> list[tuple[str, str, str, Any, Any]]:
    definitions = (
        (
            "assembly_complete_mean_ms",
            "milliseconds",
            "lower_better",
            ("assembly", "complete_latency_mean_ms"),
        ),
        (
            "assembly_mib_per_second",
            "mib_per_second",
            "higher_better",
            ("assembly", "assemble_mib_per_second"),
        ),
        (
            "promote_mib_per_second",
            "mib_per_second",
            "higher_better",
            ("assembly", "promote_mib_per_second"),
        ),
        (
            "end_to_end_mib_per_second",
            "mib_per_second",
            "higher_better",
            ("assembly", "end_to_end_mib_per_second"),
        ),
        (
            "cleanup_jobs_per_second",
            "jobs_per_second",
            "higher_better",
            ("cleanup", "jobs_per_second"),
        ),
        (
            "cleanup_removed_mib_per_second",
            "mib_per_second",
            "higher_better",
            ("cleanup", "removed_mib_per_second"),
        ),
    )
    return [
        (name, unit, direction, deep_get(baseline, *path), deep_get(current, *path))
        for name, unit, direction, path in definitions
    ]


def failure_metrics(
    baseline: dict[str, Any],
    current: dict[str, Any],
) -> list[tuple[str, str, str, Any, Any]]:
    definitions = (
        (
            "logical_failure_rate_percent",
            "percent",
            ("continuous_load", "logical_failure_rate_percent"),
        ),
        (
            "first_attempt_failure_rate_percent",
            "percent",
            ("continuous_load", "first_attempt_failure_rate_percent"),
        ),
        (
            "continuous_load_p95_ms",
            "milliseconds",
            ("continuous_load", "latency", "p95_ms"),
        ),
        (
            "maximum_success_gap_seconds",
            "seconds",
            ("continuous_load", "max_success_completion_gap_seconds"),
        ),
        (
            "first_retry_recovery_seconds",
            "seconds",
            ("continuous_load", "first_retry_recovery_after_api_kill_seconds"),
        ),
        ("api_removal_seconds", "seconds", ("api_failure", "removal_seconds")),
        ("api_takeover_seconds", "seconds", ("api_failure", "takeover_rto_seconds")),
        (
            "api_restart_readiness_seconds",
            "seconds",
            ("api_failure", "restart_readiness_seconds"),
        ),
        (
            "api_router_rejoin_seconds",
            "seconds",
            ("api_failure", "router_rejoin_seconds"),
        ),
        (
            "worker_load_completion_seconds",
            "seconds",
            ("worker_failure", "load_jobs_completion_seconds"),
        ),
        (
            "worker_takeover_seconds",
            "seconds",
            ("worker_failure", "takeover_rto_seconds"),
        ),
    )
    return [
        (
            name,
            unit,
            "lower_better",
            deep_get(baseline, *path),
            deep_get(current, *path),
        )
        for name, unit, path in definitions
    ]


def worker_metrics(
    baseline: dict[str, Any],
    current: dict[str, Any],
) -> list[tuple[str, str, str, Any, Any]]:
    metrics: list[tuple[str, str, str, Any, Any]] = []
    definitions = (
        ("jobs_per_second", "jobs_per_second", "higher_better"),
        ("drain_seconds", "seconds", "lower_better"),
        ("steady_p95_ms", "milliseconds", "lower_better"),
        ("drain_p95_ms", "milliseconds", "lower_better"),
        ("recovered_p95_ms", "milliseconds", "lower_better"),
        ("scaling_efficiency", "ratio", "higher_better"),
    )
    fields = {
        "jobs_per_second": "jobs_per_second_median",
        "drain_seconds": "drain_seconds_median",
        "steady_p95_ms": "steady_p95_ms_median",
        "drain_p95_ms": "drain_p95_ms_median",
        "recovered_p95_ms": "recovered_p95_ms_median",
        "scaling_efficiency": "scaling_efficiency",
    }
    for replicas in ("1", "2", "4"):
        baseline_summary = deep_get(baseline, "summary", replicas)
        current_summary = deep_get(current, "summary", replicas)
        for name, unit, direction in definitions:
            field = fields[name]
            metrics.append(
                (
                    f"replicas_{replicas}_{name}",
                    unit,
                    direction,
                    deep_get(baseline_summary, field),
                    deep_get(current_summary, field),
                )
            )
    return metrics


METRIC_BUILDERS = {
    "api": api_metrics,
    "storage": storage_metrics,
    "s3": s3_metrics,
    "failure": failure_metrics,
    "worker": worker_metrics,
}


def validate_pair(
    suite: str,
    baseline: dict[str, Any],
    current: dict[str, Any],
) -> None:
    expected_scenario = EXPECTED_SCENARIOS[suite]
    for label, result in (("baseline", baseline), ("current", current)):
        require(
            result.get("schema_version") == 1,
            f"{suite} {label} schema_version is not 1",
        )
        require(
            result.get("scenario") == expected_scenario,
            f"{suite} {label} scenario mismatch",
        )
        require(
            deep_get(result, "acceptance", "passed") is True,
            f"{suite} {label} acceptance failed",
        )
        require(
            deep_get(result, "git", "dirty") is False,
            f"{suite} {label} was measured from a dirty tree",
        )
        commit = deep_get(result, "git", "commit")
        require(
            isinstance(commit, str) and commit, f"{suite} {label} Git commit is missing"
        )
        binary_hash = deep_get(result, "environment", "server_binary_sha256")
        require(
            isinstance(binary_hash, str) and len(binary_hash) == 64,
            f"{suite} {label} server binary SHA-256 is missing",
        )
    baseline_parameters = deep_get(baseline, "parameters")
    current_parameters = deep_get(current, "parameters")
    require(
        isinstance(baseline_parameters, dict),
        f"{suite} baseline parameters is not an object",
    )
    require(
        isinstance(current_parameters, dict),
        f"{suite} current parameters is not an object",
    )
    require(baseline_parameters == current_parameters, f"{suite} parameters differ")
    baseline_environment = deep_get(baseline, "environment")
    current_environment = deep_get(current, "environment")
    require(
        isinstance(baseline_environment, dict),
        f"{suite} baseline environment is not an object",
    )
    require(
        isinstance(current_environment, dict),
        f"{suite} current environment is not an object",
    )
    for field in COMPARABLE_ENVIRONMENT_FIELDS:
        require(
            field in baseline_environment,
            f"{suite} baseline environment is missing {field}",
        )
        require(
            field in current_environment,
            f"{suite} current environment is missing {field}",
        )
        require(
            baseline_environment.get(field) == current_environment.get(field),
            f"{suite} environment differs at {field}",
        )
    if suite == "s3":
        for field in ("implementation", "version"):
            require(
                deep_get(baseline, "endpoint", field)
                == deep_get(current, "endpoint", field),
                f"s3 endpoint differs at {field}",
            )


def main() -> int:
    args = parse_args()
    try:
        threshold = float(args.material_threshold_percent)
        require(
            math.isfinite(threshold) and threshold > 0,
            "material threshold must be finite and positive",
        )
        pairs: dict[str, tuple[Path, Path, dict[str, Any], dict[str, Any]]] = {}
        current_commits: set[str] = set()
        current_binary_hashes: set[str] = set()
        sources: dict[str, Any] = {}
        comparisons: list[dict[str, Any]] = []

        for suite in SUITES:
            baseline_path, current_path = getattr(args, suite)
            baseline_path = baseline_path.resolve()
            current_path = current_path.resolve()
            baseline = load_result(baseline_path)
            current = load_result(current_path)
            validate_pair(suite, baseline, current)
            current_commits.add(str(deep_get(current, "git", "commit")))
            current_binary_hashes.add(
                str(deep_get(current, "environment", "server_binary_sha256"))
            )
            pairs[suite] = (baseline_path, current_path, baseline, current)
            sources[suite] = {
                "baseline": source_metadata(baseline_path, baseline),
                "current": source_metadata(current_path, current),
            }

        require(
            len(current_commits) == 1,
            "current benchmark results use different Git commits",
        )
        require(
            len(current_binary_hashes) == 1,
            "current benchmark results use different server binaries",
        )
        current_commit = next(iter(current_commits))
        current_binary_hash = next(iter(current_binary_hashes))
        for suite, (_, _, baseline, current) in pairs.items():
            for name, unit, direction, baseline_value, current_value in METRIC_BUILDERS[
                suite
            ](baseline, current):
                comparisons.append(
                    compare_metric(
                        suite,
                        name,
                        unit,
                        direction,
                        baseline_value,
                        current_value,
                        threshold,
                    )
                )

        material_regressions = [
            item
            for item in comparisons
            if item["classification"] == "material_regression"
        ]
        material_improvements = [
            item
            for item in comparisons
            if item["classification"] == "material_improvement"
        ]
        report = {
            "schema_version": 1,
            "scenario": "distributed_post_refactor_baseline_comparison",
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "current_commit": current_commit,
            "current_server_binary_sha256": current_binary_hash,
            "material_threshold_percent": threshold,
            "comparability": {
                "passed": True,
                "environment_fields": list(COMPARABLE_ENVIRONMENT_FIELDS),
                "parameters_equal": True,
                "s3_implementation_and_version_equal": True,
                "all_sources_clean": True,
                "all_current_sources_same_commit": True,
                "all_current_sources_same_binary": True,
                "all_source_acceptance_passed": True,
            },
            "sources": sources,
            "comparisons": comparisons,
            "material_regressions": material_regressions,
            "material_improvements": material_improvements,
            "acceptance": {
                "source_suite_count": len(SUITES),
                "comparison_count": len(comparisons),
                "material_regression_count": len(material_regressions),
                "material_improvement_count": len(material_improvements),
                "passed": True,
                "interpretation": (
                    "Material observations require controlled repetition; they do not override "
                    "the source benchmark acceptance and are not standalone production SLO results."
                ),
            },
        }
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(
            json.dumps(
                {
                    "scenario": report["scenario"],
                    "output": str(output),
                    "current_commit": current_commit,
                    "comparison_count": len(comparisons),
                    "material_regression_count": len(material_regressions),
                    "material_improvement_count": len(material_improvements),
                    "passed": True,
                },
                indent=2,
            )
        )
        return 0
    except ComparisonError as error:
        print(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())

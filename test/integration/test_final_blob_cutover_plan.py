#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Contract tests for the bounded final Blob maintenance-window policy."""

from __future__ import annotations

import hashlib
import json
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check-final-blob-cutover-plan.py"
PLAN = ROOT / "deploy" / "final-blob-maintenance-window.json"
EXPECTED_GATES = [
    "close_ingress",
    "stop_all_api",
    "stop_all_workers",
    "stop_scheduled_maintenance_and_blob_gc",
    "verify_no_active_completion_transaction",
    "backup_postgresql",
    "backup_local_blobs",
    "generate_and_review_manifest",
    "copy_dry_run",
    "copy_execute_and_verify_checkpoint",
    "cutover_dry_run",
    "cutover_execute_atomic",
    "configure_all_processes_for_s3",
    "start_all_s3_api_behind_closed_ingress",
    "download_and_range_probe",
    "full_reconciliation",
    "start_all_s3_workers",
    "open_ingress",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_gate(
    plan: Path, output: Path
) -> tuple[subprocess.CompletedProcess[str], dict[str, Any]]:
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--plan",
            str(plan),
            "--output",
            str(output),
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    require(output.is_file(), f"cutover gate did not write evidence: {result.stderr}")
    return result, json.loads(output.read_text(encoding="utf-8"))


def write_plan(path: Path, plan: object) -> None:
    path.write_text(json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def rejected_mutation(
    temp_root: Path,
    name: str,
    mutate: Callable[[dict[str, Any]], None],
) -> dict[str, Any]:
    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    mutate(plan)
    plan_path = temp_root / f"{name}.json"
    output_path = temp_root / f"{name}-evidence.json"
    write_plan(plan_path, plan)
    result, evidence = run_gate(plan_path, output_path)
    require(result.returncode == 1, f"invalid {name} policy was accepted")
    require(evidence["acceptance"]["passed"] is False, f"{name} evidence passed")
    return evidence


def test_reviewed_policy_and_deterministic_evidence(temp_root: Path) -> None:
    first_output = temp_root / "accepted-first.json"
    second_output = temp_root / "accepted-second.json"
    first, evidence = run_gate(PLAN, first_output)
    second, repeated = run_gate(PLAN, second_output)

    require(first.returncode == 0, first.stderr)
    require(second.returncode == 0, second.stderr)
    require(first_output.read_bytes() == second_output.read_bytes(), "evidence drifted")
    require(evidence == repeated, "repeated evidence changed")
    require(stat.S_IMODE(first_output.stat().st_mode) == 0o600, "evidence mode drifted")
    require(evidence["schema_version"] == 1, "evidence schema drifted")
    require(
        evidence["scenario"] == "bounded-final-blob-maintenance-window",
        "evidence scenario drifted",
    )
    require(evidence["acceptance"] == {"errors": [], "passed": True}, "policy rejected")
    require(all(evidence["checks"].values()), "an accepted policy check did not pass")
    require(
        evidence["plan"]["sha256"] == hashlib.sha256(PLAN.read_bytes()).hexdigest(),
        "policy digest drifted",
    )

    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    require(plan["strategy"] == "maintenance_window", "strategy drifted")
    require(plan["source_backend"] == "local", "source backend drifted")
    require(plan["target_backend"] == "s3", "target backend drifted")
    require(
        plan["window"]
        == {
            "extension_allowed": False,
            "maximum_duration_minutes": 120,
            "target_change_requires_utc_bounds": True,
        },
        "bounded window policy drifted",
    )
    require(plan["ordered_gates"] == EXPECTED_GATES, "gate order drifted")
    require(
        plan["owners"]["required_roles"]
        == ["migration_owner", "database_owner", "rollback_owner"],
        "required owners drifted",
    )
    require(
        plan["forbidden_modes"]
        == [
            "online_partial_database_cutover",
            "online_dual_write",
            "unbounded_dual_read",
        ],
        "forbidden mode set drifted",
    )


def test_unbounded_or_online_strategies_are_rejected(temp_root: Path) -> None:
    evidence = rejected_mutation(
        temp_root,
        "per-content-strategy",
        lambda plan: plan.__setitem__("strategy", "per_content_backend"),
    )
    require(
        "migration strategy must remain maintenance_window"
        in evidence["acceptance"]["errors"],
        "strategy rejection reason drifted",
    )
    rejected_mutation(
        temp_root,
        "window-extension",
        lambda plan: plan["window"].__setitem__("extension_allowed", True),
    )
    rejected_mutation(
        temp_root,
        "unbounded-window",
        lambda plan: plan["window"].__setitem__("maximum_duration_minutes", 0),
    )
    rejected_mutation(
        temp_root,
        "dual-write-not-forbidden",
        lambda plan: plan["forbidden_modes"].remove("online_dual_write"),
    )


def test_gate_order_owners_and_stop_actions_are_hard(temp_root: Path) -> None:
    def swap_copy_and_backup(plan: dict[str, Any]) -> None:
        gates = plan["ordered_gates"]
        backup = gates.index("backup_local_blobs")
        copy = gates.index("copy_dry_run")
        gates[backup], gates[copy] = gates[copy], gates[backup]

    rejected_mutation(temp_root, "copy-before-backup", swap_copy_and_backup)
    rejected_mutation(
        temp_root,
        "missing-freeze-gate",
        lambda plan: plan["ordered_gates"].remove(
            "verify_no_active_completion_transaction"
        ),
    )
    rejected_mutation(
        temp_root,
        "missing-rollback-owner",
        lambda plan: plan["owners"]["required_roles"].remove("rollback_owner"),
    )
    rejected_mutation(
        temp_root,
        "missing-window-expiry-stop",
        lambda plan: plan["stop_conditions"].remove("approved_window_expired"),
    )
    rejected_mutation(
        temp_root,
        "continue-after-cutover-expiry",
        lambda plan: plan["expiry_actions"].__setitem__(
            "after_cutover_before_traffic", "continue_and_open_ingress"
        ),
    )
    rejected_mutation(
        temp_root,
        "discard-local-sources",
        lambda plan: plan["expiry_actions"].__setitem__(
            "retain_local_sources_until_reconciliation", False
        ),
    )


def test_malformed_and_sensitive_inputs_are_redacted(temp_root: Path) -> None:
    sentinel = "https://private-endpoint-sentinel.invalid"
    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    plan["s3_endpoint"] = sentinel
    sensitive_plan = temp_root / "sensitive.json"
    write_plan(sensitive_plan, plan)
    sensitive, evidence = run_gate(
        sensitive_plan, temp_root / "sensitive-evidence.json"
    )
    rendered = json.dumps(evidence, sort_keys=True)
    require(sensitive.returncode == 1, "sensitive extra field was accepted")
    require(sentinel not in rendered, "evidence leaked an endpoint value")

    malformed_plan = temp_root / "malformed.json"
    malformed_plan.write_text("{not-json", encoding="utf-8")
    malformed, malformed_evidence = run_gate(
        malformed_plan, temp_root / "malformed-evidence.json"
    )
    require(malformed.returncode == 2, "malformed JSON was accepted")
    require(
        malformed_evidence["acceptance"]["errors"] == ["plan is not valid UTF-8 JSON"],
        "malformed input error drifted",
    )

    array_plan = temp_root / "array.json"
    write_plan(array_plan, ["maintenance_window"])
    array_result, array_evidence = run_gate(
        array_plan, temp_root / "array-evidence.json"
    )
    require(array_result.returncode == 2, "non-object plan was accepted")
    require(
        array_evidence["acceptance"]["errors"] == ["plan must be a JSON object"],
        "non-object input error drifted",
    )


def main() -> int:
    with tempfile.TemporaryDirectory(
        prefix="disk-final-blob-cutover-plan-"
    ) as temporary:
        temp_root = Path(temporary)
        test_reviewed_policy_and_deterministic_evidence(temp_root)
        test_unbounded_or_online_strategies_are_rejected(temp_root)
        test_gate_order_owners_and_stop_actions_are_hard(temp_root)
        test_malformed_and_sensitive_inputs_are_redacted(temp_root)
    print("bounded final Blob maintenance-window contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

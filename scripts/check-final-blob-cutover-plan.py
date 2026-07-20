#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Validate the reviewed bounded final Blob cutover policy."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
SCENARIO = "bounded-final-blob-maintenance-window"
MAX_PLAN_BYTES = 64 * 1024
TOP_LEVEL_FIELDS = {
    "expiry_actions",
    "forbidden_modes",
    "ordered_gates",
    "owners",
    "schema_version",
    "source_backend",
    "stop_conditions",
    "strategy",
    "target_backend",
    "window",
}
WINDOW_FIELDS = {
    "extension_allowed",
    "maximum_duration_minutes",
    "target_change_requires_utc_bounds",
}
OWNER_FIELDS = {"required_roles"}
EXPIRY_ACTION_FIELDS = {
    "after_cutover_before_traffic",
    "before_cutover",
    "retain_local_sources_until_reconciliation",
}
REQUIRED_OWNER_ROLES = [
    "migration_owner",
    "database_owner",
    "rollback_owner",
]
ORDERED_GATES = [
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
STOP_CONDITIONS = [
    "approved_window_expired",
    "writer_freeze_unverified",
    "backup_incomplete",
    "manifest_invalid_or_changed",
    "copy_or_checkpoint_verification_failed",
    "database_lock_unavailable",
    "database_snapshot_drift",
    "process_storage_configuration_mismatch",
    "download_or_range_probe_failed",
    "full_reconciliation_failed",
]
FORBIDDEN_MODES = [
    "online_partial_database_cutover",
    "online_dual_write",
    "unbounded_dual_read",
]


class PlanInputError(ValueError):
    """Raised when a plan cannot be parsed as a bounded policy."""


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check the final Blob cutover policy before change approval."
    )
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def same_value(actual: object, expected: object) -> bool:
    return type(actual) is type(expected) and actual == expected


def require_exact_fields(
    value: dict[str, Any], label: str, expected: set[str], errors: list[str]
) -> None:
    actual = set(value)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing:
        errors.append(f"{label} is missing field(s): {', '.join(missing)}")
    if unexpected:
        errors.append(f"{label} has unexpected field(s): {', '.join(unexpected)}")


def get_mapping(
    value: object, label: str, expected: set[str], errors: list[str]
) -> dict[str, Any]:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return {}
    require_exact_fields(value, label, expected, errors)
    return value


def read_plan(plan_path: Path) -> tuple[str, dict[str, Any]]:
    try:
        raw_plan = plan_path.read_bytes()
    except OSError as error:
        raise PlanInputError(f"cannot read plan: {error.strerror}") from error
    if len(raw_plan) > MAX_PLAN_BYTES:
        raise PlanInputError("plan exceeds the 65536-byte input limit")

    plan_sha256 = hashlib.sha256(raw_plan).hexdigest()
    try:
        parsed = json.loads(raw_plan)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PlanInputError("plan is not valid UTF-8 JSON") from error
    if not isinstance(parsed, dict):
        raise PlanInputError("plan must be a JSON object")
    return plan_sha256, parsed


def evaluate_plan(plan: dict[str, Any]) -> tuple[dict[str, bool], list[str]]:
    errors: list[str] = []
    require_exact_fields(plan, "plan", TOP_LEVEL_FIELDS, errors)
    window = get_mapping(plan.get("window"), "window", WINDOW_FIELDS, errors)
    owners = get_mapping(plan.get("owners"), "owners", OWNER_FIELDS, errors)
    expiry_actions = get_mapping(
        plan.get("expiry_actions"),
        "expiry_actions",
        EXPIRY_ACTION_FIELDS,
        errors,
    )

    checks = {
        "backends_are_local_to_s3": same_value(plan.get("source_backend"), "local")
        and same_value(plan.get("target_backend"), "s3"),
        "bounded_window_is_120_minutes": same_value(
            window.get("maximum_duration_minutes"), 120
        ),
        "extension_is_forbidden": same_value(window.get("extension_allowed"), False),
        "forbidden_modes_are_explicit": same_value(
            plan.get("forbidden_modes"), FORBIDDEN_MODES
        ),
        "ordered_gates_match_reviewed_sequence": same_value(
            plan.get("ordered_gates"), ORDERED_GATES
        ),
        "owner_roles_are_required": same_value(
            owners.get("required_roles"), REQUIRED_OWNER_ROLES
        ),
        "post_cutover_expiry_rolls_back_before_traffic": same_value(
            expiry_actions.get("after_cutover_before_traffic"),
            "rollback_database_before_open_ingress",
        ),
        "pre_cutover_expiry_requires_new_window": same_value(
            expiry_actions.get("before_cutover"),
            "stop_and_resume_in_new_approved_window",
        ),
        "local_sources_are_retained": same_value(
            expiry_actions.get("retain_local_sources_until_reconciliation"), True
        ),
        "schema_version_is_supported": same_value(
            plan.get("schema_version"), SCHEMA_VERSION
        ),
        "stop_conditions_match_reviewed_policy": same_value(
            plan.get("stop_conditions"), STOP_CONDITIONS
        ),
        "strategy_is_maintenance_window": same_value(
            plan.get("strategy"), "maintenance_window"
        ),
        "target_change_requires_utc_bounds": same_value(
            window.get("target_change_requires_utc_bounds"), True
        ),
    }
    rejection_messages = {
        "backends_are_local_to_s3": "source and target backends must be local and s3",
        "bounded_window_is_120_minutes": "maximum maintenance duration must remain 120 minutes",
        "extension_is_forbidden": "maintenance-window extension must remain forbidden",
        "forbidden_modes_are_explicit": "forbidden migration modes differ from the reviewed policy",
        "ordered_gates_match_reviewed_sequence": "ordered migration gates differ from the reviewed sequence",
        "owner_roles_are_required": "required owner roles differ from the reviewed policy",
        "post_cutover_expiry_rolls_back_before_traffic": "post-cutover expiry must roll back before ingress opens",
        "pre_cutover_expiry_requires_new_window": "pre-cutover expiry must require a newly approved window",
        "local_sources_are_retained": "local sources must be retained through reconciliation",
        "schema_version_is_supported": "plan schema version is unsupported",
        "stop_conditions_match_reviewed_policy": "stop conditions differ from the reviewed policy",
        "strategy_is_maintenance_window": "migration strategy must remain maintenance_window",
        "target_change_requires_utc_bounds": "target change record must require UTC window bounds",
    }
    errors.extend(
        rejection_messages[name] for name, passed in checks.items() if not passed
    )
    return checks, errors


def build_evidence(
    *, plan_sha256: str | None, checks: dict[str, bool], errors: list[str]
) -> dict[str, Any]:
    return {
        "acceptance": {"errors": errors, "passed": not errors},
        "checks": checks,
        "plan": {"sha256": plan_sha256},
        "reviewed_contract": {
            "forbidden_modes": FORBIDDEN_MODES,
            "maximum_duration_minutes": 120,
            "ordered_gates": ORDERED_GATES,
            "required_owner_roles": REQUIRED_OWNER_ROLES,
            "stop_conditions": STOP_CONDITIONS,
            "strategy": "maintenance_window",
        },
        "scenario": SCENARIO,
        "schema_version": SCHEMA_VERSION,
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
            os.fchmod(temporary.fileno(), 0o600)
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
    plan_sha256: str | None = None
    try:
        plan_sha256, plan = read_plan(arguments.plan)
    except PlanInputError as error:
        evidence = build_evidence(plan_sha256=None, checks={}, errors=[str(error)])
        write_evidence(arguments.output, evidence)
        print(f"final Blob cutover plan input rejected: {error}", file=sys.stderr)
        return 2

    checks, errors = evaluate_plan(plan)
    evidence = build_evidence(
        plan_sha256=plan_sha256,
        checks=checks,
        errors=errors,
    )
    write_evidence(arguments.output, evidence)
    if errors:
        print(
            f"final Blob cutover plan rejected with {len(errors)} error(s); "
            f"evidence: {arguments.output}",
            file=sys.stderr,
        )
        return 1
    print(f"final Blob cutover plan accepted; evidence: {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

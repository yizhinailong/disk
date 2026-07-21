#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Validate and execute reviewed S3 staging rollout transitions."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
SCENARIO = "s3-staging-progressive-rollout-plan"
MAX_PLAN_BYTES = 128 * 1024
MAX_SQL_BYTES = 64 * 1024
REPO_ROOT = Path(__file__).resolve().parents[1]
REVIEWED_VALIDATION_SQL = "deploy/staging-rollout-validation.sql"
TOP_LEVEL_FIELDS = {
    "change_id_environment",
    "execute_approval_environment",
    "owner_environment",
    "rollout",
    "rollout_adapter_environment",
    "safety",
    "schema_version",
    "stage_order_percent",
    "stages",
    "stop_conditions",
    "traffic_scope",
    "validation_query_ids",
    "validation_sql",
}
OWNER_ENVIRONMENT = {
    "release_owner": "DISK_RELEASE_OWNER",
    "rollback_owner": "DISK_ROLLBACK_OWNER",
    "database_verifier": "DISK_DATABASE_VERIFIER",
    "storage_verifier": "DISK_STORAGE_VERIFIER",
}
OWNER_ROLES = list(OWNER_ENVIRONMENT)
SAFETY = {
    "rollback_target": "immediately_previous_passing_stage",
    "preserve_persisted_descriptors": True,
    "compatible_handlers_required": True,
    "database_mutation_allowed": False,
    "object_deletion_allowed": False,
    "adapter_requires_expected_current_percent": True,
    "transition_scope_limited_to_new_upload_initialization": True,
    "target_change_requires_named_owners": True,
}
STOP_CONDITIONS = [
    {
        "id": "controlled_upload_failed",
        "predicate": "controlled_non_instant_upload_failures > 0",
    },
    {
        "id": "http_5xx_rate_exceeded",
        "predicate": "upload_http_sample_count >= 20 AND upload_http_5xx_ratio > 0.01",
    },
    {
        "id": "complete_p99_exceeded",
        "predicate": "upload_complete_p99_seconds > 1.0 for 10 consecutive minutes",
    },
    {
        "id": "s3_transient_error_burst",
        "predicate": "s3_transient_errors_5m >= 5",
    },
    {
        "id": "s3_permanent_error",
        "predicate": "s3_permanent_errors_5m > 0",
    },
    {
        "id": "expired_lease_detected",
        "predicate": "expired_upload_or_storage_job_leases > 0",
    },
    {
        "id": "lease_takeover_detected",
        "predicate": "lease_takeovers_10m > 0",
    },
    {
        "id": "dead_letter_detected",
        "predicate": "storage_dead_letter_jobs > 0",
    },
    {
        "id": "metrics_snapshot_failed",
        "predicate": "metrics_snapshot_success < 1",
    },
    {
        "id": "unresolved_finding_detected",
        "predicate": "unresolved_reconciliation_findings > 0",
    },
    {
        "id": "persisted_descriptor_drift",
        "predicate": "pre_stage_descriptor_sha256_changed",
    },
    {
        "id": "controlled_backend_ratio_mismatch",
        "predicate": "controlled_stage_s3_task_ratio != target_percent",
    },
    {
        "id": "business_invariant_mismatch",
        "predicate": "quota_file_content_or_ref_count_mismatch > 0",
    },
    {
        "id": "compatible_handler_unavailable",
        "predicate": "compatible_handler_or_required_local_volume_unavailable",
    },
]
STOP_CONDITION_IDS = [condition["id"] for condition in STOP_CONDITIONS]
VALIDATION_QUERY_IDS = [
    "preexisting_descriptors",
    "stage_uploads",
    "stage_backend_counts",
    "stage_cleanup_jobs",
    "cluster_blockers",
    "stage_quota_and_content_references",
    "unresolved_findings",
]
STAGE_SPECS = [
    (10, 0, 30, 20),
    (25, 10, 30, 20),
    (50, 25, 60, 20),
    (100, 50, 120, 20),
]
STAGE_ORDER = [stage[0] for stage in STAGE_SPECS]
VALIDATION_COMMAND = (
    'psql "$DISK_DATABASE_URL" --csv -X -v ON_ERROR_STOP=1 '
    '-v stage_started_at="$STAGE_STARTED_AT" '
    '-v stage_ended_at="$STAGE_ENDED_AT" '
    "-f deploy/staging-rollout-validation.sql"
)
FORBIDDEN_SQL = re.compile(
    r"\b(ALTER|CALL|COPY|CREATE|DELETE|DO|DROP|GRANT|INSERT|LOCK|MERGE|"
    r"REVOKE|TRUNCATE|UPDATE|VACUUM)\b",
    re.IGNORECASE,
)
CHANGE_ID_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{2,127}")
SENSITIVE_ADAPTER_ENV_PREFIXES = (
    "AWS_",
    "DATABASE_",
    "DISK_DATABASE_",
    "DISK_REDIS_",
    "DISK_S3_",
    "JWT_",
    "MINIO_",
    "PG",
    "POSTGRES_",
    "REDIS_",
)


class PlanInputError(ValueError):
    """Raised when a rollout plan cannot be interpreted safely."""


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate or run an adjacent S3 staging rollout transition."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    check_parser = subparsers.add_parser(
        "check", help="Validate the reviewed rollout plan and write evidence."
    )
    check_parser.add_argument("--plan", required=True, type=Path)
    check_parser.add_argument("--output", required=True, type=Path)

    transition_parser = subparsers.add_parser(
        "transition", help="Preview or execute one adjacent rollout transition."
    )
    transition_parser.add_argument("--plan", required=True, type=Path)
    transition_parser.add_argument(
        "--from", dest="from_percent", required=True, type=int
    )
    transition_parser.add_argument("--to", dest="to_percent", required=True, type=int)
    transition_parser.add_argument("--execute", action="store_true")
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


def read_plan(plan_path: Path) -> tuple[str, dict[str, Any]]:
    try:
        raw_plan = plan_path.read_bytes()
    except OSError as error:
        raise PlanInputError(f"cannot read plan: {error.strerror}") from error
    if len(raw_plan) > MAX_PLAN_BYTES:
        raise PlanInputError("plan exceeds the 131072-byte input limit")

    plan_sha256 = hashlib.sha256(raw_plan).hexdigest()
    try:
        parsed = json.loads(raw_plan)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PlanInputError("plan is not valid UTF-8 JSON") from error
    if not isinstance(parsed, dict):
        raise PlanInputError("plan must be a JSON object")
    return plan_sha256, parsed


def expected_stage(
    percent: int,
    previous_percent: int,
    observation_minutes: int,
    minimum_tasks: int,
) -> dict[str, Any]:
    command_prefix = (
        "uv run scripts/staging-rollout.py transition "
        "--plan deploy/staging-rollout-plan.json"
    )
    return {
        "percent": percent,
        "previous_percent": previous_percent,
        "minimum_observation_minutes": observation_minutes,
        "minimum_non_instant_tasks": minimum_tasks,
        "owner_roles": OWNER_ROLES,
        "preview_command": (
            f"{command_prefix} --from {previous_percent} --to {percent}"
        ),
        "apply_command": (
            "DISK_STAGING_ROLLOUT_APPROVED=true "
            f"{command_prefix} --from {previous_percent} --to {percent} --execute"
        ),
        "rollback_command": (
            "DISK_STAGING_ROLLOUT_APPROVED=true "
            f"{command_prefix} --from {percent} --to {previous_percent} --execute"
        ),
        "validation_command": VALIDATION_COMMAND,
        "stop_condition_ids": STOP_CONDITION_IDS,
        "validation_query_ids": VALIDATION_QUERY_IDS,
    }


def expected_stages() -> list[dict[str, Any]]:
    return [expected_stage(*stage) for stage in STAGE_SPECS]


def inspect_validation_sql() -> tuple[str | None, dict[str, bool], list[str]]:
    sql_path = REPO_ROOT / REVIEWED_VALIDATION_SQL
    errors: list[str] = []
    try:
        raw_sql = sql_path.read_bytes()
    except OSError as error:
        checks = {
            "validation_sql_has_stage_bounds": False,
            "validation_sql_has_all_query_ids": False,
            "validation_sql_has_no_mutating_statements": False,
            "validation_sql_is_bounded": False,
            "validation_sql_is_repeatable_read_only": False,
        }
        errors.append(f"cannot read reviewed validation SQL: {error.strerror}")
        return None, checks, errors

    sql_sha256 = hashlib.sha256(raw_sql).hexdigest()
    if len(raw_sql) > MAX_SQL_BYTES:
        sql_text = ""
    else:
        try:
            sql_text = raw_sql.decode("utf-8")
        except UnicodeDecodeError:
            sql_text = ""

    checks = {
        "validation_sql_has_stage_bounds": (
            ":{?stage_started_at}" in sql_text
            and ":{?stage_ended_at}" in sql_text
            and ":'stage_started_at'::timestamp" in sql_text
            and ":'stage_ended_at'::timestamp" in sql_text
        ),
        "validation_sql_has_all_query_ids": all(
            f"'{query_id}' AS query_id" in sql_text for query_id in VALIDATION_QUERY_IDS
        ),
        "validation_sql_has_no_mutating_statements": (
            bool(sql_text) and FORBIDDEN_SQL.search(sql_text) is None
        ),
        "validation_sql_is_bounded": 0 < len(raw_sql) <= MAX_SQL_BYTES,
        "validation_sql_is_repeatable_read_only": (
            sql_text.count(
                "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY;"
            )
            == 1
            and sql_text.count("COMMIT;") == 1
        ),
    }
    messages = {
        "validation_sql_has_stage_bounds": "validation SQL must require both UTC stage bounds",
        "validation_sql_has_all_query_ids": "validation SQL is missing a reviewed query ID",
        "validation_sql_has_no_mutating_statements": "validation SQL contains a mutating or privileged statement",
        "validation_sql_is_bounded": "validation SQL is empty or exceeds the 65536-byte limit",
        "validation_sql_is_repeatable_read_only": "validation SQL must use one repeatable-read read-only transaction",
    }
    errors.extend(messages[name] for name, passed in checks.items() if not passed)
    return sql_sha256, checks, errors


def evaluate_plan(
    plan: dict[str, Any],
) -> tuple[str | None, dict[str, bool], list[str]]:
    errors: list[str] = []
    require_exact_fields(plan, "plan", TOP_LEVEL_FIELDS, errors)
    sql_sha256, sql_checks, sql_errors = inspect_validation_sql()

    checks = {
        "change_id_environment_is_required": same_value(
            plan.get("change_id_environment"), "DISK_CHANGE_ID"
        ),
        "execute_approval_environment_is_required": same_value(
            plan.get("execute_approval_environment"),
            "DISK_STAGING_ROLLOUT_APPROVED",
        ),
        "owner_environment_is_complete": same_value(
            plan.get("owner_environment"), OWNER_ENVIRONMENT
        ),
        "rollout_targets_s3_staging_init": same_value(
            plan.get("rollout"), "s3_staging_upload_init"
        )
        and same_value(plan.get("traffic_scope"), "upload_init_only"),
        "rollout_adapter_environment_is_required": same_value(
            plan.get("rollout_adapter_environment"), "DISK_STAGING_ROLLOUT_CLI"
        ),
        "safety_policy_is_exact": same_value(plan.get("safety"), SAFETY),
        "schema_version_is_supported": same_value(
            plan.get("schema_version"), SCHEMA_VERSION
        ),
        "stage_order_includes_every_reviewed_percentage": same_value(
            plan.get("stage_order_percent"), STAGE_ORDER
        ),
        "stages_bind_owners_commands_stops_and_queries": same_value(
            plan.get("stages"), expected_stages()
        ),
        "stop_conditions_are_complete": same_value(
            plan.get("stop_conditions"), STOP_CONDITIONS
        ),
        "validation_query_ids_are_complete": same_value(
            plan.get("validation_query_ids"), VALIDATION_QUERY_IDS
        ),
        "validation_sql_path_is_reviewed": same_value(
            plan.get("validation_sql"), REVIEWED_VALIDATION_SQL
        ),
        **sql_checks,
    }
    messages = {
        "change_id_environment_is_required": "change ID environment contract drifted",
        "execute_approval_environment_is_required": "execute approval environment contract drifted",
        "owner_environment_is_complete": "named owner environment contract drifted",
        "rollout_targets_s3_staging_init": "rollout must remain limited to S3 upload initialization",
        "rollout_adapter_environment_is_required": "rollout adapter environment contract drifted",
        "safety_policy_is_exact": "rollout safety policy differs from the reviewed contract",
        "schema_version_is_supported": "plan schema version is unsupported",
        "stage_order_includes_every_reviewed_percentage": "stage order must remain 10, 25, 50, 100",
        "stages_bind_owners_commands_stops_and_queries": "one or more stage contracts differ from the reviewed plan",
        "stop_conditions_are_complete": "stop conditions differ from the reviewed policy",
        "validation_query_ids_are_complete": "validation query IDs differ from the reviewed policy",
        "validation_sql_path_is_reviewed": "validation SQL path differs from the reviewed policy",
    }
    errors.extend(messages[name] for name, passed in checks.items() if not passed)
    errors.extend(sql_errors)
    return sql_sha256, checks, errors


def build_evidence(
    *,
    plan_sha256: str | None,
    sql_sha256: str | None,
    checks: dict[str, bool],
    errors: list[str],
) -> dict[str, Any]:
    return {
        "acceptance": {"errors": errors, "passed": not errors},
        "checks": checks,
        "plan": {"sha256": plan_sha256},
        "reviewed_contract": {
            "named_owner_roles": OWNER_ROLES,
            "safety": SAFETY,
            "stages": [
                {
                    "minimum_non_instant_tasks": minimum_tasks,
                    "minimum_observation_minutes": observation_minutes,
                    "percent": percent,
                    "previous_percent": previous_percent,
                }
                for percent, previous_percent, observation_minutes, minimum_tasks in STAGE_SPECS
            ],
            "stop_condition_ids": STOP_CONDITION_IDS,
            "traffic_scope": "upload_init_only",
            "validation_query_ids": VALIDATION_QUERY_IDS,
        },
        "scenario": SCENARIO,
        "schema_version": SCHEMA_VERSION,
        "validation_sql": {
            "path": REVIEWED_VALIDATION_SQL,
            "sha256": sql_sha256,
        },
    }


def write_evidence(output_path: Path, evidence: dict[str, Any]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(evidence, indent=2, sort_keys=True) + "\n").encode()
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


def check_plan(plan_path: Path, output_path: Path) -> int:
    try:
        plan_sha256, plan = read_plan(plan_path)
    except PlanInputError as error:
        evidence = build_evidence(
            plan_sha256=None,
            sql_sha256=None,
            checks={},
            errors=[str(error)],
        )
        write_evidence(output_path, evidence)
        print(f"staging rollout plan input rejected: {error}", file=sys.stderr)
        return 2

    sql_sha256, checks, errors = evaluate_plan(plan)
    evidence = build_evidence(
        plan_sha256=plan_sha256,
        sql_sha256=sql_sha256,
        checks=checks,
        errors=errors,
    )
    write_evidence(output_path, evidence)
    if errors:
        print(
            f"staging rollout plan rejected with {len(errors)} error(s); "
            f"evidence: {output_path}",
            file=sys.stderr,
        )
        return 1
    print(f"staging rollout plan accepted; evidence: {output_path}")
    return 0


def required_environment_value(name: str, label: str) -> str:
    value = os.environ.get(name, "")
    if value != value.strip() or not (2 <= len(value) <= 128):
        raise PlanInputError(f"{label} must be a named, non-empty value")
    lowered = value.lower()
    if (
        any(character in value for character in ("\x00", "\n", "\r", "<", ">"))
        or lowered.startswith("replace-")
        or lowered in {"change-me", "changeme", "placeholder"}
    ):
        raise PlanInputError(f"{label} must not be a placeholder")
    return value


def resolve_adapter(environment_name: str) -> Path:
    raw_path = os.environ.get(environment_name, "")
    adapter_path = Path(raw_path)
    if not raw_path or not adapter_path.is_absolute():
        raise PlanInputError("rollout adapter must be an absolute executable path")
    try:
        resolved = adapter_path.resolve(strict=True)
    except OSError as error:
        raise PlanInputError(
            f"cannot resolve rollout adapter: {error.strerror}"
        ) from error
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise PlanInputError("rollout adapter must be an executable regular file")
    return resolved


def sanitized_adapter_environment(approval_environment: str) -> dict[str, str]:
    sanitized: dict[str, str] = {}
    for name, value in os.environ.items():
        if name == approval_environment:
            continue
        if name.startswith(SENSITIVE_ADAPTER_ENV_PREFIXES):
            continue
        sanitized[name] = value
    return sanitized


def transition(
    plan_path: Path, from_percent: int, to_percent: int, execute: bool
) -> int:
    try:
        _, plan = read_plan(plan_path)
        _, _, errors = evaluate_plan(plan)
        if errors:
            raise PlanInputError(
                f"reviewed rollout plan failed {len(errors)} contract check(s)"
            )

        adjacent = [(0, 10), (10, 25), (25, 50), (50, 100)]
        if (from_percent, to_percent) not in adjacent and (
            to_percent,
            from_percent,
        ) not in adjacent:
            raise PlanInputError("transition must be adjacent in 0, 10, 25, 50, 100")

        change_environment = str(plan["change_id_environment"])
        change_id = required_environment_value(change_environment, "change ID")
        if CHANGE_ID_PATTERN.fullmatch(change_id) is None:
            raise PlanInputError("change ID has an invalid format")

        owner_environment = plan["owner_environment"]
        owners = {
            role: required_environment_value(str(owner_environment[role]), role)
            for role in OWNER_ROLES
        }
        adapter = resolve_adapter(str(plan["rollout_adapter_environment"]))
        approval_environment = str(plan["execute_approval_environment"])
        if execute and os.environ.get(approval_environment) != "true":
            raise PlanInputError(f"execution requires {approval_environment}=true")
    except PlanInputError as error:
        print(f"staging rollout transition rejected: {error}", file=sys.stderr)
        return 1

    adapter_command = [
        str(adapter),
        "apply" if execute else "preview",
        "--traffic-scope",
        "upload_init_only",
        "--from-percent",
        str(from_percent),
        "--to-percent",
        str(to_percent),
        "--change-id",
        change_id,
        "--release-owner",
        owners["release_owner"],
        "--rollback-owner",
        owners["rollback_owner"],
        "--database-verifier",
        owners["database_verifier"],
        "--storage-verifier",
        owners["storage_verifier"],
    ]
    try:
        result = subprocess.run(
            adapter_command,
            check=False,
            env=sanitized_adapter_environment(approval_environment),
        )
    except OSError as error:
        print(
            f"staging rollout adapter could not start: {error.strerror}",
            file=sys.stderr,
        )
        return 1
    if result.returncode != 0:
        print(
            f"staging rollout adapter failed with exit code {result.returncode}",
            file=sys.stderr,
        )
    return result.returncode


def main() -> int:
    arguments = parse_arguments()
    if arguments.command == "check":
        return check_plan(arguments.plan, arguments.output)
    return transition(
        arguments.plan,
        arguments.from_percent,
        arguments.to_percent,
        arguments.execute,
    )


if __name__ == "__main__":
    raise SystemExit(main())

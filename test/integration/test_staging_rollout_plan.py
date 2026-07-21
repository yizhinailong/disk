#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Contract tests for the staged S3 upload-initialization rollout plan."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from types import ModuleType
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/staging-rollout.py"
PLAN = ROOT / "deploy/staging-rollout-plan.json"
VALIDATION_SQL = ROOT / "deploy/staging-rollout-validation.sql"
EXPECTED_STAGE_ORDER = [10, 25, 50, 100]
EXPECTED_OWNER_ROLES = [
    "release_owner",
    "rollback_owner",
    "database_verifier",
    "storage_verifier",
]
EXPECTED_QUERY_IDS = [
    "preexisting_descriptors",
    "stage_uploads",
    "stage_backend_counts",
    "stage_cleanup_jobs",
    "cluster_blockers",
    "stage_quota_and_content_references",
    "unresolved_findings",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_command(
    arguments: list[str], *, environment: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *arguments],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        env=environment,
    )


def run_check(
    plan: Path, output: Path
) -> tuple[subprocess.CompletedProcess[str], dict[str, Any]]:
    result = run_command(["check", "--plan", str(plan), "--output", str(output)])
    require(output.is_file(), f"rollout check omitted evidence: {result.stderr}")
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
    result, evidence = run_check(plan_path, output_path)
    require(result.returncode == 1, f"invalid {name} rollout plan was accepted")
    require(evidence["acceptance"]["passed"] is False, f"{name} evidence passed")
    return evidence


def load_rollout_module() -> ModuleType:
    specification = importlib.util.spec_from_file_location(
        "disk_staging_rollout_contract", SCRIPT
    )
    require(specification is not None, "cannot create rollout module specification")
    require(specification.loader is not None, "rollout module has no loader")
    module = importlib.util.module_from_spec(specification)
    previous_dont_write_bytecode = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        specification.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous_dont_write_bytecode
    return module


def test_reviewed_plan_and_deterministic_evidence(temp_root: Path) -> None:
    first_output = temp_root / "accepted-first.json"
    second_output = temp_root / "accepted-second.json"
    first, evidence = run_check(PLAN, first_output)
    second, repeated = run_check(PLAN, second_output)

    require(first.returncode == 0, first.stderr)
    require(second.returncode == 0, second.stderr)
    require(first_output.read_bytes() == second_output.read_bytes(), "evidence drifted")
    require(evidence == repeated, "repeated rollout evidence changed")
    require(stat.S_IMODE(first_output.stat().st_mode) == 0o600, "evidence mode drifted")
    require(evidence["acceptance"] == {"errors": [], "passed": True}, "plan rejected")
    require(all(evidence["checks"].values()), "an accepted rollout check failed")
    require(
        evidence["plan"]["sha256"] == hashlib.sha256(PLAN.read_bytes()).hexdigest(),
        "plan digest drifted",
    )
    require(
        evidence["validation_sql"]["sha256"]
        == hashlib.sha256(VALIDATION_SQL.read_bytes()).hexdigest(),
        "validation SQL digest drifted",
    )

    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    require(plan["stage_order_percent"] == EXPECTED_STAGE_ORDER, "stage order drifted")
    require(
        [stage["percent"] for stage in plan["stages"]] == EXPECTED_STAGE_ORDER,
        "stage list drifted",
    )
    require(
        [stage["previous_percent"] for stage in plan["stages"]] == [0, 10, 25, 50],
        "rollback targets drifted",
    )
    require(
        [stage["minimum_observation_minutes"] for stage in plan["stages"]]
        == [30, 30, 60, 120],
        "observation windows drifted",
    )
    for stage in plan["stages"]:
        require(stage["owner_roles"] == EXPECTED_OWNER_ROLES, "stage owners drifted")
        require(
            stage["validation_query_ids"] == EXPECTED_QUERY_IDS,
            "stage validation queries drifted",
        )
        require(
            stage["stop_condition_ids"]
            == [condition["id"] for condition in plan["stop_conditions"]],
            "stage stop conditions drifted",
        )


def test_missing_stage_owner_command_stop_or_query_is_rejected(temp_root: Path) -> None:
    mutations: list[tuple[str, Callable[[dict[str, Any]], None]]] = [
        (
            "stage-order-without-25",
            lambda plan: plan["stage_order_percent"].remove(25),
        ),
        (
            "stage-list-without-25",
            lambda plan: plan["stages"].pop(1),
        ),
        (
            "non-adjacent-stage",
            lambda plan: plan["stages"][2].__setitem__("previous_percent", 10),
        ),
        (
            "missing-rollback-owner",
            lambda plan: plan["stages"][0]["owner_roles"].remove("rollback_owner"),
        ),
        (
            "missing-stage-stop",
            lambda plan: plan["stages"][1]["stop_condition_ids"].pop(),
        ),
        (
            "missing-stage-query",
            lambda plan: plan["stages"][2]["validation_query_ids"].pop(),
        ),
        (
            "wrong-rollback-command",
            lambda plan: plan["stages"][2].__setitem__(
                "rollback_command", "router rollback directly to zero"
            ),
        ),
        (
            "short-observation-window",
            lambda plan: plan["stages"][3].__setitem__(
                "minimum_observation_minutes", 10
            ),
        ),
        (
            "small-stage-sample",
            lambda plan: plan["stages"][0].__setitem__("minimum_non_instant_tasks", 10),
        ),
        (
            "database-mutation-enabled",
            lambda plan: plan["safety"].__setitem__("database_mutation_allowed", True),
        ),
        (
            "missing-shared-condition",
            lambda plan: plan["stop_conditions"].pop(),
        ),
    ]
    for name, mutation in mutations:
        rejected_mutation(temp_root, name, mutation)


def test_validation_sql_is_bounded_and_read_only(temp_root: Path) -> None:
    module = load_rollout_module()
    sql_sha256, checks, errors = module.inspect_validation_sql()
    require(sql_sha256 is not None, "reviewed validation SQL has no digest")
    require(all(checks.values()), f"reviewed validation SQL failed: {errors}")

    alternate_root = temp_root / "sql-mutation"
    alternate_sql = alternate_root / "deploy/staging-rollout-validation.sql"
    alternate_sql.parent.mkdir(parents=True)
    alternate_sql.write_text(
        VALIDATION_SQL.read_text(encoding="utf-8").replace(
            "COMMIT;", "UPDATE upload_tasks SET status = status;\nCOMMIT;"
        ),
        encoding="utf-8",
    )
    original_root = module.REPO_ROOT
    try:
        module.REPO_ROOT = alternate_root
        _, mutated_checks, mutated_errors = module.inspect_validation_sql()
    finally:
        module.REPO_ROOT = original_root
    require(
        mutated_checks["validation_sql_has_no_mutating_statements"] is False,
        "mutating validation SQL was accepted",
    )
    require(
        "validation SQL contains a mutating or privileged statement" in mutated_errors,
        "mutating SQL rejection reason drifted",
    )


def write_fake_adapter(path: Path) -> None:
    path.write_text(
        """#!/usr/bin/env python3
import json
import os
import sys
from pathlib import Path

Path(os.environ["FAKE_ROLLOUT_ADAPTER_LOG"]).write_text(
    json.dumps(
        {
            "argv": sys.argv[1:],
            "sensitive_environment_present": sorted(
                name
                for name in os.environ
                if name.startswith(
                    (
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
                )
            ),
        },
        sort_keys=True,
    )
    + "\\n",
    encoding="utf-8",
)
raise SystemExit(int(os.environ.get("FAKE_ROLLOUT_ADAPTER_EXIT", "0")))
""",
        encoding="utf-8",
    )
    path.chmod(0o755)


def transition_environment(adapter: Path, log_path: Path) -> dict[str, str]:
    return {
        **os.environ,
        "DISK_CHANGE_ID": "CHG-20260721-001",
        "DISK_RELEASE_OWNER": "release.oncall",
        "DISK_ROLLBACK_OWNER": "rollback.oncall",
        "DISK_DATABASE_VERIFIER": "database.oncall",
        "DISK_STORAGE_VERIFIER": "storage.oncall",
        "DISK_STAGING_ROLLOUT_CLI": str(adapter),
        "FAKE_ROLLOUT_ADAPTER_LOG": str(log_path),
        "DISK_DATABASE_URL": "postgresql://secret-sentinel",
        "DISK_S3_SECRET_KEY": "s3-secret-sentinel",
        "AWS_SECRET_ACCESS_KEY": "aws-secret-sentinel",
    }


def test_transition_is_adjacent_approved_and_argument_safe(temp_root: Path) -> None:
    adapter = temp_root / "fake-rollout-adapter"
    log_path = temp_root / "rollout-adapter.json"
    write_fake_adapter(adapter)
    environment = transition_environment(adapter, log_path)

    preview = run_command(
        [
            "transition",
            "--plan",
            str(PLAN),
            "--from",
            "10",
            "--to",
            "25",
        ],
        environment=environment,
    )
    require(preview.returncode == 0, preview.stderr)
    invocation = json.loads(log_path.read_text(encoding="utf-8"))
    require(
        invocation["argv"]
        == [
            "preview",
            "--traffic-scope",
            "upload_init_only",
            "--from-percent",
            "10",
            "--to-percent",
            "25",
            "--change-id",
            "CHG-20260721-001",
            "--release-owner",
            "release.oncall",
            "--rollback-owner",
            "rollback.oncall",
            "--database-verifier",
            "database.oncall",
            "--storage-verifier",
            "storage.oncall",
        ],
        "rollout adapter arguments drifted",
    )
    require(
        invocation["sensitive_environment_present"] == [],
        "rollout adapter inherited database or object-store secrets",
    )

    log_path.unlink()
    unapproved = run_command(
        [
            "transition",
            "--plan",
            str(PLAN),
            "--from",
            "25",
            "--to",
            "10",
            "--execute",
        ],
        environment=environment,
    )
    require(unapproved.returncode == 1, "unapproved transition was executed")
    require(not log_path.exists(), "unapproved transition called the rollout adapter")

    approved_environment = {
        **environment,
        "DISK_STAGING_ROLLOUT_APPROVED": "true",
    }
    approved = run_command(
        [
            "transition",
            "--plan",
            str(PLAN),
            "--from",
            "25",
            "--to",
            "10",
            "--execute",
        ],
        environment=approved_environment,
    )
    require(approved.returncode == 0, approved.stderr)
    invocation = json.loads(log_path.read_text(encoding="utf-8"))
    require(invocation["argv"][0] == "apply", "approved transition did not apply")
    require(
        invocation["argv"][4:8] == ["25", "--to-percent", "10", "--change-id"],
        "rollback transition did not target the immediately previous stage",
    )

    for name, arguments, changed_environment in (
        (
            "skipped-stage",
            ["--from", "10", "--to", "50"],
            approved_environment,
        ),
        (
            "placeholder-owner",
            ["--from", "10", "--to", "25"],
            {**approved_environment, "DISK_ROLLBACK_OWNER": "<rollback-owner>"},
        ),
        (
            "relative-adapter",
            ["--from", "10", "--to", "25"],
            {
                **approved_environment,
                "DISK_STAGING_ROLLOUT_CLI": "fake-rollout-adapter",
            },
        ),
    ):
        log_path.unlink(missing_ok=True)
        rejected = run_command(
            ["transition", "--plan", str(PLAN), *arguments, "--execute"],
            environment=changed_environment,
        )
        require(rejected.returncode == 1, f"{name} transition was accepted")
        require(not log_path.exists(), f"{name} transition called the adapter")

    failing_environment = {
        **approved_environment,
        "FAKE_ROLLOUT_ADAPTER_EXIT": "23",
    }
    adapter_failure = run_command(
        [
            "transition",
            "--plan",
            str(PLAN),
            "--from",
            "50",
            "--to",
            "100",
            "--execute",
        ],
        environment=failing_environment,
    )
    require(
        adapter_failure.returncode == 23,
        "rollout adapter failure was not propagated",
    )


def test_malformed_and_sensitive_plan_inputs_are_redacted(temp_root: Path) -> None:
    sentinel = "https://private-router-sentinel.invalid"
    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    plan["rollout_endpoint"] = sentinel
    sensitive_plan = temp_root / "sensitive.json"
    write_plan(sensitive_plan, plan)
    sensitive, evidence = run_check(
        sensitive_plan, temp_root / "sensitive-evidence.json"
    )
    require(sensitive.returncode == 1, "sensitive extra field was accepted")
    require(
        sentinel not in json.dumps(evidence, sort_keys=True),
        "rejection evidence leaked a rollout endpoint",
    )

    malformed_plan = temp_root / "malformed.json"
    malformed_plan.write_text("{not-json", encoding="utf-8")
    malformed, malformed_evidence = run_check(
        malformed_plan, temp_root / "malformed-evidence.json"
    )
    require(malformed.returncode == 2, "malformed JSON was accepted")
    require(
        malformed_evidence["acceptance"]["errors"] == ["plan is not valid UTF-8 JSON"],
        "malformed input error drifted",
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="disk-staging-rollout-plan-") as temporary:
        temp_root = Path(temporary)
        test_reviewed_plan_and_deterministic_evidence(temp_root)
        test_missing_stage_owner_command_stop_or_query_is_rejected(temp_root)
        test_validation_sql_is_bounded_and_read_only(temp_root)
        test_transition_is_adjacent_approved_and_argument_safe(temp_root)
        test_malformed_and_sensitive_plan_inputs_are_redacted(temp_root)
    print("staging rollout plan contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

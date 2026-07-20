#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Prove emergency application rollback cannot remove expand schema."""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path
from typing import Any

import psycopg
from psycopg import sql
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config


ROOT = Path(__file__).resolve().parents[2]
INIT_SQL = ROOT / "sql" / "init.sql"
MIGRATOR = ROOT / "scripts" / "migrate-db.sh"
REVERSAL = ROOT / "scripts" / "reverse-expand-migration.sh"
MIGRATION_DIR = ROOT / "sql" / "migrations"
MANIFEST = MIGRATION_DIR / "manifest.tsv"
ROLLBACKS = {
    "V002_share_audit": MIGRATION_DIR / "V002_share_audit_rollback.sql",
    "V003_distributed_upload": MIGRATION_DIR / "V003_distributed_upload_rollback.sql",
    "V004_storage_reconciliation": MIGRATION_DIR
    / "V004_storage_reconciliation_rollback.sql",
}
VALID_AUTHORIZATION = {
    "DISK_SCHEMA_REVERSAL_CONTEXT": "pre_activation_reversal",
    "DISK_SCHEMA_REVERSAL_APPROVED": "true",
    "DISK_SCHEMA_CHANGE_TICKET": "TEST-SCHEMA-REVERSAL",
    "DISK_SCHEMA_READINESS_SHA256": "5" * 64,
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def admin_config() -> dict[str, Any]:
    config = database_config()
    config["dbname"] = os.environ.get("PGMAINTENANCE_DB", "postgres")
    return config


def database_environment(database_name: str) -> dict[str, str]:
    config = database_config()
    environment = os.environ.copy()
    environment.update(
        {
            "PGHOST": str(config["host"]),
            "PGPORT": str(config["port"]),
            "PGDATABASE": database_name,
            "PGUSER": str(config["user"]),
            "PGPASSWORD": str(config["password"]),
        }
    )
    environment.pop("DISK_DATABASE_URL", None)
    return environment


def connect(database_name: str) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, autocommit=True, row_factory=dict_row)


def create_database(database_name: str) -> None:
    with psycopg.connect(**admin_config(), autocommit=True) as connection:
        connection.execute(
            sql.SQL("CREATE DATABASE {}").format(sql.Identifier(database_name))
        )


def drop_database(database_name: str) -> None:
    with psycopg.connect(**admin_config(), autocommit=True) as connection:
        connection.execute(
            "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
            "WHERE datname = %s AND pid <> pg_backend_pid()",
            (database_name,),
        )
        connection.execute(
            sql.SQL("DROP DATABASE IF EXISTS {}").format(sql.Identifier(database_name))
        )


def run_command(
    command: list[str],
    database_name: str,
    *,
    overrides: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    environment = database_environment(database_name)
    environment.update(overrides or {})
    return subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )


def run_sql(
    database_name: str,
    path: Path,
    variables: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    command = ["psql", "-X", "--set=ON_ERROR_STOP=1"]
    for name, value in (variables or {}).items():
        command.append(f"--set={name}={value}")
    command.extend(("--file", str(path)))
    return run_command(command, database_name)


def run_reversal(
    database_name: str,
    version: str,
    *,
    overrides: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    authorization = VALID_AUTHORIZATION.copy()
    authorization.update(overrides or {})
    return run_command([str(REVERSAL), version], database_name, overrides=authorization)


def scalar(connection: psycopg.Connection[dict[str, Any]], statement: str) -> Any:
    row = connection.execute(statement).fetchone()
    require(row is not None, f"query returned no row: {statement}")
    return next(iter(row.values()))


def schema_snapshot(database_name: str) -> dict[str, Any]:
    with connect(database_name) as connection:
        ledger = connection.execute(
            "SELECT version, checksum FROM schema_migrations ORDER BY version"
        ).fetchall()
        return {
            "storage_jobs": scalar(
                connection, "SELECT to_regclass('public.storage_jobs')::text"
            ),
            "reconciliation_findings": scalar(
                connection,
                "SELECT to_regclass('public.storage_reconciliation_findings')::text",
            ),
            "state_version_columns": scalar(
                connection,
                "SELECT COUNT(*) FROM information_schema.columns "
                "WHERE table_schema = 'public' AND table_name = 'upload_tasks' "
                "AND column_name = 'state_version'",
            ),
            "operation_log_user_nullable": scalar(
                connection,
                "SELECT is_nullable FROM information_schema.columns "
                "WHERE table_schema = 'public' AND table_name = 'operation_logs' "
                "AND column_name = 'user_id'",
            ),
            "ledger": [dict(row) for row in ledger],
        }


def psql_variables(context: str) -> dict[str, str]:
    return {
        "disk_schema_reversal_context": context,
        "disk_schema_reversal_approved": "true",
        "disk_schema_change_ticket": "TEST-DIRECT-REVERSAL",
        "disk_schema_readiness_sha256": "6" * 64,
    }


def verify_manifest_is_forward_only() -> None:
    entries: list[Path] = []
    for raw_line in MANIFEST.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        _version, filename = line.split("\t")
        require(filename.endswith("_forward.sql"), "manifest admitted non-forward SQL")
        entries.append(MIGRATION_DIR / filename)

    destructive = re.compile(r"\bDROP\s+(?:TABLE|COLUMN|DATABASE)\b", re.IGNORECASE)
    for path in entries:
        require(
            destructive.search(path.read_text(encoding="utf-8")) is None,
            f"forward manifest contains destructive DDL: {path.name}",
        )

    for path in ROLLBACKS.values():
        text = path.read_text(encoding="utf-8")
        include_at = text.find("\\ir schema_reversal_guard.sql")
        ddl_offsets = [
            offset
            for token in ("ALTER TABLE", "DROP TABLE", "DROP INDEX")
            if (offset := text.find(token)) >= 0
        ]
        require(include_at >= 0, f"{path.name} omitted the schema reversal guard")
        require(
            ddl_offsets and include_at < min(ddl_offsets),
            f"{path.name} executes DDL before its authorization guard",
        )


def verify_runner_command_surface(temporary: Path) -> int:
    fake_psql = temporary / "psql"
    log_path = temporary / "psql.log"
    fake_psql.write_text(
        '#!/usr/bin/env bash\nprintf \'%s\\n\' "$*" >> "$FAKE_PSQL_LOG"\n',
        encoding="utf-8",
    )
    fake_psql.chmod(0o755)
    base_environment = {
        **VALID_AUTHORIZATION,
        "PSQL_BIN": str(fake_psql),
        "FAKE_PSQL_LOG": str(log_path),
    }

    rejected = 0
    cases = (
        ("V999_unknown", {}, 64),
        (
            "V004_storage_reconciliation",
            {"DISK_SCHEMA_REVERSAL_CONTEXT": "emergency_application_rollback"},
            77,
        ),
        (
            "V004_storage_reconciliation",
            {"DISK_SCHEMA_REVERSAL_APPROVED": "false"},
            77,
        ),
        (
            "V004_storage_reconciliation",
            {"DISK_SCHEMA_CHANGE_TICKET": "bad"},
            77,
        ),
        (
            "V004_storage_reconciliation",
            {"DISK_SCHEMA_READINESS_SHA256": "ABC"},
            77,
        ),
    )
    for version, overrides, expected_status in cases:
        environment = os.environ.copy()
        environment.update(base_environment)
        environment.update(overrides)
        result = subprocess.run(
            [str(REVERSAL), version],
            cwd=ROOT,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )
        require(result.returncode == expected_status, f"runner accepted {version}")
        rejected += 1
    require(not log_path.exists(), "rejected reversal opened a database client")

    environment = os.environ.copy()
    environment.update(base_environment)
    accepted = subprocess.run(
        [str(REVERSAL), "V004_storage_reconciliation"],
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    require(accepted.returncode == 0, accepted.stderr)
    calls = log_path.read_text(encoding="utf-8").splitlines()
    require(len(calls) == 1, "approved reversal did not make exactly one psql call")
    require(
        "disk_schema_reversal_context=pre_activation_reversal" in calls[0]
        and "V004_storage_reconciliation_rollback.sql" in calls[0],
        "approved reversal omitted its guarded context or exact SQL file",
    )
    return rejected


def write_evidence(payload: dict[str, Any]) -> Path:
    output = ROOT / ".sisyphus/evidence/schema-reversal-guard-summary.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=output.parent, prefix=f".{output.name}."
    )
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        os.fchmod(handle.fileno(), 0o600)
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary_name, output)
    return output


def main() -> int:
    verify_manifest_is_forward_only()
    with tempfile.TemporaryDirectory(prefix="disk-schema-reversal-command-") as temp:
        runner_rejections = verify_runner_command_surface(Path(temp))

    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_schema_reversal_{suffix}"
    create_database(database_name)
    try:
        initialized = run_sql(database_name, INIT_SQL)
        require(initialized.returncode == 0, initialized.stderr)
        baseline = schema_snapshot(database_name)

        direct_rejections = 0
        for path in ROLLBACKS.values():
            rejected = run_sql(database_name, path)
            require(rejected.returncode != 0, f"unguarded SQL executed: {path.name}")
            require(
                "schema reversal blocked" in (rejected.stdout + rejected.stderr),
                f"{path.name} did not report its authorization boundary",
            )
            require(
                schema_snapshot(database_name) == baseline,
                f"{path.name} changed schema or ledger before authorization",
            )
            direct_rejections += 1

        emergency = run_sql(
            database_name,
            ROLLBACKS["V004_storage_reconciliation"],
            psql_variables("emergency_application_rollback"),
        )
        require(emergency.returncode != 0, "emergency context executed schema DDL")
        require(
            schema_snapshot(database_name) == baseline,
            "emergency context mutated schema",
        )

        sql_parameter_rejections = 0
        for missing_name in (
            "disk_schema_reversal_approved",
            "disk_schema_change_ticket",
            "disk_schema_readiness_sha256",
        ):
            variables = psql_variables("pre_activation_reversal")
            del variables[missing_name]
            rejected = run_sql(
                database_name,
                ROLLBACKS["V004_storage_reconciliation"],
                variables,
            )
            require(
                rejected.returncode != 0,
                f"SQL guard accepted missing {missing_name}",
            )
            require(
                schema_snapshot(database_name) == baseline,
                f"missing {missing_name} mutated schema or ledger",
            )
            sql_parameter_rejections += 1

        with connect(database_name) as connection:
            connection.execute(
                "INSERT INTO storage_reconciliation_findings "
                "(finding_type, resource_id, severity, resolution_strategy) "
                "VALUES ('missing_final_blob', 'schema-guard', 2, 'manual')"
            )
        before_data_guard = schema_snapshot(database_name)
        data_blocked = run_reversal(database_name, "V004_storage_reconciliation")
        require(data_blocked.returncode != 0, "non-empty finding table was reversed")
        require(
            "contains records" in (data_blocked.stdout + data_blocked.stderr),
            "migration-specific data guard did not run",
        )
        require(
            schema_snapshot(database_name) == before_data_guard,
            "data-guard rejection mutated schema or ledger",
        )
        with connect(database_name) as connection:
            connection.execute("DELETE FROM storage_reconciliation_findings")

        approved = run_reversal(database_name, "V004_storage_reconciliation")
        require(approved.returncode == 0, approved.stderr)
        with connect(database_name) as connection:
            require(
                scalar(
                    connection,
                    "SELECT to_regclass('public.storage_reconciliation_findings')",
                )
                is None,
                "approved pre-activation reversal retained the V004 table",
            )
            require(
                scalar(
                    connection,
                    "SELECT COUNT(*) FROM schema_migrations "
                    "WHERE version = 'V004_storage_reconciliation'",
                )
                == 0,
                "approved pre-activation reversal retained the V004 ledger row",
            )

        restored = run_command([str(MIGRATOR)], database_name)
        require(restored.returncode == 0, restored.stderr)
        require(
            schema_snapshot(database_name) == baseline, "forward restore drifted schema"
        )

        evidence = {
            "schema_version": 1,
            "scenario": "expand_schema_preserved_during_emergency_rollback",
            "schema_action": "preserve_expand",
            "contract_migration_allowed": False,
            "direct_sql_rejections": direct_rejections,
            "emergency_context_rejected": True,
            "sql_parameter_rejections": sql_parameter_rejections,
            "runner_rejections_before_psql": runner_rejections,
            "migration_data_guard_rejected": True,
            "approved_pre_activation_reversal": True,
            "forward_manifest_destructive_ddl": 0,
            "rejected_schema_or_ledger_mutations": 0,
            "passed": True,
        }
        evidence_path = write_evidence(evidence)
        require(evidence_path.stat().st_mode & 0o777 == 0o600, "evidence mode drifted")
        print("PASS: emergency rollback preserved expand schema and migration ledger")
        return 0
    finally:
        drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

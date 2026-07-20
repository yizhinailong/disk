#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Exercise the upload rollback drain/freeze gate against PostgreSQL."""

from __future__ import annotations

import json
import os
import stat
import subprocess
import sys
import tempfile
import threading
import uuid
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Iterator

import psycopg
from psycopg import sql
from psycopg.conninfo import make_conninfo
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config


ROOT = Path(__file__).resolve().parents[2]
INIT_SQL = ROOT / "sql" / "init.sql"
GATE = ROOT / "scripts" / "check-upload-rollback-readiness.py"
FROZEN_MESSAGE = "Upload lifecycle is temporarily frozen for rollback"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def admin_config() -> dict[str, Any]:
    config = database_config()
    config["dbname"] = os.environ.get("PGMAINTENANCE_DB", "postgres")
    return config


def database_env(database_name: str) -> dict[str, str]:
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
    return environment


def database_url(database_name: str) -> str:
    config = database_config()
    config["dbname"] = database_name
    return make_conninfo(**config)


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


def initialize_database(database_name: str) -> None:
    result = subprocess.run(
        ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(INIT_SQL)],
        cwd=ROOT,
        env=database_env(database_name),
        check=False,
        capture_output=True,
        text=True,
    )
    require(result.returncode == 0, f"init.sql failed: {result.stderr}")


def seed_active_tasks(database_name: str, suffix: str) -> None:
    with connect(database_name) as connection:
        user = connection.execute(
            "SELECT id FROM users WHERE username = 'admin'"
        ).fetchone()
        require(user is not None, "admin fixture is missing")
        user_id = int(user["id"])
        connection.execute(
            "UPDATE users SET storage_reserved = 12 WHERE id = %s",
            (user_id,),
        )
        connection.execute(
            "INSERT INTO upload_tasks "
            "(id, user_id, filename, file_size, file_hash, chunk_size, total_chunks, "
            "reserved_bytes, temp_path, staging_backend, staging_prefix, expires_at) "
            "VALUES (%s, %s, 'rollback-in-progress.bin', 6, %s, 6, 1, 6, %s, "
            "'s3', %s, NOW() + INTERVAL '1 day')",
            (
                f"rollback-progress-{suffix}",
                user_id,
                "1" * 32,
                f"rollback-progress-{suffix}",
                f"staging/{suffix}/rollback-progress-{suffix}",
            ),
        )
        connection.execute(
            "INSERT INTO upload_tasks "
            "(id, user_id, filename, file_size, file_hash, chunk_size, total_chunks, "
            "reserved_bytes, temp_path, staging_backend, staging_prefix, status, "
            "state_version, lease_owner, lease_expires_at, finalize_attempts, expires_at) "
            "VALUES (%s, %s, 'rollback-finalizing.bin', 6, %s, 6, 1, 6, %s, "
            "'s3', %s, 4, 7, %s, NOW() + INTERVAL '1 day', 1, "
            "NOW() + INTERVAL '1 day')",
            (
                f"rollback-finalizing-{suffix}",
                user_id,
                "2" * 32,
                f"rollback-finalizing-{suffix}",
                f"staging/{suffix}/rollback-finalizing-{suffix}",
                f"rollback-owner-{suffix}",
            ),
        )


def task_snapshot(database_name: str) -> list[dict[str, Any]]:
    with connect(database_name) as connection:
        rows = connection.execute(
            "SELECT id, status, staging_backend, staging_prefix, state_version, "
            "lease_owner, lease_expires_at::text AS lease_expires_at, xmin::text AS xmin "
            "FROM upload_tasks ORDER BY id"
        ).fetchall()
    return [dict(row) for row in rows]


def terminalize_tasks(database_name: str) -> None:
    with connect(database_name) as connection:
        connection.execute(
            "UPDATE upload_tasks SET status = 2, lease_owner = NULL, "
            "lease_expires_at = NULL, state_version = state_version + 1 "
            "WHERE status IN (0, 4)"
        )
        connection.execute("UPDATE users SET storage_reserved = 0")


class ProbeServer(ThreadingHTTPServer):
    health_data: dict[str, Any] | None
    frozen_ingress: bool


class ProbeHandler(BaseHTTPRequestHandler):
    server: ProbeServer

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def send_json(
        self,
        status: int,
        payload: dict[str, Any],
        headers: dict[str, str] | None = None,
    ) -> None:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path == "/api/health/ready" and self.server.health_data is not None:
            self.send_json(
                200,
                {"code": 0, "message": "success", "data": self.server.health_data},
            )
            return
        self.send_json(404, {"code": 10004, "message": "not found", "data": None})

    def do_POST(self) -> None:
        if self.path.startswith("/api/file/upload") and self.server.frozen_ingress:
            self.send_json(
                503,
                {"code": 50013, "message": FROZEN_MESSAGE, "data": None},
                {"Retry-After": "30", "Cache-Control": "no-store"},
            )
            return
        self.send_json(401, {"code": 40106, "message": "Token missing", "data": None})


@contextmanager
def probe_server(
    *,
    health_data: dict[str, Any] | None = None,
    frozen_ingress: bool = False,
) -> Iterator[tuple[str, ProbeServer]]:
    server = ProbeServer(("127.0.0.1", 0), ProbeHandler)
    server.health_data = health_data
    server.frozen_ingress = frozen_ingress
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        host, port = server.server_address
        yield f"http://{host}:{port}", server
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


def healthy_api(instance_id: str) -> dict[str, Any]:
    return {
        "overall_status": "healthy",
        "role": "api",
        "instance_id": instance_id,
        "initialized": True,
        "draining": False,
        "worker_claiming_enabled": False,
        "worker_accepting": False,
        "upload_task_creation_enabled": False,
        "business_requests_inflight": 0,
        "version": "rollback-compatible-test",
    }


def run_gate(
    *,
    mode: str,
    api_urls: list[str],
    ingress_url: str,
    database_dsn: str,
    output: Path,
) -> tuple[subprocess.CompletedProcess[str], dict[str, Any]]:
    command = [sys.executable, str(GATE), "--mode", mode]
    for api_url in api_urls:
        command.extend(("--api-url", api_url))
    command.extend(("--ingress-url", ingress_url, "--output", str(output)))
    environment = os.environ.copy()
    environment["DISK_DATABASE_URL"] = database_dsn
    result = subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    require(output.is_file(), f"rollback gate omitted evidence: {result.stderr}")
    return result, json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_upload_rollback_{suffix}"
    create_database(database_name)
    try:
        initialize_database(database_name)
        seed_active_tasks(database_name, suffix)
        dsn = database_url(database_name)

        with (
            probe_server(health_data=healthy_api(f"rollback-api-a-{suffix}")) as api_a,
            probe_server(health_data=healthy_api(f"rollback-api-b-{suffix}")) as api_b,
            probe_server(frozen_ingress=True) as ingress,
            tempfile.TemporaryDirectory(
                prefix="disk-upload-rollback-gate-"
            ) as temporary,
        ):
            temp_root = Path(temporary)
            before = task_snapshot(database_name)
            freeze_result, freeze = run_gate(
                mode="freeze",
                api_urls=[api_a[0], api_b[0]],
                ingress_url=ingress[0],
                database_dsn=dsn,
                output=temp_root / "freeze.json",
            )
            require(freeze_result.returncode == 0, freeze_result.stderr)
            require(
                freeze["acceptance"] == {"errors": [], "passed": True},
                "freeze rejected",
            )
            require(
                freeze["database"]["transaction"] == "repeatable_read_read_only",
                "snapshot mode drifted",
            )
            require(
                freeze["database"]["status_counts"]["in_progress"] == 1,
                "in-progress count drifted",
            )
            require(
                freeze["database"]["status_counts"]["finalizing"] == 1,
                "finalizing count drifted",
            )
            require(
                freeze["database"]["active_task_count"] == 2, "active count drifted"
            )
            require(
                freeze["database"]["finalizing_with_active_lease"] == 1,
                "lease count drifted",
            )
            require(
                len(freeze["database"]["active_descriptor_sha256"]) == 64,
                "digest missing",
            )
            require(
                freeze["decision"]
                == {
                    "active_task_disposition": "freeze",
                    "compatible_handlers_required": True,
                    "old_release_upload_route_allowed": False,
                    "upload_ingress": "closed",
                },
                "freeze decision drifted",
            )
            require(
                task_snapshot(database_name) == before,
                "freeze gate mutated upload rows",
            )
            require(
                stat.S_IMODE((temp_root / "freeze.json").stat().st_mode) == 0o600,
                "evidence permissions drifted",
            )
            rendered = json.dumps(freeze, sort_keys=True)
            require(dsn not in rendered, "evidence leaked the database DSN")
            password = str(database_config()["password"])
            if password:
                require(
                    password not in rendered, "evidence leaked the database password"
                )

            drain_result, drain = run_gate(
                mode="drain",
                api_urls=[api_a[0], api_b[0]],
                ingress_url=ingress[0],
                database_dsn=dsn,
                output=temp_root / "drain-blocked.json",
            )
            require(drain_result.returncode == 1, "active tasks passed drain mode")
            require(
                drain["acceptance"]["passed"] is False, "blocked drain evidence passed"
            )
            require(
                "drain mode requires zero InProgress and zero Finalizing tasks"
                in drain["acceptance"]["errors"],
                "drain rejection reason drifted",
            )
            require(
                task_snapshot(database_name) == before, "drain gate mutated upload rows"
            )

            api_a[1].health_data["business_requests_inflight"] = 1
            inflight_result, inflight = run_gate(
                mode="freeze",
                api_urls=[api_a[0], api_b[0]],
                ingress_url=ingress[0],
                database_dsn=dsn,
                output=temp_root / "inflight-blocked.json",
            )
            require(
                inflight_result.returncode == 1, "in-flight request passed freeze mode"
            )
            require(
                inflight["checks"]["business_requests_are_drained"] is False,
                "in-flight rejection was not recorded",
            )
            api_a[1].health_data["business_requests_inflight"] = 0

            api_b[1].health_data["upload_task_creation_enabled"] = True
            creation_result, creation = run_gate(
                mode="freeze",
                api_urls=[api_a[0], api_b[0]],
                ingress_url=ingress[0],
                database_dsn=dsn,
                output=temp_root / "creation-open-blocked.json",
            )
            require(
                creation_result.returncode == 1, "open task creation passed freeze mode"
            )
            require(
                creation["checks"]["new_task_creation_is_closed"] is False,
                "creation cutoff rejection was not recorded",
            )
            api_b[1].health_data["upload_task_creation_enabled"] = False

            with probe_server(frozen_ingress=False) as open_ingress:
                ingress_result, ingress_evidence = run_gate(
                    mode="freeze",
                    api_urls=[api_a[0], api_b[0]],
                    ingress_url=open_ingress[0],
                    database_dsn=dsn,
                    output=temp_root / "ingress-open-blocked.json",
                )
            require(ingress_result.returncode == 1, "open ingress passed freeze mode")
            require(
                ingress_evidence["checks"]["public_upload_ingress_is_frozen"] is False,
                "open ingress rejection was not recorded",
            )

            terminalize_tasks(database_name)
            drained_result, drained = run_gate(
                mode="drain",
                api_urls=[api_a[0], api_b[0]],
                ingress_url=ingress[0],
                database_dsn=dsn,
                output=temp_root / "drained.json",
            )
            require(drained_result.returncode == 0, drained_result.stderr)
            require(
                drained["database"]["active_task_count"] == 0, "drain count drifted"
            )
            require(
                drained["decision"]["active_task_disposition"] == "drain"
                and drained["decision"]["compatible_handlers_required"] is False
                and drained["decision"]["old_release_upload_route_allowed"] is False,
                "drain decision drifted",
            )

        evidence_path = ROOT / ".sisyphus/evidence/upload-rollback-gate-summary.json"
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "scenario": "upload_rollback_drain_and_freeze",
                    "freeze_active_tasks": 2,
                    "freeze_descriptor_sha256": freeze["database"][
                        "active_descriptor_sha256"
                    ],
                    "drain_active_tasks": drained["database"]["active_task_count"],
                    "blocked_conditions": [
                        "active_tasks_in_drain_mode",
                        "business_requests_inflight",
                        "new_task_creation_open",
                        "upload_ingress_open",
                    ],
                    "database_mutations_by_gate": 0,
                    "old_release_upload_route_allowed": False,
                    "passed": True,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        print(
            "PASS: upload rollback gate drained or froze active tasks without mutation"
        )
        return 0
    finally:
        drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

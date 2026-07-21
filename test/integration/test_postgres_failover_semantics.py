#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify stable-endpoint PostgreSQL promotion with two live APIs."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any, Callable, IO

import httpx
import psycopg
from psycopg import sql
from psycopg.rows import dict_row

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_auth_cluster_consistency import (  # noqa: E402
    CuttableTcpProxy,
    auth_headers,
    dependency_ready,
    register_and_login,
    require_error_code,
    require_liveness,
    response_envelope,
    success_data,
)
from test_expand_mixed_version import (  # noqa: E402
    INIT_SQL,
    ManagedServer,
    allocate_ports,
    require,
    resolve_current_binary,
    server_config,
)
from test_redis_session_persistence import (  # noqa: E402
    PersistentRedis,
    resolve_executable,
)


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = ROOT / ".sisyphus/evidence/postgres-failover-semantics-summary.json"
DATABASE_NAME = "disk_failover"
DATABASE_USER = "disk_failover"
API_A = "postgres-failover-a"
API_B = "postgres-failover-b"


def wait_for(
    description: str,
    predicate: Callable[[], bool],
    *,
    timeout_seconds: float = 30,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error = ""
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (OSError, psycopg.Error, subprocess.SubprocessError) as error:
            last_error = str(error)
        time.sleep(0.1)
    detail = f": {last_error}" if last_error else ""
    raise AssertionError(f"timed out waiting for {description}{detail}")


class PostgresNode:
    """Own one isolated PostgreSQL server process and data directory."""

    def __init__(
        self,
        name: str,
        root: Path,
        port: int,
        *,
        server_settings: tuple[str, ...] = (),
    ) -> None:
        self.name = name
        self.root = root
        self.port = port
        self.server_settings = server_settings
        self.initdb_binary = resolve_executable(("initdb",))
        self.postgres_binary = resolve_executable(("postgres",))
        self.pg_basebackup_binary = resolve_executable(("pg_basebackup",))
        self.pg_ctl_binary = resolve_executable(("pg_ctl",))
        self.psql_binary = resolve_executable(("psql",))
        self.log_path = root.parent / f"{name}.log"
        self.process: subprocess.Popen[bytes] | None = None
        self.log_handle: IO[bytes] | None = None

    @property
    def pid(self) -> int:
        require(self.process is not None, f"{self.name} PostgreSQL was not started")
        return self.process.pid

    def _run_checked(
        self,
        command: list[str],
        *,
        timeout_seconds: float = 90,
    ) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
            check=False,
        )
        require(
            result.returncode == 0,
            f"{self.name} command failed with {result.returncode}: "
            f"{result.stderr.strip()[-2000:]}",
        )
        return result

    def initialize_primary(self) -> None:
        require(not self.root.exists(), "primary PostgreSQL directory already exists")
        self.root.parent.mkdir(parents=True, exist_ok=True)
        self._run_checked(
            [
                str(self.initdb_binary),
                "-D",
                str(self.root),
                f"--username={DATABASE_USER}",
                "--auth-host=trust",
                "--auth-local=trust",
                "--encoding=UTF8",
                "--no-locale",
            ]
        )

    def clone_from(self, primary: PostgresNode) -> None:
        require(not self.root.exists(), "standby PostgreSQL directory already exists")
        self.root.parent.mkdir(parents=True, exist_ok=True)
        self._run_checked(
            [
                str(self.pg_basebackup_binary),
                "-h",
                "127.0.0.1",
                "-p",
                str(primary.port),
                "-U",
                DATABASE_USER,
                "-D",
                str(self.root),
                "--format=plain",
                "--wal-method=stream",
                "--write-recovery-conf",
                "--checkpoint=fast",
                "--no-password",
            ]
        )

    def start(self) -> None:
        require(self.process is None, f"{self.name} PostgreSQL is already running")
        require(self.root.is_dir(), f"{self.name} PostgreSQL directory is missing")
        self.log_handle = self.log_path.open("ab")
        command = [
            str(self.postgres_binary),
            "-D",
            str(self.root),
            "-p",
            str(self.port),
            "-c",
            "listen_addresses=127.0.0.1",
            "-c",
            "unix_socket_directories=",
            "-c",
            "max_connections=50",
            "-c",
            "wal_level=replica",
            "-c",
            "max_wal_senders=5",
            "-c",
            "max_replication_slots=5",
            "-c",
            "hot_standby=on",
            "-c",
            "fsync=on",
            "-c",
            "synchronous_commit=on",
            "-c",
            "full_page_writes=on",
            "-c",
            "log_min_messages=warning",
        ]
        for setting in self.server_settings:
            command.extend(("-c", setting))
        try:
            self.process = subprocess.Popen(
                command,
                cwd=self.root,
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
            )
            self._wait_until_ready()
        except BaseException:
            self.stop()
            raise

    def _wait_until_ready(self) -> None:
        def ready() -> bool:
            if self.process is not None and self.process.poll() is not None:
                raise AssertionError(
                    f"{self.name} PostgreSQL exited with {self.process.returncode}\n"
                    f"{self.log_tail()}"
                )
            with self.connect("postgres") as connection:
                row = connection.execute("SELECT 1 AS ready").fetchone()
                return row is not None and row["ready"] == 1

        wait_for(f"{self.name} PostgreSQL readiness", ready, timeout_seconds=20)

    def connect(self, database_name: str) -> psycopg.Connection[dict[str, Any]]:
        return psycopg.connect(
            host="127.0.0.1",
            port=self.port,
            dbname=database_name,
            user=DATABASE_USER,
            connect_timeout=1,
            autocommit=True,
            row_factory=dict_row,
        )

    def scalar(
        self,
        query: str,
        parameters: tuple[Any, ...] = (),
        *,
        database_name: str = "postgres",
    ) -> Any:
        with self.connect(database_name) as connection:
            row = connection.execute(query, parameters).fetchone()
        require(row is not None and row, f"{self.name} query returned no scalar")
        return next(iter(row.values()))

    def execute(
        self,
        query: str,
        parameters: tuple[Any, ...] = (),
        *,
        database_name: str = "postgres",
    ) -> None:
        with self.connect(database_name) as connection:
            connection.execute(query, parameters)

    def create_database(self) -> None:
        with self.connect("postgres") as connection:
            connection.execute(
                sql.SQL("CREATE DATABASE {}").format(sql.Identifier(DATABASE_NAME))
            )

    def load_schema(self) -> None:
        self._run_checked(
            [
                str(self.psql_binary),
                "-X",
                "-v",
                "ON_ERROR_STOP=1",
                "-h",
                "127.0.0.1",
                "-p",
                str(self.port),
                "-U",
                DATABASE_USER,
                "-d",
                DATABASE_NAME,
                "-f",
                str(INIT_SQL),
            ]
        )

    def system_identifier(self) -> str:
        return str(self.scalar("SELECT system_identifier::text FROM pg_control_system()"))

    def timeline(self) -> int:
        wal_prefix = str(
            self.scalar(
                "SELECT substring(pg_walfile_name(pg_current_wal_lsn()) FROM 1 FOR 8)"
            )
        )
        return int(wal_prefix, 16)

    def promote(self) -> None:
        self._run_checked(
            [
                str(self.pg_ctl_binary),
                "-D",
                str(self.root),
                "-w",
                "-t",
                "15",
                "promote",
            ],
            timeout_seconds=20,
        )
        wait_for(
            f"{self.name} promotion",
            lambda: self.scalar("SELECT NOT pg_is_in_recovery()") is True,
        )

    def crash(self) -> int:
        require(self.process is not None, f"{self.name} PostgreSQL was not started")
        process = self.process
        old_pid = process.pid
        process.kill()
        process.wait(timeout=10)
        self.process = None
        self._close_log()
        return old_pid

    def stop(self) -> None:
        process = self.process
        if process is not None:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            self.process = None
        self._close_log()

    def _close_log(self) -> None:
        if self.log_handle is not None:
            self.log_handle.close()
            self.log_handle = None

    def log_tail(self) -> str:
        if self.log_handle is not None:
            self.log_handle.flush()
        if not self.log_path.is_file():
            return f"{self.name} PostgreSQL log unavailable"
        lines = self.log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        return f"{self.name} PostgreSQL log tail:\n" + "\n".join(lines[-80:])


def replication_ready(primary: PostgresNode, standby: PostgresNode) -> bool:
    return (
        primary.scalar(
            "SELECT EXISTS (SELECT 1 FROM pg_stat_replication WHERE state = 'streaming')"
        )
        is True
        and standby.scalar("SELECT pg_is_in_recovery()") is True
        and standby.scalar("SHOW transaction_read_only") == "on"
    )


def wait_for_replay(primary: PostgresNode, standby: PostgresNode) -> str:
    target_lsn = str(primary.scalar("SELECT pg_current_wal_lsn()::text"))
    wait_for(
        f"standby replay through {target_lsn}",
        lambda: standby.scalar(
            "SELECT pg_last_wal_replay_lsn() >= %s::pg_lsn",
            (target_lsn,),
        )
        is True,
    )
    return target_lsn


def failover_api_config(
    database_proxy_port: int,
    redis_port: int,
    api_port: int,
    instance_id: str,
    storage_root: Path,
    staging_root: Path,
) -> dict[str, Any]:
    config = server_config(
        DATABASE_NAME,
        api_port,
        instance_id,
        storage_root,
        staging_root,
        role="api",
    )
    database = config["db_clients"][0]
    database.update(
        {
            "host": "127.0.0.1",
            "port": database_proxy_port,
            "dbname": DATABASE_NAME,
            "user": DATABASE_USER,
            "passwd": "",
            "connection_number": 4,
            "timeout": 1.0,
            "auto_batch": False,
        }
    )
    redis = config["redis_clients"][0]
    redis.update(
        {
            "host": "127.0.0.1",
            "port": redis_port,
            "db": 0,
            "passwd": "",
            "timeout": 1.0,
        }
    )
    return config


def start_api(
    *,
    name: str,
    binary: Path,
    run_directory: Path,
    database_proxy_port: int,
    redis_port: int,
    api_port: int,
    storage_root: Path,
    staging_root: Path,
) -> ManagedServer:
    return ManagedServer(
        name=name,
        binary=binary,
        run_directory=run_directory,
        config=failover_api_config(
            database_proxy_port,
            redis_port,
            api_port,
            name,
            storage_root,
            staging_root,
        ),
        database_name=DATABASE_NAME,
        port=api_port,
        readiness_path="/api/health/ready",
        role="api",
        environment_overrides={
            "DATABASE_HOST": "127.0.0.1",
            "DATABASE_PORT": str(database_proxy_port),
            "DATABASE_NAME": DATABASE_NAME,
            "DATABASE_USER": DATABASE_USER,
            "DATABASE_POOL_SIZE": "4",
            "REDIS_HOST": "127.0.0.1",
            "REDIS_PORT": str(redis_port),
            "REDIS_DB": "0",
            "REDIS_POOL_SIZE": "4",
        },
        environment_removals=("DATABASE_PASSWORD", "REDIS_PASSWORD"),
    )


def require_database_failure(
    base_url: str,
    access_token: str,
    *,
    method: str = "GET",
    json_body: dict[str, str] | None = None,
) -> float:
    started_at = time.monotonic()
    response = httpx.request(
        method,
        base_url + "/api/user/profile",
        headers=auth_headers(access_token),
        json=json_body,
        timeout=10,
    )
    elapsed = time.monotonic() - started_at
    require_error_code(
        response,
        "10006",
        f"PostgreSQL failover timeout at {base_url}",
        expected_status=500,
    )
    require(
        0.5 <= elapsed < 5.0,
        f"PostgreSQL command ignored the configured timeout: {elapsed:.3f}s",
    )
    return elapsed


def database_unready(base_url: str) -> bool:
    response = httpx.get(base_url + "/api/health/ready", timeout=8)
    if response.status_code != 503:
        return False
    payload = response_envelope(response, "PostgreSQL-unready readiness")
    data = payload.get("data")
    if not isinstance(data, dict):
        return False
    components = data.get("components")
    if not isinstance(components, dict):
        return False
    database = components.get("database")
    return isinstance(database, dict) and database.get("status") == "unhealthy"


def profile_user(base_url: str, access_token: str) -> dict[str, Any]:
    response = httpx.get(
        base_url + "/api/user/profile",
        headers=auth_headers(access_token),
        timeout=10,
    )
    data = success_data(response, f"profile at {base_url}")
    user = data.get("user")
    require(isinstance(user, dict), f"profile at {base_url} has no user")
    return user


def update_nickname(
    base_url: str,
    access_token: str,
    nickname: str,
) -> None:
    response = httpx.patch(
        base_url + "/api/user/profile",
        headers=auth_headers(access_token),
        json={"nickname": nickname},
        timeout=10,
    )
    data = success_data(response, f"profile update at {base_url}")
    user = data.get("user")
    require(
        isinstance(user, dict) and user.get("nickname") == nickname,
        "profile update returned the wrong nickname",
    )


def write_evidence(payload: dict[str, Any]) -> None:
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=EVIDENCE_PATH.parent,
        prefix=f".{EVIDENCE_PATH.name}.",
    )
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        os.fchmod(handle.fileno(), 0o600)
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary_name, EVIDENCE_PATH)


def main() -> int:
    suffix = uuid.uuid4().hex[:12]
    username = f"postgres_failover_{suffix}"
    password = "FailoverPass123"
    rejected_nickname = f"rejected-{suffix}"
    promoted_nickname = f"promoted-{suffix}"
    temporary: tempfile.TemporaryDirectory[str] | None = None
    primary: PostgresNode | None = None
    standby: PostgresNode | None = None
    redis: PersistentRedis | None = None
    proxy: CuttableTcpProxy | None = None
    api_a: ManagedServer | None = None
    api_b: ManagedServer | None = None

    try:
        binary = resolve_current_binary()
        primary_port, standby_port, redis_port, api_a_port, api_b_port = allocate_ports(5)
        temporary = tempfile.TemporaryDirectory(prefix="disk-postgres-failover-")
        temporary_root = Path(temporary.name)
        storage_root = temporary_root / "final"
        staging_root = temporary_root / "staging"

        primary = PostgresNode("primary", temporary_root / "primary", primary_port)
        standby = PostgresNode("standby", temporary_root / "standby", standby_port)
        primary.initialize_primary()
        primary.start()
        primary.create_database()
        primary.load_schema()
        standby.clone_from(primary)
        standby.start()
        wait_for(
            "PostgreSQL streaming replication",
            lambda: replication_ready(primary, standby),
        )

        primary_system_identifier = primary.system_identifier()
        primary_timeline = primary.timeline()
        require(
            standby.system_identifier() == primary_system_identifier,
            "standby does not belong to the primary PostgreSQL cluster",
        )

        redis = PersistentRedis(temporary_root / "redis", redis_port)
        redis.start()
        proxy = CuttableTcpProxy("127.0.0.1", primary_port)
        with proxy:
            api_a = start_api(
                name=API_A,
                binary=binary,
                run_directory=temporary_root / "api-a",
                database_proxy_port=proxy.port,
                redis_port=redis_port,
                api_port=api_a_port,
                storage_root=storage_root,
                staging_root=staging_root,
            )
            api_b = start_api(
                name=API_B,
                binary=binary,
                run_directory=temporary_root / "api-b",
                database_proxy_port=proxy.port,
                redis_port=redis_port,
                api_port=api_b_port,
                storage_root=storage_root,
                staging_root=staging_root,
            )
            base_urls = (api_a.base_url, api_b.base_url)
            session = register_and_login(api_a.base_url, username, password)
            profile = profile_user(api_b.base_url, session["access_token"])
            require(profile.get("username") == username, "API B read the wrong user")

            primary.execute("CHECKPOINT")
            wait_for_replay(primary, standby)
            wait_for(
                "pre-failover user on PostgreSQL standby",
                lambda: standby.scalar(
                    "SELECT EXISTS (SELECT 1 FROM users WHERE username = %s)",
                    (username,),
                    database_name=DATABASE_NAME,
                )
                is True,
            )

            api_a_pid = api_a.pid
            api_b_pid = api_b.pid
            baseline_nickname = primary.scalar(
                "SELECT nickname FROM users WHERE username = %s",
                (username,),
                database_name=DATABASE_NAME,
            )
            proxy.pause_forwarding()
            timeout_durations = [
                require_database_failure(base_url, session["access_token"])
                for base_url in base_urls
            ]
            timeout_durations.append(
                require_database_failure(
                    api_a.base_url,
                    session["access_token"],
                    method="PATCH",
                    json_body={"nickname": rejected_nickname},
                )
            )
            require(
                primary.scalar(
                    "SELECT nickname FROM users WHERE username = %s",
                    (username,),
                    database_name=DATABASE_NAME,
                )
                == baseline_nickname,
                "timed-out profile update reached the old PostgreSQL primary",
            )
            for base_url in base_urls:
                wait_for(
                    f"PostgreSQL-unready state at {base_url}",
                    lambda base_url=base_url: database_unready(base_url),
                )
                require_liveness(base_url)

            primary_pid = primary.crash()
            standby.promote()
            recovery_started_at = time.monotonic()
            proxy.switch_target("127.0.0.1", standby_port)
            for base_url in base_urls:
                wait_for(
                    f"API readiness after PostgreSQL promotion at {base_url}",
                    lambda base_url=base_url: dependency_ready(base_url),
                )
            recovery_seconds = time.monotonic() - recovery_started_at

            require(api_a.pid == api_a_pid, "API A restarted during PostgreSQL failover")
            require(api_b.pid == api_b_pid, "API B restarted during PostgreSQL failover")
            require(primary.process is None, "old PostgreSQL primary remained running")
            require(standby.pid != primary_pid, "promoted standby reused the primary PID")
            require(
                standby.system_identifier() == primary_system_identifier,
                "promoted PostgreSQL writer changed cluster identity",
            )
            require(
                standby.scalar("SELECT NOT pg_is_in_recovery()") is True,
                "promoted PostgreSQL writer remained in recovery",
            )
            require(
                standby.scalar(
                    "SELECT nickname FROM users WHERE username = %s",
                    (username,),
                    database_name=DATABASE_NAME,
                )
                == baseline_nickname,
                "timed-out profile update was replayed after promotion",
            )

            for base_url in base_urls:
                user = profile_user(base_url, session["access_token"])
                require(user.get("username") == username, "pre-failover user was lost")
            update_nickname(api_b.base_url, session["access_token"], promoted_nickname)
            promoted_profile = profile_user(api_a.base_url, session["access_token"])
            require(
                promoted_profile.get("nickname") == promoted_nickname,
                "API A did not read API B's post-promotion write",
            )
            require(
                standby.scalar(
                    "SELECT nickname FROM users WHERE username = %s",
                    (username,),
                    database_name=DATABASE_NAME,
                )
                == promoted_nickname,
                "promoted PostgreSQL writer did not persist the profile update",
            )
            standby.execute("CHECKPOINT")
            require(
                standby.timeline() > primary_timeline,
                "PostgreSQL timeline did not advance after promotion",
            )

            write_evidence(
                {
                    "api_processes_preserved": True,
                    "command_timeout_seconds_max": round(max(timeout_durations), 3),
                    "command_timeout_seconds_min": round(min(timeout_durations), 3),
                    "command_timeouts_controlled": True,
                    "endpoint_recovery_seconds": round(recovery_seconds, 3),
                    "failed_write_not_replayed": True,
                    "old_primary_fenced": True,
                    "old_primary_sigkilled": True,
                    "passed": True,
                    "physical_streaming_replication": True,
                    "post_promotion_cross_instance_write_read": True,
                    "pre_failover_state_preserved": True,
                    "postgres_nodes": 2,
                    "same_cluster_system_identifier": True,
                    "scenario": "postgres_physical_standby_promotion_stable_endpoint",
                    "schema_version": 1,
                    "separate_data_directories": True,
                    "shared_database_service_touched": False,
                    "shared_redis_service_touched": False,
                    "stable_endpoint_switched": True,
                    "standby_promoted": True,
                    "standby_read_only_before_promotion": True,
                    "timeline_advanced": True,
                }
            )

            api_b.stop()
            api_b = None
            api_a.stop()
            api_a = None

        proxy = None
        redis.stop()
        redis = None
        standby.stop()
        standby = None
        primary.stop()
        primary = None
        print(
            "PASS: stable PostgreSQL endpoint promotion preserved timeouts, "
            "reconnection, committed state, and post-promotion writes"
        )
        return 0
    except BaseException:
        for server in (api_a, api_b):
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
        for postgres in (primary, standby):
            if postgres is not None:
                print(postgres.log_tail(), file=sys.stderr)
        if redis is not None:
            print(redis.log_tail(), file=sys.stderr)
        raise
    finally:
        for server in (api_b, api_a):
            if server is not None:
                server.stop()
        if proxy is not None:
            proxy.close()
        if redis is not None:
            redis.stop()
        for postgres in (standby, primary):
            if postgres is not None:
                postgres.stop()
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())

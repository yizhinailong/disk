#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify the supported PgBouncer transaction-pooling contract."""

from __future__ import annotations

import concurrent.futures
import json
import os
import re
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path
from typing import Any, IO

import httpx
import psycopg
from psycopg.rows import dict_row

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_auth_cluster_consistency import (  # noqa: E402
    auth_headers,
    register_and_login,
    success_data,
)
from test_expand_mixed_version import (  # noqa: E402
    ManagedServer,
    allocate_ports,
    require,
    resolve_current_binary,
    server_config,
)
from test_postgres_failover_semantics import (  # noqa: E402
    DATABASE_NAME,
    DATABASE_USER,
    PostgresNode,
    wait_for,
)
from test_redis_session_persistence import PersistentRedis  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = ROOT / ".sisyphus/evidence/pgbouncer-transaction-pool-summary.json"
MINIMUM_PGBOUNCER_VERSION = (1, 25, 2)
CLIENT_CONNECTIONS_PER_API = 4
SERVER_POOL_SIZE = 2


def resolve_pgbouncer_binary() -> tuple[Path, str] | None:
    configured = os.environ.get("DISK_PGBOUNCER_BIN")
    if not configured:
        print("SKIP: DISK_PGBOUNCER_BIN is not an executable PgBouncer")
        return None

    candidate = Path(configured)
    path = candidate if candidate.is_absolute() else ROOT / candidate
    if not path.is_file() or not os.access(path, os.X_OK):
        print("SKIP: DISK_PGBOUNCER_BIN is not an executable PgBouncer")
        return None

    result = subprocess.run(
        [str(path), "-V"],
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
    )
    output = "\n".join(part for part in (result.stdout, result.stderr) if part).strip()
    require(result.returncode == 0, f"PgBouncer version probe failed: {output[-1000:]}")
    match = re.search(r"\bPgBouncer\s+(\d+)\.(\d+)\.(\d+)\b", output)
    require(match is not None, "DISK_PGBOUNCER_BIN did not report a PgBouncer version")
    version = tuple(int(part) for part in match.groups())
    require(
        version >= MINIMUM_PGBOUNCER_VERSION,
        "PgBouncer transaction-pool gate requires version 1.25.2 or later",
    )
    return path.resolve(), ".".join(match.groups())


class PgBouncerProcess:
    """Own one isolated PgBouncer process and its admin connection."""

    def __init__(
        self,
        binary: Path,
        root: Path,
        port: int,
        postgres_port: int,
    ) -> None:
        self.binary = binary
        self.root = root
        self.port = port
        self.postgres_port = postgres_port
        self.config_path = root / "pgbouncer.ini"
        self.userlist_path = root / "userlist.txt"
        self.log_path = root / "pgbouncer.log"
        self.process: subprocess.Popen[bytes] | None = None
        self.log_handle: IO[bytes] | None = None

    def start(self) -> None:
        require(self.process is None, "PgBouncer is already running")
        self.root.mkdir(parents=True, exist_ok=False)
        self.userlist_path.write_text(f'"{DATABASE_USER}" ""\n', encoding="utf-8")
        self.config_path.write_text(
            "\n".join(
                (
                    "[databases]",
                    f"{DATABASE_NAME} = host=127.0.0.1 port={self.postgres_port} "
                    f"dbname={DATABASE_NAME} user={DATABASE_USER}",
                    "",
                    "[pgbouncer]",
                    "listen_addr = 127.0.0.1",
                    f"listen_port = {self.port}",
                    f"unix_socket_dir = {self.root}",
                    "auth_type = trust",
                    f"auth_file = {self.userlist_path}",
                    f"admin_users = {DATABASE_USER}",
                    "pool_mode = transaction",
                    f"default_pool_size = {SERVER_POOL_SIZE}",
                    f"min_pool_size = {SERVER_POOL_SIZE}",
                    f"max_db_connections = {SERVER_POOL_SIZE}",
                    "max_client_conn = 32",
                    "max_prepared_statements = 200",
                    "server_reset_query_always = 0",
                    "server_connect_timeout = 2",
                    "query_timeout = 10",
                    "log_connections = 0",
                    "log_disconnections = 0",
                    "log_pooler_errors = 1",
                    "",
                )
            ),
            encoding="utf-8",
        )
        self.log_handle = self.log_path.open("ab")
        try:
            self.process = subprocess.Popen(
                [str(self.binary), str(self.config_path)],
                cwd=self.root,
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
            )
            wait_for("PgBouncer readiness", self.is_ready, timeout_seconds=20)
        except BaseException:
            self.stop()
            raise

    def connect(
        self,
        database_name: str = DATABASE_NAME,
        *,
        autocommit: bool = True,
    ) -> psycopg.Connection[dict[str, Any]]:
        return psycopg.connect(
            host="127.0.0.1",
            port=self.port,
            dbname=database_name,
            user=DATABASE_USER,
            connect_timeout=2,
            autocommit=autocommit,
            row_factory=dict_row,
        )

    def is_ready(self) -> bool:
        if self.process is not None and self.process.poll() is not None:
            raise AssertionError(
                f"PgBouncer exited with {self.process.returncode}\n{self.log_tail()}"
            )
        with self.connect() as connection:
            row = connection.execute("SELECT 1 AS ready").fetchone()
        return row is not None and row["ready"] == 1

    def admin_rows(self, query: str) -> list[dict[str, Any]]:
        with self.connect("pgbouncer") as connection:
            return list(connection.execute(query).fetchall())

    def stop(self) -> None:
        process = self.process
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        self.process = None
        if self.log_handle is not None:
            self.log_handle.close()
            self.log_handle = None

    def log_tail(self) -> str:
        if self.log_handle is not None:
            self.log_handle.flush()
        if not self.log_path.is_file():
            return "PgBouncer log unavailable"
        lines = self.log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        return "PgBouncer log tail:\n" + "\n".join(lines[-80:])


def api_config(
    api_port: int,
    instance_id: str,
    storage_root: Path,
    staging_root: Path,
    pgbouncer_port: int,
    redis_port: int,
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
            "port": pgbouncer_port,
            "dbname": DATABASE_NAME,
            "user": DATABASE_USER,
            "passwd": "",
            "connection_number": CLIENT_CONNECTIONS_PER_API,
        }
    )
    database.pop("num_connection_number", None)
    redis = config["redis_clients"][0]
    redis.update(
        {
            "host": "127.0.0.1",
            "port": redis_port,
            "db": 0,
            "passwd": "",
        }
    )
    return config


def start_api(
    *,
    name: str,
    binary: Path,
    run_directory: Path,
    api_port: int,
    pgbouncer_port: int,
    redis_port: int,
    storage_root: Path,
    staging_root: Path,
) -> ManagedServer:
    return ManagedServer(
        name=name,
        binary=binary,
        run_directory=run_directory,
        config=api_config(
            api_port,
            name,
            storage_root,
            staging_root,
            pgbouncer_port,
            redis_port,
        ),
        database_name=DATABASE_NAME,
        port=api_port,
        readiness_path="/api/health/ready",
        role="api",
        environment_overrides={
            "DATABASE_HOST": "127.0.0.1",
            "DATABASE_PORT": str(pgbouncer_port),
            "DATABASE_NAME": DATABASE_NAME,
            "DATABASE_USER": DATABASE_USER,
            "DATABASE_POOL_SIZE": str(CLIENT_CONNECTIONS_PER_API),
            "REDIS_HOST": "127.0.0.1",
            "REDIS_PORT": str(redis_port),
            "REDIS_DB": "0",
        },
        environment_removals=("DATABASE_PASSWORD", "REDIS_PASSWORD"),
    )


def require_profile(base_url: str, access_token: str, username: str) -> None:
    response = httpx.get(
        base_url + "/api/user/profile",
        headers=auth_headers(access_token),
        timeout=10,
    )
    data = success_data(response, f"profile at {base_url}")
    user = data.get("user")
    require(
        isinstance(user, dict) and user.get("username") == username,
        f"profile at {base_url} returned the wrong user",
    )


def create_folder(base_url: str, access_token: str, name: str, parent_id: int) -> int:
    response = httpx.post(
        base_url + "/api/folder/create",
        headers=auth_headers(access_token),
        json={"name": name, "parent_id": parent_id},
        timeout=10,
    )
    data = success_data(response, f"create folder {name}")
    folder_id = data.get("id")
    require(isinstance(folder_id, int) and folder_id > 0, "create folder returned no ID")
    return folder_id


def rename_folder(
    base_url: str,
    access_token: str,
    folder_id: int,
    new_name: str,
) -> None:
    response = httpx.put(
        f"{base_url}/api/folder/{folder_id}/rename",
        headers=auth_headers(access_token),
        json={"new_name": new_name},
        timeout=10,
    )
    data = success_data(response, "rename folder transaction")
    require(data.get("name") == new_name, "rename transaction returned the wrong name")


def require_breadcrumb(
    base_url: str,
    access_token: str,
    folder_id: int,
    expected_names: tuple[str, ...],
) -> None:
    response = httpx.get(
        f"{base_url}/api/folder/{folder_id}/breadcrumb",
        headers=auth_headers(access_token),
        timeout=10,
    )
    data = success_data(response, "cross-instance breadcrumb")
    path = data.get("path")
    require(isinstance(path, list), "breadcrumb returned no path")
    names = tuple(item.get("name") for item in path if isinstance(item, dict))
    require(names[-len(expected_names) :] == expected_names, "breadcrumb did not reflect rename")


def drive_concurrent_profiles(
    base_urls: tuple[str, str],
    access_token: str,
    username: str,
) -> None:
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        futures = [
            executor.submit(
                require_profile,
                base_urls[index % len(base_urls)],
                access_token,
                username,
            )
            for index in range(32)
        ]
        for future in futures:
            future.result(timeout=20)


def require_pool_topology(pgbouncer: PgBouncerProcess) -> tuple[int, int]:
    def counts() -> tuple[int, int]:
        clients = [
            row
            for row in pgbouncer.admin_rows("SHOW CLIENTS")
            if row.get("database") == DATABASE_NAME
        ]
        servers = [
            row
            for row in pgbouncer.admin_rows("SHOW SERVERS")
            if row.get("database") == DATABASE_NAME
        ]
        return len(clients), len(servers)

    wait_for(
        "Drogon clients multiplexed through PgBouncer",
        lambda: counts()
        == (2 * CLIENT_CONNECTIONS_PER_API, SERVER_POOL_SIZE),
        timeout_seconds=20,
    )
    client_count, server_count = counts()
    require(client_count > server_count, "PgBouncer did not multiplex client connections")

    pools = [
        row
        for row in pgbouncer.admin_rows("SHOW POOLS")
        if row.get("database") == DATABASE_NAME
    ]
    require(len(pools) == 1, "PgBouncer did not expose exactly one application pool")
    require(pools[0].get("pool_mode") == "transaction", "PgBouncer pool mode drifted")
    return client_count, server_count


def require_prepared_statement_stats(pgbouncer: PgBouncerProcess) -> dict[str, int]:
    rows = [
        row
        for row in pgbouncer.admin_rows("SHOW STATS")
        if row.get("database") == DATABASE_NAME
    ]
    require(len(rows) == 1, "PgBouncer did not expose application database stats")
    stats = {
        name: int(rows[0][name])
        for name in (
            "total_client_parse_count",
            "total_server_parse_count",
            "total_bind_count",
        )
    }
    require(
        all(value > 0 for value in stats.values()),
        f"Drogon protocol-level prepared statement counters did not advance: {stats}",
    )
    return stats


def require_pool_config(pgbouncer: PgBouncerProcess) -> None:
    settings = {
        str(row["key"]): str(row["value"])
        for row in pgbouncer.admin_rows("SHOW CONFIG")
    }
    require(settings.get("pool_mode") == "transaction", "pool_mode is not transaction")
    require(
        settings.get("max_prepared_statements") == "200",
        "max_prepared_statements is not 200",
    )
    require(
        settings.get("server_reset_query_always") in ("0", "no"),
        "server_reset_query_always must remain disabled",
    )


def require_transaction_features(pgbouncer: PgBouncerProcess) -> None:
    lock_key = 0x4449534B
    with pgbouncer.connect(autocommit=False) as holder, pgbouncer.connect() as contender:
        holder.execute("SELECT pg_advisory_xact_lock(%s)", (lock_key,))
        blocked = contender.execute(
            "SELECT pg_try_advisory_xact_lock(%s) AS acquired",
            (lock_key,),
        ).fetchone()
        require(blocked is not None and blocked["acquired"] is False, "transaction lock did not exclude contender")
        holder.commit()
        acquired = contender.execute(
            "SELECT pg_try_advisory_xact_lock(%s) AS acquired",
            (lock_key,),
        ).fetchone()
        require(acquired is not None and acquired["acquired"] is True, "transaction lock was not released by commit")

    with pgbouncer.connect(autocommit=False) as connection:
        connection.execute("SET LOCAL statement_timeout = '5s'")
        connection.execute(
            "CREATE TEMP TABLE disk_pool_probe (id INTEGER) ON COMMIT DROP"
        )
        connection.execute("INSERT INTO disk_pool_probe (id) VALUES (1)")
        row = connection.execute("SELECT COUNT(*) AS count FROM disk_pool_probe").fetchone()
        require(row is not None and row["count"] == 1, "transaction-local temp table failed")
        connection.commit()
        dropped = connection.execute(
            "SELECT to_regclass('pg_temp.disk_pool_probe') AS relation"
        ).fetchone()
        require(
            dropped is not None and dropped["relation"] is None,
            "ON COMMIT DROP temp table survived its transaction",
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
    resolved = resolve_pgbouncer_binary()
    if resolved is None:
        return 0
    pgbouncer_binary, pgbouncer_version = resolved

    temporary: tempfile.TemporaryDirectory[str] | None = None
    postgres: PostgresNode | None = None
    redis: PersistentRedis | None = None
    pgbouncer: PgBouncerProcess | None = None
    api_a: ManagedServer | None = None
    api_b: ManagedServer | None = None

    try:
        server_binary = resolve_current_binary()
        postgres_port, redis_port, pgbouncer_port, api_a_port, api_b_port = allocate_ports(5)
        temporary = tempfile.TemporaryDirectory(prefix="disk-pgbouncer-transaction-")
        root = Path(temporary.name)
        storage_root = root / "final"
        staging_root = root / "staging"

        postgres = PostgresNode("pgbouncer-postgres", root / "postgres", postgres_port)
        postgres.initialize_primary()
        postgres.start()
        postgres.create_database()
        postgres.load_schema()

        redis = PersistentRedis(root / "redis", redis_port)
        redis.start()

        pgbouncer = PgBouncerProcess(
            pgbouncer_binary,
            root / "pgbouncer",
            pgbouncer_port,
            postgres_port,
        )
        pgbouncer.start()
        require_pool_config(pgbouncer)

        api_a = start_api(
            name="pgbouncer-api-a",
            binary=server_binary,
            run_directory=root / "api-a",
            api_port=api_a_port,
            pgbouncer_port=pgbouncer_port,
            redis_port=redis_port,
            storage_root=storage_root,
            staging_root=staging_root,
        )
        api_b = start_api(
            name="pgbouncer-api-b",
            binary=server_binary,
            run_directory=root / "api-b",
            api_port=api_b_port,
            pgbouncer_port=pgbouncer_port,
            redis_port=redis_port,
            storage_root=storage_root,
            staging_root=staging_root,
        )

        suffix = uuid.uuid4().hex[:12]
        username = f"pool_user_{suffix}"
        session = register_and_login(api_a.base_url, username, "PoolPass123")
        access_token = session["access_token"]
        require_profile(api_b.base_url, access_token, username)

        parent_name = f"pool-parent-{suffix}"
        renamed_parent = f"pool-renamed-{suffix}"
        child_name = f"pool-child-{suffix}"
        parent_id = create_folder(api_a.base_url, access_token, parent_name, 0)
        child_id = create_folder(api_b.base_url, access_token, child_name, parent_id)
        rename_folder(api_b.base_url, access_token, parent_id, renamed_parent)
        require_breadcrumb(
            api_a.base_url,
            access_token,
            child_id,
            (renamed_parent, child_name),
        )
        drive_concurrent_profiles(
            (api_a.base_url, api_b.base_url),
            access_token,
            username,
        )

        client_count, server_count = require_pool_topology(pgbouncer)
        prepared_stats = require_prepared_statement_stats(pgbouncer)

        api_b.stop()
        api_b = None
        api_a.stop()
        api_a = None
        wait_for(
            "Drogon clients to disconnect from PgBouncer",
            lambda: not [
                row
                for row in pgbouncer.admin_rows("SHOW CLIENTS")
                if row.get("database") == DATABASE_NAME
            ],
            timeout_seconds=20,
        )
        require_transaction_features(pgbouncer)

        write_evidence(
            {
                "advisory_xact_lock_excluded_then_released": True,
                "api_processes": 2,
                "client_connections": client_count,
                "client_connections_per_api": CLIENT_CONNECTIONS_PER_API,
                "cross_instance_orm_workflow": True,
                "drogon_transaction_runner_workflow": True,
                "max_prepared_statements": 200,
                "passed": True,
                "pgbouncer_version": pgbouncer_version,
                "pool_mode": "transaction",
                "prepared_statement_stats": prepared_stats,
                "scenario": "pgbouncer_transaction_pool_compatibility",
                "schema_version": 1,
                "server_connections": server_count,
                "server_pool_size": SERVER_POOL_SIZE,
                "shared_database_service_touched": False,
                "shared_redis_service_touched": False,
                "transaction_local_temp_table": True,
            }
        )

        pgbouncer.stop()
        pgbouncer = None
        redis.stop()
        redis = None
        postgres.stop()
        postgres = None
        print(
            "PASS: PgBouncer transaction pooling preserved Drogon ORM, "
            "prepared statements, transactions, and advisory locks"
        )
        return 0
    except BaseException:
        for server in (api_a, api_b):
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
        if pgbouncer is not None:
            print(pgbouncer.log_tail(), file=sys.stderr)
        if redis is not None:
            print(redis.log_tail(), file=sys.stderr)
        if postgres is not None:
            print(postgres.log_tail(), file=sys.stderr)
        raise
    finally:
        for server in (api_b, api_a):
            if server is not None:
                server.stop()
        if pgbouncer is not None:
            pgbouncer.stop()
        if redis is not None:
            redis.stop()
        if postgres is not None:
            postgres.stop()
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())

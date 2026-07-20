#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Exercise the supported V002/current binary coexistence window during expand."""

from __future__ import annotations

import hashlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any, IO

import httpx
import psycopg
from psycopg import sql
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config


REPO_ROOT = Path(__file__).resolve().parents[2]
INIT_SQL = REPO_ROOT / "sql" / "init.sql"
MIGRATOR = REPO_ROOT / "scripts" / "migrate-db.sh"
MIGRATION_DIR = REPO_ROOT / "sql" / "migrations"
ROLLBACKS = (
    MIGRATION_DIR / "V004_storage_reconciliation_rollback.sql",
    MIGRATION_DIR / "V003_distributed_upload_rollback.sql",
)
LEGACY_COMMIT = "6ed0afefb9ae6614ac5cc735b237dead28d10ecb"
JWT_SECRET = "expand-mixed-version-jwt-secret-2026-07-20"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_checked(
    command: list[str],
    *,
    cwd: Path = REPO_ROOT,
    environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def resolve_current_binary() -> Path:
    configured = os.environ.get("SERVER_BIN")
    candidates = [
        Path(configured) if configured else REPO_ROOT / "build/linux-debug-clang/src/disk",
        REPO_ROOT / "build/linux-debug-clang/disk",
    ]
    for candidate in candidates:
        path = candidate if candidate.is_absolute() else REPO_ROOT / candidate
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
    raise AssertionError(
        "current server binary is missing; build it first or set SERVER_BIN"
    )


def parse_cmake_cache(cache_path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in cache_path.read_text(encoding="utf-8").splitlines():
        if not raw_line or raw_line.startswith(("#", "//")) or "=" not in raw_line:
            continue
        key_and_type, value = raw_line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        values[key] = value
    return values


def current_build_directory(current_binary: Path) -> Path:
    configured = os.environ.get("DISK_CURRENT_BUILD_DIR")
    candidates = [Path(configured)] if configured else []
    candidates.extend((current_binary.parent.parent, current_binary.parent))
    for candidate in candidates:
        path = candidate if candidate.is_absolute() else REPO_ROOT / candidate
        if (path / "CMakeCache.txt").is_file():
            return path.resolve()
    raise AssertionError(
        "cannot locate the current CMakeCache.txt; set DISK_CURRENT_BUILD_DIR"
    )


def extract_legacy_source(source_directory: Path) -> None:
    source_directory.mkdir(parents=True, exist_ok=False)
    archive = subprocess.run(
        ["git", "archive", "--format=tar", LEGACY_COMMIT],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
    )
    if archive.returncode != 0:
        error = archive.stderr.decode("utf-8", errors="replace")
        raise AssertionError(f"failed to archive legacy commit: {error}")

    extracted = subprocess.run(
        ["tar", "-xf", "-", "-C", str(source_directory)],
        input=archive.stdout,
        check=False,
        capture_output=True,
    )
    if extracted.returncode != 0:
        error = extracted.stderr.decode("utf-8", errors="replace")
        raise AssertionError(f"failed to extract legacy source: {error}")


def build_legacy_binary(current_binary: Path) -> Path:
    configured_binary = os.environ.get("DISK_LEGACY_V002_BINARY")
    if configured_binary:
        candidate = Path(configured_binary)
        path = candidate if candidate.is_absolute() else REPO_ROOT / candidate
        require(
            path.is_file() and os.access(path, os.X_OK),
            f"DISK_LEGACY_V002_BINARY is not executable: {path}",
        )
        return path.resolve()

    commit_check = subprocess.run(
        ["git", "cat-file", "-e", f"{LEGACY_COMMIT}^{{commit}}"],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
    )
    require(
        commit_check.returncode == 0,
        "legacy commit is unavailable; fetch repository history or set "
        "DISK_LEGACY_V002_BINARY",
    )

    configured_root = os.environ.get("DISK_LEGACY_BUILD_ROOT")
    cache_root = (
        Path(configured_root)
        if configured_root
        else REPO_ROOT / "build/compat" / LEGACY_COMMIT
    )
    if not cache_root.is_absolute():
        cache_root = REPO_ROOT / cache_root
    source_directory = cache_root / "source"
    build_directory = cache_root / "build"
    stamp_path = cache_root / "source-commit.txt"
    legacy_binary = build_directory / "src/disk"

    if legacy_binary.is_file() and os.access(legacy_binary, os.X_OK):
        require(
            stamp_path.is_file()
            and stamp_path.read_text(encoding="utf-8").strip() == LEGACY_COMMIT,
            f"legacy build cache has no matching commit stamp: {cache_root}",
        )
        return legacy_binary.resolve()

    cache_root.mkdir(parents=True, exist_ok=True)
    if not source_directory.exists():
        print(f"Extracting legacy source {LEGACY_COMMIT[:12]}...")
        extract_legacy_source(source_directory)
        stamp_path.write_text(f"{LEGACY_COMMIT}\n", encoding="utf-8")
    else:
        require(
            stamp_path.is_file()
            and stamp_path.read_text(encoding="utf-8").strip() == LEGACY_COMMIT
            and (source_directory / "CMakeLists.txt").is_file(),
            f"legacy source cache is incomplete or belongs to another commit: {cache_root}",
        )

    current_build = current_build_directory(current_binary)
    cache = parse_cmake_cache(current_build / "CMakeCache.txt")
    toolchain = cache.get("CMAKE_TOOLCHAIN_FILE", "")
    installed = cache.get("VCPKG_INSTALLED_DIR", "")
    require(toolchain and Path(toolchain).is_file(), "current vcpkg toolchain is unavailable")
    require(installed and Path(installed).is_dir(), "current vcpkg dependency tree is unavailable")

    configure = [
        "cmake",
        "-S",
        str(source_directory),
        "-B",
        str(build_directory),
        "-G",
        cache.get("CMAKE_GENERATOR", "Ninja"),
        f"-DCMAKE_BUILD_TYPE={cache.get('CMAKE_BUILD_TYPE', 'Debug')}",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DVCPKG_INSTALLED_DIR={installed}",
        "-DVCPKG_MANIFEST_INSTALL=OFF",
    ]
    for name in (
        "CMAKE_C_COMPILER",
        "CMAKE_CXX_COMPILER",
        "CMAKE_C_COMPILER_LAUNCHER",
        "CMAKE_CXX_COMPILER_LAUNCHER",
        "VCPKG_TARGET_TRIPLET",
    ):
        if cache.get(name):
            configure.append(f"-D{name}={cache[name]}")

    print(f"Configuring legacy binary {LEGACY_COMMIT[:12]}...")
    run_checked(configure)
    parallelism = max(1, min(8, os.cpu_count() or 1))
    print(f"Building legacy binary with {parallelism} jobs...")
    run_checked(
        [
            "cmake",
            "--build",
            str(build_directory),
            "--target",
            "disk",
            "--parallel",
            str(parallelism),
        ]
    )
    require(
        legacy_binary.is_file() and os.access(legacy_binary, os.X_OK),
        f"legacy build did not produce an executable: {legacy_binary}",
    )
    return legacy_binary.resolve()


def admin_database_config() -> dict[str, Any]:
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


def create_database(database_name: str) -> None:
    with psycopg.connect(**admin_database_config(), autocommit=True) as connection:
        connection.execute(sql.SQL("CREATE DATABASE {}").format(sql.Identifier(database_name)))


def drop_database(database_name: str) -> None:
    with psycopg.connect(**admin_database_config(), autocommit=True) as connection:
        connection.execute(
            "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
            "WHERE datname = %s AND pid <> pg_backend_pid()",
            (database_name,),
        )
        connection.execute(
            sql.SQL("DROP DATABASE IF EXISTS {}").format(sql.Identifier(database_name))
        )


def connect(database_name: str) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, autocommit=True, row_factory=dict_row)


def run_database_command(command: list[str], database_name: str) -> None:
    run_checked(command, environment=database_environment(database_name))


def run_sql_file(database_name: str, path: Path) -> None:
    run_database_command(
        ["psql", "-X", "-v", "ON_ERROR_STOP=1", "-f", str(path)],
        database_name,
    )


def prepare_v002_database(database_name: str) -> None:
    run_sql_file(database_name, INIT_SQL)
    for rollback in ROLLBACKS:
        run_sql_file(database_name, rollback)

    with connect(database_name) as connection:
        migration_count = connection.execute(
            "SELECT COUNT(*) AS count FROM schema_migrations"
        ).fetchone()
        require(migration_count is not None and migration_count["count"] == 0, "V002 ledger is not empty")
        distributed_column = connection.execute(
            "SELECT COUNT(*) AS count FROM information_schema.columns "
            "WHERE table_schema = 'public' AND table_name = 'upload_tasks' "
            "AND column_name = 'staging_backend'"
        ).fetchone()
        require(
            distributed_column is not None and distributed_column["count"] == 0,
            "V002 baseline still contains distributed upload columns",
        )


def recycle_database_connections(database_name: str) -> int:
    with psycopg.connect(
        **admin_database_config(), autocommit=True, row_factory=dict_row
    ) as connection:
        rows = connection.execute(
            "SELECT pid, pg_terminate_backend(pid) AS terminated "
            "FROM pg_stat_activity WHERE datname = %s "
            "AND backend_type = 'client backend'",
            (database_name,),
        ).fetchall()
    require(rows, "no legacy PostgreSQL connections were available to recycle")
    require(all(row["terminated"] for row in rows), "failed to recycle a legacy DB connection")
    return len(rows)


def redis_config() -> dict[str, Any]:
    root = json.loads((REPO_ROOT / "config.json").read_text(encoding="utf-8"))
    clients = root.get("redis_clients", [])
    configured = clients[0] if clients else {}
    return {
        "host": os.environ.get("REDIS_HOST") or configured.get("host", "127.0.0.1"),
        "port": int(os.environ.get("REDIS_PORT") or configured.get("port", 6379)),
        "db": int(os.environ.get("REDIS_DB") or configured.get("db", 0)),
        "password": os.environ.get("REDIS_PASSWORD") or configured.get("passwd", ""),
    }


def allocate_ports(count: int) -> list[int]:
    ports: set[int] = set()
    while len(ports) < count:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            ports.add(int(listener.getsockname()[1]))
    return list(ports)


def server_config(
    database_name: str,
    port: int,
    instance_id: str,
    storage_root: Path,
    staging_root: Path,
    *,
    role: str = "api",
) -> dict[str, Any]:
    database = database_config()
    redis = redis_config()
    return {
        "listeners": [{"address": "127.0.0.1", "port": port}],
        "app": {
            "upload_path": str(storage_root),
            "threads_num": 2,
            "client_max_body_size": "20M",
        },
        "custom_config": {
            "disk": {
                "process_role": role,
                "instance_id": instance_id,
                "storage_backend": "local",
                "upload_staging_backend": "local",
                "storage_base_path": str(storage_root),
                "temp_upload_path": str(staging_root),
                "chunk_size": 1048576,
                "max_file_size": 16777216,
                "upload_task_expiry_seconds": 3600,
                "upload_finalize_lease_seconds": 30,
                "worker_poll_interval_ms": 1000,
                "worker_claim_batch_size": 4,
                "worker_concurrency": 1,
                "worker_lease_duration_seconds": 30,
                "worker_drain_timeout_seconds": 5,
                "upload_rate_limit_per_minute": 10000,
                "upload_rate_limit_window_seconds": 60,
                "download_rate_limit_per_minute": 10000,
                "download_rate_limit_window_seconds": 60,
                "folder_rate_limit_per_minute": 10000,
                "folder_rate_limit_window_seconds": 60,
                "admin_rate_limit_per_minute": 10000,
                "admin_rate_limit_window_seconds": 60,
                "share_access_rate_limit_per_minute": 10000,
                "share_access_rate_limit_window_seconds": 60,
                "share_browse_rate_limit_per_minute": 10000,
                "share_browse_rate_limit_window_seconds": 60,
                "share_download_rate_limit_per_minute": 10000,
                "share_download_rate_limit_window_seconds": 60,
                "register_rate_limit_per_window": 10000,
                "register_rate_limit_window_seconds": 60,
                "assembly_max_concurrent": 2,
                "assemble_buffer_size_bytes": 65536,
                "auth_cpu_pool_metrics_interval_seconds": 60,
                "s3": {
                    "bucket": "disk",
                    "region": "us-east-1",
                    "endpoint": "",
                    "use_ssl": True,
                    "force_path_style": False,
                    "verify_ssl": True,
                    "object_prefix": "objects",
                    "staging_prefix": "staging",
                    "max_connections": 4,
                    "io_threads": 1,
                    "connect_timeout_ms": 1000,
                    "request_timeout_ms": 10000,
                    "max_retries": 0,
                    "retry_base_delay_ms": 10,
                },
            }
        },
        "db_clients": [
            {
                "name": "default",
                "rdbms": "postgresql",
                "host": str(database["host"]),
                "port": int(database["port"]),
                "dbname": database_name,
                "user": str(database["user"]),
                "passwd": str(database["password"]),
                "connection_number": 6,
                "num_connection_number": 6,
            }
        ],
        "redis_clients": [
            {
                "name": "default",
                "host": str(redis["host"]),
                "port": int(redis["port"]),
                "db": int(redis["db"]),
                "passwd": str(redis["password"]),
                "is_fast": False,
                "number_of_connections": 4,
                "timeout": 5.0,
            }
        ],
        "plugins": [
            {
                "name": "drogon::plugin::GlobalFilters",
                "config": {
                    "filters": [
                        "disk::filters::RequestTraceFilter",
                        "disk::filters::JwtAuthFilter",
                        "disk::filters::RegisterRateLimitFilter",
                    ],
                    "exempt": [],
                },
            }
        ],
    }


def server_environment(
    config_path: Path,
    database_name: str,
    port: int,
    instance_id: str,
    *,
    role: str = "api",
) -> dict[str, str]:
    database = database_config()
    redis = redis_config()
    environment = os.environ.copy()
    environment.update(
        {
            "JWT_SECRET": JWT_SECRET,
            "DISK_SECURE_MODE": "false",
            "DISK_CONFIG_FILE": str(config_path),
            "DISK_LISTEN_ADDRESS": "127.0.0.1",
            "DISK_LISTEN_PORT": str(port),
            "DISK_PROCESS_ROLE": role,
            "DISK_INSTANCE_ID": instance_id,
            "DISK_STORAGE_BACKEND": "local",
            "DISK_UPLOAD_STAGING_BACKEND": "local",
            "DATABASE_HOST": str(database["host"]),
            "DATABASE_PORT": str(database["port"]),
            "DATABASE_NAME": database_name,
            "DATABASE_USER": str(database["user"]),
            "DATABASE_POOL_SIZE": "6",
            "REDIS_HOST": str(redis["host"]),
            "REDIS_PORT": str(redis["port"]),
            "REDIS_DB": str(redis["db"]),
            "REDIS_POOL_SIZE": "4",
        }
    )
    if database["password"]:
        environment["DATABASE_PASSWORD"] = str(database["password"])
    else:
        environment.pop("DATABASE_PASSWORD", None)
    if redis["password"]:
        environment["REDIS_PASSWORD"] = str(redis["password"])
    else:
        environment.pop("REDIS_PASSWORD", None)
    return environment


class ManagedServer:
    def __init__(
        self,
        *,
        name: str,
        binary: Path,
        run_directory: Path,
        config: dict[str, Any],
        database_name: str,
        port: int,
        readiness_path: str,
        role: str = "api",
    ) -> None:
        self.name = name
        self.base_url = f"http://127.0.0.1:{port}"
        self.run_directory = run_directory
        self.log_path = run_directory / f"{name}.log"
        self.config_path = run_directory / "config.json"
        self.process: subprocess.Popen[bytes] | None = None
        self.log_handle: IO[bytes] | None = None

        run_directory.mkdir(parents=True, exist_ok=False)
        self.config_path.write_text(
            json.dumps(config, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        self.log_handle = self.log_path.open("wb")
        try:
            self.process = subprocess.Popen(
                [str(binary)],
                cwd=run_directory,
                env=server_environment(
                    self.config_path,
                    database_name,
                    port,
                    name,
                    role=role,
                ),
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
            )
            self.wait_until_ready(readiness_path)
        except BaseException:
            self.stop()
            raise

    @property
    def pid(self) -> int:
        require(self.process is not None, f"{self.name} process was not started")
        return self.process.pid

    def wait_until_ready(self, readiness_path: str) -> None:
        deadline = time.monotonic() + 45
        last_error = "no response"
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                raise AssertionError(
                    f"{self.name} exited before readiness with {self.process.returncode}\n"
                    f"{self.log_tail()}"
                )
            try:
                response = httpx.get(
                    self.base_url + readiness_path,
                    timeout=1.0,
                )
                if response.status_code == 200:
                    return
                last_error = f"HTTP {response.status_code}: {response.text[:300]}"
            except httpx.HTTPError as error:
                last_error = str(error)
            time.sleep(0.2)
        raise AssertionError(
            f"{self.name} did not become ready: {last_error}\n{self.log_tail()}"
        )

    def require_running(self, label: str) -> None:
        require(
            self.process is not None and self.process.poll() is None,
            f"{self.name} exited during {label}\n{self.log_tail()}",
        )

    def log_tail(self, lines: int = 80) -> str:
        if self.log_handle is not None:
            self.log_handle.flush()
        if not self.log_path.is_file():
            return f"{self.name}: log unavailable"
        content = self.log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        return f"{self.name} log tail:\n" + "\n".join(content[-lines:])

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process = None
        if self.log_handle is not None:
            self.log_handle.close()
            self.log_handle = None


def success_data(response: httpx.Response, label: str) -> dict[str, Any]:
    try:
        body = response.json()
    except ValueError as error:
        raise AssertionError(
            f"{label} returned non-JSON HTTP {response.status_code}: {response.text[:500]}"
        ) from error
    require(
        response.status_code in (200, 201) and body.get("code") in (0, "0"),
        f"{label} failed: HTTP {response.status_code}, body={body}",
    )
    data = body.get("data")
    require(isinstance(data, dict), f"{label} returned invalid data: {body}")
    return data


def register_user(base_url: str, username: str, password: str) -> str:
    response = httpx.post(
        base_url + "/api/auth/register",
        json={
            "username": username,
            "email": f"{username}@example.test",
            "password": password,
        },
        timeout=20,
    )
    success_data(response, "legacy register")
    return login(base_url, username, password)


def login(base_url: str, username: str, password: str) -> str:
    response = httpx.post(
        base_url + "/api/auth/login",
        json={"account": username, "password": password},
        timeout=20,
    )
    data = success_data(response, f"login at {base_url}")
    token = data.get("access_token")
    require(isinstance(token, str) and token, "login did not return an access token")
    return token


def auth_headers(token: str, content_type: str = "application/json") -> dict[str, str]:
    return {
        "Authorization": f"Bearer {token}",
        "Content-Type": content_type,
    }


def require_profile(base_url: str, token: str, username: str, label: str) -> None:
    response = httpx.get(
        base_url + "/api/user/profile",
        headers=auth_headers(token),
        timeout=10,
    )
    data = success_data(response, label)
    user = data.get("user")
    require(
        isinstance(user, dict) and user.get("username") == username,
        f"{label} returned the wrong user",
    )


def wait_for_profile(base_url: str, token: str, username: str, label: str) -> None:
    deadline = time.monotonic() + 30
    last_error = "profile probe was not attempted"
    while time.monotonic() < deadline:
        try:
            require_profile(base_url, token, username, label)
            return
        except (AssertionError, httpx.HTTPError) as error:
            last_error = str(error)
            time.sleep(0.2)
    raise AssertionError(f"{label} did not recover after connection recycle: {last_error}")


def init_upload(base_url: str, token: str, filename: str, payload: bytes) -> str:
    response = httpx.post(
        base_url + "/api/file/upload/init",
        headers=auth_headers(token),
        json={
            "filename": filename,
            "file_size": len(payload),
            "file_hash": hashlib.md5(payload).hexdigest(),
            "parent_id": 0,
        },
        timeout=15,
    )
    data = success_data(response, f"init {filename}")
    require(data.get("instant_upload") is False, f"{filename} unexpectedly deduplicated")
    upload_id = data.get("upload_id")
    require(isinstance(upload_id, str) and upload_id, f"{filename} has no upload_id")
    return upload_id


def upload_chunk(base_url: str, token: str, upload_id: str, payload: bytes) -> None:
    response = httpx.post(
        base_url + "/api/file/upload/chunk",
        params={
            "upload_id": upload_id,
            "chunk_index": 0,
            "chunk_hash": hashlib.md5(payload).hexdigest(),
        },
        headers=auth_headers(token, "application/octet-stream"),
        content=payload,
        timeout=15,
    )
    data = success_data(response, f"chunk {upload_id}")
    require(data.get("uploaded") is True, f"chunk {upload_id} was not accepted")


def complete_upload(base_url: str, token: str, upload_id: str) -> int:
    response = httpx.post(
        base_url + "/api/file/upload/complete",
        headers=auth_headers(token),
        json={"upload_id": upload_id},
        timeout=30,
    )
    data = success_data(response, f"complete {upload_id}")
    file = data.get("file")
    require(isinstance(file, dict), f"complete {upload_id} returned no file")
    file_id = file.get("id")
    require(isinstance(file_id, int) and file_id > 0, f"complete {upload_id} returned no file id")
    return file_id


def download_file(base_url: str, token: str, file_id: int, payload: bytes, label: str) -> None:
    response = httpx.get(
        f"{base_url}/api/file/download/{file_id}",
        headers={"Authorization": f"Bearer {token}"},
        timeout=15,
    )
    require(response.status_code == 200, f"{label} download returned HTTP {response.status_code}")
    require(response.content == payload, f"{label} download content mismatch")


def upload_without_complete(
    base_url: str,
    token: str,
    filename: str,
    payload: bytes,
) -> str:
    upload_id = init_upload(base_url, token, filename, payload)
    upload_chunk(base_url, token, upload_id, payload)
    return upload_id


def assert_legacy_chunk(
    connection: psycopg.Connection[dict[str, Any]],
    upload_id: str,
    staging_root: Path,
) -> None:
    row = connection.execute(
        "SELECT chunk_index, size_bytes, hash_md5, object_key, etag "
        "FROM upload_task_chunks WHERE task_id = %s",
        (upload_id,),
    ).fetchone()
    require(row is not None and row["chunk_index"] == 0, "legacy chunk row is missing")
    require(
        all(row[field] is None for field in ("size_bytes", "hash_md5", "object_key", "etag")),
        "legacy chunk unexpectedly contains distributed object metadata",
    )
    require(
        (staging_root / upload_id / "0.chunk").is_file(),
        f"legacy chunk path is missing for {upload_id}",
    )


def verify_database_state(
    database_name: str,
    username: str,
    cases: dict[str, dict[str, Any]],
    storage_root: Path,
) -> None:
    with connect(database_name) as connection:
        user = connection.execute(
            "SELECT id, storage_used, storage_reserved FROM users WHERE username = %s",
            (username,),
        ).fetchone()
        require(user is not None, "test user disappeared")
        expected_size = sum(len(case["payload"]) for case in cases.values())
        require(user["storage_used"] == expected_size, "storage_used does not match completed bytes")
        require(user["storage_reserved"] == 0, "storage_reserved was not fully released")

        files = connection.execute(
            "SELECT file.id, file.name, file.size, content.hash_md5, content.hash_sha256, "
            "content.storage_path, content.ref_count "
            "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
            "WHERE file.user_id = %s ORDER BY file.name",
            (user["id"],),
        ).fetchall()
        require(len(files) == len(cases), "unexpected file count after mixed-version uploads")
        files_by_name = {row["name"]: row for row in files}
        for case in cases.values():
            row = files_by_name.get(case["filename"])
            require(row is not None, f"file row missing for {case['filename']}")
            payload = case["payload"]
            require(row["id"] == case["file_id"], f"file id mismatch for {case['filename']}")
            require(row["size"] == len(payload), f"file size mismatch for {case['filename']}")
            require(row["hash_md5"] == hashlib.md5(payload).hexdigest(), "content MD5 mismatch")
            require(row["hash_sha256"] == hashlib.sha256(payload).hexdigest(), "content SHA-256 mismatch")
            require(row["ref_count"] == 1, "content ref_count mismatch")
            storage_path = Path(row["storage_path"])
            require(storage_path.is_file(), f"final blob is missing: {storage_path}")
            require(
                storage_path.is_relative_to(storage_root),
                f"final blob escaped the shared storage root: {storage_path}",
            )

        tasks = connection.execute(
            "SELECT id, status, staging_backend, staging_prefix, state_version, "
            "lease_owner, lease_expires_at, finalize_attempts, completed_file_id, reserved_bytes "
            "FROM upload_tasks WHERE user_id = %s ORDER BY id",
            (user["id"],),
        ).fetchall()
        require(len(tasks) == len(cases), "unexpected upload task count")
        tasks_by_id = {row["id"]: row for row in tasks}
        for name, case in cases.items():
            row = tasks_by_id.get(case["upload_id"])
            require(row is not None, f"upload task missing for {name}")
            require(row["status"] == 1, f"upload task is not Completed for {name}")
            require(row["staging_backend"] == "local", f"non-local task created for {name}")
            require(row["reserved_bytes"] == len(case["payload"]), "reserved byte snapshot drifted")
            require(row["lease_owner"] is None and row["lease_expires_at"] is None, "lease was not cleared")
            if case["completed_by"] == "legacy":
                require(row["staging_prefix"] is None, "post-expand legacy default changed")
                require(row["finalize_attempts"] == 0, "legacy completion used a new finalize lease")
                require(row["completed_file_id"] is None, "legacy completion wrote a new-only field")
            else:
                require(row["finalize_attempts"] == 1, "current completion claim count drifted")
                require(row["completed_file_id"] == case["file_id"], "current completion result is not durable")

        pre_expand = tasks_by_id[cases["pre_expand_handoff"]["upload_id"]]
        require(
            pre_expand["staging_prefix"]
            == f"staging/{cases['pre_expand_handoff']['upload_id']}",
            "pre-expand task did not receive the V003 staging prefix backfill",
        )
        post_expand = tasks_by_id[cases["post_expand_handoff"]["upload_id"]]
        require(post_expand["staging_prefix"] is None, "legacy INSERT no longer uses the expand default")
        current_task = tasks_by_id[cases["current_owned"]["upload_id"]]
        require(
            current_task["staging_prefix"] == cases["current_owned"]["upload_id"],
            "current local task did not persist its explicit session prefix",
        )

        chunk_count = connection.execute(
            "SELECT COUNT(*) AS count FROM upload_task_chunks "
            "WHERE task_id = ANY(%s)",
            ([case["upload_id"] for case in cases.values()],),
        ).fetchone()
        require(chunk_count is not None and chunk_count["count"] == 0, "completed chunks were not removed")

        cleanup_jobs = connection.execute(
            "SELECT aggregate_id, dedupe_key, status FROM storage_jobs "
            "WHERE aggregate_id = ANY(%s) ORDER BY aggregate_id",
            ([case["upload_id"] for case in cases.values()],),
        ).fetchall()
        current_completed_ids = {
            case["upload_id"]
            for case in cases.values()
            if case["completed_by"] == "current"
        }
        require(
            {row["aggregate_id"] for row in cleanup_jobs} == current_completed_ids,
            "current completion cleanup jobs do not match current-owned finalizations",
        )
        require(
            all(
                row["dedupe_key"] == f"staging-cleanup:{row['aggregate_id']}"
                and row["status"] == 0
                for row in cleanup_jobs
            ),
            "staging cleanup job contract drifted",
        )

        migrations = connection.execute(
            "SELECT version FROM schema_migrations ORDER BY version"
        ).fetchall()
        require(
            [row["version"] for row in migrations]
            == ["V003_distributed_upload", "V004_storage_reconciliation"],
            "expand migration ledger is incomplete",
        )


def exercise_mixed_versions(
    database_name: str,
    legacy_binary: Path,
    current_binary: Path,
    temporary_root: Path,
) -> None:
    storage_root = temporary_root / "storage"
    staging_root = temporary_root / "staging"
    storage_root.mkdir()
    staging_root.mkdir()
    legacy_port, current_port = allocate_ports(2)
    servers: list[ManagedServer] = []
    run_tag = uuid.uuid4().hex[:10]
    username = f"mix_{run_tag}"
    password = "ExpandMix123"
    cases: dict[str, dict[str, Any]] = {
        "pre_expand_handoff": {
            "filename": f"pre-expand-{run_tag}.bin",
            "payload": (f"pre-expand-handoff-{run_tag}-".encode() * 97),
            "completed_by": "current",
        },
        "post_expand_legacy": {
            "filename": f"post-expand-legacy-{run_tag}.bin",
            "payload": (f"post-expand-legacy-{run_tag}-".encode() * 89),
            "completed_by": "legacy",
        },
        "post_expand_handoff": {
            "filename": f"post-expand-handoff-{run_tag}.bin",
            "payload": (f"post-expand-handoff-{run_tag}-".encode() * 83),
            "completed_by": "current",
        },
        "current_owned": {
            "filename": f"current-owned-{run_tag}.bin",
            "payload": (f"current-owned-{run_tag}-".encode() * 79),
            "completed_by": "current",
        },
    }

    try:
        legacy_run = temporary_root / "legacy-run"
        legacy = ManagedServer(
            name="expand-legacy-v002",
            binary=legacy_binary,
            run_directory=legacy_run,
            config=server_config(
                database_name,
                legacy_port,
                "expand-legacy-v002",
                storage_root,
                staging_root,
            ),
            database_name=database_name,
            port=legacy_port,
            readiness_path="/api/health",
        )
        servers.append(legacy)
        legacy_pid = legacy.pid

        legacy_token = register_user(legacy.base_url, username, password)
        require_profile(legacy.base_url, legacy_token, username, "legacy profile before expand")

        pre_case = cases["pre_expand_handoff"]
        pre_case["upload_id"] = upload_without_complete(
            legacy.base_url,
            legacy_token,
            pre_case["filename"],
            pre_case["payload"],
        )
        require(
            (staging_root / pre_case["upload_id"] / "0.chunk").is_file(),
            "pre-expand legacy chunk was not written",
        )

        print("Applying V003/V004 while the V002 process remains online...")
        run_database_command([str(MIGRATOR)], database_name)
        legacy.require_running("online expand migration")
        require(legacy.pid == legacy_pid, "legacy PID changed during migration")
        recycled_connections = recycle_database_connections(database_name)
        require(recycled_connections > 0, "legacy connection pool was not recycled")
        wait_for_profile(
            legacy.base_url,
            legacy_token,
            username,
            "legacy profile after expand connection recycle",
        )
        legacy.require_running("connection pool recycle")
        require(legacy.pid == legacy_pid, "legacy PID changed during connection recycle")

        with connect(database_name) as connection:
            pre_task = connection.execute(
                "SELECT staging_backend, staging_prefix FROM upload_tasks WHERE id = %s",
                (pre_case["upload_id"],),
            ).fetchone()
            require(pre_task is not None, "pre-expand task disappeared")
            require(pre_task["staging_backend"] == "local", "pre-expand task backend changed")
            require(
                pre_task["staging_prefix"] == f"staging/{pre_case['upload_id']}",
                "pre-expand task was not backfilled",
            )
            assert_legacy_chunk(connection, pre_case["upload_id"], staging_root)

        legacy_case = cases["post_expand_legacy"]
        legacy_case["upload_id"] = upload_without_complete(
            legacy.base_url,
            legacy_token,
            legacy_case["filename"],
            legacy_case["payload"],
        )
        with connect(database_name) as connection:
            legacy_task = connection.execute(
                "SELECT staging_backend, staging_prefix FROM upload_tasks WHERE id = %s",
                (legacy_case["upload_id"],),
            ).fetchone()
            require(legacy_task is not None, "post-expand legacy task disappeared")
            require(
                legacy_task["staging_backend"] == "local"
                and legacy_task["staging_prefix"] is None,
                "post-expand legacy INSERT did not use compatible defaults",
            )
            assert_legacy_chunk(connection, legacy_case["upload_id"], staging_root)
        legacy_case["file_id"] = complete_upload(
            legacy.base_url,
            legacy_token,
            legacy_case["upload_id"],
        )

        handoff_case = cases["post_expand_handoff"]
        handoff_case["upload_id"] = upload_without_complete(
            legacy.base_url,
            legacy_token,
            handoff_case["filename"],
            handoff_case["payload"],
        )
        with connect(database_name) as connection:
            assert_legacy_chunk(connection, handoff_case["upload_id"], staging_root)

        current_run = temporary_root / "current-run"
        current = ManagedServer(
            name="expand-current",
            binary=current_binary,
            run_directory=current_run,
            config=server_config(
                database_name,
                current_port,
                "expand-current",
                storage_root,
                staging_root,
            ),
            database_name=database_name,
            port=current_port,
            readiness_path="/api/health/ready",
        )
        servers.append(current)
        current_pid = current.pid

        require_profile(current.base_url, legacy_token, username, "legacy token on current")
        current_token = login(current.base_url, username, password)
        require_profile(legacy.base_url, current_token, username, "current token on legacy")

        pre_case["file_id"] = complete_upload(
            current.base_url,
            legacy_token,
            pre_case["upload_id"],
        )
        handoff_case["file_id"] = complete_upload(
            current.base_url,
            current_token,
            handoff_case["upload_id"],
        )

        current_case = cases["current_owned"]
        current_case["upload_id"] = upload_without_complete(
            current.base_url,
            current_token,
            current_case["filename"],
            current_case["payload"],
        )
        current_case["file_id"] = complete_upload(
            current.base_url,
            current_token,
            current_case["upload_id"],
        )

        legacy.require_running("mixed-version request handling")
        current.require_running("mixed-version request handling")
        require(legacy.pid == legacy_pid, "legacy process restarted during coexistence")
        require(current.pid == current_pid, "current process restarted during coexistence")

        for name, case in cases.items():
            download_file(
                current.base_url,
                legacy_token,
                case["file_id"],
                case["payload"],
                f"current/{name}",
            )

        verify_database_state(database_name, username, cases, storage_root)
        legacy.require_running("final invariant verification")
        current.require_running("final invariant verification")
    except BaseException:
        for server in servers:
            print(server.log_tail(), file=sys.stderr)
        raise
    finally:
        for server in reversed(servers):
            server.stop()


def main() -> int:
    current_binary = resolve_current_binary()
    legacy_binary = build_legacy_binary(current_binary)
    database_name = f"disk_expand_mix_{uuid.uuid4().hex[:12]}"
    database_created = False

    try:
        create_database(database_name)
        database_created = True
        prepare_v002_database(database_name)
        with tempfile.TemporaryDirectory(prefix="disk-expand-mixed-") as temporary:
            exercise_mixed_versions(
                database_name,
                legacy_binary,
                current_binary,
                Path(temporary),
            )
    finally:
        if database_created:
            drop_database(database_name)

    print(
        "PASS: V002 remained online through expand, continued local uploads, "
        "and current completed legacy staging safely"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

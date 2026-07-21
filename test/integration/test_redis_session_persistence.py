#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Prove Redis-backed session state survives a durable process restart."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any, IO

import httpx

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_auth_cluster_consistency import (  # noqa: E402
    auth_headers,
    dependency_ready,
    dependency_unready,
    register_and_login,
    require_error_code,
    require_liveness,
    require_single_rotation,
    require_success,
    wait_until,
)
from test_expand_mixed_version import (  # noqa: E402
    INIT_SQL,
    ManagedServer,
    allocate_ports,
    create_database,
    drop_database,
    require,
    resolve_current_binary,
    run_sql_file,
    server_config,
)


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = ROOT / ".sisyphus/evidence/redis-session-persistence-summary.json"
API_A = "redis-persistence-a"
API_B = "redis-persistence-b"


def resolve_executable(candidates: tuple[str, ...]) -> Path:
    for candidate in candidates:
        executable = shutil.which(candidate)
        if executable:
            return Path(executable).resolve()
    raise AssertionError(f"missing required executable: {' or '.join(candidates)}")


class PersistentRedis:
    """Own an isolated AOF Redis process that can be crash-restarted in place."""

    def __init__(self, root: Path, port: int) -> None:
        self.root = root
        self.port = port
        self.server_binary = resolve_executable(("valkey-server", "redis-server"))
        self.cli_binary = resolve_executable(("valkey-cli", "redis-cli"))
        self.log_path = root / "redis.log"
        self.process: subprocess.Popen[bytes] | None = None
        self.log_handle: IO[bytes] | None = None

    @property
    def pid(self) -> int:
        require(self.process is not None, "Redis process was not started")
        return self.process.pid

    def start(self) -> None:
        require(self.process is None, "Redis process is already running")
        self.root.mkdir(parents=True, exist_ok=True)
        self.log_handle = self.log_path.open("ab")
        try:
            self.process = subprocess.Popen(
                [
                    str(self.server_binary),
                    "--bind",
                    "127.0.0.1",
                    "--port",
                    str(self.port),
                    "--protected-mode",
                    "yes",
                    "--daemonize",
                    "no",
                    "--dir",
                    str(self.root),
                    "--appendonly",
                    "yes",
                    "--appendfsync",
                    "always",
                    "--save",
                    "",
                    "--logfile",
                    "",
                ],
                cwd=self.root,
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
            )
            self._wait_until_ready()
        except BaseException:
            self.stop()
            raise

    def _cli(
        self,
        *arguments: str,
        input_text: str | None = None,
        check: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            [
                str(self.cli_binary),
                "-h",
                "127.0.0.1",
                "-p",
                str(self.port),
                "--raw",
                *arguments,
            ],
            input=input_text,
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        if check:
            require(
                result.returncode == 0,
                f"Redis CLI failed with {result.returncode}: {result.stderr.strip()}",
            )
        return result

    def _wait_until_ready(self) -> None:
        deadline = time.monotonic() + 15
        last_error = "no response"
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                raise AssertionError(
                    f"Redis exited before readiness with {self.process.returncode}\n"
                    f"{self.log_tail()}"
                )
            try:
                result = self._cli("PING", check=False)
                if result.returncode == 0 and result.stdout.strip() == "PONG":
                    return
                last_error = result.stderr.strip() or result.stdout.strip()
            except (OSError, subprocess.SubprocessError) as error:
                last_error = str(error)
            time.sleep(0.1)
        raise AssertionError(f"Redis did not become ready: {last_error}\n{self.log_tail()}")

    def command(self, *arguments: str) -> str:
        return self._cli(*arguments).stdout.strip()

    def info(self, section: str) -> dict[str, str]:
        values: dict[str, str] = {}
        for line in self.command("INFO", section).splitlines():
            if not line or line.startswith("#") or ":" not in line:
                continue
            key, value = line.split(":", 1)
            values[key] = value
        return values

    def scan_keys(self, pattern: str) -> list[str]:
        cursor = "0"
        keys: list[str] = []
        while True:
            lines = self.command(
                "SCAN",
                cursor,
                "MATCH",
                pattern,
                "COUNT",
                "100",
            ).splitlines()
            require(lines, f"Redis SCAN returned no cursor for {pattern}")
            cursor = lines[0]
            keys.extend(line for line in lines[1:] if line)
            if cursor == "0":
                return sorted(keys)

    def pttl(self, key: str) -> int:
        value = self.command("PTTL", key)
        require(value.lstrip("-").isdigit(), f"Redis PTTL was not numeric for {key}")
        return int(value)

    def fsync_barrier(self) -> None:
        result = self._cli(
            input_text=(
                "SET test:redis-session-persistence-barrier 1 PX 60000\n"
                "WAITAOF 1 0 5000\n"
            )
        )
        lines = [line for line in result.stdout.splitlines() if line]
        require(
            len(lines) >= 3 and lines[0] == "OK" and lines[-2] == "1",
            f"Redis AOF fsync barrier failed: {lines}",
        )
        persistence = self.info("persistence")
        require(persistence.get("aof_enabled") == "1", "Redis AOF is not enabled")
        require(
            persistence.get("aof_last_write_status") == "ok",
            "Redis reported an AOF write failure",
        )

    def crash(self) -> int:
        require(self.process is not None, "Redis process was not started")
        process = self.process
        old_pid = process.pid
        process.kill()
        process.wait(timeout=5)
        self.process = None
        self._close_log()
        return old_pid

    def stop(self) -> None:
        process = self.process
        if process is not None:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
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
        if not self.log_path.exists():
            return ""
        return "\n".join(
            self.log_path.read_text(encoding="utf-8", errors="replace").splitlines()[
                -80:
            ]
        )


def persistent_api_config(
    database_name: str,
    port: int,
    instance_id: str,
    storage_root: Path,
    staging_root: Path,
    redis_port: int,
) -> dict[str, Any]:
    config = server_config(
        database_name,
        port,
        instance_id,
        storage_root,
        staging_root,
        role="api",
    )
    redis_client = config["redis_clients"][0]
    redis_client.update(
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
    database_name: str,
    port: int,
    storage_root: Path,
    staging_root: Path,
    redis_port: int,
) -> ManagedServer:
    return ManagedServer(
        name=name,
        binary=binary,
        run_directory=run_directory,
        config=persistent_api_config(
            database_name,
            port,
            name,
            storage_root,
            staging_root,
            redis_port,
        ),
        database_name=database_name,
        port=port,
        readiness_path="/api/health/ready",
        role="api",
        environment_overrides={
            "REDIS_HOST": "127.0.0.1",
            "REDIS_PORT": str(redis_port),
            "REDIS_DB": "0",
        },
    )


def require_single_key(redis: PersistentRedis, pattern: str) -> str:
    keys = redis.scan_keys(pattern)
    require(len(keys) == 1, f"expected one Redis key for {pattern}, found {len(keys)}")
    return keys[0]


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
    database_name = f"disk_redis_persistence_{suffix}"
    password = "PersistencePass123"
    database_created = False
    redis: PersistentRedis | None = None
    api_a: ManagedServer | None = None
    api_b: ManagedServer | None = None

    try:
        binary = resolve_current_binary()
        redis_port, api_a_port, api_b_port = allocate_ports(3)
        with tempfile.TemporaryDirectory(prefix="disk-redis-persistence-") as temporary:
            temporary_root = Path(temporary)
            storage_root = temporary_root / "final"
            staging_root = temporary_root / "staging"
            redis = PersistentRedis(temporary_root / "redis", redis_port)
            redis.start()

            create_database(database_name)
            database_created = True
            run_sql_file(database_name, INIT_SQL)

            api_a = start_api(
                name=API_A,
                binary=binary,
                run_directory=temporary_root / "api-a",
                database_name=database_name,
                port=api_a_port,
                storage_root=storage_root,
                staging_root=staging_root,
                redis_port=redis_port,
            )
            revoked = register_and_login(
                api_a.base_url,
                f"persist_revoked_{suffix}",
                password,
            )
            logout = httpx.post(
                api_a.base_url + "/api/auth/logout",
                headers=auth_headers(revoked["access_token"]),
                timeout=15,
            )
            require_success(logout, "persisted access-token logout")
            refreshable = register_and_login(
                api_a.base_url,
                f"persist_refresh_{suffix}",
                password,
            )

            refresh_key = require_single_key(redis, "refresh_token:*")
            revocation_key = require_single_key(redis, "access_token_blacklist:*")
            refresh_value_before = redis.command("GET", refresh_key)
            revocation_value_before = redis.command("GET", revocation_key)
            require(refresh_value_before, "refresh-token state was not stored")
            require(revocation_value_before == "1", "access revocation value changed")
            refresh_ttl_before = redis.pttl(refresh_key)
            revocation_ttl_before = redis.pttl(revocation_key)
            require(refresh_ttl_before > 0, "refresh-token state has no positive TTL")
            require(revocation_ttl_before > 0, "access revocation has no positive TTL")
            redis.fsync_barrier()

            api_a_pid = api_a.pid
            redis_pid_before = redis.crash()
            wait_until(
                "API A Redis-unready state",
                lambda: dependency_unready(api_a.base_url),
            )
            require_liveness(api_a.base_url)

            redis.start()
            require(redis.pid != redis_pid_before, "Redis restart reused the old process")
            wait_until(
                "API A readiness after persistent Redis restart",
                lambda: dependency_ready(api_a.base_url),
            )
            require(api_a.pid == api_a_pid, "API A restarted with Redis")

            require(
                require_single_key(redis, "refresh_token:*") == refresh_key,
                "refresh-token key changed after Redis restart",
            )
            require(
                require_single_key(redis, "access_token_blacklist:*")
                == revocation_key,
                "access revocation key changed after Redis restart",
            )
            require(
                redis.command("GET", refresh_key) == refresh_value_before,
                "refresh-token state changed after Redis restart",
            )
            require(
                redis.command("GET", revocation_key) == revocation_value_before,
                "access revocation state changed after Redis restart",
            )
            refresh_ttl_after = redis.pttl(refresh_key)
            revocation_ttl_after = redis.pttl(revocation_key)
            require(
                0 < refresh_ttl_after < refresh_ttl_before,
                "refresh-token TTL was lost or reset after Redis restart",
            )
            require(
                0 < revocation_ttl_after < revocation_ttl_before,
                "access revocation TTL was lost or reset after Redis restart",
            )

            api_b = start_api(
                name=API_B,
                binary=binary,
                run_directory=temporary_root / "api-b",
                database_name=database_name,
                port=api_b_port,
                storage_root=storage_root,
                staging_root=staging_root,
                redis_port=redis_port,
            )
            revoked_from_cold_api = httpx.get(
                api_b.base_url + "/api/user/profile",
                headers=auth_headers(revoked["access_token"]),
                timeout=10,
            )
            require_error_code(
                revoked_from_cold_api,
                "40111",
                "persisted revocation from cold API B",
                expected_status=401,
            )
            require_single_rotation(
                (api_a.base_url, api_b.base_url),
                refreshable["refresh_token"],
                "persisted refresh CAS after Redis restart",
            )

            write_evidence(
                {
                    "schema_version": 1,
                    "scenario": "redis_session_security_state_persistence",
                    "redis_process_crash_restarted": True,
                    "redis_aof_enabled": True,
                    "redis_appendfsync_policy": "always",
                    "api_a_process_preserved": True,
                    "api_b_started_cold": True,
                    "refresh_state_survived": True,
                    "revocation_state_survived": True,
                    "security_state_ttls_decreased": True,
                    "cold_api_enforced_revocation": True,
                    "refresh_winners_after_restart": 1,
                    "shared_redis_service_touched": False,
                    "passed": True,
                }
            )

            api_b.stop()
            api_b = None
            api_a.stop()
            api_a = None
            redis.stop()
            redis = None
            print(
                "PASS: AOF restart preserved refresh and revocation state, TTLs, "
                "cold-instance enforcement, and single-winner refresh CAS"
            )
        return 0
    except BaseException:
        for server in (api_a, api_b):
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
        if redis is not None:
            print(redis.log_tail(), file=sys.stderr)
        raise
    finally:
        for server in (api_b, api_a):
            if server is not None:
                server.stop()
        if redis is not None:
            redis.stop()
        if database_created:
            drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

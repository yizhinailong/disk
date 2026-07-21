#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify stable-endpoint Redis promotion semantics with two live APIs."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any, Callable

import httpx

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_auth_cluster_consistency import (  # noqa: E402
    CuttableTcpProxy,
    auth_headers,
    dependency_ready,
    dependency_unready,
    register_and_login,
    require_error_code,
    require_liveness,
    require_profile,
    require_single_rotation,
    require_success,
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
)
from test_redis_session_persistence import (  # noqa: E402
    PersistentRedis,
    require_single_key,
    start_api,
)


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = ROOT / ".sisyphus/evidence/redis-failover-semantics-summary.json"
API_A = "redis-failover-a"
API_B = "redis-failover-b"
SCAN_KEY_COUNT = 16


def wait_for(
    description: str,
    predicate: Callable[[], bool],
    *,
    timeout_seconds: float = 20,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.1)
    raise AssertionError(f"timed out waiting for {description}")


def replica_is_ready(replica: PersistentRedis) -> bool:
    replication = replica.info("replication")
    return (
        replication.get("role") in ("slave", "replica")
        and replication.get("master_link_status") == "up"
        and replication.get("master_sync_in_progress") == "0"
    )


def promoted_is_writable(replica: PersistentRedis) -> bool:
    replication = replica.info("replication")
    return replication.get("role") == "master"


def state_reached_replica(
    replica: PersistentRedis,
    refresh_key: str,
    refresh_value: str,
    revocation_key: str,
    revocation_value: str,
    scan_keys: set[str],
) -> bool:
    return (
        replica.command("GET", refresh_key) == refresh_value
        and replica.command("GET", revocation_key) == revocation_value
        and set(replica.scan_keys("test:redis-failover:scan:*")) == scan_keys
        and all(replica.pttl(key) > 0 for key in scan_keys)
    )


def require_timeout_failure(
    base_url: str,
    access_token: str,
) -> float:
    started_at = time.monotonic()
    response = httpx.get(
        base_url + "/api/user/profile",
        headers=auth_headers(access_token),
        timeout=10,
    )
    elapsed = time.monotonic() - started_at
    require_error_code(
        response,
        "70002",
        f"Redis failover timeout at {base_url}",
        expected_status=500,
    )
    require(
        0.5 <= elapsed < 5.0,
        f"Redis command failure ignored the configured timeout budget: {elapsed:.3f}s",
    )
    return elapsed


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
    database_name = f"disk_redis_failover_{suffix}"
    password = "FailoverPass123"
    database_created = False
    temporary: tempfile.TemporaryDirectory[str] | None = None
    primary: PersistentRedis | None = None
    replica: PersistentRedis | None = None
    proxy: CuttableTcpProxy | None = None
    api_a: ManagedServer | None = None
    api_b: ManagedServer | None = None

    try:
        binary = resolve_current_binary()
        primary_port, replica_port, api_a_port, api_b_port = allocate_ports(4)
        temporary = tempfile.TemporaryDirectory(prefix="disk-redis-failover-")
        temporary_root = Path(temporary.name)
        storage_root = temporary_root / "final"
        staging_root = temporary_root / "staging"

        primary = PersistentRedis(temporary_root / "primary", primary_port)
        primary.start()
        replica = PersistentRedis(
            temporary_root / "replica",
            replica_port,
            server_options=(
                "--replicaof",
                "127.0.0.1",
                str(primary_port),
                "--replica-read-only",
                "yes",
            ),
        )
        replica.start()
        wait_for("Redis replica link", lambda: replica_is_ready(replica))

        create_database(database_name)
        database_created = True
        run_sql_file(database_name, INIT_SQL)

        proxy = CuttableTcpProxy("127.0.0.1", primary_port)
        with proxy:
            api_a = start_api(
                name=API_A,
                binary=binary,
                run_directory=temporary_root / "api-a",
                database_name=database_name,
                port=api_a_port,
                storage_root=storage_root,
                staging_root=staging_root,
                redis_port=proxy.port,
            )
            api_b = start_api(
                name=API_B,
                binary=binary,
                run_directory=temporary_root / "api-b",
                database_name=database_name,
                port=api_b_port,
                storage_root=storage_root,
                staging_root=staging_root,
                redis_port=proxy.port,
            )
            base_urls = (api_a.base_url, api_b.base_url)

            revoked = register_and_login(
                api_a.base_url,
                f"failover_revoked_{suffix}",
                password,
            )
            logout = httpx.post(
                api_a.base_url + "/api/auth/logout",
                headers=auth_headers(revoked["access_token"]),
                timeout=15,
            )
            require_success(logout, "pre-failover logout")
            refreshable_username = f"failover_refresh_{suffix}"
            refreshable = register_and_login(
                api_a.base_url,
                refreshable_username,
                password,
            )
            require_profile(
                api_b.base_url,
                refreshable["access_token"],
                refreshable_username,
            )

            scan_keys = {
                f"test:redis-failover:scan:{suffix}:{index:02d}"
                for index in range(SCAN_KEY_COUNT)
            }
            for index, key in enumerate(sorted(scan_keys)):
                require(
                    primary.command("SET", key, str(index), "PX", "60000") == "OK",
                    "failed to create Redis failover SCAN fixture",
                )

            refresh_key = require_single_key(primary, "refresh_token:*")
            revocation_key = require_single_key(primary, "access_token_blacklist:*")
            refresh_value = primary.command("GET", refresh_key)
            revocation_value = primary.command("GET", revocation_key)
            require(refresh_value, "primary has no refresh-token state")
            require(revocation_value == "1", "primary revocation state changed")
            primary.fsync_barrier()
            wait_for(
                "security and SCAN fixtures on Redis replica",
                lambda: state_reached_replica(
                    replica,
                    refresh_key,
                    refresh_value,
                    revocation_key,
                    revocation_value,
                    scan_keys,
                ),
            )
            refresh_ttl_before = replica.pttl(refresh_key)
            revocation_ttl_before = replica.pttl(revocation_key)
            scan_ttl_before = replica.pttl(sorted(scan_keys)[0])
            require(
                min(refresh_ttl_before, revocation_ttl_before, scan_ttl_before) > 0,
                "replica contains an expired failover fixture",
            )

            api_a_pid = api_a.pid
            api_b_pid = api_b.pid
            proxy.pause_forwarding()
            timeout_durations = [
                require_timeout_failure(base_url, refreshable["access_token"])
                for base_url in base_urls
            ]
            for base_url in base_urls:
                wait_for(
                    f"Redis-unready state at {base_url}",
                    lambda base_url=base_url: dependency_unready(base_url),
                )
                require_liveness(base_url)

            primary_pid = primary.crash()
            require(
                replica.command("REPLICAOF", "NO", "ONE") == "OK",
                "failed to promote Redis replica",
            )
            wait_for("promoted Redis writer", lambda: promoted_is_writable(replica))
            recovery_started_at = time.monotonic()
            proxy.switch_target("127.0.0.1", replica_port)
            for base_url in base_urls:
                wait_for(
                    f"API readiness after Redis promotion at {base_url}",
                    lambda base_url=base_url: dependency_ready(base_url),
                )
            recovery_seconds = time.monotonic() - recovery_started_at

            require(api_a.pid == api_a_pid, "API A restarted during Redis failover")
            require(api_b.pid == api_b_pid, "API B restarted during Redis failover")
            require(primary.process is None, "old Redis primary remained writable")
            require(replica.pid != primary_pid, "promoted Redis reused the primary PID")

            require(
                replica.command("GET", refresh_key) == refresh_value,
                "refresh-token state changed during Redis promotion",
            )
            require(
                replica.command("GET", revocation_key) == revocation_value,
                "access revocation changed during Redis promotion",
            )
            refresh_ttl_after = replica.pttl(refresh_key)
            revocation_ttl_after = replica.pttl(revocation_key)
            scan_ttl_after = replica.pttl(sorted(scan_keys)[0])
            require(
                0 < refresh_ttl_after < refresh_ttl_before,
                "refresh-token TTL was lost or reset at promotion",
            )
            require(
                0 < revocation_ttl_after < revocation_ttl_before,
                "revocation TTL was lost or reset at promotion",
            )
            require(
                0 < scan_ttl_after < scan_ttl_before,
                "representative SCAN-key TTL was lost or reset at promotion",
            )
            require(
                set(replica.scan_keys("test:redis-failover:scan:*")) == scan_keys,
                "SCAN restarted from zero on the promoted writer lost keys",
            )

            revoked_after_failover = httpx.get(
                api_b.base_url + "/api/user/profile",
                headers=auth_headers(revoked["access_token"]),
                timeout=10,
            )
            require_error_code(
                revoked_after_failover,
                "40111",
                "replicated revocation after Redis promotion",
                expected_status=401,
            )
            for base_url in base_urls:
                require_profile(
                    base_url,
                    refreshable["access_token"],
                    refreshable_username,
                )
            require_single_rotation(
                base_urls,
                refreshable["refresh_token"],
                "refresh CAS after Redis replica promotion",
            )

            write_evidence(
                {
                    "schema_version": 1,
                    "scenario": "redis_replica_promotion_stable_endpoint",
                    "redis_nodes": 2,
                    "separate_persistent_directories": True,
                    "old_primary_sigkilled": True,
                    "old_primary_fenced": True,
                    "replica_promoted": True,
                    "stable_endpoint_switched": True,
                    "api_processes_preserved": True,
                    "command_timeouts_fail_closed": True,
                    "command_timeout_seconds_min": round(min(timeout_durations), 3),
                    "command_timeout_seconds_max": round(max(timeout_durations), 3),
                    "endpoint_recovery_seconds": round(recovery_seconds, 3),
                    "replicated_revocation_enforced": True,
                    "refresh_winners_after_promotion": 1,
                    "security_state_ttls_decreased": True,
                    "scan_restarted_from_zero": True,
                    "scan_key_count": SCAN_KEY_COUNT,
                    "shared_redis_service_touched": False,
                    "passed": True,
                }
            )

            api_b.stop()
            api_b = None
            api_a.stop()
            api_a = None

        proxy = None
        replica.stop()
        replica = None
        primary.stop()
        primary = None
        print(
            "PASS: stable Redis endpoint promotion preserved timeouts, reconnects, "
            "revocation, Lua CAS, SCAN, and TTL semantics"
        )
        return 0
    except BaseException:
        for server in (api_a, api_b):
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
        for redis in (primary, replica):
            if redis is not None:
                print(redis.log_tail(), file=sys.stderr)
        raise
    finally:
        for server in (api_b, api_a):
            if server is not None:
                server.stop()
        if proxy is not None:
            proxy.close()
        for redis in (replica, primary):
            if redis is not None:
                redis.stop()
        if database_created:
            drop_database(database_name)
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify cross-instance token consistency and Redis fault recovery."""

from __future__ import annotations

import concurrent.futures
import json
import os
import re
import socket
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path
from typing import Any, Callable

import httpx
import psycopg

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_expand_mixed_version import (  # noqa: E402
    INIT_SQL,
    ManagedServer,
    allocate_ports,
    connect,
    create_database,
    drop_database,
    redis_config,
    require,
    resolve_current_binary,
    run_sql_file,
    server_config,
)


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = ROOT / ".sisyphus/evidence/auth-cluster-consistency-summary.json"
API_A = "auth-cluster-a"
API_B = "auth-cluster-b"


class CuttableTcpProxy:
    """Forward TCP traffic and control only connections owned by this test."""

    def __init__(self, target_host: str, target_port: int) -> None:
        self._target = (target_host, target_port)
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen()
        self._listener.settimeout(0.1)
        self.port = int(self._listener.getsockname()[1])
        self._stop = threading.Event()
        self._cut = threading.Event()
        self._paused = threading.Event()
        self._lock = threading.Lock()
        self._connections: set[socket.socket] = set()
        self._relay_threads: list[threading.Thread] = []
        self._accept_thread = threading.Thread(
            target=self._accept_connections,
            name="switchable-tcp-proxy",
            daemon=True,
        )

    def __enter__(self) -> CuttableTcpProxy:
        self._accept_thread.start()
        return self

    def __exit__(self, _exc_type: object, _exc: object, _traceback: object) -> None:
        self.close()

    def cut(self) -> None:
        self._cut.set()
        self._paused.clear()
        self._close_connections()

    def pause_forwarding(self) -> None:
        self._paused.set()

    def switch_target(self, target_host: str, target_port: int) -> None:
        self._paused.set()
        with self._lock:
            self._target = (target_host, target_port)
        self._close_connections()
        self._cut.clear()
        self._paused.clear()

    def heal(self) -> None:
        self._cut.clear()
        self._paused.clear()

    def close(self) -> None:
        self._stop.set()
        self._cut.clear()
        self._paused.clear()
        try:
            self._listener.close()
        except OSError:
            pass
        self._close_connections()
        if self._accept_thread.is_alive():
            self._accept_thread.join(timeout=2)
        for relay_thread in self._relay_threads:
            relay_thread.join(timeout=0.2)

    def _close_connections(self) -> None:
        with self._lock:
            connections = list(self._connections)
        for connection in connections:
            self._close_socket(connection)

    @staticmethod
    def _close_socket(connection: socket.socket) -> None:
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            connection.close()
        except OSError:
            pass

    def _accept_connections(self) -> None:
        while not self._stop.is_set():
            try:
                client, _address = self._listener.accept()
            except TimeoutError:
                continue
            except OSError:
                break

            if self._cut.is_set():
                self._close_socket(client)
                continue

            try:
                with self._lock:
                    target = self._target
                upstream = socket.create_connection(target, timeout=2)
            except OSError:
                self._close_socket(client)
                continue

            if self._cut.is_set():
                self._close_socket(client)
                self._close_socket(upstream)
                continue

            client.settimeout(0.1)
            upstream.settimeout(0.1)
            with self._lock:
                self._connections.update((client, upstream))
            for source, destination in ((client, upstream), (upstream, client)):
                relay_thread = threading.Thread(
                    target=self._relay,
                    args=(source, destination),
                    name="switchable-tcp-relay",
                    daemon=True,
                )
                self._relay_threads.append(relay_thread)
                relay_thread.start()

    def _relay(self, source: socket.socket, destination: socket.socket) -> None:
        try:
            while not self._stop.is_set() and not self._cut.is_set():
                if self._paused.is_set():
                    time.sleep(0.01)
                    continue
                try:
                    data = source.recv(65536)
                except TimeoutError:
                    continue
                except OSError:
                    break
                if not data:
                    break
                if self._paused.is_set():
                    continue
                destination.sendall(data)
        except OSError:
            pass
        finally:
            for connection in (source, destination):
                self._close_socket(connection)
            with self._lock:
                self._connections.discard(source)
                self._connections.discard(destination)


def wait_until(
    description: str,
    predicate: Callable[[], Any],
    *,
    timeout_seconds: float = 20,
) -> Any:
    deadline = time.monotonic() + timeout_seconds
    last_value: Any = None
    last_error = ""
    while time.monotonic() < deadline:
        try:
            last_value = predicate()
            if last_value:
                return last_value
        except (httpx.HTTPError, psycopg.Error) as error:
            last_error = str(error)
        time.sleep(0.1)
    detail = f", last_error={last_error}" if last_error else ""
    raise AssertionError(
        f"timed out waiting for {description}: last={last_value}{detail}"
    )


def response_envelope(response: httpx.Response, label: str) -> dict[str, Any]:
    try:
        payload = response.json()
    except ValueError as error:
        raise AssertionError(
            f"{label} returned non-JSON HTTP {response.status_code}: {response.text[:300]}"
        ) from error
    require(isinstance(payload, dict), f"{label} response is not an object")
    require("code" in payload, f"{label} response has no code")
    return payload


def success_data(response: httpx.Response, label: str) -> dict[str, Any]:
    payload = response_envelope(response, label)
    require(
        response.status_code in (200, 201) and str(payload["code"]) == "0",
        f"{label} failed: HTTP {response.status_code}, body={payload}",
    )
    data = payload.get("data")
    require(isinstance(data, dict), f"{label} response has invalid data: {payload}")
    return data


def require_success(response: httpx.Response, label: str) -> dict[str, Any]:
    payload = response_envelope(response, label)
    require(
        response.status_code in (200, 201) and str(payload["code"]) == "0",
        f"{label} failed: HTTP {response.status_code}, body={payload}",
    )
    return payload


def require_error_code(
    response: httpx.Response,
    expected_code: str,
    label: str,
    *,
    expected_status: int | None = None,
) -> None:
    payload = response_envelope(response, label)
    if expected_status is not None:
        require(
            response.status_code == expected_status,
            f"{label} returned HTTP {response.status_code}, expected {expected_status}",
        )
    require(
        str(payload["code"]) == expected_code,
        f"{label} returned HTTP {response.status_code}, code={payload['code']}",
    )


def auth_headers(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


AUTH_RUNTIME_MESSAGE_PATTERNS = (
    re.compile(r"Auth CPU pool initialized: threads=\d+"),
    re.compile(r"Token service constructed"),
    re.compile(r"Revocation cache maintenance started: interval_seconds=\d+"),
    re.compile(r"Auth CPU pool metrics started: interval_seconds=\d+"),
    re.compile(
        r"Auth CPU pool metrics: period_seconds=\d+, submitted=\d+, "
        r"completed=\d+, active=\d+, peak=\d+"
    ),
    re.compile(
        r"Token cache eviction completed: access_evicted=\d+, access_size=\d+, "
        r"share_evicted=\d+, share_size=\d+"
    ),
)


def auth_runtime_events(server: ManagedServer) -> list[dict[str, Any]]:
    if server.log_handle is not None:
        server.log_handle.flush()
    if not server.log_path.is_file():
        return []

    events: list[dict[str, Any]] = []
    for line in server.log_path.read_text(
        encoding="utf-8",
        errors="replace",
    ).splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if (
            isinstance(record, dict)
            and record.get("schema_version") == 1
            and record.get("source") == "application"
            and record.get("operation") == "auth_runtime"
        ):
            events.append(record)
    return events


def auth_runtime_ready(server: ManagedServer) -> bool:
    messages = [str(event.get("message", "")) for event in auth_runtime_events(server)]
    return (
        any(message.startswith("Auth CPU pool initialized: threads=") for message in messages)
        and "Auth CPU pool metrics started: interval_seconds=1" in messages
        and any(message.startswith("Auth CPU pool metrics: period_seconds=") for message in messages)
    )


def require_auth_runtime_logs(server: ManagedServer) -> int:
    events = auth_runtime_events(server)
    require(events, "API A emitted no auth_runtime events")
    messages = [str(event.get("message", "")) for event in events]
    require(
        any(message.startswith("Auth CPU pool initialized: threads=") for message in messages),
        f"auth CPU pool initialization event is missing: {messages}",
    )
    require(
        "Auth CPU pool metrics started: interval_seconds=1" in messages,
        f"auth CPU pool metrics timer event is missing: {messages}",
    )
    require(
        any(message.startswith("Auth CPU pool metrics: period_seconds=") for message in messages),
        f"auth CPU pool periodic metrics event is missing: {messages}",
    )

    for event in events:
        require(event.get("instance_id") == API_A, f"auth runtime instance drifted: {event}")
        for field in ("request_id", "upload_id", "job_id", "lease_owner", "state_version"):
            require(event.get(field) is None, f"auth runtime event populated {field}: {event}")
        message = str(event.get("message", ""))
        require(
            any(pattern.fullmatch(message) for pattern in AUTH_RUNTIME_MESSAGE_PATTERNS),
            f"auth runtime message is not bounded: {event}",
        )
        require("instance_id=" not in message, f"auth runtime message repeats instance: {event}")
    return len(events)


def cluster_config(
    database_name: str,
    port: int,
    instance_id: str,
    storage_root: Path,
    staging_root: Path,
    redis_proxy_port: int,
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
            "port": redis_proxy_port,
            "timeout": 1.0,
        }
    )
    config["custom_config"]["disk"]["auth_cpu_pool_metrics_interval_seconds"] = 1
    real_ip_plugin = next(
        (
            plugin
            for plugin in config["plugins"]
            if plugin.get("name") == "drogon::plugin::RealIpResolver"
        ),
        None,
    )
    if real_ip_plugin is None:
        real_ip_plugin = {
            "name": "drogon::plugin::RealIpResolver",
            "config": {
                "trust_ips": [],
                "from_header": "x-real-ip",
                "attribute_key": "disk-client-ip",
            },
        }
        config["plugins"].insert(0, real_ip_plugin)
    real_ip_plugin["config"]["trust_ips"] = ["127.0.0.1"]
    global_filters = next(
        plugin
        for plugin in config["plugins"]
        if plugin.get("name") == "drogon::plugin::GlobalFilters"
    )
    global_filters["dependencies"] = ["drogon::plugin::RealIpResolver"]
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
    redis_proxy_port: int,
) -> ManagedServer:
    return ManagedServer(
        name=name,
        binary=binary,
        run_directory=run_directory,
        config=cluster_config(
            database_name,
            port,
            name,
            storage_root,
            staging_root,
            redis_proxy_port,
        ),
        database_name=database_name,
        port=port,
        readiness_path="/api/health/ready",
        role="api",
        environment_overrides={
            "REDIS_HOST": "127.0.0.1",
            "REDIS_PORT": str(redis_proxy_port),
        },
    )


def register_and_login(
    base_url: str,
    username: str,
    password: str,
    *,
    client_ip: str | None = None,
) -> dict[str, str]:
    register = httpx.post(
        base_url + "/api/auth/register",
        json={
            "username": username,
            "email": f"{username}@example.test",
            "password": password,
        },
        timeout=20,
    )
    success_data(register, "register on API A")
    headers = {"X-Real-IP": client_ip} if client_ip is not None else None
    login = httpx.post(
        base_url + "/api/auth/login",
        headers=headers,
        json={"account": username, "password": password},
        timeout=20,
    )
    data = success_data(login, "login on API A")
    access_token = data.get("access_token")
    refresh_token = data.get("refresh_token")
    require(
        isinstance(access_token, str) and access_token,
        "login returned no access token",
    )
    require(
        isinstance(refresh_token, str) and refresh_token,
        "login returned no refresh token",
    )
    return {"access_token": access_token, "refresh_token": refresh_token}


def login_from_source(
    base_url: str,
    username: str,
    password: str,
    source_ip: str,
) -> tuple[httpx.Response, dict[str, Any]]:
    transport = httpx.HTTPTransport(local_address=source_ip)
    with httpx.Client(transport=transport, timeout=20) as client:
        response = client.post(
            base_url + "/api/auth/login",
            json={"account": username, "password": password},
        )
    return response, response_envelope(response, f"login from {source_ip}")


def require_account_lock_consistency(
    base_urls: tuple[str, str],
    database_name: str,
    username: str,
    password: str,
) -> dict[str, Any]:
    register = httpx.post(
        base_urls[0] + "/api/auth/register",
        json={
            "username": username,
            "email": f"{username}@example.test",
            "password": password,
        },
        timeout=20,
    )
    success_data(register, "register account-lock user")

    request_count = 12
    attempts = [
        (
            base_urls[index % len(base_urls)],
            username,
            "WrongPass456",
            f"127.10.0.{index + 1}",
        )
        for index in range(request_count)
    ]
    with concurrent.futures.ThreadPoolExecutor(max_workers=request_count) as pool:
        results = list(pool.map(lambda arguments: login_from_source(*arguments), attempts))

    for response, payload in results:
        require(response.status_code == 401, f"wrong-password login returned {response.status_code}")
        require(
            str(payload["code"]) in {"40101", "40102"},
            f"wrong-password login returned unexpected code: {payload}",
        )

    with connect(database_name) as connection:
        locked_row = connection.execute(
            "SELECT id, status, login_attempts, locked_until, "
            "locked_until > NOW() AS lock_active "
            "FROM users WHERE username = %s",
            (username,),
        ).fetchone()
    require(locked_row is not None, "account-lock user is missing")
    require(locked_row["status"] == 1, f"automatic lock changed status: {locked_row}")
    require(locked_row["login_attempts"] == 5, f"failure count is not atomic: {locked_row}")
    require(locked_row["lock_active"] is True, f"temporary lock is not active: {locked_row}")
    original_deadline = locked_row["locked_until"]

    for index in range(4):
        response, _payload = login_from_source(
            base_urls[index % len(base_urls)],
            username,
            "WrongPass456",
            f"127.10.1.{index + 1}",
        )
        require_error_code(
            response,
            "40102",
            "locked wrong-password login",
            expected_status=401,
        )
    with connect(database_name) as connection:
        stable_row = connection.execute(
            "SELECT login_attempts, locked_until FROM users WHERE username = %s",
            (username,),
        ).fetchone()
    require(
        stable_row is not None
        and stable_row["login_attempts"] == 5
        and stable_row["locked_until"] == original_deadline,
        f"live lock was extended or incremented: {stable_row}",
    )

    for index, base_url in enumerate(base_urls):
        response, _payload = login_from_source(
            base_url,
            username,
            password,
            f"127.10.2.{index + 1}",
        )
        require_error_code(
            response,
            "40102",
            f"live lock at {base_url}",
            expected_status=401,
        )

    with connect(database_name) as connection:
        connection.execute(
            "UPDATE users SET status = 2, login_attempts = 5, "
            "locked_until = NOW() - INTERVAL '1 second' WHERE username = %s",
            (username,),
        )
    recovered_response, _payload = login_from_source(
        base_urls[1],
        username,
        password,
        "127.10.3.1",
    )
    recovered = success_data(recovered_response, "expired legacy lock recovery")
    refresh_token = recovered.get("refresh_token")
    require(isinstance(refresh_token, str) and refresh_token, "recovery returned no refresh token")
    with connect(database_name) as connection:
        recovered_row = connection.execute(
            "SELECT status, login_attempts, locked_until FROM users WHERE username = %s",
            (username,),
        ).fetchone()
    require(
        recovered_row is not None
        and recovered_row["status"] == 1
        and recovered_row["login_attempts"] == 0
        and recovered_row["locked_until"] is None,
        f"expired legacy lock was not normalized: {recovered_row}",
    )

    with connect(database_name) as connection:
        connection.execute(
            "UPDATE users SET status = 1, login_attempts = 5, "
            "locked_until = NOW() + INTERVAL '15 minutes' WHERE username = %s",
            (username,),
        )
    locked_refresh, _payload = refresh_once(base_urls[0], refresh_token)
    require_error_code(
        locked_refresh,
        "40102",
        "temporary lock refresh",
        expected_status=401,
    )

    with connect(database_name) as connection:
        connection.execute(
            "UPDATE users SET locked_until = NOW() - INTERVAL '1 second' WHERE username = %s",
            (username,),
        )
    resumed_refresh, _payload = refresh_once(base_urls[1], refresh_token)
    resumed = success_data(resumed_refresh, "expired temporary lock refresh")
    resumed_refresh_token = resumed.get("refresh_token")
    require(
        isinstance(resumed_refresh_token, str) and resumed_refresh_token,
        "expired temporary lock refresh returned no token",
    )

    with connect(database_name) as connection:
        connection.execute(
            "UPDATE users SET status = 2, login_attempts = 0, locked_until = NULL "
            "WHERE username = %s",
            (username,),
        )
    admin_locked_login, _payload = login_from_source(
        base_urls[0],
        username,
        password,
        "127.10.4.1",
    )
    require_error_code(
        admin_locked_login,
        "40102",
        "administrator lock login",
        expected_status=401,
    )
    admin_locked_refresh, _payload = refresh_once(base_urls[1], resumed_refresh_token)
    require_error_code(
        admin_locked_refresh,
        "40102",
        "administrator lock refresh",
        expected_status=401,
    )
    with connect(database_name) as connection:
        connection.execute(
            "UPDATE users SET status = 1, login_attempts = 0, locked_until = NULL "
            "WHERE username = %s",
            (username,),
        )

    return {
        "account_lock_concurrent_requests": request_count,
        "account_lock_attempts": 5,
        "account_lock_deadline_stable": True,
        "account_lock_cross_instance": True,
        "account_lock_refresh_rejected": True,
        "account_lock_legacy_recovered": True,
        "account_lock_admin_preserved": True,
    }


def require_normalized_auth_peer_persistence(
    database_name: str,
    login_username: str,
    logout_username: str,
    expected_login_ip: str,
    expected_logout_ip: str,
) -> dict[str, Any]:
    with connect(database_name) as connection:
        login_row = connection.execute(
            "SELECT last_login_ip FROM users WHERE username = %s",
            (login_username,),
        ).fetchone()
        logout_row = connection.execute(
            "SELECT operation_logs.ip_address "
            "FROM operation_logs JOIN users ON users.id = operation_logs.user_id "
            "WHERE users.username = %s AND operation_logs.action = 'logout' "
            "ORDER BY operation_logs.id DESC LIMIT 1",
            (logout_username,),
        ).fetchone()

    require(login_row is not None, "login peer persistence user is missing")
    require(
        login_row["last_login_ip"] == expected_login_ip,
        f"last_login_ip ignored trusted proxy resolution: {login_row}",
    )
    require(logout_row is not None, "logout peer audit row is missing")
    require(
        logout_row["ip_address"] == expected_logout_ip,
        f"logout audit IP ignored trusted proxy resolution: {logout_row}",
    )
    return {
        "auth_peer_login_ip_normalized": True,
        "auth_peer_logout_ip_normalized": True,
        "auth_peer_trusted_proxy_resolved": True,
    }


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


def refresh_once(base_url: str, refresh_token: str) -> tuple[httpx.Response, dict[str, Any]]:
    response = httpx.post(
        base_url + "/api/auth/refresh",
        json={"refresh_token": refresh_token},
        timeout=15,
    )
    return response, response_envelope(response, f"refresh at {base_url}")


def concurrent_refresh(
    base_urls: tuple[str, str],
    refresh_token: str,
) -> list[tuple[httpx.Response, dict[str, Any]]]:
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        return list(pool.map(lambda url: refresh_once(url, refresh_token), base_urls))


def require_single_rotation(
    base_urls: tuple[str, str],
    refresh_token: str,
    label: str,
) -> dict[str, str]:
    results = concurrent_refresh(base_urls, refresh_token)
    winners = [payload for _response, payload in results if str(payload["code"]) == "0"]
    require(len(winners) == 1, f"{label} selected {len(winners)} refresh winners")
    data = winners[0].get("data")
    require(isinstance(data, dict), f"{label} winner returned invalid data")
    access_token = data.get("access_token")
    new_refresh_token = data.get("refresh_token")
    require(
        isinstance(access_token, str) and access_token,
        f"{label} winner returned no access token",
    )
    require(
        isinstance(new_refresh_token, str) and new_refresh_token,
        f"{label} winner returned no refresh token",
    )
    replay, replay_payload = refresh_once(base_urls[1], refresh_token)
    del replay
    require(
        str(replay_payload["code"]) != "0",
        f"{label} accepted a replay of the old refresh token",
    )
    return {"access_token": access_token, "refresh_token": new_refresh_token}


def create_folder(base_url: str, access_token: str, name: str) -> int:
    response = httpx.post(
        base_url + "/api/folder/create",
        headers=auth_headers(access_token),
        json={"name": name, "parent_id": 0},
        timeout=15,
    )
    data = success_data(response, f"create folder {name}")
    folder_id = data.get("id")
    require(isinstance(folder_id, int) and folder_id > 0, "folder has no numeric ID")
    return folder_id


def issue_share_token(
    owner_base_url: str,
    visitor_base_url: str,
    access_token: str,
    folder_id: int,
) -> tuple[str, str]:
    create = httpx.post(
        owner_base_url + "/api/share",
        headers=auth_headers(access_token),
        json={
            "file_ids": [],
            "folder_ids": [folder_id],
            "permission": "view",
            "expire_days": 7,
        },
        timeout=15,
    )
    create_data = success_data(create, "create folder share")
    share_id = create_data.get("share_id")
    require(isinstance(share_id, str) and share_id, "share has no external ID")
    access = httpx.post(
        f"{visitor_base_url}/api/share/access/{share_id}",
        json={},
        timeout=15,
    )
    access_data = success_data(access, "access folder share")
    share_token = access_data.get("share_token")
    require(isinstance(share_token, str) and share_token, "share access returned no token")
    return share_id, share_token


def require_share_browse(base_url: str, share_id: str, share_token: str) -> None:
    response = httpx.get(
        f"{base_url}/api/share/browse/{share_id}",
        headers={"X-Share-Token": share_token},
        timeout=10,
    )
    success_data(response, f"browse share at {base_url}")


def cancel_share(base_url: str, access_token: str, share_id: str) -> None:
    response = httpx.request(
        "DELETE",
        base_url + "/api/share",
        headers=auth_headers(access_token),
        json={"share_ids": [share_id]},
        timeout=15,
    )
    data = success_data(response, "cancel share on API A")
    summary = data.get("summary")
    require(
        isinstance(summary, dict) and summary.get("succeeded") == 1,
        f"share cancellation summary is invalid: {data}",
    )


def require_cancelled_share(base_url: str, share_id: str, share_token: str) -> None:
    response = httpx.get(
        f"{base_url}/api/share/browse/{share_id}",
        headers={"X-Share-Token": share_token},
        timeout=10,
    )
    payload = response_envelope(response, "cancelled share browse")
    require(str(payload["code"]) != "0", "cancelled share token remained usable")


def dependency_unready(base_url: str) -> bool:
    response = httpx.get(base_url + "/api/health/ready", timeout=3)
    if response.status_code != 503:
        return False
    payload = response_envelope(response, "Redis-unready readiness")
    data = payload.get("data")
    if not isinstance(data, dict):
        return False
    components = data.get("components")
    if not isinstance(components, dict):
        return False
    redis_component = components.get("redis")
    return isinstance(redis_component, dict) and redis_component.get("status") == "unhealthy"


def dependency_ready(base_url: str) -> bool:
    response = httpx.get(base_url + "/api/health/ready", timeout=3)
    if response.status_code != 200:
        return False
    payload = response_envelope(response, "recovered readiness")
    data = payload.get("data")
    return isinstance(data, dict) and data.get("overall_status") == "healthy"


def require_liveness(base_url: str) -> None:
    response = httpx.get(base_url + "/api/health/live", timeout=3)
    data = success_data(response, f"liveness at {base_url}")
    require(data.get("overall_status") == "healthy", "liveness is not healthy")


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
    database_name = f"disk_auth_cluster_{suffix}"
    username = f"auth_cluster_{suffix}"
    password = "ClusterPass123"
    database_created = False
    api_a: ManagedServer | None = None
    api_b: ManagedServer | None = None
    proxy: CuttableTcpProxy | None = None

    try:
        binary = resolve_current_binary()
        redis = redis_config()
        api_a_port, api_b_port = allocate_ports(2)
        proxy = CuttableTcpProxy(str(redis["host"]), int(redis["port"]))

        with (
            proxy,
            tempfile.TemporaryDirectory(prefix="disk-auth-cluster-") as temporary,
        ):
            temporary_root = Path(temporary)
            storage_root = temporary_root / "final"
            staging_root = temporary_root / "staging"

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
                redis_proxy_port=proxy.port,
            )
            api_b = start_api(
                name=API_B,
                binary=binary,
                run_directory=temporary_root / "api-b",
                database_name=database_name,
                port=api_b_port,
                storage_root=storage_root,
                staging_root=staging_root,
                redis_proxy_port=proxy.port,
            )
            base_urls = (api_a.base_url, api_b.base_url)

            login_client_ip = "198.51.100.27"
            logout_client_ip = "203.0.113.41"
            initial = register_and_login(
                api_a.base_url,
                username,
                password,
                client_ip=login_client_ip,
            )
            require_profile(api_b.base_url, initial["access_token"], username)
            account_lock_evidence = require_account_lock_consistency(
                base_urls,
                database_name,
                f"locked_{suffix}",
                password,
            )
            first_rotation = require_single_rotation(
                base_urls,
                initial["refresh_token"],
                "cross-instance refresh CAS",
            )
            pre_fault_revoked = register_and_login(
                api_a.base_url,
                f"revoked_{suffix}",
                password,
            )
            pre_fault_logout = httpx.post(
                api_a.base_url + "/api/auth/logout",
                headers={
                    **auth_headers(pre_fault_revoked["access_token"]),
                    "X-Real-IP": logout_client_ip,
                },
                timeout=15,
            )
            require_success(pre_fault_logout, "pre-fault logout on API A")
            auth_peer_evidence = require_normalized_auth_peer_persistence(
                database_name,
                username,
                f"revoked_{suffix}",
                login_client_ip,
                logout_client_ip,
            )

            cancelled_folder = create_folder(
                api_a.base_url,
                initial["access_token"],
                f"cancelled-{suffix}",
            )
            cancelled_share_id, cancelled_share_token = issue_share_token(
                api_a.base_url,
                api_b.base_url,
                initial["access_token"],
                cancelled_folder,
            )
            require_share_browse(
                api_b.base_url,
                cancelled_share_id,
                cancelled_share_token,
            )
            cancel_share(
                api_a.base_url,
                initial["access_token"],
                cancelled_share_id,
            )
            require_cancelled_share(
                api_b.base_url,
                cancelled_share_id,
                cancelled_share_token,
            )

            active_folder = create_folder(
                api_a.base_url,
                first_rotation["access_token"],
                f"active-{suffix}",
            )
            active_share_id, active_share_token = issue_share_token(
                api_a.base_url,
                api_b.base_url,
                first_rotation["access_token"],
                active_folder,
            )
            require_share_browse(api_b.base_url, active_share_id, active_share_token)

            proxy.cut()
            for base_url in base_urls:
                wait_until(
                    f"Redis-unready state at {base_url}",
                    lambda base_url=base_url: dependency_unready(base_url),
                )
                require_liveness(base_url)

                profile = httpx.get(
                    base_url + "/api/user/profile",
                    headers=auth_headers(first_rotation["access_token"]),
                    timeout=10,
                )
                require_error_code(
                    profile,
                    "70002",
                    f"profile Redis fault at {base_url}",
                    expected_status=500,
                )

                browse = httpx.get(
                    f"{base_url}/api/share/browse/{active_share_id}",
                    headers={"X-Share-Token": active_share_token},
                    timeout=10,
                )
                require_error_code(
                    browse,
                    "70002",
                    f"share Redis fault at {base_url}",
                    expected_status=500,
                )

            failed_rotations = concurrent_refresh(
                base_urls,
                first_rotation["refresh_token"],
            )
            require(
                all(
                    response.status_code == 500 and str(payload["code"]) == "70002"
                    for response, payload in failed_rotations
                ),
                "Redis outage allowed or misclassified a refresh rotation",
            )

            proxy.heal()
            for base_url in base_urls:
                wait_until(
                    f"Redis recovery at {base_url}",
                    lambda base_url=base_url: dependency_ready(base_url),
                )
            require_profile(api_b.base_url, first_rotation["access_token"], username)
            require_share_browse(api_b.base_url, active_share_id, active_share_token)
            persisted_revocation = httpx.get(
                api_b.base_url + "/api/user/profile",
                headers=auth_headers(pre_fault_revoked["access_token"]),
                timeout=10,
            )
            require_error_code(
                persisted_revocation,
                "40111",
                "pre-fault revocation after Redis recovery",
                expected_status=401,
            )
            second_rotation = require_single_rotation(
                base_urls,
                first_rotation["refresh_token"],
                "post-recovery refresh CAS",
            )

            logout = httpx.post(
                api_a.base_url + "/api/auth/logout",
                headers=auth_headers(second_rotation["access_token"]),
                timeout=15,
            )
            require_success(logout, "logout on API A")
            revoked = httpx.get(
                api_b.base_url + "/api/user/profile",
                headers=auth_headers(second_rotation["access_token"]),
                timeout=10,
            )
            require_error_code(
                revoked,
                "40111",
                "revoked token on API B",
                expected_status=401,
            )

            old_api_b_pid = api_b.pid
            api_b.stop()
            api_b = start_api(
                name=f"{API_B}-restarted",
                binary=binary,
                run_directory=temporary_root / "api-b-restarted",
                database_name=database_name,
                port=api_b_port,
                storage_root=storage_root,
                staging_root=staging_root,
                redis_proxy_port=proxy.port,
            )
            require(api_b.pid != old_api_b_pid, "API B restart reused the old process")
            revoked_after_restart = httpx.get(
                api_b.base_url + "/api/user/profile",
                headers=auth_headers(second_rotation["access_token"]),
                timeout=10,
            )
            require_error_code(
                revoked_after_restart,
                "40111",
                "revoked token after API B restart",
                expected_status=401,
            )
            pre_fault_revoked_after_restart = httpx.get(
                api_b.base_url + "/api/user/profile",
                headers=auth_headers(pre_fault_revoked["access_token"]),
                timeout=10,
            )
            require_error_code(
                pre_fault_revoked_after_restart,
                "40111",
                "pre-fault revocation after API B restart",
                expected_status=401,
            )
            require_cancelled_share(
                api_b.base_url,
                cancelled_share_id,
                cancelled_share_token,
            )
            require_share_browse(api_b.base_url, active_share_id, active_share_token)

            wait_until(
                "API A auth runtime metrics",
                lambda: auth_runtime_ready(api_a),
                timeout_seconds=5,
            )
            auth_runtime_event_count = require_auth_runtime_logs(api_a)

            write_evidence(
                {
                    "schema_version": 1,
                    "scenario": "cross_instance_token_consistency_and_redis_recovery",
                    "api_instances": 2,
                    "refresh_winners_before_fault": 1,
                    "refresh_winners_during_fault": 0,
                    "refresh_winners_after_recovery": 1,
                    "owner_revocation_cross_instance": True,
                    "share_cancellation_cross_instance": True,
                    "redis_fault_fail_closed_on_both_instances": True,
                    "redis_backed_revocation_survived_fault": True,
                    "api_b_restarted": True,
                    "revocations_survived_api_restart": True,
                    "auth_runtime_context": True,
                    "auth_runtime_event_count": auth_runtime_event_count,
                    "auth_runtime_messages_bounded": True,
                    "auth_runtime_periodic_metrics": True,
                    "shared_redis_service_stopped": False,
                    **account_lock_evidence,
                    **auth_peer_evidence,
                    "passed": True,
                }
            )

            api_b.stop()
            api_b = None
            api_a.stop()
            api_a = None
            print(
                "PASS: cross-instance account locks, refresh, revocation, restart, and "
                "Redis recovery remained consistent"
            )
        return 0
    except BaseException:
        for server in (api_a, api_b):
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
        raise
    finally:
        if proxy is not None:
            proxy.heal()
        for server in (api_b, api_a):
            if server is not None:
                server.stop()
        if database_created:
            drop_database(database_name)


if __name__ == "__main__":
    raise SystemExit(main())

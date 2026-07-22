#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""
Safety-net integration tests for upload lifecycle invariants.

These tests intentionally assert DB and filesystem side effects, not only API
responses. They enforce the distributed upload recovery contract.
"""

from __future__ import annotations

import atexit
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from pathlib import Path
from urllib.parse import urlencode

EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", ".sisyphus/evidence"))
SERVER_LOG_PATH = EVIDENCE_ROOT / "safety-upload-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    assert_db_row_absent,
    assert_equal,
    assert_numeric_delta,
    assert_path_absent,
    assert_path_exists,
    assert_storage_job_succeeded,
    ensure_server,
    cleanup,
    configured_chunk_size,
    database_config,
    do_login,
    execute,
    fetch,
    final_blob_path,
    header_value,
    json_field,
    local_blob_path,
    log_fail,
    log_info,
    log_pass,
    log_section,
    log_step,
    md5_bytes,
    print_summary,
    query_one,
    redis_delete_pattern,
    redis_get_value,
    redis_set_value,
    redis_ttl,
    save_evidence,
    scalar,
    sha256_bytes,
    unique_name,
    upload_temp_dir,
)

atexit.register(cleanup)

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
EVIDENCE_PREFIX = "safety-upload"

TOKEN = ""
USER_ID = 0
REPO_ROOT = Path(__file__).resolve().parents[2]


class PartitionableTcpProxy:
    """Relay TCP traffic and hold both directions while a test partition is active."""

    def __init__(self, target_host: str, target_port: int) -> None:
        self._target = (target_host, target_port)
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen()
        self._listener.settimeout(0.1)
        self.port = int(self._listener.getsockname()[1])
        self._stop = threading.Event()
        self._partitioned = threading.Event()
        self._client_data_blocked = threading.Event()
        self._lock = threading.Lock()
        self._sockets: set[socket.socket] = set()
        self._relay_threads: list[threading.Thread] = []
        self._blocked_client_bytes = 0
        self._accept_thread = threading.Thread(
            target=self._accept_connections,
            name="safety-postgres-proxy-accept",
            daemon=True,
        )

    def __enter__(self) -> PartitionableTcpProxy:
        self._accept_thread.start()
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.close()

    @property
    def blocked_client_bytes(self) -> int:
        with self._lock:
            return self._blocked_client_bytes

    def partition(self) -> None:
        """Hold subsequent traffic without closing established connections."""
        with self._lock:
            self._blocked_client_bytes = 0
        self._client_data_blocked.clear()
        self._partitioned.set()

    def heal(self) -> None:
        """Release held traffic over the original TCP connections."""
        self._partitioned.clear()

    def wait_for_blocked_client_data(self, timeout: float) -> bool:
        """Return whether an API-to-PostgreSQL payload reached the partition."""
        return self._client_data_blocked.wait(timeout)

    def close(self) -> None:
        """Stop accepting and relay threads."""
        self._stop.set()
        self._partitioned.clear()
        try:
            self._listener.close()
        except OSError:
            pass
        with self._lock:
            sockets = list(self._sockets)
        for connection in sockets:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            connection.close()
        if self._accept_thread.is_alive():
            self._accept_thread.join(timeout=2)
        for relay_thread in self._relay_threads:
            relay_thread.join(timeout=0.2)

    def _accept_connections(self) -> None:
        while not self._stop.is_set():
            try:
                client, _address = self._listener.accept()
            except TimeoutError:
                continue
            except OSError:
                break

            try:
                upstream = socket.create_connection(self._target, timeout=5)
            except OSError:
                client.close()
                continue

            client.settimeout(0.1)
            upstream.settimeout(0.1)
            with self._lock:
                self._sockets.update((client, upstream))
            for source, destination, client_to_server in (
                (client, upstream, True),
                (upstream, client, False),
            ):
                relay_thread = threading.Thread(
                    target=self._relay,
                    args=(source, destination, client_to_server),
                    name="safety-postgres-proxy-relay",
                    daemon=True,
                )
                self._relay_threads.append(relay_thread)
                relay_thread.start()

    def _relay(
        self,
        source: socket.socket,
        destination: socket.socket,
        client_to_server: bool,
    ) -> None:
        try:
            while not self._stop.is_set():
                try:
                    data = source.recv(65536)
                except TimeoutError:
                    continue
                except OSError:
                    break
                if not data:
                    break

                if self._partitioned.is_set():
                    if client_to_server:
                        with self._lock:
                            self._blocked_client_bytes += len(data)
                        self._client_data_blocked.set()
                    while self._partitioned.is_set() and not self._stop.wait(0.01):
                        pass
                if self._stop.is_set():
                    break
                destination.sendall(data)
        except OSError:
            pass
        finally:
            for connection in (source, destination):
                try:
                    connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                connection.close()
            with self._lock:
                self._sockets.discard(source)
                self._sockets.discard(destination)


@contextmanager
def peer_api_instance(
    *,
    purpose: str = "peer",
    upload_finalize_lease_seconds: int | None = None,
    pause_after_claim_upload_id: str | None = None,
    pause_after_assembly_upload_id: str | None = None,
    pause_after_finalize_commit_upload_id: str | None = None,
    pause_before_finalize_transaction_upload_id: str | None = None,
    finalize_transaction_release_file: Path | None = None,
    database_host: str | None = None,
    database_port: int | None = None,
) -> Iterator[tuple[str, str, subprocess.Popen[bytes]]]:
    """Run a second API process that shares the primary instance's dependencies."""
    pause_targets = [
        upload_id
        for upload_id in (
            pause_after_claim_upload_id,
            pause_after_assembly_upload_id,
            pause_after_finalize_commit_upload_id,
            pause_before_finalize_transaction_upload_id,
        )
        if upload_id is not None
    ]
    if len(pause_targets) > 1:
        raise ValueError("peer API accepts only one upload fault pause stage")
    if (database_host is None) != (database_port is None):
        raise ValueError("peer API database host and port must be overridden together")
    if pause_before_finalize_transaction_upload_id is not None and finalize_transaction_release_file is None:
        raise ValueError("finalize transaction pause requires a release file")

    server_bin = Path(
        os.environ.get("SERVER_BIN", REPO_ROOT / "build/linux-debug-clang/src/disk")
    ).resolve()
    if not server_bin.is_file() or not os.access(server_bin, os.X_OK):
        raise RuntimeError(f"peer API binary is unavailable: {server_bin}")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        peer_port = int(probe.getsockname()[1])

    instance_id = f"safety-upload-{purpose}-{os.getpid()}"
    peer_url = f"http://127.0.0.1:{peer_port}"
    peer_log_path = EVIDENCE_ROOT / f"safety-upload-{purpose}.log"
    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    source_config_path = Path(os.environ.get("DISK_CONFIG_FILE", REPO_ROOT / "config.json"))
    if not source_config_path.is_absolute():
        source_config_path = REPO_ROOT / source_config_path

    with tempfile.TemporaryDirectory(prefix=f"disk-upload-{purpose}-") as temp_dir_raw:
        temp_dir = Path(temp_dir_raw)
        config = json.loads(source_config_path.read_text(encoding="utf-8"))
        config["listeners"] = [{"address": "127.0.0.1", "port": peer_port}]
        disk_config = config["custom_config"]["disk"]
        disk_config["process_role"] = "api"
        disk_config["instance_id"] = instance_id
        if upload_finalize_lease_seconds is not None:
            disk_config["upload_finalize_lease_seconds"] = upload_finalize_lease_seconds
        if database_host is not None and database_port is not None:
            config["db_clients"][0]["host"] = database_host
            config["db_clients"][0]["port"] = database_port
            config["db_clients"][0]["connection_number"] = 4
        config_path = temp_dir / "config.json"
        config_path.write_text(json.dumps(config, indent=2), encoding="utf-8")

        peer_env = os.environ.copy()
        peer_env.pop("DISK_TEST_FAULT_INJECTION", None)
        peer_env.pop("DISK_TEST_PAUSE_AFTER_FINALIZE_CLAIM_UPLOAD_ID", None)
        peer_env.pop("DISK_TEST_PAUSE_AFTER_ASSEMBLY_UPLOAD_ID", None)
        peer_env.pop("DISK_TEST_PAUSE_AFTER_FINALIZE_COMMIT_UPLOAD_ID", None)
        peer_env.pop("DISK_TEST_PAUSE_BEFORE_FINALIZE_TRANSACTION_UPLOAD_ID", None)
        peer_env.pop("DISK_TEST_FINALIZE_TRANSACTION_RELEASE_FILE", None)
        peer_env.update(
            {
                "JWT_SECRET": peer_env.get(
                    "JWT_SECRET",
                    "dev-only-jwt-secret-key-change-in-production-2024",
                ),
                "DISK_CONFIG_FILE": str(config_path),
                "DISK_LISTEN_ADDRESS": "127.0.0.1",
                "DISK_LISTEN_PORT": str(peer_port),
                "DISK_PROCESS_ROLE": "api",
                "DISK_INSTANCE_ID": instance_id,
            }
        )
        if pause_after_claim_upload_id is not None:
            peer_env.update(
                {
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_AFTER_FINALIZE_CLAIM_UPLOAD_ID": str(
                        uuid.UUID(pause_after_claim_upload_id)
                    ),
                }
            )
        if pause_after_assembly_upload_id is not None:
            peer_env.update(
                {
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_AFTER_ASSEMBLY_UPLOAD_ID": str(
                        uuid.UUID(pause_after_assembly_upload_id)
                    ),
                }
            )
        if pause_after_finalize_commit_upload_id is not None:
            peer_env.update(
                {
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_AFTER_FINALIZE_COMMIT_UPLOAD_ID": str(
                        uuid.UUID(pause_after_finalize_commit_upload_id)
                    ),
                }
            )
        if pause_before_finalize_transaction_upload_id is not None:
            assert finalize_transaction_release_file is not None
            peer_env.update(
                {
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_BEFORE_FINALIZE_TRANSACTION_UPLOAD_ID": str(
                        uuid.UUID(pause_before_finalize_transaction_upload_id)
                    ),
                    "DISK_TEST_FINALIZE_TRANSACTION_RELEASE_FILE": str(
                        finalize_transaction_release_file
                    ),
                }
            )
        if database_host is not None and database_port is not None:
            peer_env.update(
                {
                    "DATABASE_HOST": database_host,
                    "DATABASE_PORT": str(database_port),
                    "DATABASE_POOL_SIZE": "4",
                }
            )

        with peer_log_path.open("wb") as log_handle:
            process = subprocess.Popen(
                [str(server_bin)],
                cwd=REPO_ROOT,
                env=peer_env,
                stdout=log_handle,
                stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(
                            f"peer API exited with code {process.returncode}; see {peer_log_path}"
                        )
                    try:
                        ready = fetch(f"{peer_url}/api/health/ready", timeout=2)
                        if (
                            ready.status_code == 200
                            and json_field(ready.text, "code") == "0"
                            and json_field(ready.text, "data.instance_id") == instance_id
                        ):
                            yield peer_url, instance_id, process
                            return
                    except Exception:  # noqa: BLE001 - readiness is expected to fail during startup
                        pass
                    time.sleep(0.1)
                raise RuntimeError(f"peer API did not become ready; see {peer_log_path}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


@contextmanager
def reject_chunk_metadata_insert(upload_id: str) -> Iterator[None]:
    """Reject one upload's chunk metadata after its immutable object is written."""
    normalized_upload_id = str(uuid.UUID(upload_id))
    trigger_name = "safety_chunk_insert_fail"
    function_name = "fail_safety_chunk_insert"
    execute(f"DROP TRIGGER IF EXISTS {trigger_name} ON upload_task_chunks")
    execute(f"DROP FUNCTION IF EXISTS {function_name}()")

    try:
        execute(
            f"""
            CREATE FUNCTION {function_name}() RETURNS trigger AS $$
            BEGIN
                RAISE EXCEPTION 'intentional safety chunk metadata failure';
            END;
            $$ LANGUAGE plpgsql
            """
        )
        execute(
            f"CREATE TRIGGER {trigger_name} BEFORE INSERT ON upload_task_chunks "
            f"FOR EACH ROW WHEN (NEW.task_id = '{normalized_upload_id}') "
            f"EXECUTE FUNCTION {function_name}()"
        )
        yield
    finally:
        execute(f"DROP TRIGGER IF EXISTS {trigger_name} ON upload_task_chunks")
        execute(f"DROP FUNCTION IF EXISTS {function_name}()")


def auth_headers(token: str, content_type: str = "application/json") -> dict[str, str]:
    """Return authorization headers for a test request."""
    return {"Authorization": f"Bearer {token}", "Content-Type": content_type}


def current_user_id() -> int:
    """Return the authenticated test user's database id."""
    user_id = scalar("SELECT id FROM users WHERE username = %s OR email = %s LIMIT 1", (TEST_USER, TEST_USER))
    if user_id is None:
        log_fail(f"Could not resolve test user id for {TEST_USER}")
        print_summary()
    return int(user_id)


def user_quota() -> dict[str, int]:
    """Return current storage quota counters for the test user."""
    row = query_one(
        "SELECT storage_used, storage_reserved, storage_quota FROM users WHERE id = %s",
        (USER_ID,),
    )
    if row is None:
        log_fail(f"Could not load user quota for user_id={USER_ID}")
        print_summary()
    return {key: int(row[key]) for key in ("storage_used", "storage_reserved", "storage_quota")}


def user_quota_balance() -> dict[str, int]:
    """Return quota counters and their active-upload reservation residual."""
    row = query_one(
        """
        SELECT users.storage_used, users.storage_reserved, users.storage_quota,
               COALESCE(SUM(tasks.reserved_bytes), 0) AS active_reserved
        FROM users
        LEFT JOIN upload_tasks AS tasks
          ON tasks.user_id = users.id AND tasks.status IN (0, 4)
        WHERE users.id = %s
        GROUP BY users.id, users.storage_used, users.storage_reserved, users.storage_quota
        """,
        (USER_ID,),
    )
    if row is None:
        log_fail(f"Could not load quota balance for user_id={USER_ID}")
        print_summary()
    balance = {
        key: int(row[key])
        for key in ("storage_used", "storage_reserved", "storage_quota", "active_reserved")
    }
    balance["reservation_residual"] = balance["storage_reserved"] - balance["active_reserved"]
    return balance


def init_upload(filename: str, payload: bytes) -> tuple[str, str]:
    """Initialize a non-dedup chunked upload and return upload_id and file hash."""
    file_hash = md5_bytes(payload)
    resp = fetch(
        "/api/file/upload/init",
        method="POST",
        headers=auth_headers(TOKEN),
        json_body={
            "filename": filename,
            "file_size": len(payload),
            "file_hash": file_hash,
            "parent_id": 0,
        },
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{filename}-init.json", resp.text)

    if json_field(resp.text, "data.instant_upload") == "true":
        log_fail(f"{filename}: expected chunked upload but got instant_upload=true")
        print(resp.text)
        print_summary()

    upload_id = json_field(resp.text, "data.upload_id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not upload_id:
        log_fail(f"{filename}: init upload failed")
        print(resp.text)
        print_summary()

    return upload_id, file_hash


def upload_single_chunk_raw(upload_id: str, payload: bytes, evidence_suffix: str = "chunk"):
    """Upload a single chunk and return its response without asserting success."""
    resp = fetch(
        f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={md5_bytes(payload)}",
        method="POST",
        headers=auth_headers(TOKEN, "application/octet-stream"),
        data=payload,
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-{evidence_suffix}.json", resp.text)
    return resp


def upload_single_chunk(upload_id: str, payload: bytes) -> None:
    """Upload a single chunk with the payload's MD5 hash."""
    resp = upload_single_chunk_raw(upload_id, payload)
    if resp.status_code != 200 or json_field(resp.text, "data.uploaded") != "true":
        log_fail(f"{upload_id}: chunk upload failed")
        print(resp.text)
        print_summary()


def complete_upload(upload_id: str) -> str:
    """Complete an upload and return the created file id."""
    resp = complete_upload_raw(upload_id)
    file_id = json_field(resp.text, "data.file.id")
    if resp.status_code != 200 or json_field(resp.text, "code") != "0" or not file_id:
        log_fail(f"{upload_id}: complete upload failed")
        print(resp.text)
        print_summary()
    return file_id


def complete_upload_raw(upload_id: str, request_id: str | None = None):
    """Call complete upload and return the raw response without asserting success."""
    headers = auth_headers(TOKEN)
    if request_id is not None:
        headers["X-Request-Id"] = request_id
    resp = fetch(
        "/api/file/upload/complete",
        method="POST",
        headers=headers,
        json_body={"upload_id": upload_id},
    )
    save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-complete.json", resp.text)
    return resp


def root_file_names(base_url: str = BASE_URL) -> list[str]:
    """Read the cached root-file listing and return its visible names."""
    query = urlencode(
        {
            "parent_id": 0,
            "page": 1,
            "page_size": 100,
            "sort_by": "created_at",
            "sort_order": "desc",
            "type": "file",
        }
    )
    response = fetch(
        f"{base_url}/api/file/list?{query}",
        headers=auth_headers(TOKEN),
    )
    try:
        payload = json.loads(response.text)
    except json.JSONDecodeError:
        log_fail("root file list returns valid JSON")
        print(response.text)
        print_summary()

    if response.status_code != 200 or str(payload.get("code")) != "0":
        log_fail("root file list request succeeds")
        print(response.text)
        print_summary()

    items = payload.get("data", {}).get("items", [])
    if not isinstance(items, list):
        log_fail("root file list response contains an items array")
        print_summary()
    return [str(item.get("name", "")) for item in items if isinstance(item, dict)]


def wait_for_request_log(request_id: str, instance_id: str, status: int) -> str:
    """Return the exact completion log line for one failed request."""
    markers = (
        f"request_id={request_id}",
        f"instance_id={instance_id}",
        "operation=upload_complete",
        f"status={status}",
    )
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if SERVER_LOG_PATH.is_file():
            log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
            for line in log_text.splitlines():
                if all(marker in line for marker in markers):
                    return line
        time.sleep(0.05)

    log_fail(f"request completion log contains correlation tuple for {request_id}")
    print_summary()
    raise AssertionError("unreachable")


def wait_for_correlated_application_log(
    *,
    request_id: str,
    instance_id: str,
    operation: str,
    upload_id: str | None,
    message_marker: str,
    job_id: int | None = None,
    lease_owner: str | None = None,
    state_version: int | None = None,
) -> dict[str, object]:
    """Return one schema-v1 application event with exact typed correlation."""
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if SERVER_LOG_PATH.is_file():
            for line in SERVER_LOG_PATH.read_text(
                encoding="utf-8",
                errors="replace",
            ).splitlines():
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if (
                    record.get("schema_version") == 1
                    and record.get("source") == "application"
                    and record.get("request_id") == request_id
                    and record.get("instance_id") == instance_id
                    and record.get("operation") == operation
                    and record.get("upload_id") == upload_id
                    and record.get("job_id") == job_id
                    and record.get("lease_owner") == lease_owner
                    and record.get("state_version") == state_version
                    and message_marker in str(record.get("message", ""))
                ):
                    return record
        time.sleep(0.05)

    log_fail(
        f"structured log contains {operation} correlation for request_id={request_id}"
    )
    print_summary()
    raise AssertionError("unreachable")


def assert_no_unscoped_application_log(
    message: str,
    *,
    exact: bool = True,
    assertion: str = "shared DTO validation emits no unscoped duplicate",
) -> None:
    """Assert an application boundary did not emit an uncorrelated duplicate."""
    records: list[dict[str, object]] = []
    if SERVER_LOG_PATH.is_file():
        for line in SERVER_LOG_PATH.read_text(
            encoding="utf-8",
            errors="replace",
        ).splitlines():
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(record, dict):
                records.append(record)

    duplicates = [
        record
        for record in records
        if record.get("source") == "application"
        and record.get("request_id") is None
        and (
            record.get("message") == message
            if exact
            else message in str(record.get("message", ""))
        )
    ]
    assert_equal(assertion, len(duplicates), 0)


def assert_file_query_log_context(
    *,
    response,
    request_id: str,
    message_markers: tuple[str, ...],
) -> None:
    """Assert one failed file query keeps typed correlation through its visible events."""
    assert_equal(
        "file query returns a documented failure",
        response.status_code >= 400 and json_field(response.text, "code") != "0",
        True,
    )
    assert_equal(
        "file query preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("file query identifies the handling instance", bool(instance_id), True)

    for marker in (*message_markers, "HTTP request completed"):
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="file_query",
            upload_id=None,
            message_marker=marker,
        )
    log_pass("file query logs keep typed request correlation and null ownership fields")


def assert_file_mutation_log_context(
    *,
    response,
    request_id: str,
    message_markers: tuple[str, ...],
) -> None:
    """Assert one failed file mutation keeps typed correlation through its subflow."""
    assert_equal(
        "file mutation returns a documented failure",
        response.status_code >= 400 and json_field(response.text, "code") != "0",
        True,
    )
    assert_equal(
        "file mutation preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("file mutation identifies the handling instance", bool(instance_id), True)

    for marker in (*message_markers, "HTTP request completed"):
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="file_mutation",
            upload_id=None,
            message_marker=marker,
        )
    log_pass("file mutation logs keep typed request correlation and null ownership fields")


def assert_folder_log_context(
    *,
    response,
    request_id: str,
    operation: str,
    message_markers: tuple[str, ...],
) -> None:
    """Assert one failed folder request keeps its bounded typed correlation."""
    assert_equal(
        "folder request returns a documented failure",
        response.status_code >= 400 and json_field(response.text, "code") != "0",
        True,
    )
    assert_equal(
        "folder request preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("folder request identifies the handling instance", bool(instance_id), True)

    for marker in (*message_markers, "HTTP request completed"):
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation=operation,
            upload_id=None,
            message_marker=marker,
        )
    log_pass("folder logs keep typed request correlation and null ownership fields")


def assert_trash_log_context(
    *,
    response,
    request_id: str,
    message_markers: tuple[str, ...],
    expected_item_status: str | None = None,
) -> None:
    """Assert one 2xx trash request keeps bounded application correlation."""
    assert_equal(
        "trash request preserves its documented success envelope",
        response.status_code == 200 and json_field(response.text, "code") == "0",
        True,
    )
    if expected_item_status is not None:
        assert_equal(
            "missing trash item is reported inside the batch result",
            json_field(response.text, "data.results.0.status"),
            expected_item_status,
        )
    assert_equal(
        "trash request preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("trash request identifies the handling instance", bool(instance_id), True)

    for marker in message_markers:
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="trash",
            upload_id=None,
            message_marker=marker,
        )
    log_pass("trash logs keep typed request correlation and null ownership fields")


def assert_user_log_context(
    *,
    response,
    request_id: str,
    expected_success: bool,
    message_markers: tuple[str, ...],
) -> None:
    """Assert one user request keeps its bounded typed correlation."""
    if expected_success:
        assert_equal(
            "user request preserves its documented success envelope",
            response.status_code == 200 and json_field(response.text, "code") == "0",
            True,
        )
    else:
        assert_equal(
            "user request returns a documented failure",
            response.status_code >= 400 and json_field(response.text, "code") != "0",
            True,
        )
    assert_equal(
        "user request preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("user request identifies the handling instance", bool(instance_id), True)

    for marker in message_markers:
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="user",
            upload_id=None,
            message_marker=marker,
        )
    log_pass("user logs keep typed request correlation and null ownership fields")


def assert_auth_log_context(
    *,
    response,
    request_id: str,
    expected_success: bool,
    message_markers: tuple[str, ...],
) -> None:
    """Assert one authentication request keeps its bounded typed correlation."""
    if expected_success:
        assert_equal(
            "authentication request preserves its documented success envelope",
            response.status_code == 200 and json_field(response.text, "code") == "0",
            True,
        )
    else:
        assert_equal(
            "authentication request returns a documented failure",
            response.status_code >= 400 and json_field(response.text, "code") != "0",
            True,
        )
    assert_equal(
        "authentication request preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("authentication request identifies the handling instance", bool(instance_id), True)

    for marker in message_markers:
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="auth",
            upload_id=None,
            message_marker=marker,
        )
    log_pass("authentication logs keep typed request correlation and null ownership fields")


def assert_auth_filter_rejection_log_context(
    *,
    response,
    request_id: str,
    operation: str,
    message_marker: str,
) -> None:
    """Assert one authentication-filter rejection keeps bounded correlation."""
    assert_equal(
        "authentication filter returns a documented rejection",
        response.status_code >= 400 and json_field(response.text, "code") != "0",
        True,
    )
    assert_equal(
        "authentication filter preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("authentication filter identifies the handling instance", bool(instance_id), True)
    wait_for_correlated_application_log(
        request_id=request_id,
        instance_id=instance_id,
        operation=operation,
        upload_id=None,
        message_marker=message_marker,
    )
    log_pass("authentication filter log keeps typed correlation and null ownership fields")


def assert_share_log_context(
    *,
    response,
    request_id: str,
    expected_success: bool,
    message_markers: tuple[str, ...],
) -> None:
    """Assert one share request keeps its bounded typed correlation."""
    if expected_success:
        assert_equal(
            "share request preserves its documented success envelope",
            response.status_code == 200 and json_field(response.text, "code") == "0",
            True,
        )
    else:
        assert_equal(
            "share request returns a documented failure",
            response.status_code >= 400 and json_field(response.text, "code") != "0",
            True,
        )
    assert_equal(
        "share request preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("share request identifies the handling instance", bool(instance_id), True)

    for marker in message_markers:
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="share",
            upload_id=None,
            message_marker=marker,
        )
    log_pass("share logs keep typed request correlation and null ownership fields")


def assert_share_audit_correlation(
    *,
    action: str,
    share_code: str,
    request_id: str,
    forbidden_values: tuple[str, ...],
) -> None:
    """Assert one share audit row persists bounded correlation without credentials."""
    row = query_one(
        """
        SELECT details
        FROM operation_logs
        WHERE action = %s AND target_type = 'share' AND target_name = %s
        ORDER BY id DESC
        LIMIT 1
        """,
        (action, share_code),
    )
    assert_equal(f"{action} audit row exists", row is not None, True)
    details = row["details"] if row is not None else {}
    if isinstance(details, str):
        details = json.loads(details)

    assert_equal(f"{action} audit preserves request ID", details.get("request_id"), request_id)
    assert_equal(f"{action} audit preserves operation", details.get("operation"), "share")
    for forbidden_key in (
        "password",
        "password_hash",
        "share_token",
        "authorization",
        "x-share-token",
    ):
        assert_equal(
            f"{action} audit excludes {forbidden_key}",
            forbidden_key not in details,
            True,
        )
    serialized_details = json.dumps(details, default=str)
    for forbidden_value in forbidden_values:
        assert_equal(
            f"{action} audit excludes a raw credential",
            bool(forbidden_value) and forbidden_value not in serialized_details,
            True,
        )
    log_pass(f"{action} audit keeps typed request correlation without credentials")


def assert_admin_log_context(
    *,
    response,
    request_id: str,
    expected_success: bool,
    message_markers: tuple[str, ...],
) -> None:
    """Assert one core administration request keeps typed correlation."""
    if expected_success:
        assert_equal(
            "administration request preserves its documented success envelope",
            response.status_code == 200 and json_field(response.text, "code") == "0",
            True,
        )
    else:
        assert_equal(
            "administration request returns a documented failure",
            response.status_code >= 400 and json_field(response.text, "code") != "0",
            True,
        )
    assert_equal(
        "administration request preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("administration request identifies the handling instance", bool(instance_id), True)

    for marker in message_markers:
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="admin",
            upload_id=None,
            message_marker=marker,
        )
    log_pass("administration logs keep typed request correlation and null ownership fields")


def assert_admin_audit_correlation(*, action: str, request_id: str) -> None:
    """Assert one administrator audit row uses the actor and request context."""
    row = query_one(
        """
        SELECT user_id, details
        FROM operation_logs
        WHERE action = %s AND details ->> 'request_id' = %s
        ORDER BY id DESC
        LIMIT 1
        """,
        (action, request_id),
    )
    assert_equal(f"{action} administration audit row exists", row is not None, True)
    assert_equal(
        f"{action} administration audit uses the authenticated actor",
        int(row["user_id"]) if row is not None else 0,
        USER_ID,
    )
    details = row["details"] if row is not None else {}
    if isinstance(details, str):
        details = json.loads(details)

    assert_equal(f"{action} audit preserves request ID", details.get("request_id"), request_id)
    assert_equal(f"{action} audit preserves operation", details.get("operation"), "admin")
    for forbidden_key in (
        "authorization",
        "jwt",
        "password",
        "password_hash",
        "share_token",
        "storage_credential",
    ):
        assert_equal(f"{action} audit excludes {forbidden_key}", forbidden_key not in details, True)
    assert_equal(
        f"{action} audit excludes the raw administrator token",
        TOKEN not in json.dumps(details, default=str),
        True,
    )
    log_pass(f"{action} audit keeps the actor and typed request correlation")


def assert_server_log_excludes_secrets(secrets: tuple[str, ...]) -> None:
    """Assert raw authentication credentials never reached managed API stdout."""
    log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
    for secret in secrets:
        assert_equal(
            "managed authentication logs exclude a raw credential",
            bool(secret) and secret not in log_text,
            True,
        )
    log_pass("authentication logs exclude passwords and raw access/refresh tokens")


def test_file_query_log_context_invariants() -> None:
    """Verify file list, numeric detail, and search use one bounded query operation."""
    log_section("File Query Structured Log Correlation")
    missing_id = 999_999_999_999

    list_request_id = f"safety-file-list-log-{unique_name()}"
    list_response = fetch(
        f"/api/file/list?parent_id={missing_id}&page=1&page_size=20",
        headers={**auth_headers(TOKEN), "X-Request-Id": list_request_id},
    )
    assert_file_query_log_context(
        response=list_response,
        request_id=list_request_id,
        message_markers=(
            "Folder not found or no permission",
            "Get file list failed",
        ),
    )

    detail_request_id = f"safety-file-detail-log-{unique_name()}"
    detail_response = fetch(
        f"/api/file/{missing_id}",
        headers={**auth_headers(TOKEN), "X-Request-Id": detail_request_id},
    )
    assert_file_query_log_context(
        response=detail_response,
        request_id=detail_request_id,
        message_markers=(
            "File not found or no permission",
            "Get file detail failed",
        ),
    )

    search_request_id = f"safety-file-search-log-{unique_name()}"
    search_response = fetch(
        "/api/file/search?keyword=correlation&page=0",
        headers={**auth_headers(TOKEN), "X-Request-Id": search_request_id},
    )
    assert_file_query_log_context(
        response=search_response,
        request_id=search_request_id,
        message_markers=("File search request parameter validation failed",),
    )


def test_file_mutation_log_context_invariants() -> None:
    """Verify rename, move, copy, and soft-delete share one bounded operation."""
    log_section("File Mutation Structured Log Correlation")
    missing_id = 999_999_999_999

    rename_request_id = f"safety-file-rename-log-{unique_name()}"
    rename_response = fetch(
        f"/api/file/{missing_id}/rename",
        method="PUT",
        headers={**auth_headers(TOKEN), "X-Request-Id": rename_request_id},
        json_body={"new_name": "missing-renamed.bin"},
    )
    assert_file_mutation_log_context(
        response=rename_response,
        request_id=rename_request_id,
        message_markers=(
            "Rename target file not found or no permission",
            "Rename failed",
        ),
    )

    move_request_id = f"safety-file-move-log-{unique_name()}"
    move_response = fetch(
        "/api/file/move",
        method="PUT",
        headers={**auth_headers(TOKEN), "X-Request-Id": move_request_id},
        json_body={"file_ids": [missing_id], "target_folder_id": missing_id},
    )
    assert_file_mutation_log_context(
        response=move_response,
        request_id=move_request_id,
        message_markers=(
            "Move target folder not found or no permission",
            "Move file failed",
        ),
    )

    copy_request_id = f"safety-file-copy-log-{unique_name()}"
    copy_response = fetch(
        "/api/file/copy",
        method="POST",
        headers={**auth_headers(TOKEN), "X-Request-Id": copy_request_id},
        json_body={"file_ids": [missing_id], "target_folder_id": missing_id},
    )
    assert_file_mutation_log_context(
        response=copy_response,
        request_id=copy_request_id,
        message_markers=(
            "Target folder not found or no permission",
            "Copy file failed",
        ),
    )

    delete_request_id = f"safety-file-delete-log-{unique_name()}"
    delete_response = fetch(
        "/api/file",
        method="DELETE",
        headers={**auth_headers(TOKEN), "X-Request-Id": delete_request_id},
        json_body={"file_ids": [missing_id]},
    )
    assert_file_mutation_log_context(
        response=delete_response,
        request_id=delete_request_id,
        message_markers=(
            "File not found or delete failed, skipping",
            "Move-to-trash failed",
            "Delete file failed",
        ),
    )


def test_folder_log_context_invariants() -> None:
    """Verify folder query and mutation routes use two bounded operations."""
    log_section("Folder Structured Log Correlation")
    missing_id = 999_999_999_999

    tree_request_id = f"safety-folder-tree-log-{unique_name()}"
    tree_response = fetch(
        f"/api/folder/tree?parent_id={missing_id}&depth=1",
        headers={**auth_headers(TOKEN), "X-Request-Id": tree_request_id},
    )
    assert_folder_log_context(
        response=tree_response,
        request_id=tree_request_id,
        operation="folder_query",
        message_markers=(
            "Parent folder does not exist or belongs to another user",
            "Get folder tree failed",
        ),
    )

    breadcrumb_request_id = f"safety-folder-breadcrumb-log-{unique_name()}"
    breadcrumb_response = fetch(
        f"/api/folder/{missing_id}/breadcrumb",
        headers={**auth_headers(TOKEN), "X-Request-Id": breadcrumb_request_id},
    )
    assert_folder_log_context(
        response=breadcrumb_response,
        request_id=breadcrumb_request_id,
        operation="folder_query",
        message_markers=(
            "Breadcrumb folder not found or no permission",
            "Get breadcrumb failed",
        ),
    )

    create_request_id = f"safety-folder-create-log-{unique_name()}"
    create_response = fetch(
        "/api/folder/create",
        method="POST",
        headers={**auth_headers(TOKEN), "X-Request-Id": create_request_id},
        json_body={"name": f"missing-parent-{unique_name()}", "parent_id": missing_id},
    )
    assert_folder_log_context(
        response=create_response,
        request_id=create_request_id,
        operation="folder_mutation",
        message_markers=(
            "Parent folder does not exist or belongs to another user",
            "Create folder failed",
        ),
    )

    rename_request_id = f"safety-folder-rename-log-{unique_name()}"
    rename_response = fetch(
        f"/api/folder/{missing_id}/rename",
        method="PUT",
        headers={**auth_headers(TOKEN), "X-Request-Id": rename_request_id},
        json_body={"new_name": f"missing-renamed-{unique_name()}"},
    )
    assert_folder_log_context(
        response=rename_response,
        request_id=rename_request_id,
        operation="folder_mutation",
        message_markers=(
            "Folder not found or no permission",
            "Rename folder failed",
        ),
    )


def test_trash_log_context_invariants() -> None:
    """Verify exact trash paths share one bounded non-ownership operation."""
    log_section("Trash Structured Log Correlation")
    missing_id = 999_999_999_999

    list_request_id = f"safety-trash-list-log-{unique_name()}"
    list_response = fetch(
        "/api/trash?page=1&page_size=1",
        headers={**auth_headers(TOKEN), "X-Request-Id": list_request_id},
    )
    assert_trash_log_context(
        response=list_response,
        request_id=list_request_id,
        message_markers=(
            "Received get trash list request",
            "Fetching trash list",
        ),
    )

    restore_request_id = f"safety-trash-restore-log-{unique_name()}"
    restore_response = fetch(
        "/api/trash/restore",
        method="POST",
        headers={**auth_headers(TOKEN), "X-Request-Id": restore_request_id},
        json_body={"trash_ids": [missing_id]},
    )
    assert_trash_log_context(
        response=restore_response,
        request_id=restore_request_id,
        expected_item_status="failed",
        message_markers=(
            "Received batch restore request",
            "Batch restoring trash items",
        ),
    )

    delete_request_id = f"safety-trash-delete-log-{unique_name()}"
    delete_response = fetch(
        "/api/trash/delete",
        method="POST",
        headers={**auth_headers(TOKEN), "X-Request-Id": delete_request_id},
        json_body={"trash_ids": [missing_id]},
    )
    assert_trash_log_context(
        response=delete_response,
        request_id=delete_request_id,
        expected_item_status="failed",
        message_markers=(
            "Received batch permanent delete request",
            "Batch permanently deleting trash items",
        ),
    )

    invalid_restore_request_id = f"safety-trash-invalid-restore-log-{unique_name()}"
    invalid_restore_response = fetch(
        "/api/trash/restore",
        method="POST",
        headers={**auth_headers(TOKEN), "X-Request-Id": invalid_restore_request_id},
        json_body={"trash_ids": []},
    )
    assert_equal(
        "invalid trash restore is rejected",
        invalid_restore_response.status_code >= 400
        and json_field(invalid_restore_response.text, "code") != "0",
        True,
    )
    assert_equal(
        "invalid trash restore preserves caller request ID",
        header_value(invalid_restore_response.headers, "X-Request-Id"),
        invalid_restore_request_id,
    )
    invalid_restore_instance_id = header_value(
        invalid_restore_response.headers,
        "X-Disk-Instance-Id",
    )
    assert_equal(
        "invalid trash restore identifies the handling instance",
        bool(invalid_restore_instance_id),
        True,
    )
    for marker in (
        "Received batch restore request",
        "Batch restore request parameter validation failed: Parameter 'trash_ids' cannot be empty array",
        "HTTP request completed",
    ):
        wait_for_correlated_application_log(
            request_id=invalid_restore_request_id,
            instance_id=invalid_restore_instance_id,
            operation="trash",
            upload_id=None,
            message_marker=marker,
        )
    assert_no_unscoped_application_log("Parameter 'trash_ids' cannot be empty array")
    log_pass("trash HTTP failure completion keeps typed request correlation")


def test_system_info_log_context_invariants() -> None:
    """Verify exact system info requests use one bounded non-ownership operation."""
    log_section("System Info Structured Log Correlation")

    request_id = f"safety-system-info-log-{unique_name()}"
    response = fetch(
        "/api/system/info",
        headers={**auth_headers(TOKEN), "X-Request-Id": request_id},
    )
    assert_equal(
        "authenticated system info preserves its success envelope",
        response.status_code == 200 and json_field(response.text, "code") == "0",
        True,
    )
    assert_equal(
        "authenticated system info preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal(
        "authenticated system info identifies the handling instance",
        bool(instance_id),
        True,
    )
    wait_for_correlated_application_log(
        request_id=request_id,
        instance_id=instance_id,
        operation="system_info",
        upload_id=None,
        message_marker="[stage_timer] system_get_info",
    )
    log_pass("system info stage log keeps typed request correlation and null ownership")

    unauthenticated_request_id = f"safety-system-info-unauth-log-{unique_name()}"
    unauthenticated_response = fetch(
        "/api/system/info",
        headers={"X-Request-Id": unauthenticated_request_id},
    )
    assert_equal(
        "unauthenticated system info is rejected",
        unauthenticated_response.status_code == 401
        and json_field(unauthenticated_response.text, "code") != "0",
        True,
    )
    assert_equal(
        "unauthenticated system info preserves caller request ID",
        header_value(unauthenticated_response.headers, "X-Request-Id"),
        unauthenticated_request_id,
    )
    unauthenticated_instance_id = header_value(
        unauthenticated_response.headers,
        "X-Disk-Instance-Id",
    )
    assert_equal(
        "unauthenticated system info identifies the handling instance",
        bool(unauthenticated_instance_id),
        True,
    )
    wait_for_correlated_application_log(
        request_id=unauthenticated_request_id,
        instance_id=unauthenticated_instance_id,
        operation="system_info",
        upload_id=None,
        message_marker="HTTP request completed",
    )
    log_pass("system info HTTP failure completion keeps typed request correlation")


def test_operation_log_context_invariants() -> None:
    """Verify the exact current-user log route uses one bounded operation."""
    log_section("Operation Log Structured Correlation")

    request_id = f"safety-operation-log-{unique_name()}"
    response = fetch(
        "/api/logs?page=1&page_size=1",
        headers={**auth_headers(TOKEN), "X-Request-Id": request_id},
    )
    assert_equal(
        "operation log query preserves its success envelope",
        response.status_code == 200 and json_field(response.text, "code") == "0",
        True,
    )
    assert_equal(
        "operation log query preserves caller request ID",
        header_value(response.headers, "X-Request-Id"),
        request_id,
    )
    instance_id = header_value(response.headers, "X-Disk-Instance-Id")
    assert_equal("operation log query identifies the handling instance", bool(instance_id), True)
    wait_for_correlated_application_log(
        request_id=request_id,
        instance_id=instance_id,
        operation="operation_log",
        upload_id=None,
        message_marker="Received operation log list request",
    )
    log_pass("operation log controller keeps typed request correlation")

    unauthenticated_request_id = f"safety-operation-log-unauth-{unique_name()}"
    unauthenticated_response = fetch(
        "/api/logs",
        headers={"X-Request-Id": unauthenticated_request_id},
    )
    assert_equal(
        "unauthenticated operation log query is rejected",
        unauthenticated_response.status_code == 401
        and json_field(unauthenticated_response.text, "code") != "0",
        True,
    )
    assert_equal(
        "unauthenticated operation log query preserves caller request ID",
        header_value(unauthenticated_response.headers, "X-Request-Id"),
        unauthenticated_request_id,
    )
    unauthenticated_instance_id = header_value(
        unauthenticated_response.headers,
        "X-Disk-Instance-Id",
    )
    assert_equal(
        "unauthenticated operation log query identifies the handling instance",
        bool(unauthenticated_instance_id),
        True,
    )
    wait_for_correlated_application_log(
        request_id=unauthenticated_request_id,
        instance_id=unauthenticated_instance_id,
        operation="operation_log",
        upload_id=None,
        message_marker="HTTP request completed",
    )
    log_pass("operation log HTTP rejection keeps typed request correlation")


def test_user_log_context_invariants() -> None:
    """Verify exact user profile paths share one bounded non-ownership operation."""
    log_section("User Structured Log Correlation")

    profile_request_id = f"safety-user-profile-log-{unique_name()}"
    profile_response = fetch(
        "/api/user/profile",
        headers={**auth_headers(TOKEN), "X-Request-Id": profile_request_id},
    )
    assert_user_log_context(
        response=profile_response,
        request_id=profile_request_id,
        expected_success=True,
        message_markers=(
            "Received user info request",
            "Get user profile request",
            "Get user profile successful",
            "User info retrieved successfully",
        ),
    )

    update_request_id = f"safety-user-update-log-{unique_name()}"
    update_response = fetch(
        "/api/user/profile",
        method="PATCH",
        headers={**auth_headers(TOKEN), "X-Request-Id": update_request_id},
        json_body={},
    )
    assert_user_log_context(
        response=update_response,
        request_id=update_request_id,
        expected_success=False,
        message_markers=(
            "Received profile update request",
            "At least one field must be provided",
            "Profile update request validation failed",
            "HTTP request completed",
        ),
    )

    password_request_id = f"safety-user-password-log-{unique_name()}"
    password_response = fetch(
        "/api/user/password",
        method="PUT",
        headers={**auth_headers(TOKEN), "X-Request-Id": password_request_id},
        json_body={"old_password": "UnusedOld123", "new_password": "short"},
    )
    assert_user_log_context(
        response=password_response,
        request_id=password_request_id,
        expected_success=False,
        message_markers=(
            "Received change password request",
            "New password format error",
            "Change password request validation failed",
            "HTTP request completed",
        ),
    )

    storage_request_id = f"safety-user-storage-log-{unique_name()}"
    storage_response = fetch(
        "/api/user/storage",
        headers={**auth_headers(TOKEN), "X-Request-Id": storage_request_id},
    )
    assert_user_log_context(
        response=storage_response,
        request_id=storage_request_id,
        expected_success=True,
        message_markers=(
            "Received storage stats request",
            "Get storage stats successful",
        ),
    )


def test_auth_log_context_invariants() -> None:
    """Verify the four exact authentication paths share one bounded operation."""
    log_section("Authentication Structured Log Correlation")
    username = scalar("SELECT username FROM users WHERE id = %s", (USER_ID,))
    assert_equal("authentication fixture resolves the current username", bool(username), True)
    register_password = "SafetyRegister123"

    redis_delete_pattern("rate:register:*")
    register_request_id = f"safety-auth-register-log-{unique_name()}"
    register_response = fetch(
        "/api/auth/register",
        method="POST",
        headers={"Content-Type": "application/json", "X-Request-Id": register_request_id},
        json_body={
            "username": str(username),
            "email": f"safety-auth-{unique_name()}@example.com",
            "password": register_password,
        },
    )
    assert_auth_log_context(
        response=register_response,
        request_id=register_request_id,
        expected_success=False,
        message_markers=(
            "Received user registration request",
            "Username already exists",
            "User registration business logic failed",
            "HTTP request completed",
        ),
    )

    config_path = Path(os.environ.get("DISK_CONFIG_FILE", REPO_ROOT / "config.json"))
    if not config_path.is_absolute():
        config_path = REPO_ROOT / config_path
    config = json.loads(config_path.read_text(encoding="utf-8"))
    disk_config = config.get("custom_config", {}).get("disk", {})
    register_limit = int(disk_config.get("register_rate_limit_per_window", 5))
    if register_limit <= 0:
        register_limit = 5
    register_window_seconds = int(
        disk_config.get("register_rate_limit_window_seconds", 300)
    )
    if register_window_seconds <= 0:
        register_window_seconds = 300

    now = time.time()
    window_start = (int(now) // register_window_seconds) * register_window_seconds
    seconds_until_reset = window_start + register_window_seconds - now
    if seconds_until_reset < 2:
        time.sleep(seconds_until_reset + 0.05)
        now = time.time()
        window_start = (int(now) // register_window_seconds) * register_window_seconds

    register_rate_key = f"rate:register:127.0.0.1:{window_start}"
    limited_register_password = "SafetyRegisterLimit123"
    limited_register_request_id = f"safety-register-rate-log-{unique_name()}"
    try:
        redis_set_value(
            register_rate_key,
            str(register_limit),
            max(1, window_start + register_window_seconds - int(time.time())),
        )
        limited_register_response = fetch(
            "/api/auth/register",
            method="POST",
            headers={
                "Content-Type": "application/json",
                "X-Request-Id": limited_register_request_id,
            },
            json_body={
                "username": f"limited-{unique_name()}",
                "email": f"limited-{unique_name()}@example.com",
                "password": limited_register_password,
            },
        )
        assert_equal(
            "register rate boundary returns HTTP 429",
            limited_register_response.status_code,
            429,
        )
        assert_equal(
            "register rate boundary returns code 10005",
            json_field(limited_register_response.text, "code"),
            "10005",
        )
        assert_equal(
            "register rate boundary returns the configured limit",
            header_value(limited_register_response.headers, "X-RateLimit-Limit"),
            str(register_limit),
        )
        assert_equal(
            "register rate boundary reports no remaining requests",
            header_value(limited_register_response.headers, "X-RateLimit-Remaining"),
            "0",
        )
        assert_equal(
            "register rate boundary returns a reset timestamp",
            bool(header_value(limited_register_response.headers, "X-RateLimit-Reset")),
            True,
        )
        assert_equal(
            "register rate boundary returns Retry-After",
            bool(header_value(limited_register_response.headers, "Retry-After")),
            True,
        )
        assert_equal(
            "register rate boundary preserves caller request ID",
            header_value(limited_register_response.headers, "X-Request-Id"),
            limited_register_request_id,
        )
        limited_register_instance_id = header_value(
            limited_register_response.headers,
            "X-Disk-Instance-Id",
        )
        assert_equal(
            "register rate boundary identifies the handling instance",
            bool(limited_register_instance_id),
            True,
        )
        limited_register_log = wait_for_correlated_application_log(
            request_id=limited_register_request_id,
            instance_id=limited_register_instance_id,
            operation="auth",
            upload_id=None,
            message_marker="Register rate limit:",
        )
        assert_equal(
            "register rate rejection uses warning level",
            limited_register_log.get("level"),
            "warning",
        )
        log_pass("register rate-limit rejection keeps bounded request correlation")
    finally:
        redis_delete_pattern("rate:register:*")

    login_request_id = f"safety-auth-login-log-{unique_name()}"
    login_rate_key = "rate:login:127.0.0.1"
    login_rate_poison = f"safety-login-rate-poison-{unique_name()}"
    redis_delete_pattern("rate:login:*")
    try:
        redis_set_value(login_rate_key, login_rate_poison, 300)
        login_response = fetch(
            "/api/auth/login",
            method="POST",
            headers={"Content-Type": "application/json", "X-Request-Id": login_request_id},
            json_body={"account": TEST_USER, "password": TEST_PASS},
        )
        access_token = json_field(login_response.text, "data.access_token")
        refresh_token = json_field(login_response.text, "data.refresh_token")
        assert_equal("correlated login returns an access token", bool(access_token), True)
        assert_equal("correlated login returns a refresh token", bool(refresh_token), True)
        assert_auth_log_context(
            response=login_response,
            request_id=login_request_id,
            expected_success=True,
            message_markers=(
                "Received login request",
                "Redis operation failed: IncrWithExpire",
                "User login successful",
                "Login successful",
            ),
        )
    finally:
        redis_delete_pattern("rate:login:*")

    refresh_request_id = f"safety-auth-refresh-log-{unique_name()}"
    refresh_response = fetch(
        "/api/auth/refresh",
        method="POST",
        headers={"Content-Type": "application/json", "X-Request-Id": refresh_request_id},
        json_body={"refresh_token": refresh_token},
    )
    refreshed_access_token = json_field(refresh_response.text, "data.access_token")
    refreshed_refresh_token = json_field(refresh_response.text, "data.refresh_token")
    assert_equal("correlated refresh returns an access token", bool(refreshed_access_token), True)
    assert_equal("correlated refresh returns a refresh token", bool(refreshed_refresh_token), True)
    assert_auth_log_context(
        response=refresh_response,
        request_id=refresh_request_id,
        expected_success=True,
        message_markers=(
            "Received refresh token request",
            "[auth_cpu_pool] op=jwt_refresh_verify",
            "Token refresh successful",
            "Refresh token successful",
        ),
    )

    logout_request_id = f"safety-auth-logout-log-{unique_name()}"
    logout_response = fetch(
        "/api/auth/logout",
        method="POST",
        headers={
            **auth_headers(refreshed_access_token),
            "X-Request-Id": logout_request_id,
        },
    )
    assert_auth_log_context(
        response=logout_response,
        request_id=logout_request_id,
        expected_success=True,
        message_markers=(
            "[auth_cpu_pool] op=jwt_verify",
            "Received logout request",
            "User logout:",
            "User logout successful",
            "Logout successful",
        ),
    )

    assert_server_log_excludes_secrets(
        (
            register_password,
            limited_register_password,
            login_rate_key,
            login_rate_poison,
            TEST_PASS,
            access_token,
            refresh_token,
            refreshed_access_token,
            refreshed_refresh_token,
        )
    )


def test_auth_filter_log_context_invariants() -> None:
    """Verify JWT, share-token, and administrator filters retain request context."""
    log_section("Authentication Filter Structured Log Correlation")

    malformed_access_token = f"malformed-owner-token-{unique_name()}"
    jwt_request_id = f"safety-jwt-filter-log-{unique_name()}"
    jwt_response = fetch(
        "/api/file/list",
        headers={
            "Authorization": f"Bearer {malformed_access_token}",
            "X-Request-Id": jwt_request_id,
        },
    )
    assert_auth_filter_rejection_log_context(
        response=jwt_response,
        request_id=jwt_request_id,
        operation="file_query",
        message_marker="[jwt_auth_filter]",
    )
    jwt_instance_id = header_value(jwt_response.headers, "X-Disk-Instance-Id")
    wait_for_correlated_application_log(
        request_id=jwt_request_id,
        instance_id=jwt_instance_id,
        operation="file_query",
        upload_id=None,
        message_marker="JWT parsing failed:",
    )
    log_pass("access-token parser rejection keeps caller request correlation")

    malformed_share_token = f"malformed-visitor-token-{unique_name()}"
    share_request_id = f"safety-share-filter-log-{unique_name()}"
    share_response = fetch(
        f"/api/share/browse/missing-{unique_name()}",
        headers={
            "X-Request-Id": share_request_id,
            "X-Share-Token": malformed_share_token,
        },
    )
    assert_auth_filter_rejection_log_context(
        response=share_response,
        request_id=share_request_id,
        operation="share",
        message_marker="[share_auth_filter]",
    )
    share_instance_id = header_value(share_response.headers, "X-Disk-Instance-Id")
    wait_for_correlated_application_log(
        request_id=share_request_id,
        instance_id=share_instance_id,
        operation="share",
        upload_id=None,
        message_marker="Share token parsing failed:",
    )
    log_pass("share-token parser rejection keeps caller request correlation")

    username = unique_name("filteruser")
    email = f"{username}@example.com"
    password = "FilterUser123"
    user_id: int | None = None
    access_token = ""
    refresh_token = ""
    redis_delete_pattern("rate:register:*")
    try:
        register_response = fetch(
            "/api/auth/register",
            method="POST",
            headers={"Content-Type": "application/json"},
            json_body={"username": username, "email": email, "password": password},
        )
        assert_equal(
            "authentication filter fixture registration succeeds",
            json_field(register_response.text, "code"),
            "0",
        )
        user_id_value = scalar("SELECT id FROM users WHERE username = %s", (username,))
        assert_equal("authentication filter fixture user exists", user_id_value is not None, True)
        user_id = int(user_id_value)

        login_response = fetch(
            "/api/auth/login",
            method="POST",
            headers={"Content-Type": "application/json"},
            json_body={"account": username, "password": password},
        )
        access_token = str(json_field(login_response.text, "data.access_token") or "")
        refresh_token = str(json_field(login_response.text, "data.refresh_token") or "")
        assert_equal("authentication filter fixture login returns a token", bool(access_token), True)

        admin_request_id = f"safety-admin-filter-log-{unique_name()}"
        admin_response = fetch(
            "/api/admin/users",
            headers={
                **auth_headers(access_token),
                "X-Request-Id": admin_request_id,
            },
        )
        assert_auth_filter_rejection_log_context(
            response=admin_response,
            request_id=admin_request_id,
            operation="admin",
            message_marker="[admin_auth_filter]",
        )
        assert_server_log_excludes_secrets(
            (
                malformed_access_token,
                malformed_share_token,
                password,
                access_token,
                refresh_token,
            )
        )
    finally:
        if user_id is not None:
            redis_delete_pattern(f"refresh_token:{user_id}")
        execute("DELETE FROM users WHERE username = %s", (username,))
        redis_delete_pattern("rate:register:*")


def test_share_log_context_invariants() -> None:
    """Verify registered non-download share paths use one bounded operation."""
    log_section("Share Structured Log Correlation")
    create_password = f"C{unique_name()[-6:]}"
    access_password = f"A{unique_name()[-6:]}"
    detail_code = f"detail{unique_name()}"
    cancel_code = f"cancel{unique_name()}"
    access_code = f"access{unique_name()}"

    redis_delete_pattern("rate:share_access:*")
    try:
        create_request_id = f"safety-share-create-log-{unique_name()}"
        create_response = fetch(
            "/api/share",
            method="POST",
            headers={**auth_headers(TOKEN), "X-Request-Id": create_request_id},
            json_body={
                "file_ids": [],
                "folder_ids": [],
                "password": create_password,
            },
        )
        assert_share_log_context(
            response=create_response,
            request_id=create_request_id,
            expected_success=False,
            message_markers=(
                "Received create share request",
                "Create share request must contain file_ids or folder_ids",
                "Create share request parameter validation failed",
                "HTTP request completed",
            ),
        )

        list_request_id = f"safety-share-list-log-{unique_name()}"
        list_response = fetch(
            "/api/share?status=unsupported",
            headers={**auth_headers(TOKEN), "X-Request-Id": list_request_id},
        )
        assert_share_log_context(
            response=list_response,
            request_id=list_request_id,
            expected_success=False,
            message_markers=(
                "Received get share list request",
                "Parameter 'status' invalid value",
                "Share list request parameter validation failed",
                "HTTP request completed",
            ),
        )

        detail_request_id = f"safety-share-detail-log-{unique_name()}"
        detail_response = fetch(
            f"/api/share/{detail_code}",
            headers={**auth_headers(TOKEN), "X-Request-Id": detail_request_id},
        )
        assert_share_log_context(
            response=detail_response,
            request_id=detail_request_id,
            expected_success=False,
            message_markers=(
                "Received get share details request",
                "Get share details failed",
                "HTTP request completed",
            ),
        )

        cancel_request_id = f"safety-share-cancel-log-{unique_name()}"
        cancel_response = fetch(
            "/api/share",
            method="DELETE",
            headers={**auth_headers(TOKEN), "X-Request-Id": cancel_request_id},
            json_body={"share_ids": [cancel_code]},
        )
        assert_share_log_context(
            response=cancel_response,
            request_id=cancel_request_id,
            expected_success=True,
            message_markers=(
                "Received batch cancel shares request",
                "Batch cancel shares: user_id=",
                "Batch cancel shares completed",
            ),
        )
        assert_equal(
            "missing share cancellation remains an item-level failure",
            json_field(cancel_response.text, "data.results.0.status"),
            "failed",
        )
        assert_share_audit_correlation(
            action="share_cancel",
            share_code=cancel_code,
            request_id=cancel_request_id,
            forbidden_values=(create_password, access_password, TOKEN),
        )

        access_request_id = f"safety-share-access-log-{unique_name()}"
        access_response = fetch(
            f"/api/share/access/{access_code}",
            method="POST",
            headers={"Content-Type": "application/json", "X-Request-Id": access_request_id},
            json_body={"password": access_password},
        )
        assert_share_log_context(
            response=access_response,
            request_id=access_request_id,
            expected_success=False,
            message_markers=(
                "Received verify share access request",
                "Verifying share access",
                "Verify share access failed",
                "HTTP request completed",
            ),
        )
        assert_share_audit_correlation(
            action="share_pwd_fail",
            share_code=access_code,
            request_id=access_request_id,
            forbidden_values=(create_password, access_password, TOKEN),
        )
        assert_share_audit_correlation(
            action="share_access",
            share_code=access_code,
            request_id=access_request_id,
            forbidden_values=(create_password, access_password, TOKEN),
        )

        log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
        for secret in (create_password, access_password, TOKEN):
            assert_equal(
                "managed share logs exclude a raw credential",
                bool(secret) and secret not in log_text,
                True,
            )
        log_pass("share logs exclude passwords, password hashes, and owner authorization values")
    finally:
        execute(
            "DELETE FROM operation_logs "
            "WHERE target_type = 'share' AND target_name IN (%s, %s)",
            (cancel_code, access_code),
        )
        redis_delete_pattern(f"rate:share_password:{access_code}:*")
        redis_delete_pattern("rate:share_access:*")


def test_admin_log_context_invariants() -> None:
    """Verify core administration requests and audits retain one request context."""
    log_section("Core Administration Structured Log Correlation")

    invalid_list_request_id = f"safety-admin-list-log-{unique_name()}"
    invalid_list_response = fetch(
        "/api/admin/users?status=unsupported",
        headers={**auth_headers(TOKEN), "X-Request-Id": invalid_list_request_id},
    )
    assert_admin_log_context(
        response=invalid_list_response,
        request_id=invalid_list_request_id,
        expected_success=False,
        message_markers=(
            "Admin list users request:",
            "Parameter 'status' invalid format:",
            "List users request validation failed:",
            "HTTP request completed",
        ),
    )

    self_status_request_id = f"safety-admin-self-status-log-{unique_name()}"
    self_status_response = fetch(
        f"/api/admin/users/{USER_ID}/status",
        method="PUT",
        headers={**auth_headers(TOKEN), "X-Request-Id": self_status_request_id},
        json_body={"status": 1},
    )
    assert_admin_log_context(
        response=self_status_response,
        request_id=self_status_request_id,
        expected_success=False,
        message_markers=(
            "Admin change user status request:",
            "Admin change user status: target_id=",
            "Admin cannot modify self:",
            "Failed to change user status:",
            "HTTP request completed",
        ),
    )

    missing_share_id = int(
        scalar("SELECT COALESCE(MAX(id), 0) + 1000000 FROM shares") or 1000000
    )
    missing_share_request_id = f"safety-admin-missing-share-log-{unique_name()}"
    missing_share_response = fetch(
        f"/api/admin/shares/{missing_share_id}",
        headers={**auth_headers(TOKEN), "X-Request-Id": missing_share_request_id},
    )
    assert_admin_log_context(
        response=missing_share_response,
        request_id=missing_share_request_id,
        expected_success=False,
        message_markers=(
            "Admin get share detail request:",
            "Admin get share detail: share_id=",
            "Admin share not found:",
            "Failed to get share detail:",
            "HTTP request completed",
        ),
    )

    storage_request_id = f"safety-admin-storage-log-{unique_name()}"
    try:
        storage_response = fetch(
            "/api/admin/storage/stats",
            headers={**auth_headers(TOKEN), "X-Request-Id": storage_request_id},
        )
        assert_admin_log_context(
            response=storage_response,
            request_id=storage_request_id,
            expected_success=True,
            message_markers=(
                "Admin get global storage stats request:",
                "Admin get global storage stats successful",
            ),
        )
        assert_admin_audit_correlation(
            action="admin.storage.global_stats",
            request_id=storage_request_id,
        )

        log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
        assert_equal("administration logs exclude the raw JWT", TOKEN not in log_text, True)
        log_pass("administration logs and audits exclude authentication credentials")
    finally:
        execute(
            "DELETE FROM operation_logs "
            "WHERE action = %s AND details ->> 'request_id' = %s",
            ("admin.storage.global_stats", storage_request_id),
        )


def run_expired_cleanup(
    request_id: str | None = None,
    *,
    expected_upload_id: str | None = None,
    expected_state_version: int | None = None,
) -> dict[str, int]:
    """Run the deterministic admin/manual cleanup seam and return cleanup counts."""
    if (expected_upload_id is None) != (expected_state_version is None):
        raise ValueError("expected upload ID and state version must be supplied together")

    headers = auth_headers(TOKEN)
    if request_id is not None:
        headers["X-Request-Id"] = request_id
    resp = fetch(
        "/api/admin/maintenance/cleanup/expired",
        method="POST",
        headers=headers,
    )
    save_evidence(f"{EVIDENCE_PREFIX}-cleanup-expired.json", resp.text)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail("deterministic expired cleanup trigger failed")
        print(resp.text)
        print_summary()
    if request_id is not None:
        instance_id = header_value(resp.headers, "X-Disk-Instance-Id")
        assert_equal(
            "expired cleanup preserves caller request ID",
            header_value(resp.headers, "X-Request-Id"),
            request_id,
        )
        assert_equal(
            "expired cleanup identifies the handling instance",
            bool(instance_id),
            True,
        )
        for marker in (
            "Admin run expired cleanup request",
            "Starting cleanup of expired trash items",
            "Starting cleanup of expired upload tasks",
            "[cleanup_batch] upload_tasks",
            "Admin run expired cleanup successful",
        ):
            wait_for_correlated_application_log(
                request_id=request_id,
                instance_id=instance_id,
                operation="cleanup",
                upload_id=None,
                message_marker=marker,
            )
        if expected_upload_id is not None and expected_state_version is not None:
            wait_for_correlated_application_log(
                request_id=request_id,
                instance_id=instance_id,
                operation="cleanup",
                upload_id=expected_upload_id,
                message_marker="[expire_upload]",
                state_version=expected_state_version,
            )
            log_pass("expired upload lifecycle log records the committed state version")
        log_pass("expired cleanup logs keep typed request correlation")
    return {
        "expired_trash_deleted": int(json_field(resp.text, "data.expired_trash_deleted") or 0),
        "expired_upload_tasks_cleaned": int(json_field(resp.text, "data.expired_upload_tasks_cleaned") or 0),
    }


def test_init_and_chunk_log_context_invariants() -> None:
    """Verify request correlation survives init/chunk coroutine boundaries."""
    log_section("Init And Chunk Structured Log Correlation")

    dto_request_id = f"safety-init-dto-log-{unique_name()}"
    invalid_hash = f"invalid-file-dto-hash-{unique_name()}"
    dto_response = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            **auth_headers(TOKEN),
            "X-Request-Id": dto_request_id,
        },
        json_body={
            "filename": f"invalid_dto_hash_{unique_name()}.bin",
            "file_size": 1024,
            "file_hash": invalid_hash,
            "parent_id": 0,
        },
    )
    assert_equal(
        "file DTO rejects an invalid hash",
        dto_response.status_code >= 400 and json_field(dto_response.text, "code") != "0",
        True,
    )
    dto_instance_id = header_value(dto_response.headers, "X-Disk-Instance-Id")
    assert_equal(
        "file DTO failure preserves caller request ID",
        header_value(dto_response.headers, "X-Request-Id"),
        dto_request_id,
    )
    wait_for_correlated_application_log(
        request_id=dto_request_id,
        instance_id=dto_instance_id,
        operation="upload_init",
        upload_id=None,
        message_marker=f"Invalid file hash format: {invalid_hash}",
    )
    assert_no_unscoped_application_log(
        f"Invalid file hash format: {invalid_hash}",
        assertion="file DTO validation emits no unscoped duplicate",
    )
    log_pass("file DTO validation log keeps typed request correlation")

    config_path = Path(os.environ.get("DISK_CONFIG_FILE", REPO_ROOT / "config.json"))
    if not config_path.is_absolute():
        config_path = REPO_ROOT / config_path
    config = json.loads(config_path.read_text(encoding="utf-8"))
    max_file_size = int(config["custom_config"]["disk"]["max_file_size"])
    init_request_id = f"safety-init-log-{unique_name()}"
    init_response = fetch(
        "/api/file/upload/init",
        method="POST",
        headers={
            **auth_headers(TOKEN),
            "X-Request-Id": init_request_id,
        },
        json_body={
            "filename": f"oversized_log_{unique_name()}.bin",
            "file_size": max_file_size + 1,
            "file_hash": md5_bytes(b"oversized-log-probe"),
            "parent_id": 0,
        },
    )
    assert_equal(
        "oversized init is rejected",
        json_field(init_response.text, "code") != "0",
        True,
    )
    init_instance_id = header_value(init_response.headers, "X-Disk-Instance-Id")
    assert_equal(
        "oversized init preserves caller request ID",
        header_value(init_response.headers, "X-Request-Id"),
        init_request_id,
    )
    wait_for_correlated_application_log(
        request_id=init_request_id,
        instance_id=init_instance_id,
        operation="upload_init",
        upload_id=None,
        message_marker="Upload file exceeds max size",
    )
    log_pass("oversized init lifecycle log keeps typed request correlation")

    missing_upload_id = str(uuid.uuid4())
    missing_request_id = f"safety-missing-chunk-log-{unique_name()}"
    missing_chunk = b"missing-task"
    missing_response = fetch(
        f"/api/file/upload/chunk?upload_id={missing_upload_id}&chunk_index=0"
        f"&chunk_hash={md5_bytes(missing_chunk)}",
        method="POST",
        headers={
            **auth_headers(TOKEN, "application/octet-stream"),
            "X-Request-Id": missing_request_id,
        },
        data=missing_chunk,
    )
    assert_equal(
        "unknown upload task is rejected",
        json_field(missing_response.text, "code") != "0",
        True,
    )
    missing_instance_id = header_value(
        missing_response.headers,
        "X-Disk-Instance-Id",
    )
    wait_for_correlated_application_log(
        request_id=missing_request_id,
        instance_id=missing_instance_id,
        operation="upload_chunk",
        upload_id=missing_upload_id,
        message_marker="Upload task not found or not owned by user",
    )
    log_pass("chunk database validation log keeps typed request correlation")

    chunk_size = configured_chunk_size()
    seed = f"safety-chunk-log-{unique_name()}".encode()
    payload = seed + (b"L" * (chunk_size + 1 - len(seed)))
    upload_id, _ = init_upload(f"chunk_log_{unique_name()}.bin", payload)
    try:
        chunk_request_id = f"safety-chunk-log-{unique_name()}"
        invalid_chunk = b"short"
        chunk_response = fetch(
            f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0"
            f"&chunk_hash={md5_bytes(invalid_chunk)}",
            method="POST",
            headers={
                **auth_headers(TOKEN, "application/octet-stream"),
                "X-Request-Id": chunk_request_id,
            },
            data=invalid_chunk,
        )
        assert_equal(
            "invalid chunk size is rejected",
            json_field(chunk_response.text, "code") != "0",
            True,
        )
        chunk_instance_id = header_value(chunk_response.headers, "X-Disk-Instance-Id")
        assert_equal(
            "invalid chunk preserves caller request ID",
            header_value(chunk_response.headers, "X-Request-Id"),
            chunk_request_id,
        )
        wait_for_correlated_application_log(
            request_id=chunk_request_id,
            instance_id=chunk_instance_id,
            operation="upload_chunk",
            upload_id=upload_id,
            message_marker="Unexpected chunk size",
        )
        wait_for_correlated_application_log(
            request_id=chunk_request_id,
            instance_id=chunk_instance_id,
            operation="upload_chunk",
            upload_id=upload_id,
            message_marker="[upload_chunk]",
        )
        log_pass("chunk service logs keep typed request and upload correlation")
    finally:
        cancel_upload(upload_id)


def test_quota_log_context_invariants() -> None:
    """Verify quota rejection keeps request correlation without domain values."""
    log_section("Quota Service Structured Log Correlation")
    payload = f"safety-quota-log-{unique_name()}".encode()
    filename = f"quota_log_{unique_name()}.bin"
    file_hash = md5_bytes(payload)
    request_id = f"safety-quota-log-{unique_name()}"
    quota_before = user_quota()
    constrained_quota = quota_before["storage_used"] + quota_before["storage_reserved"]
    unexpected_upload_id = ""

    assert_equal(
        "quota log fixture constrains the test user",
        execute(
            "UPDATE users SET storage_quota = %s WHERE id = %s",
            (constrained_quota, USER_ID),
        ),
        1,
    )
    try:
        response = fetch(
            "/api/file/upload/init",
            method="POST",
            headers={**auth_headers(TOKEN), "X-Request-Id": request_id},
            json_body={
                "filename": filename,
                "file_size": len(payload),
                "file_hash": file_hash,
                "parent_id": 0,
            },
        )
        save_evidence(f"{EVIDENCE_PREFIX}-{request_id}.json", response.text)
        unexpected_upload_id = json_field(response.text, "data.upload_id")

        assert_equal("quota rejection returns HTTP 400", response.status_code, 400)
        assert_equal(
            "quota rejection preserves the domain error code",
            json_field(response.text, "code"),
            "50004",
        )
        assert_equal(
            "quota rejection preserves caller request ID",
            header_value(response.headers, "X-Request-Id"),
            request_id,
        )
        instance_id = header_value(response.headers, "X-Disk-Instance-Id")
        assert_equal("quota rejection identifies the handling instance", bool(instance_id), True)

        quota_log = wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="upload_init",
            upload_id=None,
            message_marker="Storage quota reservation rejected",
        )
        caller_log = wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="upload_init",
            upload_id=None,
            message_marker="Upload storage quota reservation failed",
        )
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="upload_init",
            upload_id=None,
            message_marker="HTTP request completed",
        )
        assert_equal(
            "quota helper message contains no domain values",
            quota_log.get("message"),
            "Storage quota reservation rejected",
        )
        assert_equal(
            "quota caller message contains no domain values",
            caller_log.get("message"),
            "Upload storage quota reservation failed",
        )
        assert_no_unscoped_application_log(
            "Storage quota reservation rejected",
            assertion="quota rejection emits no unscoped duplicate",
        )

        quota_after = user_quota()
        assert_equal(
            "quota rejection preserves used storage",
            quota_after["storage_used"],
            quota_before["storage_used"],
        )
        assert_equal(
            "quota rejection creates no reservation",
            quota_after["storage_reserved"],
            quota_before["storage_reserved"],
        )
        assert_equal(
            "quota rejection creates no upload task",
            int(
                scalar(
                    "SELECT COUNT(*) FROM upload_tasks "
                    "WHERE user_id = %s AND file_hash = %s AND filename = %s",
                    (USER_ID, file_hash, filename),
                )
                or 0
            ),
            0,
        )
        log_pass("quota service logs keep typed request correlation and fixed messages")
    finally:
        if unexpected_upload_id:
            cancel_upload(unexpected_upload_id)
        execute(
            "UPDATE users SET storage_quota = %s WHERE id = %s",
            (quota_before["storage_quota"], USER_ID),
        )

    assert_equal("quota log fixture restores user accounting", user_quota(), quota_before)


def cancel_upload_raw(upload_id: str, request_id: str | None = None):
    """Call cancel upload and return the raw response."""
    headers = auth_headers(TOKEN)
    if request_id is not None:
        headers["X-Request-Id"] = request_id
    resp = fetch(
        f"/api/file/upload/{upload_id}",
        method="DELETE",
        headers=headers,
    )
    evidence_request_id = request_id or "generated-request-id"
    save_evidence(
        f"{EVIDENCE_PREFIX}-{upload_id}-{evidence_request_id}-cancel.json",
        resp.text,
    )
    return resp


def cancel_upload(upload_id: str) -> None:
    """Cancel an upload task."""
    resp = cancel_upload_raw(upload_id)
    if resp.status_code != 200 or json_field(resp.text, "code") != "0":
        log_fail(f"{upload_id}: cancel upload failed")
        print(resp.text)
        print_summary()


def race_complete_cancel_expire(upload_id: str):
    """Start the three terminal contenders from one synchronization point."""
    barrier = threading.Barrier(3)

    def complete():
        barrier.wait(timeout=10)
        return complete_upload_raw(upload_id, f"terminal-race-complete-{unique_name()}")

    def cancel():
        barrier.wait(timeout=10)
        return cancel_upload_raw(upload_id)

    def expire() -> dict[str, int]:
        barrier.wait(timeout=10)
        return run_expired_cleanup()

    with ThreadPoolExecutor(max_workers=3) as executor:
        complete_future = executor.submit(complete)
        cancel_future = executor.submit(cancel)
        expire_future = executor.submit(expire)
        return complete_future.result(), cancel_future.result(), expire_future.result()


def assert_upload_task(upload_id: str, expected_status: int) -> dict[str, object]:
    """Assert upload task status and return the row."""
    row = query_one("SELECT * FROM upload_tasks WHERE id = %s", (upload_id,))
    if row is None:
        log_fail(f"upload task {upload_id} exists")
        print_summary()
    assert_equal(f"upload task {upload_id} status={expected_status}", int(row["status"]), expected_status)
    return row


def assert_chunk_row_count(upload_id: str, expected_count: int) -> None:
    """Assert the number of chunk rows tracked for an upload task."""
    count = int(scalar("SELECT COUNT(*) FROM upload_task_chunks WHERE task_id = %s", (upload_id,)) or 0)
    assert_equal(f"upload task {upload_id} chunk row count={expected_count}", count, expected_count)


def test_successful_chunked_upload_invariants() -> None:
    """Verify successful upload DB and filesystem invariants."""
    log_section("Successful Chunked Upload Invariants")
    payload = (f"safety-success-{unique_name()}".encode() + b"-payload")
    filename = f"safety_success_{unique_name()}.bin"
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    quota_after_init = user_quota()
    assert_numeric_delta("upload init reserves storage", quota_before["storage_reserved"], quota_after_init["storage_reserved"], len(payload))

    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    file_id = complete_upload(upload_id)
    assert_chunk_row_count(upload_id, 0)
    quota_after_complete = user_quota()

    task = assert_upload_task(upload_id, 1)
    assert_equal("completed task reserved_bytes equals file size", int(task["reserved_bytes"]), len(payload))
    assert_numeric_delta(
        "complete releases reserved storage",
        quota_after_init["storage_reserved"],
        quota_after_complete["storage_reserved"],
        -len(payload),
    )
    assert_numeric_delta(
        "complete increases used storage by file size",
        quota_before["storage_used"],
        quota_after_complete["storage_used"],
        len(payload),
    )

    file_row = query_one("SELECT * FROM files WHERE id = %s AND user_id = %s", (int(file_id), USER_ID))
    if file_row is None:
        log_fail("files row created after upload complete")
        print_summary()
    log_pass("files row created after upload complete")
    assert_equal("files row has expected name", file_row["name"], filename)
    assert_equal("files row has expected size", int(file_row["size"]), len(payload))

    content_row = query_one("SELECT * FROM file_contents WHERE id = %s", (file_row["content_id"],))
    if content_row is None:
        log_fail("file_contents row exists for uploaded file")
        print_summary()
    log_pass("file_contents row exists for uploaded file")
    assert_equal("file_contents md5 matches payload", content_row["hash_md5"], file_hash)
    assert_storage_job_succeeded(
        "successful upload cleanup job converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("temp upload directory cleaned after success", upload_temp_dir(upload_id))
    assert_path_absent("assembled temp artifact cleaned after success", upload_temp_dir(upload_id).parent / f"{upload_id}.tmp")
    assert_equal(
        "final blob path exists after success",
        local_blob_path(str(content_row["storage_path"])).exists(),
        True,
    )


def test_chunk_metadata_failure_retry_and_orphan_cleanup_invariants() -> None:
    """Verify retry reuse and session cleanup after chunk metadata persistence fails."""
    log_section("Chunk Object Write / Metadata Failure Recovery")

    def fail_after_object_write(upload_id: str, payload: bytes, scenario: str) -> Path:
        quota_before_failure = user_quota()
        with reject_chunk_metadata_insert(upload_id):
            response = upload_single_chunk_raw(
                upload_id,
                payload,
                f"{scenario}-chunk-db-failure",
            )

        assert_equal(f"{scenario} metadata failure returns HTTP 500", response.status_code, 500)
        assert_equal(
            f"{scenario} metadata failure returns a business error",
            json_field(response.text, "code") != "0",
            True,
        )
        chunk_path = (
            upload_temp_dir(upload_id)
            / "chunks"
            / f"0-{md5_bytes(payload)}.part"
        )
        if not chunk_path.is_file():
            log_fail(f"{scenario} immutable chunk object exists after metadata failure")
            print_summary()
        log_pass(f"{scenario} immutable chunk object exists after metadata failure")
        assert_equal(f"{scenario} immutable chunk bytes match", chunk_path.read_bytes(), payload)
        assert_chunk_row_count(upload_id, 0)
        assert_upload_task(upload_id, 0)

        quota_after_failure = user_quota()
        assert_equal(
            f"{scenario} metadata failure preserves reserved storage",
            quota_after_failure["storage_reserved"],
            quota_before_failure["storage_reserved"],
        )
        assert_equal(
            f"{scenario} metadata failure preserves used storage",
            quota_after_failure["storage_used"],
            quota_before_failure["storage_used"],
        )
        return chunk_path

    retry_payload = f"safety-chunk-db-retry-{unique_name()}".encode()
    retry_filename = f"safety_chunk_db_retry_{unique_name()}.bin"
    retry_quota_before = user_quota()
    retry_upload_id, retry_md5 = init_upload(retry_filename, retry_payload)
    retry_quota_after_init = user_quota()
    assert_numeric_delta(
        "retry fixture reserves storage once",
        retry_quota_before["storage_reserved"],
        retry_quota_after_init["storage_reserved"],
        len(retry_payload),
    )
    retry_chunk_path = fail_after_object_write(
        retry_upload_id,
        retry_payload,
        "retry",
    )
    original_stat = retry_chunk_path.stat()
    time.sleep(0.01)
    upload_single_chunk(retry_upload_id, retry_payload)
    reused_stat = retry_chunk_path.stat()
    assert_equal("retry reuses the immutable chunk inode", reused_stat.st_ino, original_stat.st_ino)
    assert_equal(
        "retry does not rewrite the immutable chunk object",
        reused_stat.st_mtime_ns,
        original_stat.st_mtime_ns,
    )
    assert_chunk_row_count(retry_upload_id, 1)

    retry_file_id = int(complete_upload(retry_upload_id))
    assert_upload_task(retry_upload_id, 1)
    assert_chunk_row_count(retry_upload_id, 0)
    retry_quota_after_complete = user_quota()
    assert_equal(
        "retry completion releases reserved storage once",
        retry_quota_after_complete["storage_reserved"],
        retry_quota_before["storage_reserved"],
    )
    assert_numeric_delta(
        "retry completion increases used storage once",
        retry_quota_before["storage_used"],
        retry_quota_after_complete["storage_used"],
        len(retry_payload),
    )

    retry_file_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
            (USER_ID, retry_filename),
        )
        or 0
    )
    assert_equal("retry completion creates one file row", retry_file_count, 1)
    retry_file = query_one(
        "SELECT file.id, content.ref_count FROM files AS file "
        "JOIN file_contents AS content ON content.id = file.content_id "
        "WHERE file.user_id = %s AND file.name = %s",
        (USER_ID, retry_filename),
    )
    if retry_file is None:
        log_fail("retry completion creates its file/content reference")
        print_summary()
    log_pass("retry completion creates its file/content reference")
    assert_equal("retry completion returns the persisted file", int(retry_file["id"]), retry_file_id)
    assert_equal("retry completion increments ref_count once", int(retry_file["ref_count"]), 1)
    retry_content_count = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (retry_md5, sha256_bytes(retry_payload)),
        )
        or 0
    )
    assert_equal("retry completion creates one content row", retry_content_count, 1)
    retry_cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{retry_upload_id}",),
        )
        or 0
    )
    assert_equal("retry completion creates one cleanup job", retry_cleanup_count, 1)
    assert_storage_job_succeeded(
        "retry completion cleanup converges",
        f"staging-cleanup:{retry_upload_id}",
    )
    assert_path_absent("retry completion removes the staging session", upload_temp_dir(retry_upload_id))
    assert_path_exists(
        "retry completion preserves one final blob",
        final_blob_path(sha256_bytes(retry_payload)),
    )

    orphan_payload = f"safety-chunk-db-orphan-{unique_name()}".encode()
    orphan_filename = f"safety_chunk_db_orphan_{unique_name()}.bin"
    orphan_quota_before = user_quota()
    orphan_upload_id, orphan_md5 = init_upload(orphan_filename, orphan_payload)
    orphan_quota_after_init = user_quota()
    assert_numeric_delta(
        "orphan fixture reserves storage once",
        orphan_quota_before["storage_reserved"],
        orphan_quota_after_init["storage_reserved"],
        len(orphan_payload),
    )
    orphan_chunk_path = fail_after_object_write(
        orphan_upload_id,
        orphan_payload,
        "orphan",
    )
    cancel_upload(orphan_upload_id)
    assert_upload_task(orphan_upload_id, 2)
    assert_chunk_row_count(orphan_upload_id, 0)

    orphan_quota_after_cancel = user_quota()
    assert_equal(
        "orphan cancellation releases reserved storage once",
        orphan_quota_after_cancel["storage_reserved"],
        orphan_quota_before["storage_reserved"],
    )
    assert_equal(
        "orphan cancellation preserves used storage",
        orphan_quota_after_cancel["storage_used"],
        orphan_quota_before["storage_used"],
    )
    assert_db_row_absent(
        "orphan cancellation creates no file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, orphan_filename),
    )
    orphan_content_count = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (orphan_md5, sha256_bytes(orphan_payload)),
        )
        or 0
    )
    assert_equal("orphan cancellation creates no content row", orphan_content_count, 0)
    orphan_cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{orphan_upload_id}",),
        )
        or 0
    )
    assert_equal("orphan cancellation creates one cleanup job", orphan_cleanup_count, 1)
    assert_storage_job_succeeded(
        "orphan cancellation cleanup converges",
        f"staging-cleanup:{orphan_upload_id}",
    )
    assert_path_absent("orphan cleanup removes the unregistered chunk", orphan_chunk_path)
    assert_path_absent("orphan cleanup removes the staging session", upload_temp_dir(orphan_upload_id))
    assert_path_absent(
        "orphan cancellation creates no final blob",
        final_blob_path(sha256_bytes(orphan_payload)),
    )

    save_evidence(
        f"{EVIDENCE_PREFIX}-chunk-metadata-failure-recovery.json",
        json.dumps(
            {
                "retry": {
                    "upload_id": retry_upload_id,
                    "status": "completed",
                    "file_id": retry_file_id,
                },
                "orphan": {
                    "upload_id": orphan_upload_id,
                    "status": "cancelled",
                },
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )


def test_hundred_concurrent_complete_invariants() -> None:
    """Verify 100 complete calls across two processes settle all effects once."""
    log_section("100 Concurrent Complete Requests Across API Instances")

    try:
        with peer_api_instance() as (peer_url, peer_instance_id, _):
            redis_delete_pattern(f"rate:upload:{USER_ID}:*")
            primary_ready = fetch(f"{BASE_URL}/api/health/ready")
            primary_instance_id = json_field(primary_ready.text, "data.instance_id")
            if primary_ready.status_code != 200 or not primary_instance_id:
                log_fail("primary API readiness exposes an instance ID")
                print_summary()
            log_pass("primary API readiness exposes an instance ID")
            assert_equal(
                "concurrent complete uses two distinct API instances",
                primary_instance_id != peer_instance_id,
                True,
            )

            payload = f"safety-hundred-complete-{unique_name()}".encode()
            filename = f"safety_hundred_complete_{unique_name()}.bin"
            quota_before = user_quota()
            upload_id, file_hash = init_upload(filename, payload)
            upload_single_chunk(upload_id, payload)
            quota_after_init = user_quota()
            assert_numeric_delta(
                "100 complete fixture reserves storage once",
                quota_before["storage_reserved"],
                quota_after_init["storage_reserved"],
                len(payload),
            )

            request_count = 100
            barrier = threading.Barrier(request_count)

            def invoke_complete(index: int):
                target_url = BASE_URL if index % 2 == 0 else peer_url
                expected_instance = primary_instance_id if index % 2 == 0 else peer_instance_id
                barrier.wait(timeout=30)
                response = fetch(
                    f"{target_url}/api/file/upload/complete",
                    method="POST",
                    headers={
                        **auth_headers(TOKEN),
                        "X-Request-Id": f"hundred-complete-{os.getpid()}-{index}",
                    },
                    json_body={"upload_id": upload_id},
                    timeout=180,
                )
                return index, target_url, expected_instance, response

            with ThreadPoolExecutor(max_workers=request_count) as executor:
                results = list(executor.map(invoke_complete, range(request_count)))

            instance_mismatches: list[int] = []
            unexpected_results: list[dict[str, object]] = []
            completed_ids: list[int] = []
            conflicts: list[tuple[int, str]] = []
            for index, target_url, expected_instance, response in results:
                response_instance = header_value(response.headers, "X-Disk-Instance-Id")
                if response_instance != expected_instance:
                    instance_mismatches.append(index)

                code = json_field(response.text, "code")
                response_file_id = json_field(response.text, "data.file.id")
                if response.status_code == 200 and code == "0" and response_file_id:
                    completed_ids.append(int(response_file_id))
                elif response.status_code == 409 and code == "10004":
                    conflicts.append((index, target_url))
                else:
                    unexpected_results.append(
                        {
                            "index": index,
                            "http_status": response.status_code,
                            "code": code,
                        }
                    )

            if instance_mismatches:
                log_fail(
                    f"100 complete requests stayed on their selected instances: {instance_mismatches}"
                )
                print_summary()
            log_pass("100 complete requests stayed on their selected instances")

            if unexpected_results:
                log_fail(f"100 complete initial responses follow the retry contract: {unexpected_results}")
                print_summary()
            log_pass("100 complete initial responses are success or documented conflict")

            if not completed_ids:
                log_fail("100 concurrent complete requests produce a winner")
                print_summary()
            log_pass("100 concurrent complete requests produce a winner")

            replay_failures: list[dict[str, object]] = []
            for index, target_url in conflicts:
                replay = fetch(
                    f"{target_url}/api/file/upload/complete",
                    method="POST",
                    headers=auth_headers(TOKEN),
                    json_body={"upload_id": upload_id},
                    timeout=30,
                )
                replay_id = json_field(replay.text, "data.file.id")
                if replay.status_code == 200 and json_field(replay.text, "code") == "0" and replay_id:
                    completed_ids.append(int(replay_id))
                else:
                    replay_failures.append(
                        {
                            "index": index,
                            "http_status": replay.status_code,
                            "code": json_field(replay.text, "code"),
                        }
                    )

            if replay_failures:
                log_fail(f"conflicting complete requests converge on replay: {replay_failures}")
                print_summary()
            log_pass("conflicting complete requests converge on replay")
            assert_equal("all 100 complete calls return a file after replay", len(completed_ids), request_count)
            assert_equal("all 100 complete calls return one file ID", len(set(completed_ids)), 1)
            completed_file_id = completed_ids[0]

            task = query_one(
                "SELECT status, completed_file_id FROM upload_tasks "
                "WHERE id = %s AND user_id = %s",
                (upload_id, USER_ID),
            )
            if task is None:
                log_fail("100 complete fixture preserves its upload task")
                print_summary()
            assert_equal("100 complete fixture reaches Completed", int(task["status"]), 1)
            assert_equal(
                "100 complete fixture records the converged file ID",
                int(task["completed_file_id"]),
                completed_file_id,
            )

            file_count = int(
                scalar(
                    "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
                    (USER_ID, filename),
                )
                or 0
            )
            assert_equal("100 complete calls create one file row", file_count, 1)
            file_row = query_one(
                "SELECT file.id, content.ref_count FROM files AS file "
                "JOIN file_contents AS content ON content.id = file.content_id "
                "WHERE file.id = %s AND file.user_id = %s",
                (completed_file_id, USER_ID),
            )
            if file_row is None:
                log_fail("100 complete calls create the converged file/content row")
                print_summary()
            assert_equal(
                "100 complete calls return the persisted file",
                int(file_row["id"]),
                completed_file_id,
            )
            content_count = int(
                scalar(
                    "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
                    (file_hash, sha256_bytes(payload)),
                )
                or 0
            )
            assert_equal("100 complete calls create one content row", content_count, 1)
            assert_equal("100 complete calls increment ref_count once", int(file_row["ref_count"]), 1)

            quota_after_complete = user_quota()
            assert_equal(
                "100 complete calls release reserved storage once",
                quota_after_complete["storage_reserved"],
                quota_before["storage_reserved"],
            )
            assert_numeric_delta(
                "100 complete calls increase used storage once",
                quota_before["storage_used"],
                quota_after_complete["storage_used"],
                len(payload),
            )
            assert_chunk_row_count(upload_id, 0)
            cleanup_job_count = int(
                scalar(
                    "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
                    (f"staging-cleanup:{upload_id}",),
                )
                or 0
            )
            assert_equal("100 complete calls create one cleanup job", cleanup_job_count, 1)
            assert_storage_job_succeeded(
                "100 complete cleanup converges",
                f"staging-cleanup:{upload_id}",
            )
            assert_path_exists(
                "100 complete calls preserve one final blob",
                final_blob_path(sha256_bytes(payload)),
            )
            save_evidence(
                f"{EVIDENCE_PREFIX}-{upload_id}-hundred-complete.json",
                json.dumps(
                    {
                        "upload_id": upload_id,
                        "primary_instance_id": primary_instance_id,
                        "peer_instance_id": peer_instance_id,
                        "request_count": request_count,
                        "initial_success_count": request_count - len(conflicts),
                        "initial_conflict_count": len(conflicts),
                        "completed_file_id": completed_file_id,
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
            )
    except Exception as error:
        log_fail(f"100 complete dual-process fixture failed: {error}")
        print_summary()
    finally:
        redis_delete_pattern(f"rate:upload:{USER_ID}:*")


def test_finalize_claim_process_death_takeover_invariants() -> None:
    """Kill a real API after its committed claim and verify lease-based takeover."""
    log_section("Finalize Claim Process Death And Lease Takeover")
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")

    payload = f"safety-finalize-claim-death-{unique_name()}".encode()
    filename = f"safety_finalize_claim_death_{unique_name()}.bin"
    payload_md5 = md5_bytes(payload)
    payload_sha256 = sha256_bytes(payload)
    quota_before = user_quota_balance()

    upload_id, file_hash = init_upload(filename, payload)
    assembled_path = upload_temp_dir(upload_id).parent / f"{upload_id}.tmp"
    blob_path = final_blob_path(payload_sha256)
    assert_equal("claim-death fixture MD5 matches init contract", file_hash, payload_md5)
    assert_path_absent("claim-death fixture starts without an assembled object", assembled_path)
    assert_path_absent("claim-death fixture starts without a final blob", blob_path)

    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    quota_after_init = user_quota()
    assert_numeric_delta(
        "claim-death fixture reserves storage once",
        quota_before["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )

    primary_ready = fetch(f"{BASE_URL}/api/health/ready")
    primary_instance_id = json_field(primary_ready.text, "data.instance_id")
    if primary_ready.status_code != 200 or not primary_instance_id:
        log_fail("claim-death fixture resolves the primary API instance")
        print_summary()
    log_pass("claim-death fixture resolves the primary API instance")

    killed_pid = 0
    killed_instance_id = ""
    killed_state_version = 0
    killed_lease_expires_at = None
    dropped_request_error = ""

    try:
        with peer_api_instance(
            purpose="claim-crash",
            upload_finalize_lease_seconds=30,
            pause_after_claim_upload_id=upload_id,
        ) as (crash_url, crash_instance_id, crash_process):
            assert_equal(
                "claim-death fixture uses two distinct API instances",
                crash_instance_id != primary_instance_id,
                True,
            )
            killed_pid = crash_process.pid
            killed_instance_id = crash_instance_id

            with ThreadPoolExecutor(max_workers=1) as executor:
                complete_future = executor.submit(
                    fetch,
                    f"{crash_url}/api/file/upload/complete",
                    method="POST",
                    headers={
                        **auth_headers(TOKEN),
                        "X-Request-Id": f"claim-death-{upload_id}",
                    },
                    json_body={"upload_id": upload_id},
                    timeout=120,
                )

                claimed_task = None
                claim_deadline = time.monotonic() + 10
                while time.monotonic() < claim_deadline:
                    candidate = query_one(
                        "SELECT status, lease_owner, lease_expires_at, state_version, "
                        "finalize_attempts, lease_expires_at > NOW() AS lease_live "
                        "FROM upload_tasks WHERE id = %s AND user_id = %s",
                        (upload_id, USER_ID),
                    )
                    if (
                        candidate is not None
                        and int(candidate["status"]) == 4
                        and candidate["lease_owner"] == crash_instance_id
                        and bool(candidate["lease_live"])
                    ):
                        claimed_task = candidate
                        break
                    time.sleep(0.05)

                if claimed_task is None:
                    log_fail("crash API commits its Finalizing claim before assembly")
                    print_summary()
                log_pass("crash API commits its Finalizing claim before assembly")
                killed_state_version = int(claimed_task["state_version"])
                killed_lease_expires_at = claimed_task["lease_expires_at"]
                assert_equal(
                    "first Finalizing claim increments finalize_attempts once",
                    int(claimed_task["finalize_attempts"]),
                    1,
                )

                quota_after_claim = user_quota()
                assert_equal(
                    "claim pause preserves reserved storage",
                    quota_after_claim["storage_reserved"],
                    quota_after_init["storage_reserved"],
                )
                assert_equal(
                    "claim pause preserves used storage",
                    quota_after_claim["storage_used"],
                    quota_after_init["storage_used"],
                )
                assert_chunk_row_count(upload_id, 1)
                assert_path_absent("claim pause creates no assembled object", assembled_path)
                assert_path_absent("claim pause creates no final blob", blob_path)
                assert_db_row_absent(
                    "claim pause creates no logical file",
                    "SELECT id FROM files WHERE user_id = %s AND name = %s",
                    (USER_ID, filename),
                )
                assert_db_row_absent(
                    "claim pause creates no content row",
                    "SELECT id FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
                    (payload_md5, payload_sha256),
                )

                assert_equal("crash API is alive at the injected pause", crash_process.poll(), None)
                crash_process.kill()
                crash_process.wait(timeout=5)
                assert_equal(
                    "crash API is terminated by a non-zero signal exit",
                    crash_process.returncode != 0,
                    True,
                )

                try:
                    crash_response = complete_future.result(timeout=10)
                except Exception as error:  # noqa: BLE001 - process death intentionally breaks HTTP
                    dropped_request_error = type(error).__name__
                    log_pass("killed API does not return a successful complete response")
                else:
                    log_fail(
                        "killed API unexpectedly returned a complete response: "
                        f"HTTP {crash_response.status_code}, body={crash_response.text}"
                    )
                    print_summary()
    except Exception as error:
        log_fail(f"claim-death process fixture failed: {error}")
        print_summary()

    task_after_kill = query_one(
        "SELECT status, lease_owner, lease_expires_at, state_version, finalize_attempts, "
        "lease_expires_at > NOW() AS lease_live "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_kill is None:
        log_fail("claim-death task remains durable after the API process dies")
        print_summary()
    log_pass("claim-death task remains durable after the API process dies")
    assert_equal("killed claim remains Finalizing", int(task_after_kill["status"]), 4)
    assert_equal("killed claim retains its owner", task_after_kill["lease_owner"], killed_instance_id)
    assert_equal(
        "process death does not mutate the claim version",
        int(task_after_kill["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "process death does not add a finalize attempt",
        int(task_after_kill["finalize_attempts"]),
        1,
    )
    assert_equal("killed claim lease is still live", bool(task_after_kill["lease_live"]), True)
    assert_path_absent("process death leaves no assembled object", assembled_path)
    assert_path_absent("process death leaves no final blob", blob_path)

    conflict_response = complete_upload_raw(
        upload_id,
        f"claim-death-live-lease-{upload_id}",
    )
    assert_equal("live lease rejects takeover with HTTP 409", conflict_response.status_code, 409)
    assert_equal(
        "live lease rejects takeover with UploadStateConflict",
        json_field(conflict_response.text, "code"),
        "10004",
    )

    task_after_conflict = query_one(
        "SELECT status, lease_owner, state_version, finalize_attempts, "
        "lease_expires_at > NOW() AS lease_live "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_conflict is None:
        log_fail("live-lease conflict preserves the upload task")
        print_summary()
    assert_equal("live-lease conflict preserves Finalizing", int(task_after_conflict["status"]), 4)
    assert_equal(
        "live-lease conflict preserves the killed owner",
        task_after_conflict["lease_owner"],
        killed_instance_id,
    )
    assert_equal(
        "live-lease conflict preserves the claim version",
        int(task_after_conflict["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "live-lease conflict does not add an attempt",
        int(task_after_conflict["finalize_attempts"]),
        1,
    )
    assert_equal("live-lease conflict occurs before expiry", bool(task_after_conflict["lease_live"]), True)

    expired_task = None
    expiry_deadline = time.monotonic() + 40
    while time.monotonic() < expiry_deadline:
        candidate = query_one(
            "SELECT status, lease_owner, state_version, finalize_attempts, "
            "lease_expires_at <= NOW() AS lease_expired "
            "FROM upload_tasks WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if candidate is not None and bool(candidate["lease_expired"]):
            expired_task = candidate
            break
        time.sleep(0.1)

    if expired_task is None:
        log_fail("killed claim expires according to PostgreSQL time")
        print_summary()
    log_pass("killed claim expires according to PostgreSQL time without a DB rewrite")
    assert_equal("expired claim remains Finalizing before takeover", int(expired_task["status"]), 4)
    assert_equal("expired claim retains the killed owner", expired_task["lease_owner"], killed_instance_id)
    assert_equal(
        "natural expiry preserves the claim version",
        int(expired_task["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "natural expiry preserves one finalize attempt",
        int(expired_task["finalize_attempts"]),
        1,
    )

    takeover_response = complete_upload_raw(
        upload_id,
        f"claim-death-takeover-{upload_id}",
    )
    takeover_file_id = json_field(takeover_response.text, "data.file.id")
    if (
        takeover_response.status_code != 200
        or json_field(takeover_response.text, "code") != "0"
        or not takeover_file_id
    ):
        log_fail("primary API completes the upload after natural lease expiry")
        print(takeover_response.text)
        print_summary()
    log_pass("primary API completes the upload after natural lease expiry")
    assert_equal(
        "takeover response comes from the primary API",
        header_value(takeover_response.headers, "X-Disk-Instance-Id"),
        primary_instance_id,
    )
    completed_file_id = int(takeover_file_id)

    completed_task = query_one(
        "SELECT status, completed_file_id, lease_owner, lease_expires_at, "
        "state_version, finalize_attempts, reserved_bytes "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if completed_task is None:
        log_fail("takeover preserves the completed upload task")
        print_summary()
    assert_equal("takeover reaches Completed", int(completed_task["status"]), 1)
    assert_equal(
        "takeover records the returned file ID",
        int(completed_task["completed_file_id"]),
        completed_file_id,
    )
    assert_equal("takeover clears lease_owner", completed_task["lease_owner"], None)
    assert_equal("takeover clears lease_expires_at", completed_task["lease_expires_at"], None)
    assert_equal(
        "takeover advances the killed claim version",
        int(completed_task["state_version"]) > killed_state_version,
        True,
    )
    assert_equal(
        "claim and takeover produce exactly two finalize attempts",
        int(completed_task["finalize_attempts"]),
        2,
    )
    assert_equal(
        "takeover preserves the target reservation amount",
        int(completed_task["reserved_bytes"]),
        len(payload),
    )

    assert_chunk_row_count(upload_id, 0)
    completed_quota = user_quota_balance()
    assert_equal(
        "takeover leaves the shared reservation residual unchanged",
        completed_quota["reservation_residual"],
        quota_before["reservation_residual"],
    )
    assert_numeric_delta(
        "takeover increases used storage once",
        quota_before["storage_used"],
        completed_quota["storage_used"],
        len(payload),
    )

    file_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
            (USER_ID, filename),
        )
        or 0
    )
    assert_equal("claim death and takeover create one file row", file_count, 1)
    file_row = query_one(
        "SELECT file.id, content.ref_count, content.hash_md5, content.hash_sha256 "
        "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
        "WHERE file.id = %s AND file.user_id = %s",
        (completed_file_id, USER_ID),
    )
    if file_row is None:
        log_fail("claim death and takeover create one file/content reference")
        print_summary()
    assert_equal("takeover returns the persisted file", int(file_row["id"]), completed_file_id)
    assert_equal("takeover increments content ref_count once", int(file_row["ref_count"]), 1)
    assert_equal("takeover content MD5 matches", file_row["hash_md5"], payload_md5)
    assert_equal("takeover content SHA-256 matches", file_row["hash_sha256"], payload_sha256)
    content_count = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (payload_md5, payload_sha256),
        )
        or 0
    )
    assert_equal("claim death and takeover create one content row", content_count, 1)

    cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{upload_id}",),
        )
        or 0
    )
    assert_equal("claim death and takeover create one cleanup job", cleanup_count, 1)
    assert_storage_job_succeeded(
        "claim-death takeover cleanup converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("takeover removes the staging session", upload_temp_dir(upload_id))
    assert_path_absent("takeover leaves no assembled object", assembled_path)
    assert_path_exists("takeover preserves one final blob", blob_path)

    save_evidence(
        f"{EVIDENCE_PREFIX}-{upload_id}-claim-death-takeover.json",
        json.dumps(
            {
                "upload_id": upload_id,
                "killed_pid": killed_pid,
                "killed_instance_id": killed_instance_id,
                "killed_state_version": killed_state_version,
                "killed_lease_expires_at": killed_lease_expires_at.isoformat()
                if killed_lease_expires_at is not None
                else None,
                "dropped_request_error": dropped_request_error,
                "live_lease_conflict_code": json_field(conflict_response.text, "code"),
                "takeover_instance_id": primary_instance_id,
                "final_state_version": int(completed_task["state_version"]),
                "finalize_attempts": int(completed_task["finalize_attempts"]),
                "completed_file_id": completed_file_id,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")


def test_assembled_object_process_death_takeover_invariants() -> None:
    """Kill an API after assembly and verify a new owner safely rebuilds it."""
    log_section("Assembled Object Process Death And Lease Takeover")
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")

    payload = f"safety-assembled-death-{unique_name()}".encode()
    filename = f"safety_assembled_death_{unique_name()}.bin"
    payload_md5 = md5_bytes(payload)
    payload_sha256 = sha256_bytes(payload)
    quota_before = user_quota_balance()

    upload_id, file_hash = init_upload(filename, payload)
    assembled_path = upload_temp_dir(upload_id).parent / f"{upload_id}.tmp"
    blob_path = final_blob_path(payload_sha256)
    assert_equal("assembly-death fixture MD5 matches init contract", file_hash, payload_md5)
    assert_path_absent("assembly-death fixture starts without an assembled object", assembled_path)
    assert_path_absent("assembly-death fixture starts without a final blob", blob_path)

    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    quota_after_init = user_quota()
    assert_numeric_delta(
        "assembly-death fixture reserves storage once",
        quota_before["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )

    primary_ready = fetch(f"{BASE_URL}/api/health/ready")
    primary_instance_id = json_field(primary_ready.text, "data.instance_id")
    if primary_ready.status_code != 200 or not primary_instance_id:
        log_fail("assembly-death fixture resolves the primary API instance")
        print_summary()
    log_pass("assembly-death fixture resolves the primary API instance")

    killed_pid = 0
    killed_instance_id = ""
    killed_state_version = 0
    killed_lease_expires_at = None
    assembled_inode = 0
    assembled_mtime_ns = 0
    dropped_request_error = ""
    crash_log_path = EVIDENCE_ROOT / "safety-upload-assembly-crash.log"

    try:
        with peer_api_instance(
            purpose="assembly-crash",
            upload_finalize_lease_seconds=30,
            pause_after_assembly_upload_id=upload_id,
        ) as (crash_url, crash_instance_id, crash_process):
            assert_equal(
                "assembly-death fixture uses two distinct API instances",
                crash_instance_id != primary_instance_id,
                True,
            )
            killed_pid = crash_process.pid
            killed_instance_id = crash_instance_id

            with ThreadPoolExecutor(max_workers=1) as executor:
                complete_future = executor.submit(
                    fetch,
                    f"{crash_url}/api/file/upload/complete",
                    method="POST",
                    headers={
                        **auth_headers(TOKEN),
                        "X-Request-Id": f"assembly-death-{upload_id}",
                    },
                    json_body={"upload_id": upload_id},
                    timeout=120,
                )

                paused_task = None
                pause_marker = (
                    "Test fault injection paused upload after assembly: "
                    f"upload_id={upload_id}"
                )
                pause_deadline = time.monotonic() + 10
                while time.monotonic() < pause_deadline:
                    candidate = query_one(
                        "SELECT status, lease_owner, lease_expires_at, state_version, "
                        "finalize_attempts, lease_expires_at > NOW() AS lease_live "
                        "FROM upload_tasks WHERE id = %s AND user_id = %s",
                        (upload_id, USER_ID),
                    )
                    log_text = (
                        crash_log_path.read_text(encoding="utf-8", errors="replace")
                        if crash_log_path.is_file()
                        else ""
                    )
                    if (
                        pause_marker in log_text
                        and candidate is not None
                        and int(candidate["status"]) == 4
                        and candidate["lease_owner"] == crash_instance_id
                        and bool(candidate["lease_live"])
                        and assembled_path.is_file()
                        and assembled_path.stat().st_size == len(payload)
                    ):
                        paused_task = candidate
                        break
                    time.sleep(0.05)

                if paused_task is None:
                    log_fail("crash API pauses after a complete assembled object is durable")
                    print_summary()
                log_pass("crash API pauses after a complete assembled object is durable")
                killed_state_version = int(paused_task["state_version"])
                killed_lease_expires_at = paused_task["lease_expires_at"]
                assembled_stat = assembled_path.stat()
                assembled_inode = assembled_stat.st_ino
                assembled_mtime_ns = assembled_stat.st_mtime_ns
                assert_equal(
                    "assembly pause retains one finalize attempt",
                    int(paused_task["finalize_attempts"]),
                    1,
                )
                assert_equal("assembled object contains the complete payload", assembled_path.read_bytes(), payload)

                quota_after_assembly = user_quota()
                assert_equal(
                    "assembly pause preserves reserved storage",
                    quota_after_assembly["storage_reserved"],
                    quota_after_init["storage_reserved"],
                )
                assert_equal(
                    "assembly pause preserves used storage",
                    quota_after_assembly["storage_used"],
                    quota_after_init["storage_used"],
                )
                assert_chunk_row_count(upload_id, 1)
                assert_path_absent("assembly pause creates no final blob", blob_path)
                assert_db_row_absent(
                    "assembly pause creates no logical file",
                    "SELECT id FROM files WHERE user_id = %s AND name = %s",
                    (USER_ID, filename),
                )
                assert_db_row_absent(
                    "assembly pause creates no content row",
                    "SELECT id FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
                    (payload_md5, payload_sha256),
                )

                assert_equal("assembly owner is alive at the injected pause", crash_process.poll(), None)
                crash_process.kill()
                crash_process.wait(timeout=5)
                assert_equal(
                    "assembly owner is terminated by a non-zero signal exit",
                    crash_process.returncode != 0,
                    True,
                )

                try:
                    crash_response = complete_future.result(timeout=10)
                except Exception as error:  # noqa: BLE001 - process death intentionally breaks HTTP
                    dropped_request_error = type(error).__name__
                    log_pass("killed assembly owner returns no successful complete response")
                else:
                    log_fail(
                        "killed assembly owner unexpectedly returned a response: "
                        f"HTTP {crash_response.status_code}, body={crash_response.text}"
                    )
                    print_summary()
    except Exception as error:
        log_fail(f"assembly-death process fixture failed: {error}")
        print_summary()

    task_after_kill = query_one(
        "SELECT status, lease_owner, lease_expires_at, state_version, finalize_attempts, "
        "lease_expires_at > NOW() AS lease_live "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_kill is None:
        log_fail("assembly-death task remains durable after the API process dies")
        print_summary()
    log_pass("assembly-death task remains durable after the API process dies")
    assert_equal("killed assembly task remains Finalizing", int(task_after_kill["status"]), 4)
    assert_equal(
        "killed assembly task retains its owner",
        task_after_kill["lease_owner"],
        killed_instance_id,
    )
    assert_equal(
        "assembly owner death preserves the claim version",
        int(task_after_kill["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "assembly owner death preserves one finalize attempt",
        int(task_after_kill["finalize_attempts"]),
        1,
    )
    assert_equal("killed assembly lease is still live", bool(task_after_kill["lease_live"]), True)
    assert_path_exists("process death preserves the assembled object", assembled_path)
    assert_equal("preserved assembled object retains its inode", assembled_path.stat().st_ino, assembled_inode)
    assert_equal(
        "preserved assembled object is not rewritten before takeover",
        assembled_path.stat().st_mtime_ns,
        assembled_mtime_ns,
    )
    assert_equal("preserved assembled bytes remain complete", assembled_path.read_bytes(), payload)
    assert_path_absent("assembly owner death leaves no final blob", blob_path)

    conflict_response = complete_upload_raw(
        upload_id,
        f"assembly-death-live-lease-{upload_id}",
    )
    assert_equal("live assembly lease rejects takeover with HTTP 409", conflict_response.status_code, 409)
    assert_equal(
        "live assembly lease rejects takeover with UploadStateConflict",
        json_field(conflict_response.text, "code"),
        "10004",
    )
    task_after_conflict = query_one(
        "SELECT status, lease_owner, state_version, finalize_attempts, "
        "lease_expires_at > NOW() AS lease_live "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_conflict is None:
        log_fail("live assembly lease conflict preserves the task")
        print_summary()
    assert_equal("live assembly conflict preserves Finalizing", int(task_after_conflict["status"]), 4)
    assert_equal(
        "live assembly conflict preserves the killed owner",
        task_after_conflict["lease_owner"],
        killed_instance_id,
    )
    assert_equal(
        "live assembly conflict preserves the claim version",
        int(task_after_conflict["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "live assembly conflict does not add an attempt",
        int(task_after_conflict["finalize_attempts"]),
        1,
    )
    assert_equal(
        "live assembly conflict occurs before lease expiry",
        bool(task_after_conflict["lease_live"]),
        True,
    )
    assert_equal("live-lease conflict leaves assembled bytes unchanged", assembled_path.read_bytes(), payload)

    expired_task = None
    expiry_deadline = time.monotonic() + 40
    while time.monotonic() < expiry_deadline:
        candidate = query_one(
            "SELECT status, lease_owner, state_version, finalize_attempts, "
            "lease_expires_at <= NOW() AS lease_expired "
            "FROM upload_tasks WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if candidate is not None and bool(candidate["lease_expired"]):
            expired_task = candidate
            break
        time.sleep(0.1)

    if expired_task is None:
        log_fail("killed assembly lease expires according to PostgreSQL time")
        print_summary()
    log_pass("killed assembly lease expires according to PostgreSQL time without a DB rewrite")
    assert_equal("expired assembly task remains Finalizing", int(expired_task["status"]), 4)
    assert_equal("expired assembly task retains the killed owner", expired_task["lease_owner"], killed_instance_id)
    assert_equal(
        "natural assembly lease expiry preserves the claim version",
        int(expired_task["state_version"]),
        killed_state_version,
    )
    assert_equal(
        "natural assembly lease expiry preserves one attempt",
        int(expired_task["finalize_attempts"]),
        1,
    )

    takeover_response = complete_upload_raw(
        upload_id,
        f"assembly-death-takeover-{upload_id}",
    )
    takeover_file_id = json_field(takeover_response.text, "data.file.id")
    if (
        takeover_response.status_code != 200
        or json_field(takeover_response.text, "code") != "0"
        or not takeover_file_id
    ):
        log_fail("primary API safely rebuilds and completes after assembly owner death")
        print(takeover_response.text)
        print_summary()
    log_pass("primary API safely rebuilds and completes after assembly owner death")
    assert_equal(
        "assembly takeover response comes from the primary API",
        header_value(takeover_response.headers, "X-Disk-Instance-Id"),
        primary_instance_id,
    )
    completed_file_id = int(takeover_file_id)

    completed_task = query_one(
        "SELECT status, completed_file_id, lease_owner, lease_expires_at, "
        "state_version, finalize_attempts, reserved_bytes "
        "FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if completed_task is None:
        log_fail("assembly takeover preserves the completed task")
        print_summary()
    assert_equal("assembly takeover reaches Completed", int(completed_task["status"]), 1)
    assert_equal(
        "assembly takeover records the returned file ID",
        int(completed_task["completed_file_id"]),
        completed_file_id,
    )
    assert_equal("assembly takeover clears lease_owner", completed_task["lease_owner"], None)
    assert_equal("assembly takeover clears lease_expires_at", completed_task["lease_expires_at"], None)
    assert_equal(
        "assembly takeover advances the killed state version",
        int(completed_task["state_version"]) > killed_state_version,
        True,
    )
    assert_equal(
        "assembly owner and takeover produce exactly two finalize attempts",
        int(completed_task["finalize_attempts"]),
        2,
    )
    assert_equal(
        "assembly takeover preserves the target reservation amount",
        int(completed_task["reserved_bytes"]),
        len(payload),
    )

    assert_chunk_row_count(upload_id, 0)
    completed_quota = user_quota_balance()
    assert_equal(
        "assembly takeover leaves the shared reservation residual unchanged",
        completed_quota["reservation_residual"],
        quota_before["reservation_residual"],
    )
    assert_numeric_delta(
        "assembly takeover increases used storage once",
        quota_before["storage_used"],
        completed_quota["storage_used"],
        len(payload),
    )

    file_count = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
            (USER_ID, filename),
        )
        or 0
    )
    assert_equal("assembly death and takeover create one file row", file_count, 1)
    file_row = query_one(
        "SELECT file.id, content.ref_count, content.hash_md5, content.hash_sha256 "
        "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
        "WHERE file.id = %s AND file.user_id = %s",
        (completed_file_id, USER_ID),
    )
    if file_row is None:
        log_fail("assembly death and takeover create one file/content reference")
        print_summary()
    assert_equal("assembly takeover returns the persisted file", int(file_row["id"]), completed_file_id)
    assert_equal("assembly takeover increments ref_count once", int(file_row["ref_count"]), 1)
    assert_equal("assembly takeover content MD5 matches", file_row["hash_md5"], payload_md5)
    assert_equal("assembly takeover content SHA-256 matches", file_row["hash_sha256"], payload_sha256)
    content_count = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (payload_md5, payload_sha256),
        )
        or 0
    )
    assert_equal("assembly death and takeover create one content row", content_count, 1)

    cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{upload_id}",),
        )
        or 0
    )
    assert_equal("assembly death and takeover create one cleanup job", cleanup_count, 1)
    assert_storage_job_succeeded(
        "assembly-death takeover cleanup converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("assembly takeover removes the staging session", upload_temp_dir(upload_id))
    assert_path_absent("assembly takeover removes the rebuilt assembled object", assembled_path)
    assert_path_exists("assembly takeover preserves one final blob", blob_path)
    assert_equal("assembly takeover final blob contains the payload", blob_path.read_bytes(), payload)

    save_evidence(
        f"{EVIDENCE_PREFIX}-{upload_id}-assembly-death-takeover.json",
        json.dumps(
            {
                "upload_id": upload_id,
                "killed_pid": killed_pid,
                "killed_instance_id": killed_instance_id,
                "killed_state_version": killed_state_version,
                "killed_lease_expires_at": killed_lease_expires_at.isoformat()
                if killed_lease_expires_at is not None
                else None,
                "assembled_inode_before_kill": assembled_inode,
                "assembled_mtime_ns_before_kill": assembled_mtime_ns,
                "dropped_request_error": dropped_request_error,
                "live_lease_conflict_code": json_field(conflict_response.text, "code"),
                "takeover_instance_id": primary_instance_id,
                "final_state_version": int(completed_task["state_version"]),
                "finalize_attempts": int(completed_task["finalize_attempts"]),
                "completed_file_id": completed_file_id,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")


def test_finalize_renewal_network_partition_fencing_invariants() -> None:
    """Delay an owner's transaction renewal past takeover and reject its recovery."""
    log_section("Finalize Renewal Network Partition And Stale Owner Fencing")
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")

    payload = f"safety-finalize-renewal-partition-{unique_name()}".encode()
    payload_md5 = md5_bytes(payload)
    payload_sha256 = sha256_bytes(payload)
    filename = f"safety_finalize_renewal_partition_{unique_name()}.bin"
    blob_path = final_blob_path(payload_sha256)
    quota_before = user_quota_balance()

    upload_id, file_hash = init_upload(filename, payload)
    assert_equal("renewal-partition fixture MD5 matches", file_hash, payload_md5)
    upload_single_chunk(upload_id, payload)
    quota_after_init = user_quota()
    assert_numeric_delta(
        "renewal-partition fixture reserves storage once",
        quota_before["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )

    primary_ready = fetch(f"{BASE_URL}/api/health/ready")
    primary_instance_id = json_field(primary_ready.text, "data.instance_id")
    if primary_ready.status_code != 200 or not primary_instance_id:
        log_fail("renewal-partition fixture resolves the primary API instance")
        print_summary()
    log_pass("renewal-partition fixture resolves the primary API instance")

    postgres = database_config()
    partitioned_instance_id = ""
    partitioned_pid = 0
    partitioned_version = 0
    partitioned_lease_expires_at = None
    blocked_client_bytes = 0
    takeover_file_id = 0
    takeover_state_version = 0
    takeover_finalized_at = None
    stale_response_status = 0
    stale_response_code = ""
    peer_log_path = EVIDENCE_ROOT / "safety-upload-lease-partition.log"

    try:
        with tempfile.TemporaryDirectory(prefix="disk-upload-lease-partition-") as fault_dir_raw:
            release_file = Path(fault_dir_raw) / "release-finalize-transaction"
            with PartitionableTcpProxy(
                str(postgres["host"]),
                int(postgres["port"]),
            ) as database_proxy:
                with peer_api_instance(
                    purpose="lease-partition",
                    upload_finalize_lease_seconds=30,
                    pause_before_finalize_transaction_upload_id=upload_id,
                    finalize_transaction_release_file=release_file,
                    database_host="127.0.0.1",
                    database_port=database_proxy.port,
                ) as (partitioned_url, peer_instance_id, peer_process):
                    partitioned_instance_id = peer_instance_id
                    partitioned_pid = peer_process.pid
                    assert_equal(
                        "renewal partition uses two distinct API instances",
                        peer_instance_id != primary_instance_id,
                        True,
                    )

                    with ThreadPoolExecutor(max_workers=1) as executor:
                        stale_future = executor.submit(
                            fetch,
                            f"{partitioned_url}/api/file/upload/complete",
                            method="POST",
                            headers={
                                **auth_headers(TOKEN),
                                "X-Request-Id": f"lease-partition-stale-{upload_id}",
                            },
                            json_body={"upload_id": upload_id},
                            timeout=120,
                        )

                        try:
                            paused_task = None
                            pause_marker = (
                                "Test fault injection paused upload before finalize transaction renewal: "
                                f"upload_id={upload_id}"
                            )
                            pause_deadline = time.monotonic() + 15
                            while time.monotonic() < pause_deadline:
                                candidate = query_one(
                                    "SELECT status, lease_owner, lease_expires_at, state_version, "
                                    "finalize_attempts, lease_expires_at > NOW() AS lease_live "
                                    "FROM upload_tasks WHERE id = %s AND user_id = %s",
                                    (upload_id, USER_ID),
                                )
                                log_text = (
                                    peer_log_path.read_text(encoding="utf-8", errors="replace")
                                    if peer_log_path.is_file()
                                    else ""
                                )
                                if (
                                    pause_marker in log_text
                                    and candidate is not None
                                    and int(candidate["status"]) == 4
                                    and candidate["lease_owner"] == peer_instance_id
                                    and bool(candidate["lease_live"])
                                    and blob_path.is_file()
                                ):
                                    paused_task = candidate
                                    break
                                time.sleep(0.05)

                            if paused_task is None:
                                log_fail(
                                    "partitioned API pauses after promotion and before its transaction renewal"
                                )
                                print_summary()
                            log_pass(
                                "partitioned API pauses after promotion and before its transaction renewal"
                            )
                            partitioned_version = int(paused_task["state_version"])
                            partitioned_lease_expires_at = paused_task["lease_expires_at"]
                            assert_equal(
                                "partitioned owner has one finalize attempt before takeover",
                                int(paused_task["finalize_attempts"]),
                                1,
                            )
                            assert_equal(
                                "partition pause retains reserved storage",
                                user_quota()["storage_reserved"],
                                quota_after_init["storage_reserved"],
                            )
                            assert_chunk_row_count(upload_id, 1)
                            assert_db_row_absent(
                                "partition pause creates no logical file",
                                "SELECT id FROM files WHERE user_id = %s AND name = %s",
                                (USER_ID, filename),
                            )
                            assert_db_row_absent(
                                "partition pause creates no content row",
                                "SELECT id FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
                                (payload_md5, payload_sha256),
                            )
                            assert_path_exists("partition pause has promoted the final blob", blob_path)
                            assert_equal(
                                "partitioned API is alive before the network fault",
                                peer_process.poll(),
                                None,
                            )

                            database_proxy.partition()
                            release_file.touch()
                            if not database_proxy.wait_for_blocked_client_data(timeout=10):
                                log_fail(
                                    "TCP partition observes the old owner's transaction renewal traffic"
                                )
                                print_summary()
                            log_pass(
                                "TCP partition observes the old owner's transaction renewal traffic"
                            )
                            blocked_client_bytes = database_proxy.blocked_client_bytes
                            assert_equal(
                                "partition holds at least one client payload byte",
                                blocked_client_bytes > 0,
                                True,
                            )
                            assert_equal(
                                "partitioned API stays alive with renewal traffic blocked",
                                peer_process.poll(),
                                None,
                            )

                            live_conflict = complete_upload_raw(
                                upload_id,
                                f"lease-partition-live-{upload_id}",
                            )
                            assert_equal(
                                "live partitioned lease rejects early takeover with HTTP 409",
                                live_conflict.status_code,
                                409,
                            )
                            assert_equal(
                                "live partitioned lease returns ResourceConflict",
                                json_field(live_conflict.text, "code"),
                                "10004",
                            )

                            expired_task = None
                            expiry_deadline = time.monotonic() + 40
                            while time.monotonic() < expiry_deadline:
                                candidate = query_one(
                                    "SELECT status, lease_owner, state_version, finalize_attempts, "
                                    "lease_expires_at <= NOW() AS lease_expired "
                                    "FROM upload_tasks WHERE id = %s AND user_id = %s",
                                    (upload_id, USER_ID),
                                )
                                if candidate is not None and bool(candidate["lease_expired"]):
                                    expired_task = candidate
                                    break
                                time.sleep(0.1)

                            if expired_task is None:
                                log_fail(
                                    "partitioned renewal lease expires according to PostgreSQL time"
                                )
                                print_summary()
                            log_pass(
                                "partitioned renewal lease expires according to PostgreSQL time without a rewrite"
                            )
                            assert_equal(
                                "network partition preserves the old owner until takeover",
                                expired_task["lease_owner"],
                                peer_instance_id,
                            )
                            assert_equal(
                                "network partition does not advance the old generation",
                                int(expired_task["state_version"]),
                                partitioned_version,
                            )
                            assert_equal(
                                "network partition does not add a finalize attempt",
                                int(expired_task["finalize_attempts"]),
                                1,
                            )

                            takeover_response = complete_upload_raw(
                                upload_id,
                                f"lease-partition-takeover-{upload_id}",
                            )
                            takeover_file_id_raw = json_field(
                                takeover_response.text,
                                "data.file.id",
                            )
                            if (
                                takeover_response.status_code != 200
                                or json_field(takeover_response.text, "code") != "0"
                                or not takeover_file_id_raw
                            ):
                                log_fail(
                                    "direct API takes over and completes during the old owner's partition"
                                )
                                print(takeover_response.text)
                                print_summary()
                            log_pass(
                                "direct API takes over and completes during the old owner's partition"
                            )
                            takeover_file_id = int(takeover_file_id_raw)
                            assert_equal(
                                "takeover response comes from the direct primary API",
                                header_value(
                                    takeover_response.headers,
                                    "X-Disk-Instance-Id",
                                ),
                                primary_instance_id,
                            )

                            completed_before_heal = query_one(
                                "SELECT status, completed_file_id, lease_owner, lease_expires_at, "
                                "state_version, finalize_attempts, finalized_at "
                                "FROM upload_tasks WHERE id = %s AND user_id = %s",
                                (upload_id, USER_ID),
                            )
                            if completed_before_heal is None:
                                log_fail("takeover result is durable before healing the partition")
                                print_summary()
                            log_pass("takeover result is durable before healing the partition")
                            assert_equal(
                                "takeover stores Completed status",
                                int(completed_before_heal["status"]),
                                1,
                            )
                            assert_equal(
                                "takeover stores its completed_file_id",
                                int(completed_before_heal["completed_file_id"]),
                                takeover_file_id,
                            )
                            assert_equal(
                                "takeover records exactly two finalize attempts",
                                int(completed_before_heal["finalize_attempts"]),
                                2,
                            )
                            takeover_state_version = int(
                                completed_before_heal["state_version"]
                            )
                            takeover_finalized_at = completed_before_heal["finalized_at"]

                            database_proxy.heal()
                            stale_response = stale_future.result(timeout=30)
                            stale_response_status = stale_response.status_code
                            stale_response_code = json_field(stale_response.text, "code")
                            assert_equal(
                                "recovered old owner returns HTTP 409",
                                stale_response_status,
                                409,
                            )
                            assert_equal(
                                "recovered old owner returns ResourceConflict",
                                stale_response_code,
                                "10004",
                            )
                            assert_equal(
                                "stale response identifies the partitioned API instance",
                                header_value(stale_response.headers, "X-Disk-Instance-Id"),
                                peer_instance_id,
                            )

                            recovered_ready = fetch(
                                f"{partitioned_url}/api/health/ready",
                                timeout=10,
                            )
                            assert_equal(
                                "old owner becomes ready after the partition heals",
                                recovered_ready.status_code,
                                200,
                            )
                            assert_equal(
                                "old owner keeps the same instance identity after recovery",
                                json_field(recovered_ready.text, "data.instance_id"),
                                peer_instance_id,
                            )
                            assert_equal(
                                "old owner process is not restarted by the partition",
                                peer_process.pid,
                                partitioned_pid,
                            )
                            assert_equal(
                                "old owner process remains alive after recovery",
                                peer_process.poll(),
                                None,
                            )
                        finally:
                            database_proxy.heal()
    except Exception as error:
        log_fail(f"renewal network partition fixture failed: {error}")
        print_summary()

    completed_after_recovery = query_one(
        "SELECT status, completed_file_id, lease_owner, lease_expires_at, state_version, "
        "finalize_attempts, finalized_at, reserved_bytes FROM upload_tasks "
        "WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if completed_after_recovery is None:
        log_fail("completed upload remains after stale owner recovery")
        print_summary()
    assert_equal(
        "stale owner preserves Completed status",
        int(completed_after_recovery["status"]),
        1,
    )
    assert_equal(
        "stale owner cannot replace takeover completed_file_id",
        int(completed_after_recovery["completed_file_id"]),
        takeover_file_id,
    )
    assert_equal(
        "stale owner cannot advance takeover state_version",
        int(completed_after_recovery["state_version"]),
        takeover_state_version,
    )
    assert_equal(
        "stale owner cannot add another finalize attempt",
        int(completed_after_recovery["finalize_attempts"]),
        2,
    )
    assert_equal(
        "stale owner cannot replace takeover finalized_at",
        completed_after_recovery["finalized_at"],
        takeover_finalized_at,
    )
    assert_equal(
        "partition recovery preserves the target reservation amount",
        int(completed_after_recovery["reserved_bytes"]),
        len(payload),
    )
    assert_equal("stale owner leaves lease_owner cleared", completed_after_recovery["lease_owner"], None)
    assert_equal(
        "stale owner leaves lease_expires_at cleared",
        completed_after_recovery["lease_expires_at"],
        None,
    )

    file_rows = int(
        scalar(
            "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
            (USER_ID, filename),
        )
        or 0
    )
    assert_equal("partition recovery creates one logical file", file_rows, 1)
    file_row = query_one(
        "SELECT file.id, content.ref_count, content.hash_md5, content.hash_sha256 "
        "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
        "WHERE file.id = %s AND file.user_id = %s",
        (takeover_file_id, USER_ID),
    )
    if file_row is None:
        log_fail("partition takeover keeps one file/content reference")
        print_summary()
    assert_equal("partition takeover file id remains authoritative", int(file_row["id"]), takeover_file_id)
    assert_equal("partition recovery increments content ref_count once", int(file_row["ref_count"]), 1)
    assert_equal("partition recovery content MD5 matches", file_row["hash_md5"], payload_md5)
    assert_equal("partition recovery content SHA-256 matches", file_row["hash_sha256"], payload_sha256)
    content_rows = int(
        scalar(
            "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
            (payload_md5, payload_sha256),
        )
        or 0
    )
    assert_equal("partition recovery creates one content row", content_rows, 1)

    completed_quota = user_quota_balance()
    assert_equal(
        "partition recovery leaves the shared reservation residual unchanged",
        completed_quota["reservation_residual"],
        quota_before["reservation_residual"],
    )
    assert_numeric_delta(
        "partition recovery increases used storage once",
        quota_before["storage_used"],
        completed_quota["storage_used"],
        len(payload),
    )
    assert_chunk_row_count(upload_id, 0)
    cleanup_count = int(
        scalar(
            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{upload_id}",),
        )
        or 0
    )
    assert_equal("partition recovery creates one cleanup job", cleanup_count, 1)
    assert_storage_job_succeeded(
        "partition recovery staging cleanup converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("partition recovery removes the staging session", upload_temp_dir(upload_id))
    assert_path_exists("partition recovery preserves the final blob", blob_path)
    assert_equal("partition recovery final blob contains the payload", blob_path.read_bytes(), payload)

    save_evidence(
        f"{EVIDENCE_PREFIX}-{upload_id}-renewal-network-partition.json",
        json.dumps(
            {
                "upload_id": upload_id,
                "partitioned_instance_id": partitioned_instance_id,
                "partitioned_pid": partitioned_pid,
                "partitioned_state_version": partitioned_version,
                "partitioned_lease_expires_at": partitioned_lease_expires_at.isoformat()
                if partitioned_lease_expires_at is not None
                else None,
                "blocked_client_bytes": blocked_client_bytes,
                "takeover_instance_id": primary_instance_id,
                "takeover_file_id": takeover_file_id,
                "takeover_state_version": takeover_state_version,
                "stale_response_status": stale_response_status,
                "stale_response_code": stale_response_code,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")


def test_final_transaction_process_death_replay_invariants() -> None:
    """Kill an API after its final commit and verify replay heals the lost response."""
    log_section("Final Transaction Commit Process Death Replay Invariants")
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")

    payload = f"safety-finalize-commit-death-{unique_name()}".encode()
    payload_md5 = md5_bytes(payload)
    payload_sha256 = sha256_bytes(payload)
    filename = f"safety_finalize_commit_death_{unique_name()}.bin"
    blob_path = final_blob_path(payload_sha256)
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    assert_equal("finalize-commit fixture MD5 matches", file_hash, payload_md5)
    upload_single_chunk(upload_id, payload)
    quota_after_init = user_quota()
    assert_numeric_delta(
        "finalize-commit fixture reserves storage once",
        quota_before["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )

    primary_ready = fetch(f"{BASE_URL}/api/health/ready")
    primary_instance_id = json_field(primary_ready.text, "data.instance_id")
    if primary_ready.status_code != 200 or not primary_instance_id:
        log_fail("finalize-commit fixture resolves the primary API instance")
        print_summary()
    log_pass("finalize-commit fixture resolves the primary API instance")

    names_before_commit = root_file_names()
    assert_equal("primed file list excludes the pending upload", filename in names_before_commit, False)
    version_key = f"file_list_version:{USER_ID}"
    version_before_commit = int(redis_get_value(version_key) or "0")
    cache_key = (
        f"file_list:{USER_ID}:{version_before_commit}:0:file:created_at:desc:1:100"
    )
    assert_equal(
        "finalize-commit fixture primes the current file-list cache entry",
        redis_ttl(cache_key) > 0,
        True,
    )

    killed_pid = 0
    killed_instance_id = ""
    dropped_request_error = ""
    committed_file_id = 0
    committed_state_version = 0
    committed_finalized_at = None
    crash_log_path = EVIDENCE_ROOT / "safety-upload-finalize-commit-crash.log"

    try:
        with peer_api_instance(
            purpose="finalize-commit-crash",
            pause_after_finalize_commit_upload_id=upload_id,
        ) as (crash_url, crash_instance_id, crash_process):
            assert_equal(
                "finalize-commit fixture uses two distinct API instances",
                crash_instance_id != primary_instance_id,
                True,
            )
            killed_pid = crash_process.pid
            killed_instance_id = crash_instance_id

            with ThreadPoolExecutor(max_workers=1) as executor:
                complete_future = executor.submit(
                    fetch,
                    f"{crash_url}/api/file/upload/complete",
                    method="POST",
                    headers={
                        **auth_headers(TOKEN),
                        "X-Request-Id": f"finalize-commit-death-{upload_id}",
                    },
                    json_body={"upload_id": upload_id},
                    timeout=120,
                )

                paused_task = None
                pause_marker = (
                    "Test fault injection paused upload after finalize commit: "
                    f"upload_id={upload_id}"
                )
                pause_deadline = time.monotonic() + 10
                while time.monotonic() < pause_deadline:
                    candidate = query_one(
                        "SELECT status, completed_file_id, lease_owner, lease_expires_at, "
                        "state_version, finalize_attempts, finalized_at "
                        "FROM upload_tasks WHERE id = %s AND user_id = %s",
                        (upload_id, USER_ID),
                    )
                    log_text = (
                        crash_log_path.read_text(encoding="utf-8", errors="replace")
                        if crash_log_path.is_file()
                        else ""
                    )
                    if (
                        pause_marker in log_text
                        and candidate is not None
                        and int(candidate["status"]) == 1
                        and candidate["completed_file_id"] is not None
                    ):
                        paused_task = candidate
                        break
                    time.sleep(0.05)

                if paused_task is None:
                    log_fail("crash API pauses after the final transaction is durable")
                    print_summary()
                log_pass("crash API pauses after the final transaction is durable")
                committed_file_id = int(paused_task["completed_file_id"])
                committed_state_version = int(paused_task["state_version"])
                committed_finalized_at = paused_task["finalized_at"]

                assert_equal("committed upload reaches Completed", int(paused_task["status"]), 1)
                assert_equal("committed upload clears lease_owner", paused_task["lease_owner"], None)
                assert_equal(
                    "committed upload clears lease_expires_at",
                    paused_task["lease_expires_at"],
                    None,
                )
                assert_equal(
                    "committed upload retains one finalize attempt",
                    int(paused_task["finalize_attempts"]),
                    1,
                )
                assert_equal(
                    "committed upload records finalized_at",
                    committed_finalized_at is not None,
                    True,
                )
                assert_chunk_row_count(upload_id, 0)

                quota_after_commit = user_quota()
                assert_equal(
                    "final transaction releases reserved storage once",
                    quota_after_commit["storage_reserved"],
                    quota_before["storage_reserved"],
                )
                assert_numeric_delta(
                    "final transaction increases used storage once",
                    quota_before["storage_used"],
                    quota_after_commit["storage_used"],
                    len(payload),
                )

                committed_file = query_one(
                    "SELECT file.id, file.content_id, content.ref_count, content.hash_md5, "
                    "content.hash_sha256, content.storage_path "
                    "FROM files AS file "
                    "JOIN file_contents AS content ON content.id = file.content_id "
                    "WHERE file.id = %s AND file.user_id = %s AND file.name = %s",
                    (committed_file_id, USER_ID, filename),
                )
                if committed_file is None:
                    log_fail("final transaction creates the committed file/content reference")
                    print_summary()
                log_pass("final transaction creates the committed file/content reference")
                assert_equal("committed content ref_count is one", int(committed_file["ref_count"]), 1)
                assert_equal("committed content MD5 matches", committed_file["hash_md5"], payload_md5)
                assert_equal(
                    "committed content SHA-256 matches",
                    committed_file["hash_sha256"],
                    payload_sha256,
                )
                assert_equal(
                    "final transaction creates one matching file row",
                    int(
                        scalar(
                            "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
                            (USER_ID, filename),
                        )
                        or 0
                    ),
                    1,
                )
                assert_equal(
                    "final transaction creates one matching content row",
                    int(
                        scalar(
                            "SELECT COUNT(*) FROM file_contents "
                            "WHERE hash_md5 = %s AND hash_sha256 = %s",
                            (payload_md5, payload_sha256),
                        )
                        or 0
                    ),
                    1,
                )
                assert_equal(
                    "final transaction creates one staging cleanup job",
                    int(
                        scalar(
                            "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
                            (f"staging-cleanup:{upload_id}",),
                        )
                        or 0
                    ),
                    1,
                )
                assert_path_exists("final transaction preserves the final blob", blob_path)
                assert_equal("final transaction stores the exact payload", blob_path.read_bytes(), payload)

                version_after_commit = int(redis_get_value(version_key) or "0")
                assert_equal(
                    "pause occurs before the first file-list generation bump",
                    version_after_commit,
                    version_before_commit,
                )
                stale_names = root_file_names()
                assert_equal(
                    "the primed list remains stale before replay",
                    filename in stale_names,
                    False,
                )
                assert_equal(
                    "reading the stale list does not change its generation",
                    int(redis_get_value(version_key) or "0"),
                    version_before_commit,
                )

                assert_equal("finalize-commit owner is alive at the injected pause", crash_process.poll(), None)
                crash_process.kill()
                crash_process.wait(timeout=5)
                assert_equal(
                    "finalize-commit owner is terminated by a non-zero signal exit",
                    crash_process.returncode != 0,
                    True,
                )

                try:
                    crash_response = complete_future.result(timeout=10)
                except Exception as error:  # noqa: BLE001 - process death intentionally breaks HTTP
                    dropped_request_error = type(error).__name__
                    log_pass("killed finalize-commit owner returns no successful complete response")
                else:
                    log_fail(
                        "killed finalize-commit owner unexpectedly returned a response: "
                        f"HTTP {crash_response.status_code}, body={crash_response.text}"
                    )
                    print_summary()
    except Exception as error:
        log_fail(f"finalize-commit process fixture failed: {error}")
        print_summary()

    task_after_kill = query_one(
        "SELECT status, completed_file_id, lease_owner, lease_expires_at, state_version, "
        "finalize_attempts, finalized_at FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if task_after_kill is None:
        log_fail("finalize-commit task remains durable after the API process dies")
        print_summary()
    log_pass("finalize-commit task remains durable after the API process dies")
    assert_equal("process death preserves Completed", int(task_after_kill["status"]), 1)
    assert_equal(
        "process death preserves completed_file_id",
        int(task_after_kill["completed_file_id"]),
        committed_file_id,
    )
    assert_equal("process death preserves the completed version", int(task_after_kill["state_version"]), committed_state_version)
    assert_equal("process death preserves finalized_at", task_after_kill["finalized_at"], committed_finalized_at)
    assert_equal("process death preserves one finalize attempt", int(task_after_kill["finalize_attempts"]), 1)
    assert_equal("completed task remains lease-free", task_after_kill["lease_owner"], None)
    assert_equal("completed task retains no lease expiry", task_after_kill["lease_expires_at"], None)

    replay_response = complete_upload_raw(
        upload_id,
        f"finalize-commit-replay-{upload_id}",
    )
    replay_file_id = json_field(replay_response.text, "data.file.id")
    if (
        replay_response.status_code != 200
        or json_field(replay_response.text, "code") != "0"
        or not replay_file_id
    ):
        log_fail("primary API replays the committed upload after response loss")
        print(replay_response.text)
        print_summary()
    log_pass("primary API replays the committed upload after response loss")
    assert_equal(
        "replay response comes from the primary API",
        header_value(replay_response.headers, "X-Disk-Instance-Id"),
        primary_instance_id,
    )
    assert_equal("replay returns the committed file ID", int(replay_file_id), committed_file_id)

    version_after_replay = int(redis_get_value(version_key) or "0")
    assert_equal(
        "completed replay advances the shared file-list generation",
        version_after_replay,
        version_before_commit + 1,
    )
    refreshed_names = root_file_names()
    assert_equal("file list exposes the committed file after replay", filename in refreshed_names, True)

    replayed_task = query_one(
        "SELECT status, completed_file_id, lease_owner, lease_expires_at, state_version, "
        "finalize_attempts, finalized_at FROM upload_tasks WHERE id = %s AND user_id = %s",
        (upload_id, USER_ID),
    )
    if replayed_task is None:
        log_fail("completed replay preserves the upload task")
        print_summary()
    assert_equal("replay keeps the task Completed", int(replayed_task["status"]), 1)
    assert_equal("replay keeps completed_file_id", int(replayed_task["completed_file_id"]), committed_file_id)
    assert_equal("replay does not advance state_version", int(replayed_task["state_version"]), committed_state_version)
    assert_equal("replay does not add a finalize attempt", int(replayed_task["finalize_attempts"]), 1)
    assert_equal("replay preserves finalized_at", replayed_task["finalized_at"], committed_finalized_at)

    quota_after_replay = user_quota()
    assert_equal(
        "replay leaves reserved storage unchanged",
        quota_after_replay["storage_reserved"],
        quota_before["storage_reserved"],
    )
    assert_numeric_delta(
        "replay leaves used storage settled once",
        quota_before["storage_used"],
        quota_after_replay["storage_used"],
        len(payload),
    )
    assert_chunk_row_count(upload_id, 0)
    assert_equal(
        "response-loss replay leaves one file row",
        int(
            scalar(
                "SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s",
                (USER_ID, filename),
            )
            or 0
        ),
        1,
    )
    replayed_content = query_one(
        "SELECT content.ref_count, content.hash_md5, content.hash_sha256 "
        "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
        "WHERE file.id = %s AND file.user_id = %s",
        (committed_file_id, USER_ID),
    )
    if replayed_content is None:
        log_fail("response-loss replay preserves the file/content reference")
        print_summary()
    assert_equal("response-loss replay leaves ref_count at one", int(replayed_content["ref_count"]), 1)
    assert_equal("response-loss replay preserves content MD5", replayed_content["hash_md5"], payload_md5)
    assert_equal(
        "response-loss replay preserves content SHA-256",
        replayed_content["hash_sha256"],
        payload_sha256,
    )
    assert_equal(
        "response-loss replay leaves one content row",
        int(
            scalar(
                "SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s AND hash_sha256 = %s",
                (payload_md5, payload_sha256),
            )
            or 0
        ),
        1,
    )
    assert_equal(
        "response-loss replay leaves one cleanup job",
        int(
            scalar(
                "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
                (f"staging-cleanup:{upload_id}",),
            )
            or 0
        ),
        1,
    )
    assert_storage_job_succeeded(
        "finalize-commit response-loss cleanup converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("response-loss cleanup removes the staging session", upload_temp_dir(upload_id))
    assert_path_absent(
        "response-loss cleanup removes the assembled object",
        upload_temp_dir(upload_id).parent / f"{upload_id}.tmp",
    )
    assert_path_exists("response-loss replay preserves the final blob", blob_path)
    assert_equal("response-loss replay preserves exact blob bytes", blob_path.read_bytes(), payload)

    save_evidence(
        f"{EVIDENCE_PREFIX}-{upload_id}-finalize-commit-replay.json",
        json.dumps(
            {
                "upload_id": upload_id,
                "killed_pid": killed_pid,
                "killed_instance_id": killed_instance_id,
                "dropped_request_error": dropped_request_error,
                "completed_file_id": committed_file_id,
                "state_version": committed_state_version,
                "finalize_attempts": int(replayed_task["finalize_attempts"]),
                "file_list_version_before_commit": version_before_commit,
                "file_list_version_after_replay": version_after_replay,
                "replay_instance_id": primary_instance_id,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )
    redis_delete_pattern(f"rate:upload:{USER_ID}:*")


def assert_failed_finalize_recoverable(upload_id: str, filename: str, file_hash: str, quota_before_complete: dict[str, int]) -> None:
    """Assert failed finalization retains a leased task for retry or takeover."""
    assert_db_row_absent(
        "failed finalize creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_db_row_absent(
        "failed finalize creates no content row",
        "SELECT id FROM file_contents WHERE hash_md5 = %s",
        (file_hash,),
    )
    task = assert_upload_task(upload_id, 4)
    assert_equal("failed finalize leaves finalized_at unset", task["finalized_at"], None)
    assert_equal("failed finalize keeps lease owner", bool(task["lease_owner"]), True)
    assert_equal("failed finalize records an error code", task["last_error_code"] is not None, True)
    assert_chunk_row_count(upload_id, 1)
    quota_after_complete = user_quota()
    assert_equal(
        "failed finalize preserves reserved storage",
        quota_after_complete["storage_reserved"],
        quota_before_complete["storage_reserved"],
    )
    assert_equal(
        "failed finalize preserves used storage",
        quota_after_complete["storage_used"],
        quota_before_complete["storage_used"],
    )


def cleanup_failed_finalize_fixture(upload_id: str, blob_path) -> None:
    """Remove one deliberately failed finalization without using business APIs."""
    execute(
        """
        WITH removed AS (
            DELETE FROM upload_tasks
            WHERE id = %s AND user_id = %s AND status = 4
            RETURNING reserved_bytes
        )
        UPDATE users
        SET storage_reserved = storage_reserved - COALESCE((SELECT reserved_bytes FROM removed), 0)
        WHERE id = %s
        """,
        (upload_id, USER_ID, USER_ID),
    )
    blob_path.unlink(missing_ok=True)
    shutil.rmtree(upload_temp_dir(upload_id), ignore_errors=True)
    (upload_temp_dir(upload_id).parent / f"{upload_id}.tmp").unlink(missing_ok=True)


def test_db_failure_after_blob_promotion_retains_created_blob() -> None:
    """Verify a transaction failure retains a recognizable final candidate."""
    log_section("Finalize DB Failure Retains Created Blob")
    payload = f"safety-db-failure-created-{unique_name()}".encode()
    filename = f"safety_db_failure_created_{unique_name()}.bin"

    upload_id, file_hash = init_upload(filename, payload)
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    quota_before_complete = user_quota()
    blob_path = final_blob_path(sha256_bytes(payload))
    assert_path_absent("new-blob failure fixture starts without final blob", blob_path)

    affected = execute(
        "UPDATE upload_tasks SET folder_id = %s WHERE id = %s AND status = 0",
        (9_223_372_036_854_000_000, upload_id),
    )
    assert_equal("new-blob failure fixture corrupts target folder", affected, 1)

    resp = complete_upload_raw(upload_id)
    assert_equal("new-blob finalize failure returns non-success code", json_field(resp.text, "code") != "0", True)
    assert_failed_finalize_recoverable(upload_id, filename, file_hash, quota_before_complete)
    assert_path_exists("created final blob retained after transaction failure", blob_path)
    cleanup_failed_finalize_fixture(upload_id, blob_path)


def test_db_failure_after_promotion_preserves_preexisting_blob() -> None:
    """Verify compensation does not delete a final blob that existed before promotion."""
    log_section("Finalize DB Failure Preserves Preexisting Blob")
    payload = f"safety-db-failure-reused-{unique_name()}".encode()
    filename = f"safety_db_failure_reused_{unique_name()}.bin"
    file_hash = md5_bytes(payload)
    blob_path = final_blob_path(sha256_bytes(payload))

    upload_id, _ = init_upload(filename, payload)
    blob_path.parent.mkdir(parents=True, exist_ok=True)
    blob_path.write_bytes(payload)
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    quota_before_complete = user_quota()
    assert_path_exists("pre-existing final blob fixture is present", blob_path)

    affected = execute(
        "UPDATE upload_tasks SET folder_id = %s WHERE id = %s AND status = 0",
        (9_223_372_036_854_000_001, upload_id),
    )
    assert_equal("pre-existing-blob failure fixture corrupts target folder", affected, 1)

    resp = complete_upload_raw(upload_id)
    assert_equal("pre-existing-blob finalize failure returns non-success code", json_field(resp.text, "code") != "0", True)
    assert_failed_finalize_recoverable(upload_id, filename, file_hash, quota_before_complete)
    assert_path_exists("pre-existing final blob survives transaction failure", blob_path)
    assert_equal("pre-existing final blob bytes unchanged", blob_path.read_bytes(), payload)
    cleanup_failed_finalize_fixture(upload_id, blob_path)


def test_missing_staging_object_records_reconciliation() -> None:
    """Verify operators can diagnose and safely retry without database writes."""
    log_section("Missing Staging Object Reconciliation")
    payload = f"safety-missing-staging-{unique_name()}".encode()
    filename = f"safety_missing_staging_{unique_name()}.bin"
    upload_id, file_hash = init_upload(filename, payload)
    scan_id: str | None = None
    recovered_file_id: int | None = None

    try:
        upload_single_chunk(upload_id, payload)
        chunk = query_one(
            "SELECT object_key FROM upload_task_chunks WHERE task_id = %s AND chunk_index = 0",
            (upload_id,),
        )
        if chunk is None or not chunk["object_key"]:
            log_fail("missing-staging fixture has a persisted object key")
            print_summary()
        chunk_path = upload_temp_dir(upload_id).parent / str(chunk["object_key"])
        assert_path_exists("missing-staging fixture object exists before fault injection", chunk_path)
        chunk_path.unlink()
        assert_path_absent("missing-staging fixture removes only the object", chunk_path)

        quota_before_complete = user_quota()
        request_id = f"safety-complete-{unique_name()}"
        resp = complete_upload_raw(upload_id, request_id)
        assert_equal("missing staging object returns HTTP 400", resp.status_code, 400)
        assert_equal("missing staging object returns ChunkVerifyFailed", json_field(resp.text, "code"), "50009")
        response_request_id = header_value(resp.headers, "X-Request-Id")
        instance_id = header_value(resp.headers, "X-Disk-Instance-Id")
        assert_equal("failed response preserves caller request ID", response_request_id, request_id)
        assert_equal("failed response identifies the handling instance", bool(instance_id), True)
        request_log = wait_for_request_log(request_id, instance_id, resp.status_code)
        log_pass("failed request maps to one instance and completion log")
        assert_failed_finalize_recoverable(
            upload_id,
            filename,
            file_hash,
            quota_before_complete,
        )
        failed_task = query_one(
            "SELECT lease_owner, state_version FROM upload_tasks "
            "WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if failed_task is None:
            log_fail("failed finalize task remains queryable for log correlation")
            print_summary()
            raise AssertionError("unreachable")
        assert_equal(
            "failed finalize lease owner matches handling instance",
            str(failed_task["lease_owner"]),
            instance_id,
        )
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="upload_complete",
            upload_id=upload_id,
            message_marker="Failed to assemble chunks",
            lease_owner=str(failed_task["lease_owner"]),
            state_version=int(failed_task["state_version"]),
        )
        wait_for_correlated_application_log(
            request_id=request_id,
            instance_id=instance_id,
            operation="upload_complete",
            upload_id=upload_id,
            message_marker="[complete_upload]",
            lease_owner=str(failed_task["lease_owner"]),
            state_version=int(failed_task["state_version"]),
        )
        log_pass("failed completion log matches persisted lease owner and state version")

        finding = query_one(
            "SELECT severity, resolution_strategy, resource_locator, details "
            "FROM storage_reconciliation_findings "
            "WHERE finding_type = 'upload_staging_mismatch' AND resource_id = %s",
            (upload_id,),
        )
        if finding is None:
            log_fail("missing staging object persists upload_staging_mismatch")
            print_summary()
        log_pass("missing staging object persists upload_staging_mismatch")
        assert_equal("staging mismatch is critical", int(finding["severity"]), 2)
        assert_equal("staging mismatch requires manual resolution", finding["resolution_strategy"], "manual")
        details = finding["details"]
        scan_id = str(details["scan_id"])
        assert_equal("staging mismatch records bounded scan id", scan_id.startswith("upload-integrity-"), True)
        assert_equal("staging mismatch records state version", int(details["state_version"]) >= 1, True)

        job = query_one(
            "SELECT id, job_type, aggregate_id FROM storage_jobs "
            "WHERE job_type = 'storage_reconcile' AND aggregate_id = %s",
            (scan_id,),
        )
        if job is None:
            log_fail("missing staging object enqueues a durable reconciliation job")
            print_summary()
        log_pass("missing staging object enqueues a durable reconciliation job")

        diagnostic = fetch(
            f"/api/admin/uploads/{upload_id}/diagnostics"
            "?chunk_page=1&chunk_page_size=20&job_page=1&job_page_size=100",
            headers=auth_headers(TOKEN),
        )
        if diagnostic.status_code != 200 or json_field(diagnostic.text, "code") != "0":
            log_fail("failed upload can be inspected through the read-only diagnostic endpoint")
            print(diagnostic.text)
            print_summary()
        diagnostic_data = json.loads(diagnostic.text)["data"]
        diagnostic_task = diagnostic_data["task"]
        diagnostic_chunks = diagnostic_data["chunks"]
        diagnostic_jobs = diagnostic_data["related_jobs"]["items"]
        assert_equal("diagnostic reports Finalizing database state", diagnostic_task["status"], "finalizing")
        assert_equal("diagnostic reports the finalize error code", diagnostic_task["last_error_code"], 50009)
        assert_equal("diagnostic returns the persisted chunk", len(diagnostic_chunks), 1)
        assert_equal("diagnostic reports the missing staging object", diagnostic_chunks[0]["object_head"]["status"], "missing")
        assert_equal("missing object cannot match the DB descriptor", diagnostic_chunks[0]["object_head"]["matches_record"], False)
        matching_jobs = [
            item
            for item in diagnostic_jobs
            if item["job_type"] == "storage_reconcile"
            and item["aggregate_id"] == scan_id
        ]
        assert_equal("diagnostic links the recovery task created by the failure", bool(matching_jobs), True)

        save_evidence(
            f"{EVIDENCE_PREFIX}-{upload_id}-failure-correlation.json",
            json.dumps(
                {
                    "request_id": request_id,
                    "instance_id": instance_id,
                    "upload_id": upload_id,
                    "request_log": request_log,
                    "database": {
                        "status": diagnostic_task["status"],
                        "state_version": diagnostic_task["state_version"],
                        "last_error_code": diagnostic_task["last_error_code"],
                    },
                    "object_head": {
                        "status": diagnostic_chunks[0]["object_head"]["status"],
                        "matches_record": diagnostic_chunks[0]["object_head"]["matches_record"],
                    },
                    "recovery_job": {
                        "id": matching_jobs[0]["id"],
                        "job_type": matching_jobs[0]["job_type"],
                        "status": matching_jobs[0]["status"],
                    },
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
        log_pass("one failed request locates instance, database state, object, and recovery task")

        log_section("API-only Upload Recovery")
        lease = diagnostic_task["lease"]
        if not isinstance(lease, dict) or not lease.get("owner"):
            log_fail("failed finalization exposes a live lease for recovery")
            print_summary()
        assert_equal("failed finalization lease is still live", lease["expired"], False)
        observed_version = int(diagnostic_task["state_version"])
        observed_owner = str(lease["owner"])

        # Repair the failed dependency at its owning storage boundary. From this
        # point onward every upload state transition and audit check uses HTTP.
        chunk_path.parent.mkdir(parents=True, exist_ok=True)
        chunk_path.write_bytes(payload)
        assert_path_exists("operator restores the exact staging object", chunk_path)

        diagnostic_path = (
            f"/api/admin/uploads/{upload_id}/diagnostics"
            "?chunk_page=1&chunk_page_size=20&job_page=1&job_page_size=100"
        )
        restored_diagnostic = fetch(diagnostic_path, headers=auth_headers(TOKEN))
        if restored_diagnostic.status_code != 200 or json_field(restored_diagnostic.text, "code") != "0":
            log_fail("restored staging object can be verified through diagnostics")
            print(restored_diagnostic.text)
            print_summary()
        restored_task = json.loads(restored_diagnostic.text)["data"]["task"]
        restored_chunk = json.loads(restored_diagnostic.text)["data"]["chunks"][0]
        assert_equal("storage repair does not change upload version", int(restored_task["state_version"]), observed_version)
        assert_equal("restored staging object is present", restored_chunk["object_head"]["status"], "present")
        assert_equal("restored staging object matches its descriptor", restored_chunk["object_head"]["matches_record"], True)

        release_path = f"/api/admin/uploads/{upload_id}/lease/release"
        dry_run = fetch(
            release_path,
            method="POST",
            headers=auth_headers(TOKEN),
            json_body={
                "expected_state_version": observed_version,
                "expected_lease_owner": observed_owner,
            },
        )
        if dry_run.status_code != 200 or json_field(dry_run.text, "code") != "0":
            log_fail("lease release dry-run succeeds with diagnostic owner and version")
            print(dry_run.text)
            print_summary()
        dry_run_data = json.loads(dry_run.text)["data"]
        assert_equal("lease release defaults to dry-run", dry_run_data["dry_run"], True)
        assert_equal("matching live lease is eligible", dry_run_data["eligible"], True)
        assert_equal("dry-run reports no mutation", dry_run_data["released"], False)
        assert_equal("dry-run preserves upload version", int(dry_run_data["state_version"]), observed_version)

        release = fetch(
            release_path,
            method="POST",
            headers=auth_headers(TOKEN),
            json_body={
                "dry_run": False,
                "confirm_upload_id": upload_id,
                "expected_state_version": observed_version,
                "expected_lease_owner": observed_owner,
                "reason": "staging dependency restored; retry completion",
            },
        )
        if release.status_code != 200 or json_field(release.text, "code") != "0":
            log_fail("confirmed lease release succeeds")
            print(release.text)
            print_summary()
        release_data = json.loads(release.text)["data"]
        released_version = observed_version + 1
        assert_equal("confirmed command releases the lease", release_data["released"], True)
        assert_equal("lease release keeps Finalizing state", release_data["status"], "finalizing")
        assert_equal("lease release increments the version", int(release_data["state_version"]), released_version)
        assert_equal("released lease is immediately expired", release_data["lease_expired"], True)

        audit_query = urlencode(
            {
                "action": "admin.upload.lease_release",
                "target_type": "upload",
                "target_name": upload_id,
                "page_size": 20,
            }
        )
        audit_response = fetch(
            f"/api/admin/logs?{audit_query}",
            headers=auth_headers(TOKEN),
        )
        if audit_response.status_code != 200 or json_field(audit_response.text, "code") != "0":
            log_fail("recovery audit is queryable through the admin API")
            print(audit_response.text)
            print_summary()
        audit_items = json.loads(audit_response.text)["data"]["items"]
        if len(audit_items) != 1:
            log_fail(f"exact audit filters returned {len(audit_items)} actions instead of one")
            print(audit_response.text)
            print_summary()
        log_pass("exact audit filters return one recovery action")
        audit_item = audit_items[0]
        assert_equal("recovery audit returns string target", audit_item["target_name"], upload_id)
        assert_equal("recovery audit uses upload target type", audit_item["target_type"], "upload")
        assert_equal("string target does not misuse numeric target ID", audit_item["target_id"], None)
        audit_details = audit_item["details"]
        if isinstance(audit_details, str):
            audit_details = json.loads(audit_details)
        assert_equal("recovery audit records previous version", int(audit_details["previous_state_version"]), observed_version)
        assert_equal("recovery audit records released version", int(audit_details["new_state_version"]), released_version)

        retry = complete_upload_raw(upload_id, f"safety-retry-{unique_name()}")
        recovered_file_id_text = json_field(retry.text, "data.file.id")
        if retry.status_code != 200 or json_field(retry.text, "code") != "0" or not recovered_file_id_text:
            log_fail("normal complete retry succeeds after audited lease release")
            print(retry.text)
            print_summary()
        recovered_file_id = int(recovered_file_id_text)

        deadline = time.monotonic() + 20.0
        final_diagnostic_data: dict[str, object] | None = None
        cleanup_status: str | None = None
        while time.monotonic() < deadline:
            final_diagnostic = fetch(diagnostic_path, headers=auth_headers(TOKEN))
            if final_diagnostic.status_code == 200 and json_field(final_diagnostic.text, "code") == "0":
                final_diagnostic_data = json.loads(final_diagnostic.text)["data"]
                cleanup_jobs = [
                    item
                    for item in final_diagnostic_data["related_jobs"]["items"]
                    if item["job_type"] == "staging_cleanup"
                    and item["aggregate_id"] == upload_id
                ]
                if cleanup_jobs:
                    cleanup_status = cleanup_jobs[0]["status"]
                    if cleanup_status in ("succeeded", "dead_letter"):
                        break
            time.sleep(0.05)

        if final_diagnostic_data is None:
            log_fail("completed upload remains available through diagnostics")
            print_summary()
        final_task = final_diagnostic_data["task"]
        assert_equal("safe retry reaches Completed", final_task["status"], "completed")
        assert_equal("safe retry records the completed file", int(final_task["completed_file_id"]), recovered_file_id)
        assert_equal("completed upload clears its lease", final_task["lease"], None)
        assert_equal("successful retry clears the prior error", final_task["last_error_code"], None)
        assert_equal("staging cleanup converges through the worker", cleanup_status, "succeeded")
        assert_path_absent("safe retry cleanup removes staging directory", upload_temp_dir(upload_id))

        save_evidence(
            f"{EVIDENCE_PREFIX}-{upload_id}-operator-recovery.json",
            json.dumps(
                {
                    "upload_id": upload_id,
                    "observed_state_version": observed_version,
                    "released_state_version": released_version,
                    "audit_id": audit_item["id"],
                    "completed_file_id": recovered_file_id,
                    "final_status": final_task["status"],
                    "cleanup_status": cleanup_status,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
        log_pass("operator diagnosis, audited lease release, and safe retry use APIs only")
    finally:
        if scan_id is not None:
            execute(
                "DELETE FROM storage_jobs WHERE job_type = 'storage_reconcile' AND aggregate_id = %s",
                (scan_id,),
            )
        execute(
            "DELETE FROM storage_reconciliation_findings "
            "WHERE finding_type = 'upload_staging_mismatch' AND resource_id = %s",
            (upload_id,),
        )
        execute(
            "DELETE FROM operation_logs "
            "WHERE action = 'admin.upload.lease_release' "
            "AND target_type = 'upload' AND target_name = %s",
            (upload_id,),
        )
        remaining_status = scalar(
            "SELECT status FROM upload_tasks WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if remaining_status == 4:
            cleanup_failed_finalize_fixture(upload_id, final_blob_path(sha256_bytes(payload)))


def test_complete_cancel_expire_race_invariants() -> None:
    """Verify concurrent terminal contenders settle quota and metadata exactly once."""
    log_section("Complete/Cancel/Expire Race Invariants")
    run_expired_cleanup()

    for expired in (False, True):
        scenario = "expired" if expired else "active"
        payload = f"safety-terminal-race-{scenario}-{unique_name()}".encode()
        filename = f"safety_terminal_race_{scenario}_{unique_name()}.bin"
        quota_before = user_quota()
        upload_id, file_hash = init_upload(filename, payload)
        upload_single_chunk(upload_id, payload)
        quota_after_init = user_quota()
        assert_numeric_delta(
            f"{scenario} race init reserves storage",
            quota_before["storage_reserved"],
            quota_after_init["storage_reserved"],
            len(payload),
        )

        if expired:
            affected = execute(
                "UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' "
                "WHERE id = %s AND user_id = %s AND status = 0",
                (upload_id, USER_ID),
            )
            assert_equal("expired race fixture crosses the database deadline", affected, 1)

        complete_response, cancel_response, cleanup_counts = race_complete_cancel_expire(upload_id)
        task = query_one(
            "SELECT status, completed_file_id, lease_owner, lease_expires_at "
            "FROM upload_tasks WHERE id = %s AND user_id = %s",
            (upload_id, USER_ID),
        )
        if task is None:
            log_fail(f"{scenario} terminal race preserves its upload task")
            print_summary()

        status = int(task["status"])
        allowed_statuses = {2, 3} if expired else {1, 2}
        if status not in allowed_statuses:
            log_fail(f"{scenario} terminal race reached illegal status {status}")
            print_summary()
        log_pass(f"{scenario} terminal race reaches one legal terminal state")

        quota_after_race = user_quota()
        assert_equal(
            f"{scenario} terminal race releases reserved storage exactly once",
            quota_after_race["storage_reserved"],
            quota_before["storage_reserved"],
        )
        assert_chunk_row_count(upload_id, 0)
        cleanup_job_count = int(
            scalar(
                "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
                (f"staging-cleanup:{upload_id}",),
            )
            or 0
        )
        assert_equal(f"{scenario} terminal race creates one cleanup job", cleanup_job_count, 1)

        file_row = query_one(
            "SELECT file.id, content.ref_count "
            "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
            "WHERE file.user_id = %s AND file.name = %s",
            (USER_ID, filename),
        )
        content_count = int(
            scalar("SELECT COUNT(*) FROM file_contents WHERE hash_md5 = %s", (file_hash,)) or 0
        )
        complete_code = json_field(complete_response.text, "code")
        cancel_code = json_field(cancel_response.text, "code")

        if status == 1:
            if file_row is None:
                log_fail("completed race creates one logical file")
                print_summary()
            log_pass("completed race creates one logical file")
            assert_equal("completed race records the winning file", int(task["completed_file_id"]), int(file_row["id"]))
            assert_equal("completed race creates one content row", content_count, 1)
            assert_equal("completed race increments content reference once", int(file_row["ref_count"]), 1)
            assert_numeric_delta(
                "completed race converts reserved storage to used once",
                quota_before["storage_used"],
                quota_after_race["storage_used"],
                len(payload),
            )
            assert_equal("completed race returns the winning complete response", complete_code, "0")
            assert_equal("completed race rejects cancellation", cancel_code == "0", False)
        else:
            assert_equal(f"{scenario} non-completed race creates no file", file_row, None)
            assert_equal(f"{scenario} non-completed race creates no content row", content_count, 0)
            assert_equal(
                f"{scenario} non-completed race preserves used storage",
                quota_after_race["storage_used"],
                quota_before["storage_used"],
            )
            assert_equal(f"{scenario} non-completed race rejects completion", complete_code == "0", False)
            if status == 2:
                assert_equal(f"{scenario} cancelled race returns success", cancel_code, "0")
            else:
                assert_equal("expired winner rejects cancellation", cancel_code == "0", False)

        assert_equal(f"{scenario} terminal race clears lease owner", task["lease_owner"], None)
        assert_equal(f"{scenario} terminal race clears lease deadline", task["lease_expires_at"], None)
        assert_storage_job_succeeded(
            f"{scenario} terminal race cleanup converges",
            f"staging-cleanup:{upload_id}",
        )
        save_evidence(
            f"{EVIDENCE_PREFIX}-{upload_id}-terminal-race.json",
            json.dumps(
                {
                    "scenario": scenario,
                    "upload_id": upload_id,
                    "status": status,
                    "complete_code": complete_code,
                    "cancel_code": cancel_code,
                    "expired_upload_tasks_cleaned": cleanup_counts[
                        "expired_upload_tasks_cleaned"
                    ],
                    "storage_used_before": quota_before["storage_used"],
                    "storage_used_after": quota_after_race["storage_used"],
                    "storage_reserved_before": quota_before["storage_reserved"],
                    "storage_reserved_after": quota_after_race["storage_reserved"],
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )


def test_cancel_upload_invariants() -> None:
    """Verify cancel correlation, versioning, and cleanup invariants."""
    log_section("Canceled Upload Invariants")
    payload = f"safety-cancel-{unique_name()}".encode()
    filename = f"safety_cancel_{unique_name()}.bin"
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    quota_after_init = user_quota()
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    task_before_cancel = assert_upload_task(upload_id, 0)
    version_before_cancel = int(task_before_cancel["state_version"])

    cancel_request_id = f"safety-cancel-log-{unique_name()}"
    cancel_response = cancel_upload_raw(upload_id, cancel_request_id)
    assert_equal("cancel request returns HTTP 200", cancel_response.status_code, 200)
    assert_equal("cancel request returns success code", json_field(cancel_response.text, "code"), "0")
    assert_equal(
        "cancel response preserves caller request ID",
        header_value(cancel_response.headers, "X-Request-Id"),
        cancel_request_id,
    )
    cancel_instance_id = header_value(
        cancel_response.headers,
        "X-Disk-Instance-Id",
    )

    assert_chunk_row_count(upload_id, 0)
    quota_after_cancel = user_quota()

    cancelled_task = assert_upload_task(upload_id, 2)
    cancelled_version = int(cancelled_task["state_version"])
    assert_equal(
        "cancel transition increments state_version once",
        cancelled_version,
        version_before_cancel + 1,
    )
    wait_for_correlated_application_log(
        request_id=cancel_request_id,
        instance_id=cancel_instance_id,
        operation="upload_cancel",
        upload_id=upload_id,
        message_marker="outcome=success",
        state_version=cancelled_version,
    )
    log_pass("cancel lifecycle summary records the committed version")

    assert_numeric_delta(
        "cancel releases reserved storage",
        quota_after_init["storage_reserved"],
        quota_after_cancel["storage_reserved"],
        -len(payload),
    )
    assert_equal("cancel preserves used storage", quota_after_cancel["storage_used"], quota_before["storage_used"])
    assert_db_row_absent(
        "cancel creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_storage_job_succeeded(
        "cancel cleanup job converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("temp upload directory cleaned after cancel", upload_temp_dir(upload_id))
    assert_path_absent("final blob absent after cancel", final_blob_path(sha256_bytes(payload)))

    replay_request_id = f"safety-cancel-replay-log-{unique_name()}"
    replay_response = cancel_upload_raw(upload_id, replay_request_id)
    assert_equal("cancel replay returns HTTP 200", replay_response.status_code, 200)
    assert_equal("cancel replay returns success code", json_field(replay_response.text, "code"), "0")
    assert_equal(
        "cancel replay preserves caller request ID",
        header_value(replay_response.headers, "X-Request-Id"),
        replay_request_id,
    )
    replay_instance_id = header_value(
        replay_response.headers,
        "X-Disk-Instance-Id",
    )
    replayed_task = assert_upload_task(upload_id, 2)
    assert_equal(
        "cancel replay preserves state_version",
        int(replayed_task["state_version"]),
        cancelled_version,
    )
    assert_equal(
        "cancel replay preserves released quota",
        user_quota()["storage_reserved"],
        quota_after_cancel["storage_reserved"],
    )
    assert_equal(
        "cancel creates one cleanup job",
        int(
            scalar(
                "SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s",
                (f"staging-cleanup:{upload_id}",),
            )
            or 0
        ),
        1,
    )
    wait_for_correlated_application_log(
        request_id=replay_request_id,
        instance_id=replay_instance_id,
        operation="upload_cancel",
        upload_id=upload_id,
        message_marker="outcome=replay",
        state_version=cancelled_version,
    )
    log_pass("cancel replay keeps the persisted version and typed correlation")


def test_expired_upload_cleanup_invariants() -> None:
    """Verify deterministic expired-upload cleanup releases reservations and temp artifacts."""
    log_section("Expired Upload Cleanup Invariants")
    run_expired_cleanup()
    payload = f"safety-expire-{unique_name()}".encode()
    filename = f"safety_expire_{unique_name()}.bin"
    quota_before = user_quota()

    upload_id, file_hash = init_upload(filename, payload)
    quota_after_init = user_quota()
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)
    task_before_expiry = assert_upload_task(upload_id, 0)
    version_before_expiry = int(task_before_expiry["state_version"])
    assert_numeric_delta(
        "expire fixture reserves storage",
        quota_before["storage_reserved"],
        quota_after_init["storage_reserved"],
        len(payload),
    )

    affected = execute(
        "UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' WHERE id = %s AND status = 0",
        (upload_id,),
    )
    assert_equal("expire fixture marks upload task expired in DB", affected, 1)

    cleanup_request_id = f"safety-cleanup-log-{unique_name()}"
    cleanup_counts = run_expired_cleanup(
        cleanup_request_id,
        expected_upload_id=upload_id,
        expected_state_version=version_before_expiry + 1,
    )
    quota_after_cleanup = user_quota()

    assert_equal("cleanup reports at least one expired upload", cleanup_counts["expired_upload_tasks_cleaned"] >= 1, True)
    assert_chunk_row_count(upload_id, 0)
    task = assert_upload_task(upload_id, 3)
    assert_equal(
        "expire transition increments state_version once",
        int(task["state_version"]),
        version_before_expiry + 1,
    )
    assert_equal("expired task fail_reason documents expiry", task["fail_reason"], "任务过期")
    assert_numeric_delta(
        "expired upload cleanup releases reserved storage",
        quota_after_init["storage_reserved"],
        quota_after_cleanup["storage_reserved"],
        -len(payload),
    )
    assert_equal("expired upload cleanup preserves used storage", quota_after_cleanup["storage_used"], quota_before["storage_used"])
    assert_db_row_absent(
        "expired upload cleanup creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_storage_job_succeeded(
        "expired upload cleanup job converges",
        f"staging-cleanup:{upload_id}",
    )
    assert_path_absent("temp upload directory cleaned after expiry", upload_temp_dir(upload_id))
    assert_path_absent("assembled temp artifact absent after expiry", upload_temp_dir(upload_id).parent / f"{upload_id}.tmp")
    assert_path_absent("final blob absent after expiry", final_blob_path(sha256_bytes(payload)))


def test_init_upload_expires_existing_task_invariants() -> None:
    """Verify upload init cleanup releases expired task quota before reserving replacement."""
    log_section("Upload Init Inline Expired Cleanup Invariants")
    payload = f"safety-init-expire-{unique_name()}".encode()
    filename = f"safety_init_expire_{unique_name()}.bin"
    quota_before = user_quota()

    old_upload_id, file_hash = init_upload(filename, payload)
    quota_after_first_init = user_quota()
    upload_single_chunk(old_upload_id, payload)
    assert_numeric_delta(
        "inline expiry fixture reserves storage",
        quota_before["storage_reserved"],
        quota_after_first_init["storage_reserved"],
        len(payload),
    )

    affected = execute(
        "UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' WHERE id = %s AND status = 0",
        (old_upload_id,),
    )
    assert_equal("inline expiry fixture marks upload task expired in DB", affected, 1)

    new_upload_id, _ = init_upload(filename, payload)
    quota_after_second_init = user_quota()

    assert_equal("inline expiry init creates replacement upload id", new_upload_id != old_upload_id, True)
    old_task = assert_upload_task(old_upload_id, 3)
    assert_equal("inline expired task fail_reason documents expiry", old_task["fail_reason"], "任务过期")
    new_task = assert_upload_task(new_upload_id, 0)
    assert_equal("replacement task reserved_bytes equals file size", int(new_task["reserved_bytes"]), len(payload))
    assert_numeric_delta(
        "inline expired init does not double-reserve storage",
        quota_before["storage_reserved"],
        quota_after_second_init["storage_reserved"],
        len(payload),
    )
    assert_equal("inline expired init preserves used storage", quota_after_second_init["storage_used"], quota_before["storage_used"])
    assert_db_row_absent(
        "inline expired init creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_storage_job_succeeded(
        "inline expiry cleanup job converges",
        f"staging-cleanup:{old_upload_id}",
    )
    assert_path_absent("old temp upload directory cleaned by inline expiry", upload_temp_dir(old_upload_id))
    assert_path_absent(
        "old assembled temp artifact absent after inline expiry",
        upload_temp_dir(old_upload_id).parent / f"{old_upload_id}.tmp",
    )
    assert_path_absent("final blob absent after inline expiry", final_blob_path(sha256_bytes(payload)))


def test_complete_upload_db_failure_after_promotion_retains_final_blob() -> None:
    """Verify promoted blob remains identifiable when DB finalization fails."""
    log_section("Complete Upload Promotion Recovery Invariants")
    payload = f"safety-db-failure-{unique_name()}".encode()
    filename = f"safety_db_failure_{unique_name()}.bin"

    upload_id, file_hash = init_upload(filename, payload)
    upload_single_chunk(upload_id, payload)
    assert_chunk_row_count(upload_id, 1)

    execute(
        """
        CREATE OR REPLACE FUNCTION fail_safety_upload_file_insert()
        RETURNS trigger AS $$
        BEGIN
            IF NEW.name LIKE 'safety_db_failure_%' THEN
                RAISE EXCEPTION 'intentional safety upload finalization failure';
            END IF;
            RETURN NEW;
        END;
        $$ LANGUAGE plpgsql
        """
    )
    execute("DROP TRIGGER IF EXISTS safety_upload_file_insert_fail ON files")
    execute(
        """
        CREATE TRIGGER safety_upload_file_insert_fail
        BEFORE INSERT ON files
        FOR EACH ROW EXECUTE FUNCTION fail_safety_upload_file_insert()
        """
    )

    request_id = f"safety-transaction-{unique_name()}"
    instance_id = ""
    try:
        resp = fetch(
            "/api/file/upload/complete",
            method="POST",
            headers={**auth_headers(TOKEN), "X-Request-Id": request_id},
            json_body={"upload_id": upload_id},
        )
        save_evidence(f"{EVIDENCE_PREFIX}-{upload_id}-complete-db-failure.json", resp.text)
        if resp.status_code == 200 and json_field(resp.text, "code") == "0":
            log_fail("complete upload should fail after trigger rejects files insert")
            print(resp.text)
            print_summary()

        log_pass("complete upload failed after files insert trigger")
        assert_equal(
            "failed transaction response preserves caller request ID",
            header_value(resp.headers, "X-Request-Id"),
            request_id,
        )
        instance_id = header_value(resp.headers, "X-Disk-Instance-Id")
        assert_equal(
            "failed transaction response identifies the handling instance",
            bool(instance_id),
            True,
        )
    finally:
        execute("DROP TRIGGER IF EXISTS safety_upload_file_insert_fail ON files")
        execute("DROP FUNCTION IF EXISTS fail_safety_upload_file_insert()")

    task = assert_upload_task(upload_id, 4)
    assert_equal("failed finalization keeps reserved_bytes", int(task["reserved_bytes"]), len(payload))
    assert_equal(
        "failed transaction lease owner matches the handling instance",
        str(task["lease_owner"]),
        instance_id,
    )
    wait_for_correlated_application_log(
        request_id=request_id,
        instance_id=instance_id,
        operation="upload_complete",
        upload_id=upload_id,
        message_marker="intentional safety upload finalization failure",
        lease_owner=str(task["lease_owner"]),
        state_version=int(task["state_version"]),
    )
    assert_no_unscoped_application_log(
        "intentional safety upload finalization failure",
        exact=False,
        assertion="transaction failure emits no unscoped duplicate",
    )
    log_pass("transaction failure log matches response and persisted upload ownership")
    assert_chunk_row_count(upload_id, 1)
    assert_db_row_absent(
        "failed finalization creates no logical file row",
        "SELECT id FROM files WHERE user_id = %s AND name = %s",
        (USER_ID, filename),
    )
    assert_db_row_absent(
        "failed finalization rolls back file content row",
        "SELECT id FROM file_contents WHERE hash_md5 = %s",
        (file_hash,),
    )
    blob_path = final_blob_path(sha256_bytes(payload))
    assert_path_exists("promoted final blob retained after DB failure", blob_path)
    assert_equal("temp upload directory remains for retry", upload_temp_dir(upload_id).exists(), True)
    cleanup_failed_finalize_fixture(upload_id, blob_path)


def main() -> None:
    """Run upload safety-net tests."""
    print("==========================================")
    print("Upload Safety-Net Integration Tests")
    print("==========================================")
    print()

    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    SERVER_LOG_PATH.unlink(missing_ok=True)
    os.environ.setdefault("DISK_INSTANCE_ID", "safety-upload-api")
    ensure_server()

    global TOKEN, USER_ID
    TOKEN = do_login(TEST_USER, TEST_PASS)
    if not TOKEN:
        sys.exit(1)
    USER_ID = current_user_id()
    log_info(f"Using user_id={USER_ID}, chunk_size={configured_chunk_size()}, base_url={BASE_URL}")

    test_init_and_chunk_log_context_invariants()
    test_quota_log_context_invariants()
    test_file_query_log_context_invariants()
    test_file_mutation_log_context_invariants()
    test_folder_log_context_invariants()
    test_trash_log_context_invariants()
    test_system_info_log_context_invariants()
    test_operation_log_context_invariants()
    test_user_log_context_invariants()
    test_auth_log_context_invariants()
    test_auth_filter_log_context_invariants()
    test_share_log_context_invariants()
    test_admin_log_context_invariants()
    test_successful_chunked_upload_invariants()
    test_hundred_concurrent_complete_invariants()
    test_chunk_metadata_failure_retry_and_orphan_cleanup_invariants()
    test_finalize_claim_process_death_takeover_invariants()
    test_assembled_object_process_death_takeover_invariants()
    test_finalize_renewal_network_partition_fencing_invariants()
    test_final_transaction_process_death_replay_invariants()
    test_db_failure_after_blob_promotion_retains_created_blob()
    test_db_failure_after_promotion_preserves_preexisting_blob()
    test_missing_staging_object_records_reconciliation()
    test_complete_cancel_expire_race_invariants()
    test_cancel_upload_invariants()
    test_expired_upload_cleanup_invariants()
    test_init_upload_expires_existing_task_invariants()
    test_complete_upload_db_failure_after_promotion_retains_final_blob()

    print_summary()


if __name__ == "__main__":
    main()

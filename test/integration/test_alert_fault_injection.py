#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "prometheus-client", "psycopg[binary]"]
# ///

"""Inject storage-job and S3 faults, then assert the real metrics cross alert thresholds."""

from __future__ import annotations

import json
import os
import socket
import subprocess
import tempfile
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

import httpx
import psycopg
from prometheus_client.parser import text_string_to_metric_families


class TestFailure(RuntimeError):
    """Raised when an alert fault-injection invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TestFailure(message)
    print(f"PASS: {message}")


class FakeS3State:
    def __init__(self) -> None:
        self._mode = "healthy"
        self._lock = threading.Lock()

    def set_mode(self, mode: str) -> None:
        with self._lock:
            self._mode = mode

    def mode(self) -> str:
        with self._lock:
            return self._mode


class ControlledS3Server(ThreadingHTTPServer):
    def __init__(self, state: FakeS3State) -> None:
        super().__init__(("127.0.0.1", 0), ControlledS3Handler)
        self.state = state


class ControlledS3Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server: ControlledS3Server

    def log_message(self, format_string: str, *args: object) -> None:
        del format_string, args

    def _send(self, status: int, body: bytes = b"") -> None:
        self.send_response(status)
        self.send_header("Content-Type", "application/xml")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.send_header("x-amz-request-id", "disk-alert-fixture")
        self.end_headers()
        if self.command != "HEAD" and body:
            self.wfile.write(body)

    def _send_error(self, status: int, code: str) -> None:
        body = (
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            f"<Error><Code>{code}</Code><Message>injected failure</Message>"
            "<RequestId>disk-alert-fixture</RequestId></Error>"
        ).encode("ascii")
        self._send(status, body)

    def _respond(self) -> None:
        mode = self.server.state.mode()
        if mode == "transient":
            self._send_error(503, "SlowDown")
            return
        if mode == "permanent":
            self._send_error(403, "AccessDenied")
            return
        if self.command == "HEAD":
            self._send(200)
            return

        body = (
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
            "<Name>disk-alert-fixture</Name><Prefix>objects/</Prefix><KeyCount>0</KeyCount>"
            "<MaxKeys>1</MaxKeys><IsTruncated>false</IsTruncated></ListBucketResult>"
        ).encode("ascii")
        self._send(200, body)

    def do_HEAD(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self._respond()

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self._respond()


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def database_config(config: dict[str, Any]) -> dict[str, Any]:
    configured = config["db_clients"][0]
    return {
        "host": os.environ.get("DATABASE_HOST", os.environ.get("PGHOST", configured["host"])),
        "port": int(
            os.environ.get("DATABASE_PORT", os.environ.get("PGPORT", configured["port"]))
        ),
        "dbname": os.environ.get(
            "DATABASE_NAME", os.environ.get("PGDATABASE", configured["dbname"])
        ),
        "user": os.environ.get(
            "DATABASE_USER", os.environ.get("PGUSER", configured["user"])
        ),
        "password": os.environ.get(
            "DATABASE_PASSWORD", os.environ.get("PGPASSWORD", configured.get("passwd", ""))
        ),
    }


def apply_database_config(config: dict[str, Any], connection: dict[str, Any]) -> None:
    configured = config["db_clients"][0]
    configured["host"] = connection["host"]
    configured["port"] = connection["port"]
    configured["dbname"] = connection["dbname"]
    configured["user"] = connection["user"]
    configured["passwd"] = connection["password"]


def metric_total(
    metrics_text: str,
    name: str,
    required_labels: dict[str, str] | None = None,
) -> float:
    labels = required_labels or {}
    values = [
        float(sample.value)
        for family in text_string_to_metric_families(metrics_text)
        for sample in family.samples
        if sample.name == name
        and all(sample.labels.get(key) == value for key, value in labels.items())
    ]
    require(bool(values), f"metrics expose {name} with labels {labels}")
    return sum(values)


def dependency_error_total(metrics_text: str, outcomes: tuple[str, ...]) -> float:
    return sum(
        metric_total(
            metrics_text,
            "disk_dependency_calls_total",
            {"dependency": "s3", "outcome": outcome},
        )
        for outcome in outcomes
    )


def wait_for_server(process: subprocess.Popen[bytes], client: httpx.Client) -> None:
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise TestFailure(f"Disk process exited during startup with code {process.returncode}")
        try:
            response = client.get("/api/auth/login")
            if response.status_code in {400, 401, 405}:
                return
        except httpx.HTTPError:
            pass
        time.sleep(0.2)
    raise TestFailure("Disk process did not become ready within 30 seconds")


def stop_process(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    server_bin = Path(
        os.environ.get("SERVER_BIN", repo_root / "build/linux-debug-clang/src/disk")
    ).resolve()
    if not server_bin.is_file():
        print(f"FAIL: server binary not found: {server_bin}")
        return 1

    run_id = uuid.uuid4().hex[:12]
    pending_dedupe_key = f"alert-backlog:{run_id}"
    expired_dedupe_key = f"alert-expired-lease:{run_id}"
    instance_id = f"alert-api-{run_id}"
    disk_port = reserve_port()
    state = FakeS3State()
    s3_server = ControlledS3Server(state)
    s3_thread = threading.Thread(target=s3_server.serve_forever, daemon=True)
    s3_thread.start()
    s3_port = int(s3_server.server_address[1])

    process: subprocess.Popen[bytes] | None = None
    fixtures_created = False
    db_connection: dict[str, Any] | None = None
    result = 1
    log_path = repo_root / ".sisyphus" / "evidence" / f"alert-fault-{run_id}.log"

    try:
        base_config = json.loads((repo_root / "config.json").read_text(encoding="utf-8"))
        db_connection = database_config(base_config)

        with tempfile.TemporaryDirectory(prefix="disk-alert-fault-") as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            config = json.loads(json.dumps(base_config))
            apply_database_config(config, db_connection)
            config["listeners"] = [{"address": "127.0.0.1", "port": disk_port}]
            config["app"]["upload_path"] = str(temp_dir / "drogon-upload")
            disk_config = config["custom_config"]["disk"]
            disk_config["process_role"] = "api"
            disk_config["instance_id"] = instance_id
            disk_config["storage_backend"] = "s3"
            disk_config["upload_staging_backend"] = "local"
            disk_config["storage_base_path"] = str(temp_dir / "unused-local-blobs")
            disk_config["temp_upload_path"] = str(temp_dir / "staging")
            disk_config["s3"] = {
                "bucket": "disk-alert-fixture",
                "region": "us-east-1",
                "endpoint": f"http://127.0.0.1:{s3_port}",
                "use_ssl": False,
                "force_path_style": True,
                "verify_ssl": False,
                "object_prefix": f"objects/alert-{run_id}",
                "staging_prefix": f"staging/alert-{run_id}",
                "max_connections": 4,
                "io_threads": 2,
                "connect_timeout_ms": 500,
                "request_timeout_ms": 2000,
                "max_retries": 0,
                "retry_base_delay_ms": 1,
            }
            config_path = temp_dir / "config.json"
            config_path.write_text(json.dumps(config, indent=2), encoding="utf-8")

            server_env = os.environ.copy()
            server_env.pop("DISK_SECURE_MODE", None)
            server_env.update(
                {
                    "JWT_SECRET": "dev-only-jwt-secret-key-change-in-production-2024",
                    "DISK_CONFIG_FILE": str(config_path),
                    "DISK_LISTEN_ADDRESS": "127.0.0.1",
                    "DISK_LISTEN_PORT": str(disk_port),
                    "DISK_PROCESS_ROLE": "api",
                    "DISK_INSTANCE_ID": instance_id,
                    "DISK_STORAGE_BACKEND": "s3",
                    "DISK_UPLOAD_STAGING_BACKEND": "local",
                    "DISK_S3_BUCKET": "disk-alert-fixture",
                    "DISK_S3_REGION": "us-east-1",
                    "DISK_S3_ENDPOINT": f"http://127.0.0.1:{s3_port}",
                    "DISK_S3_USE_SSL": "false",
                    "DISK_S3_FORCE_PATH_STYLE": "true",
                    "DISK_S3_VERIFY_SSL": "false",
                    "DISK_S3_OBJECT_PREFIX": f"objects/alert-{run_id}",
                    "DISK_S3_STAGING_PREFIX": f"staging/alert-{run_id}",
                    "DISK_S3_MAX_CONNECTIONS": "4",
                    "DISK_S3_IO_THREADS": "2",
                    "DISK_S3_CONNECT_TIMEOUT_MS": "500",
                    "DISK_S3_REQUEST_TIMEOUT_MS": "2000",
                    "DISK_S3_MAX_RETRIES": "0",
                    "DISK_S3_RETRY_BASE_DELAY_MS": "1",
                    "DISK_S3_ACCESS_KEY": "fixture-access-key",
                    "DISK_S3_SECRET_KEY": "fixture-secret-key",
                    "AWS_EC2_METADATA_DISABLED": "true",
                }
            )

            log_path.parent.mkdir(parents=True, exist_ok=True)
            with log_path.open("wb") as log_handle:
                process = subprocess.Popen(
                    [str(server_bin)],
                    cwd=repo_root,
                    env=server_env,
                    stdout=log_handle,
                    stderr=subprocess.STDOUT,
                )
                process_id = process.pid

                with httpx.Client(
                    base_url=f"http://127.0.0.1:{disk_port}", timeout=10
                ) as client:
                    wait_for_server(process, client)
                    require(True, "Disk API process starts against the controlled S3 endpoint")

                    baseline_response = client.get("/metrics")
                    require(baseline_response.status_code == 200, "baseline metrics scrape succeeds")
                    baseline_oldest_age = metric_total(
                        baseline_response.text,
                        "disk_storage_jobs_oldest_ready_age_seconds",
                    )
                    baseline_expired_leases = metric_total(
                        baseline_response.text,
                        "disk_storage_jobs_expired_leases",
                    )
                    baseline_transient_errors = dependency_error_total(
                        baseline_response.text,
                        ("timeout", "connection", "retryable"),
                    )
                    baseline_permanent_errors = dependency_error_total(
                        baseline_response.text,
                        ("permanent", "protocol", "other"),
                    )
                    backlog_age_seconds = max(600, int(baseline_oldest_age) + 600)

                    with psycopg.connect(**db_connection) as connection:
                        with connection.cursor() as cursor:
                            cursor.execute(
                                """
                                INSERT INTO storage_jobs
                                    (job_type, aggregate_id, dedupe_key, payload, status,
                                     available_at, created_at, updated_at)
                                VALUES
                                    ('storage_reconcile', %s, %s, '{}'::jsonb, 0,
                                     NOW() - (%s * INTERVAL '1 second'),
                                     NOW() - (%s * INTERVAL '1 second'),
                                     NOW() - (%s * INTERVAL '1 second'))
                                """,
                                (
                                    f"backlog-{run_id}",
                                    pending_dedupe_key,
                                    backlog_age_seconds,
                                    backlog_age_seconds,
                                    backlog_age_seconds,
                                ),
                            )
                            cursor.execute(
                                """
                                INSERT INTO storage_jobs
                                    (job_type, aggregate_id, dedupe_key, payload, status,
                                     attempts, locked_by, locked_until)
                                VALUES
                                    ('storage_reconcile', %s, %s, '{}'::jsonb, 1,
                                     1, %s, NOW() - INTERVAL '2 minutes')
                                """,
                                (f"expired-{run_id}", expired_dedupe_key, instance_id),
                            )
                        connection.commit()
                    fixtures_created = True

                    metrics_response = client.get("/metrics")
                    require(metrics_response.status_code == 200, "metrics scrape succeeds")
                    oldest_ready_age = metric_total(
                        metrics_response.text, "disk_storage_jobs_oldest_ready_age_seconds"
                    )
                    expired_leases = metric_total(
                        metrics_response.text, "disk_storage_jobs_expired_leases"
                    )
                    require(
                        oldest_ready_age > 300,
                        "injected backlog crosses the 300 second threshold",
                    )
                    require(
                        oldest_ready_age >= baseline_oldest_age + 300,
                        "injected backlog advances the oldest-job gauge beyond its baseline",
                    )
                    require(
                        expired_leases >= baseline_expired_leases + 1,
                        "injected expired lease increments the gauge from its baseline",
                    )

                    state.set_mode("transient")
                    for attempt in range(5):
                        readiness = client.get("/api/health/ready")
                        require(
                            readiness.status_code == 503,
                            f"transient S3 failure makes readiness unhealthy ({attempt + 1}/5)",
                        )

                    metrics_response = client.get("/metrics")
                    require(
                        metrics_response.status_code == 200,
                        "metrics remain available during S3 failure",
                    )
                    transient_errors = dependency_error_total(
                        metrics_response.text,
                        ("timeout", "connection", "retryable"),
                    )
                    require(
                        transient_errors >= baseline_transient_errors + 5,
                        "five injected S3 503 responses cross the transient alert threshold",
                    )

                    liveness = client.get("/api/health/live")
                    require(
                        liveness.status_code == 200
                        and liveness.json().get("data", {}).get("overall_status") == "healthy",
                        "S3 503 responses do not fail process liveness",
                    )
                    require(
                        "fixture-secret-key" not in readiness.text
                        and f"http://127.0.0.1:{s3_port}" not in readiness.text,
                        "S3 failure readiness response does not expose credentials or endpoint",
                    )

                    state.set_mode("healthy")
                    recovered = False
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        readiness = client.get("/api/health/ready")
                        if (
                            readiness.status_code == 200
                            and readiness.json().get("data", {}).get("overall_status") == "healthy"
                        ):
                            recovered = True
                            break
                        time.sleep(0.2)
                    require(recovered, "readiness recovers after the S3 503 fault is removed")
                    require(
                        process.poll() is None and process.pid == process_id,
                        "S3 recovery uses the original Disk API process",
                    )

                    state.set_mode("permanent")
                    readiness = client.get("/api/health/ready")
                    require(
                        readiness.status_code == 503,
                        "permanent S3 failure makes readiness unhealthy",
                    )
                    metrics_response = client.get("/metrics")
                    permanent_errors = dependency_error_total(
                        metrics_response.text,
                        ("permanent", "protocol", "other"),
                    )
                    require(
                        permanent_errors >= baseline_permanent_errors + 1,
                        "injected S3 403 response crosses the permanent alert threshold",
                    )

                stop_process(process)

        print("PASS: alert fault injection metrics contract is valid")
        result = 0
    except (OSError, TestFailure, httpx.HTTPError, psycopg.Error) as error:
        print(f"FAIL: {error}")
        print(f"Disk process log: {log_path}")
    finally:
        stop_process(process)
        if fixtures_created and db_connection is not None:
            try:
                with psycopg.connect(**db_connection) as connection:
                    with connection.cursor() as cursor:
                        cursor.execute(
                            "DELETE FROM storage_jobs WHERE dedupe_key IN (%s, %s)",
                            (pending_dedupe_key, expired_dedupe_key),
                        )
                    connection.commit()
            except psycopg.Error as error:
                print(f"FAIL: could not remove alert fixtures: {error}")
                result = 1
        s3_server.shutdown()
        s3_server.server_close()
        s3_thread.join(timeout=5)

    return result


if __name__ == "__main__":
    raise SystemExit(main())

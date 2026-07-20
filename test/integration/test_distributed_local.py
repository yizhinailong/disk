#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "boto3",
#   "httpx",
#   "psycopg[binary]",
#   "python-dotenv",
# ]
# ///

"""Run the Compose distributed-flow contract with isolated local processes."""

from __future__ import annotations

import http.client
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, BinaryIO, Callable

import boto3
from botocore.config import Config

sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_distributed_flow as distributed_flow


REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_ROOT = REPO_ROOT / ".sisyphus" / "evidence"
JWT_SECRET = "distributed-local-jwt-secret-2026-07-20"
DATABASE_USER = "disk"
DATABASE_PASSWORD = "distributed-local-database-password"
REDIS_PASSWORD = "distributed-local-redis-password"
MINIO_USER = "disk-local"
MINIO_PASSWORD = "distributed-local-minio-password"
BUCKET = "disk"
HOP_BY_HOP_HEADERS = {
    "connection",
    "keep-alive",
    "proxy-authenticate",
    "proxy-authorization",
    "te",
    "trailer",
    "transfer-encoding",
    "upgrade",
}
IDEMPOTENT_METHODS = {"GET", "HEAD", "OPTIONS"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def resolve_executable(name: str, alternatives: tuple[str, ...] = ()) -> Path:
    for candidate in (name, *alternatives):
        path = shutil.which(candidate)
        if path is not None:
            return Path(path).resolve()
    raise AssertionError(f"required executable is missing: {name}")


def resolve_server_binary() -> Path:
    configured = os.environ.get("SERVER_BIN")
    candidates = [
        Path(configured) if configured else REPO_ROOT / "build/linux-debug-clang/src/disk",
        REPO_ROOT / "build/linux-debug-clang/disk",
    ]
    for candidate in candidates:
        path = candidate if candidate.is_absolute() else REPO_ROOT / candidate
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
    raise AssertionError("current server binary is missing; build it first or set SERVER_BIN")


def resolve_minio_binary() -> Path:
    configured = os.environ.get("DISK_MINIO_BIN")
    require(bool(configured), "DISK_MINIO_BIN must point to an executable MinIO server")
    candidate = Path(str(configured))
    path = candidate if candidate.is_absolute() else REPO_ROOT / candidate
    require(
        path.is_file() and os.access(path, os.X_OK),
        f"DISK_MINIO_BIN is not executable: {path}",
    )
    return path.resolve()


def run_checked(
    command: list[str],
    *,
    cwd: Path = REPO_ROOT,
    environment: dict[str, str] | None = None,
    input_text: str | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        input=input_text,
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


def allocate_ports(count: int) -> list[int]:
    ports: set[int] = set()
    while len(ports) < count:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            ports.add(int(listener.getsockname()[1]))
    return list(ports)


def http_healthy(url: str) -> bool:
    try:
        with urllib.request.urlopen(url, timeout=1.0) as response:
            return response.status == 200
    except (OSError, urllib.error.URLError):
        return False


def redis_healthy(port: int) -> bool:
    auth = (
        f"*2\r\n$4\r\nAUTH\r\n${len(REDIS_PASSWORD)}\r\n"
        f"{REDIS_PASSWORD}\r\n"
    ).encode()
    ping = b"*1\r\n$4\r\nPING\r\n"
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1.0) as client:
            client.sendall(auth)
            if not client.recv(128).startswith(b"+OK"):
                return False
            client.sendall(ping)
            return client.recv(128).startswith(b"+PONG")
    except OSError:
        return False


class ManagedProcess:
    def __init__(
        self,
        name: str,
        command: list[str],
        working_directory: Path,
        environment: dict[str, str],
        ready: Callable[[], bool],
        *,
        stop_signal: signal.Signals = signal.SIGTERM,
        ready_timeout: float = 60.0,
        stop_timeout: float = 35.0,
    ) -> None:
        self.name = name
        self.command = command
        self.working_directory = working_directory
        self.environment = environment
        self.ready = ready
        self.stop_signal = stop_signal
        self.ready_timeout = ready_timeout
        self.stop_timeout = stop_timeout
        self.log_path = working_directory / f"{name}.log"
        self.process: subprocess.Popen[bytes] | None = None
        self.log_handle: BinaryIO | None = None
        self.started_at_ns = 0
        self.start_count = 0

    def start(self) -> None:
        if self.process is not None and self.process.poll() is None:
            return
        self.working_directory.mkdir(parents=True, exist_ok=True)
        self.log_handle = self.log_path.open("ab")
        self.process = subprocess.Popen(
            self.command,
            cwd=self.working_directory,
            env=self.environment,
            stdout=self.log_handle,
            stderr=subprocess.STDOUT,
        )
        self.started_at_ns = time.time_ns()
        self.start_count += 1
        deadline = time.monotonic() + self.ready_timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                error = (
                    f"{self.name} exited before readiness with {self.process.returncode}\n"
                    f"{self.log_tail()}"
                )
                self._close_log()
                raise AssertionError(error)
            try:
                if self.ready():
                    return
            except Exception:
                pass
            time.sleep(0.2)
        error = f"{self.name} did not become ready\n{self.log_tail()}"
        self.stop()
        raise AssertionError(error)

    def stop(self) -> None:
        process = self.process
        if process is not None and process.poll() is None:
            process.send_signal(self.stop_signal)
            try:
                process.wait(timeout=self.stop_timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        self.process = None
        self._close_log()

    def require_running(self) -> None:
        require(
            self.process is not None and self.process.poll() is None,
            f"{self.name} is not running\n{self.log_tail()}",
        )

    def fingerprint(self) -> str:
        self.require_running()
        require(self.process is not None, f"{self.name} has no process")
        return f"{self.process.pid}|{self.started_at_ns}|{self.start_count - 1}"

    def log_tail(self, lines: int = 80) -> str:
        if self.log_handle is not None:
            self.log_handle.flush()
        if not self.log_path.is_file():
            return f"{self.name}: log unavailable"
        content = self.log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        return f"{self.name} log tail:\n" + "\n".join(content[-lines:])

    def _close_log(self) -> None:
        if self.log_handle is not None:
            self.log_handle.close()
            self.log_handle = None


class LocalProxyServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], upstreams: list[tuple[str, int]]) -> None:
        super().__init__(address, LocalProxyHandler)
        self.upstreams = upstreams
        self.upstream_index = 0
        self.upstream_lock = threading.Lock()

    def candidates(self) -> list[tuple[str, int]]:
        with self.upstream_lock:
            first = self.upstream_index % len(self.upstreams)
            self.upstream_index += 1
        return [self.upstreams[first], self.upstreams[(first + 1) % len(self.upstreams)]]


class LocalProxyHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server: LocalProxyServer

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._proxy()

    def do_HEAD(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._proxy()

    def do_OPTIONS(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._proxy()

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._proxy()

    def do_PUT(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._proxy()

    def do_PATCH(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._proxy()

    def do_DELETE(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        self._proxy()

    def log_message(self, format_string: str, *arguments: Any) -> None:
        del format_string, arguments

    def _proxy(self) -> None:
        content_length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(content_length) if content_length else b""
        candidates = self.server.candidates()
        if self.command not in IDEMPOTENT_METHODS:
            candidates = candidates[:1]

        last_error: OSError | http.client.HTTPException | None = None
        for host, port in candidates:
            connection = http.client.HTTPConnection(host, port, timeout=190)
            try:
                headers = {
                    key: value
                    for key, value in self.headers.items()
                    if key.lower() not in HOP_BY_HOP_HEADERS
                    and key.lower() not in {"content-length", "host"}
                }
                forwarded_for = self.headers.get("X-Forwarded-For")
                client_address = self.client_address[0]
                headers["X-Forwarded-For"] = (
                    f"{forwarded_for}, {client_address}" if forwarded_for else client_address
                )
                headers["X-Real-IP"] = client_address
                headers["X-Forwarded-Proto"] = "http"
                headers["Host"] = f"{host}:{port}"
                headers["Content-Length"] = str(len(body))
                connection.request(self.command, self.path, body=body, headers=headers)
                upstream = connection.getresponse()
                response_body = upstream.read()
                self.send_response(upstream.status, upstream.reason)
                for key, value in upstream.getheaders():
                    if key.lower() not in HOP_BY_HOP_HEADERS and key.lower() != "content-length":
                        self.send_header(key, value)
                self.send_header("Content-Length", str(len(response_body)))
                self.end_headers()
                if self.command != "HEAD":
                    self.wfile.write(response_body)
                return
            except (OSError, http.client.HTTPException) as error:
                last_error = error
            finally:
                connection.close()

        message = json.dumps({"code": 50301, "message": "upstream unavailable", "data": {}}).encode()
        self.send_response(502)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(message)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(message)
        if last_error is not None:
            self.close_connection = True


class LocalTopology:
    runner_name = "local-process"

    def __init__(self, root: Path, server_binary: Path, minio_binary: Path) -> None:
        self.root = root
        self.server_binary = server_binary
        self.minio_binary = minio_binary
        (
            self.postgres_port,
            self.redis_port,
            self.minio_port,
            self.minio_console_port,
            self.api_a_port,
            self.api_b_port,
            self.worker_a_port,
            self.worker_b_port,
            self.load_balancer_port,
        ) = allocate_ports(9)
        self.database_name = f"disk_dist_{os.getpid()}_{int(time.time())}"
        self.env_file = root / "distributed.env"
        self.services: dict[str, ManagedProcess] = {}
        self.proxy: LocalProxyServer | None = None
        self.proxy_thread: threading.Thread | None = None

    def start(self) -> None:
        self._prepare_postgres()
        self._prepare_redis()
        self._prepare_minio()
        self.services["postgres"].start()
        self._initialize_database()
        self.services["redis"].start()
        self.services["minio"].start()
        self._create_bucket()
        self._write_environment_file()
        self._prepare_disk_processes()
        for service in ("api-a", "api-b", "worker-a", "worker-b"):
            self.services[service].start()
        self._start_proxy()

    def run(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        require(bool(arguments), "local topology command is empty")
        command = arguments[0]
        if command == "ps":
            if len(arguments) == 3 and arguments[1] == "-q":
                service = self._service(arguments[2])
                service.require_running()
                return subprocess.CompletedProcess(arguments, 0, f"local-{arguments[2]}\n", "")
            require(len(arguments) == 1, f"unsupported local ps arguments: {arguments}")
            for service in self.services.values():
                service.require_running()
            require(self.proxy_thread is not None and self.proxy_thread.is_alive(), "load balancer is not running")
            listing = "\n".join((*self.services.keys(), "load-balancer")) + "\n"
            return subprocess.CompletedProcess(arguments, 0, listing, "")
        require(len(arguments) == 2, f"unsupported local topology command: {arguments}")
        service = self._service(arguments[1])
        if command == "stop":
            service.stop()
        elif command == "start":
            service.start()
        else:
            raise AssertionError(f"unsupported local topology command: {arguments}")
        return subprocess.CompletedProcess(arguments, 0, "", "")

    def fingerprint(self, service: str) -> str:
        return self._service(service).fingerprint()

    def close(self, preserve_logs: bool) -> None:
        self._stop_proxy()
        for name in ("api-a", "api-b", "worker-a", "worker-b", "minio", "redis", "postgres"):
            service = self.services.get(name)
            if service is not None:
                service.stop()
        if preserve_logs:
            destination = EVIDENCE_ROOT / "distributed-local-logs"
            destination.mkdir(parents=True, exist_ok=True)
            for service in self.services.values():
                if service.log_path.is_file():
                    shutil.copy2(service.log_path, destination / service.log_path.name)

    def _service(self, name: str) -> ManagedProcess:
        service = self.services.get(name)
        require(service is not None, f"unknown local topology service: {name}")
        return service

    def _prepare_postgres(self) -> None:
        initdb = resolve_executable("initdb")
        postgres = resolve_executable("postgres")
        data = self.root / "postgres" / "data"
        socket_directory = self.root / "postgres" / "socket"
        data.parent.mkdir(parents=True)
        socket_directory.mkdir(parents=True)
        run_checked(
            [
                str(initdb),
                "-D",
                str(data),
                "--username=postgres",
                "--auth-local=trust",
                "--auth-host=scram-sha-256",
                "--no-instructions",
            ],
            cwd=data.parent,
        )

        def ready() -> bool:
            result = subprocess.run(
                [
                    str(resolve_executable("pg_isready")),
                    "-h",
                    "127.0.0.1",
                    "-p",
                    str(self.postgres_port),
                ],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            return result.returncode == 0

        self.services["postgres"] = ManagedProcess(
            "postgres",
            [
                str(postgres),
                "-D",
                str(data),
                "-h",
                "127.0.0.1",
                "-p",
                str(self.postgres_port),
                "-k",
                str(socket_directory),
                "-c",
                "statement_timeout=30s",
                "-c",
                "lock_timeout=5s",
                "-c",
                "idle_in_transaction_session_timeout=30s",
                "-c",
                "max_connections=100",
            ],
            data.parent,
            os.environ.copy(),
            ready,
            stop_signal=signal.SIGINT,
        )

    def _initialize_database(self) -> None:
        psql = resolve_executable("psql")
        createdb = resolve_executable("createdb")
        socket_directory = self.root / "postgres" / "socket"
        run_checked(
            [
                str(psql),
                "-h",
                str(socket_directory),
                "-p",
                str(self.postgres_port),
                "-U",
                "postgres",
                "-d",
                "postgres",
                "-v",
                "ON_ERROR_STOP=1",
            ],
            input_text=f"CREATE ROLE {DATABASE_USER} LOGIN PASSWORD '{DATABASE_PASSWORD}';\n",
        )
        run_checked(
            [
                str(createdb),
                "-h",
                str(socket_directory),
                "-p",
                str(self.postgres_port),
                "-U",
                "postgres",
                "-O",
                DATABASE_USER,
                self.database_name,
            ]
        )
        environment = os.environ.copy()
        environment["PGPASSWORD"] = DATABASE_PASSWORD
        run_checked(
            [
                str(psql),
                "-h",
                "127.0.0.1",
                "-p",
                str(self.postgres_port),
                "-U",
                DATABASE_USER,
                "-d",
                self.database_name,
                "-v",
                "ON_ERROR_STOP=1",
                "-f",
                str(REPO_ROOT / "sql" / "init.sql"),
            ],
            environment=environment,
        )

    def _prepare_redis(self) -> None:
        redis_server = resolve_executable("valkey-server", ("redis-server",))
        directory = self.root / "redis"
        directory.mkdir(parents=True)
        config_path = directory / "redis.conf"
        config_path.write_text(
            "\n".join(
                (
                    "bind 127.0.0.1",
                    f"port {self.redis_port}",
                    "protected-mode yes",
                    "daemonize no",
                    f"dir {directory}",
                    "appendonly yes",
                    "appendfsync always",
                    f"requirepass {REDIS_PASSWORD}",
                    'logfile ""',
                    "",
                )
            ),
            encoding="utf-8",
        )
        self.services["redis"] = ManagedProcess(
            "redis",
            [str(redis_server), str(config_path)],
            directory,
            os.environ.copy(),
            lambda: redis_healthy(self.redis_port),
        )

    def _prepare_minio(self) -> None:
        directory = self.root / "minio"
        data = directory / "data"
        data.mkdir(parents=True)
        environment = os.environ.copy()
        environment.update(
            {
                "MINIO_ROOT_USER": MINIO_USER,
                "MINIO_ROOT_PASSWORD": MINIO_PASSWORD,
                "MINIO_BROWSER": "off",
            }
        )
        self.services["minio"] = ManagedProcess(
            "minio",
            [
                str(self.minio_binary),
                "server",
                str(data),
                "--address",
                f"127.0.0.1:{self.minio_port}",
                "--console-address",
                f"127.0.0.1:{self.minio_console_port}",
            ],
            directory,
            environment,
            lambda: http_healthy(
                f"http://127.0.0.1:{self.minio_port}/minio/health/live"
            ),
        )

    def _create_bucket(self) -> None:
        client = boto3.client(
            "s3",
            endpoint_url=f"http://127.0.0.1:{self.minio_port}",
            aws_access_key_id=MINIO_USER,
            aws_secret_access_key=MINIO_PASSWORD,
            region_name="us-east-1",
            config=Config(s3={"addressing_style": "path"}),
        )
        client.create_bucket(Bucket=BUCKET)

    def _write_environment_file(self) -> None:
        values = {
            "DISK_JWT_SECRET": JWT_SECRET,
            "DISK_DATABASE_PASSWORD": DATABASE_PASSWORD,
            "DISK_DATABASE_USER": DATABASE_USER,
            "DISK_DATABASE_NAME": self.database_name,
            "DISK_REDIS_PASSWORD": REDIS_PASSWORD,
            "DISK_MINIO_ROOT_USER": MINIO_USER,
            "DISK_MINIO_ROOT_PASSWORD": MINIO_PASSWORD,
            "DISK_POSTGRES_PORT": str(self.postgres_port),
            "DISK_REDIS_PORT": str(self.redis_port),
            "DISK_MINIO_PORT": str(self.minio_port),
            "DISK_MINIO_CONSOLE_PORT": str(self.minio_console_port),
            "DISK_API_A_PORT": str(self.api_a_port),
            "DISK_API_B_PORT": str(self.api_b_port),
            "DISK_LB_PORT": str(self.load_balancer_port),
        }
        self.env_file.write_text(
            "".join(f"{key}={value}\n" for key, value in values.items()),
            encoding="utf-8",
        )

    def _prepare_disk_processes(self) -> None:
        ports = {
            "api-a": self.api_a_port,
            "api-b": self.api_b_port,
            "worker-a": self.worker_a_port,
            "worker-b": self.worker_b_port,
        }
        identities = {
            "api-a": ("api", "disk-api-a"),
            "api-b": ("api", "disk-api-b"),
            "worker-a": ("worker", "disk-worker-a"),
            "worker-b": ("worker", "disk-worker-b"),
        }
        for service, port in ports.items():
            role, instance_id = identities[service]
            run_directory = self.root / service
            run_directory.mkdir(parents=True)
            config_path = run_directory / "config.json"
            config_path.write_text(
                json.dumps(
                    self._disk_config(run_directory, port, role, instance_id),
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            environment = self._disk_environment(config_path, port, role, instance_id)
            self.services[service] = ManagedProcess(
                service,
                [str(self.server_binary)],
                run_directory,
                environment,
                lambda port=port: http_healthy(
                    f"http://127.0.0.1:{port}/api/health/ready"
                ),
                ready_timeout=90,
            )

    def _disk_config(
        self,
        run_directory: Path,
        port: int,
        role: str,
        instance_id: str,
    ) -> dict[str, Any]:
        config = json.loads(
            (REPO_ROOT / "deploy" / "config.distributed.json").read_text(encoding="utf-8")
        )
        config["listeners"][0].update({"address": "127.0.0.1", "port": port})
        config["app"]["upload_path"] = str(run_directory / "uploads")
        disk = config["custom_config"]["disk"]
        disk.update(
            {
                "process_role": role,
                "instance_id": instance_id,
                "storage_base_path": str(run_directory / "blobs"),
                "temp_upload_path": str(run_directory / "temp"),
            }
        )
        disk["s3"].update(
            {
                "endpoint": f"http://127.0.0.1:{self.minio_port}",
                "use_ssl": False,
                "force_path_style": True,
                "verify_ssl": False,
            }
        )
        config["db_clients"][0].update(
            {
                "host": "127.0.0.1",
                "port": self.postgres_port,
                "dbname": self.database_name,
                "user": DATABASE_USER,
                "passwd": DATABASE_PASSWORD,
            }
        )
        config["redis_clients"][0].update(
            {
                "host": "127.0.0.1",
                "port": self.redis_port,
                "passwd": REDIS_PASSWORD,
            }
        )
        return config

    def _disk_environment(
        self,
        config_path: Path,
        port: int,
        role: str,
        instance_id: str,
    ) -> dict[str, str]:
        environment = os.environ.copy()
        environment.update(
            {
                "DISK_CONFIG_FILE": str(config_path),
                "DISK_LISTEN_ADDRESS": "127.0.0.1",
                "DISK_LISTEN_PORT": str(port),
                "DISK_PROCESS_ROLE": role,
                "DISK_INSTANCE_ID": instance_id,
                "DISK_STORAGE_BACKEND": "s3",
                "DISK_UPLOAD_STAGING_BACKEND": "s3",
                "DATABASE_HOST": "127.0.0.1",
                "DATABASE_PORT": str(self.postgres_port),
                "DATABASE_NAME": self.database_name,
                "DATABASE_USER": DATABASE_USER,
                "DATABASE_PASSWORD": DATABASE_PASSWORD,
                "DATABASE_POOL_SIZE": "8",
                "REDIS_HOST": "127.0.0.1",
                "REDIS_PORT": str(self.redis_port),
                "REDIS_DB": "0",
                "REDIS_PASSWORD": REDIS_PASSWORD,
                "REDIS_POOL_SIZE": "4",
                "JWT_SECRET": JWT_SECRET,
                "DISK_SECURE_MODE": "false",
                "DISK_S3_BUCKET": BUCKET,
                "DISK_S3_REGION": "us-east-1",
                "DISK_S3_ENDPOINT": f"http://127.0.0.1:{self.minio_port}",
                "DISK_S3_USE_SSL": "false",
                "DISK_S3_FORCE_PATH_STYLE": "true",
                "DISK_S3_VERIFY_SSL": "false",
                "DISK_S3_OBJECT_PREFIX": "objects",
                "DISK_S3_STAGING_PREFIX": "staging",
                "DISK_S3_MAX_CONNECTIONS": "16",
                "DISK_S3_IO_THREADS": "4",
                "DISK_S3_CONNECT_TIMEOUT_MS": "3000",
                "DISK_S3_REQUEST_TIMEOUT_MS": "300000",
                "DISK_S3_MAX_RETRIES": "3",
                "DISK_S3_RETRY_BASE_DELAY_MS": "100",
                "DISK_S3_ACCESS_KEY": MINIO_USER,
                "DISK_S3_SECRET_KEY": MINIO_PASSWORD,
                "TZ": "UTC",
            }
        )
        environment.pop("DISK_S3_SESSION_TOKEN", None)
        no_proxy = environment.get("NO_PROXY", environment.get("no_proxy", ""))
        environment["NO_PROXY"] = ",".join(filter(None, (no_proxy, "127.0.0.1", "localhost")))
        return environment

    def _start_proxy(self) -> None:
        self.proxy = LocalProxyServer(
            ("127.0.0.1", self.load_balancer_port),
            [("127.0.0.1", self.api_a_port), ("127.0.0.1", self.api_b_port)],
        )
        self.proxy_thread = threading.Thread(
            target=self.proxy.serve_forever,
            name="distributed-local-proxy",
            daemon=True,
        )
        self.proxy_thread.start()

    def _stop_proxy(self) -> None:
        if self.proxy is not None:
            self.proxy.shutdown()
            self.proxy.server_close()
            self.proxy = None
        if self.proxy_thread is not None:
            self.proxy_thread.join(timeout=5)
            self.proxy_thread = None


def write_bootstrap_failure(error: Exception) -> None:
    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    summary = {
        "runner": "local-process",
        "status": "failed",
        "phase": "topology-bootstrap",
        "error": str(error),
    }
    (EVIDENCE_ROOT / "distributed-flow-summary.json").write_text(
        json.dumps(summary, indent=2) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    gate_name = "DISK_DISTRIBUTED_LOCAL_INTEGRATION"
    if os.environ.get(gate_name) != "1":
        print(f"SKIP: {gate_name} is not 1; skipping distributed flow")
        return 0

    topology: LocalTopology | None = None
    result = 1
    try:
        server_binary = resolve_server_binary()
        minio_binary = resolve_minio_binary()
        with tempfile.TemporaryDirectory(prefix="disk-distributed-local-") as temporary:
            topology = LocalTopology(Path(temporary), server_binary, minio_binary)
            topology.start()
            previous_env_file = os.environ.get("DISK_DISTRIBUTED_ENV_FILE")
            os.environ["DISK_DISTRIBUTED_ENV_FILE"] = str(topology.env_file)
            try:
                result = distributed_flow.main(topology, gate_name)
            finally:
                if previous_env_file is None:
                    os.environ.pop("DISK_DISTRIBUTED_ENV_FILE", None)
                else:
                    os.environ["DISK_DISTRIBUTED_ENV_FILE"] = previous_env_file
                topology.close(preserve_logs=result != 0)
        return result
    except Exception as error:  # noqa: BLE001 - emit a credential-free bootstrap summary
        if topology is not None:
            topology.close(preserve_logs=True)
        write_bootstrap_failure(error)
        print(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

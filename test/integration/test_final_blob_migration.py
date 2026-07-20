#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["psycopg[binary]"]
# ///

"""Verify resumable, hash-checked local final Blob migration and atomic cutover."""

from __future__ import annotations

import hashlib
import json
import os
import signal
import sqlite3
import stat
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlsplit

import psycopg
from psycopg import sql
from psycopg.rows import dict_row

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py.db import database_config


REPO_ROOT = Path(__file__).resolve().parents[2]
MIGRATOR = REPO_ROOT / "scripts" / "migrate-final-blobs.py"
EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/final-blob-manifest-summary.json"
COPY_EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/final-blob-copy-summary.json"
CUTOVER_EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/final-blob-cutover-summary.json"


class TestFailure(RuntimeError):
    """Raised when a migration integration invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TestFailure(message)
    print(f"PASS: {message}")


class ObjectState:
    def __init__(self) -> None:
        self.objects: dict[str, bytes] = {}
        self.puts: Counter[str] = Counter()
        self.requests: Counter[str] = Counter()
        self.total_puts = 0
        self.block_on_put: int | None = None
        self.blocked = threading.Event()
        self.release = threading.Event()
        self.lock = threading.Lock()

    def record_request(self, method: str) -> None:
        with self.lock:
            self.requests[method] += 1

    def request_count(self) -> int:
        with self.lock:
            return sum(self.requests.values())

    def method_request_count(self, method: str) -> int:
        with self.lock:
            return self.requests[method]

    def reset_observations(self) -> None:
        with self.lock:
            self.requests.clear()
            self.puts.clear()
            self.total_puts = 0

    def get(self, key: str) -> bytes | None:
        with self.lock:
            return self.objects.get(key)

    def set(self, key: str, payload: bytes) -> None:
        with self.lock:
            self.objects[key] = payload

    def remove(self, key: str) -> None:
        with self.lock:
            self.objects.pop(key, None)

    def receive_put(self, key: str, payload: bytes) -> bool:
        with self.lock:
            self.total_puts += 1
            sequence = self.total_puts
            self.puts[key] += 1
            should_block = self.block_on_put == sequence
        if should_block:
            self.blocked.set()
            self.release.wait(timeout=30)
            return False
        self.set(key, payload)
        return True


class ObjectServer(ThreadingHTTPServer):
    def __init__(self, state: ObjectState) -> None:
        super().__init__(("127.0.0.1", 0), ObjectHandler)
        self.state = state


class ObjectHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server: ObjectServer

    def log_message(self, format_string: str, *args: object) -> None:
        del format_string, args

    def object_key(self) -> str:
        path = unquote(urlsplit(self.path).path).lstrip("/")
        parts = path.split("/", 1)
        if len(parts) != 2 or not parts[0] or not parts[1]:
            raise TestFailure(f"invalid path-style S3 request: {self.path}")
        return parts[1]

    def read_body(self) -> bytes:
        if self.headers.get("Transfer-Encoding", "").lower() != "chunked":
            length = int(self.headers.get("Content-Length", "0"))
            return self.rfile.read(length)

        chunks: list[bytes] = []
        while True:
            size_line = self.rfile.readline().decode("ascii").strip()
            chunk_size = int(size_line.split(";", 1)[0], 16)
            if chunk_size == 0:
                while self.rfile.readline() not in {b"\r\n", b"\n", b""}:
                    pass
                return b"".join(chunks)
            chunks.append(self.rfile.read(chunk_size))
            self.rfile.read(2)

    def send_payload(
        self,
        status_code: int,
        payload: bytes = b"",
        *,
        content_length: int | None = None,
        etag: str | None = None,
    ) -> None:
        self.send_response(status_code)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header(
            "Content-Length",
            str(len(payload) if content_length is None else content_length),
        )
        self.send_header("Connection", "close")
        self.send_header("x-amz-request-id", "disk-final-blob-migration-fixture")
        if etag is not None:
            self.send_header("ETag", f'"{etag}"')
        self.end_headers()
        if self.command != "HEAD" and payload:
            try:
                self.wfile.write(payload)
            except BrokenPipeError:
                pass

    def send_s3_error(self, status_code: int, code: str) -> None:
        payload = (
            '<?xml version="1.0" encoding="UTF-8"?>'
            f"<Error><Code>{code}</Code><Message>fixture error</Message>"
            "<RequestId>disk-final-blob-migration-fixture</RequestId></Error>"
        ).encode("ascii")
        self.send_payload(status_code, payload)

    def do_HEAD(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self.server.state.record_request("HEAD")
        payload = self.server.state.get(self.object_key())
        if payload is None:
            self.send_s3_error(404, "NoSuchKey")
            return
        self.send_payload(
            200,
            content_length=len(payload),
            etag=hashlib.md5(payload, usedforsecurity=False).hexdigest(),
        )

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self.server.state.record_request("GET")
        payload = self.server.state.get(self.object_key())
        if payload is None:
            self.send_s3_error(404, "NoSuchKey")
            return
        self.send_payload(
            200,
            payload,
            etag=hashlib.md5(payload, usedforsecurity=False).hexdigest(),
        )

    def do_PUT(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        self.server.state.record_request("PUT")
        key = self.object_key()
        payload = self.read_body()
        if not self.server.state.receive_put(key, payload):
            self.send_s3_error(503, "SlowDown")
            return
        self.send_payload(
            200,
            etag=hashlib.md5(payload, usedforsecurity=False).hexdigest(),
        )


def admin_config() -> dict[str, Any]:
    config = database_config()
    config["dbname"] = os.environ.get("PGMAINTENANCE_DB", "postgres")
    return config


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


def connect(database_name: str) -> psycopg.Connection[dict[str, Any]]:
    config = database_config()
    config["dbname"] = database_name
    return psycopg.connect(**config, autocommit=True, row_factory=dict_row)


def database_env(database_name: str, endpoint: str) -> dict[str, str]:
    config = database_config()
    env = os.environ.copy()
    env.update(
        {
            "PGHOST": str(config["host"]),
            "PGPORT": str(config["port"]),
            "PGDATABASE": database_name,
            "PGUSER": str(config["user"]),
            "PGPASSWORD": str(config["password"]),
            "DISK_S3_ENDPOINT": endpoint,
            "DISK_S3_REGION": "us-east-1",
            "DISK_S3_ACCESS_KEY": "fixture-access-key",
            "DISK_S3_SECRET_KEY": "fixture-secret-key",
            "DISK_S3_FORCE_PATH_STYLE": "true",
            "DISK_S3_VERIFY_SSL": "false",
        }
    )
    env.pop("DISK_DATABASE_URL", None)
    env.pop("AWS_PROFILE", None)
    return env


def read_only_manifest_env(env: dict[str, str]) -> dict[str, str]:
    manifest_env = env.copy()
    manifest_env["PGOPTIONS"] = "-c default_transaction_read_only=on"
    manifest_env["AWS_EC2_METADATA_DISABLED"] = "true"
    for name in (
        "DISK_S3_ACCESS_KEY",
        "DISK_S3_SECRET_KEY",
        "AWS_ACCESS_KEY_ID",
        "AWS_SECRET_ACCESS_KEY",
        "AWS_SESSION_TOKEN",
        "AWS_SECURITY_TOKEN",
        "AWS_PROFILE",
        "AWS_DEFAULT_PROFILE",
        "AWS_SHARED_CREDENTIALS_FILE",
        "AWS_CONFIG_FILE",
    ):
        manifest_env.pop(name, None)
    return manifest_env


def run_command(
    arguments: list[str],
    env: dict[str, str],
    *,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(MIGRATOR), *arguments],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if check and result.returncode != 0:
        raise TestFailure(
            f"migration command failed ({result.returncode}): {' '.join(arguments)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def parse_summary(result: subprocess.CompletedProcess[str]) -> dict[str, Any]:
    lines = [line for line in result.stdout.splitlines() if line.startswith("{")]
    if not lines:
        raise TestFailure(
            f"migration command emitted no JSON summary:\n{result.stdout}"
        )
    return json.loads(lines[-1])


def current_locators(database_name: str) -> dict[int, str]:
    with connect(database_name) as connection:
        rows = connection.execute(
            "SELECT id, storage_path FROM file_contents ORDER BY id"
        ).fetchall()
    return {int(row["id"]): str(row["storage_path"]) for row in rows}


def content_snapshot(database_name: str) -> tuple[tuple[Any, ...], ...]:
    with connect(database_name) as connection:
        rows = connection.execute(
            "SELECT id, hash_md5, hash_sha256, size, storage_path, mime_type, "
            "ref_count, created_at::text AS created_at FROM file_contents ORDER BY id"
        ).fetchall()
    return tuple(
        (
            int(row["id"]),
            str(row["hash_md5"]).strip(),
            str(row["hash_sha256"]).strip(),
            int(row["size"]),
            str(row["storage_path"]),
            row["mime_type"],
            int(row["ref_count"]),
            str(row["created_at"]),
        )
        for row in rows
    )


def source_snapshot(
    records: list[dict[str, Any]],
) -> dict[int, tuple[bytes, int, int, int]]:
    snapshot: dict[int, tuple[bytes, int, int, int]] = {}
    for record in records:
        source_path = record["source_path"]
        payload = source_path.read_bytes()
        status = source_path.stat()
        snapshot[int(record["content_id"])] = (
            payload,
            status.st_size,
            stat.S_IMODE(status.st_mode),
            status.st_mtime_ns,
        )
    return snapshot


def manifest_arguments(
    manifest_path: Path,
    local_root: Path,
    path_base: Path,
    object_prefix: str,
) -> list[str]:
    return [
        "manifest",
        "--manifest",
        str(manifest_path),
        "--local-root",
        str(local_root),
        "--path-base",
        str(path_base),
        "--bucket",
        "disk-migration-fixture",
        "--object-prefix",
        object_prefix,
    ]


def wait_for_manifest_count_lock(database_name: str, timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with connect(database_name) as connection:
            blocked = connection.execute(
                "SELECT EXISTS ("
                "SELECT 1 FROM pg_stat_activity "
                "WHERE datname = %s AND pid <> pg_backend_pid() "
                "AND query LIKE '%%SELECT COUNT(*) AS count FROM file_contents%%' "
                "AND wait_event_type = 'Lock') AS blocked",
                (database_name,),
            ).fetchone()
        if blocked is not None and bool(blocked["blocked"]):
            return
        time.sleep(0.02)
    raise TestFailure("manifest process did not block on the fixture table lock")


def run_manifest_publish_race(
    arguments: list[str],
    env: dict[str, str],
    database_name: str,
    target_path: Path,
    sentinel: bytes,
) -> subprocess.CompletedProcess[str]:
    config = database_config()
    config["dbname"] = database_name
    blocker = psycopg.connect(**config)
    process: subprocess.Popen[str] | None = None
    try:
        blocker.execute("LOCK TABLE file_contents IN ACCESS EXCLUSIVE MODE")
        process = subprocess.Popen(
            [str(MIGRATOR), *arguments],
            cwd=REPO_ROOT,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        wait_for_manifest_count_lock(database_name)
        target_path.write_bytes(sentinel)
        blocker.rollback()
        stdout, stderr = process.communicate(timeout=30)
        return subprocess.CompletedProcess(
            process.args,
            process.returncode,
            stdout,
            stderr,
        )
    finally:
        try:
            blocker.rollback()
        finally:
            blocker.close()
        if process is not None and process.poll() is None:
            process.kill()
            process.communicate(timeout=10)


def write_source(path: Path, payload: bytes) -> tuple[str, str]:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return (
        hashlib.md5(payload, usedforsecurity=False).hexdigest(),
        hashlib.sha256(payload).hexdigest(),
    )


def setup_database(
    database_name: str,
    path_base: Path,
    local_root: Path,
) -> tuple[list[dict[str, Any]], dict[int, str]]:
    payloads = [
        (b"legacy-md5-first-" + bytes(range(64))) * 47,
        (b"sha256-layout-second-" + bytes(reversed(range(128)))) * 37,
        (b"legacy-md5-third-" + bytes(range(255))) * 29,
    ]
    records: list[dict[str, Any]] = []
    source_locators: dict[int, str] = {}
    for offset, payload in enumerate(payloads, start=1):
        content_id = 100 + offset
        md5_hash = hashlib.md5(payload, usedforsecurity=False).hexdigest()
        sha256_hash = hashlib.sha256(payload).hexdigest()
        if offset == 2:
            relative = Path("blobs") / "sha256" / sha256_hash[:2] / f"{sha256_hash}.bin"
        else:
            relative = Path("blobs") / "md5" / md5_hash[:2] / f"{md5_hash}.bin"
        source_path = path_base / relative
        written_md5, written_sha256 = write_source(source_path, payload)
        require(
            (written_md5, written_sha256) == (md5_hash, sha256_hash),
            f"fixture source {content_id} hashes are deterministic",
        )
        locator = relative.as_posix()
        source_locators[content_id] = locator
        records.append(
            {
                "content_id": content_id,
                "payload": payload,
                "source_path": source_path,
                "source_locator": locator,
                "size": len(payload),
                "md5": md5_hash,
                "sha256": sha256_hash,
            }
        )

    with connect(database_name) as connection:
        connection.execute(
            "CREATE TABLE file_contents ("
            "id BIGINT PRIMARY KEY, hash_md5 CHAR(32) NOT NULL, "
            "hash_sha256 CHAR(64) NOT NULL, size BIGINT NOT NULL, "
            "storage_path VARCHAR(512) NOT NULL, mime_type VARCHAR(128), "
            "ref_count INTEGER NOT NULL DEFAULT 1, "
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
            "UNIQUE (hash_md5, hash_sha256))"
        )
        for record in records:
            connection.execute(
                "INSERT INTO file_contents "
                "(id, hash_md5, hash_sha256, size, storage_path, ref_count) "
                "VALUES (%s, %s, %s, %s, %s, %s)",
                (
                    record["content_id"],
                    record["md5"],
                    record["sha256"],
                    record["size"],
                    record["source_locator"],
                    0 if record["content_id"] == 103 else record["content_id"] - 99,
                ),
            )
    require(
        local_root == path_base / "blobs", "fixture local root matches stored locators"
    )
    return records, source_locators


def verify_manifest(
    manifest_path: Path,
    records: list[dict[str, Any]],
    local_root: Path,
    object_prefix: str,
) -> dict[int, str]:
    lines = manifest_path.read_text(encoding="utf-8").splitlines()
    require(
        len(lines) == len(records) + 1,
        "manifest contains one header and every content row",
    )
    header = json.loads(lines[0])
    require(
        set(header)
        == {
            "kind",
            "version",
            "generated_at",
            "local_root",
            "path_base",
            "bucket",
            "object_prefix",
            "object_count",
        },
        "manifest header has the exact versioned fields",
    )
    require(header["kind"] == "disk-final-blob-manifest", "manifest kind is stable")
    require(header["version"] == 1, "manifest version is stable")
    require(
        header["local_root"] == str(local_root.resolve()),
        "manifest binds the local root",
    )
    require(header["object_count"] == len(records), "manifest count matches PostgreSQL")

    target_keys: dict[int, str] = {}
    for expected, line in zip(records, lines[1:], strict=True):
        item = json.loads(line)
        require(
            set(item)
            == {
                "content_id",
                "source_locator",
                "source_relative_path",
                "size",
                "md5",
                "sha256",
                "target_key",
            },
            "manifest content row has the exact versioned fields",
        )
        target_key = (
            f"{object_prefix}/sha256/{expected['sha256'][:2]}/{expected['sha256']}.bin"
        )
        require(
            item["content_id"] == expected["content_id"],
            "manifest is ordered by content ID",
        )
        require(
            item["source_locator"] == expected["source_locator"],
            "manifest keeps DB locator",
        )
        require(
            item["source_relative_path"]
            == expected["source_path"].relative_to(local_root).as_posix(),
            "manifest keeps the normalized local-root-relative source path",
        )
        require(item["size"] == expected["size"], "manifest keeps content size")
        require(item["md5"] == expected["md5"], "manifest keeps content MD5")
        require(item["sha256"] == expected["sha256"], "manifest keeps content SHA-256")
        require(
            item["target_key"] == target_key, "manifest derives the canonical S3 key"
        )
        target_keys[int(expected["content_id"])] = target_key
    return target_keys


def checkpoint_ids(checkpoint_path: Path) -> list[int]:
    with sqlite3.connect(checkpoint_path) as connection:
        return [
            int(row[0])
            for row in connection.execute(
                "SELECT content_id FROM verified_objects ORDER BY content_id"
            )
        ]


def checkpoint_metadata(checkpoint_path: Path) -> dict[str, str]:
    with sqlite3.connect(checkpoint_path) as connection:
        return dict(connection.execute("SELECT key, value FROM checkpoint_meta"))


def main() -> int:
    EVIDENCE_PATH.unlink(missing_ok=True)
    COPY_EVIDENCE_PATH.unlink(missing_ok=True)
    suffix = f"{os.getpid()}_{uuid.uuid4().hex[:8]}"
    database_name = f"disk_final_blob_migration_{suffix}"
    server_state = ObjectState()
    server = ObjectServer(server_state)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    endpoint = f"http://127.0.0.1:{server.server_address[1]}"
    create_database(database_name)

    try:
        with tempfile.TemporaryDirectory(
            prefix="disk-final-blob-migration-"
        ) as temp_raw:
            temp_root = Path(temp_raw)
            path_base = temp_root / "runtime"
            local_root = path_base / "blobs"
            local_root.mkdir(parents=True)
            manifest_path = temp_root / "final-blobs.jsonl"
            checkpoint_path = temp_root / "final-blobs.sqlite3"
            object_prefix = f"objects/migration-{suffix.replace('_', '-')}"
            records, source_locators = setup_database(
                database_name, path_base, local_root
            )
            env = database_env(database_name, endpoint)
            manifest_env = read_only_manifest_env(env)
            manifest_args = manifest_arguments(
                manifest_path, local_root, path_base, object_prefix
            )
            database_before_manifest = content_snapshot(database_name)
            sources_before_manifest = source_snapshot(records)
            require(
                database_before_manifest[-1][6] == 0,
                "manifest fixture includes a zero-reference content row",
            )
            require(
                server_state.request_count() == 0,
                "manifest fixture starts with zero S3 requests",
            )

            manifest_result = run_command(manifest_args, manifest_env)
            require(
                parse_summary(manifest_result)["objects"] == 3,
                "manifest command reports all objects",
            )
            target_keys = verify_manifest(
                manifest_path, records, local_root, object_prefix
            )
            require(
                stat.S_IMODE(manifest_path.stat().st_mode) == 0o600,
                "manifest is published with mode 0600",
            )
            require(
                content_snapshot(database_name) == database_before_manifest,
                "manifest leaves the complete content snapshot unchanged",
            )
            require(
                source_snapshot(records) == sources_before_manifest,
                "manifest leaves every source Blob byte and metadata unchanged",
            )
            require(
                server_state.request_count() == 0,
                "manifest performs no S3 request without S3 credentials",
            )
            require(
                not checkpoint_path.exists(),
                "manifest does not create a migration checkpoint",
            )

            published_manifest = (
                manifest_path.read_bytes(),
                stat.S_IMODE(manifest_path.stat().st_mode),
                manifest_path.stat().st_mtime_ns,
            )
            existing_output = run_command(manifest_args, manifest_env, check=False)
            require(
                existing_output.returncode != 0,
                "manifest refuses an existing output path",
            )
            require(
                (
                    manifest_path.read_bytes(),
                    stat.S_IMODE(manifest_path.stat().st_mode),
                    manifest_path.stat().st_mtime_ns,
                )
                == published_manifest,
                "existing manifest remains byte-for-byte and metadata unchanged",
            )

            race_manifest_path = temp_root / "concurrent-final-blobs.jsonl"
            race_sentinel = b"operator-owned-manifest-evidence\n"
            race_result = run_manifest_publish_race(
                manifest_arguments(
                    race_manifest_path, local_root, path_base, object_prefix
                ),
                manifest_env,
                database_name,
                race_manifest_path,
                race_sentinel,
            )
            require(
                race_result.returncode != 0,
                "manifest refuses a target created after its preflight check",
            )
            require(
                race_manifest_path.read_bytes() == race_sentinel,
                "concurrently published operator evidence is never overwritten",
            )
            require(
                not list(temp_root.glob(f".{race_manifest_path.name}.*")),
                "publication conflict removes its temporary manifest",
            )

            failed_manifest_path = temp_root / "failed-final-blobs.jsonl"
            with connect(database_name) as connection:
                connection.execute(
                    "UPDATE file_contents SET size = size + 1 WHERE id = 103"
                )
            invalid_database_snapshot = content_snapshot(database_name)
            failed_manifest = run_command(
                manifest_arguments(
                    failed_manifest_path, local_root, path_base, object_prefix
                ),
                manifest_env,
                check=False,
            )
            require(
                failed_manifest.returncode != 0,
                "manifest rejects a later source-size mismatch",
            )
            require(
                not failed_manifest_path.exists(),
                "failed manifest generation publishes no final partial file",
            )
            require(
                not list(temp_root.glob(f".{failed_manifest_path.name}.*")),
                "failed manifest generation removes its temporary file",
            )
            require(
                content_snapshot(database_name) == invalid_database_snapshot,
                "failed manifest leaves its database snapshot unchanged",
            )
            require(
                source_snapshot(records) == sources_before_manifest,
                "failed manifest leaves every source Blob unchanged",
            )
            require(
                server_state.request_count() == 0 and not checkpoint_path.exists(),
                "all manifest failure paths avoid S3 and checkpoint side effects",
            )
            with connect(database_name) as connection:
                connection.execute(
                    "UPDATE file_contents SET size = size - 1 WHERE id = 103"
                )
            require(
                content_snapshot(database_name) == database_before_manifest,
                "fixture restores the original database snapshot",
            )
            require(
                manifest_path.read_bytes() == published_manifest[0],
                "failed alternate output leaves the accepted manifest unchanged",
            )
            manifest_s3_requests = server_state.request_count()

            copy_args = [
                "copy",
                "--manifest",
                str(manifest_path),
                "--checkpoint",
                str(checkpoint_path),
            ]
            cutover_args = [
                "cutover",
                "--manifest",
                str(manifest_path),
                "--checkpoint",
                str(checkpoint_path),
            ]

            second = records[1]
            original_second = second["source_path"].read_bytes()
            second["source_path"].write_bytes(
                bytes([original_second[0] ^ 0xFF]) + original_second[1:]
            )
            corrupt_source = run_command(copy_args, env, check=False)
            require(
                corrupt_source.returncode != 0,
                "copy rejects same-size source hash corruption",
            )
            require(
                server_state.total_puts == 0,
                "source corruption is rejected before S3 mutation",
            )
            second["source_path"].write_bytes(original_second)

            first_key = target_keys[101]
            server_state.set(first_key, b"corrupt-existing-target")
            corrupt_target = run_command(copy_args, env, check=False)
            require(
                corrupt_target.returncode != 0,
                "copy rejects a conflicting existing target",
            )
            require(
                server_state.get(first_key) == b"corrupt-existing-target",
                "copy never overwrites a conflict",
            )
            require(server_state.total_puts == 0, "target conflict causes no PUT")
            server_state.remove(first_key)

            dry_copy = run_command(copy_args, env)
            dry_summary = parse_summary(dry_copy)
            require(
                dry_summary["would_upload"] == 3,
                "copy dry-run reports every absent target",
            )
            require(server_state.total_puts == 0, "copy dry-run performs no PUT")
            require(
                not checkpoint_path.exists(),
                "copy dry-run does not create a checkpoint",
            )
            require(
                current_locators(database_name) == source_locators,
                "copy dry-run leaves DB paths unchanged",
            )

            requests_before_missing_checkpoint = server_state.request_count()
            missing_checkpoint_cutover = run_command(
                [*cutover_args, "--execute"], env, check=False
            )
            require(
                missing_checkpoint_cutover.returncode != 0,
                "cutover rejects a missing checkpoint",
            )
            missing_checkpoint_s3_requests = (
                server_state.request_count() - requests_before_missing_checkpoint
            )
            require(
                missing_checkpoint_s3_requests == 0,
                "missing checkpoint is rejected before S3 verification",
            )
            require(
                current_locators(database_name) == source_locators,
                "missing checkpoint cutover leaves every DB path unchanged",
            )

            first = records[0]
            throttle_checkpoint_path = temp_root / "throttled-copy.sqlite3"
            throttle_mib_per_second = 0.01
            charged_transfer_bytes = int(first["size"]) * 2
            minimum_throttle_seconds = charged_transfer_bytes / (
                throttle_mib_per_second * 1024 * 1024
            )
            throttle_started = time.monotonic()
            throttled_copy = run_command(
                [
                    "copy",
                    "--manifest",
                    str(manifest_path),
                    "--checkpoint",
                    str(throttle_checkpoint_path),
                    "--max-objects",
                    "1",
                    "--rate-limit-mib-per-second",
                    str(throttle_mib_per_second),
                    "--execute",
                ],
                env,
            )
            throttle_elapsed = time.monotonic() - throttle_started
            throttle_summary = parse_summary(throttled_copy)
            require(
                throttle_summary["processed"] == 1
                and throttle_summary["uploaded"] == 1
                and throttle_summary["remaining"] == 2,
                "bounded copy processes exactly one absent target",
            )
            require(
                throttle_elapsed >= minimum_throttle_seconds * 0.9,
                "copy rate limit charges both upload and complete target GET bytes",
            )
            require(
                checkpoint_ids(throttle_checkpoint_path) == [101],
                "bounded copy checkpoints its one completely verified target",
            )
            require(
                stat.S_IMODE(throttle_checkpoint_path.stat().st_mode) == 0o600,
                "copy checkpoint is created with mode 0600",
            )
            require(
                server_state.get(first_key) == first["payload"],
                "bounded copy preserves the target bytes",
            )
            server_state.remove(first_key)
            throttle_checkpoint_path.unlink()
            server_state.reset_observations()
            require(
                server_state.get(first_key) is None
                and not throttle_checkpoint_path.exists(),
                "rate-limit probe is removed before interruption recovery",
            )

            server_state.block_on_put = 2
            interrupted = subprocess.Popen(
                [str(MIGRATOR), *copy_args, "--execute"],
                cwd=REPO_ROOT,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                start_new_session=True,
            )
            require(
                server_state.blocked.wait(timeout=60),
                "fixture observes the second object PUT",
            )
            os.killpg(interrupted.pid, signal.SIGKILL)
            interrupted_stdout, interrupted_stderr = interrupted.communicate(timeout=30)
            del interrupted_stdout, interrupted_stderr
            server_state.release.set()
            require(
                interrupted.returncode == -signal.SIGKILL,
                "migration process is killed mid-object",
            )
            partial_checkpoint_records = len(checkpoint_ids(checkpoint_path))
            require(
                partial_checkpoint_records == 1
                and checkpoint_ids(checkpoint_path) == [101],
                "checkpoint commits only the first verified object",
            )
            require(
                current_locators(database_name) == source_locators,
                "interrupted copy never changes DB paths",
            )

            partial_cutover_gets_before = server_state.method_request_count("GET")
            partial_checkpoint_cutover = run_command(
                [*cutover_args, "--execute"], env, check=False
            )
            partial_cutover_target_gets = (
                server_state.method_request_count("GET") - partial_cutover_gets_before
            )
            require(
                partial_checkpoint_cutover.returncode != 0,
                "cutover rejects a partial checkpoint",
            )
            require(
                partial_cutover_target_gets == 1,
                "cutover revalidates the recorded target then stops at the first missing record",
            )
            require(
                current_locators(database_name) == source_locators,
                "partial checkpoint cutover leaves every DB path unchanged",
            )

            server_state.block_on_put = None
            resumed = run_command([*copy_args, "--execute"], env)
            resumed_summary = parse_summary(resumed)
            require(
                resumed_summary["uploaded"] == 2,
                "resume uploads only unfinished objects",
            )
            require(
                resumed_summary["reused"] == 1,
                "resume revalidates and reuses completed target",
            )
            require(
                checkpoint_ids(checkpoint_path) == [101, 102, 103],
                "resume completes checkpoint",
            )
            expected_checkpoint_metadata = {
                "checkpoint_version": "1",
                "manifest_sha256": hashlib.sha256(
                    manifest_path.read_bytes()
                ).hexdigest(),
                "bucket": "disk-migration-fixture",
                "object_prefix": object_prefix,
            }
            require(
                checkpoint_metadata(checkpoint_path) == expected_checkpoint_metadata,
                "checkpoint binds the exact manifest digest and S3 destination",
            )
            for record in records:
                key = target_keys[int(record["content_id"])]
                require(
                    server_state.get(key) == record["payload"],
                    f"target {record['content_id']} preserves bytes",
                )
            require(
                server_state.puts[target_keys[101]] == 1,
                "first target is uploaded once",
            )
            require(
                server_state.puts[target_keys[102]] == 2,
                "interrupted target is attempted twice",
            )
            require(
                server_state.puts[target_keys[103]] == 1, "last target is uploaded once"
            )

            puts_after_resume = server_state.total_puts
            with sqlite3.connect(checkpoint_path) as connection:
                connection.execute(
                    "UPDATE checkpoint_meta SET value = 'wrong-bucket' "
                    "WHERE key = 'bucket'"
                )
            mismatched_checkpoint = run_command(
                [*copy_args, "--execute"], env, check=False
            )
            require(
                mismatched_checkpoint.returncode != 0,
                "copy rejects a checkpoint bound to another destination",
            )
            require(
                server_state.total_puts == puts_after_resume,
                "checkpoint binding mismatch is rejected before any PUT",
            )
            with sqlite3.connect(checkpoint_path) as connection:
                connection.execute(
                    "UPDATE checkpoint_meta SET value = ? WHERE key = 'bucket'",
                    (expected_checkpoint_metadata["bucket"],),
                )
            require(
                checkpoint_metadata(checkpoint_path) == expected_checkpoint_metadata,
                "fixture restores the valid checkpoint binding",
            )
            replay = run_command([*copy_args, "--execute"], env)
            replay_summary = parse_summary(replay)
            require(
                replay_summary["uploaded"] == 0,
                "copy replay uploads no verified target",
            )
            require(
                replay_summary["reused"] == 3,
                "copy replay fully revalidates every target",
            )
            require(
                server_state.total_puts == puts_after_resume,
                "copy replay issues zero PUT requests",
            )

            puts_before_cutover = server_state.total_puts
            second_key = target_keys[102]
            server_state.set(second_key, b"X" * int(second["size"]))
            corrupt_cutover = run_command(cutover_args, env, check=False)
            require(
                corrupt_cutover.returncode != 0,
                "cutover rejects same-size target hash corruption",
            )
            require(
                current_locators(database_name) == source_locators,
                "failed cutover leaves DB unchanged",
            )
            server_state.set(second_key, second["payload"])

            dry_cutover = run_command(cutover_args, env)
            dry_cutover_summary = parse_summary(dry_cutover)
            require(
                dry_cutover_summary["database_state_before"] == "source",
                "cutover dry-run sees source state",
            )
            require(
                current_locators(database_name) == source_locators,
                "cutover dry-run performs no DB update",
            )

            with connect(database_name) as connection:
                connection.execute(
                    "UPDATE file_contents SET storage_path = 'drifted/path' WHERE id = 103"
                )
            drifted = run_command([*cutover_args, "--execute"], env, check=False)
            require(drifted.returncode != 0, "cutover rejects a drifted DB snapshot")
            drifted_locators = current_locators(database_name)
            require(
                drifted_locators[101] == source_locators[101],
                "failed cutover updates no preceding row",
            )
            require(
                drifted_locators[103] == "drifted/path",
                "failed cutover preserves drift for diagnosis",
            )
            with connect(database_name) as connection:
                connection.execute(
                    "UPDATE file_contents SET storage_path = %s WHERE id = 103",
                    (source_locators[103],),
                )

            atomic_cutover_gets_before = server_state.method_request_count("GET")
            applied = run_command([*cutover_args, "--execute"], env)
            applied_summary = parse_summary(applied)
            atomic_cutover_target_gets = (
                server_state.method_request_count("GET") - atomic_cutover_gets_before
            )
            require(
                atomic_cutover_target_gets == len(records),
                "cutover completely revalidates every target before switching paths",
            )
            require(
                applied_summary["rows_changed"] == 3,
                "cutover atomically changes every locator",
            )
            expected_targets = {
                content_id: target_keys[content_id] for content_id in target_keys
            }
            require(
                current_locators(database_name) == expected_targets,
                "DB points only to canonical S3 keys",
            )

            repeated = run_command([*cutover_args, "--execute"], env)
            repeated_summary = parse_summary(repeated)
            require(
                repeated_summary["database_state_before"] == "target",
                "repeated cutover detects target state",
            )
            require(
                repeated_summary["rows_changed"] == 0, "repeated cutover is idempotent"
            )
            require(
                server_state.total_puts == puts_after_resume,
                "cutover and replay never upload objects",
            )

            rollback_args = ["rollback", "--manifest", str(manifest_path)]
            rollback_dry = run_command(rollback_args, env)
            require(
                parse_summary(rollback_dry)["database_state_before"] == "target",
                "rollback dry-run sees target state",
            )
            require(
                current_locators(database_name) == expected_targets,
                "rollback dry-run changes no path",
            )
            rolled_back = run_command([*rollback_args, "--execute"], env)
            rolled_back_summary = parse_summary(rolled_back)
            require(
                rolled_back_summary["rows_changed"] == 3,
                "rollback restores all source locators",
            )
            require(
                current_locators(database_name) == source_locators,
                "rollback uses the verified manifest sources",
            )

            final_cutover = run_command([*cutover_args, "--execute"], env)
            final_cutover_summary = parse_summary(final_cutover)
            require(
                final_cutover_summary["rows_changed"] == 3,
                "cutover remains reusable after rollback",
            )
            require(
                current_locators(database_name) == expected_targets,
                "final DB state points to S3",
            )
            for record in records:
                require(
                    record["source_path"].read_bytes() == record["payload"],
                    "migration retains local rollback source",
                )

            evidence = {
                "schema_version": 1,
                "scenario": "read_only_final_blob_manifest",
                "manifest": {
                    "objects": len(records),
                    "content_fields": [
                        "content_id",
                        "source_locator",
                        "source_relative_path",
                        "size",
                        "md5",
                        "sha256",
                        "target_key",
                    ],
                    "strict_content_id_order": True,
                    "mode": "0600",
                    "atomic_no_replace_publish": True,
                    "existing_output_preserved": True,
                    "concurrent_output_preserved": True,
                    "failure_artifacts": 0,
                },
                "read_only": {
                    "database_session_read_only": True,
                    "database_unchanged": True,
                    "source_objects_unchanged": True,
                    "s3_credentials_required": False,
                    "s3_requests": manifest_s3_requests,
                    "checkpoint_created": False,
                },
                "acceptance": {"passed": True},
            }
            serialized = json.dumps(evidence, indent=2, sort_keys=True) + "\n"
            for sensitive in (
                endpoint,
                database_name,
                object_prefix,
                str(temp_root),
                "fixture-access-key",
                "fixture-secret-key",
            ):
                require(
                    sensitive not in serialized,
                    "manifest evidence contains no locator or credential",
                )
            EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
            EVIDENCE_PATH.write_text(serialized, encoding="utf-8")

            copy_evidence = {
                "schema_version": 1,
                "scenario": "resumable_verified_final_blob_copy",
                "dry_run": {
                    "objects_inspected": int(dry_summary["processed"]),
                    "objects_would_upload": int(dry_summary["would_upload"]),
                    "put_requests": 0,
                    "checkpoint_created": False,
                    "database_unchanged": True,
                },
                "rate_limit": {
                    "configured_mib_per_second": throttle_mib_per_second,
                    "max_objects": 1,
                    "charged_transfer_bytes": charged_transfer_bytes,
                    "minimum_expected_seconds": round(minimum_throttle_seconds, 3),
                    "observed_minimum_satisfied": (
                        throttle_elapsed >= minimum_throttle_seconds * 0.9
                    ),
                    "uploaded": int(throttle_summary["uploaded"]),
                    "verified_checkpoint_records": 1,
                },
                "verification": {
                    "size_md5_sha256": True,
                    "source_corruption_rejected_before_put": True,
                    "conflicting_target_preserved": True,
                    "target_corruption_rejected_before_cutover": True,
                },
                "checkpoint": {
                    "mode": "0600",
                    "bound_to_manifest_and_destination": True,
                    "binding_mismatch_rejected_before_put": True,
                    "verified_after_process_kill": 1,
                    "verified_after_resume": len(checkpoint_ids(checkpoint_path)),
                },
                "resume": {
                    "uploaded": int(resumed_summary["uploaded"]),
                    "reused": int(resumed_summary["reused"]),
                    "all_target_bytes_match": True,
                },
                "replay": {
                    "uploaded": int(replay_summary["uploaded"]),
                    "reused": int(replay_summary["reused"]),
                    "additional_puts": server_state.total_puts - puts_after_resume,
                },
                "acceptance": {"passed": True},
            }
            copy_serialized = json.dumps(copy_evidence, indent=2, sort_keys=True) + "\n"
            for sensitive in (
                endpoint,
                database_name,
                object_prefix,
                str(temp_root),
                "fixture-access-key",
                "fixture-secret-key",
            ):
                require(
                    sensitive not in copy_serialized,
                    "copy evidence contains no locator or credential",
                )
            COPY_EVIDENCE_PATH.write_text(copy_serialized, encoding="utf-8")

            cutover_evidence = {
                "schema_version": 1,
                "scenario": "verified_atomic_final_blob_cutover",
                "preconditions": {
                    "manifest_objects": len(records),
                    "missing_checkpoint_rejected": True,
                    "missing_checkpoint_s3_requests": missing_checkpoint_s3_requests,
                    "partial_checkpoint_records": partial_checkpoint_records,
                    "partial_checkpoint_rejected": True,
                    "partial_cutover_complete_target_gets": partial_cutover_target_gets,
                    "stale_target_corruption_rejected": True,
                    "rejected_cutovers_preserved_all_source_locators": True,
                },
                "verification": {
                    "checkpoint_required_for_every_object": True,
                    "target_size_md5_sha256_revalidated": True,
                    "complete_target_gets_before_atomic_switch": atomic_cutover_target_gets,
                    "cutover_put_requests": server_state.total_puts
                    - puts_before_cutover,
                },
                "database": {
                    "dry_run_state_before": dry_cutover_summary[
                        "database_state_before"
                    ],
                    "dry_run_rows_changed": int(dry_cutover_summary["rows_changed"]),
                    "drift_rejected_without_partial_update": True,
                    "atomic_rows_changed": int(applied_summary["rows_changed"]),
                    "all_locators_are_target_after_commit": True,
                    "replay_rows_changed": int(repeated_summary["rows_changed"]),
                    "rollback_rows_changed": int(rolled_back_summary["rows_changed"]),
                    "final_cutover_rows_changed": int(
                        final_cutover_summary["rows_changed"]
                    ),
                },
                "acceptance": {"passed": True},
            }
            cutover_serialized = (
                json.dumps(cutover_evidence, indent=2, sort_keys=True) + "\n"
            )
            for sensitive in (
                endpoint,
                database_name,
                object_prefix,
                str(temp_root),
                "fixture-access-key",
                "fixture-secret-key",
            ):
                require(
                    sensitive not in cutover_serialized,
                    "cutover evidence contains no locator or credential",
                )
            CUTOVER_EVIDENCE_PATH.write_text(cutover_serialized, encoding="utf-8")
    finally:
        server.shutdown()
        server.server_close()
        server_thread.join(timeout=5)
        drop_database(database_name)

    print("Final Blob migration integration: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

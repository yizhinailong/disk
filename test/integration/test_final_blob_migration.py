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
import subprocess
import sys
import tempfile
import threading
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
        self.total_puts = 0
        self.block_on_put: int | None = None
        self.blocked = threading.Event()
        self.release = threading.Event()
        self.lock = threading.Lock()

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
                    record["content_id"] - 99,
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


def main() -> int:
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

            manifest_result = run_command(
                [
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
                ],
                env,
            )
            require(
                parse_summary(manifest_result)["objects"] == 3,
                "manifest command reports all objects",
            )
            target_keys = verify_manifest(
                manifest_path, records, local_root, object_prefix
            )

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
            require(
                checkpoint_ids(checkpoint_path) == [101],
                "checkpoint commits only the first verified object",
            )
            require(
                current_locators(database_name) == source_locators,
                "interrupted copy never changes DB paths",
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
            require(
                parse_summary(dry_cutover)["database_state_before"] == "source",
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

            applied = run_command([*cutover_args, "--execute"], env)
            applied_summary = parse_summary(applied)
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
            require(
                parse_summary(rolled_back)["rows_changed"] == 3,
                "rollback restores all source locators",
            )
            require(
                current_locators(database_name) == source_locators,
                "rollback uses the verified manifest sources",
            )

            final_cutover = run_command([*cutover_args, "--execute"], env)
            require(
                parse_summary(final_cutover)["rows_changed"] == 3,
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
    finally:
        server.shutdown()
        server.server_close()
        server_thread.join(timeout=5)
        drop_database(database_name)

    print("Final Blob migration integration: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

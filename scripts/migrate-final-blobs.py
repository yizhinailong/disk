#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["boto3", "psycopg[binary]"]
# ///

"""Migrate Disk final blobs from local storage to S3 during a maintenance window."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import sqlite3
import stat
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any, BinaryIO, Iterator

import boto3
import psycopg
from boto3.s3.transfer import TransferConfig
from botocore.config import Config
from botocore.exceptions import BotoCoreError, ClientError
from psycopg.rows import dict_row


MANIFEST_KIND = "disk-final-blob-manifest"
MANIFEST_VERSION = 1
CHECKPOINT_VERSION = "1"
HASH_CHUNK_BYTES = 1024 * 1024
DEFAULT_MULTIPART_CHUNK_BYTES = 64 * 1024 * 1024
MAX_MULTIPART_PARTS = 10_000
MAX_STORAGE_PATH_LENGTH = 512
MIGRATION_LOCK_NAME = "disk-final-blob-migration"


class MigrationError(RuntimeError):
    """Raised when a migration safety invariant is not satisfied."""


@dataclass(frozen=True)
class HashResult:
    size: int
    md5: str
    sha256: str


@dataclass(frozen=True)
class ManifestHeader:
    generated_at: str
    local_root: Path
    path_base: Path
    bucket: str
    object_prefix: str
    object_count: int


@dataclass(frozen=True)
class ManifestRecord:
    content_id: int
    source_locator: str
    source_relative_path: str
    size: int
    md5: str
    sha256: str
    target_key: str


@dataclass(frozen=True)
class Manifest:
    path: Path
    digest: str
    header: ManifestHeader

    def records(self) -> Iterator[ManifestRecord]:
        yield from iter_manifest_records(self.path, self.header)


class RateLimiter:
    """Serialize transfer callbacks to an optional byte-per-second budget."""

    def __init__(self, mib_per_second: float) -> None:
        self._bytes_per_second = mib_per_second * 1024 * 1024
        self._next_allowed = time.monotonic()
        self._lock = threading.Lock()

    def consume(self, byte_count: int) -> None:
        if self._bytes_per_second <= 0 or byte_count <= 0:
            return
        with self._lock:
            now = time.monotonic()
            start = max(now, self._next_allowed)
            self._next_allowed = start + byte_count / self._bytes_per_second
            delay = self._next_allowed - now
        if delay > 0:
            time.sleep(delay)


class Checkpoint:
    """Durable per-object verification state guarded by a process file lock."""

    def __init__(self, path: Path, manifest: Manifest, *, writable: bool) -> None:
        self._path = path.resolve()
        self._writable = writable
        self._lock_fd: int | None = None
        self._connection: sqlite3.Connection | None = None

        if writable:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            self._lock_fd = os.open(self._path, os.O_RDWR | os.O_CREAT, 0o600)
            lock_mode = fcntl.LOCK_EX | fcntl.LOCK_NB
        else:
            if not self._path.is_file():
                raise MigrationError(f"checkpoint does not exist: {self._path}")
            self._lock_fd = os.open(self._path, os.O_RDONLY)
            lock_mode = fcntl.LOCK_SH | fcntl.LOCK_NB

        try:
            fcntl.flock(self._lock_fd, lock_mode)
        except BlockingIOError as exc:
            os.close(self._lock_fd)
            self._lock_fd = None
            raise MigrationError(f"checkpoint is already in use: {self._path}") from exc

        try:
            if writable:
                self._connection = sqlite3.connect(self._path, timeout=5)
                self._connection.execute("PRAGMA journal_mode = DELETE")
                self._connection.execute("PRAGMA synchronous = FULL")
                self._initialize(manifest)
            else:
                uri = self._path.as_uri() + "?mode=ro"
                self._connection = sqlite3.connect(uri, uri=True, timeout=5)
                self._validate(manifest)
        except Exception:
            self.close()
            raise

    def __enter__(self) -> Checkpoint:
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    @property
    def connection(self) -> sqlite3.Connection:
        if self._connection is None:
            raise MigrationError("checkpoint is closed")
        return self._connection

    def close(self) -> None:
        if self._connection is not None:
            self._connection.close()
            self._connection = None
        if self._lock_fd is not None:
            fcntl.flock(self._lock_fd, fcntl.LOCK_UN)
            os.close(self._lock_fd)
            self._lock_fd = None

    def _expected_meta(self, manifest: Manifest) -> dict[str, str]:
        return {
            "checkpoint_version": CHECKPOINT_VERSION,
            "manifest_sha256": manifest.digest,
            "bucket": manifest.header.bucket,
            "object_prefix": manifest.header.object_prefix,
        }

    def _initialize(self, manifest: Manifest) -> None:
        with self.connection:
            self.connection.execute(
                "CREATE TABLE IF NOT EXISTS checkpoint_meta ("
                "key TEXT PRIMARY KEY, value TEXT NOT NULL)"
            )
            self.connection.execute(
                "CREATE TABLE IF NOT EXISTS verified_objects ("
                "content_id INTEGER PRIMARY KEY, "
                "source_locator TEXT NOT NULL, target_key TEXT NOT NULL, "
                "size INTEGER NOT NULL, hash_md5 TEXT NOT NULL, hash_sha256 TEXT NOT NULL, "
                "target_etag TEXT NOT NULL, verified_at TEXT NOT NULL)"
            )
            existing = dict(
                self.connection.execute("SELECT key, value FROM checkpoint_meta")
            )
            expected = self._expected_meta(manifest)
            if existing and existing != expected:
                raise MigrationError(
                    "checkpoint metadata does not match the manifest or S3 target"
                )
            if not existing:
                self.connection.executemany(
                    "INSERT INTO checkpoint_meta (key, value) VALUES (?, ?)",
                    expected.items(),
                )
        self._validate(manifest)

    def _validate(self, manifest: Manifest) -> None:
        try:
            tables = {
                row[0]
                for row in self.connection.execute(
                    "SELECT name FROM sqlite_master WHERE type = 'table'"
                )
            }
            if not {"checkpoint_meta", "verified_objects"} <= tables:
                raise MigrationError("checkpoint schema is incomplete")
            actual = dict(
                self.connection.execute("SELECT key, value FROM checkpoint_meta")
            )
        except sqlite3.DatabaseError as exc:
            raise MigrationError("checkpoint is not a valid SQLite database") from exc
        if actual != self._expected_meta(manifest):
            raise MigrationError(
                "checkpoint metadata does not match the manifest or S3 target"
            )

    def record_verified(self, record: ManifestRecord, etag: str) -> None:
        if not self._writable:
            raise MigrationError("read-only checkpoint cannot be updated")
        verified_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        with self.connection:
            self.connection.execute(
                "INSERT INTO verified_objects ("
                "content_id, source_locator, target_key, size, hash_md5, hash_sha256, "
                "target_etag, verified_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(content_id) DO UPDATE SET "
                "source_locator = excluded.source_locator, target_key = excluded.target_key, "
                "size = excluded.size, hash_md5 = excluded.hash_md5, "
                "hash_sha256 = excluded.hash_sha256, target_etag = excluded.target_etag, "
                "verified_at = excluded.verified_at",
                (
                    record.content_id,
                    record.source_locator,
                    record.target_key,
                    record.size,
                    record.md5,
                    record.sha256,
                    etag,
                    verified_at,
                ),
            )

    def require_verified(self, record: ManifestRecord) -> None:
        row = self.connection.execute(
            "SELECT source_locator, target_key, size, hash_md5, hash_sha256 "
            "FROM verified_objects WHERE content_id = ?",
            (record.content_id,),
        ).fetchone()
        expected = (
            record.source_locator,
            record.target_key,
            record.size,
            record.md5,
            record.sha256,
        )
        if row != expected:
            raise MigrationError(
                f"content {record.content_id} is not verified in the bound checkpoint"
            )


def is_lower_hex(value: str, length: int) -> bool:
    return len(value) == length and all(
        character in "0123456789abcdef" for character in value
    )


def normalize_object_prefix(value: str) -> str:
    prefix = value.strip("/")
    if not prefix or len(prefix) > 1024 or "\\" in prefix:
        raise MigrationError("object prefix is empty or invalid")
    for segment in prefix.split("/"):
        if not segment or segment in {".", ".."}:
            raise MigrationError("object prefix contains an unsafe path segment")
        if not all(
            character.isascii() and (character.isalnum() or character in "._-")
            for character in segment
        ):
            raise MigrationError("object prefix contains an unsupported character")
    return prefix


def target_key_for(prefix: str, sha256_hash: str) -> str:
    key = f"{prefix}/sha256/{sha256_hash[:2]}/{sha256_hash}.bin"
    if len(key) > MAX_STORAGE_PATH_LENGTH:
        raise MigrationError(
            f"target key exceeds file_contents.storage_path limit ({MAX_STORAGE_PATH_LENGTH})"
        )
    return key


def resolve_source_path(
    locator: str, path_base: Path, local_root: Path
) -> tuple[Path, str]:
    if not locator or len(locator) > MAX_STORAGE_PATH_LENGTH or "\x00" in locator:
        raise MigrationError("source storage_path is empty or invalid")
    candidate = Path(locator)
    if not candidate.is_absolute():
        candidate = path_base / candidate
    try:
        resolved = candidate.resolve(strict=True)
        relative = resolved.relative_to(local_root)
    except (FileNotFoundError, RuntimeError, ValueError) as exc:
        raise MigrationError(
            f"source path is missing or outside local root: {locator}"
        ) from exc
    mode = resolved.stat().st_mode
    if not stat.S_ISREG(mode):
        raise MigrationError(f"source path is not a regular file: {locator}")
    return resolved, relative.as_posix()


def source_path_from_record(header: ManifestHeader, record: ManifestRecord) -> Path:
    relative = PurePosixPath(record.source_relative_path)
    if (
        relative.is_absolute()
        or not relative.parts
        or any(part in {"", ".", ".."} for part in relative.parts)
    ):
        raise MigrationError(
            f"content {record.content_id} has an unsafe relative source path"
        )
    candidate = header.local_root.joinpath(*relative.parts)
    try:
        resolved = candidate.resolve(strict=True)
        resolved.relative_to(header.local_root)
    except (FileNotFoundError, RuntimeError, ValueError) as exc:
        raise MigrationError(
            f"content {record.content_id} source is missing or outside local root"
        ) from exc
    if not stat.S_ISREG(resolved.stat().st_mode):
        raise MigrationError(
            f"content {record.content_id} source is not a regular file"
        )
    return resolved


def hash_stream(stream: BinaryIO, limiter: RateLimiter | None = None) -> HashResult:
    md5_hash = hashlib.md5(usedforsecurity=False)
    sha256_hash = hashlib.sha256()
    total = 0
    while True:
        chunk = stream.read(HASH_CHUNK_BYTES)
        if not chunk:
            break
        total += len(chunk)
        md5_hash.update(chunk)
        sha256_hash.update(chunk)
        if limiter is not None:
            limiter.consume(len(chunk))
    return HashResult(
        size=total, md5=md5_hash.hexdigest(), sha256=sha256_hash.hexdigest()
    )


def hash_local_file(path: Path) -> HashResult:
    with path.open("rb") as stream:
        return hash_stream(stream)


def require_hashes(record: ManifestRecord, actual: HashResult, location: str) -> None:
    expected = HashResult(size=record.size, md5=record.md5, sha256=record.sha256)
    if actual != expected:
        raise MigrationError(
            f"content {record.content_id} {location} size/MD5/SHA-256 verification failed"
        )


def database_connection() -> psycopg.Connection[dict[str, Any]]:
    database_url = os.environ.get("DISK_DATABASE_URL", "")
    try:
        return psycopg.connect(database_url, row_factory=dict_row)
    except psycopg.Error as exc:
        raise MigrationError("failed to connect to PostgreSQL") from exc


def write_json_line(stream: Any, value: dict[str, Any]) -> None:
    stream.write(
        json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    )
    stream.write("\n")


def generate_manifest(args: argparse.Namespace) -> int:
    manifest_path = args.manifest.resolve()
    if manifest_path.exists():
        raise MigrationError(f"manifest already exists: {manifest_path}")
    if not manifest_path.parent.is_dir():
        raise MigrationError(
            f"manifest parent directory does not exist: {manifest_path.parent}"
        )

    local_root = args.local_root.resolve(strict=True)
    path_base = args.path_base.resolve(strict=True)
    if not local_root.is_dir() or not path_base.is_dir():
        raise MigrationError("local root and path base must be existing directories")
    object_prefix = normalize_object_prefix(args.object_prefix)
    if not args.bucket:
        raise MigrationError("bucket must not be empty")

    temporary_path: Path | None = None
    try:
        with database_connection() as connection:
            connection.execute(
                "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY"
            )
            count_row = connection.execute(
                "SELECT COUNT(*) AS count FROM file_contents"
            ).fetchone()
            if count_row is None:
                raise MigrationError("failed to count file_contents")
            object_count = int(count_row["count"])

            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                dir=manifest_path.parent,
                prefix=f".{manifest_path.name}.",
                delete=False,
            ) as output:
                temporary_path = Path(output.name)
                os.chmod(temporary_path, 0o600)
                write_json_line(
                    output,
                    {
                        "kind": MANIFEST_KIND,
                        "version": MANIFEST_VERSION,
                        "generated_at": datetime.now(timezone.utc)
                        .isoformat()
                        .replace("+00:00", "Z"),
                        "local_root": str(local_root),
                        "path_base": str(path_base),
                        "bucket": args.bucket,
                        "object_prefix": object_prefix,
                        "object_count": object_count,
                    },
                )

                seen_sha256: dict[str, int] = {}
                seen_sources: dict[str, int] = {}
                written = 0
                cursor = connection.cursor(name="disk_final_blob_manifest")
                cursor.itersize = 1000
                cursor.execute(
                    "SELECT id, hash_md5, hash_sha256, size, storage_path "
                    "FROM file_contents ORDER BY id"
                )
                for row in cursor:
                    content_id = int(row["id"])
                    md5_hash = str(row["hash_md5"]).strip()
                    sha256_hash = str(row["hash_sha256"]).strip()
                    size = int(row["size"])
                    source_locator = str(row["storage_path"])
                    if content_id <= 0 or size < 0:
                        raise MigrationError(
                            f"content {content_id} has an invalid ID or size"
                        )
                    if not is_lower_hex(md5_hash, 32) or not is_lower_hex(
                        sha256_hash, 64
                    ):
                        raise MigrationError(
                            f"content {content_id} has a non-canonical content hash"
                        )
                    if sha256_hash in seen_sha256:
                        raise MigrationError(
                            f"contents {seen_sha256[sha256_hash]} and {content_id} share a target SHA-256 key"
                        )
                    if source_locator in seen_sources:
                        raise MigrationError(
                            f"contents {seen_sources[source_locator]} and {content_id} share a source locator"
                        )

                    source_path, relative_path = resolve_source_path(
                        source_locator, path_base, local_root
                    )
                    if source_path.stat().st_size != size:
                        raise MigrationError(
                            f"content {content_id} source size differs from PostgreSQL"
                        )
                    target_key = target_key_for(object_prefix, sha256_hash)
                    write_json_line(
                        output,
                        {
                            "content_id": content_id,
                            "source_locator": source_locator,
                            "source_relative_path": relative_path,
                            "size": size,
                            "md5": md5_hash,
                            "sha256": sha256_hash,
                            "target_key": target_key,
                        },
                    )
                    seen_sha256[sha256_hash] = content_id
                    seen_sources[source_locator] = content_id
                    written += 1

                if written != object_count:
                    raise MigrationError(
                        "file_contents changed while generating the manifest"
                    )
                output.flush()
                os.fsync(output.fileno())

        try:
            os.link(temporary_path, manifest_path)
        except FileExistsError as exc:
            raise MigrationError(f"manifest already exists: {manifest_path}") from exc
        except OSError as exc:
            raise MigrationError(
                f"failed to publish manifest: {manifest_path}"
            ) from exc
        temporary_path.unlink()
        temporary_path = None
        directory_fd = os.open(manifest_path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except psycopg.Error as exc:
        raise MigrationError("failed to read the file_contents snapshot") from exc
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)

    print(
        json.dumps(
            {
                "command": "manifest",
                "manifest": str(manifest_path),
                "objects": object_count,
                "dry_run": True,
            },
            sort_keys=True,
        )
    )
    return 0


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(HASH_CHUNK_BYTES):
            digest.update(chunk)
    return digest.hexdigest()


def parse_manifest_header(path: Path) -> ManifestHeader:
    try:
        with path.open("r", encoding="utf-8") as stream:
            line = stream.readline()
        value = json.loads(line)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise MigrationError(f"failed to read manifest header: {path}") from exc
    if not isinstance(value, dict) or value.get("kind") != MANIFEST_KIND:
        raise MigrationError("manifest kind is invalid")
    if value.get("version") != MANIFEST_VERSION:
        raise MigrationError("manifest version is unsupported")
    try:
        local_root = Path(value["local_root"])
        path_base = Path(value["path_base"])
        generated_at = str(value["generated_at"])
        bucket = str(value["bucket"])
        object_prefix = normalize_object_prefix(str(value["object_prefix"]))
        object_count = int(value["object_count"])
    except (KeyError, TypeError, ValueError) as exc:
        raise MigrationError("manifest header fields are invalid") from exc
    if not local_root.is_absolute() or not path_base.is_absolute():
        raise MigrationError("manifest local root and path base must be absolute")
    if not bucket or object_count < 0:
        raise MigrationError("manifest bucket or object count is invalid")
    return ManifestHeader(
        generated_at=generated_at,
        local_root=local_root,
        path_base=path_base,
        bucket=bucket,
        object_prefix=object_prefix,
        object_count=object_count,
    )


def load_manifest(path: Path) -> Manifest:
    resolved = path.resolve(strict=True)
    if not resolved.is_file():
        raise MigrationError(f"manifest is not a regular file: {resolved}")
    manifest = Manifest(
        path=resolved,
        digest=file_sha256(resolved),
        header=parse_manifest_header(resolved),
    )
    count = sum(1 for _record in manifest.records())
    if count != manifest.header.object_count:
        raise MigrationError(
            f"manifest object count mismatch: header={manifest.header.object_count}, records={count}"
        )
    return manifest


def iter_manifest_records(
    path: Path, header: ManifestHeader
) -> Iterator[ManifestRecord]:
    previous_id = 0
    seen_targets: dict[str, int] = {}
    try:
        with path.open("r", encoding="utf-8") as stream:
            next(stream, None)
            for line_number, line in enumerate(stream, start=2):
                try:
                    value = json.loads(line)
                    record = ManifestRecord(
                        content_id=int(value["content_id"]),
                        source_locator=str(value["source_locator"]),
                        source_relative_path=str(value["source_relative_path"]),
                        size=int(value["size"]),
                        md5=str(value["md5"]),
                        sha256=str(value["sha256"]),
                        target_key=str(value["target_key"]),
                    )
                except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
                    raise MigrationError(
                        f"manifest line {line_number} is invalid"
                    ) from exc
                if record.content_id <= previous_id or record.size < 0:
                    raise MigrationError(
                        f"manifest line {line_number} is not in strict content ID order"
                    )
                if not is_lower_hex(record.md5, 32) or not is_lower_hex(
                    record.sha256, 64
                ):
                    raise MigrationError(
                        f"manifest line {line_number} has a non-canonical hash"
                    )
                expected_key = target_key_for(header.object_prefix, record.sha256)
                if record.target_key != expected_key:
                    raise MigrationError(
                        f"manifest line {line_number} target key is not canonical"
                    )
                if (
                    not record.source_locator
                    or len(record.source_locator) > MAX_STORAGE_PATH_LENGTH
                ):
                    raise MigrationError(
                        f"manifest line {line_number} source locator is invalid"
                    )
                if record.target_key in seen_targets:
                    raise MigrationError(
                        f"manifest contents {seen_targets[record.target_key]} and {record.content_id} share a target key"
                    )
                seen_targets[record.target_key] = record.content_id
                previous_id = record.content_id
                yield record
    except OSError as exc:
        raise MigrationError(f"failed to read manifest records: {path}") from exc


def env_bool(name: str, default: bool) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    raise MigrationError(f"{name} must be a boolean")


def s3_client(args: argparse.Namespace) -> Any:
    access_key = os.environ.get("DISK_S3_ACCESS_KEY")
    secret_key = os.environ.get("DISK_S3_SECRET_KEY")
    if bool(access_key) != bool(secret_key):
        raise MigrationError(
            "DISK_S3_ACCESS_KEY and DISK_S3_SECRET_KEY must be set together"
        )
    kwargs: dict[str, Any] = {
        "region_name": args.region,
        "verify": args.verify_ssl,
        "config": Config(
            signature_version="s3v4",
            retries={"mode": "standard", "max_attempts": 3},
            s3={"addressing_style": "path" if args.force_path_style else "auto"},
        ),
    }
    if args.endpoint:
        kwargs["endpoint_url"] = args.endpoint
    if access_key and secret_key:
        kwargs["aws_access_key_id"] = access_key
        kwargs["aws_secret_access_key"] = secret_key
        session_token = os.environ.get("DISK_S3_SESSION_TOKEN")
        if session_token:
            kwargs["aws_session_token"] = session_token
    try:
        return boto3.client("s3", **kwargs)
    except (BotoCoreError, ValueError) as exc:
        raise MigrationError("failed to initialize the S3 client") from exc


def s3_error_code(exc: ClientError) -> str:
    return str(exc.response.get("Error", {}).get("Code", "unknown"))


def head_target(client: Any, bucket: str, key: str) -> dict[str, Any] | None:
    try:
        return client.head_object(Bucket=bucket, Key=key)
    except ClientError as exc:
        if s3_error_code(exc) in {"404", "NoSuchKey", "NotFound"}:
            return None
        raise MigrationError(f"S3 HEAD failed for {key}: {s3_error_code(exc)}") from exc
    except BotoCoreError as exc:
        raise MigrationError(f"S3 HEAD failed for {key}") from exc


def verify_target(
    client: Any,
    bucket: str,
    record: ManifestRecord,
    limiter: RateLimiter,
) -> str | None:
    head = head_target(client, bucket, record.target_key)
    if head is None:
        return None
    if int(head.get("ContentLength", -1)) != record.size:
        raise MigrationError(
            f"content {record.content_id} target size verification failed"
        )
    try:
        response = client.get_object(Bucket=bucket, Key=record.target_key)
        body = response["Body"]
        try:
            actual = hash_stream(body, limiter)
        finally:
            body.close()
    except ClientError as exc:
        raise MigrationError(
            f"S3 GET failed for {record.target_key}: {s3_error_code(exc)}"
        ) from exc
    except (BotoCoreError, OSError) as exc:
        raise MigrationError(f"S3 GET failed for {record.target_key}") from exc
    require_hashes(record, actual, "target")
    return str(head.get("ETag", "")).strip('"')


def multipart_chunk_size(size: int) -> int:
    minimum = (size + MAX_MULTIPART_PARTS - 1) // MAX_MULTIPART_PARTS
    mib = 1024 * 1024
    rounded = ((minimum + mib - 1) // mib) * mib
    return max(DEFAULT_MULTIPART_CHUNK_BYTES, rounded)


def upload_target(
    client: Any,
    bucket: str,
    source: Path,
    record: ManifestRecord,
    limiter: RateLimiter,
) -> None:
    transfer_config = TransferConfig(
        multipart_threshold=DEFAULT_MULTIPART_CHUNK_BYTES,
        multipart_chunksize=multipart_chunk_size(record.size),
        max_concurrency=1,
        use_threads=False,
    )
    try:
        client.upload_file(
            str(source),
            bucket,
            record.target_key,
            ExtraArgs={
                "Metadata": {
                    "disk-content-id": str(record.content_id),
                    "disk-md5": record.md5,
                    "disk-sha256": record.sha256,
                }
            },
            Callback=limiter.consume,
            Config=transfer_config,
        )
    except ClientError as exc:
        raise MigrationError(
            f"S3 upload failed for {record.target_key}: {s3_error_code(exc)}"
        ) from exc
    except (BotoCoreError, OSError) as exc:
        raise MigrationError(f"S3 upload failed for {record.target_key}") from exc


def copy_blobs(args: argparse.Namespace) -> int:
    manifest = load_manifest(args.manifest)
    client = s3_client(args)
    limiter = RateLimiter(args.rate_limit_mib_per_second)
    checkpoint_context: Checkpoint | None = None
    uploaded = 0
    reused = 0
    would_upload = 0
    processed = 0

    try:
        if args.execute:
            checkpoint_context = Checkpoint(args.checkpoint, manifest, writable=True)
        for record in manifest.records():
            if args.max_objects is not None and processed >= args.max_objects:
                break
            source = source_path_from_record(manifest.header, record)
            require_hashes(record, hash_local_file(source), "source")
            etag = verify_target(client, manifest.header.bucket, record, limiter)
            if etag is None:
                if not args.execute:
                    would_upload += 1
                    processed += 1
                    continue
                upload_target(client, manifest.header.bucket, source, record, limiter)
                etag = verify_target(client, manifest.header.bucket, record, limiter)
                if etag is None:
                    raise MigrationError(
                        f"content {record.content_id} target is absent after successful upload"
                    )
                uploaded += 1
            else:
                reused += 1
            if checkpoint_context is not None:
                checkpoint_context.record_verified(record, etag)
            processed += 1
    finally:
        if checkpoint_context is not None:
            checkpoint_context.close()

    print(
        json.dumps(
            {
                "command": "copy",
                "dry_run": not args.execute,
                "manifest_objects": manifest.header.object_count,
                "processed": processed,
                "uploaded": uploaded,
                "reused": reused,
                "would_upload": would_upload,
                "remaining": manifest.header.object_count - processed,
            },
            sort_keys=True,
        )
    )
    return 0


def verify_all_targets(
    manifest: Manifest,
    checkpoint: Checkpoint,
    client: Any,
    limiter: RateLimiter,
) -> None:
    verified = 0
    for record in manifest.records():
        checkpoint.require_verified(record)
        if verify_target(client, manifest.header.bucket, record, limiter) is None:
            raise MigrationError(f"content {record.content_id} target is missing")
        verified += 1
    if verified != manifest.header.object_count:
        raise MigrationError("not all manifest objects were verified")


def load_manifest_temp_table(
    connection: psycopg.Connection[dict[str, Any]], manifest: Manifest
) -> None:
    connection.execute(
        "CREATE TEMP TABLE disk_final_blob_manifest ("
        "content_id BIGINT PRIMARY KEY, source_locator TEXT NOT NULL, "
        "target_key TEXT NOT NULL UNIQUE, size BIGINT NOT NULL, "
        "hash_md5 TEXT NOT NULL, hash_sha256 TEXT NOT NULL) ON COMMIT DROP"
    )
    with connection.cursor().copy(
        "COPY disk_final_blob_manifest "
        "(content_id, source_locator, target_key, size, hash_md5, hash_sha256) FROM STDIN"
    ) as copy:
        for record in manifest.records():
            copy.write_row(
                (
                    record.content_id,
                    record.source_locator,
                    record.target_key,
                    record.size,
                    record.md5,
                    record.sha256,
                )
            )


def classify_database_state(
    connection: psycopg.Connection[dict[str, Any]], manifest_count: int
) -> str:
    row = connection.execute(
        "SELECT "
        "  (SELECT COUNT(*) FROM file_contents) AS db_count, "
        "  (SELECT COUNT(*) FROM disk_final_blob_manifest) AS manifest_count, "
        "  (SELECT COUNT(*) FROM file_contents AS content "
        "   JOIN disk_final_blob_manifest AS manifest ON manifest.content_id = content.id "
        "   WHERE btrim(content.hash_md5) = manifest.hash_md5 "
        "     AND btrim(content.hash_sha256) = manifest.hash_sha256 "
        "     AND content.size = manifest.size "
        "     AND content.storage_path = manifest.source_locator) AS source_matches, "
        "  (SELECT COUNT(*) FROM file_contents AS content "
        "   JOIN disk_final_blob_manifest AS manifest ON manifest.content_id = content.id "
        "   WHERE btrim(content.hash_md5) = manifest.hash_md5 "
        "     AND btrim(content.hash_sha256) = manifest.hash_sha256 "
        "     AND content.size = manifest.size "
        "     AND content.storage_path = manifest.target_key) AS target_matches"
    ).fetchone()
    if row is None:
        raise MigrationError("failed to classify file_contents state")
    if (
        int(row["db_count"]) != manifest_count
        or int(row["manifest_count"]) != manifest_count
    ):
        raise MigrationError("file_contents set differs from the manifest")
    source_matches = int(row["source_matches"])
    target_matches = int(row["target_matches"])
    if manifest_count == 0:
        return "empty"
    if source_matches == manifest_count:
        return "source"
    if target_matches == manifest_count:
        return "target"
    raise MigrationError(
        "file_contents is drifted or contains mixed source and target paths"
    )


def switch_database_paths(
    manifest: Manifest, *, desired: str, execute: bool
) -> tuple[str, int]:
    try:
        with database_connection() as connection:
            connection.execute("SET TRANSACTION ISOLATION LEVEL SERIALIZABLE")
            load_manifest_temp_table(connection, manifest)
            connection.execute(
                "SELECT pg_advisory_xact_lock(hashtextextended(%s, 0))",
                (MIGRATION_LOCK_NAME,),
            )
            connection.execute(
                "LOCK TABLE file_contents IN ACCESS EXCLUSIVE MODE NOWAIT"
            )
            state = classify_database_state(connection, manifest.header.object_count)
            changed = 0
            if execute and desired == "target" and state == "source":
                result = connection.execute(
                    "UPDATE file_contents AS content SET storage_path = manifest.target_key "
                    "FROM disk_final_blob_manifest AS manifest "
                    "WHERE content.id = manifest.content_id "
                    "  AND content.storage_path = manifest.source_locator "
                    "  AND btrim(content.hash_md5) = manifest.hash_md5 "
                    "  AND btrim(content.hash_sha256) = manifest.hash_sha256 "
                    "  AND content.size = manifest.size"
                )
                changed = result.rowcount
            elif execute and desired == "source" and state == "target":
                result = connection.execute(
                    "UPDATE file_contents AS content SET storage_path = manifest.source_locator "
                    "FROM disk_final_blob_manifest AS manifest "
                    "WHERE content.id = manifest.content_id "
                    "  AND content.storage_path = manifest.target_key "
                    "  AND btrim(content.hash_md5) = manifest.hash_md5 "
                    "  AND btrim(content.hash_sha256) = manifest.hash_sha256 "
                    "  AND content.size = manifest.size"
                )
                changed = result.rowcount
            expected_changed = (
                manifest.header.object_count
                if state != desired and state != "empty"
                else 0
            )
            if execute and changed != expected_changed:
                raise MigrationError(
                    f"atomic path switch updated {changed} rows, expected {expected_changed}"
                )
            return state, changed
    except psycopg.errors.LockNotAvailable as exc:
        raise MigrationError(
            "file_contents is in use; stop all API and Worker processes before cutover"
        ) from exc
    except psycopg.Error as exc:
        raise MigrationError("PostgreSQL path switch failed") from exc


def cutover(args: argparse.Namespace) -> int:
    manifest = load_manifest(args.manifest)
    client = s3_client(args)
    limiter = RateLimiter(args.rate_limit_mib_per_second)
    with Checkpoint(args.checkpoint, manifest, writable=False) as checkpoint:
        verify_all_targets(manifest, checkpoint, client, limiter)
        state, changed = switch_database_paths(
            manifest, desired="target", execute=args.execute
        )
    print(
        json.dumps(
            {
                "command": "cutover",
                "dry_run": not args.execute,
                "database_state_before": state,
                "objects": manifest.header.object_count,
                "rows_changed": changed,
            },
            sort_keys=True,
        )
    )
    return 0


def rollback(args: argparse.Namespace) -> int:
    manifest = load_manifest(args.manifest)
    verified = 0
    for record in manifest.records():
        source = source_path_from_record(manifest.header, record)
        require_hashes(record, hash_local_file(source), "rollback source")
        verified += 1
    if verified != manifest.header.object_count:
        raise MigrationError("not all rollback sources were verified")
    state, changed = switch_database_paths(
        manifest, desired="source", execute=args.execute
    )
    print(
        json.dumps(
            {
                "command": "rollback",
                "dry_run": not args.execute,
                "database_state_before": state,
                "objects": manifest.header.object_count,
                "rows_changed": changed,
            },
            sort_keys=True,
        )
    )
    return 0


def add_s3_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--endpoint", default=os.environ.get("DISK_S3_ENDPOINT"))
    parser.add_argument(
        "--region", default=os.environ.get("DISK_S3_REGION", "us-east-1")
    )
    parser.add_argument(
        "--force-path-style",
        action=argparse.BooleanOptionalAction,
        default=env_bool("DISK_S3_FORCE_PATH_STYLE", True),
    )
    parser.add_argument(
        "--verify-ssl",
        action=argparse.BooleanOptionalAction,
        default=env_bool("DISK_S3_VERIFY_SSL", True),
    )
    parser.add_argument(
        "--rate-limit-mib-per-second",
        type=float,
        default=0.0,
        help="Aggregate upload and verification rate; 0 disables throttling",
    )


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    manifest_parser = subparsers.add_parser(
        "manifest", help="Create a read-only DB snapshot manifest"
    )
    manifest_parser.add_argument("--manifest", type=Path, required=True)
    manifest_parser.add_argument("--local-root", type=Path, required=True)
    manifest_parser.add_argument("--path-base", type=Path, required=True)
    manifest_parser.add_argument("--bucket", required=True)
    manifest_parser.add_argument("--object-prefix", default="objects")
    manifest_parser.set_defaults(handler=generate_manifest)

    copy_parser = subparsers.add_parser(
        "copy", help="Verify and copy manifest objects to S3"
    )
    copy_parser.add_argument("--manifest", type=Path, required=True)
    copy_parser.add_argument("--checkpoint", type=Path, required=True)
    copy_parser.add_argument("--execute", action="store_true")
    copy_parser.add_argument("--max-objects", type=positive_int)
    add_s3_arguments(copy_parser)
    copy_parser.set_defaults(handler=copy_blobs)

    cutover_parser = subparsers.add_parser(
        "cutover", help="Atomically switch DB locators to S3"
    )
    cutover_parser.add_argument("--manifest", type=Path, required=True)
    cutover_parser.add_argument("--checkpoint", type=Path, required=True)
    cutover_parser.add_argument("--execute", action="store_true")
    add_s3_arguments(cutover_parser)
    cutover_parser.set_defaults(handler=cutover)

    rollback_parser = subparsers.add_parser(
        "rollback", help="Atomically restore local DB locators"
    )
    rollback_parser.add_argument("--manifest", type=Path, required=True)
    rollback_parser.add_argument("--execute", action="store_true")
    rollback_parser.set_defaults(handler=rollback)

    return parser


def main() -> int:
    try:
        parser = build_parser()
        args = parser.parse_args()
        if (
            hasattr(args, "rate_limit_mib_per_second")
            and args.rate_limit_mib_per_second < 0
        ):
            raise MigrationError("rate limit must not be negative")
        return int(args.handler(args))
    except MigrationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

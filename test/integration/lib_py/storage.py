# test/integration/lib_py/storage.py
# Config and filesystem helpers for local-storage integration invariants.

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Any


def repo_root() -> Path:
    """Return the repository root for integration tests."""
    return Path(__file__).resolve().parents[3]


def load_disk_config() -> dict[str, Any]:
    """Load the repository config.json file."""
    config_path = Path(os.environ.get("DISK_CONFIG", repo_root() / "config.json"))
    with config_path.open(encoding="utf-8") as handle:
        return json.load(handle)


def disk_config() -> dict[str, Any]:
    """Return the custom_config.disk section from config.json."""
    return load_disk_config().get("custom_config", {}).get("disk", {})


def configured_chunk_size() -> int:
    """Return the configured upload chunk size in bytes."""
    return int(os.environ.get("DISK_CHUNK_SIZE") or disk_config().get("chunk_size", 5 * 1024 * 1024))


def configured_storage_base_path() -> Path:
    """Return the configured final blob storage root as an absolute path."""
    value = os.environ.get("DISK_STORAGE_BASE_PATH") or disk_config().get("storage_base_path", "build/uploaded")
    return _resolve_repo_path(value)


def configured_temp_upload_path() -> Path:
    """Return the configured temporary upload root as an absolute path."""
    value = os.environ.get("DISK_TEMP_UPLOAD_PATH") or disk_config().get("temp_upload_path", "build/temp_uploads")
    return _resolve_repo_path(value)


def final_blob_path(file_hash: str) -> Path:
    """Return the local final blob path for an MD5 hash."""
    return configured_storage_base_path() / file_hash[:2] / f"{file_hash}.bin"


def upload_temp_dir(upload_id: str) -> Path:
    """Return the temporary directory for an upload task."""
    return configured_temp_upload_path() / upload_id


def upload_chunk_path(upload_id: str, chunk_index: int) -> Path:
    """Return the temporary chunk path for an upload task."""
    return upload_temp_dir(upload_id) / f"{chunk_index}.chunk"


def assembled_temp_path(upload_id: str) -> Path:
    """Return the assembled temporary file path for an upload task."""
    return configured_temp_upload_path() / f"{upload_id}.tmp"


def md5_bytes(data: bytes) -> str:
    """Return the MD5 hex digest for bytes."""
    return hashlib.md5(data).hexdigest()


def sha256_bytes(data: bytes) -> str:
    """Return the SHA256 hex digest for bytes."""
    return hashlib.sha256(data).hexdigest()


def _resolve_repo_path(value: str | os.PathLike[str]) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    return repo_root() / path

# test/integration/lib_py/fixtures.py
# Test fixture helpers: temp files, MD5 hashing, unique names.

from __future__ import annotations

import hashlib
import os
import tempfile
import time


def create_temp_file(size_bytes: int = 1024, suffix: str = ".dat") -> str:
    """Create a temp file with random data, return its path.

    The caller is responsible for cleanup.
    """
    fd, path = tempfile.mkstemp(suffix=suffix, prefix="test_upload_")
    with os.fdopen(fd, "wb") as f:
        f.write(os.urandom(size_bytes))
    return path


def md5_hash(filepath: str) -> str:
    """Compute MD5 hex digest of a file, return string."""
    h = hashlib.md5()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def unique_name(prefix: str = "test") -> str:
    """Generate unique name using PID + timestamp."""
    return f"{prefix}_{os.getpid()}_{int(time.time() * 1000)}"

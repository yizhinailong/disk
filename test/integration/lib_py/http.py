# test/integration/lib_py/http.py
# HTTP utilities: JSON parsing, httpx-based fetch (stdlib urllib fallback), Redis cleanup.
#
# Mirrors lib/http.sh behavior exactly.

from __future__ import annotations

import json
import os
import socket
from typing import Any

try:
    import httpx

    _HAS_HTTPX = True
except ModuleNotFoundError:
    _HAS_HTTPX = False

BASE_URL: str = os.environ.get("BASE_URL", "http://127.0.0.1:8080")


# ─── JSON parsing ─────────────────────────────────────────────────────────────


def json_field(json_str: str, path: str) -> str:
    """Parse JSON string, navigate dot-separated path.

    Handles bool → "true"/"false", None → "", other → str.
    Returns "" if path not found.
    Supports array indexing like "data.results.0.status".
    """
    try:
        value: Any = json.loads(json_str)
    except Exception:
        return ""

    for part in path.split("."):
        if isinstance(value, dict) and part in value:
            value = value[part]
        elif isinstance(value, list):
            try:
                idx = int(part)
                value = value[idx]
            except (ValueError, IndexError):
                return ""
        else:
            return ""

    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return ""
    return str(value)


def json_value(json_str: str, path: str) -> str:
    """Alias for json_field."""
    return json_field(json_str, path)


def json_int(json_str: str, path: str) -> str:
    """Alias for json_field (returns string, caller can int() it)."""
    return json_field(json_str, path)


# ─── HTTP fetch ────────────────────────────────────────────────────────────────


class Response:
    """Simple wrapper matching the curl_fetch contract from http.sh."""

    __slots__ = ("status_code", "headers", "text")

    def __init__(self, status_code: int, headers: dict[str, str], text: str) -> None:
        self.status_code = status_code
        self.headers = _CaseInsensitiveDict(headers)
        self.text = text


class _CaseInsensitiveDict(dict):
    """Dict with case-insensitive key lookup for headers."""

    def __init__(self, data: dict[str, str] | None = None) -> None:
        super().__init__()
        self._keys: dict[str, str] = {}
        if data:
            for k, v in data.items():
                self[k] = v

    def __setitem__(self, key: str, value: str) -> None:
        lower = key.lower()
        self._keys[lower] = key
        super().__setitem__(key, value)

    def __getitem__(self, key: str) -> str:
        lower = key.lower()
        if lower in self._keys:
            return super().__getitem__(self._keys[lower])
        raise KeyError(key)

    def __contains__(self, key: object) -> bool:
        if isinstance(key, str):
            return key.lower() in self._keys
        return False

    def get(self, key: str, default: str | None = None) -> str | None:  # type: ignore[override]
        try:
            return self[key]
        except KeyError:
            return default


def fetch(
    url: str,
    *,
    method: str = "GET",
    headers: dict[str, str] | None = None,
    json_body: Any = None,
    data: bytes | str | None = None,
    timeout: int = 30,
) -> Response:
    """HTTP request. Uses httpx when available, otherwise stdlib urllib."""
    if url.startswith("/"):
        url = BASE_URL + url

    if _HAS_HTTPX:
        return _fetch_httpx(url, method=method, headers=headers, json_body=json_body, data=data, timeout=timeout)
    return _fetch_urllib(url, method=method, headers=headers, json_body=json_body, data=data, timeout=timeout)


def _fetch_httpx(
    url: str,
    *,
    method: str = "GET",
    headers: dict[str, str] | None = None,
    json_body: Any = None,
    data: bytes | str | None = None,
    timeout: int = 30,
) -> Response:
    with httpx.Client(timeout=timeout) as client:
        resp = client.request(method, url, headers=headers, json=json_body, content=data)

    resp_headers: dict[str, str] = {}
    for k, v in resp.headers.multi_items():
        resp_headers[k] = v

    return Response(resp.status_code, resp_headers, resp.text)


def _fetch_urllib(
    url: str,
    *,
    method: str = "GET",
    headers: dict[str, str] | None = None,
    json_body: Any = None,
    data: bytes | str | None = None,
    timeout: int = 30,
) -> Response:
    import urllib.request
    import urllib.error

    req_headers: dict[str, str] = dict(headers or {})

    body: bytes | None = None
    if json_body is not None:
        body = json.dumps(json_body).encode("utf-8")
        req_headers.setdefault("Content-Type", "application/json")
    elif data is not None:
        body = data.encode("utf-8") if isinstance(data, str) else data

    req = urllib.request.Request(url, data=body, headers=req_headers, method=method)

    try:
        with urllib.request.urlopen(req, timeout=timeout) as raw_resp:
            status = raw_resp.status
            resp_headers = {k: v for k, v in raw_resp.getheaders()}
            text = raw_resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        status = exc.code
        resp_headers = {k: v for k, v in exc.headers.items()} if exc.headers else {}
        text = exc.read().decode("utf-8", errors="replace") if exc.fp else ""

    return Response(status, resp_headers, text)


def header_value(headers: dict[str, str], name: str) -> str:
    """Case-insensitive header lookup. Returns '' if not found."""
    if isinstance(headers, _CaseInsensitiveDict):
        return headers.get(name, "")
    lower = name.lower()
    for k, v in headers.items():
        if k.lower() == lower:
            return v
    return ""


# ─── Redis cleanup ────────────────────────────────────────────────────────────


def _redis_command(parts: list[str], host: str, port: int) -> str:
    """Send a raw RESP command to Redis, return decoded reply."""
    payload = f"*{len(parts)}\r\n"
    for part in parts:
        encoded = part.encode()
        payload += f"${len(encoded)}\r\n{part}\r\n"

    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(payload.encode())
        reply = sock.recv(4096).decode(errors="ignore")
    return reply


def redis_get_value(
    key: str,
    host: str = "127.0.0.1",
    port: int = 6379,
) -> str | None:
    """Return a Redis string value, or None when the key does not exist."""
    host = os.environ.get("REDIS_HOST", host)
    port = int(os.environ.get("REDIS_PORT", str(port)))

    reply = _redis_command(["GET", key], host, port)
    if reply.startswith("$-1\r\n"):
        return None
    if not reply.startswith("$"):
        raise RuntimeError(f"Unexpected Redis reply: {reply.strip()}")

    length_line, separator, remainder = reply.partition("\r\n")
    if not separator:
        raise RuntimeError(f"Incomplete Redis reply: {reply.strip()}")
    length = int(length_line[1:])
    value = remainder[:length]
    if len(value.encode()) != length:
        raise RuntimeError(f"Incomplete Redis bulk reply: {reply.strip()}")
    return value


def redis_ttl(
    key: str,
    host: str = "127.0.0.1",
    port: int = 6379,
) -> int:
    """Return the Redis TTL result in seconds (-1 or -2 retain Redis semantics)."""
    host = os.environ.get("REDIS_HOST", host)
    port = int(os.environ.get("REDIS_PORT", str(port)))

    reply = _redis_command(["TTL", key], host, port)
    if not reply.startswith(":"):
        raise RuntimeError(f"Unexpected Redis reply: {reply.strip()}")
    return int(reply[1:].split("\r\n", 1)[0])


def redis_keys(
    pattern: str,
    host: str = "127.0.0.1",
    port: int = 6379,
) -> list[str]:
    """Return sorted Redis keys matching a test-owned pattern."""
    host = os.environ.get("REDIS_HOST", host)
    port = int(os.environ.get("REDIS_PORT", str(port)))

    reply = _redis_command(["KEYS", pattern], host, port)
    if not reply.startswith("*"):
        raise RuntimeError(f"Unexpected Redis reply: {reply.strip()}")

    keys: list[str] = []
    lines = reply.split("\r\n")
    index = 1
    while index < len(lines):
        if not lines[index].startswith("$"):
            index += 1
            continue

        length = int(lines[index][1:])
        key = lines[index + 1] if index + 1 < len(lines) else ""
        if len(key.encode()) == length:
            keys.append(key)
        index += 2

    return sorted(keys)


def redis_set_value(
    key: str,
    value: str,
    ttl_seconds: int,
    host: str = "127.0.0.1",
    port: int = 6379,
) -> None:
    """Set a Redis string with a positive TTL for fault/auth test setup."""
    if ttl_seconds <= 0:
        raise ValueError("ttl_seconds must be positive")

    host = os.environ.get("REDIS_HOST", host)
    port = int(os.environ.get("REDIS_PORT", str(port)))
    reply = _redis_command(
        ["SET", key, value, "EX", str(ttl_seconds)],
        host,
        port,
    )
    if not reply.startswith("+OK"):
        raise RuntimeError(f"Unexpected Redis reply: {reply.strip()}")


def redis_delete_pattern(
    pattern: str,
    host: str = "127.0.0.1",
    port: int = 6379,
) -> None:
    """Delete all Redis keys matching pattern (KEYS + DEL)."""
    host = os.environ.get("REDIS_HOST", host)
    port = int(os.environ.get("REDIS_PORT", str(port)))

    keys = redis_keys(pattern, host, port)

    if keys:
        _redis_command(["DEL"] + keys, host, port)


def redis_delete_key(
    key: str,
    host: str = "127.0.0.1",
    port: int = 6379,
) -> None:
    """Delete a single Redis key."""
    host = os.environ.get("REDIS_HOST", host)
    port = int(os.environ.get("REDIS_PORT", str(port)))

    reply = _redis_command(["DEL", key], host, port)
    if not reply.startswith(":"):
        raise RuntimeError(f"Unexpected Redis reply: {reply.strip()}")

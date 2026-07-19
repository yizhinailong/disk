#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Serial integration evidence for route-owned share rate limits."""

from __future__ import annotations

import atexit
import base64
import hashlib
import hmac
import json
import os
import subprocess
import sys
import time
import uuid
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_ROOT = Path(os.environ.get("EVIDENCE_DIR", REPO_ROOT / ".sisyphus/evidence"))
SERVER_LOG_PATH = EVIDENCE_ROOT / "share-rate-limit-server.log"
os.environ["SERVER_LOG"] = str(SERVER_LOG_PATH)

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from lib_py import (  # noqa: E402
    cleanup,
    create_temp_file,
    do_login,
    ensure_server,
    fetch,
    log_fail,
    log_info,
    log_pass,
    md5_hash,
    print_summary,
    query_all,
    redis_delete_key,
    redis_delete_pattern,
    redis_get_value,
    redis_keys,
    redis_set_value,
    redis_ttl,
    save_evidence,
    unique_name,
)

atexit.register(cleanup)

TEST_USER = os.environ.get("TEST_USER", "admin")
TEST_PASS = os.environ.get("TEST_PASS", "Admin123")
CLIENT_IP = os.environ.get("CLIENT_IP", "127.0.0.1")
JWT_SECRET = os.environ.get(
    "JWT_SECRET", "dev-only-jwt-secret-key-change-in-production-2024"
)
EVIDENCE_NAME = "share-rate-limit-summary.json"

DEFAULTS = {
    "access": {"limit": 30, "window_seconds": 60},
    "browse": {"limit": 60, "window_seconds": 60},
    "download": {"limit": 10, "window_seconds": 60},
}

RAW_CREDENTIALS: list[str] = [TEST_PASS, JWT_SECRET]
KNOWN_JTIS: set[str] = set()
RATE_IDENTITIES: set[tuple[str, str]] = set()
ACTUAL_RATE_KEYS: set[str] = set()
BLACKLIST_KEYS: set[str] = set()
CREATED_SHARES: list[str] = []
OWNER_TOKEN = ""
SHARES_CANCELLED = False


class TestFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class FamilyConfig:
    limit: int
    window_seconds: int


@dataclass(frozen=True)
class RateConfig:
    access: FamilyConfig
    browse: FamilyConfig
    download: FamilyConfig


def fail(message: str) -> None:
    log_fail(message)
    raise TestFailure(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def pass_if(condition: bool, message: str) -> None:
    require(condition, message)
    log_pass(message)


def positive_config_value(config: dict[str, Any], key: str, fallback: int) -> int:
    value = config.get(key)
    if isinstance(value, int) and not isinstance(value, bool) and value > 0:
        return value
    return fallback


def load_rate_config() -> RateConfig:
    config_path = Path(os.environ.get("DISK_CONFIG", REPO_ROOT / "config.json"))
    with config_path.open(encoding="utf-8") as handle:
        root = json.load(handle)
    disk = root.get("custom_config", {}).get("disk", {})

    families: dict[str, FamilyConfig] = {}
    for family, defaults in DEFAULTS.items():
        families[family] = FamilyConfig(
            limit=positive_config_value(
                disk,
                f"share_{family}_rate_limit_per_minute",
                defaults["limit"],
            ),
            window_seconds=positive_config_value(
                disk,
                f"share_{family}_rate_limit_window_seconds",
                defaults["window_seconds"],
            ),
        )

    for family, values in families.items():
        require(values.limit <= 500, f"{family} integration limit exceeds safety cap")
        require(values.window_seconds > 1, f"{family} window must exceed one second")

    return RateConfig(**families)


def response_json(response: Any, label: str) -> dict[str, Any]:
    try:
        body = json.loads(response.text)
    except json.JSONDecodeError as error:
        fail(f"{label}: response is not JSON: {error}")
    require(isinstance(body, dict), f"{label}: response body must be an object")
    return body


def require_success(response: Any, label: str) -> dict[str, Any]:
    body = response_json(response, label)
    require(
        response.status_code == 200 and str(body.get("code")) == "0",
        f"{label}: expected HTTP 200/code 0, got {response.status_code}/{body.get('code')}",
    )
    return body


def base64url_decode(segment: str) -> bytes:
    return base64.urlsafe_b64decode(segment + "=" * (-len(segment) % 4))


def base64url_encode(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).decode("ascii").rstrip("=")


def decode_token(token: str) -> dict[str, Any]:
    parts = token.split(".")
    require(len(parts) == 3, "issued share token must contain three JWT segments")
    try:
        payload = json.loads(base64url_decode(parts[1]).decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as error:
        fail(f"issued share token payload is invalid: {error}")
    require(isinstance(payload, dict), "issued share token payload must be an object")
    jti = payload.get("jti")
    require(isinstance(jti, str) and bool(jti), "issued share token must contain JTI")
    KNOWN_JTIS.add(jti)
    return payload


def make_invalid_signature(token: str) -> str:
    parts = token.split(".")
    replacement = "A" if parts[2][0] != "A" else "B"
    return ".".join((parts[0], parts[1], replacement + parts[2][1:]))


def make_expired_token(valid_token: str) -> str:
    header_segment, payload_segment, _ = valid_token.split(".")
    payload = decode_token(valid_token).copy()
    expired_at = int(time.time()) - 3600
    payload["iat"] = expired_at
    payload["exp"] = expired_at
    payload["jti"] = f"share-rate-expired-{uuid.uuid4()}"
    KNOWN_JTIS.add(payload["jti"])

    encoded_payload = base64url_encode(
        json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
    )
    signing_input = f"{header_segment}.{encoded_payload}".encode("ascii")
    signature = hmac.new(JWT_SECRET.encode(), signing_input, hashlib.sha256).digest()
    return f"{header_segment}.{encoded_payload}.{base64url_encode(signature)}"


def rate_pattern(family: str, identity: str) -> str:
    return f"rate:share_{family}:{identity}:*"


def clear_rate_identity(family: str, identity: str) -> None:
    RATE_IDENTITIES.add((family, identity))
    redis_delete_pattern(rate_pattern(family, identity))


def matching_rate_keys(family: str, identity: str) -> list[str]:
    RATE_IDENTITIES.add((family, identity))
    return redis_keys(rate_pattern(family, identity))


def require_single_rate_key(family: str, identity: str) -> str:
    keys = matching_rate_keys(family, identity)
    require(len(keys) == 1, f"{family} identity must own exactly one active key")
    ACTUAL_RATE_KEYS.add(keys[0])
    return keys[0]


def require_no_rate_key(family: str, identity: str, label: str) -> None:
    require(matching_rate_keys(family, identity) == [], f"{label} consumed {family} counter")


def counter_value(key: str) -> int:
    value = redis_get_value(key)
    require(value is not None, "expected Redis rate-limit counter")
    try:
        return int(value)
    except ValueError:
        fail("Redis rate-limit counter is not an integer")


def wait_for_fresh_window(config: FamilyConfig, family: str) -> None:
    minimum_remaining = min(config.window_seconds - 1, 15)
    remaining = config.window_seconds - (int(time.time()) % config.window_seconds)
    if remaining < minimum_remaining:
        wait = remaining + 1
        log_info(f"Waiting {wait}s for a fresh {family} fixed window")
        time.sleep(wait)


def upload_fixture(owner_token: str) -> str:
    path = create_temp_file(256)
    file_hash = md5_hash(path)
    filename = unique_name("share_rate_limit") + ".bin"

    try:
        init_response = fetch(
            "/api/file/upload/init",
            method="POST",
            headers={
                "Authorization": f"Bearer {owner_token}",
                "Content-Type": "application/json",
            },
            json_body={
                "filename": filename,
                "file_size": 256,
                "file_hash": file_hash,
                "parent_id": 0,
            },
        )
        init_body = require_success(init_response, "upload init")
        if init_body["data"]["instant_upload"]:
            file_id = init_body["data"].get("file_id")
            require(bool(file_id), "instant upload must return file_id")
            return str(file_id)

        upload_id = init_body["data"].get("upload_id")
        require(bool(upload_id), "upload init must return upload_id")
        with open(path, "rb") as fixture:
            content = fixture.read()

        chunk_response = fetch(
            f"/api/file/upload/chunk?upload_id={upload_id}&chunk_index=0&chunk_hash={file_hash}",
            method="POST",
            headers={
                "Authorization": f"Bearer {owner_token}",
                "Content-Type": "application/octet-stream",
            },
            data=content,
        )
        require_success(chunk_response, "upload chunk")

        complete_response = fetch(
            "/api/file/upload/complete",
            method="POST",
            headers={
                "Authorization": f"Bearer {owner_token}",
                "Content-Type": "application/json",
            },
            json_body={"upload_id": upload_id},
        )
        complete_body = require_success(complete_response, "upload complete")
        file_id = complete_body["data"]["file"].get("id")
        require(bool(file_id), "upload complete must return file id")
        return str(file_id)
    finally:
        os.unlink(path)


def create_share(owner_token: str, file_id: str, permission: str) -> str:
    response = fetch(
        "/api/share",
        method="POST",
        headers={
            "Authorization": f"Bearer {owner_token}",
            "Content-Type": "application/json",
        },
        json_body={
            "file_ids": [int(file_id)],
            "permission": permission,
            "expire_days": 7,
        },
    )
    body = require_success(response, f"create {permission} share")
    share_code = body["data"].get("share_id")
    require(bool(share_code), f"create {permission} share must return share_id")
    CREATED_SHARES.append(str(share_code))
    return str(share_code)


def issue_share_token(share_code: str) -> tuple[str, dict[str, Any]]:
    response = fetch(
        f"/api/share/access/{share_code}",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )
    body = require_success(response, "access active share")
    token = body["data"].get("share_token")
    require(isinstance(token, str) and bool(token), "share access must return token")
    RAW_CREDENTIALS.append(token)
    return token, decode_token(token)


def assert_standard_429(response: Any, expected_limit: int, family: str) -> dict[str, Any]:
    body = response_json(response, f"{family} limited response")
    required_headers = (
        "X-RateLimit-Limit",
        "X-RateLimit-Remaining",
        "X-RateLimit-Reset",
        "Retry-After",
    )
    headers = {name: response.headers.get(name, "") for name in required_headers}
    require(response.status_code == 429, f"{family} boundary must return HTTP 429")
    require(str(body.get("code")) == "10005", f"{family} boundary must return code 10005")
    require(body.get("message") == "Too many requests", f"{family} message mismatch")
    require(headers["X-RateLimit-Limit"] == str(expected_limit), f"{family} limit header mismatch")
    require(headers["X-RateLimit-Remaining"] == "0", f"{family} remaining header mismatch")
    require(all(headers.values()), f"{family} response is missing rate-limit headers")
    return {
        "http_status": response.status_code,
        "code": body["code"],
        "message": body["message"],
        "headers": headers,
    }


def test_auth_precedence(
    download_share: str,
    view_share: str,
    file_id: str,
    token_a: str,
    claims_a: dict[str, Any],
    token_b: str,
    claims_b: dict[str, Any],
    revoked_token: str,
    revoked_claims: dict[str, Any],
    view_token: str,
    view_claims: dict[str, Any],
) -> dict[str, Any]:
    identities = (
        ("browse", claims_a["jti"]),
        ("download", claims_a["jti"]),
        ("download", claims_b["jti"]),
        ("browse", revoked_claims["jti"]),
        ("download", view_claims["jti"]),
    )
    for family, identity in identities:
        clear_rate_identity(family, identity)

    cases: dict[str, dict[str, Any]] = {}

    missing = fetch(f"/api/share/browse/{download_share}")
    cases["missing"] = {"http_status": missing.status_code, "code": response_json(missing, "missing token").get("code")}
    require(cases["missing"] == {"http_status": 401, "code": 40106}, "missing token response changed")

    invalid_token = make_invalid_signature(token_a)
    RAW_CREDENTIALS.append(invalid_token)
    invalid = fetch(
        f"/api/share/browse/{download_share}",
        headers={"X-Share-Token": invalid_token},
    )
    cases["invalid"] = {"http_status": invalid.status_code, "code": response_json(invalid, "invalid token").get("code")}
    require(cases["invalid"] == {"http_status": 401, "code": 40107}, "invalid token response changed")
    require_no_rate_key("browse", claims_a["jti"], "invalid token")

    expired_token = make_expired_token(token_a)
    RAW_CREDENTIALS.append(expired_token)
    expired_claims = decode_token(expired_token)
    clear_rate_identity("browse", expired_claims["jti"])
    expired = fetch(
        f"/api/share/browse/{download_share}",
        headers={"X-Share-Token": expired_token},
    )
    cases["expired"] = {"http_status": expired.status_code, "code": response_json(expired, "expired token").get("code")}
    require(cases["expired"] == {"http_status": 401, "code": 40108}, "expired token response changed")
    require_no_rate_key("browse", expired_claims["jti"], "expired token")

    token_hash = hashlib.sha256(revoked_token.encode()).hexdigest()
    blacklist_key = f"share_token_blacklist:{token_hash}"
    redis_set_value(blacklist_key, "1", 3600)
    BLACKLIST_KEYS.add(blacklist_key)
    revoked = fetch(
        f"/api/share/browse/{download_share}",
        headers={"X-Share-Token": revoked_token},
    )
    cases["revoked"] = {"http_status": revoked.status_code, "code": response_json(revoked, "revoked token").get("code")}
    require(cases["revoked"] == {"http_status": 401, "code": 40111}, "revoked token response changed")
    require_no_rate_key("browse", revoked_claims["jti"], "revoked token")

    denied = fetch(
        f"/api/share/download/{view_share}/{file_id}/info",
        headers={"X-Share-Token": view_token},
    )
    cases["scope_denied"] = {"http_status": denied.status_code, "code": response_json(denied, "scope denied").get("code")}
    require(cases["scope_denied"] == {"http_status": 403, "code": 60004}, "scope denial response changed")
    require_no_rate_key("download", view_claims["jti"], "scope-denied token")

    owner_missing = fetch(
        f"/api/share/save/{download_share}",
        method="POST",
        headers={
            "X-Share-Token": token_b,
            "Content-Type": "application/json",
        },
        json_body={
            "file_ids": [int(file_id)],
            "folder_ids": [],
            "target_folder_id": 0,
        },
    )
    cases["owner_missing"] = {"http_status": owner_missing.status_code, "code": response_json(owner_missing, "owner missing").get("code")}
    require(cases["owner_missing"] == {"http_status": 401, "code": 40106}, "owner auth response changed")
    require_no_rate_key("download", claims_b["jti"], "owner-missing save")

    require_no_rate_key("browse", claims_a["jti"], "authentication rejection")
    require_no_rate_key("download", claims_a["jti"], "authentication rejection")
    log_pass("Authentication and scope failures preserve responses without counter consumption")
    return {"cases": cases, "authenticated_counters_created": 0}


def test_access_boundary(share_code: str, config: FamilyConfig) -> tuple[dict[str, Any], str]:
    wait_for_fresh_window(config, "access")
    clear_rate_identity("access", CLIENT_IP)

    key = ""
    for index in range(config.limit):
        issue_share_token(share_code)
        if index == 0:
            key = require_single_rate_key("access", CLIENT_IP)

    require(counter_value(key) == config.limit, "access allowed requests must match configured limit")
    limited = fetch(
        f"/api/share/access/{share_code}",
        method="POST",
        headers={"Content-Type": "application/json"},
        json_body={},
    )
    response = assert_standard_429(limited, config.limit, "access")
    require(counter_value(key) == config.limit + 1, "access boundary counter must include limited request")
    ttl = redis_ttl(key)
    require(0 < ttl <= config.window_seconds, "access counter TTL must match fixed window")
    log_pass("Access boundary honors configured limit and standard 429 contract")
    return {
        "configured_limit": config.limit,
        "configured_window_seconds": config.window_seconds,
        "allowed_requests": config.limit,
        "limited_request": config.limit + 1,
        "counter_after_limit": config.limit + 1,
        "ttl_seconds": ttl,
        "response": response,
    }, key


def test_browse_boundary(
    share_code: str,
    token: str,
    jti: str,
    config: FamilyConfig,
) -> tuple[dict[str, Any], str]:
    wait_for_fresh_window(config, "browse")
    clear_rate_identity("browse", jti)

    key = ""
    for index in range(config.limit):
        response = fetch(
            f"/api/share/browse/{share_code}",
            headers={"X-Share-Token": token},
        )
        require_success(response, f"browse request {index + 1}")
        if index == 0:
            key = require_single_rate_key("browse", jti)

    require(counter_value(key) == config.limit, "browse allowed requests must match configured limit")
    limited = fetch(
        f"/api/share/browse/{share_code}",
        headers={"X-Share-Token": token},
    )
    response = assert_standard_429(limited, config.limit, "browse")
    require(counter_value(key) == config.limit + 1, "browse counter must include limited request")
    log_pass("Browse boundary is enforced per verified JTI")
    return {
        "configured_limit": config.limit,
        "configured_window_seconds": config.window_seconds,
        "allowed_requests": config.limit,
        "limited_request": config.limit + 1,
        "counter_after_limit": config.limit + 1,
        "response": response,
    }, key


def test_isolation(
    share_code: str,
    file_id: str,
    token_b: str,
    jti_b: str,
    token_a_browse_key: str,
    access_key: str,
) -> tuple[dict[str, Any], str, str]:
    clear_rate_identity("browse", jti_b)
    clear_rate_identity("download", jti_b)
    first_jti_browse_before = counter_value(token_a_browse_key)
    access_before = counter_value(access_key)

    browse = fetch(
        f"/api/share/browse/{share_code}",
        headers={"X-Share-Token": token_b},
    )
    require_success(browse, "isolated token browse")
    browse_key = require_single_rate_key("browse", jti_b)

    download = fetch(
        f"/api/share/download/{share_code}/{file_id}/info",
        headers={"X-Share-Token": token_b},
    )
    require_success(download, "isolated token download info")
    download_key = require_single_rate_key("download", jti_b)

    require(counter_value(browse_key) == 1, "second JTI browse counter must start at one")
    require(counter_value(download_key) == 1, "second JTI download counter must start at one")
    require(
        counter_value(token_a_browse_key) == first_jti_browse_before,
        "second JTI requests must not consume first JTI browse counter",
    )
    require(
        counter_value(access_key) == access_before,
        "operation requests must not consume access counter",
    )
    log_pass("Operation families and separately issued JTIs use isolated counters")
    return {
        "second_jti_browse_count": 1,
        "second_jti_download_count": 1,
        "first_jti_browse_before_after": [first_jti_browse_before, first_jti_browse_before],
        "access_counter_before_after": [access_before, access_before],
    }, browse_key, download_key


def download_request(
    label: str,
    owner_token: str,
    share_code: str,
    file_id: str,
    share_token: str,
) -> Any:
    if label == "info":
        response = fetch(
            f"/api/share/download/{share_code}/{file_id}/info",
            headers={"X-Share-Token": share_token},
        )
        require_success(response, "download metadata")
        return response
    if label == "content":
        response = fetch(
            f"/api/share/download/{share_code}/{file_id}",
            headers={"X-Share-Token": share_token},
        )
        require(response.status_code == 200, "initial content download must return HTTP 200")
        return response
    if label == "range":
        response = fetch(
            f"/api/share/download/{share_code}/{file_id}",
            headers={"X-Share-Token": share_token, "Range": "bytes=1-"},
        )
        require(response.status_code == 206, "Range download must return HTTP 206")
        return response
    if label == "retry":
        response = fetch(
            f"/api/share/download/{share_code}/{file_id}",
            headers={"X-Share-Token": share_token},
        )
        require(response.status_code == 200, "retry content download must return HTTP 200")
        return response
    if label == "save":
        response = fetch(
            f"/api/share/save/{share_code}",
            method="POST",
            headers={
                "Authorization": f"Bearer {owner_token}",
                "X-Share-Token": share_token,
                "Content-Type": "application/json",
            },
            json_body={
                "file_ids": [int(file_id)],
                "folder_ids": [],
                "target_folder_id": 0,
            },
        )
        require_success(response, "save shared file")
        return response
    fail(f"unknown download route label: {label}")


def test_download_boundary(
    owner_token: str,
    share_code: str,
    file_id: str,
    token: str,
    jti: str,
    config: FamilyConfig,
    browse_key: str,
    access_key: str,
    isolated_browse_key: str,
    isolated_download_key: str,
) -> tuple[dict[str, Any], dict[str, Any], str]:
    require(config.limit >= 5, "download integration limit must allow all five route probes")
    wait_for_fresh_window(config, "download")
    clear_rate_identity("download", jti)
    browse_before = counter_value(browse_key)
    access_before = counter_value(access_key)
    isolated_browse_before = counter_value(isolated_browse_key)
    isolated_download_before = counter_value(isolated_download_key)

    route_trace: list[dict[str, Any]] = []
    sequence = ["info", "content", "range", "retry", "save"]
    key = ""
    for label in sequence:
        download_request(label, owner_token, share_code, file_id, token)
        if not key:
            key = require_single_rate_key("download", jti)
        route_trace.append({"route": label, "counter": counter_value(key)})

    while len(route_trace) < config.limit:
        download_request("info", owner_token, share_code, file_id, token)
        route_trace.append({"route": "info-fill", "counter": counter_value(key)})

    require(
        [entry["counter"] for entry in route_trace[:5]] == [1, 2, 3, 4, 5],
        "metadata, content, Range, retry, and save must each increment once",
    )
    require(counter_value(key) == config.limit, "download shared bucket must reach configured limit")

    limited = fetch(
        f"/api/share/download/{share_code}/{file_id}/info",
        headers={"X-Share-Token": token},
    )
    response = assert_standard_429(limited, config.limit, "download")
    require(counter_value(key) == config.limit + 1, "download counter must include limited request")
    require(counter_value(browse_key) == browse_before, "download requests must not consume browse bucket")
    require(
        counter_value(isolated_browse_key) == isolated_browse_before,
        "download requests must not consume second JTI browse bucket",
    )
    require(
        counter_value(isolated_download_key) == isolated_download_before,
        "download requests must not consume second JTI download bucket",
    )
    require(counter_value(access_key) == access_before, "download requests must not consume access bucket")
    log_pass("Download metadata, content, Range, retry, and save share one JTI bucket")

    download_evidence = {
        "configured_limit": config.limit,
        "configured_window_seconds": config.window_seconds,
        "route_trace": route_trace,
        "limited_request": config.limit + 1,
        "counter_after_limit": config.limit + 1,
        "response": response,
    }
    range_evidence = {
        "initial_counter": route_trace[1]["counter"],
        "range_counter": route_trace[2]["counter"],
        "retry_counter": route_trace[3]["counter"],
        "increments_per_http_request": [1, 1, 1],
    }
    return download_evidence, range_evidence, key


def run_focused_evidence() -> dict[str, Any]:
    test_binary = Path(
        os.environ.get("TEST_BIN", REPO_ROOT / "build/linux-debug-clang/test/disk-test")
    )
    require(test_binary.is_file(), f"focused test binary not found: {test_binary}")
    test_filter = (
        "ConfigMgrShareRateLimitTest.*:"
        "RedisServiceRuntimeTest.IncrWithExpireSetsTtlOnlyOnFirstIncrement:"
        "ShareRateLimitFilterTest.RedisFailuresFailOpenForBothFilters"
    )
    result = subprocess.run(
        [str(test_binary), f"--gtest_filter={test_filter}", "--gtest_color=no"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    output = result.stdout + result.stderr
    ok_lines = [line.strip() for line in output.splitlines() if line.startswith("[       OK ]")]
    require(result.returncode == 0, "focused configuration/fail-open tests must pass")
    require(len(ok_lines) == 6, "focused evidence must execute all six selected tests")
    require(
        "counter failed: operation=access" in output
        and "counter failed: operation=download" in output,
        "fail-open test output must contain non-secret operation diagnostics",
    )
    log_pass("Focused configuration, fixed-window, and fail-open tests passed")
    return {
        "return_code": result.returncode,
        "passed_tests": [line.split("]", 1)[1].strip().split(" (", 1)[0] for line in ok_lines],
        "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
        "diagnostic_operations": ["access", "download"],
        "filter_continued": True,
    }


def collect_rate_keys() -> list[str]:
    keys: set[str] = set()
    for family, identity in RATE_IDENTITIES:
        keys.update(redis_keys(rate_pattern(family, identity)))
    keys.update(key for key in BLACKLIST_KEYS if redis_get_value(key) is not None)
    return sorted(keys)


def collect_audit_text() -> str:
    rows: list[dict[str, Any]] = []
    for share_code in CREATED_SHARES:
        rows.extend(
            query_all(
                "SELECT action, target_name, details, ip_address, user_agent "
                "FROM operation_logs WHERE target_name = %s ORDER BY id",
                (share_code,),
            )
        )
    return json.dumps(rows, default=str, sort_keys=True)


def cleanup_owned_state(owner_token: str) -> None:
    global SHARES_CANCELLED

    for family, identity in RATE_IDENTITIES:
        try:
            redis_delete_pattern(rate_pattern(family, identity))
        except Exception:
            pass
    for key in BLACKLIST_KEYS:
        try:
            redis_delete_key(key)
        except Exception:
            pass

    if owner_token and CREATED_SHARES and not SHARES_CANCELLED:
        try:
            fetch(
                "/api/share",
                method="DELETE",
                headers={
                    "Authorization": f"Bearer {owner_token}",
                    "Content-Type": "application/json",
                },
                json_body={"share_ids": CREATED_SHARES},
            )
            SHARES_CANCELLED = True
        except Exception:
            pass


def assert_secret_exclusion(
    evidence: dict[str, Any],
    redis_key_text: str,
    audit_text: str,
) -> None:
    time.sleep(0.1)
    require(SERVER_LOG_PATH.is_file(), "share rate-limit test must own an inspectable server log")
    log_text = SERVER_LOG_PATH.read_text(encoding="utf-8", errors="replace")
    evidence_text = json.dumps(evidence, indent=2, sort_keys=True)

    for raw_value in RAW_CREDENTIALS:
        require(raw_value not in redis_key_text, "Redis keys contain raw credential material")
        require(raw_value not in log_text, "application log contains raw credential material")
        require(raw_value not in audit_text, "audit rows contain raw credential material")
        require(raw_value not in evidence_text, "evidence contains raw credential material")

    for jti in KNOWN_JTIS:
        require(jti not in log_text, "application log contains share-token JTI")
        require(jti not in audit_text, "audit rows contain share-token JTI")
        require(jti not in evidence_text, "saved evidence contains share-token JTI")

    for key in ACTUAL_RATE_KEYS:
        parts = key.split(":")
        require(len(parts) == 4, "share rate-limit key has unexpected shape")
        if parts[1] in ("share_browse", "share_download"):
            require(parts[2] in KNOWN_JTIS, "authenticated rate key does not use verified JTI")

    save_evidence(EVIDENCE_NAME, evidence_text + "\n")
    saved_text = (EVIDENCE_ROOT / EVIDENCE_NAME).read_text(encoding="utf-8")
    for raw_value in RAW_CREDENTIALS:
        require(raw_value not in saved_text, "saved evidence file contains raw credential material")
    for jti in KNOWN_JTIS:
        require(jti not in saved_text, "saved evidence file contains share-token JTI")
    log_pass("Redis keys, logs, audits, and saved evidence exclude credential material")


def main() -> None:
    global OWNER_TOKEN

    print("==========================================")
    print("Share Rate Limit Integration Test")
    print("==========================================")
    print()

    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    SERVER_LOG_PATH.unlink(missing_ok=True)
    ensure_server()

    evidence: dict[str, Any] = {
        "schema_version": 1,
        "evidence": {},
    }

    try:
        config = load_rate_config()
        log_info(f"Runtime share rate-limit config: {asdict(config)}")

        owner_token = do_login(TEST_USER, TEST_PASS)
        require(bool(owner_token), "owner login must succeed")
        OWNER_TOKEN = str(owner_token)
        RAW_CREDENTIALS.append(OWNER_TOKEN)

        file_id = upload_fixture(OWNER_TOKEN)
        download_share = create_share(OWNER_TOKEN, file_id, "download")
        view_share = create_share(OWNER_TOKEN, file_id, "view")

        token_a, claims_a = issue_share_token(download_share)
        token_b, claims_b = issue_share_token(download_share)
        revoked_token, revoked_claims = issue_share_token(download_share)
        view_token, view_claims = issue_share_token(view_share)
        require(claims_a["jti"] != claims_b["jti"], "separately issued tokens must have distinct JTIs")

        evidence["evidence"]["SHARE-RATE-AUTH-001"] = test_auth_precedence(
            download_share,
            view_share,
            file_id,
            token_a,
            claims_a,
            token_b,
            claims_b,
            revoked_token,
            revoked_claims,
            view_token,
            view_claims,
        )

        access_evidence, access_key = test_access_boundary(download_share, config.access)
        evidence["evidence"]["SHARE-RATE-ACCESS-001"] = access_evidence

        browse_evidence, browse_key = test_browse_boundary(
            download_share,
            token_a,
            claims_a["jti"],
            config.browse,
        )
        evidence["evidence"]["SHARE-RATE-BROWSE-001"] = browse_evidence

        isolation_evidence, isolated_browse_key, isolated_download_key = test_isolation(
            download_share,
            file_id,
            token_b,
            claims_b["jti"],
            browse_key,
            access_key,
        )

        download_evidence, range_evidence, _ = test_download_boundary(
            OWNER_TOKEN,
            download_share,
            file_id,
            token_a,
            claims_a["jti"],
            config.download,
            browse_key,
            access_key,
            isolated_browse_key,
            isolated_download_key,
        )
        evidence["evidence"]["SHARE-RATE-DOWNLOAD-001"] = download_evidence
        evidence["evidence"]["SHARE-RATE-RANGE-001"] = range_evidence
        evidence["evidence"]["SHARE-RATE-ISOLATION-001"] = isolation_evidence

        focused = run_focused_evidence()
        evidence["evidence"]["SHARE-RATE-CONFIG-001"] = {
            "runtime_values": asdict(config),
            "valid_missing_zero_negative_cases_passed": True,
            "focused_tests": focused["passed_tests"][:4],
        }
        evidence["evidence"]["SHARE-RATE-REDIS-001"] = {
            "fail_open_test": focused["passed_tests"][-1],
            "fixed_window_test": focused["passed_tests"][4],
            "diagnostic_operations": focused["diagnostic_operations"],
            "filter_continued": focused["filter_continued"],
            "output_sha256": focused["output_sha256"],
        }
        evidence["evidence"]["SHARE-RATE-RESPONSE-001"] = {
            "access": access_evidence["response"],
            "browse": browse_evidence["response"],
            "download": download_evidence["response"],
        }

        redis_key_text = "\n".join(collect_rate_keys())
        audit_text = collect_audit_text()
        cleanup_owned_state(OWNER_TOKEN)
        evidence["evidence"]["SHARE-RATE-SECRETS-001"] = {
            "redis_keys_checked": True,
            "application_log_checked": True,
            "audit_rows_checked": True,
            "saved_evidence_checked": True,
            "credential_material_absent": True,
            "authenticated_key_identity": "verified-jti-only",
        }
        assert_secret_exclusion(evidence, redis_key_text, audit_text)

        expected_ids = {
            "SHARE-RATE-ACCESS-001",
            "SHARE-RATE-BROWSE-001",
            "SHARE-RATE-DOWNLOAD-001",
            "SHARE-RATE-RANGE-001",
            "SHARE-RATE-ISOLATION-001",
            "SHARE-RATE-AUTH-001",
            "SHARE-RATE-CONFIG-001",
            "SHARE-RATE-RESPONSE-001",
            "SHARE-RATE-REDIS-001",
            "SHARE-RATE-SECRETS-001",
        }
        pass_if(set(evidence["evidence"]) == expected_ids, "All ten share rate-limit evidence IDs were produced")
    except TestFailure:
        pass
    except Exception as error:
        log_fail(f"Unexpected share rate-limit test failure: {error}")
    finally:
        cleanup_owned_state(OWNER_TOKEN)

    print()
    print_summary()


if __name__ == "__main__":
    main()

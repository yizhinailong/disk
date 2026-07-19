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

"""Cross-instance integration and failure-recovery checks for the Compose topology."""

from __future__ import annotations

import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import time
import uuid
from pathlib import Path
from typing import Any, Callable


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def response_json(response: Any, label: str) -> dict[str, Any]:
    try:
        payload = response.json()
    except ValueError as error:
        raise AssertionError(f"{label}: response is not JSON (HTTP {response.status_code})") from error
    require(isinstance(payload, dict), f"{label}: response envelope is not an object")
    require("code" in payload, f"{label}: response envelope has no code")
    return payload


def wait_until(
    predicate: Callable[[], Any],
    timeout: float,
    label: str,
    interval: float = 0.25,
) -> Any:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except Exception as error:  # noqa: BLE001 - retain the last transient failure
            last_error = error
        time.sleep(interval)
    suffix = f": {last_error}" if last_error is not None else ""
    raise AssertionError(f"timed out waiting for {label}{suffix}")


def main() -> int:
    if os.environ.get("DISK_DISTRIBUTED_INTEGRATION") != "1":
        print("SKIP: DISK_DISTRIBUTED_INTEGRATION is not 1; skipping distributed flow")
        return 0
    if shutil.which("docker") is None:
        print("FAIL: Docker CLI is required when DISK_DISTRIBUTED_INTEGRATION=1")
        return 1

    import boto3
    import httpx
    import psycopg
    from botocore.config import Config
    from botocore.exceptions import ClientError
    from dotenv import dotenv_values
    from psycopg.rows import dict_row

    root = Path(__file__).resolve().parents[2]
    compose_file = root / "docker-compose.distributed.yml"
    env_file = Path(os.environ.get("DISK_DISTRIBUTED_ENV_FILE", root / ".env.distributed"))
    if not env_file.is_absolute():
        env_file = (root / env_file).resolve()
    require(env_file.is_file(), f"distributed environment file does not exist: {env_file}")

    values = {key: value for key, value in dotenv_values(env_file).items() if value is not None}
    for key in (
        "DISK_JWT_SECRET",
        "DISK_DATABASE_PASSWORD",
        "DISK_REDIS_PASSWORD",
        "DISK_MINIO_ROOT_PASSWORD",
    ):
        value = values.get(key, "")
        require(value and not value.startswith("replace-"), f"{key} still contains a placeholder")

    api_a_url = f"http://127.0.0.1:{values.get('DISK_API_A_PORT', '18081')}"
    api_b_url = f"http://127.0.0.1:{values.get('DISK_API_B_PORT', '18082')}"
    load_balancer_url = f"http://127.0.0.1:{values.get('DISK_LB_PORT', '18080')}"
    postgres_port = int(values.get("DISK_POSTGRES_PORT", "15432"))
    minio_url = f"http://127.0.0.1:{values.get('DISK_MINIO_PORT', '19000')}"
    compose_base = [
        "docker",
        "compose",
        "--env-file",
        str(env_file),
        "-f",
        str(compose_file),
    ]

    def compose(*arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [*compose_base, *arguments],
            cwd=root,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

    compose("ps")

    db_parameters = {
        "host": "127.0.0.1",
        "port": postgres_port,
        "dbname": "disk",
        "user": "disk",
        "password": values["DISK_DATABASE_PASSWORD"],
        "row_factory": dict_row,
    }

    def query_one(sql: str, parameters: tuple[Any, ...] = ()) -> dict[str, Any] | None:
        with psycopg.connect(**db_parameters) as connection:
            with connection.cursor() as cursor:
                cursor.execute(sql, parameters)
                row = cursor.fetchone()
                return dict(row) if row is not None else None

    def execute(sql: str, parameters: tuple[Any, ...] = ()) -> int:
        with psycopg.connect(**db_parameters) as connection:
            with connection.cursor() as cursor:
                cursor.execute(sql, parameters)
                count = cursor.rowcount
            connection.commit()
            return count

    s3 = boto3.client(
        "s3",
        endpoint_url=minio_url,
        aws_access_key_id=values.get("DISK_MINIO_ROOT_USER", "disk-local"),
        aws_secret_access_key=values["DISK_MINIO_ROOT_PASSWORD"],
        region_name="us-east-1",
        config=Config(s3={"addressing_style": "path"}),
    )

    def object_exists(key: str) -> bool:
        try:
            s3.head_object(Bucket="disk", Key=key)
            return True
        except ClientError as error:
            status = error.response.get("ResponseMetadata", {}).get("HTTPStatusCode")
            if status == 404:
                return False
            raise

    def wait_ready(base_url: str, role: str, instance_id: str | None = None) -> dict[str, Any]:
        def probe() -> dict[str, Any] | None:
            with httpx.Client(base_url=base_url, timeout=5) as client:
                response = client.get("/api/health/ready")
            if response.status_code != 200:
                return None
            data = response_json(response, "readiness").get("data", {})
            if data.get("overall_status") != "healthy" or data.get("role") != role:
                return None
            if instance_id is not None and data.get("instance_id") != instance_id:
                return None
            return data

        return wait_until(probe, 60, f"{instance_id or role} readiness")

    run_id = uuid.uuid4().hex[:12]
    username = f"dist_{run_id}"
    email = f"dist_{run_id}@test.example.com"
    password = "DistPass123"
    filename = f"distributed_{run_id}.bin"
    first_chunk = (f"disk-distributed-{run_id}:".encode() + b"A" * 5_242_880)[:5_242_880]
    second_chunk = b"cross-instance-tail:" + run_id.encode()
    content = first_chunk + second_chunk
    file_hash = hashlib.md5(content).hexdigest()
    file_sha256 = hashlib.sha256(content).hexdigest()
    object_key = f"objects/sha256/{file_sha256[:2]}/{file_sha256}.bin"
    access_token = ""
    user_id = 0
    file_id = 0
    worker_a_stopped = False
    api_a_stopped = False
    redis_stopped = False
    evidence: dict[str, Any] = {"run_id": run_id, "checks": []}

    def passed(name: str) -> None:
        evidence["checks"].append(name)
        print(f"PASS: {name}")

    try:
        wait_ready(api_a_url, "api", "disk-api-a")
        wait_ready(api_b_url, "api", "disk-api-b")
        wait_ready(load_balancer_url, "api")
        passed("two API instances and the load balancer are ready")

        with httpx.Client(timeout=120) as http:
            register = http.post(
                f"{api_a_url}/api/auth/register",
                json={"username": username, "email": email, "password": password},
            )
            register_payload = response_json(register, "register on API A")
            require(register.status_code == 200 and str(register_payload["code"]) == "0", "register on API A failed")

            login = http.post(
                f"{api_a_url}/api/auth/login",
                json={"account": username, "password": password},
            )
            login_payload = response_json(login, "login on API A")
            require(login.status_code == 200 and str(login_payload["code"]) == "0", "login on API A failed")
            access_token = str(login_payload["data"]["access_token"])
            refresh_token = str(login_payload["data"]["refresh_token"])
            auth = {"Authorization": f"Bearer {access_token}"}

            profile = http.get(f"{api_b_url}/api/user/profile", headers=auth)
            require(str(response_json(profile, "profile on API B")["code"]) == "0", "API B rejected API A token")
            passed("login and access cross API instances")

            def rotate(base_url: str) -> tuple[int, str]:
                response = httpx.post(
                    f"{base_url}/api/auth/refresh",
                    json={"refresh_token": refresh_token},
                    timeout=30,
                )
                return response.status_code, str(response_json(response, "concurrent refresh")["code"])

            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                rotations = list(pool.map(rotate, (api_a_url, api_b_url)))
            require(sum(code == "0" for _, code in rotations) == 1, "refresh CAS did not select exactly one winner")
            replay = http.post(f"{api_b_url}/api/auth/refresh", json={"refresh_token": refresh_token})
            require(str(response_json(replay, "refresh replay")["code"]) != "0", "old refresh token was replayed")
            passed("refresh token CAS is shared across API instances")

            empty_list = http.get(
                f"{api_a_url}/api/file/list",
                params={"parent_id": 0, "page_size": 100, "sort_by": "created_at", "sort_order": "desc", "type": "file"},
                headers=auth,
            )
            require(str(response_json(empty_list, "initial file list")["code"]) == "0", "initial file list failed")

            init = http.post(
                f"{api_a_url}/api/file/upload/init",
                headers=auth,
                json={"filename": filename, "file_size": len(content), "file_hash": file_hash, "parent_id": 0},
            )
            init_payload = response_json(init, "upload init on API A")
            require(str(init_payload["code"]) == "0", "upload init failed")
            require(not init_payload["data"].get("instant_upload", False), "upload unexpectedly deduplicated")
            upload_id = str(init_payload["data"]["upload_id"])

            chunk_zero_hash = hashlib.md5(first_chunk).hexdigest()

            def upload_same_chunk(base_url: str) -> tuple[int, str]:
                response = httpx.post(
                    f"{base_url}/api/file/upload/chunk",
                    params={"upload_id": upload_id, "chunk_index": 0, "chunk_hash": chunk_zero_hash},
                    headers={**auth, "Content-Type": "application/octet-stream"},
                    content=first_chunk,
                    timeout=120,
                )
                payload = response_json(response, "concurrent chunk")
                return response.status_code, str(payload["code"])

            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                chunk_results = list(pool.map(upload_same_chunk, (api_a_url, api_b_url)))
            require(all(code == "0" for _, code in chunk_results), "same chunk was not idempotent across APIs")

            chunk_one = http.post(
                f"{api_b_url}/api/file/upload/chunk",
                params={
                    "upload_id": upload_id,
                    "chunk_index": 1,
                    "chunk_hash": hashlib.md5(second_chunk).hexdigest(),
                },
                headers={**auth, "Content-Type": "application/octet-stream"},
                content=second_chunk,
            )
            require(str(response_json(chunk_one, "chunk one on API B")["code"]) == "0", "API B chunk failed")
            passed("init and chunks can alternate between API instances")

            def complete(base_url: str) -> tuple[str, int | None]:
                response = httpx.post(
                    f"{base_url}/api/file/upload/complete",
                    headers=auth,
                    json={"upload_id": upload_id},
                    timeout=180,
                )
                payload = response_json(response, "concurrent complete")
                result_file = payload.get("data", {}).get("file", {})
                return str(payload["code"]), int(result_file["id"]) if result_file.get("id") else None

            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                completions = list(pool.map(complete, (api_a_url, api_b_url)))
            successful_ids = {candidate for code, candidate in completions if code == "0" and candidate is not None}
            for base_url, (code, _) in zip((api_a_url, api_b_url), completions, strict=True):
                if code != "0":
                    retry_code, retry_id = complete(base_url)
                    require(retry_code == "0" and retry_id is not None, "complete retry did not converge")
                    successful_ids.add(retry_id)
            require(len(successful_ids) == 1, "concurrent complete returned different file records")
            file_id = successful_ids.pop()

            user_row = query_one("SELECT id FROM users WHERE username = %s", (username,))
            require(user_row is not None, "test user is missing from PostgreSQL")
            user_id = int(user_row["id"])
            file_count = query_one(
                "SELECT COUNT(*) AS count FROM files WHERE user_id = %s AND name = %s",
                (user_id, filename),
            )
            require(file_count is not None and int(file_count["count"]) == 1, "concurrent complete created duplicate files")
            wait_until(lambda: object_exists(object_key), 30, "final S3 object")
            passed("concurrent complete converges to one file and one final object")

            refreshed_list = http.get(
                f"{api_a_url}/api/file/list",
                params={"parent_id": 0, "page_size": 100, "sort_by": "created_at", "sort_order": "desc", "type": "file"},
                headers=auth,
            )
            refreshed_payload = response_json(refreshed_list, "cross-instance file list")
            names = [item.get("name") for item in refreshed_payload.get("data", {}).get("items", [])]
            require(filename in names, "API A retained its stale pre-upload file-list cache")
            passed("file-list generation invalidation crosses API instances")

            create_share = http.post(
                f"{api_a_url}/api/share",
                headers=auth,
                json={"file_ids": [file_id], "permission": "download", "expire_days": 7},
            )
            share_payload = response_json(create_share, "share create on API A")
            require(str(share_payload["code"]) == "0", "share creation failed")
            share_id = str(share_payload["data"]["share_id"])
            access_share = http.post(f"{api_b_url}/api/share/access/{share_id}", json={})
            access_payload = response_json(access_share, "share access on API B")
            require(str(access_payload["code"]) == "0", "share access on API B failed")
            share_token = str(access_payload["data"]["share_token"])
            cancel = http.request(
                "DELETE",
                f"{api_a_url}/api/share",
                headers=auth,
                json={"share_ids": [share_id]},
            )
            require(str(response_json(cancel, "share cancel on API A")["code"]) == "0", "share cancel failed")
            old_share = http.get(
                f"{api_b_url}/api/share/browse/{share_id}",
                headers={"X-Share-Token": share_token},
            )
            require(str(response_json(old_share, "old share token on API B")["code"]) != "0", "API B accepted cancelled share token")
            passed("share cancellation is immediately visible on another API")

            compose("stop", "worker-a")
            worker_a_stopped = True
            takeover_upload_id = str(uuid.uuid4())
            takeover_key = f"dist-worker-takeover:{run_id}"
            execute(
                "INSERT INTO storage_jobs "
                "(job_type, aggregate_id, dedupe_key, payload, status, attempts, max_attempts, available_at, locked_by, locked_until) "
                "VALUES ('staging_cleanup', %s, %s, %s::jsonb, 1, 1, 8, NOW(), 'disk-worker-a', NOW() + INTERVAL '3 seconds')",
                (
                    takeover_upload_id,
                    takeover_key,
                    json.dumps({"upload_id": takeover_upload_id, "backend": "s3", "prefix": f"staging/{takeover_upload_id}"}),
                ),
            )

            def takeover_completed() -> bool:
                row = query_one("SELECT status, attempts FROM storage_jobs WHERE dedupe_key = %s", (takeover_key,))
                return row is not None and int(row["status"]) == 3 and int(row["attempts"]) >= 2

            wait_until(takeover_completed, 45, "worker lease takeover", interval=0.5)
            execute("DELETE FROM storage_jobs WHERE dedupe_key = %s", (takeover_key,))
            compose("start", "worker-a")
            worker_a_stopped = False
            passed("worker B takes over an expired worker A lease")

            compose("stop", "redis")
            redis_stopped = True

            def redis_failure_closed() -> bool:
                try:
                    response = http.get(f"{api_b_url}/api/user/profile", headers=auth, timeout=10)
                    return str(response_json(response, "Redis failure auth check")["code"]) == "70002"
                except httpx.HTTPError:
                    return False

            wait_until(redis_failure_closed, 20, "Redis fail-closed authentication", interval=0.5)
            compose("start", "redis")
            redis_stopped = False
            wait_ready(api_a_url, "api", "disk-api-a")
            wait_ready(api_b_url, "api", "disk-api-b")
            recovered_profile = http.get(f"{api_b_url}/api/user/profile", headers=auth)
            require(str(response_json(recovered_profile, "profile after Redis recovery")["code"]) == "0", "authentication did not recover with Redis")
            passed("Redis outage fails closed and recovery reconnects")

            compose("stop", "api-a")
            api_a_stopped = True
            wait_ready(api_b_url, "api", "disk-api-b")
            for _ in range(6):
                response = http.get(f"{load_balancer_url}/api/user/profile", headers=auth)
                require(str(response_json(response, "load balancer failover")["code"]) == "0", "load balancer did not route around API A")
            compose("start", "api-a")
            api_a_stopped = False
            wait_ready(api_a_url, "api", "disk-api-a")
            passed("load balancer continues serving after one API stops")

            soft_delete = http.request(
                "DELETE",
                f"{api_b_url}/api/file",
                headers=auth,
                json={"file_ids": [file_id], "folder_ids": []},
            )
            require(str(response_json(soft_delete, "soft delete")["code"]) == "0", "soft delete failed")

            def trash_id() -> int | None:
                row = query_one(
                    "SELECT id FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s",
                    (user_id, file_id),
                )
                return int(row["id"]) if row is not None else None

            deleted_trash_id = wait_until(trash_id, 10, "trash row")
            permanent = http.request(
                "DELETE",
                f"{api_b_url}/api/trash",
                headers=auth,
                json={"trash_ids": [deleted_trash_id]},
            )
            require(str(response_json(permanent, "permanent delete")["code"]) == "0", "permanent delete failed")
            wait_until(lambda: not object_exists(object_key), 45, "blob GC", interval=0.5)
            file_id = 0

            logout = http.post(f"{api_a_url}/api/auth/logout", headers=auth)
            require(str(response_json(logout, "logout on API A")["code"]) == "0", "logout on API A failed")
            revoked = http.get(f"{api_b_url}/api/user/profile", headers=auth)
            revoked_payload = response_json(revoked, "revoked token on API B")
            require(revoked.status_code == 401 and str(revoked_payload["code"]) == "40111", "API B did not immediately reject API A logout")
            passed("access-token logout is immediately visible on another API")

        execute("DELETE FROM users WHERE id = %s", (user_id,))
        user_id = 0
        evidence["status"] = "passed"
        evidence_path = root / ".sisyphus/evidence/distributed-flow-summary.json"
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
        print(f"PASS: distributed flow completed ({len(evidence['checks'])} checks)")
        return 0
    except Exception as error:  # noqa: BLE001 - print a credential-free summary
        evidence["status"] = "failed"
        evidence["error"] = str(error)
        print(f"FAIL: {error}")
        return 1
    finally:
        for stopped, service in (
            (redis_stopped, "redis"),
            (api_a_stopped, "api-a"),
            (worker_a_stopped, "worker-a"),
        ):
            if stopped:
                try:
                    compose("start", service)
                except Exception:
                    pass
        if file_id and access_token:
            # Preserve failed state for diagnosis; the test environment is isolated and can be reset with Compose.
            pass


if __name__ == "__main__":
    raise SystemExit(main())

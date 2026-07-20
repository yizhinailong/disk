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
import threading
import time
import uuid
from pathlib import Path
from typing import Any, Callable, Protocol


class TopologyControl(Protocol):
    runner_name: str

    def run(self, *arguments: str) -> subprocess.CompletedProcess[str]: ...

    def fingerprint(self, service: str) -> str: ...


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


def main(
    topology: TopologyControl | None = None,
    gate_name: str = "DISK_DISTRIBUTED_INTEGRATION",
) -> int:
    if os.environ.get(gate_name) != "1":
        print(f"SKIP: {gate_name} is not 1; skipping distributed flow")
        return 0
    if topology is None and shutil.which("docker") is None:
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
        "DISK_S3_ACCESS_KEY",
        "DISK_S3_SECRET_KEY",
    ):
        value = values.get(key, "")
        require(value and not value.startswith("replace-"), f"{key} still contains a placeholder")

    api_a_url = f"http://127.0.0.1:{values.get('DISK_API_A_PORT', '18081')}"
    api_b_url = f"http://127.0.0.1:{values.get('DISK_API_B_PORT', '18082')}"
    load_balancer_url = f"http://127.0.0.1:{values.get('DISK_LB_PORT', '18080')}"
    postgres_port = int(values.get("DISK_POSTGRES_PORT", "15432"))
    minio_url = f"http://127.0.0.1:{values.get('DISK_MINIO_PORT', '19000')}"
    compose_base = (
        [
            "docker",
            "compose",
            "--env-file",
            str(env_file),
            "-f",
            str(compose_file),
        ]
        if topology is None
        else []
    )

    def compose(*arguments: str) -> subprocess.CompletedProcess[str]:
        if topology is not None:
            return topology.run(*arguments)
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
        "dbname": values.get("DISK_DATABASE_NAME", "disk"),
        "user": values.get("DISK_DATABASE_USER", "disk"),
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
        aws_access_key_id=values["DISK_S3_ACCESS_KEY"],
        aws_secret_access_key=values["DISK_S3_SECRET_KEY"],
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

    def container_fingerprint(service: str) -> str:
        if topology is not None:
            fingerprint = topology.fingerprint(service)
            require(bool(fingerprint), f"process fingerprint is missing for {service}")
            return fingerprint
        container_ids = [
            line.strip()
            for line in compose("ps", "-q", service).stdout.splitlines()
            if line.strip()
        ]
        require(len(container_ids) == 1, f"expected one container for {service}")
        inspection = subprocess.run(
            [
                "docker",
                "inspect",
                "--format",
                "{{.Id}}|{{.State.StartedAt}}|{{.RestartCount}}",
                container_ids[0],
            ],
            cwd=root,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        fingerprint = inspection.stdout.strip()
        require(bool(fingerprint), f"container fingerprint is missing for {service}")
        return fingerprint

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
    routed_filename = f"distributed_random_route_{run_id}.bin"
    routed_prefix = f"disk-random-route-{run_id}:".encode()
    routed_content = (routed_prefix + b"R" * 1_048_576)[:1_048_576]
    routed_md5 = hashlib.md5(routed_content).hexdigest()
    routed_sha256 = hashlib.sha256(routed_content).hexdigest()
    routed_object_key = f"objects/sha256/{routed_sha256[:2]}/{routed_sha256}.bin"
    access_token = ""
    refresh_token = ""
    user_id = 0
    file_id = 0
    routed_file_id = 0
    race_upload_ids: list[str] = []
    race_file_ids: list[int] = []
    race_object_keys: list[str] = []
    worker_a_stopped = False
    api_a_stopped = False
    postgres_stopped = False
    redis_stopped = False
    minio_stopped = False
    evidence_path = root / ".sisyphus/evidence/distributed-flow-summary.json"
    evidence: dict[str, Any] = {
        "run_id": run_id,
        "runner": topology.runner_name if topology is not None else "compose",
        "checks": [],
    }

    def write_evidence() -> None:
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")

    def passed(name: str) -> None:
        evidence["checks"].append(name)
        print(f"PASS: {name}")

    def assert_sensitive_values_absent(response: Any, label: str) -> None:
        forbidden_values = (
            values["DISK_JWT_SECRET"],
            values["DISK_DATABASE_PASSWORD"],
            values["DISK_REDIS_PASSWORD"],
            values["DISK_MINIO_ROOT_PASSWORD"],
            values.get("DISK_MINIO_ROOT_USER", "disk-local"),
            values["DISK_S3_ACCESS_KEY"],
            values["DISK_S3_SECRET_KEY"],
            "http://minio:9000",
            minio_url,
            object_key,
            routed_object_key,
            access_token,
            refresh_token,
        )
        serialized_response = response.text + json.dumps(dict(response.headers), sort_keys=True)
        for forbidden in forbidden_values:
            if len(forbidden) >= 8:
                require(forbidden not in serialized_response, f"{label} leaked a sensitive value")

    def wait_dependency_unready(
        base_url: str,
        instance_id: str,
        unhealthy_components: set[str],
        label: str,
    ) -> dict[str, Any]:
        expected_components = {
            "runtime",
            "database",
            "redis",
            "staging_storage",
            "final_storage",
        }

        def probe() -> tuple[Any, Any, dict[str, Any], dict[str, Any]] | None:
            with httpx.Client(base_url=base_url, timeout=30) as client:
                live_response = client.get("/api/health/live")
                ready_response = client.get("/api/health/ready")
            if live_response.status_code != 200 or ready_response.status_code != 503:
                return None
            live_payload = response_json(live_response, f"{label} liveness")
            ready_payload = response_json(ready_response, f"{label} readiness")
            live_data = live_payload.get("data", {})
            ready_data = ready_payload.get("data", {})
            if (
                str(live_payload["code"]) != "0"
                or live_data.get("overall_status") != "healthy"
                or live_data.get("role") != "api"
                or live_data.get("instance_id") != instance_id
                or str(ready_payload["code"]) != "0"
                or ready_data.get("overall_status") != "unhealthy"
                or ready_data.get("role") != "api"
                or ready_data.get("instance_id") != instance_id
            ):
                return None
            components = ready_data.get("components", {})
            if not isinstance(components, dict) or set(components) != expected_components:
                return None
            for component in expected_components:
                expected_status = "unhealthy" if component in unhealthy_components else "healthy"
                component_data = components.get(component, {})
                if not isinstance(component_data, dict) or component_data.get("status") != expected_status:
                    return None
            return live_response, ready_response, live_data, ready_data

        live_response, ready_response, live_data, ready_data = wait_until(
            probe,
            90,
            f"{label} dependency-specific readiness",
            interval=0.5,
        )
        assert_sensitive_values_absent(live_response, f"{label} liveness")
        assert_sensitive_values_absent(ready_response, f"{label} readiness")
        expected_health_fields = {
            "overall_status",
            "role",
            "instance_id",
            "initialized",
            "draining",
            "worker_claiming_enabled",
            "worker_accepting",
            "version",
            "uptime",
            "total_check_ms",
            "timestamp",
            "components",
        }
        for response, health_data, probe_name in (
            (live_response, live_data, "liveness"),
            (ready_response, ready_data, "readiness"),
        ):
            require(
                set(response.json()) == {"code", "message", "data"},
                f"{label} {probe_name} exposed an unexpected envelope field",
            )
            require(
                set(health_data) == expected_health_fields,
                f"{label} {probe_name} exposed an unexpected health field",
            )
            require(
                health_data["worker_claiming_enabled"] is False
                and health_data["worker_accepting"] is False,
                f"{label} {probe_name} reported API Worker activity",
            )
        require(
            set(live_data["components"]) == {"runtime"},
            f"{label} liveness exposed an external dependency component",
        )
        require(
            live_data["components"]["runtime"] == {"status": "healthy", "latency_ms": 0},
            f"{label} liveness runtime component changed shape",
        )
        failure_messages = {
            "database": "Database check failed",
            "redis": "Redis check failed",
            "staging_storage": "Staging storage check failed",
            "final_storage": "Final storage check failed",
        }
        for component, component_data in ready_data["components"].items():
            expected_fields = {"status", "latency_ms"}
            if component in unhealthy_components:
                expected_fields.add("message")
            require(
                set(component_data) == expected_fields,
                f"{label} readiness exposed an unexpected component field",
            )
            if component in unhealthy_components:
                require(
                    component_data["message"] == failure_messages[component],
                    f"{label} readiness exposed an unexpected dependency message",
                )
        return ready_data

    try:
        wait_ready(api_a_url, "api", "disk-api-a")
        wait_ready(api_b_url, "api", "disk-api-b")
        wait_ready(load_balancer_url, "api")
        api_fingerprints_before = {
            service: container_fingerprint(service) for service in ("api-a", "api-b")
        }
        evidence["api_process_fingerprints_before_dependency_faults"] = api_fingerprints_before
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
                result_file = (payload.get("data") or {}).get("file", {})
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

            routed_quota_before = query_one(
                "SELECT storage_used, storage_reserved FROM users WHERE id = %s",
                (user_id,),
            )
            require(routed_quota_before is not None, "random-routed upload quota baseline is missing")
            route_sequence: list[str] = []
            expected_instances = {"disk-api-a", "disk-api-b"}

            def routed_instance(response: Any, label: str) -> str:
                instance_id = response.headers.get("X-Disk-Instance-Id")
                require(instance_id in expected_instances, f"{label} omitted a reviewed API instance ID")
                return str(instance_id)

            with httpx.Client(
                base_url=load_balancer_url,
                headers=auth,
                cookies={"disk_route_probe": run_id},
                timeout=180,
            ) as routed_http:
                routed_init = routed_http.post(
                    "/api/file/upload/init",
                    json={
                        "filename": routed_filename,
                        "file_size": len(routed_content),
                        "file_hash": routed_md5,
                        "parent_id": 0,
                    },
                )
                routed_init_payload = response_json(routed_init, "random-routed upload init")
                require(str(routed_init_payload["code"]) == "0", "random-routed upload init failed")
                require(
                    not routed_init_payload["data"].get("instant_upload", False),
                    "random-routed upload unexpectedly deduplicated",
                )
                routed_upload_id = str(routed_init_payload["data"]["upload_id"])
                route_sequence.append(routed_instance(routed_init, "random-routed upload init"))

                routed_chunk_attempts = 0
                for attempt in range(1, 17):
                    routed_chunk = routed_http.post(
                        "/api/file/upload/chunk",
                        params={
                            "upload_id": routed_upload_id,
                            "chunk_index": 0,
                            "chunk_hash": routed_md5,
                        },
                        headers={"Content-Type": "application/octet-stream"},
                        content=routed_content,
                    )
                    routed_chunk_payload = response_json(
                        routed_chunk,
                        f"random-routed chunk attempt {attempt}",
                    )
                    require(
                        str(routed_chunk_payload["code"]) == "0",
                        f"random-routed chunk attempt {attempt} was not idempotent",
                    )
                    route_sequence.append(
                        routed_instance(routed_chunk, f"random-routed chunk attempt {attempt}")
                    )
                    routed_chunk_attempts = attempt
                    if set(route_sequence) == expected_instances:
                        break

                require(
                    set(route_sequence) == expected_instances,
                    "one load-balancer client remained affine while both APIs were ready",
                )
                routed_chunk_count = query_one(
                    "SELECT COUNT(*) AS count FROM upload_task_chunks WHERE task_id = %s",
                    (routed_upload_id,),
                )
                require(
                    routed_chunk_count is not None and int(routed_chunk_count["count"]) == 1,
                    "random-routed chunk retries created duplicate metadata",
                )

                routed_complete = routed_http.post(
                    "/api/file/upload/complete",
                    json={"upload_id": routed_upload_id},
                )
                routed_complete_payload = response_json(
                    routed_complete,
                    "random-routed upload complete",
                )
                require(
                    str(routed_complete_payload["code"]) == "0",
                    "random-routed upload complete failed",
                )
                route_sequence.append(
                    routed_instance(routed_complete, "random-routed upload complete")
                )
                routed_file = (routed_complete_payload.get("data") or {}).get("file", {})
                require(routed_file.get("id") is not None, "random-routed complete omitted its file")
                routed_file_id = int(routed_file["id"])

                routed_download = routed_http.get(f"/api/file/download/{routed_file_id}")
                route_sequence.append(
                    routed_instance(routed_download, "random-routed upload download")
                )
                require(
                    routed_download.status_code == 200 and routed_download.content == routed_content,
                    "random-routed download changed the uploaded content",
                )

            routed_task = query_one(
                "SELECT status, completed_file_id FROM upload_tasks WHERE id = %s AND user_id = %s",
                (routed_upload_id, user_id),
            )
            require(
                routed_task is not None
                and int(routed_task["status"]) == 1
                and int(routed_task["completed_file_id"]) == routed_file_id,
                "random-routed task did not persist one completed file",
            )
            routed_content_row = query_one(
                "SELECT COUNT(*) AS count, MAX(content.ref_count) AS ref_count "
                "FROM files AS file "
                "JOIN file_contents AS content ON content.id = file.content_id "
                "WHERE file.user_id = %s AND file.name = %s "
                "AND content.hash_md5 = %s AND content.hash_sha256 = %s",
                (user_id, routed_filename, routed_md5, routed_sha256),
            )
            require(
                routed_content_row is not None
                and int(routed_content_row["count"]) == 1
                and int(routed_content_row["ref_count"]) == 1,
                "random-routed upload did not create one file and content reference",
            )
            routed_quota_after = query_one(
                "SELECT storage_used, storage_reserved FROM users WHERE id = %s",
                (user_id,),
            )
            require(routed_quota_after is not None, "random-routed upload quota result is missing")
            require(
                int(routed_quota_after["storage_reserved"])
                == int(routed_quota_before["storage_reserved"]),
                "random-routed upload did not release reserved quota exactly once",
            )
            require(
                int(routed_quota_after["storage_used"])
                == int(routed_quota_before["storage_used"]) + len(routed_content),
                "random-routed upload did not settle used quota exactly once",
            )
            require(object_exists(routed_object_key), "random-routed final S3 object is missing")
            evidence["random_routing_upload"] = {
                "upload_id": routed_upload_id,
                "file_id": routed_file_id,
                "content_sha256": routed_sha256,
                "chunk_attempts": routed_chunk_attempts,
                "route_sequence": route_sequence,
                "distinct_instances": sorted(set(route_sequence)),
            }
            passed("one load-balancer client completes an upload across both API instances")

            admin_login = http.post(
                f"{api_a_url}/api/auth/login",
                json={
                    "account": "admin",
                    "password": os.environ.get(
                        "DISK_DISTRIBUTED_ADMIN_PASSWORD",
                        "Admin123",
                    ),
                },
            )
            admin_login_payload = response_json(admin_login, "admin login for expiration race")
            require(
                admin_login.status_code == 200 and str(admin_login_payload["code"]) == "0",
                "admin login for expiration race failed",
            )
            admin_auth = {
                "Authorization": f"Bearer {admin_login_payload['data']['access_token']}"
            }

            def run_terminal_race(expired: bool) -> None:
                scenario = "expired" if expired else "active"
                race_id = uuid.uuid4().hex[:12]
                race_filename = f"distributed_race_{scenario}_{run_id}_{race_id}.bin"
                race_content = f"distributed-terminal-race:{scenario}:{run_id}:{race_id}".encode()
                race_md5 = hashlib.md5(race_content).hexdigest()
                race_sha256 = hashlib.sha256(race_content).hexdigest()
                race_object_key = f"objects/sha256/{race_sha256[:2]}/{race_sha256}.bin"
                quota_before = query_one(
                    "SELECT storage_used, storage_reserved FROM users WHERE id = %s",
                    (user_id,),
                )
                require(quota_before is not None, f"{scenario} race quota baseline is missing")

                race_init = http.post(
                    f"{api_a_url}/api/file/upload/init",
                    headers=auth,
                    json={
                        "filename": race_filename,
                        "file_size": len(race_content),
                        "file_hash": race_md5,
                        "parent_id": 0,
                    },
                )
                race_init_payload = response_json(race_init, f"{scenario} race init")
                require(str(race_init_payload["code"]) == "0", f"{scenario} race init failed")
                require(
                    not race_init_payload["data"].get("instant_upload", False),
                    f"{scenario} race unexpectedly deduplicated",
                )
                race_upload_id = str(race_init_payload["data"]["upload_id"])
                race_upload_ids.append(race_upload_id)

                race_chunk = http.post(
                    f"{api_b_url}/api/file/upload/chunk",
                    params={
                        "upload_id": race_upload_id,
                        "chunk_index": 0,
                        "chunk_hash": race_md5,
                    },
                    headers={**auth, "Content-Type": "application/octet-stream"},
                    content=race_content,
                )
                require(
                    str(response_json(race_chunk, f"{scenario} race chunk")["code"]) == "0",
                    f"{scenario} race chunk failed",
                )
                quota_after_init = query_one(
                    "SELECT storage_used, storage_reserved FROM users WHERE id = %s",
                    (user_id,),
                )
                require(quota_after_init is not None, f"{scenario} race init quota is missing")
                require(
                    int(quota_after_init["storage_reserved"])
                    == int(quota_before["storage_reserved"]) + len(race_content),
                    f"{scenario} race did not reserve exactly one payload",
                )

                if expired:
                    require(
                        execute(
                            "UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' "
                            "WHERE id = %s AND user_id = %s AND status = 0",
                            (race_upload_id, user_id),
                        )
                        == 1,
                        "expired race fixture did not cross the PostgreSQL deadline",
                    )

                barrier = threading.Barrier(3)

                def race_complete() -> Any:
                    barrier.wait(timeout=10)
                    return httpx.post(
                        f"{api_a_url}/api/file/upload/complete",
                        headers=auth,
                        json={"upload_id": race_upload_id},
                        timeout=180,
                    )

                def race_cancel() -> Any:
                    barrier.wait(timeout=10)
                    return httpx.request(
                        "DELETE",
                        f"{api_b_url}/api/file/upload/{race_upload_id}",
                        headers=auth,
                        timeout=180,
                    )

                def race_expire() -> Any:
                    barrier.wait(timeout=10)
                    return httpx.post(
                        f"{api_a_url}/api/admin/maintenance/cleanup/expired",
                        headers=admin_auth,
                        timeout=180,
                    )

                with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
                    complete_future = pool.submit(race_complete)
                    cancel_future = pool.submit(race_cancel)
                    expire_future = pool.submit(race_expire)
                    complete_response = complete_future.result()
                    cancel_response = cancel_future.result()
                    expire_response = expire_future.result()

                require(
                    complete_response.headers.get("X-Disk-Instance-Id") == "disk-api-a",
                    f"{scenario} race complete did not execute on API A",
                )
                require(
                    cancel_response.headers.get("X-Disk-Instance-Id") == "disk-api-b",
                    f"{scenario} race cancel did not execute on API B",
                )
                require(
                    expire_response.headers.get("X-Disk-Instance-Id") == "disk-api-a",
                    f"{scenario} race expiration did not execute on API A",
                )
                complete_payload = response_json(complete_response, f"{scenario} race complete")
                cancel_payload = response_json(cancel_response, f"{scenario} race cancel")
                expire_payload = response_json(expire_response, f"{scenario} race expiration")
                require(str(expire_payload["code"]) == "0", f"{scenario} race expiration scan failed")

                task = query_one(
                    "SELECT status, completed_file_id, lease_owner, lease_expires_at "
                    "FROM upload_tasks WHERE id = %s AND user_id = %s",
                    (race_upload_id, user_id),
                )
                require(task is not None, f"{scenario} race upload task is missing")
                status = int(task["status"])
                allowed_statuses = {2, 3} if expired else {1, 2}
                require(status in allowed_statuses, f"{scenario} race reached illegal status {status}")
                require(
                    task["lease_owner"] is None and task["lease_expires_at"] is None,
                    f"{scenario} race terminal state retained a lease",
                )

                quota_after_race = query_one(
                    "SELECT storage_used, storage_reserved FROM users WHERE id = %s",
                    (user_id,),
                )
                require(quota_after_race is not None, f"{scenario} race final quota is missing")
                require(
                    int(quota_after_race["storage_reserved"])
                    == int(quota_before["storage_reserved"]),
                    f"{scenario} race did not release reserved quota exactly once",
                )
                chunk_count = query_one(
                    "SELECT COUNT(*) AS count FROM upload_task_chunks WHERE task_id = %s",
                    (race_upload_id,),
                )
                require(
                    chunk_count is not None and int(chunk_count["count"]) == 0,
                    f"{scenario} race retained chunk metadata",
                )
                cleanup_jobs = query_one(
                    "SELECT COUNT(*) AS count FROM storage_jobs WHERE dedupe_key = %s",
                    (f"staging-cleanup:{race_upload_id}",),
                )
                require(
                    cleanup_jobs is not None and int(cleanup_jobs["count"]) == 1,
                    f"{scenario} race did not create exactly one cleanup job",
                )

                file_row = query_one(
                    "SELECT file.id, content.ref_count "
                    "FROM files AS file "
                    "JOIN file_contents AS content ON content.id = file.content_id "
                    "WHERE file.user_id = %s AND file.name = %s",
                    (user_id, race_filename),
                )
                content_count = query_one(
                    "SELECT COUNT(*) AS count FROM file_contents "
                    "WHERE hash_md5 = %s AND hash_sha256 = %s",
                    (race_md5, race_sha256),
                )
                require(content_count is not None, f"{scenario} race content count is missing")

                if status == 1:
                    require(file_row is not None, "completed race did not create its file")
                    require(
                        int(task["completed_file_id"]) == int(file_row["id"]),
                        "completed race task points at a different file",
                    )
                    require(int(content_count["count"]) == 1, "completed race content row is not unique")
                    require(int(file_row["ref_count"]) == 1, "completed race reference count is not one")
                    require(
                        int(quota_after_race["storage_used"])
                        == int(quota_before["storage_used"]) + len(race_content),
                        "completed race did not convert quota exactly once",
                    )
                    require(str(complete_payload["code"]) == "0", "completed race response was not successful")
                    require(str(cancel_payload["code"]) != "0", "completed race accepted cancellation")
                    wait_until(lambda: object_exists(race_object_key), 30, f"{scenario} race final object")
                    race_file_ids.append(int(file_row["id"]))
                    race_object_keys.append(race_object_key)
                else:
                    require(file_row is None, f"{scenario} non-completed race created a file")
                    require(int(content_count["count"]) == 0, f"{scenario} non-completed race created content")
                    require(
                        int(quota_after_race["storage_used"])
                        == int(quota_before["storage_used"]),
                        f"{scenario} non-completed race changed used quota",
                    )
                    require(str(complete_payload["code"]) != "0", f"{scenario} loser completed")
                    if status == 2:
                        require(str(cancel_payload["code"]) == "0", f"{scenario} cancel winner failed")
                    else:
                        require(str(cancel_payload["code"]) != "0", "expired winner accepted cancellation")

                def cleanup_succeeded() -> bool:
                    row = query_one(
                        "SELECT status FROM storage_jobs WHERE dedupe_key = %s",
                        (f"staging-cleanup:{race_upload_id}",),
                    )
                    return row is not None and int(row["status"]) == 3

                wait_until(cleanup_succeeded, 45, f"{scenario} race cleanup", interval=0.5)
                evidence.setdefault("terminal_races", []).append(
                    {
                        "scenario": scenario,
                        "upload_id": race_upload_id,
                        "status": status,
                        "complete_instance": complete_response.headers.get("X-Disk-Instance-Id"),
                        "cancel_instance": cancel_response.headers.get("X-Disk-Instance-Id"),
                        "expire_instance": expire_response.headers.get("X-Disk-Instance-Id"),
                        "complete_code": str(complete_payload["code"]),
                        "cancel_code": str(cancel_payload["code"]),
                    }
                )
                passed(f"{scenario} complete/cancel/expire race preserves one terminal and quota")

            run_terminal_race(expired=False)
            run_terminal_race(expired=True)

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

            postgres_stopped = True
            compose("stop", "postgres")
            for base_url, instance_id in (
                (api_a_url, "disk-api-a"),
                (api_b_url, "disk-api-b"),
            ):
                wait_dependency_unready(
                    base_url,
                    instance_id,
                    {"database"},
                    f"PostgreSQL outage on {instance_id}",
                )
            compose("start", "postgres")
            wait_ready(api_a_url, "api", "disk-api-a")
            wait_ready(api_b_url, "api", "disk-api-b")
            postgres_stopped = False
            recovered_profile = http.get(f"{api_a_url}/api/user/profile", headers=auth)
            recovered_payload = response_json(recovered_profile, "profile after PostgreSQL recovery")
            require(
                recovered_profile.status_code == 200 and str(recovered_payload["code"]) == "0",
                "database-backed profile did not recover with PostgreSQL",
            )
            passed("PostgreSQL outage preserves liveness and the existing pools reconnect")

            redis_stopped = True
            compose("stop", "redis")
            for base_url, instance_id in (
                (api_a_url, "disk-api-a"),
                (api_b_url, "disk-api-b"),
            ):
                wait_dependency_unready(
                    base_url,
                    instance_id,
                    {"redis"},
                    f"Redis outage on {instance_id}",
                )

            def redis_failure_closed() -> bool:
                try:
                    response = http.get(f"{api_b_url}/api/user/profile", headers=auth, timeout=10)
                    return str(response_json(response, "Redis failure auth check")["code"]) == "70002"
                except httpx.HTTPError:
                    return False

            wait_until(redis_failure_closed, 20, "Redis fail-closed authentication", interval=0.5)
            compose("start", "redis")
            wait_ready(api_a_url, "api", "disk-api-a")
            wait_ready(api_b_url, "api", "disk-api-b")
            redis_stopped = False
            recovered_profile = http.get(f"{api_b_url}/api/user/profile", headers=auth)
            require(
                recovered_profile.status_code == 200
                and str(response_json(recovered_profile, "profile after Redis recovery")["code"]) == "0",
                "authentication did not recover with Redis",
            )
            passed("Redis outage fails closed and recovery reconnects")

            minio_stopped = True
            compose("stop", "minio")
            for base_url, instance_id in (
                (api_a_url, "disk-api-a"),
                (api_b_url, "disk-api-b"),
            ):
                wait_dependency_unready(
                    base_url,
                    instance_id,
                    {"staging_storage", "final_storage"},
                    f"MinIO outage on {instance_id}",
                )
            unavailable_download = http.get(
                f"{api_b_url}/api/file/download/{file_id}",
                headers=auth,
                timeout=60,
            )
            unavailable_payload = response_json(unavailable_download, "download during MinIO outage")
            require(
                unavailable_download.status_code == 500 and str(unavailable_payload["code"]) == "50011",
                "MinIO outage did not return the controlled file-read error",
            )
            assert_sensitive_values_absent(unavailable_download, "download during MinIO outage")
            compose("start", "minio")
            wait_ready(api_a_url, "api", "disk-api-a")
            wait_ready(api_b_url, "api", "disk-api-b")
            minio_stopped = False
            recovered_download = http.get(
                f"{api_b_url}/api/file/download/{file_id}",
                headers=auth,
                timeout=120,
            )
            require(recovered_download.status_code == 200, "download did not recover with MinIO")
            require(recovered_download.content == content, "download after MinIO recovery changed content")
            require(object_exists(object_key), "S3 client did not reconnect after MinIO recovery")
            passed("MinIO outage preserves liveness and the existing S3 clients reconnect")

            api_fingerprints_after = {
                service: container_fingerprint(service) for service in ("api-a", "api-b")
            }
            require(
                api_fingerprints_after == api_fingerprints_before,
                "an API process restarted while an external dependency was unavailable",
            )
            evidence["api_process_fingerprints_after_dependency_recovery"] = api_fingerprints_after
            passed("all dependency recoveries complete without restarting either API process")

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

            file_ids_to_delete = [file_id, routed_file_id, *race_file_ids]
            soft_delete = http.request(
                "DELETE",
                f"{api_b_url}/api/file",
                headers=auth,
                json={"file_ids": file_ids_to_delete, "folder_ids": []},
            )
            require(str(response_json(soft_delete, "soft delete")["code"]) == "0", "soft delete failed")

            def trash_id(candidate_file_id: int) -> int | None:
                row = query_one(
                    "SELECT id FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s",
                    (user_id, candidate_file_id),
                )
                return int(row["id"]) if row is not None else None

            deleted_trash_ids = [
                wait_until(
                    lambda candidate_file_id=candidate_file_id: trash_id(candidate_file_id),
                    10,
                    f"trash row for file {candidate_file_id}",
                )
                for candidate_file_id in file_ids_to_delete
            ]
            permanent = http.request(
                "DELETE",
                f"{api_b_url}/api/trash",
                headers=auth,
                json={"trash_ids": deleted_trash_ids},
            )
            require(str(response_json(permanent, "permanent delete")["code"]) == "0", "permanent delete failed")
            for deleted_object_key in (object_key, routed_object_key, *race_object_keys):
                wait_until(
                    lambda deleted_object_key=deleted_object_key: not object_exists(deleted_object_key),
                    45,
                    f"blob GC for {deleted_object_key}",
                    interval=0.5,
                )
            file_id = 0
            routed_file_id = 0
            race_file_ids.clear()

            logout = http.post(f"{api_a_url}/api/auth/logout", headers=auth)
            require(str(response_json(logout, "logout on API A")["code"]) == "0", "logout on API A failed")
            revoked = http.get(f"{api_b_url}/api/user/profile", headers=auth)
            revoked_payload = response_json(revoked, "revoked token on API B")
            require(revoked.status_code == 401 and str(revoked_payload["code"]) == "40111", "API B did not immediately reject API A logout")
            passed("access-token logout is immediately visible on another API")

        for race_upload_id in race_upload_ids:
            execute("DELETE FROM storage_jobs WHERE aggregate_id = %s", (race_upload_id,))
        execute("DELETE FROM users WHERE id = %s", (user_id,))
        user_id = 0
        evidence["status"] = "passed"
        write_evidence()
        print(f"PASS: distributed flow completed ({len(evidence['checks'])} checks)")
        return 0
    except Exception as error:  # noqa: BLE001 - print a credential-free summary
        evidence["status"] = "failed"
        evidence["error"] = str(error)
        write_evidence()
        print(f"FAIL: {error}")
        return 1
    finally:
        for stopped, service in (
            (postgres_stopped, "postgres"),
            (redis_stopped, "redis"),
            (minio_stopped, "minio"),
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

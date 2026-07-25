#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["boto3", "httpx", "psycopg[binary]"]
# ///

"""Environment-gated Disk application flow against an S3/MinIO backend."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any


class TestFailure(RuntimeError):
    """Raised when an application-flow invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TestFailure(message)
    print(f"PASS: {message}")


def require_envelope(response: Any, expected_status: int, label: str) -> dict[str, Any]:
    require(response.status_code == expected_status, f"{label} HTTP {expected_status}")
    try:
        payload = response.json()
    except ValueError as exc:
        raise TestFailure(f"{label} returned invalid JSON: {response.text}") from exc
    require(set(("code", "message", "data")).issubset(payload), f"{label} keeps public response envelope")
    return payload


def require_response_context(
    response: Any,
    request_id: str,
    instance_id: str,
    label: str,
) -> None:
    require(
        response.headers.get("x-request-id") == request_id,
        f"{label} echoes the caller request ID",
    )
    require(
        response.headers.get("x-disk-instance-id") == instance_id,
        f"{label} identifies the handling instance",
    )


def application_records(log_path: Path) -> list[dict[str, Any]]:
    if not log_path.is_file():
        return []

    records: list[dict[str, Any]] = []
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if (
            isinstance(record, dict)
            and record.get("schema_version") == 1
            and record.get("source") == "application"
        ):
            records.append(record)
    return records


def wait_for_application_record(
    log_path: Path,
    label: str,
    predicate: Any,
    timeout_seconds: float = 10.0,
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        for record in application_records(log_path):
            if predicate(record):
                require(True, label)
                return record
        time.sleep(0.05)
    raise TestFailure(f"Timed out waiting for application log: {label}")


def has_context(
    record: dict[str, Any],
    *,
    request_id: str | None,
    instance_id: str,
    operation: str,
    upload_id: str | None,
    job_id: int | None,
    lease_owner: str | None,
    state_version: int | None,
) -> bool:
    return all(
        (
            record.get("request_id") == request_id,
            record.get("instance_id") == instance_id,
            record.get("operation") == operation,
            record.get("upload_id") == upload_id,
            record.get("job_id") == job_id,
            record.get("lease_owner") == lease_owner,
            record.get("state_version") == state_version,
        )
    )


def object_exists(client: Any, bucket: str, key: str) -> bool:
    from botocore.exceptions import ClientError

    try:
        client.head_object(Bucket=bucket, Key=key)
        return True
    except ClientError as exc:
        code = str(exc.response.get("Error", {}).get("Code", ""))
        if code in {"404", "NoSuchKey", "NotFound"}:
            return False
        raise


def wait_for_object_state(
    client: Any,
    bucket: str,
    key: str,
    expected_exists: bool,
    timeout_seconds: float = 10.0,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if object_exists(client, bucket, key) == expected_exists:
            return
        time.sleep(0.1)
    state = "exist" if expected_exists else "be absent"
    raise TestFailure(f"S3 object {key} did not {state} within {timeout_seconds:.0f}s")


def remove_prefix(client: Any, bucket: str, prefix: str) -> None:
    continuation_token: str | None = None
    while True:
        request: dict[str, Any] = {"Bucket": bucket, "Prefix": prefix}
        if continuation_token:
            request["ContinuationToken"] = continuation_token
        response = client.list_objects_v2(**request)
        objects = [{"Key": item["Key"]} for item in response.get("Contents", [])]
        if objects:
            client.delete_objects(Bucket=bucket, Delete={"Objects": objects, "Quiet": True})
        if not response.get("IsTruncated"):
            return
        continuation_token = response.get("NextContinuationToken")


def list_prefix_keys(client: Any, bucket: str, prefix: str) -> list[str]:
    keys: list[str] = []
    continuation_token: str | None = None
    while True:
        request: dict[str, Any] = {"Bucket": bucket, "Prefix": prefix}
        if continuation_token:
            request["ContinuationToken"] = continuation_token
        response = client.list_objects_v2(**request)
        keys.extend(str(item["Key"]) for item in response.get("Contents", []))
        if not response.get("IsTruncated"):
            return keys
        continuation_token = response.get("NextContinuationToken")


def main() -> int:
    if os.environ.get("DISK_S3_APP_INTEGRATION") != "1":
        print("SKIP: DISK_S3_APP_INTEGRATION is not 1; skipping Disk S3 application flow")
        return 0

    required_env = (
        "DISK_S3_ENDPOINT",
        "DISK_S3_BUCKET",
        "DISK_S3_ACCESS_KEY",
        "DISK_S3_SECRET_KEY",
    )
    missing = [name for name in required_env if not os.environ.get(name)]
    if missing:
        print(f"FAIL: missing required S3 application integration env vars: {', '.join(missing)}")
        return 1

    import boto3
    import httpx
    import psycopg
    from botocore.config import Config
    from psycopg.rows import dict_row

    repo_root = Path(__file__).resolve().parents[2]
    server_bin = Path(
        os.environ.get("SERVER_BIN", repo_root / "build/linux-debug-clang/src/disk")
    ).resolve()
    if not server_bin.is_file():
        print(f"FAIL: server binary not found: {server_bin}")
        return 1

    endpoint = os.environ["DISK_S3_ENDPOINT"]
    bucket = os.environ["DISK_S3_BUCKET"]
    region = os.environ.get("DISK_S3_REGION", "us-east-1")
    port = int(os.environ.get("DISK_S3_APP_PORT", "18080"))
    base_url = f"http://127.0.0.1:{port}"
    run_id = uuid.uuid4().hex[:12]
    instance_id = f"s3-app-{run_id}"
    request_ids = {
        "init": f"s3-app-init-{run_id}",
        "chunk": f"s3-app-chunk-{run_id}",
        "complete": f"s3-app-complete-{run_id}",
        "full": f"s3-app-download-full-{run_id}",
        "range": f"s3-app-download-range-{run_id}",
    }
    object_prefix = f"objects/app-{run_id}"
    staging_prefix = f"staging/app-{run_id}"
    payload = (f"disk-s3-app-flow-{run_id}-".encode() + bytes(range(256))) * 64
    payload_hash = hashlib.md5(payload).hexdigest()
    payload_sha256 = hashlib.sha256(payload).hexdigest()
    filename = f"s3_app_{run_id}.bin"

    s3_client = boto3.client(
        "s3",
        endpoint_url=endpoint,
        aws_access_key_id=os.environ["DISK_S3_ACCESS_KEY"],
        aws_secret_access_key=os.environ["DISK_S3_SECRET_KEY"],
        region_name=region,
        config=Config(s3={"addressing_style": "path"}),
    )

    base_config = json.loads((repo_root / "config.json").read_text(encoding="utf-8"))
    db_config = dict(base_config["db_clients"][0])
    db_connect = {
        "host": os.environ.get("PGHOST", db_config.get("host", "127.0.0.1")),
        "port": int(os.environ.get("PGPORT", db_config.get("port", 5432))),
        "dbname": os.environ.get("PGDATABASE", db_config.get("dbname", "disk")),
        "user": os.environ.get("PGUSER", db_config.get("user", "postgres")),
        "password": os.environ.get("PGPASSWORD", db_config.get("passwd", "postgresql")),
        "row_factory": dict_row,
    }

    def query_one(sql: str, params: tuple[Any, ...]) -> dict[str, Any] | None:
        with psycopg.connect(**db_connect) as connection:
            with connection.cursor() as cursor:
                cursor.execute(sql, params)
                row = cursor.fetchone()
                return dict(row) if row is not None else None

    def execute(sql: str, params: tuple[Any, ...]) -> int:
        with psycopg.connect(**db_connect) as connection:
            with connection.cursor() as cursor:
                cursor.execute(sql, params)
                affected = cursor.rowcount
            connection.commit()
            return affected

    def wait_for_staging_cleanup(
        upload_id: str,
        timeout_seconds: float = 20.0,
    ) -> dict[str, Any]:
        dedupe_key = f"staging-cleanup:{upload_id}"
        prefix = f"{staging_prefix}/{upload_id}/"
        deadline = time.monotonic() + timeout_seconds
        last_job: dict[str, Any] | None = None
        last_keys: list[str] = []
        while time.monotonic() < deadline:
            last_job = query_one(
                "SELECT id, job_type, aggregate_id, dedupe_key, status, attempts, "
                "locked_by, last_error FROM storage_jobs WHERE dedupe_key = %s",
                (dedupe_key,),
            )
            last_keys = list_prefix_keys(s3_client, bucket, prefix)
            if last_job is not None and int(last_job["status"]) == 3 and not last_keys:
                return last_job
            if last_job is not None and int(last_job["status"]) == 4:
                raise TestFailure(
                    f"S3 staging cleanup entered DeadLetter: {last_job}"
                )
            time.sleep(0.1)
        raise TestFailure(
            f"S3 staging cleanup did not converge within {timeout_seconds:.0f}s: "
            f"job={last_job}, remaining_keys={last_keys}"
        )

    def remove_upload_records(
        upload_id: str,
        user: int,
        baseline_reserved: int | None = None,
    ) -> None:
        with psycopg.connect(**db_connect) as connection:
            with connection.cursor() as cursor:
                if baseline_reserved is not None:
                    cursor.execute(
                        "UPDATE users SET storage_reserved = %s WHERE id = %s",
                        (baseline_reserved, user),
                    )
                cursor.execute(
                    "DELETE FROM storage_jobs WHERE aggregate_id = %s",
                    (upload_id,),
                )
                cursor.execute(
                    "DELETE FROM upload_tasks WHERE id = %s AND user_id = %s",
                    (upload_id, user),
                )
            connection.commit()

    server: subprocess.Popen[bytes] | None = None
    log_handle: Any = None
    log_path = repo_root / ".sisyphus/evidence" / f"s3-app-server-{run_id}.log"
    token = ""
    user_id = 0
    file_id: int | None = None
    primary_upload_id: str | None = None
    under_upload_id: str | None = None
    under_baseline_reserved: int | None = None
    return_code = 0

    with tempfile.TemporaryDirectory(prefix="disk-s3-app-") as temp_dir_raw:
        temp_dir = Path(temp_dir_raw)
        framework_log_path = temp_dir / "framework-log"
        framework_log_path.mkdir()
        config = json.loads(json.dumps(base_config))
        config["listeners"] = [{"address": "127.0.0.1", "port": port}]
        config["app"]["upload_path"] = str(temp_dir / "drogon-upload")
        config["app"]["log"] = {
            "log_level": "DEBUG",
            "log_path": str(framework_log_path),
        }
        disk_config = config["custom_config"]["disk"]
        disk_config["instance_id"] = instance_id
        disk_config["storage_backend"] = "s3"
        disk_config["upload_staging_backend"] = "s3"
        disk_config["storage_base_path"] = str(temp_dir / "unused-local-blobs")
        disk_config["temp_upload_path"] = str(temp_dir / "staging")
        disk_config["s3"] = {
            "bucket": bucket,
            "region": region,
            "endpoint": endpoint,
            "use_ssl": endpoint.startswith("https://"),
            "force_path_style": True,
            "verify_ssl": False,
            "object_prefix": object_prefix,
            "staging_prefix": staging_prefix,
            "connect_timeout_ms": 3000,
            "request_timeout_ms": 300000,
        }
        (temp_dir / "config.json").write_text(json.dumps(config, indent=2), encoding="utf-8")

        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_handle = log_path.open("wb")
        server_env = os.environ.copy()
        server_env.setdefault("JWT_SECRET", "dev-only-jwt-secret-key-change-in-production-2024")

        try:
            server = subprocess.Popen(
                [str(server_bin)],
                cwd=temp_dir,
                env=server_env,
                stdout=log_handle,
                stderr=subprocess.STDOUT,
            )

            deadline = time.monotonic() + 30
            with httpx.Client(base_url=base_url, timeout=30) as http:
                while time.monotonic() < deadline:
                    if server.poll() is not None:
                        raise TestFailure(f"Disk S3 server exited during startup with code {server.returncode}")
                    try:
                        probe = http.get("/api/auth/login")
                        if probe.status_code in {400, 401, 405}:
                            break
                    except httpx.HTTPError:
                        pass
                    time.sleep(0.2)
                else:
                    raise TestFailure("Disk S3 server did not become ready within 30 seconds")
                require(True, "Disk server starts with storage_backend=s3 and validates the bucket")

                login = http.post(
                    "/api/auth/login",
                    json={
                        "account": os.environ.get("TEST_USER", "admin"),
                        "password": os.environ.get("TEST_PASS", "Admin123"),
                    },
                )
                login_payload = require_envelope(login, 200, "login")
                require(str(login_payload["code"]) == "0", "login succeeds")
                token = str(login_payload["data"]["access_token"])
                auth_headers = {"Authorization": f"Bearer {token}"}

                user_row = query_one(
                    "SELECT id FROM users WHERE username = %s OR email = %s LIMIT 1",
                    (os.environ.get("TEST_USER", "admin"), os.environ.get("TEST_USER", "admin")),
                )
                require(user_row is not None, "authenticated user exists in PostgreSQL")
                user_id = int(user_row["id"])

                init = http.post(
                    "/api/file/upload/init",
                    headers={**auth_headers, "X-Request-Id": request_ids["init"]},
                    json={
                        "filename": filename,
                        "file_size": len(payload),
                        "file_hash": payload_hash,
                        "parent_id": 0,
                    },
                )
                init_payload = require_envelope(init, 200, "upload init")
                require_response_context(init, request_ids["init"], instance_id, "upload init")
                require(str(init_payload["code"]) == "0", "upload init succeeds")
                require(not init_payload["data"].get("instant_upload", False), "application upload uses chunk finalization")
                upload_id = str(init_payload["data"]["upload_id"])
                primary_upload_id = upload_id

                chunk = http.post(
                    "/api/file/upload/chunk",
                    params={"upload_id": upload_id, "chunk_index": 0, "chunk_hash": payload_hash},
                    headers={
                        **auth_headers,
                        "Content-Type": "application/octet-stream",
                        "X-Request-Id": request_ids["chunk"],
                    },
                    content=payload,
                )
                chunk_payload = require_envelope(chunk, 200, "upload chunk")
                require_response_context(chunk, request_ids["chunk"], instance_id, "upload chunk")
                require(chunk_payload["data"]["uploaded"] is True, "upload chunk succeeds")
                require(
                    not (temp_dir / "staging" / upload_id).exists(),
                    "S3-native chunk upload creates no node-local session directory",
                )
                task_staging = query_one(
                    "SELECT staging_backend, staging_prefix, state_version FROM upload_tasks WHERE id = %s",
                    (upload_id,),
                )
                require(
                    task_staging is not None
                    and task_staging["staging_backend"] == "s3"
                    and task_staging["staging_prefix"] == f"{staging_prefix}/{upload_id}",
                    "upload task persists its exact S3 staging session",
                )
                staged_keys = list_prefix_keys(
                    s3_client,
                    bucket,
                    f"{staging_prefix}/{upload_id}/",
                )
                require(len(staged_keys) == 1 and "/chunks/0-" in staged_keys[0], "chunk is stored in S3 staging")
                chunk_row = query_one(
                    "SELECT object_key FROM upload_task_chunks WHERE task_id = %s AND chunk_index = 0",
                    (upload_id,),
                )
                require(
                    chunk_row is not None and chunk_row["object_key"] == staged_keys[0],
                    "PostgreSQL chunk descriptor identifies the S3 staging object",
                )

                complete = http.post(
                    "/api/file/upload/complete",
                    headers={**auth_headers, "X-Request-Id": request_ids["complete"]},
                    json={"upload_id": upload_id},
                )
                complete_payload = require_envelope(complete, 200, "upload complete")
                require_response_context(
                    complete,
                    request_ids["complete"],
                    instance_id,
                    "upload complete",
                )
                require(str(complete_payload["code"]) == "0", "S3 upload finalization succeeds")
                file_id = int(complete_payload["data"]["file"]["id"])

                object_key = f"{object_prefix}/sha256/{payload_sha256[:2]}/{payload_sha256}.bin"
                wait_for_object_state(s3_client, bucket, object_key, True)
                head = s3_client.head_object(Bucket=bucket, Key=object_key)
                require(head["ContentLength"] == len(payload), "final S3 object has the uploaded size")
                cleanup_job = wait_for_staging_cleanup(upload_id)
                require(True, "Worker cleans only the completed S3 staging session")
                require(
                    int(cleanup_job["id"]) > 0
                    and cleanup_job["job_type"] == "staging_cleanup"
                    and cleanup_job["aggregate_id"] == upload_id
                    and cleanup_job["dedupe_key"] == f"staging-cleanup:{upload_id}"
                    and cleanup_job["locked_by"] is None,
                    "PostgreSQL cleanup job retains authoritative upload identity and clears its lease",
                )
                completed_task = query_one(
                    "SELECT status, state_version, lease_owner FROM upload_tasks WHERE id = %s",
                    (upload_id,),
                )
                require(
                    completed_task is not None
                    and int(completed_task["status"]) == 1
                    and int(completed_task["state_version"]) > int(task_staging["state_version"])
                    and completed_task["lease_owner"] is None,
                    "PostgreSQL upload task persists the completed version and clears its lease",
                )
                content_row = query_one(
                    "SELECT fc.hash_sha256, fc.storage_path FROM files f "
                    "JOIN file_contents fc ON fc.id = f.content_id WHERE f.id = %s",
                    (file_id,),
                )
                require(
                    content_row is not None
                    and content_row["hash_sha256"] == payload_sha256
                    and content_row["storage_path"] == object_key,
                    "database persists SHA-256 and the authoritative final object key",
                )

                full = http.get(
                    f"/api/file/download/{file_id}",
                    headers={**auth_headers, "X-Request-Id": request_ids["full"]},
                )
                require(full.status_code == 200, "full S3-backed download returns HTTP 200")
                require_response_context(full, request_ids["full"], instance_id, "full download")
                require(full.content == payload, "full S3-backed download preserves bytes")
                require(full.headers.get("accept-ranges") == "bytes", "full download keeps range capability header")

                partial = http.get(
                    f"/api/file/download/{file_id}",
                    headers={
                        **auth_headers,
                        "Range": "bytes=7-31",
                        "X-Request-Id": request_ids["range"],
                    },
                )
                require(partial.status_code == 206, "range S3-backed download returns HTTP 206")
                require_response_context(
                    partial,
                    request_ids["range"],
                    instance_id,
                    "range download",
                )
                require(partial.content == payload[7:32], "range S3-backed download preserves selected bytes")
                require(
                    partial.headers.get("content-range") == f"bytes 7-31/{len(payload)}",
                    "range download keeps public Content-Range shape",
                )

                completed_version = int(completed_task["state_version"])
                job_id = int(cleanup_job["id"])
                wait_for_application_record(
                    log_path,
                    "upload init log links the request to the PostgreSQL upload ID",
                    lambda record: has_context(
                        record,
                        request_id=request_ids["init"],
                        instance_id=instance_id,
                        operation="upload_init",
                        upload_id=upload_id,
                        job_id=None,
                        lease_owner=None,
                        state_version=None,
                    )
                    and str(record.get("message", "")).startswith("Upload task created successfully:"),
                )
                wait_for_application_record(
                    log_path,
                    "upload chunk log retains request and upload correlation",
                    lambda record: has_context(
                        record,
                        request_id=request_ids["chunk"],
                        instance_id=instance_id,
                        operation="upload_chunk",
                        upload_id=upload_id,
                        job_id=None,
                        lease_owner=None,
                        state_version=None,
                    )
                    and str(record.get("message", "")).startswith("Chunk upload successful:"),
                )
                wait_for_application_record(
                    log_path,
                    "S3 chunk write retains API correlation across the blocking queue",
                    lambda record: has_context(
                        record,
                        request_id=request_ids["chunk"],
                        instance_id=instance_id,
                        operation="upload_chunk",
                        upload_id=upload_id,
                        job_id=None,
                        lease_owner=None,
                        state_version=None,
                    )
                    and record.get("message")
                    == "S3 SDK call result: sdk_operation=put_object, outcome=success",
                )
                wait_for_application_record(
                    log_path,
                    "upload completion log uses the committed state version and cleared lease",
                    lambda record: has_context(
                        record,
                        request_id=request_ids["complete"],
                        instance_id=instance_id,
                        operation="upload_complete",
                        upload_id=upload_id,
                        job_id=None,
                        lease_owner=None,
                        state_version=completed_version,
                    )
                    and str(record.get("message", "")).startswith("File upload completed:"),
                )
                wait_for_application_record(
                    log_path,
                    "S3 promotion retains the active finalize lease and database version",
                    lambda record: record.get("request_id") == request_ids["complete"]
                    and record.get("instance_id") == instance_id
                    and record.get("operation") == "upload_complete"
                    and record.get("upload_id") == upload_id
                    and record.get("job_id") is None
                    and record.get("lease_owner") == instance_id
                    and isinstance(record.get("state_version"), int)
                    and 0 < int(record["state_version"]) < completed_version
                    and record.get("message")
                    == "S3 SDK call result: sdk_operation=complete_multipart_upload, outcome=success",
                )
                for request_name, label in (("full", "full"), ("range", "range")):
                    wait_for_application_record(
                        log_path,
                        f"S3 {label} download retains request correlation",
                        lambda record, request_name=request_name: has_context(
                            record,
                            request_id=request_ids[request_name],
                            instance_id=instance_id,
                            operation="download",
                            upload_id=None,
                            job_id=None,
                            lease_owner=None,
                            state_version=None,
                        )
                        and record.get("message")
                        == "S3 SDK call result: sdk_operation=get_object, outcome=success",
                    )
                wait_for_application_record(
                    log_path,
                    "Worker cleanup start uses the claimed PostgreSQL job lease",
                    lambda record: has_context(
                        record,
                        request_id=None,
                        instance_id=instance_id,
                        operation="storage_job_staging_cleanup",
                        upload_id=upload_id,
                        job_id=job_id,
                        lease_owner=instance_id,
                        state_version=None,
                    )
                    and str(record.get("message", "")).startswith("Storage job execution started:"),
                )
                wait_for_application_record(
                    log_path,
                    "S3 cleanup retains claimed Worker correlation",
                    lambda record: has_context(
                        record,
                        request_id=None,
                        instance_id=instance_id,
                        operation="storage_job_staging_cleanup",
                        upload_id=upload_id,
                        job_id=job_id,
                        lease_owner=instance_id,
                        state_version=None,
                    )
                    and record.get("message")
                    == "S3 SDK call result: sdk_operation=delete_objects, outcome=success",
                )
                wait_for_application_record(
                    log_path,
                    "Worker cleanup completion retains job identity and clears lease correlation",
                    lambda record: has_context(
                        record,
                        request_id=None,
                        instance_id=instance_id,
                        operation="storage_job_staging_cleanup",
                        upload_id=upload_id,
                        job_id=job_id,
                        lease_owner=None,
                        state_version=None,
                    )
                    and str(record.get("message", "")).startswith("Storage job execution completed:"),
                )

                soft_delete = http.request(
                    "DELETE",
                    "/api/file",
                    headers=auth_headers,
                    json={"file_ids": [file_id], "folder_ids": []},
                )
                soft_payload = require_envelope(soft_delete, 200, "soft delete")
                require(str(soft_payload["code"]) == "0", "soft delete keeps public success envelope")
                trash_row = query_one(
                    "SELECT id FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s",
                    (user_id, file_id),
                )
                require(trash_row is not None, "soft delete creates a trash row")
                trash_id = int(trash_row["id"])

                permanent = http.request(
                    "DELETE",
                    "/api/trash",
                    headers=auth_headers,
                    json={"trash_ids": [trash_id]},
                )
                permanent_payload = require_envelope(permanent, 200, "permanent delete")
                require(
                    permanent_payload["data"]["results"][0]["status"] == "success",
                    "permanent delete reports the existing batch result shape",
                )
                wait_for_object_state(s3_client, bucket, object_key, False)
                require(True, "permanent delete removes the zero-reference S3 object")
                file_id = None

                fault_payload = (f"disk-s3-compensation-{run_id}".encode() + bytes(range(128))) * 32
                fault_hash = hashlib.md5(fault_payload).hexdigest()
                fault_sha256 = hashlib.sha256(fault_payload).hexdigest()
                fault_name = f"s3_recovery_{run_id}.bin"
                baseline_quota = query_one(
                    "SELECT storage_reserved FROM users WHERE id = %s",
                    (user_id,),
                )
                require(baseline_quota is not None, "quota baseline exists before compensation test")
                under_baseline_reserved = int(baseline_quota["storage_reserved"])

                fault_init = http.post(
                    "/api/file/upload/init",
                    headers=auth_headers,
                    json={
                        "filename": fault_name,
                        "file_size": len(fault_payload),
                        "file_hash": fault_hash,
                        "parent_id": 0,
                    },
                )
                fault_init_payload = require_envelope(fault_init, 200, "compensation upload init")
                require(str(fault_init_payload["code"]) == "0", "compensation fixture init succeeds")
                require(not fault_init_payload["data"].get("instant_upload", False), "compensation fixture requires promotion")
                under_upload_id = str(fault_init_payload["data"]["upload_id"])

                fault_chunk = http.post(
                    "/api/file/upload/chunk",
                    params={
                        "upload_id": under_upload_id,
                        "chunk_index": 0,
                        "chunk_hash": fault_hash,
                    },
                    headers={**auth_headers, "Content-Type": "application/octet-stream"},
                    content=fault_payload,
                )
                fault_chunk_payload = require_envelope(fault_chunk, 200, "compensation upload chunk")
                require(fault_chunk_payload["data"]["uploaded"] is True, "compensation fixture chunk succeeds")

                expected_reserved = under_baseline_reserved + len(fault_payload)
                require(
                    execute(
                        "UPDATE users SET storage_reserved = %s WHERE id = %s AND storage_reserved = %s",
                        (len(fault_payload) - 1, user_id, expected_reserved),
                    )
                    == 1,
                    "fault injection creates an under-reservation after upload staging",
                )

                fault_complete = http.post(
                    "/api/file/upload/complete",
                    headers=auth_headers,
                    json={"upload_id": under_upload_id},
                )
                fault_complete_payload = require_envelope(fault_complete, 500, "compensation upload complete")
                require(str(fault_complete_payload["code"]) == "10006", "DB finalization failure keeps InternalError code")

                fault_key = f"{object_prefix}/sha256/{fault_sha256[:2]}/{fault_sha256}.bin"
                wait_for_object_state(s3_client, bucket, fault_key, True)
                require(True, "DB failure retains the promoted final object for reconciliation")
                require(
                    query_one("SELECT id FROM files WHERE user_id = %s AND name = %s", (user_id, fault_name)) is None,
                    "compensation failure creates no file row",
                )
                task_row = query_one(
                    "SELECT status, reserved_bytes FROM upload_tasks WHERE id = %s AND user_id = %s",
                    (under_upload_id, user_id),
                )
                require(
                    task_row is not None and int(task_row["status"]) == 4,
                    "failed finalization keeps the upload in Finalizing for lease recovery",
                )
                require(
                    bool(list_prefix_keys(s3_client, bucket, f"{staging_prefix}/{under_upload_id}/")),
                    "DB failure retains identifiable S3 staging objects",
                )

                remove_upload_records(under_upload_id, user_id, under_baseline_reserved)
                under_upload_id = None
                final_quota = query_one("SELECT storage_reserved FROM users WHERE id = %s", (user_id,))
                require(
                    final_quota is not None and int(final_quota["storage_reserved"]) == under_baseline_reserved,
                    "recovery fixture restores reserved quota",
                )

            print("PASS: Disk S3-native upload/download/delete/recovery flow succeeded")
            return 0
        except Exception as exc:
            print(f"FAIL: {exc}")
            return_code = 1
        finally:
            if server is not None and server.poll() is None and token:
                try:
                    with httpx.Client(base_url=base_url, timeout=10) as cleanup_http:
                        headers = {"Authorization": f"Bearer {token}"}
                        if under_upload_id is not None and user_id and under_baseline_reserved is not None:
                            remove_upload_records(under_upload_id, user_id, under_baseline_reserved)
                        if file_id is not None and user_id:
                            cleanup_http.request(
                                "DELETE",
                                "/api/file",
                                headers=headers,
                                json={"file_ids": [file_id], "folder_ids": []},
                            )
                            trash_row = query_one(
                                "SELECT id FROM trash WHERE user_id = %s AND item_type = 'file' AND item_id = %s",
                                (user_id, file_id),
                            )
                            if trash_row is not None:
                                cleanup_http.request(
                                    "DELETE",
                                    "/api/trash",
                                    headers=headers,
                                    json={"trash_ids": [int(trash_row["id"])]},
                                )
                except Exception as cleanup_exc:
                    print(f"WARN: application fixture cleanup failed: {cleanup_exc}")

            if primary_upload_id is not None and user_id:
                try:
                    remove_upload_records(primary_upload_id, user_id)
                except Exception as cleanup_exc:
                    print(f"WARN: upload record cleanup failed: {cleanup_exc}")

            try:
                remove_prefix(s3_client, bucket, object_prefix)
                remove_prefix(s3_client, bucket, staging_prefix)
            except Exception as cleanup_exc:
                print(f"WARN: S3 prefix cleanup failed: {cleanup_exc}")

            if server is not None and server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)
            if log_handle is not None:
                log_handle.close()

            if return_code != 0 and log_path.exists():
                log_text = log_path.read_text(encoding="utf-8", errors="replace")
                print(f"--- server log tail: {log_path} ---")
                print("\n".join(log_text.splitlines()[-120:]))

        return return_code


if __name__ == "__main__":
    sys.exit(main())

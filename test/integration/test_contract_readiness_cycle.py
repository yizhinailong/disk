#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "boto3",
#   "httpx",
#   "moto[server]==5.2.2",
#   "prometheus-client",
#   "psycopg[binary]",
# ]
# ///

"""Prove one natural S3 upload expiry/reclamation cycle before contract review."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import time
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

import boto3
import httpx
from botocore.config import Config
from moto.server import ThreadedMotoServer

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_expand_mixed_version import (  # noqa: E402
    INIT_SQL,
    ManagedServer,
    allocate_ports,
    connect,
    create_database,
    database_environment,
    drop_database,
    require,
    resolve_current_binary,
    run_sql_file,
    success_data,
)
from test_staging_canary import (  # noqa: E402
    initialize_upload,
    md5,
    process_config,
    process_environment,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/contract-readiness-cycle-summary.json"
CONTRACT_READINESS_SQL = REPO_ROOT / "deploy" / "contract-readiness.sql"
EXPIRY_SECONDS = 3
POLL_SECONDS = 0.1
PROCESS_TIMEOUT_SECONDS = 45
RECONCILIATION_TIMEOUT_SECONDS = 45
SCOPES = ("contents", "users", "staging", "final")
SEEDER_STARTED_MARKER = "Periodic storage job seeder started"


def wait_until(
    predicate: Callable[[], Any],
    timeout_seconds: float,
    label: str,
) -> Any:
    deadline = time.monotonic() + timeout_seconds
    last_error: BaseException | None = None
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except BaseException as error:  # Preserve transient dependency observations.
            last_error = error
        time.sleep(POLL_SECONDS)
    suffix = f": {last_error}" if last_error is not None else ""
    raise AssertionError(f"timed out waiting for {label}{suffix}")


def query_one(
    database_name: str,
    statement: str,
    parameters: tuple[Any, ...] = (),
) -> dict[str, Any]:
    with connect(database_name) as connection:
        row = connection.execute(statement, parameters).fetchone()
    require(row is not None, "database query returned no row")
    return dict(row)


def scalar(
    database_name: str,
    statement: str,
    parameters: tuple[Any, ...] = (),
) -> int:
    row = query_one(database_name, statement, parameters)
    require(len(row) == 1, "scalar query returned more than one column")
    return int(next(iter(row.values())))


def upload_single_chunk(
    client: httpx.Client,
    base_url: str,
    headers: dict[str, str],
    upload_id: str,
    payload: bytes,
    label: str,
) -> None:
    response = client.post(
        base_url + "/api/file/upload/chunk",
        params={
            "upload_id": upload_id,
            "chunk_index": 0,
            "chunk_hash": md5(payload),
        },
        headers={**headers, "Content-Type": "application/octet-stream"},
        content=payload,
    )
    data = success_data(response, f"{label} chunk")
    require(data.get("uploaded") is True, f"{label} chunk was not persisted")


def complete_upload(
    client: httpx.Client,
    base_url: str,
    headers: dict[str, str],
    upload_id: str,
) -> int:
    response = client.post(
        base_url + "/api/file/upload/complete",
        headers=headers,
        json={"upload_id": upload_id},
    )
    data = success_data(response, "post-cycle upload completion")
    file_data = data.get("file")
    require(isinstance(file_data, dict), "completion response omitted file data")
    file_id = file_data.get("id")
    require(
        isinstance(file_id, int) and file_id > 0, "completion response omitted file ID"
    )
    return file_id


def list_keys(s3: Any, bucket: str, prefix: str) -> list[str]:
    keys: list[str] = []
    continuation_token: str | None = None
    while True:
        request: dict[str, Any] = {"Bucket": bucket, "Prefix": prefix}
        if continuation_token is not None:
            request["ContinuationToken"] = continuation_token
        response = s3.list_objects_v2(**request)
        keys.extend(str(item["Key"]) for item in response.get("Contents", []))
        if not response.get("IsTruncated"):
            return keys
        continuation_token = response.get("NextContinuationToken")
        require(
            isinstance(continuation_token, str) and continuation_token,
            "S3 inventory pagination omitted its continuation token",
        )


def read_log(server: ManagedServer) -> str:
    if server.log_handle is not None:
        server.log_handle.flush()
    return server.log_path.read_text(encoding="utf-8", errors="replace")


def wait_for_database_expiry(
    database_name: str, upload_id: str
) -> dict[str, Any] | None:
    row = query_one(
        database_name,
        """
        SELECT created_at, expires_at, clock_timestamp()::timestamp AS observed_at,
               EXTRACT(EPOCH FROM (expires_at - created_at)) AS ttl_seconds,
               EXTRACT(EPOCH FROM (clock_timestamp()::timestamp - created_at))
                   AS elapsed_seconds,
               clock_timestamp()::timestamp > expires_at AS expired
        FROM upload_tasks WHERE id = %s
        """,
        (upload_id,),
    )
    return row if row["expired"] else None


def wait_for_expiry_cleanup(
    database_name: str,
    upload_id: str,
    worker: ManagedServer,
) -> dict[str, Any] | None:
    worker.require_running("natural upload expiration")
    with connect(database_name) as connection:
        task = connection.execute(
            """
            SELECT status, expires_at, lease_owner, lease_expires_at
            FROM upload_tasks WHERE id = %s
            """,
            (upload_id,),
        ).fetchone()
        cleanup = connection.execute(
            """
            SELECT status, attempts, payload, created_at, completed_at
            FROM storage_jobs
            WHERE job_type = 'staging_cleanup' AND aggregate_id = %s
            """,
            (upload_id,),
        ).fetchall()
        expire_jobs = connection.execute(
            """
            SELECT status, attempts, created_at, completed_at, payload
            FROM storage_jobs WHERE job_type = 'expire_uploads' ORDER BY id
            """
        ).fetchall()
        chunks = connection.execute(
            "SELECT COUNT(*) AS count FROM upload_task_chunks WHERE task_id = %s",
            (upload_id,),
        ).fetchone()
        quota = connection.execute(
            "SELECT storage_used, storage_reserved FROM users WHERE username = 'admin'"
        ).fetchone()
    if (
        task is None
        or task["status"] != 3
        or len(cleanup) != 1
        or cleanup[0]["status"] != 3
        or len(expire_jobs) != 1
        or expire_jobs[0]["status"] != 3
        or chunks is None
        or int(chunks["count"]) != 0
        or quota is None
        or int(quota["storage_reserved"]) != 0
    ):
        return None
    return {
        "task": dict(task),
        "cleanup": dict(cleanup[0]),
        "expire_job": dict(expire_jobs[0]),
        "quota": dict(quota),
    }


def wait_for_cleanup_job(
    database_name: str,
    upload_id: str,
    worker: ManagedServer,
) -> dict[str, Any] | None:
    worker.require_running("post-cycle staging cleanup")
    with connect(database_name) as connection:
        rows = connection.execute(
            """
            SELECT status, attempts, payload, completed_at
            FROM storage_jobs
            WHERE job_type = 'staging_cleanup' AND aggregate_id = %s
            """,
            (upload_id,),
        ).fetchall()
    if len(rows) != 1 or rows[0]["status"] != 3:
        return None
    return dict(rows[0])


def enqueue_reconciliation(
    client: httpx.Client,
    base_url: str,
    headers: dict[str, str],
    scan_id: str,
) -> None:
    path = f"/api/admin/storage-reconciliation/{scan_id}/enqueue"
    for scope in SCOPES:
        dry_run = client.post(
            base_url + path,
            headers=headers,
            json={"scope": scope},
        )
        dry_data = success_data(dry_run, f"{scope} reconciliation dry-run")
        require(
            dry_data.get("eligible") is True, f"{scope} reconciliation was not eligible"
        )
        require(dry_data.get("enqueued") is False, f"{scope} dry-run mutated the queue")

        execute = client.post(
            base_url + path,
            headers=headers,
            json={
                "scope": scope,
                "dry_run": False,
                "confirm_scan_id": scan_id,
                "reason": f"contract readiness {scope} verification",
            },
        )
        execute_data = success_data(execute, f"{scope} reconciliation enqueue")
        require(execute_data.get("enqueued") is True, f"{scope} scan was not enqueued")
        require(
            execute_data.get("job_status") == "pending", f"{scope} scan was not Pending"
        )


def wait_for_reconciliation(
    database_name: str,
    scan_id: str,
    worker: ManagedServer,
) -> list[dict[str, Any]] | None:
    worker.require_running("contract-readiness reconciliation")
    with connect(database_name) as connection:
        rows = [
            dict(row)
            for row in connection.execute(
                """
                SELECT status, attempts, last_error, payload
                FROM storage_jobs
                WHERE job_type = 'storage_reconcile' AND aggregate_id = %s
                ORDER BY id
                """,
                (scan_id,),
            ).fetchall()
        ]
    require(
        not any(row["status"] == 4 for row in rows), "reconciliation entered DeadLetter"
    )
    scopes = {str(row["payload"]["scope"]) for row in rows}
    if scopes != set(SCOPES) or not rows or any(row["status"] != 3 for row in rows):
        return None
    return rows


def final_blockers(
    database_name: str,
    cutover_at: datetime,
    scan_id: str,
) -> dict[str, int]:
    statements: dict[str, tuple[str, tuple[Any, ...]]] = {
        "pre_cutover_active": (
            "SELECT COUNT(*) FROM upload_tasks WHERE created_at <= %s AND status IN (0, 4)",
            (cutover_at,),
        ),
        "local_nonterminal": (
            "SELECT COUNT(*) FROM upload_tasks "
            "WHERE staging_backend = 'local' AND status IN (0, 4)",
            (),
        ),
        "local_cleanup_incomplete": (
            "SELECT COUNT(*) FROM storage_jobs WHERE job_type = 'staging_cleanup' "
            "AND payload->>'backend' = 'local' AND status <> 3",
            (),
        ),
        "active_upload_leases": (
            "SELECT COUNT(*) FROM upload_tasks WHERE status = 4 "
            "OR lease_owner IS NOT NULL OR lease_expires_at IS NOT NULL",
            (),
        ),
        "null_staging_prefix": (
            "SELECT COUNT(*) FROM upload_tasks WHERE staging_prefix IS NULL",
            (),
        ),
        "nullable_chunk_compat_fields": (
            "SELECT COUNT(*) FROM upload_task_chunks WHERE size_bytes IS NULL "
            "OR hash_md5 IS NULL OR object_key IS NULL",
            (),
        ),
        "unfinished_upload_jobs": (
            "SELECT COUNT(*) FROM storage_jobs WHERE job_type IN "
            "('expire_uploads', 'staging_cleanup', 'multipart_abort') "
            "AND status IN (0, 1, 2, 4)",
            (),
        ),
        "unfinished_reconciliation_jobs": (
            "SELECT COUNT(*) FROM storage_jobs WHERE job_type = 'storage_reconcile' "
            "AND aggregate_id = %s AND status <> 3",
            (scan_id,),
        ),
        "unfinished_jobs_all_types": (
            "SELECT COUNT(*) FROM storage_jobs WHERE status <> 3",
            (),
        ),
        "unresolved_findings": (
            "SELECT COUNT(*) FROM storage_reconciliation_findings WHERE resolved_at IS NULL",
            (),
        ),
        "quota_mismatches": (
            """
            SELECT COUNT(*) FROM users
            WHERE storage_used <>
                    (COALESCE((SELECT SUM(size) FROM files
                               WHERE user_id = users.id), 0) +
                     COALESCE((SELECT SUM(item_size) FROM trash
                               WHERE user_id = users.id), 0))
               OR storage_reserved <>
                    COALESCE((SELECT SUM(reserved_bytes) FROM upload_tasks
                              WHERE user_id = users.id AND status IN (0, 4)), 0)
            """,
            (),
        ),
        "content_ref_count_mismatches": (
            """
            SELECT COUNT(*) FROM file_contents AS content
            WHERE content.ref_count <>
                ((SELECT COUNT(*) FROM files WHERE content_id = content.id) +
                 (SELECT COUNT(*) FROM trash
                  WHERE content_id = content.id AND item_type = 'file'))
            """,
            (),
        ),
    }
    return {
        name: scalar(database_name, statement, parameters)
        for name, (statement, parameters) in statements.items()
    }


def contract_state_fingerprint(database_name: str) -> dict[str, str]:
    return query_one(
        database_name,
        """
        SELECT
            (SELECT MD5(COALESCE(STRING_AGG(TO_JSONB(row_data)::text, E'\\n' ORDER BY id), ''))
             FROM upload_tasks AS row_data) AS upload_tasks,
            (SELECT MD5(COALESCE(STRING_AGG(TO_JSONB(row_data)::text, E'\\n'
                                            ORDER BY task_id, chunk_index), ''))
             FROM upload_task_chunks AS row_data) AS upload_task_chunks,
            (SELECT MD5(COALESCE(STRING_AGG(TO_JSONB(row_data)::text, E'\\n' ORDER BY id), ''))
             FROM storage_jobs AS row_data) AS storage_jobs,
            (SELECT MD5(COALESCE(STRING_AGG(TO_JSONB(row_data)::text, E'\\n' ORDER BY id), ''))
             FROM storage_reconciliation_findings AS row_data) AS findings,
            (SELECT MD5(COALESCE(STRING_AGG(TO_JSONB(row_data)::text, E'\\n' ORDER BY id), ''))
             FROM users AS row_data) AS users,
            (SELECT MD5(COALESCE(STRING_AGG(TO_JSONB(row_data)::text, E'\\n' ORDER BY id), ''))
             FROM files AS row_data) AS files,
            (SELECT MD5(COALESCE(STRING_AGG(TO_JSONB(row_data)::text, E'\\n' ORDER BY id), ''))
             FROM file_contents AS row_data) AS file_contents,
            (SELECT MD5(COALESCE(STRING_AGG(TO_JSONB(row_data)::text, E'\\n' ORDER BY id), ''))
             FROM trash AS row_data) AS trash
        """,
    )


def run_contract_readiness_sql(
    database_name: str,
    variables: dict[str, str],
) -> subprocess.CompletedProcess[str]:
    command = ["psql", "-X", "-qAt", "-v", "ON_ERROR_STOP=1"]
    for name, value in variables.items():
        command.extend(("-v", f"{name}={value}"))
    command.extend(("-f", str(CONTRACT_READINESS_SQL)))
    return subprocess.run(
        command,
        cwd=REPO_ROOT,
        env=database_environment(database_name),
        check=False,
        capture_output=True,
        text=True,
    )


def executable_contract_snapshot(
    database_name: str,
    cutover_at: datetime,
    scan_id: str,
    *,
    verify_missing_inputs: bool = True,
) -> dict[str, Any]:
    variables = {
        "t_s3_only": iso_timestamp(cutover_at),
        "scan_id": scan_id,
    }
    if verify_missing_inputs:
        for omitted_name in variables:
            partial_variables = {
                name: value for name, value in variables.items() if name != omitted_name
            }
            result = run_contract_readiness_sql(database_name, partial_variables)
            require(
                result.returncode == 3,
                f"contract readiness accepted missing {omitted_name}: {result.stderr}",
            )
            require(
                f"{omitted_name} is required" in result.stdout + result.stderr,
                f"missing {omitted_name} error drifted",
            )

    result = run_contract_readiness_sql(database_name, variables)
    require(
        result.returncode == 0,
        f"contract readiness SQL failed: {result.stdout}\n{result.stderr}",
    )
    output_lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    require(len(output_lines) == 1, "contract readiness SQL must emit one JSON line")
    snapshot = json.loads(output_lines[0])
    require(isinstance(snapshot, dict), "contract readiness output is not an object")
    return snapshot


def iso_timestamp(value: datetime) -> str:
    return value.isoformat(timespec="milliseconds")


def main() -> int:
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_contract_cycle_{suffix}"
    bucket = "disk-contract-cycle"
    object_prefix = f"objects/contract-{suffix}"
    staging_prefix = f"staging/contract-{suffix}"
    database_created = False
    moto: ThreadedMotoServer | None = None
    api: ManagedServer | None = None
    worker: ManagedServer | None = None
    started_at = time.monotonic()

    try:
        binary = resolve_current_binary()
        api_port, worker_port, moto_port = allocate_ports(3)
        endpoint = f"http://127.0.0.1:{moto_port}"

        moto = ThreadedMotoServer(ip_address="127.0.0.1", port=moto_port, verbose=False)
        moto.start()
        s3 = boto3.client(
            "s3",
            endpoint_url=endpoint,
            aws_access_key_id="disk-contract-cycle",
            aws_secret_access_key="disk-contract-cycle-secret",
            region_name="us-east-1",
            config=Config(s3={"addressing_style": "path"}),
        )
        s3.create_bucket(Bucket=bucket)

        with tempfile.TemporaryDirectory(prefix="disk-contract-cycle-") as temporary:
            root = Path(temporary)
            create_database(database_name)
            database_created = True
            run_sql_file(database_name, INIT_SQL)

            api_config = process_config(
                database_name,
                api_port,
                f"contract-api-{suffix}",
                "api",
                root / "unused-api-final",
                root / "unused-api-staging",
                endpoint,
                bucket,
                object_prefix,
                staging_prefix,
                "s3",
            )
            api_config["custom_config"]["disk"]["upload_task_expiry_seconds"] = (
                EXPIRY_SECONDS
            )
            api = ManagedServer(
                name=f"contract-api-{suffix}",
                binary=binary,
                run_directory=root / "api-run",
                config=api_config,
                database_name=database_name,
                port=api_port,
                readiness_path="/api/health/ready",
                role="api",
                environment_overrides=process_environment(endpoint, bucket, "s3"),
            )

            cutover_at = query_one(
                database_name,
                "SELECT clock_timestamp()::timestamp AS cutover_at",
            )["cutover_at"]
            require(
                isinstance(cutover_at, datetime), "database cutover time is invalid"
            )
            cutover_wall = time.monotonic()

            expired_payload = b"contract-readiness-expired-probe-v1" * 128
            completed_payload = b"contract-readiness-post-cycle-upload-v1" * 128
            with httpx.Client(timeout=60) as client:
                login = client.post(
                    api.base_url + "/api/auth/login",
                    json={"account": "admin", "password": "Admin123"},
                )
                token = success_data(login, "admin login").get("access_token")
                require(isinstance(token, str) and token, "login omitted access token")
                headers = {"Authorization": f"Bearer {token}"}

                expired_upload_id = initialize_upload(
                    client,
                    api.base_url,
                    headers,
                    f"contract-expired-{suffix}.bin",
                    expired_payload,
                )
                upload_single_chunk(
                    client,
                    api.base_url,
                    headers,
                    expired_upload_id,
                    expired_payload,
                    "expiry probe",
                )

                initial = query_one(
                    database_name,
                    """
                    SELECT task.created_at, task.expires_at, task.staging_backend,
                           task.staging_prefix, task.status, task.reserved_bytes,
                           chunk.size_bytes, chunk.hash_md5, chunk.object_key, chunk.etag,
                           users.storage_used, users.storage_reserved
                    FROM upload_tasks AS task
                    JOIN upload_task_chunks AS chunk ON chunk.task_id = task.id
                    JOIN users ON users.id = task.user_id
                    WHERE task.id = %s AND chunk.chunk_index = 0
                    """,
                    (expired_upload_id,),
                )
                expected_expired_prefix = f"{staging_prefix}/{expired_upload_id}"
                expected_chunk_key = (
                    f"{expected_expired_prefix}/chunks/0-{md5(expired_payload)}.part"
                )
                require(
                    initial["created_at"] > cutover_at,
                    "probe was not created after S3-only cutover",
                )
                require(
                    initial["staging_backend"] == "s3",
                    "probe did not persist S3 staging",
                )
                require(
                    initial["staging_prefix"] == expected_expired_prefix,
                    "probe prefix drifted",
                )
                require(initial["status"] == 0, "probe was not InProgress")
                require(
                    initial["reserved_bytes"] == len(expired_payload),
                    "task reservation drifted",
                )
                require(initial["storage_used"] == 0, "probe changed used quota")
                require(
                    initial["storage_reserved"] == len(expired_payload),
                    "probe reservation missing",
                )
                require(
                    initial["size_bytes"] == len(expired_payload),
                    "chunk size metadata drifted",
                )
                require(
                    initial["hash_md5"] == md5(expired_payload),
                    "chunk MD5 metadata drifted",
                )
                require(
                    initial["object_key"] == expected_chunk_key,
                    "chunk key metadata drifted",
                )
                require(
                    isinstance(initial["etag"], str) and initial["etag"],
                    "S3 ETag is missing",
                )
                require(
                    list_keys(s3, bucket, expected_expired_prefix + "/")
                    == [expected_chunk_key],
                    "expiry probe S3 inventory is not exact",
                )
                require(
                    s3.get_object(Bucket=bucket, Key=expected_chunk_key)["Body"].read()
                    == expired_payload,
                    "expiry probe S3 bytes changed",
                )
                require(
                    scalar(database_name, "SELECT COUNT(*) FROM storage_jobs") == 0,
                    "API created persistent jobs before Worker admission",
                )

                expiry_observation = wait_until(
                    lambda: wait_for_database_expiry(database_name, expired_upload_id),
                    PROCESS_TIMEOUT_SECONDS,
                    "PostgreSQL natural upload expiry",
                )
                wall_expiry_elapsed = time.monotonic() - cutover_wall
                require(
                    float(expiry_observation["ttl_seconds"]) == EXPIRY_SECONDS,
                    "persisted upload TTL differs from configuration",
                )
                require(
                    float(expiry_observation["elapsed_seconds"]) >= EXPIRY_SECONDS,
                    "database observation did not span the configured TTL",
                )
                require(
                    wall_expiry_elapsed >= EXPIRY_SECONDS,
                    "wall-clock observation did not span the configured TTL",
                )

                worker_config = process_config(
                    database_name,
                    worker_port,
                    f"contract-worker-{suffix}",
                    "worker",
                    root / "unused-worker-final",
                    root / "unused-worker-staging",
                    endpoint,
                    bucket,
                    object_prefix,
                    staging_prefix,
                    "s3",
                )
                worker_config["custom_config"]["disk"]["upload_task_expiry_seconds"] = (
                    EXPIRY_SECONDS
                )
                worker = ManagedServer(
                    name=f"contract-worker-{suffix}",
                    binary=binary,
                    run_directory=root / "worker-run",
                    config=worker_config,
                    database_name=database_name,
                    port=worker_port,
                    readiness_path="/api/health/ready",
                    role="worker",
                    environment_overrides=process_environment(endpoint, bucket, "s3"),
                )

                expired = wait_until(
                    lambda: wait_for_expiry_cleanup(
                        database_name, expired_upload_id, worker
                    ),
                    PROCESS_TIMEOUT_SECONDS,
                    "Worker expiration and S3 cleanup",
                )
                require(
                    expired["task"]["expires_at"] == initial["expires_at"],
                    "expires_at changed",
                )
                require(
                    expired["task"]["lease_owner"] is None,
                    "expired task retained lease owner",
                )
                require(
                    expired["task"]["lease_expires_at"] is None,
                    "expired task retained lease",
                )
                require(
                    expired["cleanup"]["attempts"] == 1,
                    "cleanup did not execute exactly once",
                )
                require(
                    expired["cleanup"]["payload"]["backend"] == "s3",
                    "cleanup backend drifted",
                )
                require(
                    expired["cleanup"]["payload"]["prefix"] == expected_expired_prefix,
                    "cleanup prefix drifted",
                )
                require(expired["expire_job"]["attempts"] == 1, "expire scan retried")
                require(
                    expired["expire_job"]["created_at"] > initial["expires_at"],
                    "expire scan was not created after the probe expired",
                )
                wait_until(
                    lambda: not list_keys(s3, bucket, expected_expired_prefix + "/"),
                    PROCESS_TIMEOUT_SECONDS,
                    "expired probe prefix deletion",
                )

                completed_upload_id = initialize_upload(
                    client,
                    api.base_url,
                    headers,
                    f"contract-completed-{suffix}.bin",
                    completed_payload,
                )
                upload_single_chunk(
                    client,
                    api.base_url,
                    headers,
                    completed_upload_id,
                    completed_payload,
                    "post-cycle upload",
                )
                file_id = complete_upload(
                    client,
                    api.base_url,
                    headers,
                    completed_upload_id,
                )
                download = client.get(
                    f"{api.base_url}/api/file/download/{file_id}",
                    headers=headers,
                )
                require(download.status_code == 200, "post-cycle download failed")
                require(
                    download.content == completed_payload,
                    "post-cycle download bytes changed",
                )

                completed_prefix = f"{staging_prefix}/{completed_upload_id}"
                completed_cleanup = wait_until(
                    lambda: wait_for_cleanup_job(
                        database_name, completed_upload_id, worker
                    ),
                    PROCESS_TIMEOUT_SECONDS,
                    "post-cycle staging cleanup",
                )
                require(
                    completed_cleanup["attempts"] == 1, "post-cycle cleanup retried"
                )
                wait_until(
                    lambda: not list_keys(s3, bucket, completed_prefix + "/"),
                    PROCESS_TIMEOUT_SECONDS,
                    "post-cycle staging prefix deletion",
                )

                final_sha256 = hashlib.sha256(completed_payload).hexdigest()
                final_key = (
                    f"{object_prefix}/sha256/{final_sha256[:2]}/{final_sha256}.bin"
                )
                require(
                    s3.get_object(Bucket=bucket, Key=final_key)["Body"].read()
                    == completed_payload,
                    "post-cycle final object bytes changed",
                )

                scan_id = f"contract-readiness-{suffix}"
                enqueue_reconciliation(client, api.base_url, headers, scan_id)
                reconciliation_rows = wait_until(
                    lambda: wait_for_reconciliation(database_name, scan_id, worker),
                    RECONCILIATION_TIMEOUT_SECONDS,
                    "four-scope contract-readiness reconciliation",
                )

            task_summary = query_one(
                database_name,
                """
                SELECT COUNT(*) AS total,
                       COUNT(*) FILTER (WHERE staging_backend = 's3') AS s3_tasks,
                       COUNT(*) FILTER (WHERE staging_backend = 'local') AS local_tasks,
                       COUNT(*) FILTER (WHERE status = 1) AS completed,
                       COUNT(*) FILTER (WHERE status = 3) AS expired
                FROM upload_tasks
                """,
            )
            data_summary = query_one(
                database_name,
                """
                SELECT (SELECT COUNT(*) FROM files) AS files,
                       (SELECT COUNT(*) FROM file_contents) AS contents,
                       (SELECT COALESCE(SUM(ref_count), 0) FROM file_contents) AS ref_count,
                       (SELECT COUNT(*) FROM upload_task_chunks) AS chunks,
                       (SELECT storage_used FROM users WHERE username = 'admin') AS storage_used,
                       (SELECT storage_reserved FROM users WHERE username = 'admin')
                           AS storage_reserved,
                       (SELECT COUNT(*) FROM storage_jobs
                        WHERE job_type = 'staging_cleanup') AS cleanup_jobs,
                       (SELECT COUNT(*) FROM storage_jobs
                        WHERE job_type = 'staging_cleanup' AND status = 3)
                           AS cleanup_succeeded
                """,
            )
            require(
                task_summary
                == {
                    "total": 2,
                    "s3_tasks": 2,
                    "local_tasks": 0,
                    "completed": 1,
                    "expired": 1,
                },
                f"upload terminal summary drifted: {task_summary}",
            )
            require(data_summary["files"] == 1, "post-cycle file uniqueness changed")
            require(
                data_summary["contents"] == 1, "post-cycle content uniqueness changed"
            )
            require(data_summary["ref_count"] == 1, "post-cycle ref_count changed")
            require(data_summary["chunks"] == 0, "terminal chunk rows remain")
            require(
                data_summary["storage_used"] == len(completed_payload),
                "used quota changed",
            )
            require(data_summary["storage_reserved"] == 0, "reserved quota remains")
            require(data_summary["cleanup_jobs"] == 2, "cleanup job uniqueness changed")
            require(
                data_summary["cleanup_succeeded"] == 2, "cleanup jobs did not converge"
            )

            blockers = final_blockers(database_name, cutover_at, scan_id)
            require(
                all(value == 0 for value in blockers.values()),
                f"contract blockers remain: {blockers}",
            )
            page_counts = {
                scope: sum(
                    1 for row in reconciliation_rows if row["payload"]["scope"] == scope
                )
                for scope in SCOPES
            }
            require(
                all(count >= 1 for count in page_counts.values()),
                "a reconciliation scope is missing",
            )
            require(
                all(row["attempts"] == 1 for row in reconciliation_rows),
                "contract reconciliation retried",
            )

            readiness_sql = CONTRACT_READINESS_SQL.read_text(encoding="utf-8")
            normalized_readiness_sql = readiness_sql.upper()
            require(
                normalized_readiness_sql.count(
                    "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY"
                )
                == 1
                and normalized_readiness_sql.count("COMMIT;") == 1,
                "contract readiness transaction boundary drifted",
            )
            for forbidden_statement in (
                "INSERT INTO",
                "UPDATE ",
                "DELETE FROM",
                "MERGE INTO",
                "ALTER ",
                "DROP ",
                "TRUNCATE ",
                "CREATE ",
                "GRANT ",
                "REVOKE ",
            ):
                require(
                    forbidden_statement not in normalized_readiness_sql,
                    f"contract readiness SQL contains {forbidden_statement.strip()}",
                )

            state_before_snapshot = contract_state_fingerprint(database_name)
            executable_snapshot = executable_contract_snapshot(
                database_name,
                cutover_at,
                scan_id,
            )
            incomplete_snapshot = executable_contract_snapshot(
                database_name,
                cutover_at,
                f"{scan_id}-not-completed",
                verify_missing_inputs=False,
            )
            state_after_snapshot = contract_state_fingerprint(database_name)
            require(
                state_after_snapshot == state_before_snapshot,
                "contract readiness SQL changed application state",
            )
            require(
                executable_snapshot.get("schema_version") == 1,
                "contract readiness schema version drifted",
            )
            inputs = executable_snapshot.get("inputs")
            require(
                isinstance(inputs, dict) and inputs.get("scan_id") == scan_id,
                "contract readiness inputs drifted",
            )
            require(
                executable_snapshot.get("blockers") == blockers,
                "executable blocker counts differ from independent queries",
            )
            reconciliation_snapshot = executable_snapshot.get("reconciliation")
            expected_scopes = {
                scope: {"pages": count, "all_succeeded": True}
                for scope, count in page_counts.items()
            }
            require(
                isinstance(reconciliation_snapshot, dict)
                and reconciliation_snapshot.get("scope_count") == len(SCOPES)
                and reconciliation_snapshot.get("required_scope_count")
                == len(SCOPES)
                and reconciliation_snapshot.get("all_pages_succeeded") is True
                and reconciliation_snapshot.get("scopes") == expected_scopes,
                "contract readiness reconciliation summary drifted",
            )
            require(
                executable_snapshot.get("contract_design_review_admitted") is True
                and executable_snapshot.get("compatibility_removal_allowed") is False,
                "contract readiness exceeded design-review authority",
            )
            incomplete_reconciliation = incomplete_snapshot.get("reconciliation")
            require(
                isinstance(incomplete_reconciliation, dict)
                and incomplete_reconciliation.get("scope_count") == 0
                and incomplete_snapshot.get("contract_design_review_admitted") is False
                and incomplete_snapshot.get("compatibility_removal_allowed") is False,
                "incomplete reconciliation evidence admitted compatibility retirement",
            )

            staging_inventory = list_keys(s3, bucket, staging_prefix + "/")
            final_inventory = list_keys(s3, bucket, object_prefix + "/")
            require(not staging_inventory, "S3 staging inventory is not empty")
            require(final_inventory == [final_key], "S3 final inventory is not exact")

            api_log = read_log(api)
            worker_log = read_log(worker)
            require(
                SEEDER_STARTED_MARKER not in api_log, "API started a periodic seeder"
            )
            require(
                worker_log.count(SEEDER_STARTED_MARKER) == 1,
                "Worker did not own exactly one periodic seeder",
            )
            api.require_running("contract-readiness acceptance")
            worker.require_running("contract-readiness acceptance")

            evidence = {
                "schema_version": 1,
                "scenario": "contract_readiness_natural_s3_expiry_cycle",
                "observation": {
                    "cutover_database_time": iso_timestamp(cutover_at),
                    "probe_created_database_time": iso_timestamp(initial["created_at"]),
                    "probe_expires_database_time": iso_timestamp(initial["expires_at"]),
                    "expiry_observed_database_time": iso_timestamp(
                        expiry_observation["observed_at"]
                    ),
                    "configured_ttl_seconds": EXPIRY_SECONDS,
                    "persisted_ttl_seconds": float(expiry_observation["ttl_seconds"]),
                    "database_elapsed_seconds": round(
                        float(expiry_observation["elapsed_seconds"]), 3
                    ),
                    "wall_clock_elapsed_seconds": round(wall_expiry_elapsed, 3),
                    "expires_at_unchanged": True,
                    "worker_admitted_after_database_expiry": True,
                    "expire_scan_created_after_expiry": True,
                },
                "expiration": {
                    "terminal_status": "expired",
                    "expire_scan_attempts": int(expired["expire_job"]["attempts"]),
                    "cleanup_status": "succeeded",
                    "cleanup_attempts": int(expired["cleanup"]["attempts"]),
                    "remaining_chunk_rows": 0,
                    "remaining_staging_objects": 0,
                    "released_reserved_bytes": len(expired_payload),
                },
                "post_cycle_upload": {
                    "terminal_status": "completed",
                    "bytes": len(completed_payload),
                    "download_matches": True,
                    "cleanup_status": "succeeded",
                    "files": int(data_summary["files"]),
                    "contents": int(data_summary["contents"]),
                    "ref_count": int(data_summary["ref_count"]),
                    "storage_used": int(data_summary["storage_used"]),
                    "storage_reserved": int(data_summary["storage_reserved"]),
                },
                "reconciliation": {
                    "scopes": page_counts,
                    "all_pages_succeeded": True,
                    "all_pages_single_attempt": True,
                    "unresolved_findings": blockers["unresolved_findings"],
                },
                "contract_blockers": blockers,
                "executable_snapshot": {
                    "schema_version": int(executable_snapshot["schema_version"]),
                    "missing_inputs_rejected": True,
                    "incomplete_evidence_blocked": True,
                    "independent_blockers_match": True,
                    "state_unchanged": True,
                    "design_review_admitted": True,
                    "compatibility_removal_allowed": False,
                },
                "ownership": {
                    "api_periodic_seeders": 0,
                    "worker_periodic_seeders": 1,
                },
                "object_inventory": {
                    "staging_objects": len(staging_inventory),
                    "final_objects": len(final_inventory),
                },
                "acceptance": {
                    "passed": True,
                    "contract_review_admitted": True,
                    "contract_migration_executed": False,
                },
                "total_elapsed_seconds": round(time.monotonic() - started_at, 3),
            }
            serialized = json.dumps(evidence, indent=2) + "\n"
            for sensitive in (
                endpoint,
                database_name,
                token,
                expired_upload_id,
                completed_upload_id,
                expected_expired_prefix,
                completed_prefix,
                expected_chunk_key,
                final_key,
                "disk-contract-cycle-secret",
            ):
                require(
                    sensitive not in serialized,
                    "evidence contains a sensitive locator or credential",
                )
            EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
            EVIDENCE_PATH.write_text(serialized, encoding="utf-8")

            worker.stop()
            worker = None
            api.stop()
            api = None

        print(
            "PASS: natural S3 expiry, Worker reclamation, post-cycle upload, and "
            "four-scope reconciliation admitted contract review without executing DDL"
        )
        return 0
    except BaseException:
        for process in (api, worker):
            if process is not None:
                print(process.log_tail(), file=sys.stderr)
        raise
    finally:
        for process in (worker, api):
            if process is not None:
                process.stop()
        if database_created:
            drop_database(database_name)
        if moto is not None:
            moto.stop()


if __name__ == "__main__":
    raise SystemExit(main())

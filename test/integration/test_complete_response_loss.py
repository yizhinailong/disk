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

"""Recover a committed S3 upload after the completing API loses its response."""

from __future__ import annotations

import hashlib
import json
import os
import queue
import sys
import tempfile
import threading
import time
import uuid
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
    drop_database,
    require,
    resolve_current_binary,
    run_sql_file,
    success_data,
)
from test_staging_canary import (  # noqa: E402
    UPLOAD_SIZE,
    initialize_upload,
    process_config,
    process_environment,
    upload_chunks,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/complete-response-loss-summary.json"
BUCKET = "disk-complete-response-loss"


def wait_until(
    description: str,
    predicate: Callable[[], Any],
    *,
    timeout_seconds: float,
) -> Any:
    deadline = time.monotonic() + timeout_seconds
    last_value: Any = None
    while time.monotonic() < deadline:
        last_value = predicate()
        if last_value:
            return last_value
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {description}: last={last_value}")


def read_log(server: ManagedServer) -> str:
    if server.log_handle is not None:
        server.log_handle.flush()
    return server.log_path.read_text(encoding="utf-8", errors="replace")


def task_snapshot(database_name: str, upload_id: str) -> dict[str, Any]:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT status, state_version, finalize_attempts, completed_file_id, "
            "lease_owner, lease_expires_at, finalized_at, staging_backend, "
            "staging_prefix, updated_at FROM upload_tasks WHERE id = %s",
            (upload_id,),
        ).fetchone()
    require(row is not None, "upload task disappeared")
    return dict(row)


def quota_snapshot(database_name: str) -> dict[str, int]:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT storage_used, storage_reserved FROM users WHERE username = 'admin'"
        ).fetchone()
    require(row is not None, "admin quota row is unavailable")
    return {
        "storage_used": int(row["storage_used"]),
        "storage_reserved": int(row["storage_reserved"]),
    }


def admin_user_id(database_name: str) -> int:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT id FROM users WHERE username = 'admin'"
        ).fetchone()
    require(row is not None, "admin user row is unavailable")
    return int(row["id"])


def side_effect_snapshot(
    database_name: str,
    user_id: int,
    upload_id: str,
    filename: str,
    md5_hash: str,
    sha256_hash: str,
    completed_file_id: int,
) -> dict[str, int]:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT "
            "(SELECT COUNT(*) FROM files WHERE user_id = %s AND name = %s) AS files, "
            "(SELECT COUNT(*) FROM file_contents "
            " WHERE hash_md5 = %s AND hash_sha256 = %s) AS contents, "
            "(SELECT ref_count FROM file_contents AS content "
            " JOIN files AS file ON file.content_id = content.id "
            " WHERE file.id = %s) AS ref_count, "
            "(SELECT COUNT(*) FROM upload_task_chunks WHERE task_id = %s) AS chunks, "
            "(SELECT COUNT(*) FROM storage_jobs WHERE dedupe_key = %s) AS cleanup_jobs",
            (
                user_id,
                filename,
                md5_hash,
                sha256_hash,
                completed_file_id,
                upload_id,
                f"staging-cleanup:{upload_id}",
            ),
        ).fetchone()
    require(row is not None, "upload side-effect query returned no row")
    return {name: int(row[name]) for name in row.keys()}


def cleanup_job_snapshot(database_name: str, upload_id: str) -> dict[str, Any]:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT status, attempts, locked_by, locked_until, completed_at "
            "FROM storage_jobs WHERE dedupe_key = %s",
            (f"staging-cleanup:{upload_id}",),
        ).fetchone()
    require(row is not None, "staging cleanup job is unavailable")
    return dict(row)


def current_keys(s3: Any, key_prefix: str) -> list[str]:
    response = s3.list_objects_v2(Bucket=BUCKET, Prefix=key_prefix)
    return sorted(str(item["Key"]) for item in response.get("Contents", []))


def version_snapshot(s3: Any, key: str) -> dict[str, Any]:
    response = s3.list_object_versions(Bucket=BUCKET, Prefix=key)
    versions = [item for item in response.get("Versions", []) if item["Key"] == key]
    delete_markers = [
        item for item in response.get("DeleteMarkers", []) if item["Key"] == key
    ]
    require(len(versions) == 1, f"final object version count drifted: {versions}")
    require(
        not delete_markers, f"final object acquired a delete marker: {delete_markers}"
    )
    version = versions[0]
    require(
        version.get("IsLatest") is True, "the only final object version is not latest"
    )
    version_id = version.get("VersionId")
    require(
        isinstance(version_id, str) and version_id, "final object has no version ID"
    )
    return {
        "version_id": version_id,
        "version_count": len(versions),
        "delete_marker_count": len(delete_markers),
    }


def write_evidence(payload: dict[str, Any]) -> None:
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=EVIDENCE_PATH.parent,
        prefix=f".{EVIDENCE_PATH.name}.",
    )
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        os.fchmod(handle.fileno(), 0o600)
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary_name, EVIDENCE_PATH)


def main() -> int:
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_complete_response_loss_{suffix}"
    object_prefix = f"objects/complete-response-loss-{suffix}"
    staging_prefix = f"staging/complete-response-loss-{suffix}"
    database_created = False
    moto: ThreadedMotoServer | None = None
    api_a: ManagedServer | None = None
    api_b: ManagedServer | None = None
    worker: ManagedServer | None = None
    request_thread: threading.Thread | None = None

    try:
        binary = resolve_current_binary()
        api_b_port, api_a_port, worker_port, moto_port = allocate_ports(4)
        endpoint = f"http://127.0.0.1:{moto_port}"

        moto = ThreadedMotoServer(
            ip_address="127.0.0.1",
            port=moto_port,
            verbose=False,
        )
        moto.start()
        s3 = boto3.client(
            "s3",
            endpoint_url=endpoint,
            aws_access_key_id="disk-complete-replay",
            aws_secret_access_key="disk-complete-replay-secret",
            region_name="us-east-1",
            config=Config(s3={"addressing_style": "path"}),
        )
        s3.create_bucket(Bucket=BUCKET)
        s3.put_bucket_versioning(
            Bucket=BUCKET,
            VersioningConfiguration={"Status": "Enabled"},
        )
        require(
            s3.get_bucket_versioning(Bucket=BUCKET).get("Status") == "Enabled",
            "S3 versioning fixture is not enabled",
        )

        with tempfile.TemporaryDirectory(
            prefix="disk-complete-response-loss-"
        ) as temporary:
            root = Path(temporary)
            create_database(database_name)
            database_created = True
            run_sql_file(database_name, INIT_SQL)

            common_environment = process_environment(endpoint, BUCKET, "s3")
            api_b = ManagedServer(
                name=f"complete-replay-b-{suffix}",
                binary=binary,
                run_directory=root / "api-b-run",
                config=process_config(
                    database_name,
                    api_b_port,
                    f"complete-replay-b-{suffix}",
                    "api",
                    root / "unused-api-b-final",
                    root / "unused-api-b-staging",
                    endpoint,
                    BUCKET,
                    object_prefix,
                    staging_prefix,
                    "s3",
                ),
                database_name=database_name,
                port=api_b_port,
                readiness_path="/api/health/ready",
                role="api",
                environment_overrides=common_environment,
            )

            payload_seed = b"disk-s3-complete-response-loss-"
            payload = (payload_seed * (UPLOAD_SIZE // len(payload_seed) + 1))[
                :UPLOAD_SIZE
            ]
            md5_hash = hashlib.md5(payload, usedforsecurity=False).hexdigest()
            sha256_hash = hashlib.sha256(payload).hexdigest()
            filename = f"complete_response_loss_{suffix}.bin"
            final_key = f"{object_prefix}/sha256/{sha256_hash[:2]}/{sha256_hash}.bin"

            with httpx.Client(timeout=180) as client:
                login = client.post(
                    api_b.base_url + "/api/auth/login",
                    json={"account": "admin", "password": "Admin123"},
                )
                token = success_data(login, "complete replay login").get("access_token")
                require(isinstance(token, str) and token, "login omitted access token")
                headers = {"Authorization": f"Bearer {token}"}

                quota_before = quota_snapshot(database_name)
                user_id = admin_user_id(database_name)
                upload_id = initialize_upload(
                    client,
                    api_b.base_url,
                    headers,
                    filename,
                    payload,
                )
                upload_chunks(
                    client,
                    (api_b.base_url,),
                    headers,
                    upload_id,
                    payload,
                    "response-loss",
                )

            initial_task = task_snapshot(database_name, upload_id)
            require(
                initial_task["status"] == 0, "response-loss upload is not in progress"
            )
            require(
                initial_task["staging_backend"] == "s3",
                "response-loss upload did not persist S3 staging",
            )

            fault_environment = dict(common_environment)
            fault_environment.update(
                {
                    "DISK_TEST_FAULT_INJECTION": "1",
                    "DISK_TEST_PAUSE_AFTER_FINALIZE_COMMIT_UPLOAD_ID": upload_id,
                }
            )
            api_a = ManagedServer(
                name=f"complete-crash-a-{suffix}",
                binary=binary,
                run_directory=root / "api-a-run",
                config=process_config(
                    database_name,
                    api_a_port,
                    f"complete-crash-a-{suffix}",
                    "api",
                    root / "unused-api-a-final",
                    root / "unused-api-a-staging",
                    endpoint,
                    BUCKET,
                    object_prefix,
                    staging_prefix,
                    "s3",
                ),
                database_name=database_name,
                port=api_a_port,
                readiness_path="/api/health/ready",
                role="api",
                environment_overrides=fault_environment,
            )

            request_outcome: queue.Queue[tuple[str, Any]] = queue.Queue(maxsize=1)

            def complete_on_api_a() -> None:
                try:
                    response = httpx.post(
                        api_a.base_url + "/api/file/upload/complete",
                        headers={
                            **headers,
                            "X-Request-Id": f"complete-response-loss-{suffix}",
                        },
                        json={"upload_id": upload_id},
                        timeout=120,
                    )
                    request_outcome.put(("response", response))
                except BaseException as error:
                    request_outcome.put(("error", type(error).__name__))

            request_thread = threading.Thread(
                target=complete_on_api_a,
                name="complete-response-loss-request",
                daemon=True,
            )
            request_thread.start()

            pause_marker = (
                "Test fault injection paused upload after finalize commit: "
                f"upload_id={upload_id}"
            )

            def committed_and_paused() -> dict[str, Any] | None:
                task = task_snapshot(database_name, upload_id)
                if (
                    task["status"] == 1
                    and task["completed_file_id"] is not None
                    and pause_marker in read_log(api_a)
                ):
                    return task
                return None

            committed_task = wait_until(
                "durable finalization before response",
                committed_and_paused,
                timeout_seconds=30,
            )
            completed_file_id = int(committed_task["completed_file_id"])
            require(
                committed_task["lease_owner"] is None, "completed task retained owner"
            )
            require(
                committed_task["lease_expires_at"] is None,
                "completed task retained lease deadline",
            )
            require(
                int(committed_task["finalize_attempts"]) == 1,
                "first completion used more than one finalize attempt",
            )

            quota_after_commit = quota_snapshot(database_name)
            require(
                quota_after_commit["storage_used"]
                == quota_before["storage_used"] + len(payload),
                "final transaction did not settle used quota exactly once",
            )
            require(
                quota_after_commit["storage_reserved"]
                == quota_before["storage_reserved"],
                "final transaction did not release reserved quota",
            )
            effects_after_commit = side_effect_snapshot(
                database_name,
                user_id,
                upload_id,
                filename,
                md5_hash,
                sha256_hash,
                completed_file_id,
            )
            require(
                effects_after_commit
                == {
                    "files": 1,
                    "contents": 1,
                    "ref_count": 1,
                    "chunks": 0,
                    "cleanup_jobs": 1,
                },
                f"final transaction side effects are not unique: {effects_after_commit}",
            )
            cleanup_before_kill = cleanup_job_snapshot(database_name, upload_id)
            require(
                cleanup_before_kill["status"] == 0
                and cleanup_before_kill["attempts"] == 0,
                "cleanup ran before the Worker started",
            )
            require(
                current_keys(s3, f"{staging_prefix}/{upload_id}/"),
                "staging objects disappeared before Worker cleanup",
            )
            final_before_kill = version_snapshot(s3, final_key)
            final_bytes = s3.get_object(Bucket=BUCKET, Key=final_key)["Body"].read()
            require(
                final_bytes == payload, "committed S3 final object failed integrity"
            )

            require(api_a.process is not None, "completing API process is unavailable")
            api_a.process.kill()
            api_a.process.wait(timeout=5)
            require(api_a.process.returncode != 0, "faulted API did not exit by signal")
            request_thread.join(timeout=10)
            require(
                not request_thread.is_alive(),
                "failed complete request did not terminate",
            )
            outcome_type, outcome_value = request_outcome.get_nowait()
            require(
                outcome_type == "error",
                f"faulted complete unexpectedly delivered HTTP response: {outcome_value}",
            )

            task_after_kill = task_snapshot(database_name, upload_id)
            require(
                task_after_kill == committed_task,
                "API process death changed the committed upload task",
            )
            require(
                version_snapshot(s3, final_key) == final_before_kill,
                "API process death changed the final object version",
            )

            replay_started = time.monotonic()
            replay_response = httpx.post(
                api_b.base_url + "/api/file/upload/complete",
                headers={
                    **headers,
                    "X-Request-Id": f"complete-response-replay-{suffix}",
                },
                json={"upload_id": upload_id},
                timeout=20,
            )
            replay_elapsed = time.monotonic() - replay_started
            replay_data = success_data(replay_response, "completed upload replay")
            replay_file = replay_data.get("file")
            require(isinstance(replay_file, dict), "replay omitted file data")
            require(
                replay_file.get("id") == completed_file_id,
                "replay returned a different file ID",
            )
            require(replay_elapsed < 5, "completed replay waited for a lease")
            require(
                replay_response.headers.get("X-Disk-Instance-Id") == api_b.name,
                "replay did not come from the compatible API",
            )

            diagnostic_response = httpx.get(
                api_b.base_url
                + f"/api/admin/uploads/{upload_id}/diagnostics"
                + "?chunk_page=1&chunk_page_size=20",
                headers=headers,
                timeout=20,
            )
            diagnostic = success_data(
                diagnostic_response,
                "read-only completed upload diagnostic",
            )
            diagnostic_task = diagnostic.get("task")
            require(isinstance(diagnostic_task, dict), "diagnostic omitted upload task")
            require(
                diagnostic_task.get("status") == "completed"
                and diagnostic_task.get("completed_file_id") == completed_file_id,
                "diagnostic did not report the replayed completed file",
            )

            task_after_replay = task_snapshot(database_name, upload_id)
            require(
                task_after_replay == committed_task,
                "completed replay changed task version, attempt, lease, or result",
            )
            require(
                side_effect_snapshot(
                    database_name,
                    user_id,
                    upload_id,
                    filename,
                    md5_hash,
                    sha256_hash,
                    completed_file_id,
                )
                == effects_after_commit,
                "completed replay duplicated a database side effect",
            )
            require(
                quota_snapshot(database_name) == quota_after_commit,
                "completed replay settled quota again",
            )
            require(
                cleanup_job_snapshot(database_name, upload_id) == cleanup_before_kill,
                "completed replay changed the pending cleanup job",
            )
            require(
                version_snapshot(s3, final_key) == final_before_kill,
                "completed replay deleted or recreated the final object",
            )

            worker = ManagedServer(
                name=f"complete-replay-worker-{suffix}",
                binary=binary,
                run_directory=root / "worker-run",
                config=process_config(
                    database_name,
                    worker_port,
                    f"complete-replay-worker-{suffix}",
                    "worker",
                    root / "unused-worker-final",
                    root / "unused-worker-staging",
                    endpoint,
                    BUCKET,
                    object_prefix,
                    staging_prefix,
                    "s3",
                ),
                database_name=database_name,
                port=worker_port,
                readiness_path="/api/health/ready",
                role="worker",
                environment_overrides=common_environment,
            )

            def staging_cleanup_completed() -> dict[str, Any] | None:
                job = cleanup_job_snapshot(database_name, upload_id)
                if job["status"] == 3 and not current_keys(
                    s3, f"{staging_prefix}/{upload_id}/"
                ):
                    return job
                return None

            cleanup_after_worker = wait_until(
                "Worker staging cleanup",
                staging_cleanup_completed,
                timeout_seconds=30,
            )
            require(
                cleanup_after_worker["attempts"] == 1,
                "staging cleanup required more than one attempt",
            )
            final_after_cleanup = version_snapshot(s3, final_key)
            require(
                final_after_cleanup == final_before_kill,
                "Worker staging cleanup changed the final object",
            )
            require(
                s3.get_object(Bucket=BUCKET, Key=final_key)["Body"].read() == payload,
                "final object bytes changed after staging cleanup",
            )

            write_evidence(
                {
                    "schema_version": 1,
                    "scenario": "s3_complete_response_loss_business_replay",
                    "fault_stage": "after_finalize_commit_before_http_response",
                    "first_success_response_delivered": False,
                    "completing_api_exit_signal": "SIGKILL",
                    "replay_http_status": replay_response.status_code,
                    "replay_same_completed_file_id": True,
                    "replay_without_lease_wait": True,
                    "diagnostic_status": diagnostic_task["status"],
                    "database": {
                        "task_status": "completed",
                        "finalize_attempts": int(
                            task_after_replay["finalize_attempts"]
                        ),
                        "state_version_unchanged": True,
                        "file_rows": effects_after_commit["files"],
                        "content_rows": effects_after_commit["contents"],
                        "content_ref_count": effects_after_commit["ref_count"],
                        "chunk_rows": effects_after_commit["chunks"],
                        "cleanup_job_rows": effects_after_commit["cleanup_jobs"],
                        "quota_settlements": 1,
                        "fixture_business_table_updates": 0,
                    },
                    "s3": {
                        "final_versions": final_after_cleanup["version_count"],
                        "final_delete_markers": final_after_cleanup[
                            "delete_marker_count"
                        ],
                        "final_version_unchanged": True,
                        "final_bytes_match": True,
                        "manual_final_delete_calls": 0,
                        "staging_cleanup_status": "succeeded",
                        "staging_cleanup_attempts": int(
                            cleanup_after_worker["attempts"]
                        ),
                        "current_staging_objects": 0,
                    },
                    "passed": True,
                }
            )
            require(
                EVIDENCE_PATH.stat().st_mode & 0o777 == 0o600,
                "complete response-loss evidence mode drifted",
            )

            worker.stop()
            worker = None
            api_b.stop()
            api_b = None
            api_a.stop()
            api_a = None

        print(
            "PASS: committed S3 upload recovered through idempotent complete replay "
            "without deleting the final object"
        )
        return 0
    except BaseException:
        for server in (api_a, api_b, worker):
            if server is not None:
                print(server.log_tail(), file=sys.stderr)
        raise
    finally:
        for server in (worker, api_b, api_a):
            if server is not None:
                server.stop()
        if request_thread is not None and request_thread.is_alive():
            request_thread.join(timeout=2)
        if database_created:
            drop_database(database_name)
        if moto is not None:
            moto.stop()


if __name__ == "__main__":
    raise SystemExit(main())

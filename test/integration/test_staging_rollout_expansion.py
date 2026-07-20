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

"""Exercise 10% -> 50% -> 100% S3 staging rollout expansion."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
import time
import uuid
from dataclasses import dataclass
from pathlib import Path

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
    CHUNK_SIZE,
    S3_ERROR_OUTCOMES,
    UPLOAD_SIZE,
    complete_upload,
    initialize_upload,
    md5,
    metric_sum,
    process_config,
    process_environment,
    scrape_metrics,
    task_row,
    wait_until,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/staging-rollout-expansion-summary.json"
STAGE_TASKS = 10


@dataclass(frozen=True)
class RolloutTask:
    stage_percent: int
    index: int
    upload_id: str
    backend: str
    prefix: str
    payload: bytes


def initialize_stage(
    client: httpx.Client,
    s3_base_urls: tuple[str, ...],
    local_base_url: str | None,
    headers: dict[str, str],
    database_name: str,
    staging_prefix: str,
    suffix: str,
    stage_percent: int,
    s3_tasks: int,
    guard_payloads: dict[int, bytes] | None = None,
) -> list[RolloutTask]:
    require(0 <= s3_tasks <= STAGE_TASKS, "invalid rollout stage size")
    guard_payloads = guard_payloads or {}
    local_tasks = STAGE_TASKS - s3_tasks
    tasks: list[RolloutTask] = []

    for index in range(STAGE_TASKS):
        backend = "local" if index < local_tasks else "s3"
        if backend == "local":
            require(local_base_url is not None, "local rollout stage omitted its API")
            base_url = local_base_url
        else:
            base_url = s3_base_urls[(index - local_tasks) % len(s3_base_urls)]
        payload = guard_payloads.get(
            index,
            f"rollout-{suffix}-{stage_percent}-{index}".encode(),
        )
        upload_id = initialize_upload(
            client,
            base_url,
            headers,
            f"rollout_{suffix}_{stage_percent}_{index}.bin",
            payload,
        )
        row = task_row(database_name, upload_id)
        expected_prefix = upload_id if backend == "local" else f"{staging_prefix}/{upload_id}"
        require(row["staging_backend"] == backend, f"stage {stage_percent} persisted wrong backend")
        require(row["staging_prefix"] == expected_prefix, f"stage {stage_percent} persisted wrong prefix")
        tasks.append(
            RolloutTask(
                stage_percent=stage_percent,
                index=index,
                upload_id=upload_id,
                backend=backend,
                prefix=expected_prefix,
                payload=payload,
            )
        )
    return tasks


def descriptor_snapshot(
    database_name: str,
    tasks: list[RolloutTask],
) -> dict[str, tuple[str, str]]:
    upload_ids = [task.upload_id for task in tasks]
    with connect(database_name) as connection:
        rows = connection.execute(
            "SELECT id, staging_backend, staging_prefix FROM upload_tasks "
            "WHERE id = ANY(%s) ORDER BY id",
            (upload_ids,),
        ).fetchall()
    require(len(rows) == len(tasks), "descriptor snapshot omitted rollout tasks")
    return {
        str(row["id"]): (str(row["staging_backend"]), str(row["staging_prefix"]))
        for row in rows
    }


def expected_snapshot(tasks: list[RolloutTask]) -> dict[str, tuple[str, str]]:
    return {task.upload_id: (task.backend, task.prefix) for task in tasks}


def require_unchanged(database_name: str, tasks: list[RolloutTask], label: str) -> None:
    require(
        descriptor_snapshot(database_name, tasks) == expected_snapshot(tasks),
        f"persisted staging descriptors changed after {label}",
    )


def send_chunk(
    client: httpx.Client,
    base_url: str,
    headers: dict[str, str],
    task: RolloutTask,
    chunk_index: int,
) -> None:
    start = chunk_index * CHUNK_SIZE
    chunk = task.payload[start : start + CHUNK_SIZE]
    require(len(chunk) == CHUNK_SIZE, f"guard chunk {chunk_index} fixture changed")
    response = client.post(
        base_url + "/api/file/upload/chunk",
        params={
            "upload_id": task.upload_id,
            "chunk_index": chunk_index,
            "chunk_hash": md5(chunk),
        },
        headers={**headers, "Content-Type": "application/octet-stream"},
        content=chunk,
    )
    data = success_data(response, f"{task.backend} guard chunk {chunk_index}")
    require(data.get("uploaded") is True, f"{task.backend} guard chunk was not stored")


def cancel_upload(
    client: httpx.Client,
    base_url: str,
    headers: dict[str, str],
    upload_id: str,
) -> None:
    response = client.delete(
        base_url + f"/api/file/upload/{upload_id}",
        headers=headers,
    )
    try:
        body = response.json()
    except ValueError as error:
        raise AssertionError(
            f"cancel {upload_id} returned non-JSON HTTP {response.status_code}"
        ) from error
    require(
        response.status_code == 200 and body.get("code") in (0, "0"),
        f"cancel {upload_id} failed: HTTP {response.status_code}, body={body}",
    )


def stage_counts(database_name: str, tasks: list[RolloutTask]) -> dict[str, int]:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT COUNT(*) AS total, "
            "COUNT(*) FILTER (WHERE staging_backend = 'local') AS local_tasks, "
            "COUNT(*) FILTER (WHERE staging_backend = 's3') AS s3_tasks "
            "FROM upload_tasks WHERE id = ANY(%s)",
            ([task.upload_id for task in tasks],),
        ).fetchone()
    require(row is not None, "rollout stage count returned no row")
    return {
        "total": int(row["total"]),
        "local_tasks": int(row["local_tasks"]),
        "s3_tasks": int(row["s3_tasks"]),
    }


def main() -> int:
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_staging_expansion_{suffix}"
    bucket = "disk-rollout-expansion"
    object_prefix = f"objects/expansion-{suffix}"
    staging_prefix = f"staging/expansion-{suffix}"
    database_created = False
    moto: ThreadedMotoServer | None = None
    api_canary: ManagedServer | None = None
    api_baseline: ManagedServer | None = None
    api_expanded: ManagedServer | None = None
    worker: ManagedServer | None = None
    started_at = time.monotonic()

    try:
        binary = resolve_current_binary()
        canary_port, baseline_port, worker_port, moto_port = allocate_ports(4)
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
            aws_access_key_id="disk-rollout",
            aws_secret_access_key="disk-rollout-secret",
            region_name="us-east-1",
            config=Config(s3={"addressing_style": "path"}),
        )
        s3.create_bucket(Bucket=bucket)

        with tempfile.TemporaryDirectory(prefix="disk-staging-expansion-") as temporary:
            root = Path(temporary)
            canary_local = root / "canary-local"
            baseline_local = root / "baseline-local"

            create_database(database_name)
            database_created = True
            run_sql_file(database_name, INIT_SQL)

            api_canary = ManagedServer(
                name=f"expansion-canary-api-{suffix}",
                binary=binary,
                run_directory=root / "canary-api-run",
                config=process_config(
                    database_name,
                    canary_port,
                    f"expansion-canary-api-{suffix}",
                    "api",
                    root / "unused-canary-final",
                    canary_local,
                    endpoint,
                    bucket,
                    object_prefix,
                    staging_prefix,
                    "s3",
                ),
                database_name=database_name,
                port=canary_port,
                readiness_path="/api/health/ready",
                role="api",
                environment_overrides=process_environment(endpoint, bucket, "s3"),
            )
            api_baseline = ManagedServer(
                name=f"expansion-baseline-api-{suffix}",
                binary=binary,
                run_directory=root / "baseline-api-run",
                config=process_config(
                    database_name,
                    baseline_port,
                    f"expansion-baseline-api-{suffix}",
                    "api",
                    root / "unused-baseline-final",
                    baseline_local,
                    endpoint,
                    bucket,
                    object_prefix,
                    staging_prefix,
                    "local",
                ),
                database_name=database_name,
                port=baseline_port,
                readiness_path="/api/health/ready",
                role="api",
                environment_overrides=process_environment(endpoint, bucket, "local"),
            )

            with httpx.Client(timeout=180) as client:
                login = client.post(
                    api_canary.base_url + "/api/auth/login",
                    json={"account": "admin", "password": "Admin123"},
                )
                token = success_data(login, "rollout login").get("access_token")
                require(isinstance(token, str) and token, "rollout login omitted access token")
                headers = {"Authorization": f"Bearer {token}"}

                local_guard_payload = os.urandom(UPLOAD_SIZE)
                s3_guard_payload = os.urandom(UPLOAD_SIZE)
                stage_10 = initialize_stage(
                    client,
                    (api_canary.base_url,),
                    api_baseline.base_url,
                    headers,
                    database_name,
                    staging_prefix,
                    suffix,
                    10,
                    1,
                    {0: local_guard_payload, 9: s3_guard_payload},
                )
                local_guard = stage_10[0]
                s3_guard = stage_10[9]
                send_chunk(client, api_baseline.base_url, headers, local_guard, 0)
                send_chunk(client, api_canary.base_url, headers, s3_guard, 0)
                require_unchanged(database_name, stage_10, "10 percent stage")

                stage_50 = initialize_stage(
                    client,
                    (api_canary.base_url,),
                    api_baseline.base_url,
                    headers,
                    database_name,
                    staging_prefix,
                    suffix,
                    50,
                    5,
                )
                first_twenty = stage_10 + stage_50
                require_unchanged(database_name, first_twenty, "50 percent stage")

                api_baseline.stop()
                api_baseline = None
                api_expanded = ManagedServer(
                    name=f"expansion-s3-api-{suffix}",
                    binary=binary,
                    run_directory=root / "expanded-api-run",
                    config=process_config(
                        database_name,
                        baseline_port,
                        f"expansion-s3-api-{suffix}",
                        "api",
                        root / "unused-expanded-final",
                        baseline_local,
                        endpoint,
                        bucket,
                        object_prefix,
                        staging_prefix,
                        "s3",
                    ),
                    database_name=database_name,
                    port=baseline_port,
                    readiness_path="/api/health/ready",
                    role="api",
                    environment_overrides=process_environment(endpoint, bucket, "s3"),
                )

                stage_100 = initialize_stage(
                    client,
                    (api_canary.base_url, api_expanded.base_url),
                    None,
                    headers,
                    database_name,
                    staging_prefix,
                    suffix,
                    100,
                    10,
                )
                all_tasks = first_twenty + stage_100
                require_unchanged(database_name, all_tasks, "100 percent stage")

                for chunk_index in range(1, UPLOAD_SIZE // CHUNK_SIZE):
                    send_chunk(
                        client,
                        api_expanded.base_url,
                        headers,
                        local_guard,
                        chunk_index,
                    )
                    s3_target = api_expanded if chunk_index % 2 else api_canary
                    send_chunk(client, s3_target.base_url, headers, s3_guard, chunk_index)

                local_file_id, local_completion_ms = complete_upload(
                    client,
                    api_expanded.base_url,
                    headers,
                    local_guard.upload_id,
                    "complete pre-expansion local guard",
                )
                s3_file_id, s3_completion_ms = complete_upload(
                    client,
                    api_expanded.base_url,
                    headers,
                    s3_guard.upload_id,
                    "complete pre-expansion S3 guard",
                )

                guard_ids = {local_guard.upload_id, s3_guard.upload_id}
                for task in all_tasks:
                    if task.upload_id not in guard_ids:
                        cancel_upload(
                            client,
                            api_expanded.base_url,
                            headers,
                            task.upload_id,
                        )

            require_unchanged(database_name, all_tasks, "terminal processing")
            require(task_row(database_name, local_guard.upload_id)["status"] == 1, "local guard did not complete")
            require(task_row(database_name, s3_guard.upload_id)["status"] == 1, "S3 guard did not complete")

            final_keys: dict[str, str] = {}
            for task in (local_guard, s3_guard):
                sha256_hash = hashlib.sha256(task.payload).hexdigest()
                key = f"{object_prefix}/sha256/{sha256_hash[:2]}/{sha256_hash}.bin"
                final_keys[task.backend] = key
                final_object = s3.get_object(Bucket=bucket, Key=key)["Body"].read()
                require(final_object == task.payload, f"{task.backend} guard final object changed")

            worker = ManagedServer(
                name=f"expansion-worker-{suffix}",
                binary=binary,
                run_directory=root / "worker-run",
                config=process_config(
                    database_name,
                    worker_port,
                    f"expansion-worker-{suffix}",
                    "worker",
                    root / "unused-worker-final",
                    baseline_local,
                    endpoint,
                    bucket,
                    object_prefix,
                    staging_prefix,
                    "s3",
                ),
                database_name=database_name,
                port=worker_port,
                readiness_path="/api/health/ready",
                role="worker",
                environment_overrides=process_environment(endpoint, bucket, "s3"),
            )

            all_upload_ids = [task.upload_id for task in all_tasks]

            def cleanup_complete() -> bool:
                with connect(database_name) as connection:
                    row = connection.execute(
                        "SELECT COUNT(*) AS total, "
                        "COUNT(*) FILTER (WHERE status = 3) AS succeeded "
                        "FROM storage_jobs WHERE job_type = 'staging_cleanup' "
                        "AND aggregate_id = ANY(%s)",
                        (all_upload_ids,),
                    ).fetchone()
                return (
                    row is not None
                    and int(row["total"]) == len(all_tasks)
                    and int(row["succeeded"]) == len(all_tasks)
                )

            wait_until(cleanup_complete, 30, "rollout staging cleanup")
            require_unchanged(database_name, all_tasks, "Worker cleanup")
            require(
                not any((baseline_local / upload_id).exists() for upload_id in all_upload_ids),
                "rollout local staging was not cleaned",
            )
            staging_inventory = s3.list_objects_v2(
                Bucket=bucket,
                Prefix=staging_prefix + "/",
            ).get("Contents", [])
            require(not staging_inventory, "rollout S3 staging was not cleaned")

            stage_results = {
                "10": stage_counts(database_name, stage_10),
                "50": stage_counts(database_name, stage_50),
                "100": stage_counts(database_name, stage_100),
            }
            require(stage_results["10"] == {"total": 10, "local_tasks": 9, "s3_tasks": 1}, "10% stage drifted")
            require(stage_results["50"] == {"total": 10, "local_tasks": 5, "s3_tasks": 5}, "50% stage drifted")
            require(stage_results["100"] == {"total": 10, "local_tasks": 0, "s3_tasks": 10}, "100% stage drifted")

            with connect(database_name) as connection:
                terminal = connection.execute(
                    "SELECT COUNT(*) FILTER (WHERE status = 1) AS completed, "
                    "COUNT(*) FILTER (WHERE status = 2) AS cancelled "
                    "FROM upload_tasks WHERE id = ANY(%s)",
                    (all_upload_ids,),
                ).fetchone()
                quota = connection.execute(
                    "SELECT storage_used, storage_reserved FROM users WHERE username = 'admin'"
                ).fetchone()
                unresolved = connection.execute(
                    "SELECT COUNT(*) AS count FROM storage_reconciliation_findings "
                    "WHERE resolved_at IS NULL"
                ).fetchone()
            require(
                terminal is not None
                and int(terminal["completed"]) == 2
                and int(terminal["cancelled"]) == len(all_tasks) - 2,
                "rollout terminal state counts changed",
            )
            require(
                quota is not None
                and int(quota["storage_used"]) == 2 * UPLOAD_SIZE
                and int(quota["storage_reserved"]) == 0,
                "rollout quota reconciliation changed",
            )
            require(
                unresolved is not None and int(unresolved["count"]) == 0,
                "rollout produced unresolved findings",
            )

            metrics = [
                scrape_metrics(api_canary.base_url),
                scrape_metrics(api_expanded.base_url),
                scrape_metrics(worker.base_url),
            ]
            s3_errors = sum(
                metric_sum(
                    snapshot,
                    "disk_dependency_calls_total",
                    dependency="s3",
                    outcome=outcome,
                )
                for snapshot in metrics
                for outcome in S3_ERROR_OUTCOMES
            )
            require(s3_errors == 0, "rollout metrics reported actionable S3 errors")

            api_canary.require_running("rollout acceptance")
            api_expanded.require_running("rollout acceptance")
            worker.require_running("rollout acceptance")

            EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
            EVIDENCE_PATH.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "scenario": "s3_staging_progressive_expansion",
                        "stages": [
                            {
                                "target_s3_percent": int(percent),
                                **stage_results[percent],
                            }
                            for percent in ("10", "50", "100")
                        ],
                        "descriptor_invariance": {
                            "sessions_checked": len(all_tasks),
                            "checkpoints": 5,
                            "mutations": 0,
                        },
                        "guards": {
                            "local": {
                                "upload_id": local_guard.upload_id,
                                "file_id": local_file_id,
                                "completion_ms": round(local_completion_ms, 3),
                                "final_key": final_keys["local"],
                            },
                            "s3": {
                                "upload_id": s3_guard.upload_id,
                                "file_id": s3_file_id,
                                "completion_ms": round(s3_completion_ms, 3),
                                "final_key": final_keys["s3"],
                            },
                            "first_chunk_written_before_expansion": True,
                            "completed_after_all_apis_defaulted_to_s3": True,
                        },
                        "terminal": {
                            "completed": int(terminal["completed"]),
                            "cancelled": int(terminal["cancelled"]),
                            "staging_cleanup_succeeded": len(all_tasks),
                            "storage_reserved": int(quota["storage_reserved"]),
                            "unresolved_findings": int(unresolved["count"]),
                            "actionable_s3_errors": s3_errors,
                        },
                        "elapsed_seconds": round(time.monotonic() - started_at, 3),
                        "passed": True,
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            worker.stop()
            worker = None
            api_expanded.stop()
            api_expanded = None
            api_canary.stop()
            api_canary = None

        print(
            "PASS: 10% -> 50% -> 100% S3 staging expansion preserved all "
            "persisted descriptors and drained compatible local/S3 tasks"
        )
        return 0
    except BaseException:
        for process in (api_canary, api_baseline, api_expanded, worker):
            if process is not None:
                print(process.log_tail(), file=sys.stderr)
        raise
    finally:
        for process in (worker, api_expanded, api_baseline, api_canary):
            if process is not None:
                process.stop()
        if database_created:
            drop_database(database_name)
        if moto is not None:
            moto.stop()


if __name__ == "__main__":
    raise SystemExit(main())

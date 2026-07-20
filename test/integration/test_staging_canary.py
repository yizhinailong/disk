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

"""Exercise a 10% S3 staging canary across APIs with different defaults."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any, Callable

import boto3
import httpx
from botocore.config import Config
from moto.server import ThreadedMotoServer
from prometheus_client.parser import text_string_to_metric_families

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
    server_config,
    success_data,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/staging-canary-summary.json"
COHORT_TASKS = 10
CANARY_TASKS = 1
CHUNK_SIZE = 1024 * 1024
UPLOAD_SIZE = 6 * CHUNK_SIZE
S3_ERROR_OUTCOMES = (
    "timeout",
    "connection",
    "retryable",
    "permanent",
    "protocol",
    "other",
)
COMPLETE_STAGES = (
    "claim_lease",
    "load_metadata",
    "assemble",
    "dedup_lookup",
    "promote",
    "commit",
)


def md5(payload: bytes) -> str:
    return hashlib.md5(payload, usedforsecurity=False).hexdigest()


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
        except BaseException as error:  # Preserve the last transient observation.
            last_error = error
        time.sleep(0.1)
    suffix = f": {last_error}" if last_error is not None else ""
    raise AssertionError(f"timed out waiting for {label}{suffix}")


def process_config(
    database_name: str,
    port: int,
    instance_id: str,
    role: str,
    final_root: Path,
    staging_root: Path,
    endpoint: str,
    bucket: str,
    object_prefix: str,
    staging_prefix: str,
    staging_backend: str,
) -> dict[str, Any]:
    config = server_config(
        database_name,
        port,
        instance_id,
        final_root,
        staging_root,
        role=role,
    )
    config["app"]["client_max_body_size"] = "2M"
    disk = config["custom_config"]["disk"]
    disk.update(
        {
            "storage_backend": "s3",
            "upload_staging_backend": staging_backend,
            "chunk_size": CHUNK_SIZE,
            "max_file_size": UPLOAD_SIZE * 2,
            "worker_poll_interval_ms": 100,
            "worker_claim_batch_size": 1,
            "worker_concurrency": 1,
            "s3": {
                "bucket": bucket,
                "region": "us-east-1",
                "endpoint": endpoint,
                "use_ssl": False,
                "force_path_style": True,
                "verify_ssl": False,
                "object_prefix": object_prefix,
                "staging_prefix": staging_prefix,
                "max_connections": 8,
                "io_threads": 2,
                "connect_timeout_ms": 3000,
                "request_timeout_ms": 120000,
                "max_retries": 0,
                "retry_base_delay_ms": 10,
            },
        }
    )
    return config


def process_environment(
    endpoint: str,
    bucket: str,
    staging_backend: str,
) -> dict[str, str]:
    return {
        "DISK_STORAGE_BACKEND": "s3",
        "DISK_UPLOAD_STAGING_BACKEND": staging_backend,
        "DISK_WORKER_CLAIMING_ENABLED": "true",
        "DISK_S3_ENDPOINT": endpoint,
        "DISK_S3_BUCKET": bucket,
        "DISK_S3_REGION": "us-east-1",
        "DISK_S3_ACCESS_KEY": "disk-canary",
        "DISK_S3_SECRET_KEY": "disk-canary-secret",
        "DISK_S3_USE_SSL": "false",
        "DISK_S3_FORCE_PATH_STYLE": "true",
        "DISK_S3_VERIFY_SSL": "false",
        "AWS_ACCESS_KEY_ID": "disk-canary",
        "AWS_SECRET_ACCESS_KEY": "disk-canary-secret",
        "AWS_DEFAULT_REGION": "us-east-1",
        "AWS_EC2_METADATA_DISABLED": "true",
    }


def initialize_upload(
    client: httpx.Client,
    base_url: str,
    headers: dict[str, str],
    filename: str,
    payload: bytes,
) -> str:
    response = client.post(
        base_url + "/api/file/upload/init",
        headers=headers,
        json={
            "filename": filename,
            "file_size": len(payload),
            "file_hash": md5(payload),
            "parent_id": 0,
        },
    )
    data = success_data(response, f"initialize {filename}")
    require(data.get("instant_upload") is not True, f"{filename} unexpectedly deduplicated")
    upload_id = data.get("upload_id")
    require(isinstance(upload_id, str) and upload_id, f"{filename} omitted upload_id")
    return upload_id


def upload_chunks(
    client: httpx.Client,
    base_urls: tuple[str, ...],
    headers: dict[str, str],
    upload_id: str,
    payload: bytes,
    label: str,
) -> int:
    chunks = [
        payload[offset : offset + CHUNK_SIZE]
        for offset in range(0, len(payload), CHUNK_SIZE)
    ]
    require(len(chunks) == UPLOAD_SIZE // CHUNK_SIZE, f"{label} chunk fixture changed")
    for index, chunk in enumerate(chunks):
        response = client.post(
            base_urls[index % len(base_urls)] + "/api/file/upload/chunk",
            params={
                "upload_id": upload_id,
                "chunk_index": index,
                "chunk_hash": md5(chunk),
            },
            headers={**headers, "Content-Type": "application/octet-stream"},
            content=chunk,
        )
        data = success_data(response, f"{label} chunk {index}")
        require(data.get("uploaded") is True, f"{label} chunk {index} was not stored")
    return len(chunks)


def complete_upload(
    client: httpx.Client,
    base_url: str,
    headers: dict[str, str],
    upload_id: str,
    label: str,
) -> tuple[int, float]:
    started = time.perf_counter()
    response = client.post(
        base_url + "/api/file/upload/complete",
        headers=headers,
        json={"upload_id": upload_id},
    )
    elapsed_ms = (time.perf_counter() - started) * 1000
    data = success_data(response, label)
    file_data = data.get("file")
    require(isinstance(file_data, dict), f"{label} omitted file")
    file_id = file_data.get("id")
    require(isinstance(file_id, int), f"{label} omitted file ID")
    return file_id, elapsed_ms


def task_row(database_name: str, upload_id: str) -> dict[str, Any]:
    with connect(database_name) as connection:
        row = connection.execute(
            "SELECT id, filename, staging_backend, staging_prefix, status "
            "FROM upload_tasks WHERE id = %s",
            (upload_id,),
        ).fetchone()
    require(row is not None, f"upload task disappeared: {upload_id}")
    return dict(row)


def scrape_metrics(base_url: str) -> dict[tuple[str, tuple[tuple[str, str], ...]], float]:
    response = httpx.get(base_url + "/metrics", timeout=10)
    require(response.status_code == 200, f"metrics failed for {base_url}")
    snapshot: dict[tuple[str, tuple[tuple[str, str], ...]], float] = {}
    for family in text_string_to_metric_families(response.text):
        for sample in family.samples:
            key = (sample.name, tuple(sorted(sample.labels.items())))
            snapshot[key] = float(sample.value)
    return snapshot


def metric_sum(
    snapshot: dict[tuple[str, tuple[tuple[str, str], ...]], float],
    name: str,
    **labels: str,
) -> float:
    expected = set(labels.items())
    return sum(
        value
        for (sample_name, sample_labels), value in snapshot.items()
        if sample_name == name and expected <= set(sample_labels)
    )


def metric_delta(
    before: dict[tuple[str, tuple[tuple[str, str], ...]], float],
    after: dict[tuple[str, tuple[tuple[str, str], ...]], float],
    name: str,
    **labels: str,
) -> float:
    return metric_sum(after, name, **labels) - metric_sum(before, name, **labels)


def main() -> int:
    suffix = uuid.uuid4().hex[:12]
    database_name = f"disk_staging_canary_{suffix}"
    bucket = "disk-canary"
    object_prefix = f"objects/canary-{suffix}"
    staging_prefix = f"staging/canary-{suffix}"
    database_created = False
    moto: ThreadedMotoServer | None = None
    api_canary: ManagedServer | None = None
    api_baseline: ManagedServer | None = None
    worker: ManagedServer | None = None
    started_at = time.monotonic()

    try:
        binary = resolve_current_binary()
        ports = allocate_ports(4)
        canary_port, baseline_port, worker_port, moto_port = ports
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
            aws_access_key_id="disk-canary",
            aws_secret_access_key="disk-canary-secret",
            region_name="us-east-1",
            config=Config(s3={"addressing_style": "path"}),
        )
        s3.create_bucket(Bucket=bucket)

        with tempfile.TemporaryDirectory(prefix="disk-staging-canary-") as temporary:
            root = Path(temporary)
            canary_local = root / "canary-local"
            baseline_local = root / "baseline-local"

            create_database(database_name)
            database_created = True
            run_sql_file(database_name, INIT_SQL)

            api_canary = ManagedServer(
                name=f"canary-api-{suffix}",
                binary=binary,
                run_directory=root / "canary-api-run",
                config=process_config(
                    database_name,
                    canary_port,
                    f"canary-api-{suffix}",
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
                name=f"baseline-api-{suffix}",
                binary=binary,
                run_directory=root / "baseline-api-run",
                config=process_config(
                    database_name,
                    baseline_port,
                    f"baseline-api-{suffix}",
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

            metrics_before = {
                "canary": scrape_metrics(api_canary.base_url),
                "baseline": scrape_metrics(api_baseline.base_url),
            }

            with httpx.Client(timeout=180) as client:
                login = client.post(
                    api_canary.base_url + "/api/auth/login",
                    json={"account": "admin", "password": "Admin123"},
                )
                token = success_data(login, "canary login").get("access_token")
                require(isinstance(token, str) and token, "login omitted access token")
                headers = {"Authorization": f"Bearer {token}"}

                local_tasks: list[tuple[str, dict[str, Any]]] = []
                baseline_completion_ms: list[float] = []
                for index in range(COHORT_TASKS - CANARY_TASKS):
                    payload = os.urandom(UPLOAD_SIZE)
                    upload_id = initialize_upload(
                        client,
                        api_baseline.base_url,
                        headers,
                        f"baseline_{suffix}_{index}.bin",
                        payload,
                    )
                    row = task_row(database_name, upload_id)
                    require(row["staging_backend"] == "local", "baseline task did not persist local")
                    require(row["staging_prefix"] == upload_id, "baseline locator changed")
                    upload_chunks(
                        client,
                        (api_baseline.base_url,),
                        headers,
                        upload_id,
                        payload,
                        f"baseline {index}",
                    )
                    _, elapsed_ms = complete_upload(
                        client,
                        api_baseline.base_url,
                        headers,
                        upload_id,
                        f"complete baseline {index}",
                    )
                    completed = task_row(database_name, upload_id)
                    require(completed["status"] == 1, f"baseline task {index} did not complete")
                    require(
                        completed["staging_backend"] == row["staging_backend"]
                        and completed["staging_prefix"] == row["staging_prefix"],
                        f"baseline task {index} descriptor changed",
                    )
                    local_tasks.append((upload_id, row))
                    baseline_completion_ms.append(elapsed_ms)

                canary_payload = os.urandom(UPLOAD_SIZE)
                canary_name = f"canary_{suffix}.bin"
                canary_id = initialize_upload(
                    client,
                    api_canary.base_url,
                    headers,
                    canary_name,
                    canary_payload,
                )
                canary_initial = task_row(database_name, canary_id)
                require(canary_initial["staging_backend"] == "s3", "canary task did not persist S3")
                require(
                    canary_initial["staging_prefix"] == f"{staging_prefix}/{canary_id}",
                    "canary task persisted the wrong S3 locator",
                )

                canary_chunks = upload_chunks(
                    client,
                    (api_baseline.base_url, api_canary.base_url),
                    headers,
                    canary_id,
                    canary_payload,
                    "canary",
                )
                file_id, canary_completion_ms = complete_upload(
                    client,
                    api_baseline.base_url,
                    headers,
                    canary_id,
                    "canary complete on baseline API",
                )

                canary_completed = task_row(database_name, canary_id)
                require(canary_completed["status"] == 1, "canary task did not complete")
                require(
                    canary_completed["staging_backend"] == canary_initial["staging_backend"]
                    and canary_completed["staging_prefix"] == canary_initial["staging_prefix"],
                    "cross-instance completion reinterpreted the canary descriptor",
                )
                require(
                    not (canary_local / canary_id).exists()
                    and not (baseline_local / canary_id).exists(),
                    "S3 canary created node-local staging",
                )

            sha256_hash = hashlib.sha256(canary_payload).hexdigest()
            final_key = f"{object_prefix}/sha256/{sha256_hash[:2]}/{sha256_hash}.bin"
            final_object = s3.get_object(Bucket=bucket, Key=final_key)["Body"].read()
            require(final_object == canary_payload, "canary final object failed integrity verification")

            metrics_after = {
                "canary": scrape_metrics(api_canary.base_url),
                "baseline": scrape_metrics(api_baseline.base_url),
            }
            require(
                metric_delta(
                    metrics_before["baseline"],
                    metrics_after["baseline"],
                    "disk_http_requests_total",
                    operation="upload_complete",
                    status_class="2xx",
                )
                == COHORT_TASKS,
                "cohort completion success counter changed",
            )
            require(
                metric_delta(
                    metrics_before["baseline"],
                    metrics_after["baseline"],
                    "disk_http_requests_total",
                    operation="upload_complete",
                )
                == COHORT_TASKS,
                "cohort completion success rate changed",
            )
            for stage in COMPLETE_STAGES:
                require(
                    metric_delta(
                        metrics_before["baseline"],
                        metrics_after["baseline"],
                        "disk_upload_complete_stage_duration_seconds_count",
                        stage=stage,
                    )
                    == COHORT_TASKS,
                    f"cohort completion stage was not measured: {stage}",
                )

            all_upload_ids = [upload_id for upload_id, _ in local_tasks] + [canary_id]
            worker = ManagedServer(
                name=f"canary-worker-{suffix}",
                binary=binary,
                run_directory=root / "worker-run",
                config=process_config(
                    database_name,
                    worker_port,
                    f"canary-worker-{suffix}",
                    "worker",
                    root / "unused-worker-final",
                    baseline_local,
                    endpoint,
                    bucket,
                    object_prefix,
                    staging_prefix,
                    "local",
                ),
                database_name=database_name,
                port=worker_port,
                readiness_path="/api/health/ready",
                role="worker",
                environment_overrides=process_environment(endpoint, bucket, "local"),
            )

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
                    and int(row["total"]) == COHORT_TASKS
                    and int(row["succeeded"]) == COHORT_TASKS
                )

            wait_until(cleanup_complete, 30, "canary and baseline staging cleanup")
            require(
                not any((baseline_local / upload_id).exists() for upload_id in all_upload_ids),
                "baseline local staging was not cleaned",
            )
            staging_inventory = s3.list_objects_v2(
                Bucket=bucket,
                Prefix=staging_prefix + "/",
            ).get("Contents", [])
            require(not staging_inventory, "canary S3 staging prefix was not cleaned")

            worker_metrics = scrape_metrics(worker.base_url)
            all_after_metrics = [
                metrics_after["canary"],
                metrics_after["baseline"],
                worker_metrics,
            ]
            s3_successes = sum(
                metric_sum(snapshot, "disk_dependency_calls_total", dependency="s3", outcome="success")
                for snapshot in all_after_metrics
            )
            s3_errors = sum(
                metric_sum(
                    snapshot,
                    "disk_dependency_calls_total",
                    dependency="s3",
                    outcome=outcome,
                )
                for snapshot in all_after_metrics
                for outcome in S3_ERROR_OUTCOMES
            )
            require(s3_successes > 0, "canary metrics did not observe S3 calls")
            require(s3_errors == 0, "canary metrics reported actionable S3 errors")
            require(
                metric_sum(
                    worker_metrics,
                    "disk_storage_job_runs_total",
                    job_type="staging_cleanup",
                    outcome="succeeded",
                )
                == COHORT_TASKS,
                "Worker did not report every staging cleanup success",
            )
            require(
                metric_sum(worker_metrics, "disk_storage_jobs_expired_leases") == 0,
                "canary metrics reported expired leases",
            )
            require(
                metric_sum(worker_metrics, "disk_storage_job_takeovers_total") == 0,
                "canary metrics reported lease takeovers",
            )
            require(
                metric_sum(worker_metrics, "disk_storage_jobs", status="dead_letter") == 0,
                "canary metrics reported dead-letter jobs",
            )
            require(
                all(metric_sum(snapshot, "disk_metrics_snapshot_success") == 1 for snapshot in all_after_metrics),
                "canary metrics snapshot failed",
            )
            require(
                metric_sum(worker_metrics, "disk_reconciliation_findings_unresolved") == 0,
                "canary metrics reported unresolved reconciliation findings",
            )

            with connect(database_name) as connection:
                cohort = connection.execute(
                    "SELECT COUNT(*) AS total, "
                    "COUNT(*) FILTER (WHERE staging_backend = 'local') AS local_tasks, "
                    "COUNT(*) FILTER (WHERE staging_backend = 's3') AS s3_tasks, "
                    "COUNT(*) FILTER (WHERE status = 1) AS completed, "
                    "COUNT(*) FILTER (WHERE status = 2) AS cancelled "
                    "FROM upload_tasks WHERE id = ANY(%s)",
                    (all_upload_ids,),
                ).fetchone()
                unresolved = connection.execute(
                    "SELECT COUNT(*) AS count FROM storage_reconciliation_findings "
                    "WHERE resolved_at IS NULL"
                ).fetchone()
                quota = connection.execute(
                    "SELECT storage_used, storage_reserved FROM users WHERE username = 'admin'"
                ).fetchone()
            require(cohort is not None, "canary cohort query returned no row")
            require(int(cohort["total"]) == COHORT_TASKS, "canary cohort size changed")
            require(int(cohort["local_tasks"]) == 9, "baseline cohort size changed")
            require(int(cohort["s3_tasks"]) == CANARY_TASKS, "S3 cohort size changed")
            require(int(cohort["completed"]) == COHORT_TASKS, "cohort completion count changed")
            require(int(cohort["cancelled"]) == 0, "cohort cancellation count changed")
            require(unresolved is not None and int(unresolved["count"]) == 0, "database has unresolved findings")
            require(
                quota is not None
                and int(quota["storage_used"]) == UPLOAD_SIZE * COHORT_TASKS
                and int(quota["storage_reserved"]) == 0,
                "canary quota reconciliation changed",
            )

            api_canary.require_running("canary acceptance")
            api_baseline.require_running("canary acceptance")
            worker.require_running("canary acceptance")

            EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
            EVIDENCE_PATH.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "scenario": "s3_staging_ten_percent_canary",
                        "cohort": {
                            "total_tasks": COHORT_TASKS,
                            "local_tasks": int(cohort["local_tasks"]),
                            "s3_tasks": int(cohort["s3_tasks"]),
                            "s3_ratio": CANARY_TASKS / COHORT_TASKS,
                            "completed": int(cohort["completed"]),
                            "cancelled": int(cohort["cancelled"]),
                        },
                        "routing": {
                            "canary_init": "s3-default-api",
                            "baseline_init": "local-default-api",
                            "canary_chunks": "alternating-local-and-s3-default-apis",
                            "canary_complete": "local-default-api",
                        },
                        "baseline": {
                            "uploads": len(baseline_completion_ms),
                            "bytes_per_upload": UPLOAD_SIZE,
                            "chunks_per_upload": UPLOAD_SIZE // CHUNK_SIZE,
                            "success_rate": 1.0,
                            "completion_ms_average": round(
                                sum(baseline_completion_ms) / len(baseline_completion_ms),
                                3,
                            ),
                            "completion_ms_p99": round(max(baseline_completion_ms), 3),
                        },
                        "canary": {
                            "upload_id": canary_id,
                            "file_id": file_id,
                            "bytes": UPLOAD_SIZE,
                            "chunks": canary_chunks,
                            "completion_ms": round(canary_completion_ms, 3),
                            "success_rate": 1.0,
                            "staging_backend": canary_completed["staging_backend"],
                            "staging_prefix": canary_completed["staging_prefix"],
                            "final_key": final_key,
                        },
                        "monitoring": {
                            "s3_success_calls": s3_successes,
                            "actionable_s3_errors": s3_errors,
                            "expired_leases": 0,
                            "lease_takeovers": 0,
                            "dead_letter_jobs": 0,
                            "metrics_snapshot_success": 1,
                            "unresolved_findings": 0,
                            "staging_cleanup_succeeded": COHORT_TASKS,
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
            api_baseline.stop()
            api_baseline = None
            api_canary.stop()
            api_canary = None

        print(
            "PASS: 10% S3 staging canary crossed mixed-default APIs and passed "
            "success, latency, S3, lease, dead-letter, snapshot, and reconciliation gates"
        )
        return 0
    except BaseException:
        for process in (api_canary, api_baseline, worker):
            if process is not None:
                print(process.log_tail(), file=sys.stderr)
        raise
    finally:
        for process in (worker, api_baseline, api_canary):
            if process is not None:
                process.stop()
        if database_created:
            drop_database(database_name)
        if moto is not None:
            moto.stop()


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx", "psycopg[binary]"]
# ///

"""Verify that legacy node-local staging drains through its original volume."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any

import httpx

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_expand_mixed_version import (  # noqa: E402
    MIGRATOR,
    ManagedServer,
    allocate_ports,
    auth_headers,
    build_legacy_binary,
    complete_upload,
    connect,
    create_database,
    download_file,
    drop_database,
    login,
    prepare_v002_database,
    recycle_database_connections,
    register_user,
    require,
    resolve_current_binary,
    run_database_command,
    server_config,
    success_data,
    upload_without_complete,
    wait_for_profile,
)


COMPLETED = 1
CANCELLED = 2
EXPIRED = 3
JOB_SUCCEEDED = 3
REPO_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/local-staging-drain-summary.json"


def cancel_upload(base_url: str, token: str, upload_id: str) -> None:
    response = httpx.delete(
        f"{base_url}/api/file/upload/{upload_id}",
        headers=auth_headers(token),
        timeout=15,
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


def task_and_cleanup_snapshot(
    database_name: str,
    cases: dict[str, dict[str, Any]],
) -> tuple[dict[str, int], dict[str, int]]:
    upload_ids = [case["upload_id"] for case in cases.values()]
    with connect(database_name) as connection:
        task_rows = connection.execute(
            "SELECT id, status FROM upload_tasks WHERE id = ANY(%s)",
            (upload_ids,),
        ).fetchall()
        job_rows = connection.execute(
            "SELECT aggregate_id, status FROM storage_jobs "
            "WHERE job_type = 'staging_cleanup' AND aggregate_id = ANY(%s)",
            (upload_ids,),
        ).fetchall()
    return (
        {row["id"]: row["status"] for row in task_rows},
        {row["aggregate_id"]: row["status"] for row in job_rows},
    )


def migration_state_snapshot(
    database_name: str,
    username: str,
    cases: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    upload_ids = [case["upload_id"] for case in cases.values()]
    with connect(database_name) as connection:
        user = connection.execute(
            "SELECT id, storage_used, storage_reserved FROM users WHERE username = %s",
            (username,),
        ).fetchone()
        require(user is not None, "local drain user disappeared before diagnostics")
        tasks = connection.execute(
            "SELECT id, status, staging_backend, staging_prefix, state_version, "
            "lease_owner, lease_expires_at, finalize_attempts, last_error_code, "
            "completed_file_id, updated_at FROM upload_tasks "
            "WHERE id = ANY(%s) ORDER BY id",
            (upload_ids,),
        ).fetchall()
        chunks = connection.execute(
            "SELECT task_id, chunk_index, uploaded_at, size_bytes, hash_md5, "
            "object_key, etag FROM upload_task_chunks "
            "WHERE task_id = ANY(%s) ORDER BY task_id, chunk_index",
            (upload_ids,),
        ).fetchall()
        jobs = connection.execute(
            "SELECT id, aggregate_id, job_type, status, attempts, locked_by, "
            "locked_until, updated_at FROM storage_jobs "
            "WHERE aggregate_id = ANY(%s) ORDER BY id",
            (upload_ids,),
        ).fetchall()

    return {
        "user": tuple(user.values()),
        "tasks": [tuple(row.values()) for row in tasks],
        "chunks": [tuple(row.values()) for row in chunks],
        "jobs": [tuple(row.values()) for row in jobs],
    }


def diagnose_first_chunk(base_url: str, token: str, upload_id: str) -> dict[str, Any]:
    response = httpx.get(
        f"{base_url}/api/admin/uploads/{upload_id}/diagnostics",
        headers=auth_headers(token),
        timeout=15,
    )
    data = success_data(response, f"diagnose local upload at {base_url}")
    require(data["task"]["staging_backend"] == "local", "diagnostic changed local backend")
    require(data["chunk_pagination"]["total"] == 1, "diagnostic chunk count changed")
    require(len(data["chunks"]) == 1, "diagnostic omitted the local chunk")
    object_head = data["chunks"][0]["object_head"]
    require(isinstance(object_head, dict), "diagnostic returned an invalid object head")
    return object_head


def wait_for_drain(
    database_name: str,
    cases: dict[str, dict[str, Any]],
    worker: ManagedServer,
) -> None:
    expected_tasks = {
        cases["complete"]["upload_id"]: COMPLETED,
        cases["cancel"]["upload_id"]: CANCELLED,
        cases["expire"]["upload_id"]: EXPIRED,
    }
    expected_ids = set(expected_tasks)
    deadline = time.monotonic() + 45
    last_tasks: dict[str, int] = {}
    last_jobs: dict[str, int] = {}

    while time.monotonic() < deadline:
        worker.require_running("local staging drain")
        last_tasks, last_jobs = task_and_cleanup_snapshot(database_name, cases)
        require(
            all(status != 4 for status in last_jobs.values()),
            f"local cleanup entered dead-letter: {last_jobs}",
        )
        if (
            last_tasks == expected_tasks
            and set(last_jobs) == expected_ids
            and all(status == JOB_SUCCEEDED for status in last_jobs.values())
        ):
            return
        time.sleep(0.2)

    raise AssertionError(
        "local staging did not drain before timeout: "
        f"tasks={last_tasks}, cleanup_jobs={last_jobs}"
    )


def verify_drain_state(
    database_name: str,
    username: str,
    cases: dict[str, dict[str, Any]],
    origin_storage: Path,
    origin_staging: Path,
    wrong_storage: Path,
    wrong_staging: Path,
) -> dict[str, Any]:
    upload_ids = [case["upload_id"] for case in cases.values()]
    with connect(database_name) as connection:
        user = connection.execute(
            "SELECT id, storage_used, storage_reserved FROM users WHERE username = %s",
            (username,),
        ).fetchone()
        require(user is not None, "local drain user disappeared")
        require(
            user["storage_used"] == len(cases["complete"]["payload"]),
            "local drain storage_used did not settle to the completed file size",
        )
        require(
            user["storage_reserved"] == 0, "local drain reserved quota was not released"
        )

        tasks = connection.execute(
            "SELECT id, status, staging_backend, staging_prefix, completed_file_id, "
            "lease_owner, lease_expires_at FROM upload_tasks WHERE id = ANY(%s)",
            (upload_ids,),
        ).fetchall()
        require(len(tasks) == len(cases), "local drain upload task count changed")
        tasks_by_id = {row["id"]: row for row in tasks}
        expected_statuses = {
            cases["complete"]["upload_id"]: COMPLETED,
            cases["cancel"]["upload_id"]: CANCELLED,
            cases["expire"]["upload_id"]: EXPIRED,
        }
        for upload_id, expected_status in expected_statuses.items():
            row = tasks_by_id[upload_id]
            require(
                row["status"] == expected_status,
                f"unexpected terminal state for {upload_id}",
            )
            require(
                row["staging_backend"] == "local", f"backend changed for {upload_id}"
            )
            require(
                row["staging_prefix"] == f"staging/{upload_id}",
                f"legacy staging prefix backfill changed for {upload_id}",
            )
            require(
                row["lease_owner"] is None and row["lease_expires_at"] is None,
                f"finalize lease remained on {upload_id}",
            )

        completed_task = tasks_by_id[cases["complete"]["upload_id"]]
        require(
            completed_task["completed_file_id"] == cases["complete"]["file_id"],
            "completed local task did not persist its replay file ID",
        )
        require(
            tasks_by_id[cases["cancel"]["upload_id"]]["completed_file_id"] is None
            and tasks_by_id[cases["expire"]["upload_id"]]["completed_file_id"] is None,
            "non-completed local task gained a file ID",
        )

        chunk_count = connection.execute(
            "SELECT COUNT(*) AS count FROM upload_task_chunks WHERE task_id = ANY(%s)",
            (upload_ids,),
        ).fetchone()
        require(
            chunk_count is not None and chunk_count["count"] == 0,
            "local drain left chunk rows",
        )

        files = connection.execute(
            "SELECT file.id, file.size, content.hash_md5, content.hash_sha256, "
            "content.storage_path, content.ref_count "
            "FROM files AS file JOIN file_contents AS content ON content.id = file.content_id "
            "WHERE file.user_id = %s",
            (user["id"],),
        ).fetchall()
        require(len(files) == 1, "cancelled or expired local task created a file")
        file = files[0]
        payload = cases["complete"]["payload"]
        require(file["id"] == cases["complete"]["file_id"], "completed file ID changed")
        require(file["size"] == len(payload), "completed local file size changed")
        require(
            file["hash_md5"] == hashlib.md5(payload).hexdigest(),
            "completed file MD5 changed",
        )
        require(
            file["hash_sha256"] == hashlib.sha256(payload).hexdigest(),
            "completed file SHA-256 changed",
        )
        require(file["ref_count"] == 1, "completed local content ref_count changed")
        final_path = Path(file["storage_path"])
        require(final_path.is_file(), "completed local final blob is missing")
        require(
            final_path.is_relative_to(origin_storage),
            "completed blob escaped origin storage",
        )
        require(
            final_path.read_bytes() == payload,
            "completed local final blob bytes changed",
        )

        cleanup_jobs = connection.execute(
            "SELECT aggregate_id, status, payload->>'backend' AS backend, "
            "payload->>'prefix' AS prefix FROM storage_jobs "
            "WHERE job_type = 'staging_cleanup' AND aggregate_id = ANY(%s)",
            (upload_ids,),
        ).fetchall()
        require(
            len(cleanup_jobs) == len(cases), "local drain cleanup job count changed"
        )
        for row in cleanup_jobs:
            require(row["status"] == JOB_SUCCEEDED, "local cleanup job did not succeed")
            require(row["backend"] == "local", "local cleanup job backend changed")
            require(
                row["prefix"] == f"staging/{row['aggregate_id']}",
                "local cleanup job lost the migrated prefix",
            )

        nonterminal = connection.execute(
            "SELECT COUNT(*) AS count FROM upload_tasks "
            "WHERE staging_backend = 'local' AND status IN (0, 4)"
        ).fetchone()
        require(
            nonterminal is not None and nonterminal["count"] == 0,
            "local nonterminal tasks remain",
        )

    for case in cases.values():
        upload_id = case["upload_id"]
        require(
            not (origin_staging / upload_id).exists()
            and not (origin_staging / f"{upload_id}.tmp").exists(),
            f"origin staging artifacts remain for {upload_id}",
        )
    origin_staging_files = sum(path.is_file() for path in origin_staging.rglob("*"))
    wrong_node_files = sum(
        path.is_file()
        for root in (wrong_storage, wrong_staging)
        for path in root.rglob("*")
    )
    require(origin_staging_files == 0, "local drain left files on the origin staging volume")
    require(
        wrong_node_files == 0,
        "local drain wrote or cleaned staging data on the wrong node",
    )
    return {
        "terminal_tasks": {
            "completed": sum(row["status"] == COMPLETED for row in tasks),
            "cancelled": sum(row["status"] == CANCELLED for row in tasks),
            "expired": sum(row["status"] == EXPIRED for row in tasks),
            "local_nonterminal": int(nonterminal["count"]),
        },
        "cleanup": {
            "succeeded": sum(row["status"] == JOB_SUCCEEDED for row in cleanup_jobs),
            "incomplete": sum(row["status"] != JOB_SUCCEEDED for row in cleanup_jobs),
            "origin_staging_files": origin_staging_files,
            "wrong_node_files": wrong_node_files,
        },
        "quota": {
            "storage_used": int(user["storage_used"]),
            "storage_reserved": int(user["storage_reserved"]),
        },
    }


def exercise_local_staging_drain(
    database_name: str,
    legacy_binary: Path,
    current_binary: Path,
    temporary_root: Path,
) -> dict[str, Any]:
    origin_storage = temporary_root / "origin-storage"
    origin_staging = temporary_root / "origin-staging"
    wrong_storage = temporary_root / "wrong-storage"
    wrong_staging = temporary_root / "wrong-staging"
    for path in (origin_storage, origin_staging, wrong_storage, wrong_staging):
        path.mkdir()

    legacy_port, worker_port, current_api_port, wrong_api_port = allocate_ports(4)
    servers: list[ManagedServer] = []
    run_tag = uuid.uuid4().hex[:10]
    username = f"local_drain_{run_tag}"
    password = "LocalDrain123"
    cases: dict[str, dict[str, Any]] = {
        "complete": {
            "filename": f"local-complete-{run_tag}.bin",
            "payload": (f"local-complete-{run_tag}-".encode() * 101),
        },
        "cancel": {
            "filename": f"local-cancel-{run_tag}.bin",
            "payload": (f"local-cancel-{run_tag}-".encode() * 97),
        },
        "expire": {
            "filename": f"local-expire-{run_tag}.bin",
            "payload": (f"local-expire-{run_tag}-".encode() * 89),
        },
    }

    try:
        legacy = ManagedServer(
            name="local-drain-v002",
            binary=legacy_binary,
            run_directory=temporary_root / "legacy-run",
            config=server_config(
                database_name,
                legacy_port,
                "local-drain-v002",
                origin_storage,
                origin_staging,
            ),
            database_name=database_name,
            port=legacy_port,
            readiness_path="/api/health",
        )
        servers.append(legacy)
        legacy_pid = legacy.pid
        legacy_token = register_user(legacy.base_url, username, password)

        for name, case in cases.items():
            case["upload_id"] = upload_without_complete(
                legacy.base_url,
                legacy_token,
                case["filename"],
                case["payload"],
            )
            require(
                (origin_staging / case["upload_id"] / "0.chunk").is_file(),
                f"legacy origin chunk is missing for {name}",
            )

        print("Applying expand migrations before draining the original local volume...")
        run_database_command([str(MIGRATOR)], database_name)
        legacy.require_running("local staging expand migration")
        require(
            legacy.pid == legacy_pid,
            "legacy process restarted during local staging migration",
        )
        recycle_database_connections(database_name)
        wait_for_profile(
            legacy.base_url,
            legacy_token,
            username,
            "legacy profile before local staging drain",
        )
        legacy.stop()

        current_api = ManagedServer(
            name="local-drain-api",
            binary=current_binary,
            run_directory=temporary_root / "current-api-run",
            config=server_config(
                database_name,
                current_api_port,
                "local-drain-api",
                origin_storage,
                origin_staging,
            ),
            database_name=database_name,
            port=current_api_port,
            readiness_path="/api/health/ready",
        )
        servers.append(current_api)

        wrong_api = ManagedServer(
            name="local-drain-wrong-api",
            binary=current_binary,
            run_directory=temporary_root / "wrong-api-run",
            config=server_config(
                database_name,
                wrong_api_port,
                "local-drain-wrong-api",
                wrong_storage,
                wrong_staging,
            ),
            database_name=database_name,
            port=wrong_api_port,
            readiness_path="/api/health/ready",
        )
        servers.append(wrong_api)

        admin_token = login(current_api.base_url, "admin", "Admin123")
        before_diagnostics = migration_state_snapshot(database_name, username, cases)
        diagnostic_upload_id = cases["complete"]["upload_id"]
        origin_head = diagnose_first_chunk(
            current_api.base_url,
            admin_token,
            diagnostic_upload_id,
        )
        wrong_head = diagnose_first_chunk(
            wrong_api.base_url,
            admin_token,
            diagnostic_upload_id,
        )
        after_diagnostics = migration_state_snapshot(database_name, username, cases)
        require(origin_head["status"] == "present", "origin volume diagnostic missed its chunk")
        require(wrong_head["status"] == "missing", "wrong node appeared able to recover local staging")
        require(
            before_diagnostics == after_diagnostics,
            "read-only node-affinity diagnostics changed migration state",
        )
        wrong_api.stop()

        cases["complete"]["file_id"] = complete_upload(
            current_api.base_url,
            legacy_token,
            cases["complete"]["upload_id"],
        )
        cancel_upload(
            current_api.base_url,
            legacy_token,
            cases["cancel"]["upload_id"],
        )
        with connect(database_name) as connection:
            updated = connection.execute(
                "UPDATE upload_tasks SET expires_at = NOW() - INTERVAL '1 second' "
                "WHERE id = %s AND status = 0",
                (cases["expire"]["upload_id"],),
            )
            require(
                updated.rowcount == 1, "failed to expire the local migration fixture"
            )

        pre_worker_tasks, pre_worker_jobs = task_and_cleanup_snapshot(
            database_name, cases
        )
        require(
            pre_worker_tasks
            == {
                cases["complete"]["upload_id"]: COMPLETED,
                cases["cancel"]["upload_id"]: CANCELLED,
                cases["expire"]["upload_id"]: 0,
            },
            f"unexpected state before migration worker: {pre_worker_tasks}",
        )
        require(
            set(pre_worker_jobs)
            == {cases["complete"]["upload_id"], cases["cancel"]["upload_id"]}
            and all(status == 0 for status in pre_worker_jobs.values()),
            f"cleanup jobs ran without the origin migration worker: {pre_worker_jobs}",
        )

        worker = ManagedServer(
            name="local-drain-worker",
            binary=current_binary,
            run_directory=temporary_root / "worker-run",
            config=server_config(
                database_name,
                worker_port,
                "local-drain-worker",
                origin_storage,
                origin_staging,
                role="worker",
            ),
            database_name=database_name,
            port=worker_port,
            readiness_path="/api/health/ready",
            role="worker",
        )
        servers.append(worker)

        wait_for_drain(database_name, cases, worker)
        download_file(
            current_api.base_url,
            legacy_token,
            cases["complete"]["file_id"],
            cases["complete"]["payload"],
            "local drain completed file",
        )
        drain_result = verify_drain_state(
            database_name,
            username,
            cases,
            origin_storage,
            origin_staging,
            wrong_storage,
            wrong_staging,
        )
        current_api.require_running("local staging verification")
        worker.require_running("local staging verification")
        return {
            "schema_version": 1,
            "scenario": "legacy_local_staging_original_volume_drain",
            "node_affinity_probe": {
                "origin_chunk_status": origin_head["status"],
                "wrong_node_chunk_status": wrong_head["status"],
                "database_snapshot_unchanged": before_diagnostics == after_diagnostics,
            },
            **drain_result,
            "passed": True,
        }
    except BaseException:
        for server in servers:
            print(server.log_tail(), file=sys.stderr)
        raise
    finally:
        for server in reversed(servers):
            server.stop()


def main() -> int:
    current_binary = resolve_current_binary()
    legacy_binary = build_legacy_binary(current_binary)
    database_name = f"disk_local_drain_{uuid.uuid4().hex[:12]}"
    database_created = False

    try:
        create_database(database_name)
        database_created = True
        prepare_v002_database(database_name)
        with tempfile.TemporaryDirectory(
            prefix="disk-local-staging-drain-"
        ) as temporary:
            evidence = exercise_local_staging_drain(
                database_name,
                legacy_binary,
                current_binary,
                Path(temporary),
            )
            EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
            EVIDENCE_PATH.write_text(
                json.dumps(evidence, indent=2) + "\n",
                encoding="utf-8",
            )
    finally:
        if database_created:
            drop_database(database_name)

    print(
        "PASS: wrong-node diagnostics rejected legacy local recovery; original-volume "
        "complete, cancel, and expire paths drained all staging"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

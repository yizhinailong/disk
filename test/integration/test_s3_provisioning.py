#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["boto3"]
# ///

"""Exercise least-privilege provisioning against an isolated real MinIO."""

from __future__ import annotations

import hashlib
import json
import os
import signal
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path
from typing import Any, Callable

import boto3
from botocore.config import Config
from botocore.exceptions import ClientError


REPO_ROOT = Path(__file__).resolve().parents[2]
PROVISION_SCRIPT = REPO_ROOT / "deploy" / "minio" / "provision.sh"
POLICY_PATH = REPO_ROOT / "deploy" / "minio" / "app-policy.json"
LIFECYCLE_PATH = REPO_ROOT / "deploy" / "minio" / "lifecycle.json"
EVIDENCE_PATH = REPO_ROOT / ".sisyphus" / "evidence" / "s3-provisioning-summary.json"
BUCKET = "disk"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def executable_from_env(name: str) -> Path:
    configured = os.environ.get(name, "")
    require(bool(configured), f"{name} must point to an executable")
    candidate = Path(configured)
    path = candidate if candidate.is_absolute() else (REPO_ROOT / candidate)
    require(
        path.is_file() and os.access(path, os.X_OK), f"{name} is not executable: {path}"
    )
    return path.resolve()


def allocate_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def wait_for_minio(
    endpoint: str, process: subprocess.Popen[bytes], log_path: Path
) -> None:
    deadline = time.monotonic() + 60
    health_url = f"{endpoint}/minio/health/live"
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(
                f"MinIO exited before readiness with {process.returncode}:\n"
                f"{log_path.read_text(encoding='utf-8', errors='replace')}"
            )
        try:
            with urllib.request.urlopen(health_url, timeout=1) as response:
                if response.status == 200:
                    return
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.2)
    raise AssertionError("MinIO did not become ready")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def binary_version(path: Path) -> str:
    result = subprocess.run(
        [str(path), "--version"],
        check=True,
        capture_output=True,
        text=True,
    )
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    return lines[0] if lines else path.name


def run_provision(environment: dict[str, str]) -> None:
    result = subprocess.run(
        ["/bin/sh", str(PROVISION_SCRIPT)],
        cwd=REPO_ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"MinIO provisioning failed with {result.returncode}:\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def s3_client(endpoint: str, access_key: str, secret_key: str) -> Any:
    return boto3.client(
        "s3",
        endpoint_url=endpoint,
        aws_access_key_id=access_key,
        aws_secret_access_key=secret_key,
        region_name="us-east-1",
        config=Config(s3={"addressing_style": "path"}),
    )


def require_access_denied(label: str, operation: Callable[[], Any]) -> None:
    try:
        operation()
    except ClientError as error:
        code = str(error.response.get("Error", {}).get("Code", ""))
        status = int(
            error.response.get("ResponseMetadata", {}).get("HTTPStatusCode", 0)
        )
        require(
            code in {"AccessDenied", "AllAccessDisabled"} or status == 403,
            f"{label}: {code}",
        )
        return
    raise AssertionError(f"{label}: operation unexpectedly succeeded")


def write_evidence(evidence: dict[str, Any]) -> None:
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE_PATH.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    gate = "DISK_S3_PROVISIONING_INTEGRATION"
    if os.environ.get(gate) != "1":
        print(f"SKIP: {gate} is not 1; skipping MinIO provisioning integration")
        return 0

    minio_binary = executable_from_env("DISK_MINIO_BIN")
    mc_binary = executable_from_env("DISK_MC_BIN")
    run_id = uuid.uuid4().hex[:10]
    root_access_key = f"root{run_id}"
    root_secret_key = f"RootSecret-{uuid.uuid4().hex}"
    app_access_key = f"app{run_id}"
    app_secret_key = f"AppSecret-{uuid.uuid4().hex}"
    checks: list[str] = []
    evidence: dict[str, Any] = {
        "schema_version": 1,
        "scenario": "s3_least_privilege_provisioning",
        "run_id": run_id,
        "minio_version": binary_version(minio_binary),
        "mc_version": binary_version(mc_binary),
        "policy_sha256": sha256(POLICY_PATH),
        "lifecycle_sha256": sha256(LIFECYCLE_PATH),
        "checks": checks,
        "passed": False,
    }

    process: subprocess.Popen[bytes] | None = None
    log_handle: Any = None
    try:
        with tempfile.TemporaryDirectory(prefix="disk-s3-provisioning-") as temporary:
            root = Path(temporary)
            data = root / "data"
            data.mkdir()
            api_port = allocate_port()
            console_port = allocate_port()
            endpoint = f"http://127.0.0.1:{api_port}"
            log_path = root / "minio.log"
            minio_environment = os.environ.copy()
            minio_environment.update(
                {
                    "MINIO_ROOT_USER": root_access_key,
                    "MINIO_ROOT_PASSWORD": root_secret_key,
                    "MINIO_BROWSER": "off",
                    "MINIO_API_STALE_UPLOADS_EXPIRY": "168h",
                    "MINIO_API_STALE_UPLOADS_CLEANUP_INTERVAL": "6h",
                }
            )
            log_handle = log_path.open("wb")
            process = subprocess.Popen(
                [
                    str(minio_binary),
                    "server",
                    str(data),
                    "--address",
                    f"127.0.0.1:{api_port}",
                    "--console-address",
                    f"127.0.0.1:{console_port}",
                ],
                cwd=root,
                env=minio_environment,
                stdout=log_handle,
                stderr=subprocess.STDOUT,
            )
            wait_for_minio(endpoint, process, log_path)
            checks.append("isolated_minio_ready")

            provision_environment = os.environ.copy()
            provision_environment.update(
                {
                    "MINIO_ROOT_USER": root_access_key,
                    "MINIO_ROOT_PASSWORD": root_secret_key,
                    "DISK_S3_ENDPOINT": endpoint,
                    "DISK_S3_BUCKET": BUCKET,
                    "DISK_S3_ACCESS_KEY": app_access_key,
                    "DISK_S3_SECRET_KEY": app_secret_key,
                    "DISK_MC_BIN": str(mc_binary),
                    "MC_CONFIG_DIR": str(root / "mc"),
                    "DISK_S3_LIFECYCLE_FILE": str(LIFECYCLE_PATH),
                    "DISK_S3_POLICY_FILE": str(POLICY_PATH),
                }
            )
            run_provision(provision_environment)
            run_provision(provision_environment)
            checks.append("provisioning_idempotent")

            minio_api_config = subprocess.run(
                [
                    str(mc_binary),
                    "--config-dir",
                    str(root / "mc"),
                    "admin",
                    "config",
                    "get",
                    "local",
                    "api",
                ],
                check=True,
                capture_output=True,
                text=True,
            ).stdout
            require(
                "# MINIO_API_STALE_UPLOADS_EXPIRY=168h" in minio_api_config,
                f"stale upload expiry drifted: {minio_api_config.strip()}",
            )
            require(
                "# MINIO_API_STALE_UPLOADS_CLEANUP_INTERVAL=6h" in minio_api_config,
                f"stale upload cleanup interval drifted: {minio_api_config.strip()}",
            )
            checks.append("stale_multipart_cleanup_configured")

            root_client = s3_client(endpoint, root_access_key, root_secret_key)
            app_client = s3_client(endpoint, app_access_key, app_secret_key)
            lifecycle = root_client.get_bucket_lifecycle_configuration(Bucket=BUCKET)
            lifecycle_ids = {rule["ID"] for rule in lifecycle.get("Rules", [])}
            require(
                lifecycle_ids == {"expire-upload-staging"},
                f"unexpected lifecycle rules: {lifecycle_ids}",
            )
            checks.append("lifecycle_imported")

            app_client.get_bucket_location(Bucket=BUCKET)
            app_client.list_objects_v2(Bucket=BUCKET, Prefix="staging/")
            app_client.list_objects_v2(Bucket=BUCKET, Prefix="objects/")
            checks.append("allowed_prefix_listing")

            staging_key = f"staging/integration/{run_id}/source.bin"
            final_key = f"objects/integration/{run_id}/final.bin"
            payload = b"disk-s3-provisioning-payload\n"
            app_client.put_object(Bucket=BUCKET, Key=staging_key, Body=payload)
            head = app_client.head_object(Bucket=BUCKET, Key=staging_key)
            require(
                int(head["ContentLength"]) == len(payload), "staging HEAD size mismatch"
            )
            app_client.copy_object(
                Bucket=BUCKET,
                Key=final_key,
                CopySource={"Bucket": BUCKET, "Key": staging_key},
            )
            copied = app_client.get_object(Bucket=BUCKET, Key=final_key)["Body"].read()
            require(copied == payload, "server-side copy payload mismatch")
            checks.append("object_put_head_get_copy")

            multipart_key = f"staging/integration/{run_id}/multipart.bin"
            created = app_client.create_multipart_upload(
                Bucket=BUCKET, Key=multipart_key
            )
            uploaded = app_client.upload_part(
                Bucket=BUCKET,
                Key=multipart_key,
                UploadId=created["UploadId"],
                PartNumber=1,
                Body=b"multipart-payload",
            )
            app_client.complete_multipart_upload(
                Bucket=BUCKET,
                Key=multipart_key,
                UploadId=created["UploadId"],
                MultipartUpload={
                    "Parts": [{"ETag": uploaded["ETag"], "PartNumber": 1}]
                },
            )
            abort_key = f"staging/integration/{run_id}/abort.bin"
            abort_upload = app_client.create_multipart_upload(
                Bucket=BUCKET, Key=abort_key
            )
            app_client.abort_multipart_upload(
                Bucket=BUCKET,
                Key=abort_key,
                UploadId=abort_upload["UploadId"],
            )
            checks.append("multipart_complete_and_abort")

            require_access_denied(
                "outside-prefix write",
                lambda: app_client.put_object(
                    Bucket=BUCKET,
                    Key=f"outside/{run_id}.bin",
                    Body=b"forbidden",
                ),
            )
            require_access_denied(
                "outside-prefix listing",
                lambda: app_client.list_objects_v2(Bucket=BUCKET, Prefix="outside/"),
            )
            require_access_denied(
                "unscoped bucket head", lambda: app_client.head_bucket(Bucket=BUCKET)
            )
            require_access_denied(
                "lifecycle administration",
                lambda: app_client.put_bucket_lifecycle_configuration(
                    Bucket=BUCKET,
                    LifecycleConfiguration={"Rules": []},
                ),
            )
            visible_buckets = {
                item["Name"] for item in app_client.list_buckets().get("Buckets", [])
            }
            require(
                visible_buckets == {BUCKET},
                f"application identity can see unexpected buckets: {visible_buckets}",
            )
            checks.append("out_of_scope_and_admin_denied")

            for key in (staging_key, final_key, multipart_key):
                app_client.delete_object(Bucket=BUCKET, Key=key)
            remaining = []
            for prefix in ("staging/", "objects/"):
                remaining.extend(
                    item["Key"]
                    for item in app_client.list_objects_v2(
                        Bucket=BUCKET, Prefix=prefix
                    ).get("Contents", [])
                )
            require(not remaining, f"provisioning test objects remain: {remaining}")
            checks.append("test_objects_cleaned")

        evidence["passed"] = True
        write_evidence(evidence)
        print(
            "PASS: MinIO lifecycle and least-privilege application policy are enforced"
        )
        return 0
    except Exception as error:  # noqa: BLE001 - persist a credential-free failure summary
        evidence["error"] = str(error)
        write_evidence(evidence)
        print(f"FAIL: {error}")
        return 1
    finally:
        if process is not None and process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if log_handle is not None:
            log_handle.close()


if __name__ == "__main__":
    raise SystemExit(main())

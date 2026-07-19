#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///

"""Static contract checks for the local distributed deployment topology."""

from __future__ import annotations

import json
import re
from pathlib import Path

import yaml


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    compose = yaml.safe_load((root / "docker-compose.distributed.yml").read_text(encoding="utf-8"))
    services = compose["services"]
    expected_services = {
        "postgres",
        "redis",
        "minio",
        "minio-init",
        "api-a",
        "api-b",
        "worker-a",
        "worker-b",
        "load-balancer",
    }
    require(set(services) == expected_services, "distributed Compose service set drifted")

    app_names = ("api-a", "api-b", "worker-a", "worker-b")
    images = {services[name]["image"] for name in app_names}
    require(images == {"disk-distributed:local"}, "application replicas must use one image")
    require(
        all(services[name]["build"]["dockerfile"] == "Dockerfile" for name in app_names),
        "application replicas must share one Dockerfile",
    )

    expected_runtime = {
        "api-a": ("api", "disk-api-a"),
        "api-b": ("api", "disk-api-b"),
        "worker-a": ("worker", "disk-worker-a"),
        "worker-b": ("worker", "disk-worker-b"),
    }
    for name, (role, instance_id) in expected_runtime.items():
        environment = services[name]["environment"]
        require(environment["DISK_PROCESS_ROLE"] == role, f"{name} role drifted")
        require(environment["DISK_INSTANCE_ID"] == instance_id, f"{name} instance ID drifted")
        require(environment["DISK_STORAGE_BACKEND"] == "s3", f"{name} final storage is not S3")
        require(
            environment["DISK_UPLOAD_STAGING_BACKEND"] == "s3",
            f"{name} staging storage is not S3",
        )

    require(
        services["postgres"]["ports"][0].startswith("127.0.0.1:"),
        "PostgreSQL host port must bind loopback",
    )
    require(
        services["redis"]["ports"][0].startswith("127.0.0.1:"),
        "Redis host port must bind loopback",
    )
    postgres_command = " ".join(services["postgres"]["command"])
    for setting in ("statement_timeout", "lock_timeout", "idle_in_transaction_session_timeout"):
        require(setting in postgres_command, f"PostgreSQL {setting} is missing")

    runtime_config = json.loads(
        (root / "deploy/config.distributed.json").read_text(encoding="utf-8")
    )
    require(runtime_config["db_clients"][0]["passwd"] == "", "database password leaked into JSON")
    require(runtime_config["redis_clients"][0]["passwd"] == "", "Redis password leaked into JSON")
    disk_config = runtime_config["custom_config"]["disk"]
    require(disk_config["storage_backend"] == "s3", "distributed final backend drifted")
    require(disk_config["upload_staging_backend"] == "s3", "distributed staging backend drifted")

    nginx_lines = []
    for raw_line in (root / "deploy/nginx/disk.conf").read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if line:
            nginx_lines.append(line)
    nginx = "\n".join(nginx_lines)
    require(not re.search(r"\bip_hash\s*;", nginx), "load balancer must not use ip_hash")
    require("sticky" not in nginx.lower(), "load balancer must not use sticky routing")
    require("non_idempotent" not in nginx, "proxy must not retry non-idempotent requests")
    for header in ("X-Real-IP", "X-Forwarded-For", "X-Forwarded-Proto", "X-Request-Id"):
        require(f"proxy_set_header {header}" in nginx, f"proxy header {header} is missing")
    require("proxy_request_buffering off;" in nginx, "streaming request forwarding is missing")
    require("location = /metrics" in nginx, "public metrics deny route is missing")
    require("return 404;" in nginx, "public metrics route must return 404")

    alert_groups = yaml.safe_load(
        (root / "deploy/prometheus/disk-alerts.yml").read_text(encoding="utf-8")
    )["groups"]
    alerts = {
        rule["alert"]: rule
        for group in alert_groups
        for rule in group["rules"]
    }
    expected_alerts = {
        "DiskInstanceDown",
        "DiskMetricsSnapshotFailed",
        "DiskApiHighErrorRate",
        "DiskApiP99LatencyHigh",
        "DiskStorageJobBacklog",
        "DiskStorageJobDeadLetter",
        "DiskStorageJobRepeatedTakeover",
        "DiskReconciliationFindings",
    }
    require(set(alerts) == expected_alerts, "distributed alert rule set drifted")
    require(
        all(rule.get("for") for rule in alerts.values()),
        "every distributed alert must have a hold duration",
    )

    print("PASS: distributed topology contract is valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

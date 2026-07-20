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


def compact_expression(rule: dict[str, object]) -> str:
    return " ".join(str(rule["expr"]).split())


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

    canary_override = yaml.safe_load(
        (root / "deploy/docker-compose.staging-canary.yml").read_text(encoding="utf-8")
    )
    require(
        canary_override
        == {
            "services": {
                "api-a": {
                    "environment": {"DISK_UPLOAD_STAGING_BACKEND": "s3"}
                },
                "api-b": {
                    "environment": {"DISK_UPLOAD_STAGING_BACKEND": "local"}
                },
            }
        },
        "staging canary override must change only the two API defaults",
    )

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
        require(
            services[name]["stop_grace_period"]
            == "${DISK_STOP_GRACE_PERIOD:-40s}",
            f"{name} termination grace no longer protects application drain",
        )
        require(environment["DISK_STORAGE_BACKEND"] == "s3", f"{name} final storage is not S3")
        require(
            environment["DISK_UPLOAD_STAGING_BACKEND"]
            == "${DISK_UPLOAD_STAGING_BACKEND:-s3}",
            f"{name} staging rollout override is missing",
        )
        require(
            environment["DISK_UPLOAD_TASK_CREATION_ENABLED"]
            == "${DISK_UPLOAD_TASK_CREATION_ENABLED:-true}",
            f"{name} upload creation cutoff override is missing",
        )

    env_example = set(
        (root / "deploy/distributed.env.example").read_text(encoding="utf-8").splitlines()
    )
    require(
        "DISK_UPLOAD_STAGING_BACKEND=s3" in env_example,
        "distributed env template must persist the final S3 staging default",
    )
    require(
        "DISK_UPLOAD_TASK_CREATION_ENABLED=true" in env_example,
        "distributed env template must expose the rollback creation cutoff",
    )
    require(
        "DISK_STOP_GRACE_PERIOD=40s" in env_example,
        "distributed env template must expose the termination grace",
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
    require(runtime_config["app"]["threads_num"] == 4, "HTTP thread recommendation drifted")
    require(runtime_config["db_clients"][0]["passwd"] == "", "database password leaked into JSON")
    require(runtime_config["redis_clients"][0]["passwd"] == "", "Redis password leaked into JSON")
    require(
        runtime_config["db_clients"][0]["connection_number"] == 8,
        "PostgreSQL per-process pool recommendation drifted",
    )
    require(
        runtime_config["redis_clients"][0]["number_of_connections"] == 4,
        "Redis per-process pool recommendation drifted",
    )
    disk_config = runtime_config["custom_config"]["disk"]
    require(disk_config["storage_backend"] == "s3", "distributed final backend drifted")
    require(disk_config["upload_staging_backend"] == "s3", "distributed staging backend drifted")
    require(disk_config["auth_cpu_pool_threads"] == 4, "auth CPU pool recommendation drifted")
    require(disk_config["assembly_max_concurrent"] == 2, "assembly concurrency drifted")
    require(
        disk_config["worker_claiming_enabled"] is True,
        "final distributed profile must enable Worker claiming",
    )
    require(
        disk_config["worker_drain_timeout_seconds"] == 30,
        "Worker drain timeout drifted beyond the reviewed Compose grace",
    )
    require(disk_config["worker_concurrency"] == 1, "Worker concurrency drifted")
    require(disk_config["s3"]["max_connections"] == 16, "S3 connection pool drifted")
    require(disk_config["s3"]["io_threads"] == 4, "S3 I/O thread pool drifted")

    expected_compose_pools = {
        "DATABASE_POOL_SIZE": "${DISK_DATABASE_POOL_SIZE:-8}",
        "REDIS_POOL_SIZE": "${DISK_REDIS_POOL_SIZE:-4}",
        "DISK_S3_MAX_CONNECTIONS": "${DISK_S3_MAX_CONNECTIONS:-16}",
        "DISK_S3_IO_THREADS": "${DISK_S3_IO_THREADS:-4}",
    }
    for name in app_names:
        environment = services[name]["environment"]
        for key, expected in expected_compose_pools.items():
            require(environment[key] == expected, f"{name} {key} default drifted")

    for name in ("worker-a", "worker-b"):
        require(
            services[name]["environment"]["DISK_WORKER_CLAIMING_ENABLED"]
            == "${DISK_WORKER_CLAIMING_ENABLED:-true}",
            f"{name} observation override is missing",
        )
    for name in ("api-a", "api-b"):
        require(
            "DISK_WORKER_CLAIMING_ENABLED" not in services[name]["environment"],
            f"{name} must not receive the Worker claiming override",
        )

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
        "DiskInstanceDown": ("2m", "critical"),
        "DiskReadinessFailed": ("2m", "critical"),
        "DiskMetricsSnapshotFailed": ("30s", "warning"),
        "DiskApiHighErrorRate": ("5m", "critical"),
        "DiskApiP99LatencyHigh": ("10m", "warning"),
        "DiskStorageJobBacklog": ("5m", "warning"),
        "DiskStorageJobExpiredLease": ("1m", "warning"),
        "DiskStorageJobDeadLetter": ("1m", "critical"),
        "DiskStorageJobRepeatedTakeover": ("1m", "warning"),
        "DiskS3TransientErrors": ("2m", "warning"),
        "DiskS3PermanentErrors": ("1m", "critical"),
        "DiskReconciliationFindings": ("5m", "warning"),
    }
    require(set(alerts) == set(expected_alerts), "distributed alert rule set drifted")
    require(
        all(rule.get("for") for rule in alerts.values()),
        "every distributed alert must have a hold duration",
    )
    for name, (hold_duration, severity) in expected_alerts.items():
        require(alerts[name]["for"] == hold_duration, f"{name} hold duration drifted")
        require(alerts[name]["labels"]["severity"] == severity, f"{name} severity drifted")

    readiness = compact_expression(alerts["DiskReadinessFailed"])
    for fragment in (
        'sum by (job, instance)',
        'job=~"disk-(api|worker)"',
        'operation="health"',
        'status_class="5xx"',
        "[1m]",
    ):
        require(fragment in readiness, f"readiness alert is missing {fragment}")

    api_error_rate = compact_expression(alerts["DiskApiHighErrorRate"])
    require(api_error_rate.count('job="disk-api"') == 3, "API error alert job scope drifted")
    require('status_class="5xx"' in api_error_rate, "API error numerator drifted")
    require('operation!="health"' in api_error_rate, "API error alert includes health")
    require('operation!="metrics"' in api_error_rate, "API error alert includes metrics")
    require("> 0.01" in api_error_rate, "API error ratio threshold drifted")
    require(">= 20" in api_error_rate, "API error sample threshold drifted")
    require("clamp_min" not in api_error_rate, "API error ratio must not dilute low traffic")

    api_p99 = compact_expression(alerts["DiskApiP99LatencyHigh"])
    require('job="disk-api"' in api_p99, "API P99 alert job scope drifted")
    require("histogram_quantile( 0.99" in api_p99, "API P99 quantile drifted")
    require("[10m]" in api_p99 and "> 1" in api_p99, "API P99 window drifted")

    expired_lease = compact_expression(alerts["DiskStorageJobExpiredLease"])
    require(
        expired_lease == "max(disk_storage_jobs_expired_leases) > 0",
        "expired storage job lease alert drifted",
    )

    transient_s3 = compact_expression(alerts["DiskS3TransientErrors"])
    permanent_s3 = compact_expression(alerts["DiskS3PermanentErrors"])
    require('dependency="s3"' in transient_s3, "transient S3 dependency scope drifted")
    require(
        'outcome=~"timeout|connection|retryable"' in transient_s3,
        "transient S3 outcome set drifted",
    )
    require("[5m]" in transient_s3 and ">= 5" in transient_s3, "transient S3 threshold drifted")
    require('dependency="s3"' in permanent_s3, "permanent S3 dependency scope drifted")
    require(
        'outcome=~"permanent|protocol|other"' in permanent_s3,
        "permanent S3 outcome set drifted",
    )
    require("[5m]" in permanent_s3 and "> 0" in permanent_s3, "permanent S3 threshold drifted")
    for expression in (transient_s3, permanent_s3):
        require("not_found" not in expression, "expected S3 not-found entered an alert")
        require("conflict" not in expression, "expected S3 conflict entered an alert")

    for name in (
        "DiskStorageJobBacklog",
        "DiskStorageJobExpiredLease",
        "DiskStorageJobDeadLetter",
        "DiskReconciliationFindings",
    ):
        require(compact_expression(alerts[name]).startswith("max("), f"{name} must aggregate replicas")

    alert_tests = yaml.safe_load(
        (root / "deploy/prometheus/disk-alerts.test.yml").read_text(encoding="utf-8")
    )
    require(alert_tests["rule_files"] == ["disk-alerts.yml"], "alert test rule path drifted")
    alert_test_source = json.dumps(alert_tests, sort_keys=True)
    for metric in (
        "disk_storage_jobs_oldest_ready_age_seconds",
        "disk_storage_jobs_expired_leases",
        "disk_dependency_calls_total",
    ):
        require(metric in alert_test_source, f"alert test fixture is missing {metric}")

    print("PASS: distributed topology contract is valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

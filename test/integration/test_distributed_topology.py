#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///

"""Static contracts for the distributed topology and its entry documentation."""

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
    readme = (root / "README.md").read_text(encoding="utf-8")
    require(
        "`storage_backend=local` 与 `upload_staging_backend=local`" in readme,
        "README local quick start must keep final and staging storage local",
    )
    require(
        "`storage_backend=s3` 与 `upload_staging_backend=s3`" in readme,
        "README distributed target must use shared S3 final and staging storage",
    )
    require(
        "上传分片和组装文件仍使用本机" not in readme,
        "README regressed to the obsolete node-local staging topology",
    )
    require(
        "docker-compose.distributed.yml" in readme,
        "README must link the authoritative distributed Compose fixture",
    )
    require(
        "DISK_SECURE_MODE=true" in readme,
        "README production guidance must retain the secure-mode gate",
    )

    system_overview = (root / "docs/design/00-系统概述.md").read_text(encoding="utf-8")
    for marker in (
        "StorageJobWorker",
        "UploadStagingStorage",
        "BlobStore",
        "AssemblyConcurrencyLimiter",
        "staging_cleanup",
    ):
        require(marker in system_overview, f"system overview is missing {marker}")
    for obsolete in (
        'subgraph Server["当前服务端层（单实例）"]',
        'ScheduledTasks["定时任务"]',
        'AssemblyPool["文件组装池"]',
        'Staging[("本地上传暂存")]',
        "participant FS as 文件系统",
    ):
        require(obsolete not in system_overview, f"system overview retains obsolete: {obsolete}")

    feature_spec = (root / "docs/design/01-功能需求规格.md").read_text(encoding="utf-8")
    for marker in (
        "不同内容不得覆盖已有分片",
        "staging_cleanup",
        "blob_gc",
        "Worker 异步清理暂存",
    ):
        require(marker in feature_spec, f"feature specification is missing {marker}")
    for obsolete in (
        "重复上传同一分片会覆盖",
        "清理临时分片文件",
        'D4["删除物理文件"]',
        "后台定时任务（每日凌晨执行）",
    ):
        require(obsolete not in feature_spec, f"feature specification retains obsolete: {obsolete}")

    decision_record = (root / "docs/backend-refactor-decisions.md").read_text(
        encoding="utf-8"
    )
    for marker in (
        "upload_staging_backend",
        "Implemented S3-native staging responsibilities",
        "retained for an idempotent retry or reconciliation",
        "multipart_abort",
        "blob_gc",
    ):
        require(marker in decision_record, f"backend decision record is missing {marker}")
    for obsolete in (
        "Possible future S3-native staging responsibilities",
        "deliberately keeps upload chunks",
        "best-effort compensation to delete",
        "S3-native upload staging is not an active requirement",
    ):
        require(obsolete not in decision_record, f"backend decision record retains obsolete: {obsolete}")

    system_test_plan = (root / "docs/design/04-系统测试计划.md").read_text(
        encoding="utf-8"
    )
    require(
        "✅ 代码与仓库级合同已实现；⏳ 目标环境 MinIO、多实例、故障与性能门禁待执行"
        in system_test_plan,
        "system test plan must distinguish repository contracts from target-environment gates",
    )
    require(
        "⏳ ADR-002 重构范围，完成前不得宣称生产级分布式" not in system_test_plan,
        "system test plan retains the obsolete ADR-002 implementation status",
    )

    operations_guide = (root / "docs/design/05-部署运维指南.md").read_text(
        encoding="utf-8"
    )
    for marker in (
        "deploy/systemd/disk@.service",
        "deploy/nginx/includes/upstream.inc",
        "deploy/nginx/includes/proxy-server.inc",
        "deploy/nginx/disk-tls.conf.example",
        "deploy/nginx/upstreams/production.example.inc",
        "DISK_CONFIG_FILE=/etc/disk/config.distributed.json",
        "DATABASE_PASSWORD=your_db_password",
        "DATABASE_POOL_SIZE=8",
        "REDIS_POOL_SIZE=4",
        "disk@api-a",
        "disk@worker-a",
        "后端不解析 -c/--config 命令行参数",
        "S3/MinIO final + staging",
        "id -u disk >/dev/null 2>&1 ||",
        "sudo chown -R root:root /opt/disk",
        "sudo chown -R disk:disk /var/lib/disk /var/log/disk",
        "自管 PostgreSQL 只绑定数据库私网地址",
        "托管 PostgreSQL/Redis 应使用服务商的私网稳定入口",
        'nssm set DiskApi AppEnvironmentExtra "DISK_CONFIG_FILE=',
        'nssm set DiskWorker AppEnvironmentExtra "DISK_CONFIG_FILE=',
        "只恢复明确备份的二进制，不递归删除安装目录",
        "/opt/disk/bin/disk.bak",
        "return 308 https://$server_name$request_uri;",
        "sudo systemctl reload nginx",
        "API 继续使用 `deploy/config.distributed.json` 的 HTTP `8080`",
    ):
        require(marker in operations_guide, f"operations guide is missing {marker}")
    for obsolete in (
        "/etc/systemd/system/disk.service",
        "ExecStart=/opt/disk/bin/disk -c",
        'disk.exe\" \"-c',
        "/var/lib/disk/.config/disk/server/config.json",
        "systemctl reload disk",
        "files[\"files/ (用户文件存储)\"]",
        "uploads[\"uploads/ (上传临时目录)\"]",
        "sudo systemctl start disk\n",
        "sudo systemctl stop disk\n",
        "sudo systemctl restart disk\n",
        "`DB_PASSWORD`",
        '${DB_PASSWORD}',
        '"passwd": "${REDIS_PASSWORD}"',
        "DATABASE_POOL_SIZE=16",
        "REDIS_POOL_SIZE=8",
        "nssm set DiskApi AppEnvironmentExtra \\\n",
        "nssm set DiskWorker AppEnvironmentExtra \\\n",
        "sudo cp -r /opt/disk /opt/disk.bak",
        "sudo rm -rf /opt/disk",
        "sudo chown -R disk:disk /opt/disk",
        "sudo ufw allow 6379/tcp",
        "限制 PostgreSQL 只监听本地",
        "限制 Redis 只监听本地",
        "应显示: 0.0.0.0:6379 或 127.0.0.1:6379",
        'instances/*.env (角色、实例 ID、监听端口)',
        "upstream disk_backend",
        "return 301 https://$server_name$request_uri;",
        "location /api/file/download/",
        '"port": 443,\n      "https": true',
        "sudo systemctl restart nginx",
    ):
        require(obsolete not in operations_guide, f"operations guide retains obsolete: {obsolete}")

    systemd_guide = operations_guide.split("### 5.1 Systemd 服务配置", 1)[1].split(
        "### 5.3 Windows 服务配置", 1
    )[0]
    windows_guide = operations_guide.split("### 5.3 Windows 服务配置", 1)[1].split(
        "### 5.4 API 与 Worker 角色", 1
    )[0]
    for guide_name, guide in (
        ("systemd", systemd_guide),
        ("Windows", windows_guide),
    ):
        require(
            "DISK_INSTANCE_ID=" not in guide,
            f"{guide_name} service example must not reuse a stable lease owner",
        )
    require(
        "由进程每次启动生成" in systemd_guide,
        "systemd guide must document per-process generated instance IDs",
    )
    require(
        "由每次启动的进程生成新 owner" in windows_guide,
        "Windows guide must document per-process generated instance IDs",
    )

    systemd_unit = (root / "deploy/systemd/disk@.service").read_text(
        encoding="utf-8"
    )
    for directive in (
        "User=disk",
        "Group=disk",
        "WorkingDirectory=/opt/disk",
        "Environment=DISK_CONFIG_FILE=/etc/disk/config.distributed.json",
        "EnvironmentFile=/etc/disk/env",
        "EnvironmentFile=/etc/disk/instances/%i.env",
        "ExecStart=/opt/disk/bin/disk",
        "Restart=on-failure",
        "TimeoutStopSec=40s",
        "KillSignal=SIGTERM",
        "UMask=0027",
        "NoNewPrivileges=true",
        "ProtectSystem=strict",
        "ProtectHome=true",
        "ProtectKernelTunables=true",
        "ProtectKernelModules=true",
        "ProtectControlGroups=true",
        "LockPersonality=true",
        "RestrictSUIDSGID=true",
        "StateDirectory=disk",
        "LogsDirectory=disk",
        "ReadWritePaths=/var/lib/disk /var/log/disk",
    ):
        require(directive in systemd_unit, f"systemd template is missing {directive}")
    for obsolete in (
        "User=root",
        "DISK_INSTANCE_ID=",
        " -c ",
        "ExecReload=",
        "Restart=always",
        "/data/disk",
    ):
        require(obsolete not in systemd_unit, f"systemd template retains obsolete: {obsolete}")

    unit_test_plan = (root / "docs/design/06-单元测试用例.md").read_text(
        encoding="utf-8"
    )
    for marker in (
        "test/services/UploadStateMachine_test.cpp | ✅ 已实现",
        "test/integration/test_upload_state_machine.py | ✅ 已实现",
        "test/services/StorageJobRepository_test.cpp + test/integration/test_storage_job_queue.py",
        "test/storage/S3ObjectStorage_test.cpp + test/integration/test_s3_app_flow.py",
        "test/services/TokenServiceRevocation_test.cpp + test/integration/test_distributed_flow.py",
        "目标 S3/MinIO 环境门控",
        "目标双 API 环境门控",
    ):
        require(marker in unit_test_plan, f"unit test inventory is missing {marker}")
    for obsolete in (
        "⏳ ADR-002 待实现",
        "S3UploadStaging_test.cpp",
        "test_token_revocation_cluster.py",
        "现有 + 待新增",
        "分享操作限流测试，待新增",
    ):
        require(obsolete not in unit_test_plan, f"unit test inventory retains obsolete: {obsolete}")
    for relative_path in (
        "test/services/UploadStateMachine_test.cpp",
        "test/integration/test_upload_state_machine.py",
        "test/services/StorageJobRepository_test.cpp",
        "test/integration/test_storage_job_queue.py",
        "test/storage/S3ObjectStorage_test.cpp",
        "test/integration/test_s3_app_flow.py",
        "test/services/TokenServiceRevocation_test.cpp",
        "test/integration/test_distributed_flow.py",
    ):
        require((root / relative_path).is_file(), f"documented test entry is missing: {relative_path}")

    todo = (root / "TODO.md").read_text(encoding="utf-8")
    latest_verification = re.search(
        r"^> 最近验证（[^\n]*?）：[^\n]*完整 CTest 共 (\d+) 项，(\d+) 通过、(\d+) 项按环境门控跳过[^\n]*0 失败[^\n]*$",
        todo,
        re.MULTILINE,
    )
    require(latest_verification is not None, "TODO latest verification summary is missing or malformed")
    total, passed_count, skipped_count = map(int, latest_verification.groups())
    require(
        (total, passed_count, skipped_count) == (1381, 1375, 6),
        "TODO latest CTest inventory drifted without an explicit contract update",
    )
    require(total == passed_count + skipped_count, "TODO latest CTest totals do not reconcile")
    require(
        "环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行" in latest_verification.group(0),
        "TODO latest verification must retain the target-environment caveat",
    )

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
    for nginx_path in (
        root / "deploy/nginx/disk.conf",
        root / "deploy/nginx/includes/upstream.inc",
        root / "deploy/nginx/includes/proxy-server.inc",
    ):
        for raw_line in nginx_path.read_text(encoding="utf-8").splitlines():
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

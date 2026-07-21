#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///

"""Validate the reversible dual-API load-balancer admission contract."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
from pathlib import Path
from typing import Any

import yaml


REPO_ROOT = Path(__file__).resolve().parents[2]
COMPOSE_PATH = REPO_ROOT / "docker-compose.distributed.yml"
FALLBACK_COMPOSE_PATH = REPO_ROOT / "deploy/docker-compose.api-a-only.yml"
UPLOAD_FREEZE_COMPOSE_PATH = REPO_ROOT / "deploy/docker-compose.upload-frozen.yml"
NGINX_PATH = REPO_ROOT / "deploy/nginx/disk.conf"
NGINX_TLS_PATH = REPO_ROOT / "deploy/nginx/disk-tls.conf.example"
NGINX_UPSTREAM_INCLUDE_PATH = REPO_ROOT / "deploy/nginx/includes/upstream.inc"
NGINX_PROXY_INCLUDE_PATH = REPO_ROOT / "deploy/nginx/includes/proxy-server.inc"
DUAL_UPSTREAM_PATH = REPO_ROOT / "deploy/nginx/upstreams/api-a-b.inc"
FALLBACK_UPSTREAM_PATH = REPO_ROOT / "deploy/nginx/upstreams/api-a-only.inc"
PRODUCTION_UPSTREAM_PATH = REPO_ROOT / "deploy/nginx/upstreams/production.example.inc"
OPEN_UPLOAD_PATH = REPO_ROOT / "deploy/nginx/upload-mode/open.inc"
FROZEN_UPLOAD_PATH = REPO_ROOT / "deploy/nginx/upload-mode/frozen.inc"
SWITCH_SCRIPT = REPO_ROOT / "scripts/switch-api-pool.sh"
UPLOAD_SWITCH_SCRIPT = REPO_ROOT / "scripts/switch-upload-ingress.sh"
EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/api-pool-rollout-summary.json"
NGINX_TARGET = "/etc/nginx/conf.d/default.conf"
UPSTREAM_TARGET = "/etc/nginx/conf.d/disk-api-upstreams.inc"
UPLOAD_MODE_TARGET = "/etc/nginx/conf.d/disk-upload-mode.inc"
INCLUDES_TARGET = "/etc/nginx/disk"
UPSTREAM_INCLUDE_TARGET = f"{INCLUDES_TARGET}/upstream.inc"
PROXY_INCLUDE_TARGET = f"{INCLUDES_TARGET}/proxy-server.inc"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_yaml(path: Path) -> dict[str, Any]:
    value = yaml.safe_load(path.read_text(encoding="utf-8"))
    require(isinstance(value, dict), f"{path} must contain a YAML mapping")
    return value


def active_lines(path: Path) -> list[str]:
    lines: list[str] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if line:
            lines.append(line)
    return lines


def short_mounts(volumes: object, label: str) -> dict[str, str]:
    require(isinstance(volumes, list), f"{label} volumes must be a list")
    mounts: dict[str, str] = {}
    for volume in volumes:
        require(isinstance(volume, str), f"{label} must use reviewed short bind mounts")
        parts = volume.split(":")
        require(len(parts) == 3 and parts[2] == "ro", f"{label} mount must be read-only: {volume}")
        source, target, _ = parts
        require(target not in mounts, f"{label} repeats mount target {target}")
        mounts[target] = source
    return mounts


def run_switch(
    mode: str,
    env_file: Path,
    fake_bin: Path,
    log_path: Path,
    *,
    fail_config: bool = False,
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment.update(
        {
            "PATH": f"{fake_bin}:{environment.get('PATH', '')}",
            "DISK_DISTRIBUTED_ENV_FILE": str(env_file),
            "FAKE_DOCKER_LOG": str(log_path),
            "FAKE_DOCKER_FAIL_CONFIG": "1" if fail_config else "0",
        }
    )
    return subprocess.run(
        ["bash", str(SWITCH_SCRIPT), mode],
        cwd=REPO_ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )


def run_upload_switch(
    mode: str,
    env_file: Path,
    fake_bin: Path,
    log_path: Path,
    *,
    approved: bool = False,
    fail_config: bool = False,
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment.update(
        {
            "PATH": f"{fake_bin}:{environment.get('PATH', '')}",
            "DISK_DISTRIBUTED_ENV_FILE": str(env_file),
            "DISK_UPLOAD_UNFREEZE_APPROVED": "true" if approved else "false",
            "FAKE_DOCKER_LOG": str(log_path),
            "FAKE_DOCKER_FAIL_CONFIG": "1" if fail_config else "0",
        }
    )
    return subprocess.run(
        ["bash", str(UPLOAD_SWITCH_SCRIPT), mode],
        cwd=REPO_ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )


def docker_calls(log_path: Path) -> list[str]:
    if not log_path.exists():
        return []
    return [line for line in log_path.read_text(encoding="utf-8").splitlines() if line]


def require_switch_pair(calls: list[str], *, fallback: bool) -> None:
    require(len(calls) == 2, f"pool switch must make exactly two Docker calls: {calls}")
    config_call, apply_call = calls
    base_argument = f"-f {COMPOSE_PATH}"
    fallback_argument = f"-f {FALLBACK_COMPOSE_PATH}"
    for call in calls:
        require(call.startswith("compose --env-file "), f"unexpected Docker call: {call}")
        require(base_argument in call, "pool switch omitted the base Compose file")
        require(
            (fallback_argument in call) is fallback,
            "pool switch selected the wrong fallback overlay",
        )
    require(config_call.endswith("config --quiet"), "Compose validation must run first")
    require(
        apply_call.endswith("up -d --no-deps --force-recreate load-balancer"),
        "only the load balancer may be recreated after validation",
    )


def require_upload_switch_pair(calls: list[str], *, frozen: bool) -> None:
    require(len(calls) == 2, f"upload switch must make exactly two Docker calls: {calls}")
    config_call, apply_call = calls
    base_argument = f"-f {COMPOSE_PATH}"
    freeze_argument = f"-f {UPLOAD_FREEZE_COMPOSE_PATH}"
    for call in calls:
        require(call.startswith("compose --env-file "), f"unexpected Docker call: {call}")
        require(base_argument in call, "upload switch omitted the base Compose file")
        require(
            (freeze_argument in call) is frozen,
            "upload switch selected the wrong ingress overlay",
        )
    require(config_call.endswith("config --quiet"), "upload Compose validation must run first")
    require(
        apply_call.endswith("up -d --no-deps --force-recreate load-balancer"),
        "upload switch may only recreate the load balancer",
    )


def main() -> int:
    EVIDENCE_PATH.unlink(missing_ok=True)

    compose = load_yaml(COMPOSE_PATH)
    services = compose.get("services")
    require(isinstance(services, dict), "base Compose services are missing")
    api_names = [name for name in services if name.startswith("api-")]
    require(api_names == ["api-a", "api-b"], "base Compose must define exactly api-a and api-b")

    instance_ids: list[str] = []
    for api_name in api_names:
        service = services[api_name]
        require(isinstance(service, dict), f"{api_name} service must be a mapping")
        environment = service.get("environment")
        require(isinstance(environment, dict), f"{api_name} environment is missing")
        require(environment.get("DISK_PROCESS_ROLE") == "api", f"{api_name} is not an API role")
        instance_ids.append(str(environment.get("DISK_INSTANCE_ID", "")))
        healthcheck = service.get("healthcheck")
        require(isinstance(healthcheck, dict), f"{api_name} healthcheck is missing")
        health_command = " ".join(str(item) for item in healthcheck.get("test", []))
        require("/api/health/ready" in health_command, f"{api_name} does not check readiness")
    require(instance_ids == ["disk-api-a", "disk-api-b"], "API instance IDs drifted")

    nginx = active_lines(NGINX_PATH)
    require(
        nginx
        == [
            f"include {UPSTREAM_INCLUDE_TARGET};",
            "server {",
            "listen 8080;",
            "server_name _;",
            f"include {PROXY_INCLUDE_TARGET};",
            "}",
        ],
        "container Nginx entry point must only select its listener and shared policy",
    )

    upstream_include = active_lines(NGINX_UPSTREAM_INCLUDE_PATH)
    require(
        upstream_include
        == [
            "upstream disk_api {",
            "zone disk_api 64k;",
            "least_conn;",
            f"include {UPSTREAM_TARGET};",
            "keepalive 32;",
            "}",
        ],
        "shared upstream policy drifted",
    )
    require(
        not any(line.startswith("server api-") for line in upstream_include),
        "API members must not remain hard-coded in the shared upstream policy",
    )

    proxy_include = active_lines(NGINX_PROXY_INCLUDE_PATH)
    required_proxy_lines = {
        "client_max_body_size 20m;",
        "client_body_timeout 60s;",
        "location = /metrics {",
        "return 404;",
        f"include {UPLOAD_MODE_TARGET};",
        "proxy_pass http://disk_api;",
        'proxy_set_header Connection "";',
        "proxy_set_header Host $host;",
        "proxy_set_header X-Real-IP $remote_addr;",
        "proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;",
        "proxy_set_header X-Forwarded-Proto $scheme;",
        "proxy_set_header X-Request-Id $request_id;",
        "proxy_request_buffering off;",
        "proxy_buffering off;",
        "proxy_connect_timeout 3s;",
        "proxy_send_timeout 330s;",
        "proxy_read_timeout 330s;",
        "proxy_next_upstream error timeout http_502 http_503 http_504;",
        "proxy_next_upstream_tries 2;",
    }
    require(
        required_proxy_lines <= set(proxy_include),
        "shared proxy-server policy drifted",
    )
    require(
        not any("non_idempotent" in line for line in proxy_include),
        "shared proxy policy must not replay non-idempotent requests",
    )
    require(
        not any("/api/file/download" in line for line in proxy_include),
        "downloads must not bypass the shared forwarding policy",
    )

    tls_nginx = active_lines(NGINX_TLS_PATH)
    for line in (
        f"include {UPSTREAM_INCLUDE_TARGET};",
        "listen 80;",
        "listen [::]:80;",
        "return 308 https://$server_name$request_uri;",
        "listen 443 ssl http2;",
        "listen [::]:443 ssl http2;",
        "ssl_certificate /etc/letsencrypt/live/disk.example.com/fullchain.pem;",
        "ssl_certificate_key /etc/letsencrypt/live/disk.example.com/privkey.pem;",
        "ssl_protocols TLSv1.2 TLSv1.3;",
        "ssl_session_tickets off;",
        f"include {PROXY_INCLUDE_TARGET};",
    ):
        require(line in tls_nginx, f"production TLS entry point is missing: {line}")
    require(
        not any(line.startswith("proxy_") for line in tls_nginx),
        "production TLS entry point must not duplicate shared proxy directives",
    )
    require(
        not any(line.startswith("location ") for line in tls_nginx),
        "production TLS entry point must not duplicate shared locations",
    )

    expected_a = "server api-a:8080 max_fails=2 fail_timeout=5s;"
    expected_b = "server api-b:8080 max_fails=2 fail_timeout=5s;"
    require(
        active_lines(DUAL_UPSTREAM_PATH) == [expected_a, expected_b],
        "reviewed dual API pool drifted",
    )
    require(
        active_lines(FALLBACK_UPSTREAM_PATH) == [expected_a],
        "reviewed api-a-only pool drifted",
    )
    require(
        active_lines(PRODUCTION_UPSTREAM_PATH)
        == [
            "server 10.0.1.11:8080 max_fails=2 fail_timeout=5s;",
            "server 10.0.1.12:8080 max_fails=2 fail_timeout=5s;",
        ],
        "production private API pool example drifted",
    )

    load_balancer = services.get("load-balancer")
    require(isinstance(load_balancer, dict), "base load-balancer service is missing")
    base_mounts = short_mounts(load_balancer.get("volumes"), "base load balancer")
    require(
        base_mounts.get(NGINX_TARGET) == "./deploy/nginx/disk.conf",
        "base Compose must mount the reviewed Nginx entry point",
    )
    require(
        base_mounts.get(UPSTREAM_TARGET) == "./deploy/nginx/upstreams/api-a-b.inc",
        "base Compose must mount the dual API pool",
    )
    require(
        base_mounts.get(UPLOAD_MODE_TARGET) == "./deploy/nginx/upload-mode/open.inc",
        "base Compose must mount the reviewed open upload mode",
    )
    require(
        base_mounts.get(INCLUDES_TARGET) == "./deploy/nginx/includes",
        "base Compose must mount the shared Nginx policy directory",
    )
    require(active_lines(OPEN_UPLOAD_PATH) == [], "open upload mode must not add a location")

    frozen_lines = active_lines(FROZEN_UPLOAD_PATH)
    frozen_return = (
        "return 503 "
        "'{\"code\":50013,\"message\":\"Upload lifecycle is temporarily "
        "frozen for rollback\",\"data\":null}';"
    )
    require(
        "location = /api/file/upload {" in frozen_lines
        and "location ^~ /api/file/upload/ {" in frozen_lines,
        "frozen upload mode must cover the root and every lifecycle subpath",
    )
    require(
        frozen_lines.count(frozen_return) == 2,
        "frozen upload mode must return the stable 503/50013 envelope",
    )
    require(
        frozen_lines.count("add_header Retry-After 30 always;") == 2
        and frozen_lines.count('add_header Cache-Control "no-store" always;') == 2,
        "frozen upload mode must provide retry and no-cache guidance",
    )

    freeze_compose = load_yaml(UPLOAD_FREEZE_COMPOSE_PATH)
    require(set(freeze_compose) == {"services"}, "freeze overlay may only override services")
    freeze_services = freeze_compose["services"]
    require(
        isinstance(freeze_services, dict) and set(freeze_services) == {"load-balancer"},
        "freeze overlay may only override the load balancer",
    )
    freeze_service = freeze_services["load-balancer"]
    require(isinstance(freeze_service, dict), "freeze load balancer must be a mapping")
    require(set(freeze_service) == {"volumes"}, "freeze overlay may only replace a volume")
    require(
        short_mounts(freeze_service["volumes"], "freeze load balancer")
        == {UPLOAD_MODE_TARGET: "./deploy/nginx/upload-mode/frozen.inc"},
        "freeze overlay must replace only the upload-mode target",
    )

    fallback_compose = load_yaml(FALLBACK_COMPOSE_PATH)
    require(
        set(fallback_compose) == {"services"},
        "fallback Compose may only override services",
    )
    fallback_services = fallback_compose["services"]
    require(
        isinstance(fallback_services, dict) and set(fallback_services) == {"load-balancer"},
        "fallback Compose may only override the load balancer",
    )
    fallback_service = fallback_services["load-balancer"]
    require(isinstance(fallback_service, dict), "fallback load balancer must be a mapping")
    require(set(fallback_service) == {"volumes"}, "fallback may only replace a volume")
    fallback_mounts = short_mounts(fallback_service["volumes"], "fallback load balancer")
    require(
        fallback_mounts == {UPSTREAM_TARGET: "./deploy/nginx/upstreams/api-a-only.inc"},
        "fallback Compose must replace the upstream target with api-a-only",
    )

    with tempfile.TemporaryDirectory(prefix="disk-api-pool-") as temporary:
        temp_root = Path(temporary)
        fake_bin = temp_root / "bin"
        fake_bin.mkdir()
        fake_docker = fake_bin / "docker"
        fake_docker.write_text(
            "#!/bin/sh\n"
            "printf '%s\\n' \"$*\" >> \"$FAKE_DOCKER_LOG\"\n"
            "if [ \"$FAKE_DOCKER_FAIL_CONFIG\" = 1 ]; then\n"
            "    for argument in \"$@\"; do\n"
            "        [ \"$argument\" = config ] && exit 23\n"
            "    done\n"
            "fi\n",
            encoding="utf-8",
        )
        fake_docker.chmod(0o755)
        env_file = temp_root / ".env.distributed"
        env_file.write_text("# fake Docker does not evaluate Compose\n", encoding="utf-8")

        dual_log = temp_root / "dual.log"
        dual_result = run_switch("dual", env_file, fake_bin, dual_log)
        require(dual_result.returncode == 0, f"dual switch failed: {dual_result.stderr}")
        require_switch_pair(docker_calls(dual_log), fallback=False)

        fallback_log = temp_root / "fallback.log"
        fallback_result = run_switch("api-a-only", env_file, fake_bin, fallback_log)
        require(
            fallback_result.returncode == 0,
            f"api-a-only switch failed: {fallback_result.stderr}",
        )
        require_switch_pair(docker_calls(fallback_log), fallback=True)

        validation_log = temp_root / "validation-failure.log"
        validation_result = run_switch(
            "dual",
            env_file,
            fake_bin,
            validation_log,
            fail_config=True,
        )
        require(validation_result.returncode == 23, "validation failure status was not preserved")
        validation_calls = docker_calls(validation_log)
        require(
            len(validation_calls) == 1 and validation_calls[0].endswith("config --quiet"),
            "invalid configuration must not reach the apply command",
        )

        invalid_log = temp_root / "invalid-mode.log"
        invalid_result = run_switch("unexpected", temp_root / "missing.env", fake_bin, invalid_log)
        require(invalid_result.returncode == 64, "invalid pool mode must be rejected")
        require(not docker_calls(invalid_log), "invalid pool mode must not invoke Docker")

        freeze_log = temp_root / "upload-freeze.log"
        freeze_result = run_upload_switch("freeze", env_file, fake_bin, freeze_log)
        require(freeze_result.returncode == 0, f"upload freeze failed: {freeze_result.stderr}")
        require_upload_switch_pair(docker_calls(freeze_log), frozen=True)

        unapproved_log = temp_root / "upload-open-unapproved.log"
        unapproved = run_upload_switch("open", env_file, fake_bin, unapproved_log)
        require(unapproved.returncode == 77, "unapproved upload reopening was accepted")
        require(not docker_calls(unapproved_log), "unapproved reopening invoked Docker")

        open_log = temp_root / "upload-open.log"
        opened = run_upload_switch("open", env_file, fake_bin, open_log, approved=True)
        require(opened.returncode == 0, f"approved upload opening failed: {opened.stderr}")
        require_upload_switch_pair(docker_calls(open_log), frozen=False)

        freeze_validation_log = temp_root / "upload-freeze-validation-failure.log"
        freeze_validation = run_upload_switch(
            "freeze",
            env_file,
            fake_bin,
            freeze_validation_log,
            fail_config=True,
        )
        require(
            freeze_validation.returncode == 23,
            "upload freeze validation failure status was not preserved",
        )
        require(
            len(docker_calls(freeze_validation_log)) == 1,
            "invalid upload configuration reached the apply command",
        )

    evidence = {
        "schema_version": 1,
        "scenario": "reversible_dual_api_admission",
        "repository_contract_passed": True,
        "api_services": api_names,
        "instance_ids": instance_ids,
        "reviewed_pools": {
            "api-a-only": ["api-a:8080"],
            "dual": ["api-a:8080", "api-b:8080"],
        },
        "switch_contract": {
            "apply_target": "load-balancer",
            "config_failure_blocks_apply": True,
            "validation_precedes_apply": True,
        },
        "upload_ingress_freeze": {
            "error_code": 50013,
            "open_requires_explicit_approval": True,
            "paths": ["/api/file/upload", "/api/file/upload/**"],
            "switch_apply_target": "load-balancer",
        },
        "target_environment_validation_required": [
            "direct api-b readiness and instance header",
            "dual-pool admission through the real load balancer",
            "api-a-only probes before api-b termination",
        ],
    }
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE_PATH.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("PASS: reversible dual-API admission contract is valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

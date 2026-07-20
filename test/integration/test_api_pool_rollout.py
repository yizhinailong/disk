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
NGINX_PATH = REPO_ROOT / "deploy/nginx/disk.conf"
DUAL_UPSTREAM_PATH = REPO_ROOT / "deploy/nginx/upstreams/api-a-b.inc"
FALLBACK_UPSTREAM_PATH = REPO_ROOT / "deploy/nginx/upstreams/api-a-only.inc"
SWITCH_SCRIPT = REPO_ROOT / "scripts/switch-api-pool.sh"
EVIDENCE_PATH = REPO_ROOT / ".sisyphus/evidence/api-pool-rollout-summary.json"
UPSTREAM_TARGET = "/etc/nginx/conf.d/disk-api-upstreams.inc"


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
        f"include {UPSTREAM_TARGET};" in nginx,
        "Nginx must load the selected upstream fragment",
    )
    require(
        not any(line.startswith("server api-") for line in nginx),
        "API members must not remain hard-coded in the main Nginx config",
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

    load_balancer = services.get("load-balancer")
    require(isinstance(load_balancer, dict), "base load-balancer service is missing")
    base_mounts = short_mounts(load_balancer.get("volumes"), "base load balancer")
    require(
        base_mounts.get(UPSTREAM_TARGET) == "./deploy/nginx/upstreams/api-a-b.inc",
        "base Compose must mount the dual API pool",
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

#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///

"""Validate that the local S3 lifecycle baseline cannot expire final blobs."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import yaml


REPO_ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = REPO_ROOT / "config.json"
LIFECYCLE_PATH = REPO_ROOT / "deploy" / "minio" / "lifecycle.json"
COMPOSE_PATH = REPO_ROOT / "docker-compose.s3.yml"


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    assert isinstance(value, dict)
    return value


def main() -> int:
    app_config = load_json(CONFIG_PATH)["custom_config"]["disk"]
    s3_config = app_config["s3"]
    staging_prefix = f"{s3_config['staging_prefix'].rstrip('/')}/"
    final_prefix = f"{s3_config['object_prefix'].rstrip('/')}/"
    minimum_retention_days = (int(app_config["upload_task_expiry_seconds"]) // 86400) + 1

    lifecycle = load_json(LIFECYCLE_PATH)
    rules = lifecycle.get("Rules")
    assert isinstance(rules, list) and rules

    staging_expiration = [
        rule
        for rule in rules
        if rule.get("Status") == "Enabled"
        and rule.get("Filter", {}).get("Prefix") == staging_prefix
        and "Expiration" in rule
    ]
    assert len(staging_expiration) == 1
    assert int(staging_expiration[0]["Expiration"]["Days"]) >= minimum_retention_days

    abort_rules = [
        rule
        for rule in rules
        if rule.get("Status") == "Enabled" and "AbortIncompleteMultipartUpload" in rule
    ]
    assert len(abort_rules) == 1
    assert abort_rules[0].get("Filter", {}).get("Prefix") == ""
    assert (
        int(abort_rules[0]["AbortIncompleteMultipartUpload"]["DaysAfterInitiation"])
        >= minimum_retention_days
    )

    expiration_prefixes = {
        rule.get("Filter", {}).get("Prefix", "")
        for rule in rules
        if rule.get("Status") == "Enabled" and "Expiration" in rule
    }
    assert "" not in expiration_prefixes
    assert not any(
        final_prefix.startswith(prefix) or prefix.startswith(final_prefix)
        for prefix in expiration_prefixes
    )

    with COMPOSE_PATH.open(encoding="utf-8") as stream:
        compose = yaml.safe_load(stream)
    init_service = compose["services"]["minio-init"]
    assert "./deploy/minio/lifecycle.json:/config/lifecycle.json:ro" in init_service["volumes"]
    entrypoint = init_service["entrypoint"]
    assert "mc ilm rule import local/disk /config/lifecycle.json" in entrypoint
    assert "mc ilm rule export local/disk" in entrypoint

    print("S3 lifecycle configuration: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

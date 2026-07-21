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
AWS_LIFECYCLE_PATH = REPO_ROOT / "deploy" / "s3" / "lifecycle.json"
POLICY_PATH = REPO_ROOT / "deploy" / "minio" / "app-policy.json"
MIGRATION_POLICY_PATH = REPO_ROOT / "deploy" / "minio" / "migration-policy.json"
SELF_HOSTED_ASSESSMENT_PATH = (
    REPO_ROOT / "deploy" / "minio" / "self-hosted-assessment.json"
)
PROVISION_PATH = REPO_ROOT / "deploy" / "minio" / "provision.sh"
REVOKE_PATH = REPO_ROOT / "deploy" / "minio" / "revoke-migration-access.sh"
S3_COMPOSE_PATH = REPO_ROOT / "docker-compose.s3.yml"
DISTRIBUTED_COMPOSE_PATH = REPO_ROOT / "docker-compose.distributed.yml"
ALERTS_PATH = REPO_ROOT / "deploy" / "prometheus" / "disk-alerts.yml"
ENV_EXAMPLE_PATH = REPO_ROOT / "deploy" / "distributed.env.example"


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
    minimum_retention_days = (
        int(app_config["upload_task_expiry_seconds"]) // 86400
    ) + 1

    assessment = load_json(SELF_HOSTED_ASSESSMENT_PATH)
    evaluated_release = "RELEASE.2025-04-22T22-12-26Z"
    assert assessment["schema_version"] == 1
    assert assessment["reviewed_on"] == "2026-07-22"
    assert assessment["evaluated_release"] == evaluated_release
    assert assessment["decision"] == {
        "initial_production_model": "managed_s3",
        "self_hosted_status": "not_approved",
        "repository_fixture_class": "development_only",
        "repository_fixture_is_highly_available": False,
    }

    production_floor = assessment["upstream_production_floor"]
    assert production_floor == {
        "servers": 4,
        "drives_per_server": 2,
        "erasure_set_size": 8,
        "default_parity": 4,
        "read_node_failure_tolerance": 2,
        "write_node_failure_tolerance": 1,
        "accepted_for_disk": False,
        "rejection_reason": "write_tolerance_below_failure_domain_width",
    }

    candidate = assessment["disk_candidate"]
    assert candidate == {
        "servers": 6,
        "drives_per_server": 2,
        "erasure_set_size": 12,
        "default_parity": 4,
        "physical_failure_domains": 3,
        "servers_per_failure_domain": 2,
        "read_node_failure_tolerance": 2,
        "write_node_failure_tolerance": 2,
        "required_failure_drill": "lose_one_complete_failure_domain",
        "dedicated_hosts": True,
        "homogeneous_nodes_and_drives": True,
        "local_data_drives": True,
        "filesystem": "xfs",
        "network_filesystems_allowed": False,
    }
    assert candidate["servers"] == (
        candidate["physical_failure_domains"]
        * candidate["servers_per_failure_domain"]
    )
    assert candidate["erasure_set_size"] == (
        candidate["servers"] * candidate["drives_per_server"]
    )
    assert (
        production_floor["write_node_failure_tolerance"]
        < candidate["servers_per_failure_domain"]
        <= candidate["write_node_failure_tolerance"]
    )

    backup_requirements = assessment["backup_requirements"]
    assert backup_requirements == {
        "independent_site_or_provider": True,
        "versioned_source": True,
        "versioned_destination": True,
        "delete_marker_replication": False,
        "permanent_delete_replication": False,
        "encrypted_in_transit": True,
        "encrypted_at_rest": True,
        "object_version_manifest": True,
        "periodic_hash_and_readability_check": True,
        "isolated_restore_drill": True,
        "target_rpo_seconds": None,
        "target_rto_seconds": None,
    }
    assert assessment["security_requirements"] == {
        "server_and_internode_tls": True,
        "client_certificate_verification": True,
        "kms_managed_server_side_encryption": True,
        "least_privilege_workload_identities": True,
        "private_service_endpoint": True,
    }

    approval = assessment["approval"]
    assert approval["status"] == "not_approved"
    assert set(approval["required_evidence"]) == {
        "target_topology_inventory",
        "capacity_and_healing_headroom",
        "failure_domain_loss_drill",
        "independent_backup_inventory",
        "isolated_restore_drill",
        "approved_rpo_rto",
        "bucket_security_and_lifecycle_validation",
        "tls_kms_network_validation",
        "native_monitoring_and_alerts",
    }
    assert approval["satisfied_evidence"] == []
    assert backup_requirements["target_rpo_seconds"] is None
    assert backup_requirements["target_rto_seconds"] is None

    expected_references = {
        f"https://github.com/minio/minio/blob/{evaluated_release}/docs/distributed/SIZING.md",
        f"https://github.com/minio/minio/blob/{evaluated_release}/docs/distributed/README.md",
        f"https://github.com/minio/minio/blob/{evaluated_release}/docs/erasure/README.md",
        f"https://github.com/minio/minio/blob/{evaluated_release}/docs/bucket/replication/README.md",
        f"https://github.com/minio/minio/blob/{evaluated_release}/docs/tls/README.md",
    }
    assert set(assessment["references"]) == expected_references

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

    assert not any("AbortIncompleteMultipartUpload" in rule for rule in rules)

    aws_lifecycle = load_json(AWS_LIFECYCLE_PATH)
    aws_rules = aws_lifecycle.get("Rules")
    assert isinstance(aws_rules, list) and aws_rules
    abort_rules = [
        rule
        for rule in aws_rules
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

    policy = load_json(POLICY_PATH)
    assert policy.get("Version") == "2012-10-17"
    statements = policy.get("Statement")
    assert isinstance(statements, list)
    statements_by_id = {statement["Sid"]: statement for statement in statements}
    assert set(statements_by_id) == {
        "BucketMetadata",
        "ListApplicationPrefixes",
        "ApplicationObjects",
        "DenyObjectVersionPurge",
    }
    assert all(
        statements_by_id[sid].get("Effect") == "Allow"
        for sid in (
            "BucketMetadata",
            "ListApplicationPrefixes",
            "ApplicationObjects",
        )
    )
    assert statements_by_id["BucketMetadata"]["Action"] == ["s3:GetBucketLocation"]
    assert statements_by_id["BucketMetadata"]["Resource"] == ["arn:aws:s3:::disk"]
    assert statements_by_id["ListApplicationPrefixes"]["Action"] == ["s3:ListBucket"]
    assert statements_by_id["ListApplicationPrefixes"]["Resource"] == [
        "arn:aws:s3:::disk"
    ]
    assert statements_by_id["ListApplicationPrefixes"]["Condition"] == {
        "StringLike": {"s3:prefix": ["objects", "objects/*", "staging", "staging/*"]}
    }
    assert set(statements_by_id["ApplicationObjects"]["Action"]) == {
        "s3:GetObject",
        "s3:PutObject",
        "s3:DeleteObject",
        "s3:AbortMultipartUpload",
        "s3:ListMultipartUploadParts",
    }
    assert set(statements_by_id["ApplicationObjects"]["Resource"]) == {
        "arn:aws:s3:::disk/objects/*",
        "arn:aws:s3:::disk/staging/*",
    }
    assert statements_by_id["DenyObjectVersionPurge"] == {
        "Sid": "DenyObjectVersionPurge",
        "Effect": "Deny",
        "Action": ["s3:DeleteObjectVersion"],
        "Resource": [
            "arn:aws:s3:::disk/objects/*",
            "arn:aws:s3:::disk/staging/*",
        ],
    }

    migration_policy = load_json(MIGRATION_POLICY_PATH)
    assert migration_policy.get("Version") == "2012-10-17"
    migration_statements = migration_policy.get("Statement")
    assert isinstance(migration_statements, list)
    migration_statements_by_id = {
        statement["Sid"]: statement for statement in migration_statements
    }
    assert migration_statements_by_id == {
        "MigrationFinalObjects": {
            "Sid": "MigrationFinalObjects",
            "Effect": "Allow",
            "Action": [
                "s3:GetObject",
                "s3:PutObject",
                "s3:AbortMultipartUpload",
                "s3:ListMultipartUploadParts",
            ],
            "Resource": ["arn:aws:s3:::disk/objects/*"],
        },
        "DenyMigrationObjectDeletion": {
            "Sid": "DenyMigrationObjectDeletion",
            "Effect": "Deny",
            "Action": ["s3:DeleteObject", "s3:DeleteObjectVersion"],
            "Resource": ["arn:aws:s3:::disk/objects/*"],
        },
    }
    assert all(
        action != "s3:*" and not action.startswith("iam:")
        for statement in [*statements, *migration_statements]
        for action in statement["Action"]
    )
    assert all(
        resource not in {"*", "arn:aws:s3:::*"}
        for statement in [*statements, *migration_statements]
        for resource in statement["Resource"]
    )
    assert all(
        "staging" not in resource
        for statement in migration_statements
        for resource in statement["Resource"]
    )

    expected_volumes = {
        "./deploy/minio/lifecycle.json:/config/lifecycle.json:ro",
        "./deploy/minio/app-policy.json:/config/app-policy.json:ro",
        "./deploy/minio/migration-policy.json:/config/migration-policy.json:ro",
        "./deploy/minio/provision.sh:/config/provision.sh:ro",
    }
    for compose_path in (S3_COMPOSE_PATH, DISTRIBUTED_COMPOSE_PATH):
        with compose_path.open(encoding="utf-8") as stream:
            compose = yaml.safe_load(stream)
        init_service = compose["services"]["minio-init"]
        assert set(init_service["volumes"]) == expected_volumes
        assert init_service["entrypoint"] == ["/bin/sh", "/config/provision.sh"]
        init_environment = init_service["environment"]
        assert set(init_environment) == {
            "MINIO_ROOT_USER",
            "MINIO_ROOT_PASSWORD",
            "DISK_S3_BUCKET",
            "DISK_S3_ACCESS_KEY",
            "DISK_S3_SECRET_KEY",
            "DISK_S3_MIGRATION_ACCESS_KEY",
            "DISK_S3_MIGRATION_SECRET_KEY",
        }
        assert init_environment["DISK_S3_BUCKET"] == "disk"
        assert len(
            {
                init_environment["MINIO_ROOT_USER"],
                init_environment["DISK_S3_ACCESS_KEY"],
                init_environment["DISK_S3_MIGRATION_ACCESS_KEY"],
            }
        ) == 3
        assert len(
            {
                init_environment["MINIO_ROOT_PASSWORD"],
                init_environment["DISK_S3_SECRET_KEY"],
                init_environment["DISK_S3_MIGRATION_SECRET_KEY"],
            }
        ) == 3
        minio_environment = compose["services"]["minio"]["environment"]
        assert minio_environment["MINIO_API_STALE_UPLOADS_EXPIRY"] == "168h"
        assert minio_environment["MINIO_API_STALE_UPLOADS_CLEANUP_INTERVAL"] == "6h"
        assert compose["services"]["minio"]["command"] == (
            'server /data --console-address ":9001"'
        )
        assert compose["services"]["minio"]["volumes"] == ["minio-data:/data"]

    with DISTRIBUTED_COMPOSE_PATH.open(encoding="utf-8") as stream:
        distributed_compose = yaml.safe_load(stream)
    assert (
        distributed_compose["services"]["minio"]["image"]
        == f"minio/minio:{evaluated_release}"
    )
    for service_name in ("api-a", "api-b", "worker-a", "worker-b"):
        environment = distributed_compose["services"][service_name]["environment"]
        assert (
            environment["DISK_S3_ACCESS_KEY"]
            == "${DISK_S3_ACCESS_KEY:?set DISK_S3_ACCESS_KEY}"
        )
        assert (
            environment["DISK_S3_SECRET_KEY"]
            == "${DISK_S3_SECRET_KEY:?set DISK_S3_SECRET_KEY}"
        )
        assert "DISK_MINIO_ROOT_USER" not in environment
        assert "DISK_MINIO_ROOT_PASSWORD" not in environment
        assert "DISK_S3_MIGRATION_ACCESS_KEY" not in environment
        assert "DISK_S3_MIGRATION_SECRET_KEY" not in environment
        assert "DISK_S3_MIGRATION_SESSION_TOKEN" not in environment

    provision = PROVISION_PATH.read_text(encoding="utf-8")
    for command in (
        "run_mc mb --ignore-existing",
        "run_mc version enable",
        "run_mc --json version info",
        "run_mc ilm rule import",
        "run_mc admin user add",
        "run_mc admin policy create",
        "run_mc admin policy attach",
        "run_mc ilm rule export",
    ):
        assert command in provision
    assert '\"versioning\":{\"status\":\"Enabled\"' in provision
    assert '"${MINIO_ROOT_USER}" = "${DISK_S3_ACCESS_KEY}"' in provision
    assert '"${MINIO_ROOT_PASSWORD}" = "${DISK_S3_SECRET_KEY}"' in provision
    assert '"${MINIO_ROOT_USER}" = "${DISK_S3_MIGRATION_ACCESS_KEY}"' in provision
    assert '"${DISK_S3_ACCESS_KEY}" = "${DISK_S3_MIGRATION_ACCESS_KEY}"' in provision
    assert '"${MINIO_ROOT_PASSWORD}" = "${DISK_S3_MIGRATION_SECRET_KEY}"' in provision
    assert '"${DISK_S3_SECRET_KEY}" = "${DISK_S3_MIGRATION_SECRET_KEY}"' in provision
    assert "run_mc admin policy create local disk-migration" in provision
    assert "run_mc admin policy attach local disk-migration" in provision

    revoke = REVOKE_PATH.read_text(encoding="utf-8")
    assert REVOKE_PATH.stat().st_mode & 0o111
    assert "run_mc admin user remove" in revoke
    assert "DISK_S3_MIGRATION_ACCESS_KEY" in revoke
    assert "DISK_S3_ACCESS_KEY+x" in revoke
    assert "DISK_S3_SECRET_KEY+x" in revoke

    environment_example = ENV_EXAMPLE_PATH.read_text(encoding="utf-8")
    for name in (
        "DISK_MINIO_ROOT_USER",
        "DISK_MINIO_ROOT_PASSWORD",
        "DISK_S3_ACCESS_KEY",
        "DISK_S3_SECRET_KEY",
        "DISK_S3_MIGRATION_ACCESS_KEY",
        "DISK_S3_MIGRATION_SECRET_KEY",
    ):
        assert f"{name}=" in environment_example

    with ALERTS_PATH.open(encoding="utf-8") as stream:
        alert_groups = yaml.safe_load(stream)["groups"]
    alerts = {
        rule["alert"]: str(rule["expr"])
        for group in alert_groups
        for rule in group["rules"]
    }
    assert 'dependency="s3"' in alerts["DiskS3TransientErrors"]
    assert 'dependency="s3"' in alerts["DiskS3PermanentErrors"]
    assert (
        "disk_reconciliation_findings_unresolved"
        in alerts["DiskReconciliationFindings"]
    )

    print(
        "S3 lifecycle, identities, MinIO HA assessment, and monitoring: PASS"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

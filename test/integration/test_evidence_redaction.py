#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///

"""Regression coverage for credential-safe integration evidence."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_py import common  # noqa: E402


def read_evidence(root: Path, name: str) -> str:
    return (root / name).read_text(encoding="utf-8")


def main() -> None:
    secrets = {
        "access": "access-secret-value-001",
        "refresh": "refresh-secret-value-002",
        "share": "share-secret-value-003",
        "password": "password-secret-value-004",
        "s3": "s3-secret-value-005",
        "database": "database-secret-value-006",
        "aws_access": "AKIA0123456789ABCDEF",
        "aws_secret": "aws-secret-access-value-007",
        "aws_session": "aws-session-token-value-008",
        "minio_user": "minio-root-user-value-009",
        "jwt": (
            "eyJhbGciOiJIUzI1NiJ9."
            "eyJzdWIiOiJldmlkZW5jZS10ZXN0In0."
            "signature_value"
        ),
    }

    with tempfile.TemporaryDirectory(prefix="disk-evidence-redaction-") as temporary:
        evidence_root = Path(temporary)
        original_root = common.EVIDENCE_DIR
        common.EVIDENCE_DIR = str(evidence_root)
        try:
            structured = {
                "data": {
                    "access_token": secrets["access"],
                    "refresh-token": secrets["refresh"],
                    "nested": [
                        {"X-Share-Token": secrets["share"]},
                        {"password_hash": secrets["password"]},
                        {"SecretAccessKey": secrets["aws_secret"]},
                    ],
                    "continuation_token": "page-token-kept",
                    "upload_id": "upload-id-kept",
                    "jti": "jti-kept",
                },
                "message": f"Bearer {secrets['jwt']}",
            }
            common.save_evidence("structured.json", json.dumps(structured))
            structured_text = read_evidence(evidence_root, "structured.json")
            structured_saved = json.loads(structured_text)

            assert structured_saved["data"]["access_token"] == common.REDACTED
            assert structured_saved["data"]["refresh-token"] == common.REDACTED
            assert structured_saved["data"]["nested"][0]["X-Share-Token"] == common.REDACTED
            assert structured_saved["data"]["nested"][1]["password_hash"] == common.REDACTED
            assert structured_saved["data"]["continuation_token"] == "page-token-kept"
            assert structured_saved["data"]["upload_id"] == "upload-id-kept"
            assert structured_saved["data"]["jti"] == "jti-kept"
            assert common.REDACTED in structured_saved["message"]

            plain_text = "\n".join(
                (
                    f"Authorization: Bearer {secrets['jwt']}",
                    f"X-Share-Token: {secrets['share']}",
                    f"DISK_S3_SECRET_KEY={secrets['s3']}",
                    f"DATABASE_PASSWORD='{secrets['database']}'",
                    f"aws_access_key={secrets['aws_access']}",
                    f"AWS_SECRET_ACCESS_KEY={secrets['aws_secret']}",
                    f"AWS_SESSION_TOKEN={secrets['aws_session']}",
                    f"MINIO_ROOT_USER={secrets['minio_user']}",
                    f"postgresql://disk:{secrets['password']}@database.internal/disk",
                )
            )
            common.save_evidence("plain.txt", plain_text)
            plain_saved = read_evidence(evidence_root, "plain.txt")

            for secret in secrets.values():
                assert secret not in structured_text
                assert secret not in plain_saved
            assert plain_saved.count(common.REDACTED) >= 6

            synthetic_payload = "synthetic fixture bytes only"
            common.save_raw_evidence("synthetic.raw", synthetic_payload)
            assert read_evidence(evidence_root, "synthetic.raw") == synthetic_payload
        finally:
            common.EVIDENCE_DIR = original_root

    print("Evidence redaction regression passed")


if __name__ == "__main__":
    main()

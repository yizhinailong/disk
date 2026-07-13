#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["boto3"]
# ///

"""Optional MinIO/S3 smoke test for the object-storage fixture.

This test is intentionally skipped unless DISK_S3_INTEGRATION=1 so the
standard CTest suite does not require MinIO or AWS credentials.
"""

import os
import sys
import uuid


def main() -> int:
    if os.environ.get("DISK_S3_INTEGRATION") != "1":
        print("SKIP: DISK_S3_INTEGRATION is not 1; skipping S3/MinIO integration smoke")
        return 0

    missing = [
        name
        for name in (
            "DISK_S3_ENDPOINT",
            "DISK_S3_BUCKET",
            "DISK_S3_ACCESS_KEY",
            "DISK_S3_SECRET_KEY",
        )
        if not os.environ.get(name)
    ]
    if missing:
        print(f"FAIL: missing required S3 integration env vars: {', '.join(missing)}")
        return 1

    import boto3
    from botocore.config import Config

    endpoint = os.environ["DISK_S3_ENDPOINT"]
    bucket = os.environ["DISK_S3_BUCKET"]
    access_key = os.environ["DISK_S3_ACCESS_KEY"]
    secret_key = os.environ["DISK_S3_SECRET_KEY"]
    region = os.environ.get("DISK_S3_REGION", "us-east-1")
    key = f"objects/integration/{uuid.uuid4().hex}.bin"
    payload = b"0123456789abcdef" * 1024

    client = boto3.client(
        "s3",
        endpoint_url=endpoint,
        aws_access_key_id=access_key,
        aws_secret_access_key=secret_key,
        region_name=region,
        config=Config(s3={"addressing_style": "path"}),
    )

    client.put_object(Bucket=bucket, Key=key, Body=payload)
    try:
        head = client.head_object(Bucket=bucket, Key=key)
        if head["ContentLength"] != len(payload):
            print(f"FAIL: expected size {len(payload)}, got {head['ContentLength']}")
            return 1

        obj = client.get_object(Bucket=bucket, Key=key, Range="bytes=2-5")
        ranged = obj["Body"].read()
        if ranged != payload[2:6]:
            print(f"FAIL: range read mismatch: {ranged!r}")
            return 1
    finally:
        client.delete_object(Bucket=bucket, Key=key)

    try:
        client.head_object(Bucket=bucket, Key=key)
        print("FAIL: object still exists after delete")
        return 1
    except Exception:
        pass

    print("PASS: S3/MinIO object put/head/range/delete smoke succeeded")
    return 0


if __name__ == "__main__":
    sys.exit(main())

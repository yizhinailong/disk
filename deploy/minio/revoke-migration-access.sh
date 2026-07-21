#!/bin/sh

set -eu

: "${MINIO_ROOT_USER:?MINIO_ROOT_USER is required}"
: "${MINIO_ROOT_PASSWORD:?MINIO_ROOT_PASSWORD is required}"
: "${DISK_S3_MIGRATION_ACCESS_KEY:?DISK_S3_MIGRATION_ACCESS_KEY is required}"

if [ "${DISK_S3_ACCESS_KEY+x}" = "x" ] || [ "${DISK_S3_SECRET_KEY+x}" = "x" ]; then
    echo "Disk application credentials must not be passed to migration revocation" >&2
    exit 1
fi

MC_BIN="${DISK_MC_BIN:-mc}"
MC_CONFIG_DIR="${MC_CONFIG_DIR:-/tmp/disk-mc}"

run_mc() {
    "${MC_BIN}" --config-dir "${MC_CONFIG_DIR}" "$@"
}

run_mc alias set local "${DISK_S3_ENDPOINT:-http://minio:9000}" \
    "${MINIO_ROOT_USER}" "${MINIO_ROOT_PASSWORD}"
run_mc admin user remove local "${DISK_S3_MIGRATION_ACCESS_KEY}"

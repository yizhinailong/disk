#!/bin/sh

set -eu

: "${MINIO_ROOT_USER:?MINIO_ROOT_USER is required}"
: "${MINIO_ROOT_PASSWORD:?MINIO_ROOT_PASSWORD is required}"
: "${DISK_S3_BUCKET:?DISK_S3_BUCKET is required}"
: "${DISK_S3_ACCESS_KEY:?DISK_S3_ACCESS_KEY is required}"
: "${DISK_S3_SECRET_KEY:?DISK_S3_SECRET_KEY is required}"
: "${DISK_S3_MIGRATION_ACCESS_KEY:?DISK_S3_MIGRATION_ACCESS_KEY is required}"
: "${DISK_S3_MIGRATION_SECRET_KEY:?DISK_S3_MIGRATION_SECRET_KEY is required}"

if [ "${MINIO_ROOT_USER}" = "${DISK_S3_ACCESS_KEY}" ] || \
    [ "${MINIO_ROOT_USER}" = "${DISK_S3_MIGRATION_ACCESS_KEY}" ] || \
    [ "${DISK_S3_ACCESS_KEY}" = "${DISK_S3_MIGRATION_ACCESS_KEY}" ]; then
    echo "MinIO root, application, and migration access keys must differ" >&2
    exit 1
fi
if [ "${MINIO_ROOT_PASSWORD}" = "${DISK_S3_SECRET_KEY}" ] || \
    [ "${MINIO_ROOT_PASSWORD}" = "${DISK_S3_MIGRATION_SECRET_KEY}" ] || \
    [ "${DISK_S3_SECRET_KEY}" = "${DISK_S3_MIGRATION_SECRET_KEY}" ]; then
    echo "MinIO root, application, and migration secret keys must differ" >&2
    exit 1
fi
if [ "${DISK_S3_BUCKET}" != "disk" ]; then
    echo "deploy/minio policies must be rendered for bucket ${DISK_S3_BUCKET}" >&2
    exit 1
fi

MC_BIN="${DISK_MC_BIN:-mc}"
MC_CONFIG_DIR="${MC_CONFIG_DIR:-/tmp/disk-mc}"
LIFECYCLE_FILE="${DISK_S3_LIFECYCLE_FILE:-/config/lifecycle.json}"
APP_POLICY_FILE="${DISK_S3_POLICY_FILE:-/config/app-policy.json}"
MIGRATION_POLICY_FILE="${DISK_S3_MIGRATION_POLICY_FILE:-/config/migration-policy.json}"

run_mc() {
    "${MC_BIN}" --config-dir "${MC_CONFIG_DIR}" "$@"
}

run_mc alias set local "${DISK_S3_ENDPOINT:-http://minio:9000}" \
    "${MINIO_ROOT_USER}" "${MINIO_ROOT_PASSWORD}"
run_mc mb --ignore-existing "local/${DISK_S3_BUCKET}"
run_mc version enable "local/${DISK_S3_BUCKET}"
versioning_info="$(run_mc --json version info "local/${DISK_S3_BUCKET}")"
case "${versioning_info}" in
    *'"versioning":{"status":"Enabled"'*) ;;
    *)
        echo "MinIO bucket versioning is not enabled" >&2
        exit 1
        ;;
esac
run_mc ilm rule import "local/${DISK_S3_BUCKET}" < "${LIFECYCLE_FILE}"
run_mc admin user add local "${DISK_S3_ACCESS_KEY}" "${DISK_S3_SECRET_KEY}"
run_mc admin policy create local disk-app "${APP_POLICY_FILE}"
run_mc admin policy attach local disk-app --user "${DISK_S3_ACCESS_KEY}"
run_mc admin user add local "${DISK_S3_MIGRATION_ACCESS_KEY}" "${DISK_S3_MIGRATION_SECRET_KEY}"
run_mc admin policy create local disk-migration "${MIGRATION_POLICY_FILE}"
run_mc admin policy attach local disk-migration --user "${DISK_S3_MIGRATION_ACCESS_KEY}"
run_mc ilm rule export "local/${DISK_S3_BUCKET}"

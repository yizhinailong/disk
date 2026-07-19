#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MIGRATION_DIR="${REPO_ROOT}/sql/migrations"
MANIFEST="${MIGRATION_DIR}/manifest.tsv"
PSQL_BIN="${PSQL_BIN:-psql}"

if ! command -v "${PSQL_BIN}" >/dev/null 2>&1; then
    echo "migrate-db: psql executable not found: ${PSQL_BIN}" >&2
    exit 1
fi

if [[ ! -f "${MANIFEST}" ]]; then
    echo "migrate-db: manifest not found: ${MANIFEST}" >&2
    exit 1
fi

psql_args=(-X --set ON_ERROR_STOP=1)
if [[ -n "${DISK_DATABASE_URL:-}" ]]; then
    psql_args+=(--dbname "${DISK_DATABASE_URL}")
fi

checksum_file() {
    local file_path="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "${file_path}" | awk '{print $1}'
        return
    fi
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "${file_path}" | awk '{print $1}'
        return
    fi
    echo "migrate-db: sha256sum or shasum is required" >&2
    exit 1
}

"${PSQL_BIN}" "${psql_args[@]}" --command "
CREATE TABLE IF NOT EXISTS schema_migrations (
    version VARCHAR(128) PRIMARY KEY,
    checksum CHAR(64) NOT NULL,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);" >/dev/null

while IFS=$'\t' read -r version filename; do
    if [[ -z "${version}" || "${version}" == \#* ]]; then
        continue
    fi
    if [[ ! "${version}" =~ ^[A-Za-z0-9_.-]+$ ]]; then
        echo "migrate-db: invalid migration version: ${version}" >&2
        exit 1
    fi

    migration_path="${MIGRATION_DIR}/${filename}"
    if [[ ! -f "${migration_path}" ]]; then
        echo "migrate-db: migration file not found: ${migration_path}" >&2
        exit 1
    fi

    checksum="$(checksum_file "${migration_path}")"
    applied_checksum="$(
        "${PSQL_BIN}" "${psql_args[@]}" \
            --tuples-only --no-align \
            --command "SELECT checksum FROM schema_migrations WHERE version = '${version}';"
    )"

    if [[ -n "${applied_checksum}" ]]; then
        if [[ "${applied_checksum}" != "${checksum}" ]]; then
            echo "migrate-db: checksum mismatch for ${version}" >&2
            exit 1
        fi
        echo "migrate-db: ${version} already applied"
        continue
    fi

    echo "migrate-db: applying ${version}"
    "${PSQL_BIN}" "${psql_args[@]}" --single-transaction \
        --command "SELECT pg_advisory_xact_lock(hashtextextended('disk-schema-migrations', 0));" \
        --command "
DO \$migration_guard\$
DECLARE
    recorded_checksum TEXT;
BEGIN
    SELECT checksum INTO recorded_checksum
    FROM schema_migrations
    WHERE version = '${version}';

    IF recorded_checksum IS NOT NULL AND recorded_checksum <> '${checksum}' THEN
        RAISE EXCEPTION 'checksum mismatch for ${version} after acquiring migration lock';
    END IF;
END
\$migration_guard\$;" \
        --file "${migration_path}" \
        --command "
INSERT INTO schema_migrations (version, checksum)
VALUES ('${version}', '${checksum}')
ON CONFLICT (version) DO NOTHING;" >/dev/null
    echo "migrate-db: applied ${version}"
done < "${MANIFEST}"

#!/usr/bin/env bash

set -euo pipefail

usage() {
    printf 'Usage: %s {V002_share_audit|V003_distributed_upload|V004_storage_reconciliation}\n' "${0##*/}" >&2
}

if (( $# != 1 )); then
    usage
    exit 64
fi

version=$1
case "$version" in
    V002_share_audit)
        filename=V002_share_audit_rollback.sql
        ;;
    V003_distributed_upload)
        filename=V003_distributed_upload_rollback.sql
        ;;
    V004_storage_reconciliation)
        filename=V004_storage_reconciliation_rollback.sql
        ;;
    *)
        usage
        exit 64
        ;;
esac

if [[ ${DISK_SCHEMA_REVERSAL_CONTEXT:-} != pre_activation_reversal ]]; then
    printf 'Schema reversal requires DISK_SCHEMA_REVERSAL_CONTEXT=pre_activation_reversal; emergency application rollback preserves expand schema\n' >&2
    exit 77
fi
if [[ ${DISK_SCHEMA_REVERSAL_APPROVED:-} != true ]]; then
    printf 'Schema reversal requires DISK_SCHEMA_REVERSAL_APPROVED=true\n' >&2
    exit 77
fi
if [[ ! ${DISK_SCHEMA_CHANGE_TICKET:-} =~ ^[A-Za-z0-9][A-Za-z0-9._:/-]{5,127}$ ]]; then
    printf 'Schema reversal requires a valid DISK_SCHEMA_CHANGE_TICKET\n' >&2
    exit 77
fi
if [[ ! ${DISK_SCHEMA_READINESS_SHA256:-} =~ ^[0-9a-f]{64}$ ]]; then
    printf 'Schema reversal requires a lowercase 64-character DISK_SCHEMA_READINESS_SHA256\n' >&2
    exit 77
fi

psql_bin=${PSQL_BIN:-psql}
if ! command -v "$psql_bin" >/dev/null 2>&1; then
    printf 'Schema reversal requires psql: %s\n' "$psql_bin" >&2
    exit 69
fi

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
rollback_path="$repo_root/sql/migrations/$filename"
psql_args=(
    -X
    --set=ON_ERROR_STOP=1
    --set="disk_schema_reversal_context=$DISK_SCHEMA_REVERSAL_CONTEXT"
    --set="disk_schema_reversal_approved=$DISK_SCHEMA_REVERSAL_APPROVED"
    --set="disk_schema_change_ticket=$DISK_SCHEMA_CHANGE_TICKET"
    --set="disk_schema_readiness_sha256=$DISK_SCHEMA_READINESS_SHA256"
)
if [[ -n ${DISK_DATABASE_URL:-} ]]; then
    psql_args+=(--dbname "$DISK_DATABASE_URL")
fi

printf 'Applying approved pre-activation schema reversal %s for %s...\n' \
    "$version" "$DISK_SCHEMA_CHANGE_TICKET"
"$psql_bin" "${psql_args[@]}" --file "$rollback_path"

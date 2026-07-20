#!/usr/bin/env bash

set -euo pipefail

usage() {
    printf 'Usage: %s {freeze|open}\n' "${0##*/}" >&2
}

if (( $# != 1 )); then
    usage
    exit 64
fi

mode=$1
case "$mode" in
    freeze) ;;
    open)
        if [[ ${DISK_UPLOAD_UNFREEZE_APPROVED:-false} != true ]]; then
            printf 'Opening upload ingress requires DISK_UPLOAD_UNFREEZE_APPROVED=true\n' >&2
            exit 77
        fi
        ;;
    *)
        usage
        exit 64
        ;;
esac

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
env_file=${DISK_DISTRIBUTED_ENV_FILE:-.env.distributed}
if [[ "$env_file" != /* ]]; then
    env_file="$repo_root/$env_file"
fi

if [[ ! -f "$env_file" ]]; then
    printf 'Distributed environment file not found: %s\n' "$env_file" >&2
    exit 66
fi
if ! command -v docker >/dev/null 2>&1; then
    printf 'docker is required to switch upload ingress\n' >&2
    exit 69
fi

compose=(
    docker compose
    --env-file "$env_file"
    -f "$repo_root/docker-compose.distributed.yml"
)
if [[ "$mode" == freeze ]]; then
    compose+=(-f "$repo_root/deploy/docker-compose.upload-frozen.yml")
fi

printf 'Validating %s upload ingress configuration...\n' "$mode"
"${compose[@]}" config --quiet

printf 'Applying %s upload ingress configuration...\n' "$mode"
"${compose[@]}" up -d --no-deps --force-recreate load-balancer

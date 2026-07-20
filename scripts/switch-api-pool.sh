#!/usr/bin/env bash

set -euo pipefail

usage() {
    printf 'Usage: %s {dual|api-a-only}\n' "${0##*/}" >&2
}

if (( $# != 1 )); then
    usage
    exit 64
fi

mode=$1
case "$mode" in
    dual | api-a-only) ;;
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
    printf 'docker is required to switch the API pool\n' >&2
    exit 69
fi

compose=(
    docker compose
    --env-file "$env_file"
    -f "$repo_root/docker-compose.distributed.yml"
)
if [[ "$mode" == api-a-only ]]; then
    compose+=(-f "$repo_root/deploy/docker-compose.api-a-only.yml")
fi

printf 'Validating %s API pool configuration...\n' "$mode"
"${compose[@]}" config --quiet

printf 'Applying %s API pool configuration...\n' "$mode"
"${compose[@]}" up -d --no-deps --force-recreate load-balancer

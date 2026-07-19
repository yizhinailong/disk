#!/usr/bin/env bash
# test/benchmark/get_token.sh
# Obtain a JWT token from the login endpoint and output to stdout.

set -euo pipefail

# Configuration with env var overrides
BENCH_HOST="${BENCH_HOST:-http://127.0.0.1:8080}"
BENCH_ACCOUNT="${BENCH_ACCOUNT:-admin}"
BENCH_PASSWORD="${BENCH_PASSWORD:-Admin123}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Prerequisite checks
if ! command -v curl &>/dev/null; then
    echo "ERROR: curl is required but not found in PATH" >&2
    exit 1
fi

if ! command -v jq &>/dev/null; then
    echo "ERROR: jq is required but not found in PATH" >&2
    exit 1
fi

# Check server is reachable
if ! curl -sf "${BENCH_HOST}/api/health" >/dev/null 2>&1; then
    echo "ERROR: Server is not reachable at ${BENCH_HOST}" >&2
    echo "  Make sure the server is running and BENCH_HOST is set correctly." >&2
    exit 1
fi

# Request JWT token
RESPONSE=$(curl -s -X POST "${BENCH_HOST}/api/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"account\":\"${BENCH_ACCOUNT}\",\"password\":\"${BENCH_PASSWORD}\"}")

# Extract token from response
TOKEN=$(echo "$RESPONSE" | jq -r '.data.access_token // empty')

if [[ -z "$TOKEN" ]]; then
    echo "ERROR: Failed to obtain JWT token" >&2
    echo "  Response: $RESPONSE" >&2
    exit 1
fi

echo "$TOKEN"

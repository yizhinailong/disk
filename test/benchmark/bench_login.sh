#!/usr/bin/env bash
set -euo pipefail

# === Configuration ===
BENCH_HOST="${BENCH_HOST:-http://127.0.0.1:8080}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parse CLI arguments with defaults
NUM_REQUESTS="${1:-10000}"
CONCURRENCY="${2:-100}"
THREADS="${3:-4}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n) NUM_REQUESTS="$2"; shift 2 ;;
        -c) CONCURRENCY="$2"; shift 2 ;;
        -t) THREADS="$2"; shift 2 ;;
        *) shift ;;
    esac
done

echo "=========================================="
echo "  Benchmark: Login (POST /api/auth/login)"
echo "=========================================="
echo "  Host:        ${BENCH_HOST}"
echo "  Requests:    ${NUM_REQUESTS}"
echo "  Concurrency: ${CONCURRENCY}"
echo "  Threads:     ${THREADS}"
echo "=========================================="
echo ""

# === Prerequisite Checks ===
if ! command -v drogon_ctl &>/dev/null; then
    echo "ERROR: drogon_ctl not found in PATH" >&2
    exit 1
fi

if ! curl -sf "${BENCH_HOST}/api/health" >/dev/null 2>&1; then
    echo "ERROR: Server not reachable at ${BENCH_HOST}" >&2
    exit 1
fi

# === Run Benchmark ===
drogon_ctl press \
    -n "${NUM_REQUESTS}" \
    -c "${CONCURRENCY}" \
    -t "${THREADS}" \
    -f "${SCRIPT_DIR}/requests/login.json" \
    "${BENCH_HOST}/api/auth/login"
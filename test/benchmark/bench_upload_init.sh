#!/usr/bin/env bash
set -euo pipefail

# === Configuration ===
BENCH_HOST="${BENCH_HOST:-http://127.0.0.1:8080}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Default to SAFE values to stay under 240/min rate limit
NUM_REQUESTS="${1:-200}"
CONCURRENCY="${2:-10}"
THREADS="${3:-2}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n) NUM_REQUESTS="$2"; shift 2 ;;
        -c) CONCURRENCY="$2"; shift 2 ;;
        -t) THREADS="$2"; shift 2 ;;
        *) shift ;;
    esac
done

echo "=========================================="
echo "  Benchmark: Upload Init (POST /api/file/upload/init)"
echo "=========================================="
echo "  Host:        ${BENCH_HOST}"
echo "  Requests:    ${NUM_REQUESTS}"
echo "  Concurrency: ${CONCURRENCY}"
echo "  Threads:     ${THREADS}"
echo "=========================================="
echo ""
echo "  WARNING: Rate limit 240 requests/min/user."
echo "  Default parameters (-n 200 -c 10 -t 2) stay within this limit."
echo "  For high-throughput testing, temporarily adjust UploadRateLimitFilter in config.json."
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

# === Obtain JWT Token ===
TOKEN=$("${SCRIPT_DIR}/get_token.sh")
if [[ -z "$TOKEN" ]]; then
    echo "ERROR: Failed to obtain JWT token" >&2
    exit 1
fi
echo "JWT token obtained: ${TOKEN:0:20}..."
echo ""

# === Prepare Request JSON ===
TMP_JSON=$(mktemp "${SCRIPT_DIR}/bench_upload_init.XXXXXX.json")
trap 'rm -f "${TMP_JSON}"' EXIT

sed "s/__TOKEN__/${TOKEN}/g" "${SCRIPT_DIR}/requests/upload_init.json" > "${TMP_JSON}"

# === Run Benchmark ===
drogon_ctl press \
    -n "${NUM_REQUESTS}" \
    -c "${CONCURRENCY}" \
    -t "${THREADS}" \
    -f "${TMP_JSON}" \
    "${BENCH_HOST}/api/file/upload/init"
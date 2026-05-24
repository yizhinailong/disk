#!/usr/bin/env bash
# test/benchmark/bench_download.sh
# Download throughput benchmark measuring performance across file size categories.
# Uses curl with timing output for portability (does not require drogon_ctl).

set -euo pipefail

# === Configuration ===
BENCH_HOST="${BENCH_HOST:-http://127.0.0.1:8080}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Defaults
CONCURRENCY=50
REQUESTS=100
TOKEN=""
FILE_ID=""
OUTPUT_DIR=""
VERBOSE=0

# === Usage ===
usage() {
    cat <<'EOF'
Usage: bench_download.sh [OPTIONS]

Download throughput benchmark for the Disk file server.

Required:
  -t, --token TOKEN      JWT access token (or set BENCH_TOKEN)
  -f, --file-id ID       File ID to download (or set BENCH_FILE_ID)

Options:
  -u, --url URL          Base URL (default: http://127.0.0.1:8080)
  -c, --concurrency N    Concurrent workers (default: 50)
  -n, --requests N       Total requests per category (default: 100)
  -o, --output DIR       Output directory for CSV results (default: cwd)
  -v, --verbose          Print per-request details
  -h, --help             Show this help message

File Size Categories:
  small   < 200KB    (streaming path in DownloadResponder)
  medium  200KB-1MB  (sendfile path)
  large   1MB-10MB   (sendfile path)
  xlarge  > 100MB    (sendfile with large files)

Output:
  CSV file: bench_download_YYYYMMDD_HHMMSS.csv
  Columns: category,file_size_bytes,requests,total_time_s,avg_latency_ms,
           throughput_mbs,errors

Examples:
  # Basic run with a specific file
  bench_download.sh -t "$TOKEN" -f "abc123"

  # Custom concurrency and request count
  bench_download.sh -t "$TOKEN" -f "abc123" -c 20 -n 50

  # Using environment variables
  BENCH_TOKEN="$TOKEN" BENCH_FILE_ID="abc123" bench_download.sh

  # Save results to a specific directory
  bench_download.sh -t "$TOKEN" -f "abc123" -o /tmp/results

Environment Variables:
  BENCH_HOST       Server base URL (default: http://127.0.0.1:8080)
  BENCH_TOKEN      JWT access token
  BENCH_FILE_ID    File ID to download
  BENCH_ACCOUNT    Login account for auto-token (default: admin)
  BENCH_PASSWORD   Login password for auto-token (default: Admin123)

Note:
  This script downloads real file content to measure throughput.
  Server must be running and the file must exist.
  For --size-test mode, use a file with known size and ensure the
  server download path is reachable.
EOF
    exit 0
}

# === Parse Arguments ===
while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--token)       TOKEN="$2"; shift 2 ;;
        -f|--file-id)     FILE_ID="$2"; shift 2 ;;
        -u|--url)         BENCH_HOST="$2"; shift 2 ;;
        -c|--concurrency) CONCURRENCY="$2"; shift 2 ;;
        -n|--requests)    REQUESTS="$2"; shift 2 ;;
        -o|--output)      OUTPUT_DIR="$2"; shift 2 ;;
        -v|--verbose)     VERBOSE=1; shift ;;
        -h|--help)        usage ;;
        *) echo "Unknown option: $1" >&2; usage ;;
    esac
done

# Apply environment variable fallbacks
TOKEN="${TOKEN:-${BENCH_TOKEN:-}}"
FILE_ID="${FILE_ID:-${BENCH_FILE_ID:-}}"

# === Validate Required Parameters ===
if [[ -z "$TOKEN" ]]; then
    echo "INFO: No token provided, attempting auto-login..." >&2
    TOKEN=$("${SCRIPT_DIR}/get_token.sh" 2>/dev/null || true)
    if [[ -z "$TOKEN" ]]; then
        echo "ERROR: JWT token required. Use -t TOKEN or set BENCH_TOKEN." >&2
        exit 1
    fi
    echo "INFO: Token obtained via auto-login." >&2
fi

if [[ -z "$FILE_ID" ]]; then
    echo "ERROR: File ID required. Use -f FILE_ID or set BENCH_FILE_ID." >&2
    exit 1
fi

# === Prerequisite Checks ===
if ! command -v curl &>/dev/null; then
    echo "ERROR: curl is required but not found in PATH." >&2
    exit 1
fi

if ! command -v bc &>/dev/null; then
    echo "ERROR: bc is required but not found in PATH." >&2
    exit 1
fi

if ! curl -sf "${BENCH_HOST}/api/health" >/dev/null 2>&1; then
    echo "ERROR: Server not reachable at ${BENCH_HOST}" >&2
    exit 1
fi

# === Determine File Size ===
# Fetch download info to get file size without downloading the whole file
echo "INFO: Fetching file metadata for ${FILE_ID}..." >&2

FILE_INFO=$(curl -s -w "\n%{http_code}" \
    -H "Authorization: Bearer ${TOKEN}" \
    "${BENCH_HOST}/api/file/download/${FILE_ID}/info")

HTTP_STATUS=$(echo "$FILE_INFO" | tail -1)
FILE_INFO_BODY=$(echo "$FILE_INFO" | sed '$d')

if [[ "$HTTP_STATUS" != "200" ]]; then
    echo "ERROR: Failed to get file info (HTTP ${HTTP_STATUS})" >&2
    echo "  Response: ${FILE_INFO_BODY}" >&2
    exit 1
fi

# Extract file size from the response
if command -v jq &>/dev/null; then
    FILE_SIZE=$(echo "$FILE_INFO_BODY" | jq -r '.data.size // 0')
    FILE_NAME=$(echo "$FILE_INFO_BODY" | jq -r '.data.name // "unknown"')
else
    # Fallback: rough parse without jq
    FILE_SIZE=$(echo "$FILE_INFO_BODY" | grep -o '"size":[0-9]*' | head -1 | cut -d: -f2)
    FILE_SIZE="${FILE_SIZE:-0}"
    FILE_NAME="unknown"
fi

if [[ "$FILE_SIZE" -eq 0 ]]; then
    echo "ERROR: Could not determine file size from response." >&2
    echo "  Response: ${FILE_INFO_BODY}" >&2
    exit 1
fi

# === Determine Category ===
determine_category() {
    local size="$1"
    if [[ "$size" -lt 204800 ]]; then
        echo "small"
    elif [[ "$size" -lt 1048576 ]]; then
        echo "medium"
    elif [[ "$size" -lt 10485760 ]]; then
        echo "large"
    else
        echo "xlarge"
    fi
}

CATEGORY=$(determine_category "$FILE_SIZE")

# === Display Header ===
echo "=========================================="
echo "  Benchmark: File Download"
echo "=========================================="
echo "  Host:        ${BENCH_HOST}"
echo "  File ID:     ${FILE_ID}"
echo "  File Name:   ${FILE_NAME}"
echo "  File Size:   ${FILE_SIZE} bytes"
echo "  Category:    ${CATEGORY}"
echo "  Requests:    ${REQUESTS}"
echo "  Concurrency: ${CONCURRENCY}"
echo "  Token:       ${TOKEN:0:20}..."
echo "=========================================="
echo ""

# === Prepare Output ===
TIMESTAMP=$(date '+%Y%m%d_%H%M%S')
if [[ -n "$OUTPUT_DIR" ]]; then
    mkdir -p "$OUTPUT_DIR"
    CSV_FILE="${OUTPUT_DIR}/bench_download_${TIMESTAMP}.csv"
else
    CSV_FILE="bench_download_${TIMESTAMP}.csv"
fi

# CSV header
echo "category,file_size_bytes,requests,total_time_s,avg_latency_ms,throughput_mbs,errors" > "$CSV_FILE"

# === Run Benchmark ===
# Curl write-out format for timing data
CURL_FORMAT='status:%{http_code} size:%{size_download} time:%{time_total} speed:%{speed_download}\n'

TMP_DIR=$(mktemp -d "${SCRIPT_DIR}/bench_download.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

echo "Running ${REQUESTS} download requests with concurrency ${CONCURRENCY}..."
echo ""

# Worker function: runs a single download and records timing
download_worker() {
    local worker_id="$1"
    local token="$2"
    local url="$3"
    local tmp_dir="$4"
    local verbose="$5"

    local result
    result=$(curl -s -o /dev/null \
        -w "status:%{http_code} size:%{size_download} time:%{time_total} speed:%{speed_download}" \
        -H "Authorization: Bearer ${token}" \
        "${url}" 2>/dev/null)

    local status size time_spent speed
    status=$(echo "$result" | sed 's/.*status:\([0-9]*\).*/\1/')
    size=$(echo "$result" | sed 's/.*size:\([0-9]*\).*/\1/')
    time_spent=$(echo "$result" | sed 's/.*time:\([0-9.]*\).*/\1/')
    speed=$(echo "$result" | sed 's/.*speed:\([0-9.]*\).*/\1/')

    # Write result to temp file
    echo "${status},${size},${time_spent},${speed}" > "${tmp_dir}/result_${worker_id}.csv"

    if [[ "$verbose" -eq 1 ]]; then
        echo "  [worker ${worker_id}] status=${status} size=${size} time=${time_spent}s speed=${speed} B/s" >&2
    fi
}

export -f download_worker

BATCH_START=$(date +%s.%N)

# Launch requests in batches to control concurrency
PENDING=0
ERRORS=0
TOTAL_LATENCY=0
TOTAL_BYTES=0

for i in $(seq 1 "$REQUESTS"); do
    download_worker "$i" "$TOKEN" "${BENCH_HOST}/api/file/download/${FILE_ID}" "$TMP_DIR" "$VERBOSE" &
    PENDING=$((PENDING + 1))

    # Throttle to concurrency limit
    if [[ "$PENDING" -ge "$CONCURRENCY" ]]; then
        wait -n 2>/dev/null || true
        PENDING=$((PENDING - 1))
    fi

    # Progress indicator
    if [[ $((i % 50)) -eq 0 ]]; then
        echo "  Progress: ${i}/${REQUESTS} requests launched..." >&2
    fi
done

# Wait for all remaining background jobs
wait

BATCH_END=$(date +%s.%N)
TOTAL_TIME=$(echo "${BATCH_END} - ${BATCH_START}" | bc -l)

echo ""
echo "All requests completed. Aggregating results..."

# === Aggregate Results ===
ERRORS=0
TOTAL_LATENCY_MS=0
REQUEST_COUNT=0

for result_file in "${TMP_DIR}"/result_*.csv; do
    [[ -f "$result_file" ]] || continue

    IFS=',' read -r status size time_spent speed < "$result_file"

    REQUEST_COUNT=$((REQUEST_COUNT + 1))
    TOTAL_LATENCY_MS=$(echo "${TOTAL_LATENCY_MS} + (${time_spent} * 1000)" | bc -l)
    TOTAL_BYTES=$(echo "${TOTAL_BYTES} + ${size}" | bc -l)

    if [[ "$status" != "200" ]]; then
        ERRORS=$((ERRORS + 1))
    fi
done

if [[ "$REQUEST_COUNT" -eq 0 ]]; then
    echo "ERROR: No results collected." >&2
    exit 1
fi

# Calculate metrics
AVG_LATENCY=$(echo "scale=2; ${TOTAL_LATENCY_MS} / ${REQUEST_COUNT}" | bc -l)

# Throughput: total bytes / total time, converted to MB/s
# We use TOTAL_TIME (wall clock) rather than sum of per-request times
THROUGHPUT=$(echo "scale=2; (${TOTAL_BYTES} / 1048576) / ${TOTAL_TIME}" | bc -l)

# Write CSV row
echo "${CATEGORY},${FILE_SIZE},${REQUEST_COUNT},${TOTAL_TIME},${AVG_LATENCY},${THROUGHPUT},${ERRORS}" >> "$CSV_FILE"

# === Display Results ===
echo ""
echo "=========================================="
echo "  DOWNLOAD BENCHMARK RESULTS"
echo "=========================================="
echo "  Category:       ${CATEGORY}"
echo "  File Size:      ${FILE_SIZE} bytes"
echo "  Requests:       ${REQUEST_COUNT}"
echo "  Total Time:     ${TOTAL_TIME} s"
echo "  Avg Latency:    ${AVG_LATENCY} ms"
echo "  Throughput:     ${THROUGHPUT} MB/s"
echo "  Total Download: ${TOTAL_BYTES} bytes"
echo "  Errors:         ${ERRORS}"
echo "=========================================="
echo ""
echo "Results saved to: ${CSV_FILE}"
echo ""

if [[ "$ERRORS" -gt 0 ]]; then
    echo "WARNING: ${ERRORS} requests returned non-200 status codes." >&2
fi

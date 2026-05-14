#!/usr/bin/env bash
set -uo pipefail

# === Configuration ===
BENCH_HOST="${BENCH_HOST:-http://127.0.0.1:8080}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Optional override via environment variables
BENCH_N="${BENCH_N:-}"
BENCH_C="${BENCH_C:-}"
BENCH_T="${BENCH_T:-}"

# Build extra args for sub-scripts
EXTRA_ARGS=()
[[ -n "${BENCH_N}" ]] && EXTRA_ARGS+=(-n "${BENCH_N}")
[[ -n "${BENCH_C}" ]] && EXTRA_ARGS+=(-c "${BENCH_C}")
[[ -n "${BENCH_T}" ]] && EXTRA_ARGS+=(-t "${BENCH_T}")

# === Prerequisite Checks ===
echo "============================================"
echo "  Drogon Benchmark Suite - run_all.sh"
echo "============================================"
echo "  Host:   ${BENCH_HOST}"
echo "  Time:   $(date '+%Y-%m-%d %H:%M:%S')"
echo "============================================"
echo ""

if ! command -v drogon_ctl &>/dev/null; then
    echo "ERROR: drogon_ctl not found in PATH" >&2
    exit 1
fi

if ! curl -sf "${BENCH_HOST}/api/health" >/dev/null 2>&1; then
    echo "ERROR: Server not reachable at ${BENCH_HOST}" >&2
    exit 1
fi

echo "Prerequisites OK. Starting benchmarks..."
echo ""

# === Run Benchmarks ===
FAILURES=0
RESULTS=()

run_bench() {
    local name="$1"
    local script="$2"
    shift 2

    echo ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
    echo "  Starting: ${name}"
    echo "  Time: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
    echo ""

    if bash "${script}" "$@" 2>&1; then
        RESULTS+=("PASS: ${name}")
    else
        RESULTS+=("FAIL: ${name}")
        FAILURES=$((FAILURES + 1))
    fi

    echo ""
    echo "--------------------------------------------"
    echo "  Completed: ${name}"
    echo "  Time: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "--------------------------------------------"
    echo ""
}

# Run each benchmark sequentially
run_bench "Health Check"   "${SCRIPT_DIR}/bench_health.sh"       "${EXTRA_ARGS[@]}"
run_bench "Login"          "${SCRIPT_DIR}/bench_login.sh"        "${EXTRA_ARGS[@]}"
run_bench "File List"      "${SCRIPT_DIR}/bench_file_list.sh"    "${EXTRA_ARGS[@]}"
run_bench "Upload Init"    "${SCRIPT_DIR}/bench_upload_init.sh"  "${EXTRA_ARGS[@]}"

# === Summary ===
echo "============================================"
echo "  BENCHMARK SUMMARY"
echo "============================================"
for result in "${RESULTS[@]}"; do
    echo "  ${result}"
done
echo "============================================"
echo "  Total: ${#RESULTS[@]} | Passed: $(( ${#RESULTS[@]} - FAILURES )) | Failed: ${FAILURES}"
echo "  Time: $(date '+%Y-%m-%d %H:%M:%S')"
echo "============================================"

if [[ ${FAILURES} -gt 0 ]]; then
    exit 1
fi
exit 0
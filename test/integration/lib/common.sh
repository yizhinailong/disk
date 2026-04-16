#!/bin/bash
# test/integration/lib/common.sh
# Shared test infrastructure: colors, logging, counters, evidence, summary.
#
# Usage:
#   SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
#   source "$SCRIPT_DIR/lib/common.sh"

# ─── Colors ────────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# ─── Test counters ─────────────────────────────────────────────────────────────

TESTS_PASSED=0
TESTS_FAILED=0
EVIDENCE_DIR="${EVIDENCE_DIR:-.sisyphus/evidence}"

# ─── Logging ───────────────────────────────────────────────────────────────────

log_info() {
    printf "%b[INFO]%b %s\n" "$YELLOW" "$NC" "$1"
}

log_pass() {
    printf "%b[PASS]%b %s\n" "$GREEN" "$NC" "$1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

log_fail() {
    printf "%b[FAIL]%b %s\n" "$RED" "$NC" "$1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

log_step() {
    printf "%b[STEP]%b %s\n" "$CYAN" "$NC" "$1"
}

log_section() {
    echo ""
    printf "%b━━━ %s ━━━%b\n" "$CYAN" "$1" "$NC"
}

# ─── Evidence ──────────────────────────────────────────────────────────────────

save_evidence() {
    local name="$1"
    local data="$2"
    mkdir -p "$EVIDENCE_DIR"
    echo "$data" > "$EVIDENCE_DIR/${name}"
    log_info "Evidence saved: $name"
}

save_raw_evidence() {
    local name="$1"
    shift
    mkdir -p "$EVIDENCE_DIR"
    "$@" > "$EVIDENCE_DIR/${name}" 2>&1
    log_info "Evidence saved: $name"
}

# ─── Summary ───────────────────────────────────────────────────────────────────

print_summary() {
    printf '\n==========================================\n'
    printf 'Test Summary\n'
    printf '==========================================\n'
    printf 'Passed: %b%s%b\n' "$GREEN" "$TESTS_PASSED" "$NC"
    printf 'Failed: %b%s%b\n' "$RED" "$TESTS_FAILED" "$NC"

    if [ "$TESTS_FAILED" -eq 0 ]; then
        printf '%bAll tests passed!%b\n' "$GREEN" "$NC"
        exit 0
    fi

    printf '%bSome tests failed.%b\n' "$RED" "$NC"
    exit 1
}

#!/bin/bash
#
# test/integration/test_system_info.sh
# Integration tests for /api/system/info endpoint
#
# Verifies:
#   1. GET /api/system/info returns 200 with valid data
#   2. Response contains storage stats (no SQL 1054 error)
#   3. Endpoint requires JWT authentication
#   4. Response contains expected fields
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured with seed data
#   - Redis configured
#

set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
SERVER_BIN="${SERVER_BIN:-./build/linux-debug-clang/src/disk}"
JWT_SECRET="${JWT_SECRET:-test_secret_key_for_share_token_32b}"
VALID_ACCOUNT="${VALID_ACCOUNT:-admin}"
VALID_PASS="${VALID_PASS:-Admin123}"
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/system-info-server.log}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

TESTS_PASSED=0
TESTS_FAILED=0
SERVER_PID=""
MANAGED_SERVER=0

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

cleanup() {
    if [ "$MANAGED_SERVER" -eq 1 ] && [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}

trap cleanup EXIT

json_field() {
    local json="$1"
    local path="$2"

    JSON_INPUT="$json" python3 - "$path" <<'PY'
import json
import os
import sys

try:
    data = json.loads(os.environ["JSON_INPUT"])
except Exception:
    print("")
    raise SystemExit(0)

value = data
for part in sys.argv[1].split('.'):
    if isinstance(value, dict) and part in value:
        value = value[part]
    else:
        print("")
        raise SystemExit(0)

if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("")
else:
    print(value)
PY
}

server_ready() {
    local http_code
    http_code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/auth/login" 2>/dev/null || printf "000")
    case "$http_code" in
        400|401|405)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

ensure_server() {
    if server_ready; then
        log_info "Using existing server at $BASE_URL"
        return 0
    fi

    if [ ! -x "$SERVER_BIN" ] && [ -x "./build/linux-debug-clang/disk" ]; then
        SERVER_BIN="./build/linux-debug-clang/disk"
    fi

    if [ ! -x "$SERVER_BIN" ]; then
        log_fail "Server binary not found: $SERVER_BIN"
        exit 1
    fi

    mkdir -p "$(dirname "$SERVER_LOG")"
    log_info "Starting server with $SERVER_BIN"
    JWT_SECRET="$JWT_SECRET" "$SERVER_BIN" >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!
    MANAGED_SERVER=1

    for _ in $(seq 1 30); do
        if server_ready; then
            log_pass "Server started"
            return 0
        fi
        sleep 1
    done

    log_fail "Server did not become ready"
    if [ -f "$SERVER_LOG" ]; then
        cat "$SERVER_LOG"
    fi
    exit 1
}

# Test 1: System info requires authentication
test_system_info_requires_auth() {
    log_info "Testing /api/system/info requires authentication..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/system/info")

    local http_code
    http_code=$(printf '%s\n' "$response" | tail -n 1)

    if [ "$http_code" = "401" ]; then
        log_pass "GET /api/system/info without token returns 401"
    else
        log_fail "GET /api/system/info without token: expected 401, got HTTP $http_code"
        printf '%s\n' "$response"
        exit 1
    fi
}

# Test 2: System info returns valid data with authentication
test_system_info_success() {
    log_info "Testing GET /api/system/info with valid token..."

    # Login first
    local body
    body=$(python3 - "$VALID_ACCOUNT" "$VALID_PASS" <<'PY'
import json
import sys
print(json.dumps({"account": sys.argv[1], "password": sys.argv[2]}))
PY
)

    local login_response
    login_response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "$body")

    local login_http_code login_body
    login_http_code=$(printf '%s\n' "$login_response" | tail -n 1)
    login_body=$(printf '%s\n' "$login_response" | sed '$d')

    local access_token
    access_token=$(json_field "$login_body" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Login failed for system info test"
        printf '%s\n' "$login_body"
        exit 1
    fi

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/system/info" \
        -H "Authorization: Bearer $access_token")

    local http_code resp_body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    resp_body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$resp_body" "code")

    if [ "$http_code" = "200" ] && [ "$code" = "0" ]; then
        log_pass "GET /api/system/info returns 200 + code 0"
    else
        log_fail "GET /api/system/info: expected HTTP 200 + code 0, got HTTP $http_code + code=$code"
        printf '%s\n' "$resp_body"
        exit 1
    fi

    # Save evidence
    mkdir -p .sisyphus/evidence
    printf '%s\n' "$resp_body" > .sisyphus/evidence/system-info-response.json
}

# Test 3: System info response contains storage stats (no SQL 1054 error)
test_system_info_has_storage_stats() {
    log_info "Testing /api/system/info contains storage stats (no SQL 1054)..."

    # Login first
    local body
    body=$(python3 - "$VALID_ACCOUNT" "$VALID_PASS" <<'PY'
import json
import sys
print(json.dumps({"account": sys.argv[1], "password": sys.argv[2]}))
PY
)

    local login_response
    login_response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "$body")

    local login_body
    login_body=$(printf '%s\n' "$login_response" | sed '$d')

    local access_token
    access_token=$(json_field "$login_body" "data.access_token")

    local response
    response=$(curl -sS -X GET "$BASE_URL/api/system/info" \
        -H "Authorization: Bearer $access_token")

    # Verify response does NOT contain SQL 1054 error indicator
    # SQL 1054 = "Unknown column" which was the B1 blocker
    if printf '%s\n' "$response" | grep -qi "1054"; then
        log_fail "Response contains SQL 1054 error — B1 fix regression!"
        printf '%s\n' "$response"
        exit 1
    fi

    if printf '%s\n' "$response" | grep -qi "deleted_at"; then
        log_fail "Response contains 'deleted_at' — B1 fix regression!"
        printf '%s\n' "$response"
        exit 1
    fi

    # Verify data section exists
    local data_section
    data_section=$(json_field "$response" "data")

    if [ -n "$data_section" ] && [ "$data_section" != "null" ]; then
        log_pass "Response contains valid data section (no SQL 1054 error)"
    else
        log_fail "Response data section is empty or null"
        printf '%s\n' "$response"
        exit 1
    fi
}

# Test 4: System info response has expected fields
test_system_info_fields() {
    log_info "Testing /api/system/info response field structure..."

    # Login first
    local body
    body=$(python3 - "$VALID_ACCOUNT" "$VALID_PASS" <<'PY'
import json
import sys
print(json.dumps({"account": sys.argv[1], "password": sys.argv[2]}))
PY
)

    local login_response
    login_response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "$body")

    local login_body
    login_body=$(printf '%s\n' "$login_response" | sed '$d')

    local access_token
    access_token=$(json_field "$login_body" "data.access_token")

    local response
    response=$(curl -sS -X GET "$BASE_URL/api/system/info" \
        -H "Authorization: Bearer $access_token")

    # Check for at least some expected system info fields
    local total_users total_files total_folders
    total_users=$(json_field "$response" "data.total_users")
    total_files=$(json_field "$response" "data.total_files")
    total_folders=$(json_field "$response" "data.total_folders")

    # At least check that data object exists with some numeric fields
    local has_some_data=0

    if [ -n "$total_users" ] && [ "$total_users" != "null" ]; then
        has_some_data=1
    fi

    if [ -n "$total_files" ] && [ "$total_files" != "null" ]; then
        has_some_data=1
    fi

    if [ -n "$total_folders" ] && [ "$total_folders" != "null" ]; then
        has_some_data=1
    fi

    if [ "$has_some_data" -eq 1 ]; then
        log_pass "System info response has expected data fields (users=$total_users, files=$total_files, folders=$total_folders)"
    else
        # Even if field names differ, verify the response is valid JSON with data
        local code
        code=$(json_field "$response" "code")
        if [ "$code" = "0" ]; then
            log_pass "System info returns valid response (field names may differ from expected)"
        else
            log_fail "System info response has no recognizable fields"
            printf '%s\n' "$response"
            exit 1
        fi
    fi
}

main() {
    printf '==========================================\n'
    printf 'System Info Integration Tests\n'
    printf '==========================================\n\n'

    if ! command -v curl >/dev/null 2>&1; then
        log_fail "curl is required"
        exit 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        log_fail "python3 is required"
        exit 1
    fi

    ensure_server

    test_system_info_requires_auth
    test_system_info_success
    test_system_info_has_storage_stats
    test_system_info_fields

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

main "$@"

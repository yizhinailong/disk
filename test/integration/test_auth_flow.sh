#!/bin/bash
#
# test/integration/test_auth_flow.sh
# Integration tests for login success/failure auth flow (NOT rate limiting)
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured with seed data
#   - Redis configured
#
# Usage:
#   ./test/integration/test_auth_flow.sh
#
# Environment variables:
#   BASE_URL    - Server URL (default: http://127.0.0.1:8080)
#   TEST_USER   - Test username (default: admin)
#   TEST_PASS   - Test password (default: Admin123)
#

set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
SERVER_BIN="${SERVER_BIN:-./build/linux-debug-clang/src/disk}"
JWT_SECRET="${JWT_SECRET:-test_secret_key_for_share_token_32b}"
VALID_ACCOUNT="${VALID_ACCOUNT:-admin}"
VALID_PASS="${VALID_PASS:-Admin123}"
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/auth-flow-server.log}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"

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

redis_delete_pattern() {
    local pattern="$1"

    python3 - "$REDIS_HOST" "$REDIS_PORT" "$pattern" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
pattern = sys.argv[3]
parts = ["KEYS", pattern]
payload = "*{}\r\n".format(len(parts))
for part in parts:
    payload += "${}\r\n{}\r\n".format(len(part.encode()), part)

with socket.create_connection((host, port), timeout=5) as sock:
    sock.sendall(payload.encode())
    reply = sock.recv(4096).decode(errors="ignore")

keys = []
if reply.startswith("*"):
    lines = reply.split("\r\n")
    i = 1
    while i < len(lines):
        if lines[i].startswith("$"):
            length = int(lines[i][1:])
            key = lines[i + 1] if i + 1 < len(lines) else ""
            if len(key.encode()) == length:
                keys.append(key)
            i += 2
        else:
            i += 1

if keys:
    del_parts = ["DEL"] + keys
    del_payload = "*{}\r\n".format(len(del_parts))
    for part in del_parts:
        del_payload += "${}\r\n{}\r\n".format(len(part.encode()), part)
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(del_payload.encode())
        sock.recv(1024)
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

send_login_request() {
    local account="$1"
    local password="$2"
    local body
    body=$(python3 - "$account" "$password" <<'PY'
import json
import sys
print(json.dumps({"account": sys.argv[1], "password": sys.argv[2]}))
PY
)

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "$body")

    LOGIN_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    LOGIN_BODY=$(printf '%s\n' "$response" | sed '$d')
}

# Test 1: Login with valid credentials returns 200 with tokens
test_login_success() {
    log_info "Testing login with valid credentials..."

    send_login_request "$VALID_ACCOUNT" "$VALID_PASS"

    local access_token refresh_token code
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")
    refresh_token=$(json_field "$LOGIN_BODY" "data.refresh_token")
    code=$(json_field "$LOGIN_BODY" "code")

    if [ "$LOGIN_HTTP_CODE" = "200" ] && [ "$code" = "0" ] && \
       [ -n "$access_token" ] && [ "$access_token" != "null" ] && \
       [ -n "$refresh_token" ] && [ "$refresh_token" != "null" ]; then
        log_pass "Login success: HTTP 200, code=0, both tokens returned"
    else
        log_fail "Login success: expected HTTP 200 + code 0 + tokens, got HTTP $LOGIN_HTTP_CODE code=$code"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi
}

# Test 2: Login with wrong password returns error
test_login_wrong_password() {
    log_info "Testing login with wrong password..."

    send_login_request "$VALID_ACCOUNT" "WrongPassword123"

    local code
    code=$(json_field "$LOGIN_BODY" "code")

    # Wrong password should return non-zero code (40100 for not found or similar auth error)
    if [ "$LOGIN_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Login wrong password: HTTP $LOGIN_HTTP_CODE, code=$code (rejected as expected)"
    else
        log_fail "Login wrong password: expected failure but got success"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi
}

# Test 3: Login with nonexistent user returns error
test_login_nonexistent_user() {
    log_info "Testing login with nonexistent user..."

    send_login_request "nonexistent_user_xyz_12345" "SomePassword123"

    local code
    code=$(json_field "$LOGIN_BODY" "code")

    if [ "$LOGIN_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Login nonexistent user: HTTP $LOGIN_HTTP_CODE, code=$code (rejected as expected)"
    else
        log_fail "Login nonexistent user: expected failure but got success"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi
}

# Test 4: Login with missing fields returns error
test_login_missing_fields() {
    log_info "Testing login with missing fields..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d '{"account":"admin"}')

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Login missing password: HTTP $http_code, code=$code (rejected as expected)"
    else
        log_fail "Login missing password: expected failure but got success"
        printf '%s\n' "$body"
        exit 1
    fi
}

# Test 5: Login with empty body returns error
test_login_empty_body() {
    log_info "Testing login with empty body..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d '{}')

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Login empty body: HTTP $http_code, code=$code (rejected as expected)"
    else
        log_fail "Login empty body: expected failure but got success"
        printf '%s\n' "$body"
        exit 1
    fi
}

# Test 6: Access token can authenticate protected endpoints
test_access_token_works() {
    log_info "Testing that access token authenticates protected endpoint..."

    send_login_request "$VALID_ACCOUNT" "$VALID_PASS"

    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Cannot get access token for auth test"
        exit 1
    fi

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/user/profile" \
        -H "Authorization: Bearer $access_token")

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    if [ "$http_code" = "200" ] && [ "$code" = "0" ]; then
        log_pass "Access token authenticates /api/user/profile"
    else
        log_fail "Access token failed to authenticate: HTTP $http_code, code=$code"
        printf '%s\n' "$body"
        exit 1
    fi
}

# Test 7: Invalid token is rejected
test_invalid_token_rejected() {
    log_info "Testing that invalid token is rejected..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/user/profile" \
        -H "Authorization: Bearer invalid.token.value")

    local http_code
    http_code=$(printf '%s\n' "$response" | tail -n 1)

    if [ "$http_code" = "401" ]; then
        log_pass "Invalid token rejected with 401"
    else
        log_fail "Invalid token: expected 401, got HTTP $http_code"
        printf '%s\n' "$response"
        exit 1
    fi
}

main() {
    printf '==========================================\n'
    printf 'Auth Flow Integration Tests\n'
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

    redis_delete_pattern "rate:*"

    test_login_success
    test_login_wrong_password
    test_login_nonexistent_user
    test_login_missing_fields
    test_login_empty_body
    test_access_token_works
    test_invalid_token_rejected

    redis_delete_pattern "rate:*"

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

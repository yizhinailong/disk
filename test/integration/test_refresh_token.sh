#!/bin/bash
#
# test/integration/test_refresh_token.sh
# Integration tests for refresh token rotation
#
# Verifies:
#   1. Login returns both access_token and refresh_token
#   2. Refresh token can be exchanged for new token pair
#   3. Old refresh_token is invalidated after rotation (single-use)
#   4. Invalid refresh_token is rejected
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
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/refresh-token-server.log}"
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

do_login() {
    local body
    body=$(python3 - "$VALID_ACCOUNT" "$VALID_PASS" <<'PY'
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

do_refresh() {
    local refresh_token="$1"
    local body
    body=$(python3 - "$refresh_token" <<'PY'
import json
import sys
print(json.dumps({"refresh_token": sys.argv[1]}))
PY
)

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/refresh" \
        -H "Content-Type: application/json" \
        -d "$body")

    REFRESH_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    REFRESH_BODY=$(printf '%s\n' "$response" | sed '$d')
}

# Test 1: Login returns both access_token and refresh_token
test_login_returns_both_tokens() {
    log_info "Testing login returns both tokens..."

    do_login

    local access_token refresh_token code
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")
    refresh_token=$(json_field "$LOGIN_BODY" "data.refresh_token")
    code=$(json_field "$LOGIN_BODY" "code")

    if [ "$LOGIN_HTTP_CODE" = "200" ] && [ "$code" = "0" ] && \
       [ -n "$access_token" ] && [ "$access_token" != "null" ] && \
       [ -n "$refresh_token" ] && [ "$refresh_token" != "null" ]; then
        log_pass "Login returns both access_token and refresh_token"
    else
        log_fail "Login did not return both tokens"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi
}

# Test 2: Refresh token can get new token pair
test_refresh_gets_new_tokens() {
    log_info "Testing refresh token exchange..."

    do_login

    local refresh_token
    refresh_token=$(json_field "$LOGIN_BODY" "data.refresh_token")

    if [ -z "$refresh_token" ] || [ "$refresh_token" = "null" ]; then
        log_fail "No refresh_token from login"
        exit 1
    fi

    do_refresh "$refresh_token"

    local new_access new_refresh code
    new_access=$(json_field "$REFRESH_BODY" "data.access_token")
    new_refresh=$(json_field "$REFRESH_BODY" "data.refresh_token")
    code=$(json_field "$REFRESH_BODY" "code")

    if [ "$REFRESH_HTTP_CODE" = "200" ] && [ "$code" = "0" ] && \
       [ -n "$new_access" ] && [ "$new_access" != "null" ] && \
       [ -n "$new_refresh" ] && [ "$new_refresh" != "null" ]; then
        log_pass "Refresh token exchange returns new token pair"
    else
        log_fail "Refresh token exchange failed: HTTP $REFRESH_HTTP_CODE, code=$code"
        printf '%s\n' "$REFRESH_BODY"
        exit 1
    fi
}

# Test 3: Old refresh_token is invalidated after rotation
test_old_refresh_token_invalidated() {
    log_info "Testing old refresh_token is invalidated after rotation..."

    do_login

    local old_refresh
    old_refresh=$(json_field "$LOGIN_BODY" "data.refresh_token")

    if [ -z "$old_refresh" ] || [ "$old_refresh" = "null" ]; then
        log_fail "No refresh_token from login"
        exit 1
    fi

    # First refresh — should succeed
    do_refresh "$old_refresh"

    local code
    code=$(json_field "$REFRESH_BODY" "code")

    if [ "$REFRESH_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_fail "First refresh should succeed but failed"
        printf '%s\n' "$REFRESH_BODY"
        exit 1
    fi

    # Second refresh with same old token — should fail (rotation)
    do_refresh "$old_refresh"

    code=$(json_field "$REFRESH_BODY" "code")

    if [ "$REFRESH_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Old refresh_token correctly rejected after rotation"
    else
        log_fail "Old refresh_token was accepted again (rotation broken!)"
        printf '%s\n' "$REFRESH_BODY"
        exit 1
    fi
}

# Test 4: New refresh_token from rotation works
test_new_refresh_token_works() {
    log_info "Testing new refresh_token from rotation works..."

    do_login

    local old_refresh
    old_refresh=$(json_field "$LOGIN_BODY" "data.refresh_token")

    # First refresh to get new token
    do_refresh "$old_refresh"

    local new_refresh
    new_refresh=$(json_field "$REFRESH_BODY" "data.refresh_token")

    if [ -z "$new_refresh" ] || [ "$new_refresh" = "null" ]; then
        log_fail "No new refresh_token from first rotation"
        exit 1
    fi

    # Use the new refresh token — should succeed
    do_refresh "$new_refresh"

    local code
    code=$(json_field "$REFRESH_BODY" "code")

    if [ "$REFRESH_HTTP_CODE" = "200" ] && [ "$code" = "0" ]; then
        log_pass "New refresh_token from rotation works correctly"
    else
        log_fail "New refresh_token from rotation failed: HTTP $REFRESH_HTTP_CODE, code=$code"
        printf '%s\n' "$REFRESH_BODY"
        exit 1
    fi
}

# Test 5: Invalid refresh_token is rejected
test_invalid_refresh_token_rejected() {
    log_info "Testing invalid refresh_token is rejected..."

    do_refresh "invalid.refresh.token.value.that.does.not.exist"

    local code
    code=$(json_field "$REFRESH_BODY" "code")

    if [ "$REFRESH_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Invalid refresh_token rejected: HTTP $REFRESH_HTTP_CODE, code=$code"
    else
        log_fail "Invalid refresh_token was accepted"
        printf '%s\n' "$REFRESH_BODY"
        exit 1
    fi
}

main() {
    printf '==========================================\n'
    printf 'Refresh Token Rotation Integration Tests\n'
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

    test_login_returns_both_tokens
    test_refresh_gets_new_tokens
    test_old_refresh_token_invalidated
    test_new_refresh_token_works
    test_invalid_refresh_token_rejected

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

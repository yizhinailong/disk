#!/bin/bash

set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
SERVER_BIN="${SERVER_BIN:-./build/linux-debug-clang/src/disk}"
JWT_SECRET="${JWT_SECRET:-test_secret_key_for_share_token_32b}"
VALID_ACCOUNT="${VALID_ACCOUNT:-admin}"
VALID_PASS="${VALID_PASS:-Admin123}"
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/login-rate-limit-server.log}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"
LOGIN_RATE_KEY="${LOGIN_RATE_KEY:-rate:login:127.0.0.1}"

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

login_body() {
    python3 - "$1" "$2" <<'PY'
import json
import sys

print(json.dumps({"account": sys.argv[1], "password": sys.argv[2]}))
PY
}

redis_delete_key() {
    local key="$1"

    python3 - "$REDIS_HOST" "$REDIS_PORT" "$key" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
key = sys.argv[3]
parts = ["DEL", key]
payload = "*{}\r\n".format(len(parts))
for part in parts:
    payload += "${}\r\n{}\r\n".format(len(part.encode()), part)

with socket.create_connection((host, port), timeout=5) as sock:
    sock.sendall(payload.encode())
    reply = sock.recv(1024).decode(errors="ignore")

if not reply.startswith(":"):
    raise SystemExit("Unexpected Redis reply: {}".format(reply.strip()))
PY
}

reset_rate_limit_counter() {
    redis_delete_key "$LOGIN_RATE_KEY"
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
    body=$(login_body "$account" "$password")

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "$body")

    RESPONSE_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    RESPONSE_BODY=$(printf '%s\n' "$response" | sed '$d')
    RESPONSE_CODE=$(json_field "$RESPONSE_BODY" "code")
    RESPONSE_MESSAGE=$(json_field "$RESPONSE_BODY" "message")
}

assert_user_not_found() {
    local context="$1"
    if [ "$RESPONSE_HTTP_CODE" = "404" ] && [ "$RESPONSE_CODE" = "40100" ]; then
        log_pass "$context"
    else
        log_fail "$context (expected 404/40100, got HTTP $RESPONSE_HTTP_CODE code $RESPONSE_CODE)"
        printf '%s\n' "$RESPONSE_BODY"
        exit 1
    fi
}

test_below_threshold_allows_first_five() {
    local account="rate_limit_missing_user_below"

    reset_rate_limit_counter

    for attempt in $(seq 1 5); do
        send_login_request "$account" "WrongPass123"
        assert_user_not_found "前 5 次尝试允许通过（第 ${attempt} 次）"
    done
}

test_above_threshold_blocks_sixth() {
    local account="rate_limit_missing_user_blocked"

    reset_rate_limit_counter

    for _ in $(seq 1 5); do
        send_login_request "$account" "WrongPass123"
        assert_user_not_found "达到阈值前仍返回业务错误"
    done

    send_login_request "$account" "WrongPass123"

    if [ "$RESPONSE_HTTP_CODE" = "429" ] && [ "$RESPONSE_CODE" = "10005" ] && [ "$RESPONSE_MESSAGE" = "Too many login attempts, please try again in 5 minutes" ]; then
        log_pass "第 6 次尝试返回相同 429 行为"
    else
        log_fail "第 6 次尝试未返回预期 429 行为"
        printf '%s\n' "$RESPONSE_BODY"
        exit 1
    fi
}

test_success_clears_counter() {
    local account="rate_limit_missing_user_reset"

    reset_rate_limit_counter

    for _ in $(seq 1 3); do
        send_login_request "$account" "WrongPass123"
        assert_user_not_found "成功登录前的失败计数可累加"
    done

    send_login_request "$VALID_ACCOUNT" "$VALID_PASS"
    local access_token
    access_token=$(json_field "$RESPONSE_BODY" "data.access_token")
    if [ "$RESPONSE_HTTP_CODE" = "200" ] && [ -n "$access_token" ]; then
        log_pass "成功登录清除频率限制计数器"
    else
        log_fail "成功登录未返回访问令牌"
        printf '%s\n' "$RESPONSE_BODY"
        exit 1
    fi

    send_login_request "$account" "WrongPass123"
    assert_user_not_found "成功登录后立即再次失败不会被 429 阻断"
}

main() {
    printf '==========================================\n'
    printf 'Login Rate Limit Integration Test\n'
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

    test_below_threshold_allows_first_five
    test_above_threshold_blocks_sixth
    test_success_clears_counter
    reset_rate_limit_counter

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

#!/bin/bash

set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
SERVER_BIN="${SERVER_BIN:-./build/linux-debug-clang/src/disk}"
JWT_SECRET="${JWT_SECRET:-test_secret_key_for_share_token_32b}"
VALID_ACCOUNT="${VALID_ACCOUNT:-admin}"
VALID_PASS="${VALID_PASS:-Admin123}"
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/user-profile-storage-server.log}"
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

send_authed_request() {
    local method="$1"
    local path="$2"
    local token="$3"

    local response
    response=$(curl -sS -w "\n%{http_code}" -X "$method" "$BASE_URL$path" \
        -H "Authorization: Bearer $token")

    RESP_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    RESP_BODY=$(printf '%s\n' "$response" | sed '$d')
    RESP_CODE=$(json_field "$RESP_BODY" "code")
}

test_get_profile_with_valid_token() {
    send_login_request "$VALID_ACCOUNT" "$VALID_PASS"
    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ "$LOGIN_HTTP_CODE" != "200" ] || [ -z "$access_token" ]; then
        log_fail "Login failed: HTTP $LOGIN_HTTP_CODE"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi

    send_authed_request "GET" "/api/user/profile" "$access_token"

    if [ "$RESP_HTTP_CODE" != "200" ] || [ "$RESP_CODE" != "0" ]; then
        log_fail "GET /api/user/profile 失败: HTTP $RESP_HTTP_CODE, code=$RESP_CODE"
        printf '%s\n' "$RESP_BODY"
        exit 1
    fi

    local id username email file_count folder_count storage_quota storage_used
    id=$(json_field "$RESP_BODY" "data.user.id")
    username=$(json_field "$RESP_BODY" "data.user.username")
    email=$(json_field "$RESP_BODY" "data.user.email")
    file_count=$(json_field "$RESP_BODY" "data.user.file_count")
    folder_count=$(json_field "$RESP_BODY" "data.user.folder_count")
    storage_quota=$(json_field "$RESP_BODY" "data.user.storage_quota")
    storage_used=$(json_field "$RESP_BODY" "data.user.storage_used")
    local nickname avatar created_at updated_at
    nickname=$(json_field "$RESP_BODY" "data.user.nickname")
    avatar=$(json_field "$RESP_BODY" "data.user.avatar")
    created_at=$(json_field "$RESP_BODY" "data.user.created_at")
    updated_at=$(json_field "$RESP_BODY" "data.user.updated_at")

    if [ -n "$id" ] && [ -n "$username" ] && [ -n "$email" ] && \
       [ -n "$file_count" ] && [ -n "$folder_count" ] && \
       [ -n "$storage_quota" ] && [ -n "$storage_used" ] && \
       [ -n "$created_at" ] && [ -n "$updated_at" ]; then
        log_pass "GET /api/user/profile 返回所有预期字段 (id=$id, username=$username, files=$file_count, folders=$folder_count)"
    else
        log_fail "GET /api/user/profile 缺少字段: id=$id, username=$username, email=$email, file_count=$file_count, folder_count=$folder_count, storage_quota=$storage_quota, storage_used=$storage_used, created_at=$created_at, updated_at=$updated_at"
        printf '%s\n' "$RESP_BODY"
        exit 1
    fi
}

test_get_storage_with_valid_token() {
    send_login_request "$VALID_ACCOUNT" "$VALID_PASS"
    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ "$LOGIN_HTTP_CODE" != "200" ] || [ -z "$access_token" ]; then
        log_fail "Login failed: HTTP $LOGIN_HTTP_CODE"
        exit 1
    fi

    send_authed_request "GET" "/api/user/storage" "$access_token"

    if [ "$RESP_HTTP_CODE" != "200" ] || [ "$RESP_CODE" != "0" ]; then
        log_fail "GET /api/user/storage 失败: HTTP $RESP_HTTP_CODE, code=$RESP_CODE"
        printf '%s\n' "$RESP_BODY"
        exit 1
    fi

    local used quota percentage file_count folder_count
    used=$(json_field "$RESP_BODY" "data.used")
    quota=$(json_field "$RESP_BODY" "data.quota")
    percentage=$(json_field "$RESP_BODY" "data.percentage")
    file_count=$(json_field "$RESP_BODY" "data.file_count")
    folder_count=$(json_field "$RESP_BODY" "data.folder_count")

    if [ -n "$used" ] && [ -n "$quota" ] && [ -n "$percentage" ] && \
       [ -n "$file_count" ] && [ -n "$folder_count" ]; then
        log_pass "GET /api/user/storage 返回所有预期字段 (used=$used, quota=$quota, percentage=$percentage%, files=$file_count, folders=$folder_count)"
    else
        log_fail "GET /api/user/storage 缺少字段: used=$used, quota=$quota, percentage=$percentage, file_count=$file_count, folder_count=$folder_count"
        printf '%s\n' "$RESP_BODY"
        exit 1
    fi
}

test_get_profile_without_token() {
    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/user/profile")

    RESP_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    RESP_BODY=$(printf '%s\n' "$response" | sed '$d')
    RESP_CODE=$(json_field "$RESP_BODY" "code")

    if [ "$RESP_HTTP_CODE" = "401" ]; then
        log_pass "GET /api/user/profile 无令牌返回 401"
    else
        log_fail "GET /api/user/profile 无令牌期望 401，实际 HTTP $RESP_HTTP_CODE"
        printf '%s\n' "$RESP_BODY"
        exit 1
    fi
}

test_get_storage_with_malformed_token() {
    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/user/storage" \
        -H "Authorization: Bearer not.a.valid.jwt.token")

    RESP_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    RESP_BODY=$(printf '%s\n' "$response" | sed '$d')

    if [ "$RESP_HTTP_CODE" = "401" ]; then
        log_pass "GET /api/user/storage 畸形令牌返回 401"
    else
        log_fail "GET /api/user/storage 畸形令牌期望 401，实际 HTTP $RESP_HTTP_CODE"
        printf '%s\n' "$RESP_BODY"
        exit 1
    fi
}

main() {
    printf '==========================================\n'
    printf 'User Profile/Storage Integration Test\n'
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

    test_get_profile_with_valid_token
    test_get_storage_with_valid_token
    test_get_profile_without_token
    test_get_storage_with_malformed_token

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

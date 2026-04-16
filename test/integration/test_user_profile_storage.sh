#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"
source "$SCRIPT_DIR/lib/http.sh"
source "$SCRIPT_DIR/lib/auth.sh"

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
SERVER_BIN="${SERVER_BIN:-./build/linux-debug-clang/src/disk}"
JWT_SECRET="${JWT_SECRET:-test_secret_key_for_share_token_32b}"
VALID_ACCOUNT="${VALID_ACCOUNT:-admin}"
VALID_PASS="${VALID_PASS:-Admin123}"
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/user-profile-storage-server.log}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"

trap cleanup EXIT

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
    do_login
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
    do_login
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

    print_summary
}

main "$@"

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
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/login-rate-limit-server.log}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"
LOGIN_RATE_KEY="${LOGIN_RATE_KEY:-rate:login:127.0.0.1}"

trap cleanup EXIT

reset_rate_limit_counter() {
    redis_delete_key "$LOGIN_RATE_KEY"
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

send_login_request_with_response() {
    local account="$1"
    local password="$2"
    send_login_request "$account" "$password"
    RESPONSE_HTTP_CODE="$LOGIN_HTTP_CODE"
    RESPONSE_BODY="$LOGIN_BODY"
    RESPONSE_CODE=$(json_field "$RESPONSE_BODY" "code")
    RESPONSE_MESSAGE=$(json_field "$RESPONSE_BODY" "message")
}

test_below_threshold_allows_first_five() {
    local account="rate_limit_missing_user_below"

    reset_rate_limit_counter

    for attempt in $(seq 1 5); do
        send_login_request_with_response "$account" "WrongPass123"
        assert_user_not_found "前 5 次尝试允许通过（第 ${attempt} 次）"
    done
}

test_above_threshold_blocks_sixth() {
    local account="rate_limit_missing_user_blocked"

    reset_rate_limit_counter

    for _ in $(seq 1 5); do
        send_login_request_with_response "$account" "WrongPass123"
        assert_user_not_found "达到阈值前仍返回业务错误"
    done

    send_login_request_with_response "$account" "WrongPass123"

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
        send_login_request_with_response "$account" "WrongPass123"
        assert_user_not_found "成功登录前的失败计数可累加"
    done

    send_login_request_with_response "$VALID_ACCOUNT" "$VALID_PASS"
    local access_token
    access_token=$(json_field "$RESPONSE_BODY" "data.access_token")
    if [ "$RESPONSE_HTTP_CODE" = "200" ] && [ -n "$access_token" ]; then
        log_pass "成功登录清除频率限制计数器"
    else
        log_fail "成功登录未返回访问令牌"
        printf '%s\n' "$RESPONSE_BODY"
        exit 1
    fi

    send_login_request_with_response "$account" "WrongPass123"
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

    print_summary
}

main "$@"

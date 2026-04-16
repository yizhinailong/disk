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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"
source "$SCRIPT_DIR/lib/http.sh"
source "$SCRIPT_DIR/lib/auth.sh"

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
SERVER_BIN="${SERVER_BIN:-./build/linux-debug-clang/src/disk}"
JWT_SECRET="${JWT_SECRET:-test_secret_key_for_share_token_32b}"
VALID_ACCOUNT="${VALID_ACCOUNT:-admin}"
VALID_PASS="${VALID_PASS:-Admin123}"
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/auth-flow-server.log}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"

trap cleanup EXIT

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

    print_summary
}

main "$@"

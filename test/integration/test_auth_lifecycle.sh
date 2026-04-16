#!/bin/bash
#
# test/integration/test_auth_lifecycle.sh
# Integration tests for auth lifecycle: register and logout
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured
#   - Redis configured
#
# Usage:
#   ./test/integration/test_auth_lifecycle.sh
#
# Environment variables:
#   BASE_URL    - Server URL (default: http://127.0.0.1:8080)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"
source "$SCRIPT_DIR/lib/http.sh"
source "$SCRIPT_DIR/lib/auth.sh"

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
EVIDENCE_DIR="${EVIDENCE_DIR:-.sisyphus/evidence}"

# Generate unique username/email with timestamp + PID
TIMESTAMP_PID="$(date +%s)_$$"
TEST_USERNAME="testuser_${TIMESTAMP_PID}"
TEST_EMAIL="testuser_${TIMESTAMP_PID}@test.example.com"
TEST_PASSWORD="TestPass123"

# Test 1: Register new user
test_register_new_user() {
    log_info "Testing registration with new unique user..."

    local body
    body=$(python3 - "$TEST_USERNAME" "$TEST_EMAIL" "$TEST_PASSWORD" <<'PY'
import json
import sys
print(json.dumps({"username": sys.argv[1], "email": sys.argv[2], "password": sys.argv[3]}))
PY
    )

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/register" \
        -H "Content-Type: application/json" \
        -d "$body")

    local http_code body code user_id user_id_field username_field
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")
    user_id_field=$(json_field "$body" "data.user.id")
    username_field=$(json_field "$body" "data.user.username")

    save_evidence "register_new_user_response.json" "$body"

    if [ "$http_code" = "200" ] && [ "$code" = "0" ] && \
       [ -n "$user_id_field" ] && [ "$user_id_field" != "null" ] && \
       [ "$username_field" = "$TEST_USERNAME" ]; then
        log_pass "Register new user: HTTP 200, code=0, user.id=$user_id_field, username matches"
    else
        log_fail "Register new user: expected HTTP 200 + code 0 + user data, got HTTP $http_code code=$code"
        printf '%s\n' "$body"
        exit 1
    fi
}

# Test 2: Login with new user
test_login_new_user() {
    log_info "Testing login with newly registered user..."

    send_login_request "$TEST_USERNAME" "$TEST_PASSWORD"

    local access_token code
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")
    code=$(json_field "$LOGIN_BODY" "code")

    save_evidence "login_new_user_response.json" "$LOGIN_BODY"

    if [ "$LOGIN_HTTP_CODE" = "200" ] && [ "$code" = "0" ] && \
       [ -n "$access_token" ] && [ "$access_token" != "null" ]; then
        log_pass "Login new user: HTTP 200, code=0, access_token present"
    else
        log_fail "Login new user: expected HTTP 200 + code 0 + token, got HTTP $LOGIN_HTTP_CODE code=$code"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi
}

# Test 3: Duplicate registration rejected
test_duplicate_registration_rejected() {
    log_info "Testing duplicate registration is rejected..."

    local body
    body=$(python3 - "$TEST_USERNAME" "$TEST_EMAIL" "$TEST_PASSWORD" <<'PY'
import json
import sys
print(json.dumps({"username": sys.argv[1], "email": sys.argv[2], "password": sys.argv[3]}))
PY
    )

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/register" \
        -H "Content-Type: application/json" \
        -d "$body")

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "duplicate_registration_response.json" "$body"

    # Duplicate registration should return non-zero code (or non-200 HTTP)
    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Duplicate registration: HTTP $http_code, code=$code (rejected as expected)"
    else
        log_fail "Duplicate registration: expected failure but got success"
        printf '%s\n' "$body"
        exit 1
    fi
}

# Test 4: Logout
test_logout() {
    log_info "Testing logout with valid token..."

    # First login to get a fresh token
    send_login_request "$TEST_USERNAME" "$TEST_PASSWORD"
    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Cannot get access token for logout test"
        exit 1
    fi

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/logout" \
        -H "Authorization: Bearer $access_token")

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "logout_response.json" "$body"

    if [ "$http_code" = "200" ] && [ "$code" = "0" ]; then
        log_pass "Logout: HTTP 200, code=0"
    else
        log_fail "Logout: expected HTTP 200 + code 0, got HTTP $http_code code=$code"
        printf '%s\n' "$body"
        exit 1
    fi
}

# Test 5: Token invalidation after logout
test_token_invalidated_after_logout() {
    log_info "Testing that token is invalidated after logout..."

    # Login to get a token
    send_login_request "$TEST_USERNAME" "$TEST_PASSWORD"
    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Cannot get access token for token invalidation test"
        exit 1
    fi

    # Logout to invalidate the token
    curl -sS -X POST "$BASE_URL/api/auth/logout" \
        -H "Authorization: Bearer $access_token" > /dev/null

    # Try to use the token - should get 401
    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/user/profile" \
        -H "Authorization: Bearer $access_token")

    local http_code
    http_code=$(printf '%s\n' "$response" | tail -n 1)

    if [ "$http_code" = "401" ]; then
        log_pass "Token invalidation: old token returns 401 after logout"
    else
        log_fail "Token invalidation: expected 401, got HTTP $http_code"
        printf '%s\n' "$response"
        exit 1
    fi
}

main() {
    printf '==========================================\n'
    printf 'Auth Lifecycle Integration Tests\n'
    printf '==========================================\n\n'

    if ! command -v curl >/dev/null 2>&1; then
        log_fail "curl is required"
        exit 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        log_fail "python3 is required"
        exit 1
    fi

    # Use check_server (not ensure_server) - server lifecycle managed by other scripts
    check_server

    log_section "Test User Credentials"
    log_info "Username: $TEST_USERNAME"
    log_info "Email: $TEST_EMAIL"
    log_info "Password: $TEST_PASSWORD"
    echo ""

    test_register_new_user
    test_login_new_user
    test_duplicate_registration_rejected
    test_logout
    test_token_invalidated_after_logout

    print_summary
}

main "$@"

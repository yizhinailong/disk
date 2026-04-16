#!/bin/bash
#
# test/integration/test_password_update.sh
# Integration tests for password update flow
#
# Verifies:
#   1. Change password with correct old password succeeds
#   2. Old password no longer works after change
#   3. New password works after change
#   4. Change password with wrong old password fails
#   5. Restore original password at end
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured with seed data
#   - Redis configured
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
NEW_PASS="${NEW_PASS:-TestNewPass456}"
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/password-update-server.log}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"

trap cleanup EXIT

do_change_password() {
    local token="$1"
    local old_pw="$2"
    local new_pw="$3"
    local body
    body=$(python3 - "$old_pw" "$new_pw" <<'PY'
import json
import sys
print(json.dumps({"old_password": sys.argv[1], "new_password": sys.argv[2]}))
PY
)

    local response
    response=$(curl -sS -w "\n%{http_code}" -X PUT "$BASE_URL/api/user/password" \
        -H "Authorization: Bearer $token" \
        -H "Content-Type: application/json" \
        -d "$body")

    CHANGE_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    CHANGE_BODY=$(printf '%s\n' "$response" | sed '$d')
}

# Test 1: Change password with correct old password
test_change_password_success() {
    log_info "Testing password change with correct old password..."

    do_login "$VALID_ACCOUNT" "$VALID_PASS"

    local access_token code
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Login failed for password change test"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi

    do_change_password "$access_token" "$VALID_PASS" "$NEW_PASS"

    code=$(json_field "$CHANGE_BODY" "code")

    if [ "$CHANGE_HTTP_CODE" = "200" ] && [ "$code" = "0" ]; then
        log_pass "Password change with correct old password succeeds"
    else
        log_fail "Password change failed: HTTP $CHANGE_HTTP_CODE, code=$code"
        printf '%s\n' "$CHANGE_BODY"
        exit 1
    fi
}

# Test 2: Old password no longer works after change
test_old_password_fails() {
    log_info "Testing old password fails after change..."

    do_login "$VALID_ACCOUNT" "$VALID_PASS"

    local code
    code=$(json_field "$LOGIN_BODY" "code")

    if [ "$LOGIN_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Old password correctly rejected after change"
    else
        log_fail "Old password still works after change!"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi
}

# Test 3: New password works after change
test_new_password_works() {
    log_info "Testing new password works after change..."

    do_login "$VALID_ACCOUNT" "$NEW_PASS"

    local access_token code
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")
    code=$(json_field "$LOGIN_BODY" "code")

    if [ "$LOGIN_HTTP_CODE" = "200" ] && [ "$code" = "0" ] && \
       [ -n "$access_token" ] && [ "$access_token" != "null" ]; then
        log_pass "New password works for login"
    else
        log_fail "New password login failed: HTTP $LOGIN_HTTP_CODE, code=$code"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi
}

# Test 4: Change password with wrong old password fails
test_change_wrong_old_password() {
    log_info "Testing password change with wrong old password..."

    # Login with new password
    do_login "$VALID_ACCOUNT" "$NEW_PASS"

    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Login with new password failed"
        exit 1
    fi

    # Try to change with wrong old password
    do_change_password "$access_token" "WrongOldPass999" "$VALID_PASS"

    local code
    code=$(json_field "$CHANGE_BODY" "code")

    if [ "$CHANGE_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Password change with wrong old password rejected"
    else
        log_fail "Password change with wrong old password succeeded!"
        printf '%s\n' "$CHANGE_BODY"
        exit 1
    fi
}

# Cleanup: Restore original password
test_restore_password() {
    log_info "Restoring original password..."

    do_login "$VALID_ACCOUNT" "$NEW_PASS"

    local access_token code
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Cannot login to restore password"
        exit 1
    fi

    do_change_password "$access_token" "$NEW_PASS" "$VALID_PASS"

    code=$(json_field "$CHANGE_BODY" "code")

    if [ "$CHANGE_HTTP_CODE" = "200" ] && [ "$code" = "0" ]; then
        log_pass "Original password restored"
    else
        log_fail "Failed to restore original password: HTTP $CHANGE_HTTP_CODE, code=$code"
        printf '%s\n' "$CHANGE_BODY"
        # Don't exit here — this is cleanup
    fi
}

main() {
    printf '==========================================\n'
    printf 'Password Update Integration Tests\n'
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

    test_change_password_success
    test_old_password_fails
    test_new_password_works
    test_change_wrong_old_password
    test_restore_password

    redis_delete_pattern "rate:*"

    print_summary
}

main "$@"

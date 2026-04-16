#!/bin/bash
#
# test/integration/test_user_profile_update.sh
# Integration tests for user profile update flow
#
# Verifies:
#   1. Get current profile and save original nickname
#   2. Update nickname with unique value succeeds
#   3. Verify nickname persistence via GET
#   4. Invalid payload (empty body) is rejected
#   5. Restore original nickname at end
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
VALID_ACCOUNT="${VALID_ACCOUNT:-admin}"
VALID_PASS="${VALID_PASS:-Admin123}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"

# Global variables for test data
ORIGINAL_NICKNAME=""
NEW_NICKNAME=""

do_get_profile() {
    local token="$1"
    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/user/profile" \
        -H "Authorization: Bearer $token")

    PROFILE_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    PROFILE_BODY=$(printf '%s\n' "$response" | sed '$d')
}

do_update_profile() {
    local token="$1"
    local nickname="$2"
    local body
    body=$(python3 - "$nickname" <<'PY'
import json
import sys
print(json.dumps({"nickname": sys.argv[1]}))
PY
)

    local response
    response=$(curl -sS -w "\n%{http_code}" -X PATCH "$BASE_URL/api/user/profile" \
        -H "Authorization: Bearer $token" \
        -H "Content-Type: application/json" \
        -d "$body")

    UPDATE_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    UPDATE_BODY=$(printf '%s\n' "$response" | sed '$d')
}

# Test 1: Get current profile and save original nickname
test_get_current_profile() {
    log_info "Getting current user profile..."

    do_login "$VALID_ACCOUNT" "$VALID_PASS"

    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Login failed for profile test"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi

    do_get_profile "$access_token"

    local code
    code=$(json_field "$PROFILE_BODY" "code")

    if [ "$PROFILE_HTTP_CODE" = "200" ] && [ "$code" = "0" ]; then
        ORIGINAL_NICKNAME=$(json_field "$PROFILE_BODY" "data.user.nickname")
        log_pass "Retrieved current profile, original nickname: $ORIGINAL_NICKNAME"
        save_evidence "user-profile-original.json" "$PROFILE_BODY"
    else
        log_fail "Failed to get profile: HTTP $PROFILE_HTTP_CODE, code=$code"
        printf '%s\n' "$PROFILE_BODY"
        exit 1
    fi
}

# Test 2: Update nickname with unique value
test_update_nickname_success() {
    log_info "Testing nickname update with unique value..."

    do_login "$VALID_ACCOUNT" "$VALID_PASS"

    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Login failed for nickname update test"
        exit 1
    fi

    NEW_NICKNAME="TestNick$(date +%s)_$$"
    do_update_profile "$access_token" "$NEW_NICKNAME"

    local code
    code=$(json_field "$UPDATE_BODY" "code")

    if [ "$UPDATE_HTTP_CODE" = "200" ] && [ "$code" = "0" ]; then
        log_pass "Nickname updated successfully to: $NEW_NICKNAME"
        save_evidence "user-profile-update-response.json" "$UPDATE_BODY"
    else
        log_fail "Nickname update failed: HTTP $UPDATE_HTTP_CODE, code=$code"
        printf '%s\n' "$UPDATE_BODY"
        exit 1
    fi
}

# Test 3: Verify nickname persistence via GET
test_verify_nickname_persistence() {
    log_info "Verifying nickname persistence..."

    do_login "$VALID_ACCOUNT" "$VALID_PASS"

    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Login failed for persistence verification"
        exit 1
    fi

    do_get_profile "$access_token"

    local code current_nickname
    code=$(json_field "$PROFILE_BODY" "code")
    current_nickname=$(json_field "$PROFILE_BODY" "data.user.nickname")

    if [ "$PROFILE_HTTP_CODE" = "200" ] && [ "$code" = "0" ] && [ "$current_nickname" = "$NEW_NICKNAME" ]; then
        log_pass "Nickname persisted correctly: $current_nickname"
        save_evidence "user-profile-persisted.json" "$PROFILE_BODY"
    else
        log_fail "Nickname persistence verification failed: HTTP $PROFILE_HTTP_CODE, code=$code, expected '$NEW_NICKNAME', got '$current_nickname'"
        printf '%s\n' "$PROFILE_BODY"
        exit 1
    fi
}

# Test 4: Invalid payload (empty body) should be rejected
test_invalid_empty_body() {
    log_info "Testing invalid payload (empty body)..."

    do_login "$VALID_ACCOUNT" "$VALID_PASS"

    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Login failed for invalid payload test"
        exit 1
    fi

    local body='{}'
    local response
    response=$(curl -sS -w "\n%{http_code}" -X PATCH "$BASE_URL/api/user/profile" \
        -H "Authorization: Bearer $access_token" \
        -H "Content-Type: application/json" \
        -d "$body")

    local http_code body_text
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body_text=$(printf '%s\n' "$response" | sed '$d')

    local code
    code=$(json_field "$body_text" "code")

    # API returns 400 + code 10001 for empty body
    if [ "$http_code" = "400" ] || [ "$code" != "0" ]; then
        log_pass "Empty body correctly rejected: HTTP $http_code, code=$code"
        save_evidence "user-profile-empty-body-rejection.json" "$body_text"
    else
        log_fail "Empty body should be rejected but succeeded: HTTP $http_code, code=$code"
        printf '%s\n' "$body_text"
        exit 1
    fi
}

# Test 5: Restore original nickname
test_restore_original_nickname() {
    log_info "Restoring original nickname..."

    do_login "$VALID_ACCOUNT" "$VALID_PASS"

    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Cannot login to restore nickname"
        exit 1
    fi

    if [ -z "$ORIGINAL_NICKNAME" ]; then
        log_fail "Original nickname not saved, cannot restore"
        exit 1
    fi

    do_update_profile "$access_token" "$ORIGINAL_NICKNAME"

    local code
    code=$(json_field "$UPDATE_BODY" "code")

    if [ "$UPDATE_HTTP_CODE" = "200" ] && [ "$code" = "0" ]; then
        log_pass "Original nickname restored: $ORIGINAL_NICKNAME"
        save_evidence "user-profile-restored.json" "$UPDATE_BODY"
    else
        log_fail "Failed to restore original nickname: HTTP $UPDATE_HTTP_CODE, code=$code"
        printf '%s\n' "$UPDATE_BODY"
        # Don't exit here - this is cleanup
    fi
}

main() {
    printf '==========================================\n'
    printf 'User Profile Update Integration Tests\n'
    printf '==========================================\n\n'

    if ! command -v curl >/dev/null 2>&1; then
        log_fail "curl is required"
        exit 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        log_fail "python3 is required"
        exit 1
    fi

    check_server

    redis_delete_pattern "rate:*"

    test_get_current_profile
    test_update_nickname_success
    test_verify_nickname_persistence
    test_invalid_empty_body
    test_restore_original_nickname

    redis_delete_pattern "rate:*"

    print_summary
}

main "$@"

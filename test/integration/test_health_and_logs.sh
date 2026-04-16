#!/bin/bash
#
# test/integration/test_health_and_logs.sh
# Integration tests for /api/health, /api/logs, and share-browse exemption.
#
# Verifies:
#   1. GET /api/health succeeds unauthenticated with valid payload
#   2. Health payload shape has expected fields and valid enum values
#   3. GET /api/logs requires authentication (401 without token)
#   4. GET /api/logs returns success with authentication (200 + code 0)
#   5. Share-browse exemption behavior documented (no config changes)
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
EVIDENCE_DIR="${EVIDENCE_DIR:-.sisyphus/evidence}"

# ─── Test 1: Health check unauthenticated ──────────────────────────────────────

test_health_unauthenticated() {
    log_info "Testing GET /api/health without authentication..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/health")

    local http_code resp_body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    resp_body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$resp_body" "code")

    # Health endpoint returns 200 when healthy, 503 when degraded/unhealthy
    if [ "$http_code" = "200" ] || [ "$http_code" = "503" ]; then
        log_pass "GET /api/health returns HTTP $http_code (acceptable)"
    else
        log_fail "GET /api/health: expected HTTP 200 or 503, got HTTP $http_code"
        printf '%s\n' "$resp_body"
        return 1
    fi

    if [ "$code" = "0" ]; then
        log_pass "Health response code=0"
    else
        log_fail "Health response: expected code=0, got code=$code"
        printf '%s\n' "$resp_body"
        return 1
    fi

    # Verify core fields exist
    local overall_status version components
    overall_status=$(json_field "$resp_body" "data.overall_status")
    version=$(json_field "$resp_body" "data.version")
    components=$(json_field "$resp_body" "data.components")

    if [ -n "$overall_status" ] && [ "$overall_status" != "null" ]; then
        log_pass "data.overall_status present: $overall_status"
    else
        log_fail "data.overall_status is missing or null"
        printf '%s\n' "$resp_body"
        return 1
    fi

    if [ -n "$version" ] && [ "$version" != "null" ]; then
        log_pass "data.version present: $version"
    else
        log_fail "data.version is missing or null"
        printf '%s\n' "$resp_body"
        return 1
    fi

    if [ -n "$components" ] && [ "$components" != "null" ]; then
        log_pass "data.components present"
    else
        log_fail "data.components is missing or null"
        printf '%s\n' "$resp_body"
        return 1
    fi

    # Save evidence
    mkdir -p "$EVIDENCE_DIR"
    printf '%s\n' "$resp_body" > "$EVIDENCE_DIR/health-response.json"
}

# ─── Test 2: Health payload shape validation ───────────────────────────────────

test_health_payload_shape() {
    log_info "Testing health payload shape (enum values)..."

    # Re-fetch to get a fresh response
    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/health")

    local http_code resp_body
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    resp_body=$(printf '%s\n' "$response" | sed '$d')

    # overall_status must be one of: healthy, degraded, unhealthy
    local overall_status
    overall_status=$(json_field "$resp_body" "data.overall_status")

    case "$overall_status" in
        healthy|degraded|unhealthy)
            log_pass "data.overall_status='$overall_status' is a valid enum value"
            ;;
        *)
            log_fail "data.overall_status='$overall_status' is NOT one of healthy/degraded/unhealthy"
            printf '%s\n' "$resp_body"
            return 1
            ;;
    esac

    # database status must be one of: healthy, unhealthy
    local db_status
    db_status=$(json_field "$resp_body" "data.components.database.status")

    case "$db_status" in
        healthy|unhealthy)
            log_pass "data.components.database.status='$db_status' is valid"
            ;;
        *)
            log_fail "data.components.database.status='$db_status' is NOT healthy/unhealthy"
            printf '%s\n' "$resp_body"
            return 1
            ;;
    esac

    # redis status must be one of: healthy, unhealthy
    local redis_status
    redis_status=$(json_field "$resp_body" "data.components.redis.status")

    case "$redis_status" in
        healthy|unhealthy)
            log_pass "data.components.redis.status='$redis_status' is valid"
            ;;
        *)
            log_fail "data.components.redis.status='$redis_status' is NOT healthy/unhealthy"
            printf '%s\n' "$resp_body"
            return 1
            ;;
    esac
}

# ─── Test 3: Logs without auth ─────────────────────────────────────────────────

test_logs_without_auth() {
    log_info "Testing GET /api/logs without authentication..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/logs")

    local http_code resp_body
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    resp_body=$(printf '%s\n' "$response" | sed '$d')

    if [ "$http_code" = "401" ]; then
        log_pass "GET /api/logs without token returns 401"
    else
        log_fail "GET /api/logs without token: expected 401, got HTTP $http_code"
        printf '%s\n' "$resp_body"
        return 1
    fi

    # Save evidence
    mkdir -p "$EVIDENCE_DIR"
    printf '%s\n' "$resp_body" > "$EVIDENCE_DIR/logs-no-auth-response.json"
}

# ─── Test 4: Logs with auth ────────────────────────────────────────────────────

test_logs_with_auth() {
    log_info "Testing GET /api/logs with authentication..."

    do_login

    local access_token
    access_token=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$access_token" ] || [ "$access_token" = "null" ]; then
        log_fail "Login failed for logs test"
        printf '%s\n' "$LOGIN_BODY"
        return 1
    fi

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/logs" \
        -H "Authorization: Bearer $access_token")

    local http_code resp_body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    resp_body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$resp_body" "code")

    if [ "$http_code" = "200" ] && [ "$code" = "0" ]; then
        log_pass "GET /api/logs with token returns 200 + code 0"
    else
        log_fail "GET /api/logs with token: expected HTTP 200 + code 0, got HTTP $http_code + code=$code"
        printf '%s\n' "$resp_body"
        return 1
    fi

    # Verify data.items is an array (may be empty)
    local items
    items=$(json_field "$resp_body" "data.items")

    if [ -n "$items" ] && [ "$items" != "null" ]; then
        log_pass "data.items is present (may be empty array)"
    else
        log_fail "data.items is missing or null"
        printf '%s\n' "$resp_body"
        return 1
    fi

    # Save evidence
    mkdir -p "$EVIDENCE_DIR"
    printf '%s\n' "$resp_body" > "$EVIDENCE_DIR/logs-with-auth-response.json"
}

# ─── Test 5: Share-browse exemption validation ─────────────────────────────────

test_share_browse_exemption() {
    log_info "Testing GET /api/share/browse/<id> without auth (exemption evidence)..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/share/browse/nonexistent_share_id")

    local http_code resp_body
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    resp_body=$(printf '%s\n' "$response" | sed '$d')

    # Save evidence regardless of outcome — we are documenting behavior
    mkdir -p "$EVIDENCE_DIR"
    printf 'HTTP Status: %s\n\n' "$http_code" > "$EVIDENCE_DIR/share-browse-exemption-evidence.txt"
    printf 'Response Body:\n%s\n' "$resp_body" >> "$EVIDENCE_DIR/share-browse-exemption-evidence.txt"
    printf '\nInterpretation:\n' >> "$EVIDENCE_DIR/share-browse-exemption-evidence.txt"

    if [ "$http_code" = "401" ]; then
        printf 'The endpoint /api/share/browse/{share_id} is NOT exempted from JWT auth.\n' >> "$EVIDENCE_DIR/share-browse-exemption-evidence.txt"
        printf 'It returned 401 without authentication, meaning the config.json exemption pattern\n' >> "$EVIDENCE_DIR/share-browse-exemption-evidence.txt"
        printf '"^/api/share-browse/.*" does NOT match the actual route "/api/share/browse/{share_id}".\n' >> "$EVIDENCE_DIR/share-browse-exemption-evidence.txt"
        log_pass "Share-browse endpoint: HTTP $http_code (requires auth — exemption pattern may not match)"
    else
        printf 'The endpoint /api/share/browse/{share_id} IS publicly accessible (HTTP %s).\n' "$http_code" >> "$EVIDENCE_DIR/share-browse-exemption-evidence.txt"
        printf 'It did NOT return 401, meaning the config.json exemption pattern likely matches.\n' >> "$EVIDENCE_DIR/share-browse-exemption-evidence.txt"
        log_pass "Share-browse endpoint: HTTP $http_code (publicly accessible — exemption active)"
    fi

    log_info "Evidence saved to $EVIDENCE_DIR/share-browse-exemption-evidence.txt"
}

# ─── Main ──────────────────────────────────────────────────────────────────────

main() {
    printf '==========================================\n'
    printf 'Health & Logs Integration Tests\n'
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

    test_health_unauthenticated
    test_health_payload_shape
    test_logs_without_auth
    test_logs_with_auth
    test_share_browse_exemption

    print_summary
}

main "$@"

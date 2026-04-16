#!/bin/bash
#
# test/integration/test_share_browse.sh
# Integration tests for share create + access + browse flow
#
# Verifies:
#   1. List shares returns 200 + code 0
#   2. Create share without auth returns 401
#   3. Upload fixture file, create share with file_ids → assert success
#   4. Access share → get share_token
#   5. Browse share with valid share_token → 200 + code 0
#   6. Browse share without share_token → error
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
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/share-browse-server.log}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"

EVIDENCE_PREFIX="share-browse"

# Shared state across tests
TOKEN=""
FILE_ID=""
CREATED_SHARE_ID=""
SHARE_TOKEN=""

trap cleanup EXIT

# ─── Fixture: upload a small file ─────────────────────────────────────────────

upload_fixture() {
    log_step "Uploading fixture file for share tests..."

    local fixture="/tmp/disk_share_browse_fixture_$$.bin"
    dd if=/dev/urandom of="$fixture" bs=256 count=1 2>/dev/null
    local file_size=256
    local file_hash
    file_hash=$(md5sum "$fixture" | cut -d' ' -f1)

    local init_resp
    init_resp=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"filename\":\"share_browse_fixture_$$.bin\",\"file_size\":$file_size,\"file_hash\":\"$file_hash\",\"parent_id\":0}")

    local instant
    instant=$(json_field "$init_resp" "data.instant_upload")

    if [ "$instant" = "true" ]; then
        FILE_ID=$(json_field "$init_resp" "data.file_id")
    else
        local upload_id
        upload_id=$(json_field "$init_resp" "data.upload_id")

        curl -s -X POST "$BASE_URL/api/file/upload/chunk?upload_id=$upload_id&chunk_index=0&chunk_hash=$file_hash" \
            -H "Authorization: Bearer $TOKEN" \
            -H "Content-Type: application/octet-stream" \
            --data-binary @"$fixture" > /dev/null

        local complete_resp
        complete_resp=$(curl -s -X POST "$BASE_URL/api/file/upload/complete" \
            -H "Authorization: Bearer $TOKEN" \
            -H "Content-Type: application/json" \
            -d "{\"upload_id\":\"$upload_id\"}")

        FILE_ID=$(json_field "$complete_resp" "data.file.id")
    fi
    rm -f "$fixture"

    if [ -z "$FILE_ID" ] || [ "$FILE_ID" = "null" ]; then
        log_fail "Fixture upload failed — no file_id"
        save_evidence "${EVIDENCE_PREFIX}-upload-init.json" "$init_resp"
        exit 1
    fi

    log_pass "Fixture uploaded — file_id=$FILE_ID"
}

# ─── Test 1: List shares ──────────────────────────────────────────────────────

test_list_shares() {
    log_info "Testing GET /api/share (list shares)..."

    do_login

    TOKEN=$(json_field "$LOGIN_BODY" "data.access_token")
    if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
        log_fail "Login failed for share test"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/share" \
        -H "Authorization: Bearer $TOKEN")

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    if [ "$http_code" = "200" ] && [ "$code" = "0" ]; then
        log_pass "GET /api/share returns 200 + code 0"
    else
        log_fail "GET /api/share failed: HTTP $http_code, code=$code"
        printf '%s\n' "$body"
        exit 1
    fi
}

# ─── Test 2: Create share requires auth ───────────────────────────────────────

test_share_requires_auth() {
    log_info "Testing share endpoints require authentication..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/share" \
        -H "Content-Type: application/json" \
        -d '{"file_ids":[1],"permission":"download"}')

    local http_code
    http_code=$(printf '%s\n' "$response" | tail -n 1)

    if [ "$http_code" = "401" ]; then
        log_pass "POST /api/share without token returns 401"
    else
        log_fail "POST /api/share without token: expected 401, got HTTP $http_code"
        printf '%s\n' "$response"
        exit 1
    fi
}

# ─── Test 3: Create share with fixture file ───────────────────────────────────

test_create_share() {
    log_info "Testing POST /api/share (create share)..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/share" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\":[$FILE_ID],\"permission\":\"download\",\"expire_days\":7}")

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-create.json" "$body"

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_fail "Create share failed: HTTP $http_code, code=$code"
        printf '%s\n' "$body"
        exit 1
    fi

    local share_id
    share_id=$(json_field "$body" "data.share_id")

    if [ -z "$share_id" ] || [ "$share_id" = "null" ]; then
        log_fail "Create share returned 200 but no share_id"
        printf '%s\n' "$body"
        exit 1
    fi

    CREATED_SHARE_ID="$share_id"
    log_pass "Create share succeeds: share_id=$CREATED_SHARE_ID"
}

# ─── Test 4: Access share → get share_token ───────────────────────────────────

test_access_share() {
    log_info "Testing POST /api/share/access/$CREATED_SHARE_ID..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/share/access/$CREATED_SHARE_ID" \
        -H "Content-Type: application/json" \
        -d '{}')

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-access.json" "$body"

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_fail "Access share failed: HTTP $http_code, code=$code"
        printf '%s\n' "$body"
        exit 1
    fi

    local share_token
    share_token=$(json_field "$body" "data.share_token")

    if [ -z "$share_token" ] || [ "$share_token" = "null" ]; then
        log_fail "Access share returned 200 but no share_token"
        printf '%s\n' "$body"
        exit 1
    fi

    SHARE_TOKEN="$share_token"
    log_pass "Access share returns share_token"
}

# ─── Test 5: Browse share with valid token → 200 + code 0 ────────────────────

test_browse_share_valid_token() {
    log_info "Testing GET /api/share/browse/$CREATED_SHARE_ID with valid share_token..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/share/browse/$CREATED_SHARE_ID" \
        -H "X-Share-Token: $SHARE_TOKEN")

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-browse-valid.json" "$body"

    if [ "$http_code" = "200" ] && [ "$code" = "0" ]; then
        log_pass "Browse share with valid token returns 200 + code 0"
    else
        log_fail "Browse share with valid token: expected 200 + code 0, got HTTP $http_code, code=$code"
        printf '%s\n' "$body"
        exit 1
    fi
}

# ─── Test 6: Browse share without token → error ──────────────────────────────

test_browse_share_no_token() {
    log_info "Testing GET /api/share/browse/$CREATED_SHARE_ID without share_token..."

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/share/browse/$CREATED_SHARE_ID")

    local http_code body code
    http_code=$(printf '%s\n' "$response" | tail -n 1)
    body=$(printf '%s\n' "$response" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-browse-notoken.json" "$body"

    # Should be rejected: 401 (token missing) or error code != 0
    if [ "$http_code" = "401" ]; then
        log_pass "Browse without token returns 401"
    elif [ "$code" != "0" ]; then
        log_pass "Browse without token returns error code=$code"
    else
        log_fail "Browse without token: expected error, got HTTP $http_code + code=0"
        printf '%s\n' "$body"
        exit 1
    fi
}

# ─── Main ──────────────────────────────────────────────────────────────────────

main() {
    printf '==========================================\n'
    printf 'Share Browse Integration Tests\n'
    printf '==========================================\n\n'

    if ! command -v curl >/dev/null 2>&1; then
        log_fail "curl is required"
        exit 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        log_fail "python3 is required"
        exit 1
    fi

    if ! command -v md5sum >/dev/null 2>&1; then
        log_fail "md5sum is required"
        exit 1
    fi

    ensure_server

    test_list_shares
    test_share_requires_auth

    # Upload fixture so share creation always has a real file
    upload_fixture

    test_create_share
    test_access_share
    test_browse_share_valid_token
    test_browse_share_no_token

    print_summary
}

main "$@"

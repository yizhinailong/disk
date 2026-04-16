#!/bin/bash
#
# test/integration/test_share_management.sh
# Integration tests for share management: detail, update, batch cancel.
#
# Verifies:
#   1. Upload fixture + create share → share_id
#   2. GET /api/share/{share_id} → 200 + code 0, data.share_id matches
#   3. PUT /api/share/{share_id} permission=view → 200 + code 0
#   4. Verify update: GET again → data.permission == "view"
#   5. DELETE /api/share batch cancel → 200 + summary.succeeded == 1
#   6. Detail after cancel → error (code != 0)
#   7. Access after cancel → error (code != 0)
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
JWT_SECRET="${JWT_SECRET:-test_secret_key_for_share_mgmt_32b}"
VALID_ACCOUNT="${VALID_ACCOUNT:-admin}"
VALID_PASS="${VALID_PASS:-Admin123}"
SERVER_LOG="${SERVER_LOG:-.sisyphus/evidence/share-mgmt-server.log}"

EVIDENCE_PREFIX="share-mgmt"

TOKEN=""
FILE_ID=""
SHARE_ID=""

trap cleanup EXIT

# ─── Upload fixture file ──────────────────────────────────────────────────────

upload_fixture() {
    log_step "Uploading fixture file..."

    local fixture="/tmp/disk_share_mgmt_fixture_$$.bin"
    dd if=/dev/urandom of="$fixture" bs=256 count=1 2>/dev/null
    local file_size=256
    local file_hash
    file_hash=$(md5sum "$fixture" | cut -d' ' -f1)

    local init_resp
    init_resp=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"filename\":\"share_mgmt_fixture_$$.bin\",\"file_size\":$file_size,\"file_hash\":\"$file_hash\",\"parent_id\":0}")

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

# ─── Create share ─────────────────────────────────────────────────────────────

create_share() {
    log_step "Creating share for file_id=$FILE_ID..."

    local resp
    resp=$(curl -s -X POST "$BASE_URL/api/share" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\":[$FILE_ID],\"permission\":\"download\",\"expire_days\":7}")

    save_evidence "${EVIDENCE_PREFIX}-share-create.json" "$resp"

    local code
    code=$(json_field "$resp" "code")

    if [ "$code" != "0" ]; then
        log_fail "Create share failed: code=$code"
        printf '%s\n' "$resp"
        exit 1
    fi

    SHARE_ID=$(json_field "$resp" "data.share_id")

    if [ -z "$SHARE_ID" ] || [ "$SHARE_ID" = "null" ]; then
        log_fail "Create share returned success but no share_id"
        printf '%s\n' "$resp"
        exit 1
    fi

    log_pass "Share created — share_id=$SHARE_ID"
}

# ─── Test 1: GET share detail ─────────────────────────────────────────────────

test_get_share_detail() {
    log_info "Test: GET /api/share/$SHARE_ID → 200 + code 0"

    local resp http_code body code
    resp=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/share/$SHARE_ID" \
        -H "Authorization: Bearer $TOKEN")

    http_code=$(printf '%s\n' "$resp" | tail -n 1)
    body=$(printf '%s\n' "$resp" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-detail.json" "$body"

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_fail "GET share detail: expected 200 + code 0, got HTTP $http_code + code=$code"
        printf '%s\n' "$body"
        exit 1
    fi

    local data_share_id
    data_share_id=$(json_field "$body" "data.share_id")

    if [ "$data_share_id" != "$SHARE_ID" ]; then
        log_fail "GET share detail: data.share_id mismatch — expected $SHARE_ID, got $data_share_id"
        exit 1
    fi

    log_pass "GET share detail returns correct share_id=$data_share_id"
}

# ─── Test 2: PUT update share permission ──────────────────────────────────────

test_update_share() {
    log_info "Test: PUT /api/share/$SHARE_ID with permission=view"

    local resp http_code body code
    resp=$(curl -sS -w "\n%{http_code}" -X PUT "$BASE_URL/api/share/$SHARE_ID" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d '{"permission":"view"}')

    http_code=$(printf '%s\n' "$resp" | tail -n 1)
    body=$(printf '%s\n' "$resp" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-update.json" "$body"

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_fail "PUT share update: expected 200 + code 0, got HTTP $http_code + code=$code"
        printf '%s\n' "$body"
        exit 1
    fi

    local permission
    permission=$(json_field "$body" "data.permission")

    if [ "$permission" != "view" ]; then
        log_fail "PUT share update: expected data.permission=view, got '$permission'"
        exit 1
    fi

    log_pass "PUT share update: permission changed to view"
}

# ─── Test 3: Verify update via GET ────────────────────────────────────────────

test_verify_update() {
    log_info "Test: GET /api/share/$SHARE_ID again → permission=view"

    local resp http_code body code
    resp=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/share/$SHARE_ID" \
        -H "Authorization: Bearer $TOKEN")

    http_code=$(printf '%s\n' "$resp" | tail -n 1)
    body=$(printf '%s\n' "$resp" | sed '$d')
    code=$(json_field "$body" "code")

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_fail "Verify update GET: expected 200 + code 0, got HTTP $http_code + code=$code"
        printf '%s\n' "$body"
        exit 1
    fi

    local permission
    permission=$(json_field "$body" "data.permission")

    if [ "$permission" != "view" ]; then
        log_fail "Verify update: expected data.permission=view, got '$permission'"
        exit 1
    fi

    log_pass "Verify update: permission persists as view"
}

# ─── Test 4: DELETE batch cancel share ────────────────────────────────────────

test_cancel_share_batch() {
    log_info "Test: DELETE /api/share batch cancel share_ids=[$SHARE_ID]"

    local resp http_code body code
    resp=$(curl -sS -w "\n%{http_code}" -X DELETE "$BASE_URL/api/share" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"share_ids\":[\"$SHARE_ID\"]}")

    http_code=$(printf '%s\n' "$resp" | tail -n 1)
    body=$(printf '%s\n' "$resp" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-cancel.json" "$body"

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_fail "DELETE batch cancel: expected 200 + code 0, got HTTP $http_code + code=$code"
        printf '%s\n' "$body"
        exit 1
    fi

    local succeeded
    succeeded=$(json_field "$body" "data.summary.succeeded")

    if [ "$succeeded" != "1" ]; then
        log_fail "DELETE batch cancel: expected summary.succeeded=1, got '$succeeded'"
        printf '%s\n' "$body"
        exit 1
    fi

    log_pass "DELETE batch cancel: summary.succeeded=1"
}

# ─── Test 5: Detail after cancel → error ──────────────────────────────────────

test_detail_after_cancel() {
    log_info "Test: GET /api/share/$SHARE_ID after cancel → error"

    local resp http_code body code
    resp=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/share/$SHARE_ID" \
        -H "Authorization: Bearer $TOKEN")

    http_code=$(printf '%s\n' "$resp" | tail -n 1)
    body=$(printf '%s\n' "$resp" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-detail-after-cancel.json" "$body"

    if [ "$code" = "0" ]; then
        log_fail "Detail after cancel: expected error code, got code=0"
        printf '%s\n' "$body"
        exit 1
    fi

    # Expect 60001 ShareNotFound
    if [ "$code" = "60001" ]; then
        log_pass "Detail after cancel: code=60001 (ShareNotFound)"
    else
        log_pass "Detail after cancel: error code=$code (expected != 0)"
    fi
}

# ─── Test 6: Access after cancel → error ──────────────────────────────────────

test_access_after_cancel() {
    log_info "Test: POST /api/share/access/$SHARE_ID after cancel → error"

    local resp http_code body code
    resp=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/share/access/$SHARE_ID" \
        -H "Content-Type: application/json" \
        -d '{}')

    http_code=$(printf '%s\n' "$resp" | tail -n 1)
    body=$(printf '%s\n' "$resp" | sed '$d')
    code=$(json_field "$body" "code")

    save_evidence "${EVIDENCE_PREFIX}-share-access-after-cancel.json" "$body"

    if [ "$code" = "0" ]; then
        log_fail "Access after cancel: expected error code, got code=0"
        printf '%s\n' "$body"
        exit 1
    fi

    if [ "$code" = "60001" ]; then
        log_pass "Access after cancel: code=60001 (ShareNotFound)"
    else
        log_pass "Access after cancel: error code=$code (expected != 0)"
    fi
}

# ─── Evidence summary ─────────────────────────────────────────────────────────

write_summary_evidence() {
    {
        echo "=== Share Management Integration Test Summary ==="
        echo "Date: $(date -Iseconds)"
        echo "BASE_URL: $BASE_URL"
        echo ""
        echo "FILE_ID: $FILE_ID"
        echo "SHARE_ID: $SHARE_ID"
        echo ""
        echo "Passed: $TESTS_PASSED"
        echo "Failed: $TESTS_FAILED"
    } > "$EVIDENCE_DIR/${EVIDENCE_PREFIX}-summary.txt"
    log_info "Summary evidence: ${EVIDENCE_PREFIX}-summary.txt"
}

# ─── Main ──────────────────────────────────────────────────────────────────────

main() {
    printf '==========================================\n'
    printf 'Share Management Integration Tests\n'
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
    do_login

    TOKEN=$(json_field "$LOGIN_BODY" "data.access_token")
    if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
        log_fail "Login failed"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi

    upload_fixture
    create_share

    echo ""
    printf '==========================================\n'
    printf 'Running Share Management Tests\n'
    printf '==========================================\n\n'

    test_get_share_detail
    test_update_share
    test_verify_update
    test_cancel_share_batch
    test_detail_after_cancel
    test_access_after_cancel

    echo ""
    printf '==========================================\n'
    printf 'Test Summary\n'
    printf '==========================================\n'
    printf 'Passed: %b%s%b\n' "$GREEN" "$TESTS_PASSED" "$NC"
    printf 'Failed: %b%s%b\n' "$RED" "$TESTS_FAILED" "$NC"

    write_summary_evidence
    print_summary
}

main "$@"

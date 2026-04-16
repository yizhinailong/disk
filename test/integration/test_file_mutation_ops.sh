#!/bin/bash
#
# test/integration/test_file_mutation_ops.sh
# Integration tests for file mutation operations: rename, move, upload cancel.
#
# Covers:
#   1. Upload a unique file (inline fixture with instant_upload handling)
#   2. Rename file — PUT /api/file/{file_id}/rename
#   3. Verify rename — GET /api/file/{file_id}
#   4. Create folder for move — POST /api/folder/create
#   5. Move file — PUT /api/file/move
#   6. Verify move — GET /api/file/{file_id}
#   7. Upload cancel — DELETE /api/file/upload/{upload_id}
#   8. Cancel prevents completion — POST /api/file/upload/complete with canceled upload_id
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured
#   - Redis configured
#   - User account exists (default: admin / Admin123)
#
# Usage:
#   TEST_USER=admin TEST_PASS=Admin123 bash test/integration/test_file_mutation_ops.sh
#
# Environment variables:
#   BASE_URL    - Server URL (default: http://localhost:8080)
#   TEST_USER   - Test username (default: admin)
#   TEST_PASS   - Test password (default: Admin123)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"
source "$SCRIPT_DIR/lib/http.sh"
source "$SCRIPT_DIR/lib/auth.sh"
source "$SCRIPT_DIR/lib/assert.sh"

# ─── Configuration ────────────────────────────────────────────────────────────

BASE_URL="${BASE_URL:-http://localhost:8080}"
TEST_USER="${TEST_USER:-admin}"
TEST_PASS="${TEST_PASS:-Admin123}"
EVIDENCE_DIR=".sisyphus/evidence"
EVIDENCE_PREFIX="mutation-ops"
TS="$(date +%s)"
PID="$$"

# ─── State variables ──────────────────────────────────────────────────────────

FILE_ID=""
FOLDER_ID=""
CANCELED_UPLOAD_ID=""

# ─── Prerequisites ────────────────────────────────────────────────────────────

check_prereqs() {
    if ! command -v jq &> /dev/null; then
        log_fail "jq is required but not installed"
        exit 1
    fi
    if ! command -v curl &> /dev/null; then
        log_fail "curl is required but not installed"
        exit 1
    fi
    if ! command -v md5sum &> /dev/null; then
        log_fail "md5sum is required but not installed"
        exit 1
    fi
}

# ─── Upload fixture (inline, with instant_upload handling) ────────────────────

do_upload_fixture() {
    log_step "Uploading test fixture file..."

    local fixture="/tmp/disk_mutation_test_$PID.bin"
    dd if=/dev/urandom of="$fixture" bs=256 count=1 2>/dev/null
    local file_size=256
    local file_hash
    file_hash=$(md5sum "$fixture" | cut -d' ' -f1)

    # --- Init Upload ---
    local init_resp
    init_resp=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{
            \"filename\": \"mutation_test_$PID.bin\",
            \"file_size\": $file_size,
            \"file_hash\": \"$file_hash\",
            \"parent_id\": 0
        }")

    save_evidence "${EVIDENCE_PREFIX}-upload-init.json" "$init_resp"

    local instant_upload
    instant_upload=$(echo "$init_resp" | jq -r '.data.instant_upload // false')

    if [ "$instant_upload" = "true" ]; then
        # File already exists (instant upload / dedup)
        FILE_ID=$(echo "$init_resp" | jq -r '.data.file_id // empty')
        if [ -z "$FILE_ID" ] || [ "$FILE_ID" = "null" ]; then
            log_fail "Instant upload but no file_id returned"
            echo "$init_resp"
            rm -f "$fixture"
            return 1
        fi
        log_info "Instant upload (dedup) — file_id=$FILE_ID"
    else
        local upload_id
        upload_id=$(echo "$init_resp" | jq -r '.data.upload_id // empty')
        if [ -z "$upload_id" ] || [ "$upload_id" = "null" ]; then
            log_fail "Init upload failed — no upload_id"
            echo "$init_resp"
            rm -f "$fixture"
            return 1
        fi

        # --- Upload Chunk (single chunk = whole file) ---
        local chunk_resp
        chunk_resp=$(curl -s -X POST \
            "$BASE_URL/api/file/upload/chunk?upload_id=${upload_id}&chunk_index=0&chunk_hash=${file_hash}" \
            -H "Authorization: Bearer $TOKEN" \
            -H "Content-Type: application/octet-stream" \
            --data-binary @"$fixture")

        local uploaded
        uploaded=$(echo "$chunk_resp" | jq -r '.data.uploaded // empty')
        if [ "$uploaded" != "true" ]; then
            log_fail "Upload chunk failed"
            echo "$chunk_resp"
            rm -f "$fixture"
            return 1
        fi
        save_evidence "${EVIDENCE_PREFIX}-upload-chunk.json" "$chunk_resp"

        # --- Complete Upload ---
        local complete_resp
        complete_resp=$(curl -s -X POST "$BASE_URL/api/file/upload/complete" \
            -H "Authorization: Bearer $TOKEN" \
            -H "Content-Type: application/json" \
            -d "{\"upload_id\": \"$upload_id\"}")

        FILE_ID=$(echo "$complete_resp" | jq -r '.data.file.id // empty')
        if [ -z "$FILE_ID" ] || [ "$FILE_ID" = "null" ]; then
            log_fail "Complete upload — no file.id"
            echo "$complete_resp"
            rm -f "$fixture"
            return 1
        fi
        save_evidence "${EVIDENCE_PREFIX}-upload-complete.json" "$complete_resp"
    fi

    rm -f "$fixture"
    log_pass "File uploaded — file_id=$FILE_ID"
}

# ─── Test 1: Rename file ──────────────────────────────────────────────────────

test_rename() {
    log_step "Test 1: Rename file"

    local new_name="renamed_${PID}.txt"
    local resp
    resp=$(curl -s -w "\n%{http_code}" -X PUT "$BASE_URL/api/file/$FILE_ID/rename" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"new_name\": \"$new_name\"}")

    local http_code
    http_code=$(echo "$resp" | tail -n 1)
    local body
    body=$(echo "$resp" | sed '$d')

    save_evidence "${EVIDENCE_PREFIX}-rename.json" "$body"

    local ok=true
    assert_status "rename" "$http_code" "200" || ok=false
    assert_json_field "rename" "$body" "code" "0" || ok=false

    if $ok; then
        log_pass "rename: file renamed to '$new_name' (HTTP $http_code, code=0)"
    fi
}

# ─── Test 2: Verify rename ───────────────────────────────────────────────────

test_verify_rename() {
    log_step "Test 2: Verify rename via GET /api/file/{file_id}"

    local resp
    resp=$(curl -s "$BASE_URL/api/file/$FILE_ID" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-verify-rename.json" "$resp"

    local actual_name
    actual_name=$(echo "$resp" | jq -r '.data.name // empty')

    local expected_name="renamed_${PID}.txt"
    if [ "$actual_name" = "$expected_name" ]; then
        log_pass "verify-rename: name = '$actual_name'"
    else
        log_fail "verify-rename: expected name='$expected_name', got='$actual_name'"
    fi
}

# ─── Test 3: Create folder for move ──────────────────────────────────────────

test_create_folder() {
    log_step "Test 3: Create folder for move target"

    local folder_name="move_target_${PID}_${TS}"
    local resp
    resp=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL/api/folder/create" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"name\": \"$folder_name\", \"parent_id\": 0}")

    local http_code
    http_code=$(echo "$resp" | tail -n 1)
    local body
    body=$(echo "$resp" | sed '$d')

    save_evidence "${EVIDENCE_PREFIX}-create-folder.json" "$body"

    FOLDER_ID=$(echo "$body" | jq -r '.data.id // empty')
    if [ -z "$FOLDER_ID" ] || [ "$FOLDER_ID" = "null" ]; then
        log_fail "create-folder: no folder id returned"
        echo "$body"
        return 1
    fi

    local ok=true
    assert_status "create-folder" "$http_code" "200" || ok=false
    assert_json_field "create-folder" "$body" "code" "0" || ok=false

    if $ok; then
        log_pass "create-folder: folder_id=$FOLDER_ID, name='$folder_name'"
    fi
}

# ─── Test 4: Move file ───────────────────────────────────────────────────────

test_move() {
    log_step "Test 4: Move file to folder"

    local resp
    resp=$(curl -s -w "\n%{http_code}" -X PUT "$BASE_URL/api/file/move" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [$FILE_ID], \"target_folder_id\": $FOLDER_ID}")

    local http_code
    http_code=$(echo "$resp" | tail -n 1)
    local body
    body=$(echo "$resp" | sed '$d')

    save_evidence "${EVIDENCE_PREFIX}-move.json" "$body"

    local ok=true
    assert_status "move" "$http_code" "200" || ok=false
    assert_json_field "move" "$body" "code" "0" || ok=false
    assert_json_field_numeric_gt "move" "$body" "data.moved_count" "0" || ok=false

    if $ok; then
        log_pass "move: file moved to folder_id=$FOLDER_ID"
    fi
}

# ─── Test 5: Verify move ─────────────────────────────────────────────────────

test_verify_move() {
    log_step "Test 5: Verify move via GET /api/file/{file_id}"

    local resp
    resp=$(curl -s "$BASE_URL/api/file/$FILE_ID" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-verify-move.json" "$resp"

    local actual_parent
    actual_parent=$(echo "$resp" | jq -r '.data.parent_id // empty')

    # Compare as integers (strip leading zeros if any)
    if [ "$((actual_parent + 0))" = "$((FOLDER_ID + 0))" ]; then
        log_pass "verify-move: parent_id = $actual_parent (matches folder_id=$FOLDER_ID)"
    else
        log_fail "verify-move: expected parent_id=$FOLDER_ID, got='$actual_parent'"
    fi
}

# ─── Test 6: Upload cancel ───────────────────────────────────────────────────

test_upload_cancel() {
    log_step "Test 6: Init a new upload, then cancel it"

    # Use a DIFFERENT file content to avoid instant upload (dedup)
    local fixture="/tmp/disk_mutation_cancel_$PID.bin"
    dd if=/dev/urandom of="$fixture" bs=128 count=1 2>/dev/null
    local file_size=128
    local file_hash
    file_hash=$(md5sum "$fixture" | cut -d' ' -f1)

    # Init upload
    local init_resp
    init_resp=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{
            \"filename\": \"cancel_test_$PID.bin\",
            \"file_size\": $file_size,
            \"file_hash\": \"$file_hash\",
            \"parent_id\": 0
        }")

    save_evidence "${EVIDENCE_PREFIX}-cancel-init.json" "$init_resp"

    local instant
    instant=$(echo "$init_resp" | jq -r '.data.instant_upload // false')

    if [ "$instant" = "true" ]; then
        # Rare: hash collision with existing file. Skip cancel test.
        log_info "cancel: instant_upload=true (dedup hit), skipping cancel test"
        rm -f "$fixture"
        return 0
    fi

    local upload_id
    upload_id=$(echo "$init_resp" | jq -r '.data.upload_id // empty')
    if [ -z "$upload_id" ] || [ "$upload_id" = "null" ]; then
        log_fail "cancel: failed to get upload_id for cancel test"
        echo "$init_resp"
        rm -f "$fixture"
        return 1
    fi

    CANCELED_UPLOAD_ID="$upload_id"
    log_info "cancel: upload_id=$upload_id"

    # Upload a chunk so the upload session is active
    local chunk_resp
    chunk_resp=$(curl -s -X POST \
        "$BASE_URL/api/file/upload/chunk?upload_id=${upload_id}&chunk_index=0&chunk_hash=${file_hash}" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary @"$fixture")

    local uploaded
    uploaded=$(echo "$chunk_resp" | jq -r '.data.uploaded // empty')
    if [ "$uploaded" != "true" ]; then
        log_fail "cancel: chunk upload failed"
        echo "$chunk_resp"
        rm -f "$fixture"
        return 1
    fi

    rm -f "$fixture"

    # Now cancel the upload
    local cancel_resp
    cancel_resp=$(curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL/api/file/upload/$upload_id" \
        -H "Authorization: Bearer $TOKEN")

    local http_code
    http_code=$(echo "$cancel_resp" | tail -n 1)
    local body
    body=$(echo "$cancel_resp" | sed '$d')

    save_evidence "${EVIDENCE_PREFIX}-cancel-delete.json" "$body"

    local ok=true
    assert_status "cancel" "$http_code" "200" || ok=false
    assert_json_field "cancel" "$body" "code" "0" || ok=false

    if $ok; then
        log_pass "cancel: upload canceled (upload_id=$upload_id)"
    fi
}

# ─── Test 7: Cancel prevents completion ───────────────────────────────────────

test_cancel_prevents_completion() {
    log_step "Test 7: Verify canceled upload cannot be completed"

    if [ -z "$CANCELED_UPLOAD_ID" ]; then
        log_info "cancel-prevents-completion: skipped (no canceled upload_id)"
        return 0
    fi

    local resp
    resp=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL/api/file/upload/complete" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"upload_id\": \"$CANCELED_UPLOAD_ID\"}")

    local http_code
    http_code=$(echo "$resp" | tail -n 1)
    local body
    body=$(echo "$resp" | sed '$d')

    save_evidence "${EVIDENCE_PREFIX}-cancel-complete-attempt.json" "$body"

    # Expect: HTTP non-200 OR code != 0
    local code
    code=$(echo "$body" | jq -r '.code // empty')

    if [ "$http_code" != "200" ] || [ "$code" != "0" ]; then
        log_pass "cancel-prevents-completion: complete rejected (HTTP=$http_code, code=$code)"
    else
        log_fail "cancel-prevents-completion: expected rejection, got HTTP=$http_code, code=$code"
        echo "$body"
    fi
}

# ─── Evidence Summary ─────────────────────────────────────────────────────────

write_summary_evidence() {
    {
        echo "=== File Mutation Ops Integration Test Summary ==="
        echo "Date: $(date -Iseconds)"
        echo "BASE_URL: $BASE_URL"
        echo "TEST_USER: $TEST_USER"
        echo ""
        echo "--- Fixture ---"
        echo "FILE_ID: $FILE_ID"
        echo "FOLDER_ID: $FOLDER_ID"
        echo "CANCELED_UPLOAD_ID: $CANCELED_UPLOAD_ID"
        echo ""
        echo "--- Results ---"
        echo "Passed: $TESTS_PASSED"
        echo "Failed: $TESTS_FAILED"
        echo ""
        echo "--- Operations Verified ---"
        echo "  1. Rename: PUT /api/file/{file_id}/rename"
        echo "  2. Verify rename: GET /api/file/{file_id}"
        echo "  3. Create folder: POST /api/folder/create"
        echo "  4. Move file: PUT /api/file/move"
        echo "  5. Verify move: GET /api/file/{file_id}"
        echo "  6. Upload cancel: DELETE /api/file/upload/{upload_id}"
        echo "  7. Cancel prevents completion: POST /api/file/upload/complete"
    } > "$EVIDENCE_DIR/${EVIDENCE_PREFIX}-summary.txt"
    log_info "Summary evidence: ${EVIDENCE_PREFIX}-summary.txt"
}

# ─── Main ─────────────────────────────────────────────────────────────────────

main() {
    echo "=========================================="
    echo "File Mutation Ops Integration Tests"
    echo "=========================================="
    echo ""

    check_prereqs
    check_server || exit 1

    # Setup
    do_login "$TEST_USER" "$TEST_PASS" || exit 1
    do_upload_fixture || exit 1

    echo ""
    echo "=========================================="
    echo "Running Mutation Tests"
    echo "=========================================="
    echo ""

    # Test 1: Rename file
    test_rename

    # Test 2: Verify rename
    test_verify_rename

    # Test 3: Create folder for move target
    test_create_folder

    # Test 4: Move file into folder
    test_move

    # Test 5: Verify move
    test_verify_move

    # Test 6: Upload cancel
    test_upload_cancel

    # Test 7: Cancel prevents completion
    test_cancel_prevents_completion

    # Summary
    echo ""
    print_summary
    write_summary_evidence
}

main "$@"

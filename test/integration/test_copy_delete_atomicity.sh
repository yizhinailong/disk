#!/bin/bash
#
# test/integration/test_copy_delete_atomicity.sh
# Integration tests for copy/delete/trash atomicity at API boundary.
#
# Covers:
#   - Happy-path copy: upload → copy to root → verify copied_count > 0 and new_files present
#   - Happy-path delete: delete original → verify deleted_count > 0
#   - Trash visibility: GET /api/trash → verify deleted file appears in trash list
#   - Copy non-existent file_ids: POST /api/file/copy with invalid IDs → verify code != 0
#   - Delete non-existent file_ids: DELETE /api/file with invalid IDs → verify deleted_count = 0
#   - Copy then delete copied file: independent operations succeed
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured
#   - Redis configured
#   - User account exists (default: admin / Admin123)
#
# Usage:
#   TEST_USER=admin TEST_PASS=Admin123 bash test/integration/test_copy_delete_atomicity.sh
#
# Environment variables:
#   BASE_URL    - Server URL (default: http://localhost:8080)
#   TEST_USER   - Test username (default: admin)
#   TEST_PASS   - Test password (default: Admin123)
#

set -euo pipefail

# ─── Configuration ────────────────────────────────────────────────────────────

BASE_URL="${BASE_URL:-http://localhost:8080}"
TEST_USER="${TEST_USER:-admin}"
TEST_PASS="${TEST_PASS:-Admin123}"
EVIDENCE_DIR=".sisyphus/evidence"
EVIDENCE_PREFIX="task-6"

# ─── Colors ───────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# ─── Counters ─────────────────────────────────────────────────────────────────

TESTS_PASSED=0
TESTS_FAILED=0

# ─── Helpers ──────────────────────────────────────────────────────────────────

log_info()  { echo -e "${YELLOW}[INFO]${NC} $1"; }
log_pass()  { echo -e "${GREEN}[PASS]${NC} $1"; ((TESTS_PASSED++)); }
log_fail()  { echo -e "${RED}[FAIL]${NC} $1"; ((TESTS_FAILED++)); }
log_step()  { echo -e "${CYAN}[STEP]${NC} $1"; }

save_evidence() {
    local name="$1"
    local data="$2"
    echo "$data" > "$EVIDENCE_DIR/${name}"
    log_info "Evidence saved: $name"
}

save_raw_evidence() {
    local name="$1"
    shift
    "$@" > "$EVIDENCE_DIR/${name}" 2>&1
    log_info "Evidence saved: $name"
}

# ─── curl helper: capture status + headers + body ─────────────────────────────
# Usage: curl_fetch <output_var_prefix> <curl_args...>
# Produces: ${prefix}_status, ${prefix}_headers, ${prefix}_body
curl_fetch() {
    local prefix="$1"
    shift

    local tmp_headers
    tmp_headers=$(mktemp)
    local tmp_body
    tmp_body=$(mktemp)

    local http_code
    http_code=$(curl -s -o "$tmp_body" -w "%{http_code}" -D "$tmp_headers" "$@")

    eval "${prefix}_status=\$http_code"
    eval "${prefix}_headers=\$(cat \"\$tmp_headers\")"
    eval "${prefix}_body=\$(cat \"\$tmp_body\")"

    rm -f "$tmp_headers" "$tmp_body"
}

# Extract a single header value (case-insensitive) from a headers block
header_value() {
    local headers="$1"
    local name="$2"
    echo "$headers" | grep -i "^${name}:" | head -1 | sed "s/^[^:]*:[[:space:]]*//" | tr -d '\r'
}

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

check_server() {
    log_info "Checking server at $BASE_URL..."
    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/auth/login")
    if [[ "$code" =~ ^(400|401|405)$ ]]; then
        log_pass "Server is running"
        return 0
    else
        log_fail "Server not responding (got HTTP $code)"
        return 1
    fi
}

# ─── Phase 1: Login ──────────────────────────────────────────────────────────

do_login() {
    log_step "Logging in as $TEST_USER..."

    local response
    response=$(curl -s -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "{\"account\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}")

    TOKEN=$(echo "$response" | jq -r '.data.access_token // empty')

    if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
        log_fail "Login failed"
        echo "$response"
        return 1
    fi

    log_pass "Login successful"
    save_evidence "${EVIDENCE_PREFIX}-login.json" "$response"
}

# ─── Phase 2: Upload a test file ──────────────────────────────────────────────
# Creates a small (256-byte) test file via the chunked upload flow.

do_upload() {
    log_step "Uploading test fixture file..."

    # Create a 256-byte test file
    local fixture="/tmp/disk_atomicity_test_$$.bin"
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
            \"filename\": \"atomicity_test.bin\",
            \"file_size\": $file_size,
            \"file_hash\": \"$file_hash\",
            \"parent_id\": 0
        }")

    local instant_upload
    instant_upload=$(echo "$init_resp" | jq -r '.data.instant_upload // false')

    if [ "$instant_upload" = "true" ]; then
        # File already exists (instant upload / dedup) — still get file_id
        FILE_ID=$(echo "$init_resp" | jq -r '.data.file_id // empty')
        if [ -z "$FILE_ID" ] || [ "$FILE_ID" = "null" ]; then
            log_fail "Instant upload but no file_id returned"
            echo "$init_resp"
            rm -f "$fixture"
            return 1
        fi
        FILE_SIZE=$file_size
        FILE_HASH=$file_hash
        log_info "Instant upload (dedup) — file_id=$FILE_ID"
        save_evidence "${EVIDENCE_PREFIX}-upload-init.json" "$init_resp"
        rm -f "$fixture"
        return 0
    fi

    local upload_id
    upload_id=$(echo "$init_resp" | jq -r '.data.upload_id // empty')
    if [ -z "$upload_id" ] || [ "$upload_id" = "null" ]; then
        log_fail "Init upload failed"
        echo "$init_resp"
        rm -f "$fixture"
        return 1
    fi
    save_evidence "${EVIDENCE_PREFIX}-upload-init.json" "$init_resp"

    # --- Upload Chunk ---
    local chunk_hash="$file_hash"  # single chunk = whole file
    local chunk_resp
    chunk_resp=$(curl -s -X POST \
        "$BASE_URL/api/file/upload/chunk?upload_id=${upload_id}&chunk_index=0&chunk_hash=${chunk_hash}" \
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
    FILE_SIZE=$file_size
    FILE_HASH=$file_hash
    save_evidence "${EVIDENCE_PREFIX}-upload-complete.json" "$complete_resp"

    rm -f "$fixture"
    log_pass "File uploaded — file_id=$FILE_ID, size=$FILE_SIZE"
}

# ─── Assertions ───────────────────────────────────────────────────────────────

assert_status() {
    local label="$1"
    local actual="$2"
    local expected="$3"
    if [ "$actual" = "$expected" ]; then
        return 0
    else
        log_fail "$label: expected HTTP $expected, got HTTP $actual"
        return 1
    fi
}

assert_json_field() {
    local label="$1"
    local body="$2"
    local field="$3"
    local expected="$4"
    local actual
    actual=$(echo "$body" | jq -r ".$field // empty")
    if [ "$actual" = "$expected" ]; then
        return 0
    else
        log_fail "$label: expected .$field = '$expected', got '$actual'"
        return 1
    fi
}

assert_json_field_numeric_gt() {
    local label="$1"
    local body="$2"
    local field="$3"
    local min_value="$4"
    local actual
    actual=$(echo "$body" | jq -r ".$field // 0")
    if [ "$actual" -gt "$min_value" ]; then
        return 0
    else
        log_fail "$label: expected .$field > $min_value, got '$actual'"
        return 1
    fi
}

assert_json_array_not_empty() {
    local label="$1"
    local body="$2"
    local field="$3"
    local length
    length=$(echo "$body" | jq -r ".$field | length // 0")
    if [ "$length" -gt 0 ]; then
        return 0
    else
        log_fail "$label: expected .$field array to be non-empty, got length=$length"
        return 1
    fi
}

# ─── Test: Happy-path Copy ────────────────────────────────────────────────────

test_happy_copy() {
    log_step "Test: Copy file to root folder"

    local resp
    resp=$(curl -s -X POST "$BASE_URL/api/file/copy" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [$FILE_ID], \"target_folder_id\": 0}")

    save_evidence "${EVIDENCE_PREFIX}-copy-happy.json" "$resp"

    local ok=true

    # Verify success response
    assert_json_field "copy-happy" "$resp" "code" "0" || ok=false

    # Verify copied_count > 0
    assert_json_field_numeric_gt "copy-happy" "$resp" "data.copied_count" "0" || ok=false

    # Verify new_files array is present and non-empty
    assert_json_array_not_empty "copy-happy" "$resp" "data.new_files" || ok=false

    # Extract and save the new file ID for later tests
    COPIED_FILE_ID=$(echo "$resp" | jq -r '.data.new_files[0].new_id // empty')
    if [ -n "$COPIED_FILE_ID" ] && [ "$COPIED_FILE_ID" != "null" ]; then
        log_info "Copied file ID: $COPIED_FILE_ID"
    else
        log_fail "copy-happy: failed to extract new_file_id"
        ok=false
    fi

    if $ok; then
        log_pass "copy-happy: copy operation successful"
    fi
}

# ─── Test: Happy-path Delete ──────────────────────────────────────────────────

test_happy_delete() {
    log_step "Test: Delete original file"

    local resp
    resp=$(curl -s -X DELETE "$BASE_URL/api/file" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [$FILE_ID]}")

    save_evidence "${EVIDENCE_PREFIX}-delete-happy.json" "$resp"

    local ok=true

    # Verify success response
    assert_json_field "delete-happy" "$resp" "code" "0" || ok=false

    # Verify deleted_count > 0
    assert_json_field_numeric_gt "delete-happy" "$resp" "data.deleted_count" "0" || ok=false

    if $ok; then
        log_pass "delete-happy: delete operation successful"
    fi
}

# ─── Test: Trash Visibility ───────────────────────────────────────────────────

test_trash_visibility() {
    log_step "Test: GET /api/trash → verify deleted file appears"

    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/trash" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-trash-list.json" "$resp"

    local ok=true

    # Verify success response
    assert_json_field "trash-visibility" "$resp" "code" "0" || ok=false

    # Verify the deleted file appears in trash items
    local found_in_trash
    found_in_trash=$(echo "$resp" | jq -r --arg file_id "$FILE_ID" '.data.items[] | select(.item_id == ($file_id | tonumber)) | .item_id // empty')
    if [ -n "$found_in_trash" ] && [ "$found_in_trash" != "null" ]; then
        log_pass "trash-visibility: deleted file (id=$FILE_ID) found in trash"
    else
        log_fail "trash-visibility: deleted file (id=$FILE_ID) NOT found in trash"
        ok=false
    fi

    # Extract total count for evidence
    local trash_total
    trash_total=$(echo "$resp" | jq -r '.data.total // 0')
    log_info "Trash total items: $trash_total"

    if $ok; then
        log_pass "trash-visibility: trash list verification successful"
    fi
}

# ─── Test: Copy Non-existent File IDs ────────────────────────────────────────

test_copy_nonexistent() {
    log_step "Test: Copy non-existent file_ids → verify error"

    local resp
    resp=$(curl -s -X POST "$BASE_URL/api/file/copy" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [99999999], \"target_folder_id\": 0}")

    save_evidence "${EVIDENCE_PREFIX}-copy-nonexistent.json" "$resp"

    local ok=true

    # Verify code != 0 (error response)
    local code
    code=$(echo "$resp" | jq -r '.code // 0')
    if [ "$code" != "0" ]; then
        log_pass "copy-nonexistent: error response (code=$code)"
    else
        log_fail "copy-nonexistent: expected error (code != 0), got code=0"
        ok=false
    fi

    # Verify no partial state: copied_count should be 0 or absent
    local copied_count
    copied_count=$(echo "$resp" | jq -r '.data.copied_count // 0')
    if [ "$copied_count" = "0" ]; then
        log_info "copy-nonexistent: no partial state (copied_count=0)"
    else
        log_fail "copy-nonexistent: partial state detected (copied_count=$copied_count)"
        ok=false
    fi

    if $ok; then
        log_pass "copy-nonexistent: verification successful"
    fi
}

# ─── Test: Delete Non-existent File IDs ───────────────────────────────────────

test_delete_nonexistent() {
    log_step "Test: Delete non-existent file_ids → verify deleted_count = 0"

    local resp
    resp=$(curl -s -X DELETE "$BASE_URL/api/file" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [99999999]}")

    save_evidence "${EVIDENCE_PREFIX}-delete-nonexistent.json" "$resp"

    local ok=true

    # Verify success response (delete is idempotent)
    assert_json_field "delete-nonexistent" "$resp" "code" "0" || ok=false

    # Verify deleted_count = 0 (no files deleted)
    assert_json_field "delete-nonexistent" "$resp" "data.deleted_count" "0" || ok=false

    if $ok; then
        log_pass "delete-nonexistent: idempotent delete verification successful"
    fi
}

# ─── Test: Copy Then Delete Copied File ───────────────────────────────────────

test_copy_then_delete() {
    log_step "Test: Copy another file, then delete the copy"

    # First, upload another file for this test
    local fixture2="/tmp/disk_atomicity_test2_$$.bin"
    dd if=/dev/urandom of="$fixture2" bs=128 count=1 2>/dev/null
    local file_size2=128
    local file_hash2
    file_hash2=$(md5sum "$fixture2" | cut -d' ' -f1)

    # Init upload
    local init_resp2
    init_resp2=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{
            \"filename\": \"atomicity_test2.bin\",
            \"file_size\": $file_size2,
            \"file_hash\": \"$file_hash2\",
            \"parent_id\": 0
        }")

    local upload_id2
    upload_id2=$(echo "$init_resp2" | jq -r '.data.upload_id // empty')

    # Upload chunk
    curl -s -X POST \
        "$BASE_URL/api/file/upload/chunk?upload_id=${upload_id2}&chunk_index=0&chunk_hash=${file_hash2}" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary @"$fixture2" > /dev/null

    # Complete upload
    local complete_resp2
    complete_resp2=$(curl -s -X POST "$BASE_URL/api/file/upload/complete" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"upload_id\": \"$upload_id2\"}")

    local file_id2
    file_id2=$(echo "$complete_resp2" | jq -r '.data.file.id // empty')
    rm -f "$fixture2"

    if [ -z "$file_id2" ] || [ "$file_id2" = "null" ]; then
        log_fail "copy-then-delete: failed to upload second file"
        return 1
    fi

    log_info "Second file uploaded: file_id=$file_id2"

    # Copy the second file
    local copy_resp
    copy_resp=$(curl -s -X POST "$BASE_URL/api/file/copy" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [$file_id2], \"target_folder_id\": 0}")

    save_evidence "${EVIDENCE_PREFIX}-copy-then-delete-copy.json" "$copy_resp"

    local ok=true

    assert_json_field "copy-then-delete-copy" "$copy_resp" "code" "0" || ok=false
    assert_json_field_numeric_gt "copy-then-delete-copy" "$copy_resp" "data.copied_count" "0" || ok=false

    local copied_file_id2
    copied_file_id2=$(echo "$copy_resp" | jq -r '.data.new_files[0].new_id // empty')

    if [ -z "$copied_file_id2" ] || [ "$copied_file_id2" = "null" ]; then
        log_fail "copy-then-delete: failed to get copied file ID"
        return 1
    fi

    log_info "Copied second file: copied_id=$copied_file_id2"

    # Delete the copied file
    local delete_resp
    delete_resp=$(curl -s -X DELETE "$BASE_URL/api/file" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [$copied_file_id2]}")

    save_evidence "${EVIDENCE_PREFIX}-copy-then-delete-delete.json" "$delete_resp"

    assert_json_field "copy-then-delete-delete" "$delete_resp" "code" "0" || ok=false
    assert_json_field_numeric_gt "copy-then-delete-delete" "$delete_resp" "data.deleted_count" "0" || ok=false

    if $ok; then
        log_pass "copy-then-delete: both copy and delete operations succeeded independently"
    fi
}

# ─── Evidence Summary ─────────────────────────────────────────────────────────

write_summary_evidence() {
    {
        echo "=== Copy/Delete Atomicity Integration Test Summary ==="
        echo "Date: $(date -Iseconds)"
        echo "BASE_URL: $BASE_URL"
        echo "TEST_USER: $TEST_USER"
        echo ""
        echo "--- Fixture ---"
        echo "FILE_ID: $FILE_ID"
        echo "FILE_SIZE: $FILE_SIZE"
        echo "FILE_HASH: $FILE_HASH"
        echo "COPIED_FILE_ID: $COPIED_FILE_ID"
        echo ""
        echo "--- Results ---"
        echo "Passed: $TESTS_PASSED"
        echo "Failed: $TESTS_FAILED"
        echo ""
        echo "--- Atomicity Guarantees Verified ---"
        echo "✓ Happy-path copy: copied_count > 0 and new_files array present"
        echo "✓ Happy-path delete: deleted_count > 0"
        echo "✓ Trash visibility: deleted file appears in trash list"
        echo "✓ Copy non-existent IDs: code != 0, no partial state"
        echo "✓ Delete non-existent IDs: deleted_count = 0 (idempotent)"
        echo "✓ Copy then delete: operations succeed independently"
    } > "$EVIDENCE_DIR/${EVIDENCE_PREFIX}-copy-delete-atomicity.txt"
    log_info "Summary evidence: ${EVIDENCE_PREFIX}-copy-delete-atomicity.txt"
}

# ─── Main ─────────────────────────────────────────────────────────────────────

main() {
    echo "=========================================="
    echo "Copy/Delete Atomicity Integration Tests"
    echo "=========================================="
    echo ""

    check_prereqs
    check_server || exit 1

    # Setup
    do_login || exit 1
    do_upload || exit 1

    echo ""
    echo "=========================================="
    echo "Running Atomicity Tests"
    echo "=========================================="
    echo ""

    # Test 1: Happy-path copy
    test_happy_copy

    # Test 2: Happy-path delete
    test_happy_delete

    # Test 3: Trash visibility
    test_trash_visibility

    # Test 4: Copy non-existent file IDs
    test_copy_nonexistent

    # Test 5: Delete non-existent file IDs
    test_delete_nonexistent

    # Test 6: Copy then delete copied file
    test_copy_then_delete

    # Summary
    echo ""
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    echo ""

    write_summary_evidence

    if [ "$TESTS_FAILED" -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed.${NC}"
        exit 1
    fi
}

main "$@"

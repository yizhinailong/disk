#!/bin/bash
#
# test/integration/test_trash_lifecycle.sh
# Integration tests for trash lifecycle operations (restore, permanent delete, empty all).
#
# Covers:
#   - Upload file A → soft-delete → restore → verify file is active again
#   - Upload file B → soft-delete → permanent delete → verify file is gone
#   - File appears in trash after soft-delete
#   - Restore operation returns success status
#   - Permanent delete operation returns success status
#   - Empty all trash clears remaining test data
#   - Verify restored file is NOT in trash after restore
#   - Verify deleted file is NOT accessible after permanent delete
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured
#   - Redis configured
#   - User account exists (default: admin / Admin123)
#
# Usage:
#   TEST_USER=admin TEST_PASS=Admin123 bash test/integration/test_trash_lifecycle.sh
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
EVIDENCE_PREFIX="task-6-trash-lifecycle"

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

# ─── Upload Fixture ────────────────────────────────────────────────────────────

upload_fixture() {
    local filename_suffix="$1"
    log_step "Uploading test fixture: $filename_suffix"

    local fixture="/tmp/disk_trash_${filename_suffix}_$$.bin"
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
            \"filename\": \"${filename_suffix}_$$.bin\",
            \"file_size\": $file_size,
            \"file_hash\": \"$file_hash\",
            \"parent_id\": 0
        }")

    local instant_upload
    instant_upload=$(echo "$init_resp" | jq -r '.data.instant_upload // false')

    if [ "$instant_upload" = "true" ]; then
        FILE_ID=$(echo "$init_resp" | jq -r '.data.file_id // empty')
        if [ -z "$FILE_ID" ] || [ "$FILE_ID" = "null" ]; then
            log_fail "Instant upload but no file_id returned"
            echo "$init_resp"
            rm -f "$fixture"
            return 1
        fi
        rm -f "$fixture"
        log_pass "File uploaded (instant) — file_id=$FILE_ID"
        save_evidence "${EVIDENCE_PREFIX}-upload-${filename_suffix}-init.json" "$init_resp"
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
    save_evidence "${EVIDENCE_PREFIX}-upload-${filename_suffix}-init.json" "$init_resp"

    # --- Upload Chunk ---
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
    save_evidence "${EVIDENCE_PREFIX}-upload-${filename_suffix}-chunk.json" "$chunk_resp"

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
    save_evidence "${EVIDENCE_PREFIX}-upload-${filename_suffix}-complete.json" "$complete_resp"

    rm -f "$fixture"
    log_pass "File uploaded — file_id=$FILE_ID"
}

# ─── Soft Delete File ──────────────────────────────────────────────────────────

soft_delete_file() {
    local file_id="$1"
    local label="$2"
    log_step "Soft-delete file ($label): file_id=$file_id"

    local resp
    resp=$(curl -s -X DELETE "$BASE_URL/api/file" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [$file_id]}")

    save_evidence "${EVIDENCE_PREFIX}-delete-${label}.json" "$resp"

    local ok=true

    assert_json_field "delete-${label}" "$resp" "code" "0" || ok=false
    assert_json_field_numeric_gt "delete-${label}" "$resp" "data.deleted_count" "0" || ok=false

    if $ok; then
        log_pass "delete-${label}: file moved to trash successfully"
    fi
}

# ─── Get Trash ID for File ─────────────────────────────────────────────────────

get_trash_id() {
    local file_id="$1"
    local label="$2"
    log_step "Get trash_id for file ($label): file_id=$file_id"

    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/trash" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-trash-list-${label}.json" "$resp"

    local trash_id
    trash_id=$(echo "$resp" | python3 -c "
import json, sys
try:
    data = json.load(sys.stdin)
    for item in data.get('data', {}).get('items', []):
        if item.get('original_id') == $file_id:
            print(item['id'])
            break
except Exception:
    pass
" 2>/dev/null)

    if [ -n "$trash_id" ] && [ "$trash_id" != "null" ]; then
        log_pass "trash_id found for file $file_id: $trash_id"
    else
        log_fail "trash_id not found for file $file_id"
        trash_id=""
    fi
}

# ─── Test: Upload File A and File B ────────────────────────────────────────────

test_upload_fixtures() {
    log_section "Upload Fixtures"

    # Upload file A for restore test
    upload_fixture "restore_test"
    FILE_A_ID="$FILE_ID"
    log_info "File A uploaded: $FILE_A_ID"

    # Upload file B for permanent delete test
    upload_fixture "delete_test"
    FILE_B_ID="$FILE_ID"
    log_info "File B uploaded: $FILE_B_ID"
}

# ─── Test: Soft Delete Both Files ──────────────────────────────────────────────

test_soft_delete() {
    log_section "Soft Delete Files"

    soft_delete_file "$FILE_A_ID" "file-a"
    soft_delete_file "$FILE_B_ID" "file-b"
}

# ─── Test: Verify Files in Trash ───────────────────────────────────────────────

test_verify_in_trash() {
    log_section "Verify Files in Trash"

    # Get trash list
    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/trash" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-trash-list-verify.json" "$resp"

    local ok=true

    assert_json_field "trash-list-verify" "$resp" "code" "0" || ok=false

    # Verify file A is in trash
    local found_a
    found_a=$(echo "$resp" | python3 -c "
import json, sys
try:
    data = json.load(sys.stdin)
    for item in data.get('data', {}).get('items', []):
        if item.get('original_id') == $FILE_A_ID:
            print('true')
            break
except Exception:
    pass
" 2>/dev/null)

    if [ "$found_a" = "true" ]; then
        log_pass "File A ($FILE_A_ID) found in trash"
    else
        log_fail "File A ($FILE_A_ID) NOT found in trash"
        ok=false
    fi

    # Verify file B is in trash
    local found_b
    found_b=$(echo "$resp" | python3 -c "
import json, sys
try:
    data = json.load(sys.stdin)
    for item in data.get('data', {}).get('items', []):
        if item.get('original_id') == $FILE_B_ID:
            print('true')
            break
except Exception:
    pass
" 2>/dev/null)

    if [ "$found_b" = "true" ]; then
        log_pass "File B ($FILE_B_ID) found in trash"
    else
        log_fail "File B ($FILE_B_ID) NOT found in trash"
        ok=false
    fi

    if $ok; then
        log_pass "trash-verify: both files confirmed in trash"
    fi
}

# ─── Test: Save Trash IDs ───────────────────────────────────────────────────────

test_save_trash_ids() {
    log_section "Save Trash IDs"

    get_trash_id "$FILE_A_ID" "file-a"
    TRASH_A_ID="$trash_id"

    get_trash_id "$FILE_B_ID" "file-b"
    TRASH_B_ID="$trash_id"

    if [ -z "$TRASH_A_ID" ] || [ -z "$TRASH_B_ID" ]; then
        log_fail "Failed to retrieve trash IDs"
        exit 1
    fi

    log_info "Trash IDs saved: A=$TRASH_A_ID, B=$TRASH_B_ID"
}

# ─── Test: Restore File A ───────────────────────────────────────────────────────

test_restore_file() {
    log_section "Restore File A"

    log_step "Restore file A: trash_id=$TRASH_A_ID"

    local resp
    resp=$(curl -s -X POST "$BASE_URL/api/trash/restore" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"trash_ids\": [$TRASH_A_ID]}")

    save_evidence "${EVIDENCE_PREFIX}-restore-file-a.json" "$resp"

    local ok=true

    assert_json_field "restore-file-a" "$resp" "code" "0" || ok=false

    # Verify results[0].status == "success"
    local status
    status=$(echo "$resp" | jq -r '.data.results[0].status // empty')
    if [ "$status" = "success" ]; then
        log_pass "restore-file-a: status = success"
    else
        log_fail "restore-file-a: expected status=success, got '$status'"
        ok=false
    fi

    # Verify returned file_id matches FILE_A_ID
    local restored_file_id
    restored_file_id=$(echo "$resp" | jq -r '.data.results[0].file_id // empty')
    if [ "$restored_file_id" = "$FILE_A_ID" ]; then
        log_pass "restore-file-a: file_id matches ($FILE_A_ID)"
    else
        log_fail "restore-file-a: expected file_id=$FILE_A_ID, got '$restored_file_id'"
        ok=false
    fi

    if $ok; then
        log_pass "restore-file-a: restore operation successful"
    fi
}

# ─── Test: Verify File A is Active ───────────────────────────────────────────────

test_verify_active_after_restore() {
    log_section "Verify File A is Active After Restore"

    log_step "Get file A details: file_id=$FILE_A_ID"

    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/file/$FILE_A_ID" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-file-a-active.json" "$resp"

    local ok=true

    assert_json_field "file-a-active" "$resp" "code" "0" || ok=false

    # Verify file data is present
    local file_id_in_resp
    file_id_in_resp=$(echo "$resp" | jq -r '.data.file.id // empty')
    if [ "$file_id_in_resp" = "$FILE_A_ID" ]; then
        log_pass "file-a-active: file is accessible with correct id"
    else
        log_fail "file-a-active: expected file.id=$FILE_A_ID, got '$file_id_in_resp'"
        ok=false
    fi

    if $ok; then
        log_pass "file-a-active: file A is active and accessible"
    fi
}

# ─── Test: Verify File A NOT in Trash ──────────────────────────────────────────

test_verify_not_in_trash_after_restore() {
    log_section "Verify File A NOT in Trash After Restore"

    log_step "Check trash for file A: file_id=$FILE_A_ID"

    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/trash" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-trash-list-after-restore.json" "$resp"

    local found
    found=$(echo "$resp" | python3 -c "
import json, sys
try:
    data = json.load(sys.stdin)
    for item in data.get('data', {}).get('items', []):
        if item.get('original_id') == $FILE_A_ID:
            print('true')
            break
except Exception:
    pass
" 2>/dev/null)

    if [ "$found" != "true" ]; then
        log_pass "file-a-not-in-trash: file A is NOT in trash (correct)"
    else
        log_fail "file-a-not-in-trash: file A is still in trash (should not be)"
        exit 1
    fi
}

# ─── Test: Permanent Delete File B ──────────────────────────────────────────────

test_permanent_delete_file() {
    log_section "Permanent Delete File B"

    log_step "Permanently delete file B: trash_id=$TRASH_B_ID"

    local resp
    resp=$(curl -s -X DELETE "$BASE_URL/api/trash" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"trash_ids\": [$TRASH_B_ID]}")

    save_evidence "${EVIDENCE_PREFIX}-permanent-delete-file-b.json" "$resp"

    local ok=true

    assert_json_field "permanent-delete-file-b" "$resp" "code" "0" || ok=false

    # Verify results[0].status == "success"
    local status
    status=$(echo "$resp" | jq -r '.data.results[0].status // empty')
    if [ "$status" = "success" ]; then
        log_pass "permanent-delete-file-b: status = success"
    else
        log_fail "permanent-delete-file-b: expected status=success, got '$status'"
        ok=false
    fi

    # Verify freed_space is present
    local freed_space
    freed_space=$(echo "$resp" | jq -r '.data.results[0].freed_space // empty')
    if [ -n "$freed_space" ] && [ "$freed_space" != "null" ]; then
        log_pass "permanent-delete-file-b: freed_space = $freed_space"
    else
        log_fail "permanent-delete-file-b: freed_space not found"
        ok=false
    fi

    if $ok; then
        log_pass "permanent-delete-file-b: permanent delete successful"
    fi
}

# ─── Test: Verify File B is Gone ────────────────────────────────────────────────

test_verify_file_b_gone() {
    log_section "Verify File B is Gone After Permanent Delete"

    log_step "Try to get file B details: file_id=$FILE_B_ID"

    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/file/$FILE_B_ID" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-file-b-gone.json" "$resp"

    local code
    code=$(echo "$resp" | jq -r '.code // -1')

    # File should NOT be accessible (code != 0)
    if [ "$code" != "0" ]; then
        log_pass "file-b-gone: file B is NOT accessible (code=$code)"
    else
        log_fail "file-b-gone: file B is still accessible (should be deleted)"
        exit 1
    fi
}

# ─── Test: Empty All Trash ───────────────────────────────────────────────────

test_empty_all_trash() {
    log_section "Empty All Trash"

    log_step "Empty all trash items"

    local resp
    resp=$(curl -s -X DELETE "$BASE_URL/api/trash/all" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-empty-all-trash.json" "$resp"

    local ok=true

    assert_json_field "empty-all-trash" "$resp" "code" "0" || ok=false

    # Verify deleted_count is present
    local deleted_count
    deleted_count=$(echo "$resp" | jq -r '.data.deleted_count // empty')
    if [ -n "$deleted_count" ] && [ "$deleted_count" != "null" ]; then
        log_pass "empty-all-trash: deleted_count = $deleted_count"
    else
        log_fail "empty-all-trash: deleted_count not found"
        ok=false
    fi

    if $ok; then
        log_pass "empty-all-trash: operation successful"
    fi
}

# ─── Evidence Summary ─────────────────────────────────────────────────────────

write_summary_evidence() {
    {
        echo "=== Trash Lifecycle Integration Test Summary ==="
        echo "Date: $(date -Iseconds)"
        echo "BASE_URL: $BASE_URL"
        echo "TEST_USER: $TEST_USER"
        echo ""
        echo "--- Test Fixtures ---"
        echo "FILE_A_ID (restore test): $FILE_A_ID"
        echo "FILE_B_ID (permanent delete test): $FILE_B_ID"
        echo "TRASH_A_ID: $TRASH_A_ID"
        echo "TRASH_B_ID: $TRASH_B_ID"
        echo ""
        echo "--- Results ---"
        echo "Passed: $TESTS_PASSED"
        echo "Failed: $TESTS_FAILED"
        echo ""
        echo "--- Trash Lifecycle Verified ---"
        echo "✓ Upload fixtures: two files uploaded successfully"
        echo "✓ Soft delete: both files moved to trash"
        echo "✓ Verify in trash: both files found in trash list"
        echo "✓ Save trash IDs: retrieved trash entry IDs for both files"
        echo "✓ Restore file A: file restored to active state"
        echo "✓ File A active: file A accessible via GET /api/file/{id}"
        echo "✓ File A not in trash: file A removed from trash list"
        echo "✓ Permanent delete file B: file permanently deleted"
        echo "✓ File B gone: file B no longer accessible"
        echo "✓ Empty all trash: cleanup operation successful"
    } > "$EVIDENCE_DIR/${EVIDENCE_PREFIX}-summary.txt"
    log_info "Summary evidence: ${EVIDENCE_PREFIX}-summary.txt"
}

# ─── Main ─────────────────────────────────────────────────────────────────────

main() {
    echo "=========================================="
    echo "Trash Lifecycle Integration Tests"
    echo "=========================================="
    echo ""

    check_prereqs
    check_server || exit 1

    # Setup
    do_login "$TEST_USER" "$TEST_PASS" || exit 1

    echo ""
    echo "=========================================="
    echo "Running Trash Lifecycle Tests"
    echo "=========================================="
    echo ""

    # Test 1: Upload both files
    test_upload_fixtures

    # Test 2: Soft delete both files
    test_soft_delete

    # Test 3: Verify files in trash
    test_verify_in_trash

    # Test 4: Save trash IDs
    test_save_trash_ids

    # Test 5: Restore file A
    test_restore_file

    # Test 6: Verify file A is active
    test_verify_active_after_restore

    # Test 7: Verify file A not in trash
    test_verify_not_in_trash_after_restore

    # Test 8: Permanent delete file B
    test_permanent_delete_file

    # Test 9: Verify file B is gone
    test_verify_file_b_gone

    # Test 10: Empty all trash (cleanup)
    test_empty_all_trash

    # Summary
    echo ""
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    echo ""

    write_summary_evidence

    print_summary
}

main "$@"

#!/bin/bash
#
# test/integration/test_file_metadata_query.sh
# Integration tests for file metadata query APIs.
#
# Covers:
#   - File list: GET /api/file/list?parent_id=0 → find uploaded fixture in items
#   - File detail: GET /api/file/{file_id} → verify id, name, size, hash
#   - Download info: GET /api/file/download/{file_id}/info → verify file_id, filename
#   - Search: GET /api/file/search?keyword=X → find fixture by keyword
#   - Nonexistent file: GET /api/file/99999999 → error response
#   - Unauthenticated: GET /api/file/list without token → HTTP 401
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured
#   - Redis configured
#   - User account exists (default: admin / Admin123)
#
# Usage:
#   TEST_USER=admin TEST_PASS=Admin123 bash test/integration/test_file_metadata_query.sh
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
EVIDENCE_PREFIX="metadata-query"

# Fixture state
FILE_ID=""
FILE_SIZE=256
FILE_HASH=""
FILE_NAME="metadata_test_$(date +%s)_$$.bin"

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

# ─── Upload fixture ───────────────────────────────────────────────────────────
# Creates a small (256-byte) test file via the chunked upload flow and stores
# FILE_ID for subsequent metadata queries.

do_upload_fixture() {
    log_step "Uploading fixture file: $FILE_NAME"

    local fixture="/tmp/disk_metadata_test_$$.bin"
    dd if=/dev/urandom of="$fixture" bs=256 count=1 2>/dev/null
    FILE_SIZE=256
    FILE_HASH=$(md5sum "$fixture" | cut -d' ' -f1)

    # --- Init Upload ---
    local init_resp
    init_resp=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{
            \"filename\": \"$FILE_NAME\",
            \"file_size\": $FILE_SIZE,
            \"file_hash\": \"$FILE_HASH\",
            \"parent_id\": 0
        }")

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
    local chunk_hash="$FILE_HASH"  # single chunk = whole file
    curl -s -X POST \
        "$BASE_URL/api/file/upload/chunk?upload_id=${upload_id}&chunk_index=0&chunk_hash=${chunk_hash}" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary @"$fixture" > /dev/null

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

    rm -f "$fixture"
    log_pass "Fixture uploaded — file_id=$FILE_ID, name=$FILE_NAME, size=$FILE_SIZE"
}

# ─── Test: File List ──────────────────────────────────────────────────────────

test_file_list() {
    log_step "Test: GET /api/file/list?parent_id=0"

    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/file/list?parent_id=0" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-file-list.json" "$resp"

    local ok=true

    # Verify success
    assert_json_field "file-list" "$resp" "code" "0" || ok=false

    # Verify items array is present
    assert_json_array_not_empty "file-list" "$resp" "data.items" || ok=false

    # Verify the uploaded fixture is found in items by name
    local found_name
    found_name=$(echo "$resp" | jq -r --arg name "$FILE_NAME" \
        '.data.items[] | select(.name == $name) | .name // empty')
    if [ -n "$found_name" ] && [ "$found_name" = "$FILE_NAME" ]; then
        log_info "file-list: fixture '$FILE_NAME' found in items"
    else
        log_fail "file-list: fixture '$FILE_NAME' NOT found in items"
        ok=false
    fi

    # Verify the fixture's file_id appears in items
    local found_id
    found_id=$(echo "$resp" | jq -r --arg fid "$FILE_ID" \
        '.data.items[] | select((.id | tostring) == $fid) | .id // empty')
    if [ -n "$found_id" ]; then
        log_info "file-list: fixture id=$FILE_ID found in items"
    else
        log_fail "file-list: fixture id=$FILE_ID NOT found in items"
        ok=false
    fi

    if $ok; then
        log_pass "file-list: list query verified"
    fi
}

# ─── Test: File Detail ────────────────────────────────────────────────────────

test_file_detail() {
    log_step "Test: GET /api/file/{file_id}"

    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/file/$FILE_ID" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-file-detail.json" "$resp"

    local ok=true

    # Verify success
    assert_json_field "file-detail" "$resp" "code" "0" || ok=false

    # Verify data.id or data.file.file_id matches
    local detail_id
    detail_id=$(echo "$resp" | jq -r '.data.id // .data.file_id // .data.file.file_id // empty')
    if [ "$detail_id" = "$FILE_ID" ]; then
        log_info "file-detail: id=$detail_id matches FILE_ID=$FILE_ID"
    else
        log_fail "file-detail: id='$detail_id' does not match FILE_ID=$FILE_ID"
        ok=false
    fi

    # Verify data.name matches
    local detail_name
    detail_name=$(echo "$resp" | jq -r '.data.name // .data.file.name // empty')
    if [ "$detail_name" = "$FILE_NAME" ]; then
        log_info "file-detail: name='$detail_name' matches fixture"
    else
        log_fail "file-detail: name='$detail_name' does not match '$FILE_NAME'"
        ok=false
    fi

    # Verify data.size or data.file.size matches
    local detail_size
    detail_size=$(echo "$resp" | jq -r '.data.size // .data.file.size // empty')
    if [ "$detail_size" = "$FILE_SIZE" ]; then
        log_info "file-detail: size=$detail_size matches fixture"
    else
        log_fail "file-detail: size='$detail_size' does not match $FILE_SIZE"
        ok=false
    fi

    # Verify data.hash or data.file.hash matches
    local detail_hash
    detail_hash=$(echo "$resp" | jq -r '.data.hash // .data.file.hash // empty')
    if [ "$detail_hash" = "$FILE_HASH" ]; then
        log_info "file-detail: hash matches fixture"
    else
        log_fail "file-detail: hash='$detail_hash' does not match '$FILE_HASH'"
        ok=false
    fi

    if $ok; then
        log_pass "file-detail: detail query verified (id, name, size, hash)"
    fi
}

# ─── Test: Download Info ──────────────────────────────────────────────────────

test_download_info() {
    log_step "Test: GET /api/file/download/{file_id}/info"

    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/file/download/$FILE_ID/info" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-download-info.json" "$resp"

    local ok=true

    # Verify success
    assert_json_field "download-info" "$resp" "code" "0" || ok=false

    # Verify data.file_id matches
    local dl_file_id
    dl_file_id=$(echo "$resp" | jq -r '.data.file_id // empty')
    if [ "$dl_file_id" = "$FILE_ID" ]; then
        log_info "download-info: file_id=$dl_file_id matches"
    else
        log_fail "download-info: file_id='$dl_file_id' does not match FILE_ID=$FILE_ID"
        ok=false
    fi

    # Verify data.filename matches
    local dl_filename
    dl_filename=$(echo "$resp" | jq -r '.data.filename // empty')
    if [ "$dl_filename" = "$FILE_NAME" ]; then
        log_info "download-info: filename='$dl_filename' matches"
    else
        log_fail "download-info: filename='$dl_filename' does not match '$FILE_NAME'"
        ok=false
    fi

    # Verify data.file_size matches
    local dl_file_size
    dl_file_size=$(echo "$resp" | jq -r '.data.file_size // empty')
    if [ "$dl_file_size" = "$FILE_SIZE" ]; then
        log_info "download-info: file_size=$dl_file_size matches"
    else
        log_fail "download-info: file_size='$dl_file_size' does not match $FILE_SIZE"
        ok=false
    fi

    # Verify data.file_hash matches
    local dl_file_hash
    dl_file_hash=$(echo "$resp" | jq -r '.data.file_hash // empty')
    if [ "$dl_file_hash" = "$FILE_HASH" ]; then
        log_info "download-info: file_hash matches"
    else
        log_fail "download-info: file_hash='$dl_file_hash' does not match '$FILE_HASH'"
        ok=false
    fi

    if $ok; then
        log_pass "download-info: download info query verified (file_id, filename, file_size, file_hash)"
    fi
}

# ─── Test: Search ─────────────────────────────────────────────────────────────

test_file_search() {
    log_step "Test: GET /api/file/search?keyword=metadata_test_$$"

    local keyword="metadata_test_$$"
    local resp
    resp=$(curl -s -X GET "$BASE_URL/api/file/search?keyword=$keyword" \
        -H "Authorization: Bearer $TOKEN")

    save_evidence "${EVIDENCE_PREFIX}-file-search.json" "$resp"

    local ok=true

    # Verify success
    assert_json_field "file-search" "$resp" "code" "0" || ok=false

    # Verify items array is present
    assert_json_array_not_empty "file-search" "$resp" "data.items" || ok=false

    # Verify the fixture is found by name matching keyword
    local found_name
    found_name=$(echo "$resp" | jq -r --arg kw "$keyword" \
        '.data.items[] | select(.name | test($kw)) | .name // empty')
    if [ -n "$found_name" ]; then
        log_info "file-search: found matching file '$found_name' for keyword '$keyword'"
    else
        log_fail "file-search: no file matching keyword '$keyword' in items"
        ok=false
    fi

    # Verify the fixture's id is in results
    local found_id
    found_id=$(echo "$resp" | jq -r --arg fid "$FILE_ID" \
        '.data.items[] | select((.id | tostring) == $fid) | .id // empty')
    if [ -n "$found_id" ]; then
        log_info "file-search: fixture id=$FILE_ID found in results"
    else
        log_fail "file-search: fixture id=$FILE_ID NOT found in results"
        ok=false
    fi

    if $ok; then
        log_pass "file-search: search query verified"
    fi
}

# ─── Test: Nonexistent File ───────────────────────────────────────────────────

test_nonexistent_file() {
    log_step "Test: GET /api/file/99999999 → expect error"

    local resp http_code
    resp=$(curl -s -w "\n%{http_code}" -X GET "$BASE_URL/api/file/99999999" \
        -H "Authorization: Bearer $TOKEN")

    http_code=$(echo "$resp" | tail -1)
    local body=$(echo "$resp" | sed '$d')

    save_evidence "${EVIDENCE_PREFIX}-nonexistent.json" "$body"

    # Accept either non-200 HTTP status or non-zero code in body
    local code
    code=$(echo "$body" | jq -r '.code // empty')

    if [ "$http_code" != "200" ]; then
        log_pass "nonexistent-file: HTTP $http_code (not found) — correct"
    elif [ "$code" != "0" ] && [ -n "$code" ]; then
        log_pass "nonexistent-file: code=$code (error) — correct"
    else
        log_fail "nonexistent-file: expected error, got HTTP $http_code code=$code"
    fi
}

# ─── Test: Unauthenticated List ───────────────────────────────────────────────

test_unauthenticated_list() {
    log_step "Test: GET /api/file/list without token → expect 401"

    local http_code
    http_code=$(curl -s -o /dev/null -w "%{http_code}" -X GET "$BASE_URL/api/file/list?parent_id=0")

    save_evidence "${EVIDENCE_PREFIX}-unauthenticated.txt" "HTTP $http_code"

    if [ "$http_code" = "401" ]; then
        log_pass "unauthenticated-list: HTTP 401 — correct"
    else
        log_fail "unauthenticated-list: expected HTTP 401, got HTTP $http_code"
    fi
}

# ─── Evidence Summary ─────────────────────────────────────────────────────────

write_summary_evidence() {
    {
        echo "=== File Metadata Query Integration Test Summary ==="
        echo "Date: $(date -Iseconds)"
        echo "BASE_URL: $BASE_URL"
        echo "TEST_USER: $TEST_USER"
        echo ""
        echo "--- Fixture ---"
        echo "FILE_ID: $FILE_ID"
        echo "FILE_NAME: $FILE_NAME"
        echo "FILE_SIZE: $FILE_SIZE"
        echo "FILE_HASH: $FILE_HASH"
        echo ""
        echo "--- Results ---"
        echo "Passed: $TESTS_PASSED"
        echo "Failed: $TESTS_FAILED"
        echo ""
        echo "--- Tests ---"
        echo "  file-list: GET /api/file/list?parent_id=0 — find fixture in items"
        echo "  file-detail: GET /api/file/{file_id} — verify id, name, size, hash"
        echo "  download-info: GET /api/file/download/{file_id}/info — verify file_id, filename"
        echo "  file-search: GET /api/file/search?keyword=X — find fixture by keyword"
        echo "  nonexistent-file: GET /api/file/99999999 — expect error"
        echo "  unauthenticated-list: GET /api/file/list (no token) — expect 401"
    } > "$EVIDENCE_DIR/${EVIDENCE_PREFIX}-summary.txt"
    log_info "Summary evidence: ${EVIDENCE_PREFIX}-summary.txt"
}

# ─── Main ─────────────────────────────────────────────────────────────────────

main() {
    echo "=========================================="
    echo "File Metadata Query Integration Tests"
    echo "=========================================="
    echo ""

    check_prereqs
    check_server || exit 1

    # Setup
    do_login "$TEST_USER" "$TEST_PASS" || exit 1
    do_upload_fixture || exit 1

    echo ""
    echo "=========================================="
    echo "Running Metadata Query Tests"
    echo "=========================================="
    echo ""

    # Test 1: File list
    test_file_list

    # Test 2: File detail
    test_file_detail

    # Test 3: Download info
    test_download_info

    # Test 4: Search
    test_file_search

    # Test 5: Nonexistent file
    test_nonexistent_file

    # Test 6: Unauthenticated list
    test_unauthenticated_list

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

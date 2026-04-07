#!/bin/bash
#
# test/integration/test_download_flow.sh
# Integration tests for download parity: personal file + share file.
#
# Covers:
#   - File download: 200 (full), 206 (partial Range), 416 (unsatisfiable Range)
#   - Share download: 200, 206, 416 (same assertions)
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured
#   - Redis configured
#   - User account exists (default: admin / Admin123)
#
# Usage:
#   TEST_USER=admin TEST_PASS=Admin123 bash test/integration/test_download_flow.sh
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
EVIDENCE_PREFIX="task-2"

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
    local fixture="/tmp/disk_download_test_$$.bin"
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
            \"filename\": \"download_test.bin\",
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

# ─── Phase 3: Create share ───────────────────────────────────────────────────

do_create_share() {
    log_step "Creating share for file_id=$FILE_ID..."

    local resp
    resp=$(curl -s -X POST "$BASE_URL/api/share" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\": [$FILE_ID], \"permission\": \"download\", \"expire_days\": 1}")

    SHARE_ID=$(echo "$resp" | jq -r '.data.share_id // empty')
    if [ -z "$SHARE_ID" ] || [ "$SHARE_ID" = "null" ]; then
        log_fail "Create share failed"
        echo "$resp"
        return 1
    fi
    save_evidence "${EVIDENCE_PREFIX}-share-create.json" "$resp"
    log_pass "Share created — share_id=$SHARE_ID"
}

# ─── Phase 4: Access share to get share_token ────────────────────────────────

do_access_share() {
    log_step "Accessing share to get share_token..."

    local resp
    resp=$(curl -s -X POST "$BASE_URL/api/share/access/$SHARE_ID" \
        -H "Content-Type: application/json" \
        -d "{}")

    SHARE_TOKEN=$(echo "$resp" | jq -r '.data.share_token // empty')
    if [ -z "$SHARE_TOKEN" ] || [ "$SHARE_TOKEN" = "null" ]; then
        log_fail "Access share failed"
        echo "$resp"
        return 1
    fi

    # Also extract the file_id from the share's file list (should match)
    local share_file_id
    share_file_id=$(echo "$resp" | jq -r '.data.files[0].id // empty')
    if [ -n "$share_file_id" ] && [ "$share_file_id" != "null" ]; then
        SHARE_FILE_ID="$share_file_id"
    else
        SHARE_FILE_ID="$FILE_ID"
    fi

    save_evidence "${EVIDENCE_PREFIX}-share-access.json" "$resp"
    log_pass "Share access — share_token obtained, file_id=$SHARE_FILE_ID"
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

assert_header_contains() {
    local label="$1"
    local headers="$2"
    local header_name="$3"
    local expected_value="$4"
    local actual
    actual=$(header_value "$headers" "$header_name")
    if echo "$actual" | grep -qF "$expected_value"; then
        return 0
    else
        log_fail "$label: expected $header_name containing '$expected_value', got '$actual'"
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

# ─── Test: Personal File Download 200 ─────────────────────────────────────────

test_file_download_200() {
    log_step "Test: GET /api/file/download/$FILE_ID → 200 (full download)"

    curl_fetch "r" -s "$BASE_URL/api/file/download/$FILE_ID" \
        -H "Authorization: Bearer $TOKEN"

    save_evidence "${EVIDENCE_PREFIX}-file-200.headers.txt" echo "${r_headers}"
    save_evidence "${EVIDENCE_PREFIX}-file-200.body.bin" echo "${r_body}"

    local ok=true
    assert_status "file-200" "${r_status}" "200" || ok=false
    assert_header_contains "file-200" "${r_headers}" "Content-Disposition" "attachment" || ok=false
    assert_header_contains "file-200" "${r_headers}" "Content-Disposition" "download_test.bin" || ok=false
    assert_header_contains "file-200" "${r_headers}" "Content-Length" "$FILE_SIZE" || ok=false
    assert_header_contains "file-200" "${r_headers}" "Accept-Ranges" "bytes" || ok=false

    if [ -n "$FILE_HASH" ]; then
        assert_header_contains "file-200" "${r_headers}" "ETag" "$FILE_HASH" || ok=false
    fi

    if $ok; then
        log_pass "file-200: full download OK"
    fi
}

# ─── Test: Personal File Download 206 ─────────────────────────────────────────

test_file_download_206() {
    log_step "Test: GET /api/file/download/$FILE_ID (Range: bytes=0-9) → 206"

    curl_fetch "r" -s "$BASE_URL/api/file/download/$FILE_ID" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Range: bytes=0-9"

    save_evidence "${EVIDENCE_PREFIX}-file-206.headers.txt" echo "${r_headers}"
    # Body is binary, save length info
    local body_len=${#r_body}
    save_evidence "${EVIDENCE_PREFIX}-file-206.body.len" echo "body_length=$body_len"

    local ok=true
    assert_status "file-206" "${r_status}" "206" || ok=false

    local expected_range="bytes 0-9/$FILE_SIZE"
    assert_header_contains "file-206" "${r_headers}" "Content-Range" "bytes 0-9/$FILE_SIZE" || ok=false
    assert_header_contains "file-206" "${r_headers}" "Content-Length" "10" || ok=false
    assert_header_contains "file-206" "${r_headers}" "Accept-Ranges" "bytes" || ok=false

    if $ok; then
        log_pass "file-206: partial content OK"
    fi
}

# ─── Test: Personal File Download 416 ─────────────────────────────────────────

test_file_download_416() {
    log_step "Test: GET /api/file/download/$FILE_ID (Range: bytes=99999-99999) → 416"

    curl_fetch "r" -s "$BASE_URL/api/file/download/$FILE_ID" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Range: bytes=99999-99999"

    save_evidence "${EVIDENCE_PREFIX}-file-416.headers.txt" echo "${r_headers}"
    save_evidence "${EVIDENCE_PREFIX}-file-416.json" echo "${r_body}"

    local ok=true
    assert_status "file-416" "${r_status}" "416" || ok=false

    # Content-Range: bytes */<size>
    assert_header_contains "file-416" "${r_headers}" "Content-Range" "bytes */$FILE_SIZE" || ok=false

    # JSON body shape: { "code": 10002, "message": "...", "data": { ... } }
    assert_json_field "file-416" "${r_body}" "code" "10002" || ok=false
    local has_msg
    has_msg=$(echo "${r_body}" | jq -r '.message // empty')
    if [ -z "$has_msg" ]; then
        log_fail "file-416: missing .message field"
        ok=false
    fi
    local has_data
    has_data=$(echo "${r_body}" | jq -r '.data // empty')
    if [ -z "$has_data" ]; then
        log_fail "file-416: missing .data field"
        ok=false
    fi

    if $ok; then
        log_pass "file-416: unsatisfiable range OK"
    fi
}

# ─── Test: Share Download 200 ─────────────────────────────────────────────────

test_share_download_200() {
    log_step "Test: GET /api/share/download/$SHARE_ID/$SHARE_FILE_ID → 200"

    curl_fetch "r" -s "$BASE_URL/api/share/download/$SHARE_ID/$SHARE_FILE_ID" \
        -H "X-Share-Token: $SHARE_TOKEN"

    save_evidence "${EVIDENCE_PREFIX}-share-200.headers.txt" echo "${r_headers}"
    save_evidence "${EVIDENCE_PREFIX}-share-200.body.bin" echo "${r_body}"

    local ok=true
    assert_status "share-200" "${r_status}" "200" || ok=false
    assert_header_contains "share-200" "${r_headers}" "Content-Disposition" "attachment" || ok=false
    assert_header_contains "share-200" "${r_headers}" "Content-Disposition" "download_test.bin" || ok=false
    assert_header_contains "share-200" "${r_headers}" "Content-Length" "$FILE_SIZE" || ok=false
    assert_header_contains "share-200" "${r_headers}" "Accept-Ranges" "bytes" || ok=false

    if $ok; then
        log_pass "share-200: full share download OK"
    fi
}

# ─── Test: Share Download 206 ─────────────────────────────────────────────────

test_share_download_206() {
    log_step "Test: GET /api/share/download/$SHARE_ID/$SHARE_FILE_ID (Range: bytes=0-9) → 206"

    curl_fetch "r" -s "$BASE_URL/api/share/download/$SHARE_ID/$SHARE_FILE_ID" \
        -H "X-Share-Token: $SHARE_TOKEN" \
        -H "Range: bytes=0-9"

    save_evidence "${EVIDENCE_PREFIX}-share-206.headers.txt" echo "${r_headers}"
    local body_len=${#r_body}
    save_evidence "${EVIDENCE_PREFIX}-share-206.body.len" echo "body_length=$body_len"

    local ok=true
    assert_status "share-206" "${r_status}" "206" || ok=false
    assert_header_contains "share-206" "${r_headers}" "Content-Range" "bytes 0-9/$FILE_SIZE" || ok=false
    assert_header_contains "share-206" "${r_headers}" "Content-Length" "10" || ok=false
    assert_header_contains "share-206" "${r_headers}" "Accept-Ranges" "bytes" || ok=false

    if $ok; then
        log_pass "share-206: partial share download OK"
    fi
}

# ─── Test: Share Download 416 ─────────────────────────────────────────────────

test_share_download_416() {
    log_step "Test: GET /api/share/download/$SHARE_ID/$SHARE_FILE_ID (Range: bytes=99999-99999) → 416"

    curl_fetch "r" -s "$BASE_URL/api/share/download/$SHARE_ID/$SHARE_FILE_ID" \
        -H "X-Share-Token: $SHARE_TOKEN" \
        -H "Range: bytes=99999-99999"

    save_evidence "${EVIDENCE_PREFIX}-share-416.headers.txt" echo "${r_headers}"
    save_evidence "${EVIDENCE_PREFIX}-share-416.json" echo "${r_body}"

    local ok=true
    assert_status "share-416" "${r_status}" "416" || ok=false

    # Content-Range: bytes */<size>
    assert_header_contains "share-416" "${r_headers}" "Content-Range" "bytes */$FILE_SIZE" || ok=false

    # JSON body shape: { "code": 10002, "message": "...", "data": { ... } }
    assert_json_field "share-416" "${r_body}" "code" "10002" || ok=false
    local has_msg
    has_msg=$(echo "${r_body}" | jq -r '.message // empty')
    if [ -z "$has_msg" ]; then
        log_fail "share-416: missing .message field"
        ok=false
    fi
    local has_data
    has_data=$(echo "${r_body}" | jq -r '.data // empty')
    if [ -z "$has_data" ]; then
        log_fail "share-416: missing .data field"
        ok=false
    fi

    if $ok; then
        log_pass "share-416: unsatisfiable range OK"
    fi
}

# ─── Evidence Summary ─────────────────────────────────────────────────────────

write_summary_evidence() {
    {
        echo "=== Download Flow Integration Test Summary ==="
        echo "Date: $(date -Iseconds)"
        echo "BASE_URL: $BASE_URL"
        echo "TEST_USER: $TEST_USER"
        echo ""
        echo "--- Fixture ---"
        echo "FILE_ID: $FILE_ID"
        echo "FILE_SIZE: $FILE_SIZE"
        echo "FILE_HASH: $FILE_HASH"
        echo "SHARE_ID: $SHARE_ID"
        echo "SHARE_FILE_ID: $SHARE_FILE_ID"
        echo ""
        echo "--- Results ---"
        echo "Passed: $TESTS_PASSED"
        echo "Failed: $TESTS_FAILED"
    } > "$EVIDENCE_DIR/${EVIDENCE_PREFIX}-download-flow.txt"
    log_info "Summary evidence: ${EVIDENCE_PREFIX}-download-flow.txt"
}

# ─── Main ─────────────────────────────────────────────────────────────────────

main() {
    echo "=========================================="
    echo "Download Flow Integration Tests"
    echo "=========================================="
    echo ""

    check_prereqs
    check_server || exit 1

    # Setup
    do_login || exit 1
    do_upload || exit 1
    do_create_share || exit 1
    do_access_share || exit 1

    echo ""
    echo "=========================================="
    echo "Running Download Tests"
    echo "=========================================="
    echo ""

    # Personal file download tests
    test_file_download_200
    test_file_download_206
    test_file_download_416

    # Share file download tests
    test_share_download_200
    test_share_download_206
    test_share_download_416

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

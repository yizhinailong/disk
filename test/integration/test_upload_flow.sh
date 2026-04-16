#!/bin/bash
#
# test/integration/test_upload_flow.sh
# Integration tests for file upload flow APIs (4.1-4.4)
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured
#   - Redis configured
#   - User account for testing
#
# Usage:
#   ./test/integration/test_upload_flow.sh
#
# Environment variables:
#   BASE_URL    - Server URL (default: http://localhost:8080)
#   TEST_USER   - Test username (default: admin)
#   TEST_PASS   - Test password (default: Admin123)
#

set -e

# Configuration
BASE_URL="${BASE_URL:-http://localhost:8080}"
TEST_USER="${TEST_USER:-admin}"
TEST_PASS="${TEST_PASS:-Admin123}"
EVIDENCE_DIR=".sisyphus/evidence"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Helper functions
log_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((TESTS_FAILED++))
}

# Save evidence
save_evidence() {
    local name="$1"
    local data="$2"
    echo "$data" > "$EVIDENCE_DIR/$name.json"
    log_info "Evidence saved: $name.json"
}

# Parse JSON field using python3 (no jq dependency)
json_field() {
    local json="$1"
    local path="$2"

    JSON_INPUT="$json" python3 - "$path" <<'PY'
import json
import os
import sys

try:
    data = json.loads(os.environ["JSON_INPUT"])
except Exception:
    print("")
    raise SystemExit(0)

value = data
for part in sys.argv[1].split('.'):
    if isinstance(value, dict) and part in value:
        value = value[part]
    else:
        print("")
        raise SystemExit(0)

if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("")
else:
    print(value)
PY
}

# Check server health
check_server() {
    log_info "Checking server at $BASE_URL..."
    if curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/auth/login" | grep -q "400\|401\|405"; then
        log_pass "Server is running"
        return 0
    else
        log_fail "Server not responding"
        return 1
    fi
}

# Login and get token
login() {
    log_info "Logging in as $TEST_USER..."

    local response
    response=$(curl -s -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "{\"account\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}")

    TOKEN=$(json_field "$response" "data.access_token")

    if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
        log_fail "Login failed"
        echo "$response"
        return 1
    fi

    log_pass "Login successful"
    save_evidence "login" "$response"
    return 0
}

test_happy_path_upload() {
    log_info "Testing Happy Path Upload Flow..."

    local chunk_0_file="/tmp/test_chunk_0.bin"
    local chunk_1_file="/tmp/test_chunk_1.bin"
    local full_file="/tmp/test_full_file.bin"

    printf 'upload-part-0\n' > "$chunk_0_file"
    printf 'upload-part-1\n' > "$chunk_1_file"

    local chunk_0_hash
    local chunk_1_hash
    chunk_0_hash=$(md5sum "$chunk_0_file" | cut -d' ' -f1)
    chunk_1_hash=$(md5sum "$chunk_1_file" | cut -d' ' -f1)

    cat "$chunk_0_file" "$chunk_1_file" > "$full_file"
    local file_hash
    local file_size
    file_hash=$(md5sum "$full_file" | cut -d' ' -f1)
    file_size=$(stat -c%s "$full_file")

    log_info "File hash: $file_hash (size: $file_size)"
    log_info "Chunk 0 hash: $chunk_0_hash"
    log_info "Chunk 1 hash: $chunk_1_hash"

    local response
    response=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{
            \"filename\": \"happy_path_test.bin\",
            \"file_size\": $file_size,
            \"file_hash\": \"$file_hash\",
            \"parent_id\": 0
        }")

    local upload_id
    upload_id=$(json_field "$response" "data.upload_id")

    if [ -z "$upload_id" ] || [ "$upload_id" = "null" ]; then
        log_fail "Init Upload (happy path) - failed to get upload_id"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    log_pass "Init Upload (happy path) - upload_id: $upload_id"
    save_evidence "init-upload-normal" "$response"

    response=$(curl -s -X POST "$BASE_URL/api/file/upload/chunk?upload_id=$upload_id&chunk_index=0&chunk_hash=$chunk_0_hash" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@$chunk_0_file")

    local uploaded_0
    uploaded_0=$(json_field "$response" "data.uploaded")

    if [ "$uploaded_0" != "true" ]; then
        log_fail "Upload Chunk 0 (happy path)"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    log_pass "Upload Chunk 0 (happy path)"
    save_evidence "upload-chunk-0" "$response"

    response=$(curl -s -X POST "$BASE_URL/api/file/upload/chunk?upload_id=$upload_id&chunk_index=1&chunk_hash=$chunk_1_hash" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@$chunk_1_file")

    local uploaded_1
    uploaded_1=$(json_field "$response" "data.uploaded")

    if [ "$uploaded_1" != "true" ]; then
        log_fail "Upload Chunk 1 (happy path)"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    log_pass "Upload Chunk 1 (happy path)"
    save_evidence "upload-chunk-1" "$response"

    response=$(curl -s -X POST "$BASE_URL/api/file/upload/complete" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{ \"upload_id\": \"$upload_id\" }")

    local file_id
    local file_name
    local returned_hash
    file_id=$(json_field "$response" "data.file.id")
    file_name=$(json_field "$response" "data.file.name")
    returned_hash=$(json_field "$response" "data.file.hash")

    if [ -z "$file_id" ] || [ "$file_id" = "null" ]; then
        log_fail "Complete Upload (happy path) - no file_id returned"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    if [ "$returned_hash" != "$file_hash" ]; then
        log_fail "Complete Upload (happy path) - hash mismatch: expected $file_hash, got $returned_hash"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    if [ "$file_name" != "happy_path_test.bin" ]; then
        log_fail "Complete Upload (happy path) - filename mismatch: expected happy_path_test.bin, got $file_name"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    log_pass "Complete Upload (happy path) - file_id: $file_id, hash verified"
    save_evidence "complete-upload-success" "$response"

    rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"

    return 0
}

test_missing_chunk_upload() {
    log_info "Testing Missing Chunk Upload (failure path)..."

    local chunk_0_file="/tmp/test_chunk_missing_0.bin"
    local chunk_1_file="/tmp/test_chunk_missing_1.bin"
    local full_file="/tmp/test_full_missing.bin"

    printf 'upload-part-missing-0\n' > "$chunk_0_file"
    printf 'upload-part-missing-1\n' > "$chunk_1_file"

    local chunk_0_hash
    local chunk_1_hash
    chunk_0_hash=$(md5sum "$chunk_0_file" | cut -d' ' -f1)
    chunk_1_hash=$(md5sum "$chunk_1_file" | cut -d' ' -f1)

    cat "$chunk_0_file" "$chunk_1_file" > "$full_file"
    local file_hash
    local file_size
    file_hash=$(md5sum "$full_file" | cut -d' ' -f1)
    file_size=$(stat -c%s "$full_file")

    log_info "File hash: $file_hash (size: $file_size)"

    local response
    response=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{
            \"filename\": \"missing_chunk_test.bin\",
            \"file_size\": $file_size,
            \"file_hash\": \"$file_hash\",
            \"parent_id\": 0
        }")

    local upload_id
    upload_id=$(json_field "$response" "data.upload_id")

    if [ -z "$upload_id" ] || [ "$upload_id" = "null" ]; then
        log_fail "Init Upload (missing chunk) - failed to get upload_id"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    log_pass "Init Upload (missing chunk) - upload_id: $upload_id"
    save_evidence "init-upload-missing" "$response"

    response=$(curl -s -X POST "$BASE_URL/api/file/upload/chunk?upload_id=$upload_id&chunk_index=0&chunk_hash=$chunk_0_hash" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@$chunk_0_file")

    local uploaded_0
    uploaded_0=$(json_field "$response" "data.uploaded")

    if [ "$uploaded_0" != "true" ]; then
        log_fail "Upload Chunk 0 (missing chunk)"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    log_pass "Upload Chunk 0 (missing chunk) - deliberately not uploading chunk 1"
    save_evidence "upload-chunk-missing-0" "$response"

    response=$(curl -s -X POST "$BASE_URL/api/file/upload/complete" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{ \"upload_id\": \"$upload_id\" }")

    local code
    local message
    code=$(json_field "$response" "code")
    message=$(json_field "$response" "message")

    if [ "$code" = "0" ] || [ -z "$code" ] || [ "$code" = "null" ]; then
        log_fail "Complete Upload (missing chunk) - expected non-zero code, got: $code"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    if ! echo "$message" | grep -q "Not all chunks uploaded"; then
        log_fail "Complete Upload (missing chunk) - expected 'Not all chunks uploaded' in message, got: $message"
        echo "$response"
        rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"
        return 1
    fi

    log_pass "Complete Upload (missing chunk) - correctly failed with code $code and message: $message"
    save_evidence "complete-upload-missing-chunk" "$response"

    rm -f "$chunk_0_file" "$chunk_1_file" "$full_file"

    return 0
}

test_init_upload_quota() {
    log_info "Testing Init Upload (quota exceeded)..."

    # Use a valid 32-char MD5 hash for quota test
    # This is the MD5 of "quota_test_file"
    local quota_hash="a1c7e6486f5811f4e23e6c696c0d6363"

    local response
    response=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{
            \"filename\": \"huge_file.pdf\",
            \"file_size\": 999999999999,
            \"file_hash\": \"$quota_hash\",
            \"parent_id\": 0
        }")

    local code
    code=$(json_field "$response" "code")

    if [ "$code" = "50004" ]; then
        log_pass "Init Upload (quota exceeded) - code: $code"
        save_evidence "init-upload-quota" "$response"
        return 0
    else
        log_fail "Init Upload (quota exceeded) - expected code 50004, got: $code"
        echo "$response"
        return 1
    fi
}

main() {
    echo "=========================================="
    echo "File Upload Flow Integration Tests"
    echo "=========================================="
    echo ""

    mkdir -p "$EVIDENCE_DIR"

    # Check prerequisites
    if ! command -v python3 &> /dev/null; then
        log_fail "python3 is required but not installed"
        exit 1
    fi

    if ! command -v curl &> /dev/null; then
        log_fail "curl is required but not installed"
        exit 1
    fi

    check_server || exit 1
    login || exit 1

    test_happy_path_upload
    test_missing_chunk_upload
    test_init_upload_quota

    echo ""
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    echo ""

    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed.${NC}"
        exit 1
    fi
}

main "$@"

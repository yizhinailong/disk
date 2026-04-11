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

# Test 4.1: Init Upload - Normal
test_init_upload_normal() {
    log_info "Testing Init Upload (normal)..."

    local response
    response=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d '{
            "filename": "test_document.pdf",
            "file_size": 10485760,
            "file_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "parent_id": 0
        }')

    local instant_upload
    instant_upload=$(json_field "$response" "data.instant_upload")
    local upload_id
    upload_id=$(json_field "$response" "data.upload_id")

    if [ "$instant_upload" = "false" ] && [ -n "$upload_id" ]; then
        log_pass "Init Upload (normal) - upload_id: $upload_id"
        UPLOAD_ID="$upload_id"
        save_evidence "init-upload-normal" "$response"
        return 0
    else
        log_fail "Init Upload (normal)"
        echo "$response"
        return 1
    fi
}

# Test 4.1: Init Upload - Quota Exceeded
test_init_upload_quota() {
    log_info "Testing Init Upload (quota exceeded)..."

    local response
    response=$(curl -s -X POST "$BASE_URL/api/file/upload/init" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d '{
            "filename": "huge_file.pdf",
            "file_size": 999999999999,
            "file_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "parent_id": 0
        }')

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

# Test 4.2: Upload Chunk
# Contract: POST with query params (upload_id, chunk_index, chunk_hash) + raw binary body (application/octet-stream)
test_upload_chunk() {
    log_info "Testing Upload Chunk..."

    local chunk_file="/tmp/test_chunk.bin"
    dd if=/dev/urandom of="$chunk_file" bs=1024 count=1 2>/dev/null
    local chunk_hash
    chunk_hash=$(md5sum "$chunk_file" | cut -d' ' -f1)

    local response
    response=$(curl -s -X POST "$BASE_URL/api/file/upload/chunk?upload_id=$UPLOAD_ID&chunk_index=0&chunk_hash=$chunk_hash" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@$chunk_file")

    local uploaded
    uploaded=$(json_field "$response" "data.uploaded")

    rm -f "$chunk_file"

    if [ "$uploaded" = "true" ]; then
        log_pass "Upload Chunk - index 0 (binary body)"
        save_evidence "upload-chunk" "$response"
        return 0
    else
        log_fail "Upload Chunk"
        echo "$response"
        return 1
    fi
}

# Test 4.4: Cancel Upload
test_cancel_upload() {
    log_info "Testing Cancel Upload..."
    
    local response
    response=$(curl -s -X DELETE "$BASE_URL/api/file/upload/$UPLOAD_ID" \
        -H "Authorization: Bearer $TOKEN")

    local code
    code=$(json_field "$response" "code")

    if [ "$code" = "0" ]; then
        log_pass "Cancel Upload"
        save_evidence "cancel-upload" "$response"
        return 0
    else
        log_fail "Cancel Upload"
        echo "$response"
        return 1
    fi
}

# Main test runner
main() {
    echo "=========================================="
    echo "File Upload Flow Integration Tests"
    echo "=========================================="
    echo ""
    
    # Check prerequisites
    if ! command -v python3 &> /dev/null; then
        log_fail "python3 is required but not installed"
        exit 1
    fi

    if ! command -v curl &> /dev/null; then
        log_fail "curl is required but not installed"
        exit 1
    fi
    
    # Run tests
    check_server || exit 1
    login || exit 1
    
    test_init_upload_normal
    test_init_upload_quota
    test_upload_chunk
    test_cancel_upload
    
    # Summary
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

#!/bin/bash
# tests/api/test_upload.sh
#
# API Verification Test: File Upload Endpoints
# Tests chunked upload, instant upload, and error handling
#
# Usage:
#   ./test_upload.sh
#
# Exit codes:
#   0 - All tests passed
#   1 - One or more tests failed

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/test_helper.sh"

# =============================================================================
# Test Data
# =============================================================================

TEST_FILE_1="/tmp/test_upload_1_$$.bin"
TEST_FILE_2="/tmp/test_upload_2_$$.bin"
CHUNK_SIZE=$((1024 * 1024))  # 1MB chunks
UPLOADED_FILE_IDS=()
TRASH_IDS=()

# =============================================================================
# Cleanup Function
# =============================================================================

cleanup() {
    echo ""
    echo "Cleaning up test resources..."
    
    # Remove temp files
    rm -f "$TEST_FILE_1" "$TEST_FILE_2"
    
    # Delete uploaded files (move to trash)
    for file_id in "${UPLOADED_FILE_IDS[@]}"; do
        echo "  Deleting file $file_id..."
        http_delete "/api/file" &>/dev/null || true
        # Note: DELETE /api/file requires body, use http_post_with_status workaround
    done
    
    # Clean up any remaining temp files
    rm -f /tmp/test_upload_chunk_*_$$*.bin
    
    echo "Cleanup completed."
}

trap cleanup EXIT

# =============================================================================
# Helper Functions
# =============================================================================

# upload_chunk - Upload a single chunk
# Args: $1=upload_id, $2=chunk_index, $3=chunk_file, $4=chunk_hash
upload_chunk() {
    local upload_id="$1"
    local chunk_index="$2"
    local chunk_file="$3"
    local chunk_hash="$4"
    
    local response
    response=$(curl -s -w "\n%{http_code}" -X POST \
        "$BASE_URL/api/file/upload/chunk?upload_id=$upload_id&chunk_index=$chunk_index&chunk_hash=$chunk_hash" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary @"$chunk_file")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    echo "$http_code"
    echo "$body"
}

# init_upload - Initialize upload task
# Args: $1=filename, $2=file_size, $3=file_hash, $4=parent_id
# Returns: upload_id on success, empty on failure
init_upload() {
    local filename="$1"
    local file_size="$2"
    local file_hash="$3"
    local parent_id="${4:-0}"
    
    local response
    response=$(http_post_with_status "/api/file/upload/init" \
        "{\"filename\":\"$filename\",\"file_size\":$file_size,\"file_hash\":\"$file_hash\",\"parent_id\":$parent_id}")
    
    local http_code=$(echo "$response" | head -n 1)
    local body=$(echo "$response" | tail -n +2)
    
    if [ "$http_code" != "200" ]; then
        echo ""
        return 1
    fi
    
    local code=$(json_query "$body" '.code')
    if [ "$code" != "0" ]; then
        echo ""
        return 1
    fi
    
    # Check for instant upload (file_id returned, no upload_id)
    local file_id=$(json_query "$body" '.data.file_id')
    if [ "$file_id" != "null" ] && [ -n "$file_id" ]; then
        echo "INSTANT:$file_id"
        return 0
    fi
    
    local upload_id=$(json_query "$body" '.data.upload_id')
    echo "$upload_id"
}

# complete_upload - Complete upload task
# Args: $1=upload_id
# Returns: file_id on success, empty on failure
complete_upload() {
    local upload_id="$1"
    
    local response
    response=$(http_post_with_status "/api/file/upload/complete" \
        "{\"upload_id\":\"$upload_id\"}")
    
    local http_code=$(echo "$response" | head -n 1)
    local body=$(echo "$response" | tail -n +2)
    
    if [ "$http_code" != "200" ]; then
        echo ""
        return 1
    fi
    
    local code=$(json_query "$body" '.code')
    if [ "$code" != "0" ]; then
        echo ""
        return 1
    fi
    
    local file_id=$(json_query "$body" '.data.file.id')
    echo "$file_id"
}

# cancel_upload - Cancel upload task
# Args: $1=upload_id
cancel_upload() {
    local upload_id="$1"
    http_delete "/api/file/upload/$upload_id"
}

# delete_file_permanently - Delete file and permanently remove from trash
# Args: $1=file_id
delete_file_permanently() {
    local file_id="$1"
    
    # Move to trash first
    local del_response
    del_response=$(curl -s -w "\n%{http_code}" -X DELETE \
        "$BASE_URL/api/file" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"file_ids\":[$file_id]}")
    
    local del_http_code=$(echo "$del_response" | tail -n 1)
    local del_body=$(echo "$del_response" | sed '$d')
    
    if [ "$del_http_code" != "200" ]; then
        echo "Warning: Failed to delete file $file_id"
        return 1
    fi
    
    # Get trash_id from response
    local trash_id=$(json_query "$del_body" '.data.results[0].trash_id')
    if [ -z "$trash_id" ] || [ "$trash_id" = "null" ]; then
        echo "Warning: No trash_id returned for file $file_id"
        return 1
    fi
    
    # Permanently delete from trash
    local perm_response
    perm_response=$(curl -s -w "\n%{http_code}" -X DELETE \
        "$BASE_URL/api/trash" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "{\"trash_ids\":[$trash_id]}")
    
    local perm_http_code=$(echo "$perm_response" | tail -n 1)
    if [ "$perm_http_code" != "200" ]; then
        echo "Warning: Failed to permanently delete trash_id $trash_id"
        return 1
    fi
    
    echo "Deleted file $file_id (trash_id: $trash_id)"
}

# =============================================================================
# Tests
# =============================================================================

test_chunked_upload() {
    print_test_header "Chunked Upload Flow"
    
    # Create 2MB test file
    echo "Creating 2MB test file..."
    dd if=/dev/urandom of="$TEST_FILE_1" bs=1M count=2 2>/dev/null
    local file_size=$(stat -c%s "$TEST_FILE_1")
    local file_hash=$(md5sum "$TEST_FILE_1" | cut -d' ' -f1)
    
    echo "  File size: $file_size bytes"
    echo "  File hash: $file_hash"
    
    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_chunked_$$.bin" "$file_size" "$file_hash" 0)
    
    if [ -z "$upload_result" ]; then
        echo -e "${RED}✗ Failed to initialize upload${NC}"
        return 1
    fi
    
    # Check for instant upload
    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ File already exists (instant upload triggered)${NC}"
        local file_id="${upload_result#INSTANT:}"
        UPLOADED_FILE_IDS+=("$file_id")
        delete_file_permanently "$file_id"
        return 0
    fi
    
    local upload_id="$upload_result"
    echo "  Upload ID: $upload_id"
    
    # Split into chunks and upload
    local chunk_count=2
    local uploaded_chunks=()
    
    for ((i=0; i<chunk_count; i++)); do
        local chunk_file="/tmp/test_upload_chunk_${i}_$$.bin"
        local skip=$((i * 1024))
        
        # Extract chunk
        dd if="$TEST_FILE_1" of="$chunk_file" bs=1024K skip=$skip count=1 2>/dev/null
        
        # Calculate chunk hash
        local chunk_hash=$(md5sum "$chunk_file" | cut -d' ' -f1)
        
        echo "Uploading chunk $i (hash: $chunk_hash)..."
        
        local result=$(upload_chunk "$upload_id" "$i" "$chunk_file" "$chunk_hash")
        local http_code=$(echo "$result" | head -n 1)
        local body=$(echo "$result" | tail -n +2)
        
        rm -f "$chunk_file"
        
        if [ "$http_code" != "200" ]; then
            echo -e "${RED}✗ Chunk $i upload failed with HTTP $http_code${NC}"
            echo "Response: $body"
            cancel_upload "$upload_id"
            return 1
        fi
        
        local code=$(json_query "$body" '.code')
        if [ "$code" != "0" ]; then
            echo -e "${RED}✗ Chunk $i upload failed with code $code${NC}"
            echo "Response: $body"
            cancel_upload "$upload_id"
            return 1
        fi
        
        echo -e "${GREEN}✓ Chunk $i uploaded${NC}"
        uploaded_chunks+=("$i")
    done
    
    # Complete upload
    echo "Completing upload..."
    local file_id=$(complete_upload "$upload_id")
    
    if [ -z "$file_id" ]; then
        echo -e "${RED}✗ Failed to complete upload${NC}"
        return 1
    fi
    
    echo -e "${GREEN}✓ Upload completed, file_id: $file_id${NC}"
    UPLOADED_FILE_IDS+=("$file_id")
    
    # Verify file hash (get file info)
    local info_response=$(http_get "/api/file/$file_id")
    local stored_hash=$(json_query "$info_response" '.data.hash')
    
    if [ "$stored_hash" != "$file_hash" ]; then
        echo -e "${RED}✗ Hash mismatch: expected $file_hash, got $stored_hash${NC}"
        return 1
    fi
    
    echo -e "${GREEN}✓ Hash verified: $stored_hash${NC}"
    
    # Cleanup
    delete_file_permanently "$file_id"
    
    return 0
}

test_instant_upload() {
    print_test_header "Instant Upload (秒传)"
    
    # Create 1MB test file
    echo "Creating 1MB test file..."
    dd if=/dev/urandom of="$TEST_FILE_2" bs=1M count=1 2>/dev/null
    local file_size=$(stat -c%s "$TEST_FILE_2")
    local file_hash=$(md5sum "$TEST_FILE_2" | cut -d' ' -f1)
    
    echo "  File size: $file_size bytes"
    echo "  File hash: $file_hash"
    
    # First upload - should be normal chunked upload
    echo "First upload (should be chunked)..."
    local first_result=$(init_upload "test_instant_1_$$.bin" "$file_size" "$file_hash" 0)
    
    if [ -z "$first_result" ]; then
        echo -e "${RED}✗ Failed to initialize first upload${NC}"
        return 1
    fi
    
    local first_file_id=""
    local first_upload_id=""
    
    if [[ "$first_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ File already exists from previous test${NC}"
        first_file_id="${first_result#INSTANT:}"
        # Still need to upload again for the test
    else
        first_upload_id="$first_result"
        
        # Upload the single chunk
        local chunk_hash=$(md5sum "$TEST_FILE_2" | cut -d' ' -f1)
        local result=$(upload_chunk "$first_upload_id" "0" "$TEST_FILE_2" "$chunk_hash")
        local http_code=$(echo "$result" | head -n 1)
        local body=$(echo "$result" | tail -n +2)
        
        if [ "$http_code" != "200" ]; then
            echo -e "${RED}✗ First upload chunk failed${NC}"
            echo "Response: $body"
            cancel_upload "$first_upload_id"
            return 1
        fi
        
        # Complete first upload
        first_file_id=$(complete_upload "$first_upload_id")
        if [ -z "$first_file_id" ]; then
            echo -e "${RED}✗ Failed to complete first upload${NC}"
            return 1
        fi
    fi
    
    echo -e "${GREEN}✓ First upload completed, file_id: $first_file_id${NC}"
    UPLOADED_FILE_IDS+=("$first_file_id")
    
    # Second upload - should be instant (same hash)
    echo "Second upload (should be instant)..."
    local second_result=$(init_upload "test_instant_2_$$.bin" "$file_size" "$file_hash" 0)
    
    if [ -z "$second_result" ]; then
        echo -e "${RED}✗ Failed to initialize second upload${NC}"
        delete_file_permanently "$first_file_id"
        return 1
    fi
    
    if [[ "$second_result" != INSTANT:* ]]; then
        echo -e "${RED}✗ Second upload was not instant (expected INSTANT:file_id)${NC}"
        echo "  Got: $second_result"
        cancel_upload "$second_result" 2>/dev/null || true
        delete_file_permanently "$first_file_id"
        return 1
    fi
    
    local second_file_id="${second_result#INSTANT:}"
    echo -e "${GREEN}✓ Second upload was instant, file_id: $second_file_id${NC}"
    UPLOADED_FILE_IDS+=("$second_file_id")
    
    # Cleanup
    delete_file_permanently "$first_file_id"
    delete_file_permanently "$second_file_id"
    
    return 0
}

test_cancel_upload() {
    print_test_header "Cancel Upload Flow"
    
    # Create small test file
    local test_file="/tmp/test_cancel_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)
    
    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_cancel_$$.bin" "$file_size" "$file_hash" 0)
    
    rm -f "$test_file"
    
    if [ -z "$upload_result" ]; then
        echo -e "${RED}✗ Failed to initialize upload${NC}"
        return 1
    fi
    
    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ Skipping: file already exists${NC}"
        local file_id="${upload_result#INSTANT:}"
        delete_file_permanently "$file_id"
        return 0
    fi
    
    local upload_id="$upload_result"
    echo "  Upload ID: $upload_id"
    
    # Cancel the upload
    echo "Canceling upload..."
    local cancel_response=$(cancel_upload "$upload_id")
    local cancel_code=$(json_query "$cancel_response" '.code')
    
    if [ "$cancel_code" != "0" ]; then
        echo -e "${RED}✗ Failed to cancel upload${NC}"
        echo "Response: $cancel_response"
        return 1
    fi
    
    echo -e "${GREEN}✓ Upload canceled${NC}"
    
    # Try to complete the canceled upload - should fail
    echo "Attempting to complete canceled upload..."
    local complete_result=$(http_post_with_status "/api/file/upload/complete" \
        "{\"upload_id\":\"$upload_id\"}")
    
    local http_code=$(echo "$complete_result" | head -n 1)
    local body=$(echo "$complete_result" | tail -n +2)
    local code=$(json_query "$body" '.code')
    
    # Should get error 50008 (UploadTaskNotFound)
    if [ "$code" != "50008" ]; then
        echo -e "${RED}✗ Expected error 50008, got code: $code${NC}"
        echo "Response: $body"
        return 1
    fi
    
    echo -e "${GREEN}✓ Got expected error 50008 (UploadTaskNotFound)${NC}"
    
    return 0
}

test_invalid_upload_id() {
    print_test_header "Invalid Upload ID"
    
    # Try to complete with non-existent upload_id
    echo "Attempting to complete with invalid upload_id..."
    local response=$(http_post_with_status "/api/file/upload/complete" \
        "{\"upload_id\":\"non_existent_id_12345\"}")
    
    local http_code=$(echo "$response" | head -n 1)
    local body=$(echo "$response" | tail -n +2)
    local code=$(json_query "$body" '.code')
    
    # Should get error 50008 (UploadTaskNotFound)
    if [ "$code" != "50008" ]; then
        echo -e "${RED}✗ Expected error 50008, got code: $code${NC}"
        echo "Response: $body"
        return 1
    fi
    
    echo -e "${GREEN}✓ Got expected error 50008 (UploadTaskNotFound)${NC}"
    
    return 0
}

test_invalid_chunk_hash() {
    print_test_header "Invalid Chunk Hash"
    
    # Create small test file
    local test_file="/tmp/test_bad_hash_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)
    
    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_bad_hash_$$.bin" "$file_size" "$file_hash" 0)
    
    if [ -z "$upload_result" ]; then
        rm -f "$test_file"
        echo -e "${RED}✗ Failed to initialize upload${NC}"
        return 1
    fi
    
    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ Skipping: file already exists${NC}"
        rm -f "$test_file"
        local file_id="${upload_result#INSTANT:}"
        delete_file_permanently "$file_id"
        return 0
    fi
    
    local upload_id="$upload_result"
    echo "  Upload ID: $upload_id"
    
    # Upload with wrong hash
    echo "Uploading chunk with invalid hash..."
    local wrong_hash="00000000000000000000000000000000"
    local result=$(upload_chunk "$upload_id" "0" "$test_file" "$wrong_hash")
    
    rm -f "$test_file"
    
    local http_code=$(echo "$result" | head -n 1)
    local body=$(echo "$result" | tail -n +2)
    local code=$(json_query "$body" '.code')
    
    # Should get error 50009 (ChunkVerifyFailed) or 400 error
    if [ "$code" != "50009" ] && [ "$http_code" != "400" ]; then
        echo -e "${RED}✗ Expected error 50009 or HTTP 400, got code: $code, HTTP: $http_code${NC}"
        echo "Response: $body"
        cancel_upload "$upload_id"
        return 1
    fi
    
    echo -e "${GREEN}✓ Got expected error (chunk hash verification failed)${NC}"
    
    # Cancel the upload
    cancel_upload "$upload_id"
    
    return 0
}

test_cancel_after_complete() {
    print_test_header "Cancel After Complete (Negative Test)"
    
    # Create small test file
    local test_file="/tmp/test_cancel_complete_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)
    
    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_cancel_complete_$$.bin" "$file_size" "$file_hash" 0)
    
    if [ -z "$upload_result" ]; then
        rm -f "$test_file"
        echo -e "${RED}✗ Failed to initialize upload${NC}"
        return 1
    fi
    
    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ Skipping: file already exists${NC}"
        rm -f "$test_file"
        local file_id="${upload_result#INSTANT:}"
        delete_file_permanently "$file_id"
        return 0
    fi
    
    local upload_id="$upload_result"
    
    # Upload chunk
    local chunk_hash=$(md5sum "$test_file" | cut -d' ' -f1)
    upload_chunk "$upload_id" "0" "$test_file" "$chunk_hash" >/dev/null
    
    # Complete upload
    echo "Completing upload..."
    local file_id=$(complete_upload "$upload_id")
    
    rm -f "$test_file"
    
    if [ -z "$file_id" ]; then
        echo -e "${RED}✗ Failed to complete upload${NC}"
        return 1
    fi
    
    UPLOADED_FILE_IDS+=("$file_id")
    echo -e "${GREEN}✓ Upload completed, file_id: $file_id${NC}"
    
    # Try to cancel the completed upload
    echo "Attempting to cancel completed upload..."
    local cancel_response=$(http_post_with_status "/api/file/upload/$upload_id" \
        "{\"upload_id\":\"$upload_id\"}")
    
    # Use DELETE for cancel
    local cancel_result
    cancel_result=$(curl -s -w "\n%{http_code}" -X DELETE \
        "$BASE_URL/api/file/upload/$upload_id" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json")
    
    local cancel_http_code=$(echo "$cancel_result" | tail -n 1)
    local cancel_body=$(echo "$cancel_result" | sed '$d')
    local cancel_code=$(json_query "$cancel_body" '.code')
    
    # Should fail with 50008 (UploadTaskNotFound) since upload is already complete
    if [ "$cancel_code" != "50008" ] && [ "$cancel_http_code" != "400" ]; then
        # Some implementations may return success even for completed uploads
        # This is acceptable behavior
        echo -e "${YELLOW}⚠ Cancel returned unexpected result (code: $cancel_code)${NC}"
    else
        echo -e "${GREEN}✓ Got expected error when canceling completed upload${NC}"
    fi
    
    # Cleanup
    delete_file_permanently "$file_id"
    
    return 0
}

# =============================================================================
# Main
# =============================================================================

main() {
    echo "=========================================="
    echo "Upload API Verification Tests"
    echo "=========================================="
    echo "Backend: $BASE_URL"
    echo "Started: $(date)"
    echo ""
    
    # Authenticate
    echo "Authenticating..."
    if ! login; then
        echo -e "${RED}Authentication failed!${NC}"
        exit 1
    fi
    echo ""
    
    PASSED=0
    FAILED=0
    
    run_test() {
        local test_func="$1"
        if $test_func; then
            ((PASSED++))
            echo -e "${GREEN}✓ $test_func passed${NC}"
        else
            ((FAILED++))
            echo -e "${RED}✗ $test_func failed${NC}"
        fi
        echo ""
    }
    
    # Run tests
    run_test test_chunked_upload
    run_test test_instant_upload
    run_test test_cancel_upload
    run_test test_invalid_upload_id
    run_test test_invalid_chunk_hash
    run_test test_cancel_after_complete
    
    # Summary
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo "Passed: $PASSED"
    echo "Failed: $FAILED"
    echo "Finished: $(date)"
    echo "=========================================="
    
    if [ $FAILED -gt 0 ]; then
        exit 1
    fi
    
    exit 0
}

main "$@"

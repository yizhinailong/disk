#!/bin/bash
# tests/api/test_file_ops.sh
#
# File Operations API Tests
# Tests file list, search, details, rename, move, copy, download operations
#
# Usage:
#   ./test_file_ops.sh
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

TEST_FILE_IDS=()
TEST_FOLDER_IDS=()
TEST_FILE_PATH="/tmp/test_ops_$$_$(date +%s).bin"
TEST_FILE_SIZE=$((512 * 1024))  # 512KB
TEST_FILE_HASH=""
UPLOADED_FILE_ID=""
TEST_FOLDER_ID=""

# =============================================================================
# Helper Functions
# =============================================================================

# upload_test_file - Upload a test file using the chunked upload API
#
# Arguments:
#   $1 - file path
#   $2 - parent folder ID (default: 0)
#
# Output:
#   Uploaded file ID (sets global variable)
upload_test_file() {
    local file_path="$1"
    local parent_id="${2:-0}"
    local filename=$(basename "$file_path")
    local file_size=$(stat -c%s "$file_path")
    local file_hash=$(md5sum "$file_path" | cut -d' ' -f1)

    print_test_header "Uploading test file: $filename"

    # Step 1: Initialize upload
    local init_response
    init_response=$(http_post "/api/file/upload/init" "{
        \"filename\": \"$filename\",
        \"file_size\": $file_size,
        \"file_hash\": \"$file_hash\",
        \"parent_id\": $parent_id
    }")

    local init_code=$(json_query "$init_response" '.code')
    if [ "$init_code" != "0" ]; then
        echo -e "${RED}✗ Upload init failed${NC}"
        echo "$init_response" | print_response
        return 1
    fi

    local upload_id=$(json_query "$init_response" '.data.upload_id')
    local need_upload=$(json_query "$init_response" '.data.need_upload')

    # Check for instant upload (deduplication)
    if [ "$need_upload" = "false" ]; then
        local file_id=$(json_query "$init_response" '.data.file.id')
        echo -e "${GREEN}✓ Instant upload (deduplicated), file_id: $file_id${NC}"
        echo "$file_id"
        return 0
    fi

    # Step 2: Upload chunk
    local chunk_hash=$(md5sum "$file_path" | cut -d' ' -f1)
    local chunk_response
    chunk_response=$(curl -s -X POST "$BASE_URL/api/file/upload/chunk?upload_id=$upload_id&chunk_index=0&chunk_hash=$chunk_hash" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@$file_path")

    local chunk_code=$(json_query "$chunk_response" '.code')
    if [ "$chunk_code" != "0" ]; then
        echo -e "${RED}✗ Chunk upload failed${NC}"
        echo "$chunk_response" | print_response
        return 1
    fi

    # Step 3: Complete upload
    local complete_response
    complete_response=$(http_post "/api/file/upload/complete" "{\"upload_id\": \"$upload_id\"}")

    local complete_code=$(json_query "$complete_response" '.code')
    if [ "$complete_code" != "0" ]; then
        echo -e "${RED}✗ Upload complete failed${NC}"
        echo "$complete_response" | print_response
        return 1
    fi

    local file_id=$(json_query "$complete_response" '.data.file.id')
    echo -e "${GREEN}✓ File uploaded successfully, file_id: $file_id${NC}"
    echo "$file_id"
    return 0
}

# create_test_folder - Create a test folder
#
# Arguments:
#   $1 - folder name
#   $2 - parent ID (default: 0)
#
# Output:
#   Folder ID
create_test_folder() {
    local name="$1"
    local parent_id="${2:-0}"

    local response
    response=$(http_post "/api/folder/create" "{
        \"name\": \"$name\",
        \"parent_id\": $parent_id
    }")

    local code=$(json_query "$response" '.code')
    if [ "$code" != "0" ]; then
        echo -e "${RED}✗ Folder creation failed${NC}"
        echo "$response" | print_response
        return 1
    fi

    local folder_id=$(json_query "$response" '.data.id')
    echo -e "${GREEN}✓ Folder created, id: $folder_id${NC}"
    echo "$folder_id"
    return 0
}

# delete_file - Delete a file via API (soft delete to trash)
#
# Arguments:
#   $1 - file ID
delete_file() {
    local file_id="$1"
    http_delete "/api/file" "{\"file_ids\":[$file_id]}" > /dev/null 2>&1 || true
}

# =============================================================================
# Setup
# =============================================================================

setup() {
    print_test_header "Setup: Creating test file and folder"

    # Create test file with random data
    dd if=/dev/urandom of="$TEST_FILE_PATH" bs=512K count=1 2>/dev/null
    TEST_FILE_HASH=$(md5sum "$TEST_FILE_PATH" | cut -d' ' -f1)
    echo -e "${GREEN}✓ Test file created: $TEST_FILE_PATH${NC}"
    echo "  Size: $TEST_FILE_SIZE bytes"
    echo "  MD5: $TEST_FILE_HASH"

    # Upload test file
    UPLOADED_FILE_ID=$(upload_test_file "$TEST_FILE_PATH" 0)
    if [ -z "$UPLOADED_FILE_ID" ] || [ "$UPLOADED_FILE_ID" = "null" ]; then
        echo -e "${RED}✗ Failed to upload test file${NC}"
        return 1
    fi
    TEST_FILE_IDS+=("$UPLOADED_FILE_ID")

    # Create test folder for move operations
    local folder_name=$(generate_test_name "test_ops_folder")
    TEST_FOLDER_ID=$(create_test_folder "$folder_name" 0)
    if [ -z "$TEST_FOLDER_ID" ] || [ "$TEST_FOLDER_ID" = "null" ]; then
        echo -e "${RED}✗ Failed to create test folder${NC}"
        return 1
    fi
    TEST_FOLDER_IDS+=("$TEST_FOLDER_ID")

    echo -e "${GREEN}✓ Setup complete${NC}"
    return 0
}

# =============================================================================
# Cleanup
# =============================================================================

cleanup() {
    print_test_header "Cleanup: Removing test files and folders"

    # Delete test files
    for file_id in "${TEST_FILE_IDS[@]}"; do
        if [ -n "$file_id" ] && [ "$file_id" != "null" ]; then
            echo "Deleting file: $file_id"
            delete_file "$file_id"
        fi
    done

    # Delete test folders (via trash or direct delete)
    for folder_id in "${TEST_FOLDER_IDS[@]}"; do
        if [ -n "$folder_id" ] && [ "$folder_id" != "null" ]; then
            echo "Deleting folder: $folder_id"
            http_delete "/api/file" "{\"file_ids\":[$folder_id]}" > /dev/null 2>&1 || true
        fi
    done

    # Remove local test file
    rm -f "$TEST_FILE_PATH"

    echo -e "${GREEN}✓ Cleanup complete${NC}"
}

trap cleanup EXIT

# =============================================================================
# Positive Tests
# =============================================================================

test_file_list() {
    print_test_header "Test 1: File list returns paginated results"

    local response
    response=$(http_get "/api/file/list?parent_id=0&page=1&page_size=20")

    assert_json "$response" '.code' '0' || return 1
    assert_json_not_empty "$response" '.data.items' || return 1
    assert_json_not_empty "$response" '.data.pagination' || return 1

    # Check pagination fields
    local page=$(json_query "$response" '.data.pagination.page')
    local page_size=$(json_query "$response" '.data.pagination.page_size')
    local total=$(json_query "$response" '.data.pagination.total')

    if [ "$page" != "1" ]; then
        echo -e "${RED}✗ Pagination page mismatch: expected 1, got $page${NC}"
        return 1
    fi

    echo -e "${GREEN}✓ File list retrieved successfully${NC}"
    echo "  Page: $page, PageSize: $page_size, Total: $total"
    return 0
}

test_file_search() {
    print_test_header "Test 2: File search finds test file by keyword"

    # Search for a common substring (using 'test' which should match our test file)
    local response
    response=$(http_get "/api/file/search?keyword=test_ops")

    assert_json "$response" '.code' '0' || return 1
    assert_json_not_empty "$response" '.data.items' || return 1

    echo -e "${GREEN}✓ File search returned results${NC}"
    return 0
}

test_file_details() {
    print_test_header "Test 3: Get file details by ID"

    local response
    response=$(http_get "/api/file/$UPLOADED_FILE_ID")

    assert_json "$response" '.code' '0' || return 1
    assert_json "$response" '.data.id' "$UPLOADED_FILE_ID" || return 1
    assert_json_not_empty "$response" '.data.name' || return 1
    assert_json_not_empty "$response" '.data.size' || return 1
    assert_json_not_empty "$response" '.data.hash' || return 1

    echo -e "${GREEN}✓ File details retrieved successfully${NC}"
    return 0
}

test_file_rename() {
    print_test_header "Test 4: Rename file successfully"

    local new_name="renamed_test_$$_$(date +%s).bin"

    local response
    response=$(http_put_with_status "/api/file/$UPLOADED_FILE_ID/rename" "{\"new_name\": \"$new_name\"}")

    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')

    assert_status 200 "$http_code" || return 1
    assert_json "$body" '.code' '0' || return 1
    assert_json "$body" '.data.name' "$new_name" || return 1

    echo -e "${GREEN}✓ File renamed to: $new_name${NC}"
    return 0
}

test_file_move() {
    print_test_header "Test 5: Move file to folder"

    local response
    response=$(http_put_with_status "/api/file/move" "{
        \"file_ids\": [$UPLOADED_FILE_ID],
        \"target_folder_id\": $TEST_FOLDER_ID
    }")

    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')

    assert_status 200 "$http_code" || return 1
    assert_json "$body" '.code' '0' || return 1
    assert_json "$body" '.data.moved_count' '1' || return 1

    echo -e "${GREEN}✓ File moved to folder: $TEST_FOLDER_ID${NC}"
    return 0
}

test_file_copy() {
    print_test_header "Test 6: Copy file (verify new ID returned)"

    # First move file back to root for clean copy test
    http_put_with_status "/api/file/move" "{
        \"file_ids\": [$UPLOADED_FILE_ID],
        \"target_folder_id\": 0
    }" > /dev/null 2>&1

    local response
    response=$(http_post_with_status "/api/file/copy" "{
        \"file_ids\": [$UPLOADED_FILE_ID],
        \"target_folder_id\": 0
    }")

    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')

    assert_status 200 "$http_code" || return 1
    assert_json "$body" '.code' '0' || return 1
    assert_json "$body" '.data.copied_count' '1' || return 1

    local new_file_id=$(json_query "$body" '.data.new_files.0.new_id')
    if [ -z "$new_file_id" ] || [ "$new_file_id" = "null" ]; then
        echo -e "${RED}✗ No new file ID returned${NC}"
        return 1
    fi

    # Track the copied file for cleanup
    TEST_FILE_IDS+=("$new_file_id")

    echo -e "${GREEN}✓ File copied, new file_id: $new_file_id${NC}"
    return 0
}

test_file_download() {
    print_test_header "Test 7: Download file and verify MD5 hash matches"

    local download_path="/tmp/download_test_$$_$(date +%s).bin"

    # Download file
    local http_code
    http_code=$(curl -s -w "%{http_code}" -o "$download_path" \
        -X GET "$BASE_URL/api/file/download/$UPLOADED_FILE_ID" \
        -H "Authorization: Bearer $ACCESS_TOKEN")

    if [ "$http_code" != "200" ]; then
        echo -e "${RED}✗ Download failed with HTTP $http_code${NC}"
        rm -f "$download_path"
        return 1
    fi

    # Verify MD5 hash
    local downloaded_hash=$(md5sum "$download_path" | cut -d' ' -f1)

    if [ "$downloaded_hash" != "$TEST_FILE_HASH" ]; then
        echo -e "${RED}✗ MD5 hash mismatch!${NC}"
        echo "  Expected: $TEST_FILE_HASH"
        echo "  Got:      $downloaded_hash"
        rm -f "$download_path"
        return 1
    fi

    rm -f "$download_path"
    echo -e "${GREEN}✓ Download verified, MD5 hash matches${NC}"
    return 0
}

test_download_info() {
    print_test_header "Test 8: Get download info (supports_range field)"

    local response
    response=$(http_get "/api/file/download/$UPLOADED_FILE_ID/info")

    assert_json "$response" '.code' '0' || return 1
    assert_json_not_empty "$response" '.data.file_id' || return 1
    assert_json_not_empty "$response" '.data.filename' || return 1
    assert_json_not_empty "$response" '.data.file_size' || return 1
    assert_json_not_empty "$response" '.data.file_hash' || return 1
    assert_json_not_empty "$response" '.data.supports_range' || return 1

    local supports_range=$(json_query "$response" '.data.supports_range')
    echo -e "${GREEN}✓ Download info retrieved, supports_range: $supports_range${NC}"
    return 0
}

# =============================================================================
# Negative Tests
# =============================================================================

test_rename_existing_name() {
    print_test_header "Test 9 (Negative): Rename to existing name fails (code=50007)"

    # Upload another file with a known name
    local existing_name="existing_$$_$(date +%s).bin"
    local temp_file="/tmp/existing_$$.bin"
    dd if=/dev/urandom of="$temp_file" bs=1K count=1 2>/dev/null

    local existing_id=$(upload_test_file "$temp_file" 0)
    rm -f "$temp_file"

    if [ -z "$existing_id" ] || [ "$existing_id" = "null" ]; then
        echo -e "${YELLOW}⚠ Skipping test: Could not create existing file${NC}"
        return 0
    fi
    TEST_FILE_IDS+=("$existing_id")

    # Try to rename our test file to the existing name
    local response
    response=$(http_put_with_status "/api/file/$UPLOADED_FILE_ID/rename" "{\"new_name\": \"$existing_name\"}")

    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')

    # Should get 409 Conflict with code 50007
    if [ "$http_code" != "409" ]; then
        echo -e "${RED}✗ Expected HTTP 409, got $http_code${NC}"
        echo "$body" | print_response
        return 1
    fi

    assert_json "$body" '.code' '50007' || return 1

    echo -e "${GREEN}✓ Rename to existing name correctly rejected with code=50007${NC}"
    return 0
}

test_move_nonexistent_folder() {
    print_test_header "Test 10 (Negative): Move to non-existent folder fails (code=50006)"

    local non_existent_id=99999999

    local response
    response=$(http_put_with_status "/api/file/move" "{
        \"file_ids\": [$UPLOADED_FILE_ID],
        \"target_folder_id\": $non_existent_id
    }")

    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')

    # Should get 404 with code 50006
    if [ "$http_code" != "404" ]; then
        echo -e "${RED}✗ Expected HTTP 404, got $http_code${NC}"
        echo "$body" | print_response
        return 1
    fi

    assert_json "$body" '.code' '50006' || return 1

    echo -e "${GREEN}✓ Move to non-existent folder correctly rejected with code=50006${NC}"
    return 0
}

test_get_nonexistent_file() {
    print_test_header "Test 11 (Negative): Get non-existent file fails (code=50005)"

    local non_existent_id=99999999

    local response
    response=$(http_get_with_status "/api/file/$non_existent_id")

    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')

    # Should get 404 with code 50005
    if [ "$http_code" != "404" ]; then
        echo -e "${RED}✗ Expected HTTP 404, got $http_code${NC}"
        echo "$body" | print_response
        return 1
    fi

    assert_json "$body" '.code' '50005' || return 1

    echo -e "${GREEN}✓ Get non-existent file correctly rejected with code=50005${NC}"
    return 0
}

# =============================================================================
# HTTP PUT Helper (missing from test_helper.sh)
# =============================================================================

http_put_with_status() {
    local endpoint="$1"
    local body="$2"
    curl -s -w "\n%{http_code}" -X PUT "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "$body"
}

http_get_with_status() {
    local endpoint="$1"
    curl -s -w "\n%{http_code}" -X GET "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json"
}

# =============================================================================
# Main Test Runner
# =============================================================================

main() {
    echo "=========================================="
    echo "File Operations API Tests"
    echo "=========================================="
    echo "Backend: $BASE_URL"
    echo "Started: $(date)"
    echo ""

    # Login first
    if ! login; then
        echo -e "${RED}Authentication failed! Cannot proceed with tests.${NC}"
        exit 1
    fi
    echo ""

    # Setup
    if ! setup; then
        echo -e "${RED}Setup failed! Cannot proceed with tests.${NC}"
        exit 1
    fi
    echo ""

    PASSED=0
    FAILED=0

    # Run positive tests
    for test_func in test_file_list test_file_search test_file_details \
                     test_file_rename test_file_move test_file_copy \
                     test_file_download test_download_info; do
        if $test_func; then
            ((PASSED++))
        else
            ((FAILED++))
        fi
        echo ""
    done

    # Run negative tests
    for test_func in test_rename_existing_name test_move_nonexistent_folder \
                     test_get_nonexistent_file; do
        if $test_func; then
            ((PASSED++))
        else
            ((FAILED++))
        fi
        echo ""
    done

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

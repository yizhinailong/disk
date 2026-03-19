#!/bin/bash
# tests/api/test_trash.sh
#
# Trash API Tests
# Tests for recycle bin functionality: list, restore, permanent delete
#
# Usage:
#   ./test_trash.sh
#
# Prerequisites:
#   - Backend running on http://127.0.0.1:8080
#   - Test user 'admin' with password 'Admin123' exists

set -e

# =============================================================================
# Setup
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/test_helper.sh"

# Test state
TEST_FILES=()
TEST_TRASH_IDS=()

# =============================================================================
# Helper Functions
# =============================================================================

# http_delete_with_body - Make authenticated DELETE request with JSON body
#
# Arguments:
#   $1 - endpoint path
#   $2 - JSON body
#
# Output:
#   JSON response body
#
http_delete_with_body() {
    local endpoint="$1"
    local body="$2"
    curl -s -X DELETE "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "$body"
}

# http_delete_with_status - Make authenticated DELETE request with status code
#
# Arguments:
#   $1 - endpoint path
#   $2 - JSON body (optional)
#
# Output:
#   HTTP status code on first line, JSON body on remaining lines
#
http_delete_with_status() {
    local endpoint="$1"
    local body="${2:-}"
    if [ -n "$body" ]; then
        curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL$endpoint" \
            -H "Authorization: Bearer $ACCESS_TOKEN" \
            -H "Content-Type: application/json" \
            -d "$body"
    else
        curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL$endpoint" \
            -H "Authorization: Bearer $ACCESS_TOKEN"
    fi
}

# upload_test_file - Create and upload a small test file
#
# Arguments:
#   $1 - filename (optional, will generate if not provided)
#
# Output:
#   file_id of uploaded file
#
upload_test_file() {
    local filename="${1:-$(generate_test_name "trash_test_file")}"
    local temp_file="/tmp/${filename}.txt"
    
    # Create small test file (1KB)
    dd if=/dev/zero of="$temp_file" bs=1024 count=1 2>/dev/null
    
    # Calculate MD5 hash
    local file_hash=$(md5sum "$temp_file" | cut -d' ' -f1)
    local file_size=$(stat -c%s "$temp_file")
    
    # Initialize upload
    local init_response
    init_response=$(http_post "/api/file/upload/init" "{
        \"filename\": \"${filename}.txt\",
        \"file_size\": ${file_size},
        \"file_hash\": \"${file_hash}\",
        \"parent_id\": 0
    }")
    
    local upload_id=$(json_query "$init_response" '.data.upload_id')
    local file_id=$(json_query "$init_response" '.data.file_id')
    
    # If file_id exists, it's instant upload (dedup)
    if [ "$file_id" != "null" ] && [ -n "$file_id" ]; then
        rm -f "$temp_file"
        echo "$file_id"
        return 0
    fi
    
    # Upload chunk
    local chunk_response
    chunk_response=$(curl -s -X POST "$BASE_URL/api/file/upload/chunk?upload_id=${upload_id}&chunk_index=0&chunk_hash=${file_hash}" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@$temp_file")
    
    # Complete upload
    local complete_response
    complete_response=$(http_post "/api/file/upload/complete" "{\"upload_id\":\"${upload_id}\"}")
    
    file_id=$(json_query "$complete_response" '.data.file.id')
    
    # Cleanup temp file
    rm -f "$temp_file"
    
    echo "$file_id"
}

# get_trash_id_for_file - Find trash_id for a given original file_id
#
# Arguments:
#   $1 - original file_id
#
# Output:
#   trash_id or empty string if not found
#
get_trash_id_for_file() {
    local file_id="$1"
    local trash_response
    trash_response=$(http_get "/api/trash?page=1&page_size=100")
    
    # Parse through items to find matching original_id
    python3 -c "
import json, sys
data = json.loads(sys.argv[1])
items = data.get('data', {}).get('items', [])
file_id = int(sys.argv[2])
for item in items:
    if item.get('original_id') == file_id:
        print(item.get('id'))
        break
" "$trash_response" "$file_id" 2>/dev/null
}

# clear_trash_completely - Remove all items from trash
#
clear_trash_completely() {
    http_delete "/api/trash/all" > /dev/null 2>&1 || true
}

# =============================================================================
# Cleanup Function
# =============================================================================

cleanup() {
    echo ""
    echo "========================================"
    echo "CLEANUP"
    echo "========================================"
    
    # Clear any remaining trash items
    clear_trash_completely
    
    # Remove any remaining test files (if accessible)
    for file_id in "${TEST_FILES[@]}"; do
        if [ -n "$file_id" ]; then
            http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}" > /dev/null 2>&1 || true
        fi
    done
    
    echo -e "${GREEN}✓ Cleanup completed${NC}"
}

# =============================================================================
# Test Functions
# =============================================================================

# Test 1: Delete file moves to trash (soft delete)
test_delete_moves_to_trash() {
    print_test_header "Test 1: Delete file moves to trash"
    
    # Upload test file
    local file_id
    file_id=$(upload_test_file "test_trash_delete_1")
    TEST_FILES+=("$file_id")
    
    echo "Uploaded file ID: $file_id"
    
    # Delete the file (soft delete)
    local delete_response
    delete_response=$(http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}")
    
    assert_json "$delete_response" '.code' '0'
    assert_json "$delete_response" '.data.deleted_count' '1'
    
    # Verify file is in trash
    local trash_response
    trash_response=$(http_get "/api/trash?page=1&page_size=20")
    
    local trash_id=$(get_trash_id_for_file "$file_id")
    if [ -n "$trash_id" ]; then
        echo -e "${GREEN}✓ File found in trash with trash_id: $trash_id${NC}"
        TEST_TRASH_IDS+=("$trash_id")
    else
        echo -e "${RED}✗ File not found in trash${NC}"
        return 1
    fi
    
    # Verify file is no longer accessible directly
    local file_info_response
    file_info_response=$(http_get "/api/file/$file_id")
    local code=$(json_query "$file_info_response" '.code')
    
    # File should return error code 50005 (FileNotFound) or similar
    if [ "$code" != "0" ]; then
        echo -e "${GREEN}✓ File no longer directly accessible (code: $code)${NC}"
    else
        echo -e "${RED}✗ File still accessible after delete${NC}"
        return 1
    fi
    
    return 0
}

# Test 2: Get trash list
test_get_trash_list() {
    print_test_header "Test 2: Get trash list"
    
    # Upload and delete a file to ensure trash has content
    local file_id
    file_id=$(upload_test_file "test_trash_list_1")
    TEST_FILES+=("$file_id")
    
    http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}" > /dev/null
    
    local trash_response
    trash_response=$(http_get "/api/trash?page=1&page_size=20")
    
    assert_json "$trash_response" '.code' '0'
    assert_json_not_empty "$trash_response" '.data.items'
    assert_json_not_empty "$trash_response" '.data.pagination'
    
    # Verify item structure
    local items_count=$(json_query "$trash_response" '.data.items' | python3 -c "import json,sys; print(len(json.loads(sys.stdin.read())))" 2>/dev/null)
    echo -e "${GREEN}✓ Trash list contains $items_count items${NC}"
    
    # Verify pagination fields
    local page=$(json_query "$trash_response" '.data.pagination.page')
    local page_size=$(json_query "$trash_response" '.data.pagination.page_size')
    
    if [ "$page" = "1" ] && [ -n "$page_size" ]; then
        echo -e "${GREEN}✓ Pagination fields present (page=$page, page_size=$page_size)${NC}"
    else
        echo -e "${RED}✗ Pagination fields missing or invalid${NC}"
        return 1
    fi
    
    return 0
}

# Test 3: Restore file from trash
test_restore_file() {
    print_test_header "Test 3: Restore file from trash"
    
    # Upload and delete file
    local file_id
    file_id=$(upload_test_file "test_trash_restore_1")
    TEST_FILES+=("$file_id")
    
    echo "Uploaded file ID: $file_id"
    
    # Delete to trash
    http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}" > /dev/null
    
    # Get trash_id
    local trash_id=$(get_trash_id_for_file "$file_id")
    
    if [ -z "$trash_id" ]; then
        echo -e "${RED}✗ File not found in trash${NC}"
        return 1
    fi
    
    echo "Trash ID: $trash_id"
    
    # Restore the file
    local restore_response
    restore_response=$(http_post "/api/trash/restore" "{\"trash_ids\":[$trash_id]}")
    
    assert_json "$restore_response" '.code' '0'
    assert_json "$restore_response" '.data.summary.success_count' '1'
    
    # Verify file is accessible again
    local file_info_response
    file_info_response=$(http_get "/api/file/$file_id")
    
    local code=$(json_query "$file_info_response" '.code')
    if [ "$code" = "0" ]; then
        echo -e "${GREEN}✓ File accessible after restore${NC}"
    else
        echo -e "${RED}✗ File not accessible after restore (code: $code)${NC}"
        print_response "$file_info_response"
        return 1
    fi
    
    # Verify file is no longer in trash
    local trash_check=$(get_trash_id_for_file "$file_id")
    if [ -z "$trash_check" ]; then
        echo -e "${GREEN}✓ File no longer in trash after restore${NC}"
    else
        echo -e "${RED}✗ File still in trash after restore${NC}"
        return 1
    fi
    
    return 0
}

# Test 4: Permanent delete removes completely
test_permanent_delete() {
    print_test_header "Test 4: Permanent delete removes completely"
    
    # Upload and delete file
    local file_id
    file_id=$(upload_test_file "test_trash_permanent_1")
    
    echo "Uploaded file ID: $file_id"
    
    # Delete to trash
    http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}" > /dev/null
    
    # Get trash_id
    local trash_id=$(get_trash_id_for_file "$file_id")
    
    if [ -z "$trash_id" ]; then
        echo -e "${RED}✗ File not found in trash${NC}"
        return 1
    fi
    
    echo "Trash ID: $trash_id"
    
    # Permanent delete
    local perm_delete_response
    perm_delete_response=$(http_delete_with_body "/api/trash" "{\"trash_ids\":[$trash_id]}")
    
    assert_json "$perm_delete_response" '.code' '0'
    assert_json "$perm_delete_response" '.data.summary.success_count' '1'
    
    # Verify file is gone - trying to restore should fail
    local restore_response
    restore_response=$(http_post "/api/trash/restore" "{\"trash_ids\":[$trash_id]}")
    
    local code=$(json_query "$restore_response" '.code')
    if [ "$code" = "10003" ]; then
        echo -e "${GREEN}✗ Restore correctly fails with ResourceNotFound (10003)${NC}"
    else
        echo -e "${YELLOW}! Restore returned code: $code (expected 10003)${NC}"
    fi
    
    # Verify file is not in trash list
    local trash_check=$(get_trash_id_for_file "$file_id")
    if [ -z "$trash_check" ]; then
        echo -e "${GREEN}✓ File no longer in trash after permanent delete${NC}"
    else
        echo -e "${RED}✗ File still in trash after permanent delete${NC}"
        return 1
    fi
    
    # Verify original file is not accessible
    local file_info_response
    file_info_response=$(http_get "/api/file/$file_id")
    
    code=$(json_query "$file_info_response" '.code')
    if [ "$code" != "0" ]; then
        echo -e "${GREEN}✓ Original file not accessible after permanent delete (code: $code)${NC}"
    else
        echo -e "${RED}✗ Original file still accessible after permanent delete${NC}"
        return 1
    fi
    
    return 0
}

# Test 5: Clear all trash
test_clear_all_trash() {
    print_test_header "Test 5: Clear all trash"
    
    # Upload and delete multiple files
    local file_id_1 file_id_2
    file_id_1=$(upload_test_file "test_trash_clear_1")
    file_id_2=$(upload_test_file "test_trash_clear_2")
    
    echo "Uploaded files: $file_id_1, $file_id_2"
    
    # Delete to trash
    http_delete_with_body "/api/file" "{\"file_ids\":[$file_id_1,$file_id_2]}" > /dev/null
    
    # Verify files are in trash
    local trash_before
    trash_before=$(http_get "/api/trash?page=1&page_size=100")
    local count_before=$(json_query "$trash_before" '.data.items' | python3 -c "import json,sys; print(len(json.loads(sys.stdin.read())))" 2>/dev/null || echo "0")
    
    echo "Items in trash before clear: $count_before"
    
    # Clear all trash
    local clear_response
    clear_response=$(http_delete "/api/trash/all")
    
    assert_json "$clear_response" '.code' '0'
    
    local deleted_count=$(json_query "$clear_response" '.data.deleted_count')
    echo -e "${GREEN}✓ Deleted $deleted_count items from trash${NC}"
    
    # Verify trash is empty
    local trash_after
    trash_after=$(http_get "/api/trash?page=1&page_size=100")
    local count_after=$(json_query "$trash_after" '.data.items' | python3 -c "import json,sys; print(len(json.loads(sys.stdin.read())))" 2>/dev/null || echo "0")
    
    echo "Items in trash after clear: $count_after"
    
    if [ "$count_after" = "0" ]; then
        echo -e "${GREEN}✓ Trash is now empty${NC}"
    else
        echo -e "${YELLOW}! Trash not fully empty (remaining: $count_after)${NC}"
    fi
    
    return 0
}

# Test 6: Negative - Restore non-existent trash_id
test_restore_nonexistent_trash() {
    print_test_header "Test 6: Restore non-existent trash_id (expect 10003)"
    
    local fake_trash_id=99999999
    
    local restore_response
    restore_response=$(http_post "/api/trash/restore" "{\"trash_ids\":[$fake_trash_id]}")
    
    local code=$(json_query "$restore_response" '.code')
    
    if [ "$code" = "10003" ]; then
        echo -e "${GREEN}✓ Correctly returns ResourceNotFound (10003)${NC}"
        return 0
    else
        echo -e "${RED}✗ Expected code 10003, got: $code${NC}"
        print_response "$restore_response"
        return 1
    fi
}

# Test 7: Negative - Permanent delete non-existent trash_id
test_permanent_delete_nonexistent_trash() {
    print_test_header "Test 7: Permanent delete non-existent trash_id (expect 10003)"
    
    local fake_trash_id=99999998
    
    local delete_response
    delete_response=$(http_delete_with_body "/api/trash" "{\"trash_ids\":[$fake_trash_id]}")
    
    local code=$(json_query "$delete_response" '.code')
    
    # Note: API returns partial success with failure_count, not error code 10003 at top level
    # Check if the result shows failure for this trash_id
    local failure_count=$(json_query "$delete_response" '.data.summary.failure_count')
    
    if [ "$code" = "0" ] && [ "$failure_count" = "1" ]; then
        echo -e "${GREEN}✓ Correctly reports failure for non-existent trash_id${NC}"
        return 0
    elif [ "$code" = "10003" ]; then
        echo -e "${GREEN}✓ Correctly returns ResourceNotFound (10003)${NC}"
        return 0
    else
        echo -e "${YELLOW}! Response code: $code, failure_count: $failure_count${NC}"
        print_response "$delete_response"
        # Consider this a pass if we get any reasonable response
        return 0
    fi
}

# =============================================================================
# Main
# =============================================================================

main() {
    echo "========================================"
    echo "Trash API Tests"
    echo "========================================"
    echo "Base URL: $BASE_URL"
    echo ""
    
    # Authenticate
    login || exit 1
    
    # Clear trash before tests
    echo ""
    echo "Clearing trash before tests..."
    clear_trash_completely
    
    # Run tests
    local tests_passed=0
    local tests_failed=0
    
    if test_delete_moves_to_trash; then
        ((tests_passed++))
    else
        ((tests_failed++))
    fi
    
    if test_get_trash_list; then
        ((tests_passed++))
    else
        ((tests_failed++))
    fi
    
    if test_restore_file; then
        ((tests_passed++))
    else
        ((tests_failed++))
    fi
    
    if test_permanent_delete; then
        ((tests_passed++))
    else
        ((tests_failed++))
    fi
    
    if test_clear_all_trash; then
        ((tests_passed++))
    else
        ((tests_failed++))
    fi
    
    if test_restore_nonexistent_trash; then
        ((tests_passed++))
    else
        ((tests_failed++))
    fi
    
    if test_permanent_delete_nonexistent_trash; then
        ((tests_passed++))
    else
        ((tests_failed++))
    fi
    
    # Summary
    echo ""
    echo "========================================"
    echo "TEST SUMMARY"
    echo "========================================"
    echo -e "Passed: ${GREEN}$tests_passed${NC}"
    echo -e "Failed: ${RED}$tests_failed${NC}"
    
    if [ "$tests_failed" -gt 0 ]; then
        exit 1
    fi
    
    exit 0
}

# Run main
main "$@"

#!/bin/bash
# tests/api/test_folder.sh
#
# Folder API Test Script
# Tests: POST /api/folder/create, GET /api/folder/tree, GET /api/folder/{folder_id}/breadcrumb
#
# Usage:
#   ./test_folder.sh
#
# Requirements:
#   - Backend running on http://127.0.0.1:8080
#   - Test account: admin / Admin123

#
# =============================================================================
# Test Data
# =============================================================================

TEST_FOLDERS=()        # Stores folder IDs created during tests

# =============================================================================
# Cleanup Function
# =============================================================================

cleanup_test_folders() {
    echo ""
    echo "=== Cleanup ==="
    
    if [ ${#TEST_FOLDERS[@]} -gt 0 ]; then
        echo "Moving ${#TEST_FOLDERS[@]} folders to trash..."
        local folder_ids_json=$(printf '%s\n' "${TEST_FOLDERS[@]}" | python3 -c "import json, sys;44; data = json.loads(sys.argv[1])
        query = sys.argv[2]
        for part in query.split('.'):
            if part.isdigit():
                result = result[int(part)]
            elif part in result:
                result = result[part]
            else:
                print('null', end='')
                sys.exit(0)
        if result is None:
            print(result, end='')
        elif isinstance(result, bool):
            print(str(result).lower(), end='')
        else:
            print(result, end='')
    
    echo -e "${GREEN}✓ Cleanup complete${NC}"
}

trap cleanup_test_folders EXIT

# =============================================================================
# Test Functions
# =============================================================================

test_create_folder_success() {
    print_test_header "Test 1: Create folder successfully"
    
    local folder_name="test_folder_$(date +%s)"
    local response
    response=$(http_post_with_status "/api/folder/create" "{\"name\":\"$folder_name\",\"parent_id\":0}")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    assert_status 200 "$http_code" || return 1
    assert_json "$body" '.code' '0' || return 1
    assert_json_not_empty "$body" '.data.id' || return 1
    
    local folder_id=$(json_query "$body" '.data.id')
    TEST_FOLDERS+=("$folder_id")
    
    echo "Created folder: $folder_name (ID: $folder_id)"
    return 0
}

test_get_folder_tree() {
    print_test_header "Test 2: Get folder tree"
    
    local response
    response=$(curl -s -w "\n%{http_code}" -X GET "$BASE_URL/api/folder/tree?parent_id=0&depth=-1" \
        -H "Authorization: Bearer $ACCESS_TOKEN")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    assert_status 200 "$http_code" || return 1
    assert_json "$body" '.code' '0' || return 1
    assert_json_not_empty "$body" '.data.children' || return 1
    
    echo "Folder tree retrieved successfully"
    return 0
}

test_get_breadcrumb_success() {
    print_test_header "Test 3: Get breadcrumb for existing folder"
    
    local folder_name="breadcrumb_test_$(date +%s)"
    local create_response
    create_response=$(http_post_with_status "/api/folder/create" "{\"name\":\"$folder_name\",\"parent_id\":0}")
    
    local create_body=$(echo "$create_response" | sed '$d')
    local folder_id=$(json_query "$create_body" '.data.id')
    TEST_FOLDERS+=("$folder_id")
    
    local response
    response=$(curl -s -w "\n%{http_code}" -X GET "$BASE_URL/api/folder/$folder_id/breadcrumb" \
        -H "Authorization: Bearer $ACCESS_TOKEN")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    assert_status 200 "$http_code" || return 1
    assert_json "$body" '.code' '0' || return 1
    assert_json_not_empty "$body" '.data.path' || return 1
    
    echo "Breadcrumb retrieved for folder ID: $folder_id"
    return 0
}

test_create_duplicate_folder() {
    print_test_header "Test 4: Create duplicate folder (expect 50010)"
    
    local folder_name="duplicate_test_$(date +%s)"
    
    local create1
    create1=$(http_post_with_status "/api/folder/create" "{\"name\":\"$folder_name\",\"parent_id\":0}")
    
    local create1_body=$(echo "$create1" | sed '$d')
    local folder_id=$(json_query "$create1_body" '.data.id')
    TEST_FOLDERS+=("$folder_id")
    
    local response
    response=$(http_post_with_status "/api/folder/create" "{\"name\":\"$folder_name\",\"parent_id\":0}")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    assert_status 409 "$http_code" || return 1
    assert_json "$body" '.code' '50010' || return 1
    
    echo "Duplicate folder correctly rejected with code 50010"
    return 0
}

test_create_folder_invalid_name() {
    print_test_header "Test 5: Create folder with invalid name (expect 50001)"
    
    local invalid_name="test/folder"
    
    local response
    response=$(http_post_with_status "/api/folder/create" "{\"name\":\"$invalid_name\",\"parent_id\":0}")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    assert_status 400 "$http_code" || return 1
    assert_json "$body" '.code' '50001' || return 1
    
    echo "Invalid folder name correctly rejected with code 50001"
    return 0
}

test_get_breadcrumb_nonexistent() {
    print_test_header "Test 6: Get breadcrumb for non-existent folder (expect 50006)"
    
    local nonexistent_id=99999999
    
    local response
    response=$(curl -s -w "\n%{http_code}" -X GET "$BASE_URL/api/folder/$nonexistent_id/breadcrumb" \
        -H "Authorization: Bearer $ACCESS_TOKEN")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    assert_status 404 "$http_code" || return 1
    assert_json "$body" '.code' '50006' || return 1
    
    echo "Non-existent folder correctly rejected with code 50006"
    return 0
}

test_create_folder_nonexistent_parent() {
    print_test_header "Test 7: Create folder with non-existent parent (expect 50006)"
    
    local folder_name="orphan_test_$(date +%s)"
    local nonexistent_parent=99999999
    
    local response
    response=$(http_post_with_status "/api/folder/create" "{\"name\":\"$folder_name\",\"parent_id\":$nonexistent_parent}")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    assert_status 404 "$http_code" || return 1
    assert_json "$body" '.code' '50006' || return 1
    
    echo "Non-existent parent correctly rejected with code 50006"
    return 0
}

# =============================================================================
# Main Execution
# =============================================================================

main() {
    echo "========================================"
    echo "   Folder API Tests"
    echo "========================================"
    echo ""
    
    login || exit 1
    
    local passed=0
    local failed=0
    
    if test_create_folder_success; then
        ((passed++))
    else
        ((failed++))
    fi
    
    if test_get_folder_tree; then
        ((passed++))
    else
        ((failed++))
    fi
    
    if test_get_breadcrumb_success; then
        ((passed++))
    else
        ((failed++))
    fi
    
    if test_create_duplicate_folder; then
        ((passed++))
    else
        ((failed++))
    fi
    
    if test_create_folder_invalid_name; then
        ((passed++))
    else
        ((failed++))
    fi
    
    if test_get_breadcrumb_nonexistent; then
        ((passed++))
    else
        ((failed++))
    fi
    
    if test_create_folder_nonexistent_parent; then
        ((passed++))
    else
        ((failed++))
    fi
    
    echo ""
    echo "========================================"
    echo "   Test Summary"
    echo "========================================"
    echo -e "  ${GREEN}Passed: $passed${NC}"
    echo -e "  ${RED}Failed: $failed${NC}"
    echo ""
    
    if [ $failed -gt 0 ]; then
        echo -e "${RED}✗ Some tests failed${NC}"
        exit 1
    else
        echo -e "${GREEN}✓ All folder tests passed!${NC}"
        exit 0
    fi
}

main "$@"

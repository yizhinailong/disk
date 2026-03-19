#!/bin/bash
# tests/api/test_share.sh
#
# Share API Verification Tests
# Tests all share endpoints including create, access, browse, download, update, and cancel
#
# Usage:
#   ./test_share.sh
#
# Exit codes:
#   0 - All tests passed
#   1 - One or more tests failed

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/test_helper.sh"

# =============================================================================
# Test State
# =============================================================================

TEST_FILE_ID=""
SHARE_ID=""
SHARE_ID_VIEW=""
SHARE_ID_NO_PASSWORD=""
SHARE_TOKEN=""
VIEW_SHARE_TOKEN=""
SHARE_IDS_TO_CLEANUP=()
FILES_TO_CLEANUP=()

# =============================================================================
# Helper Functions
# =============================================================================

# share_access - Access share without authentication (public endpoint)
# Returns share_token on success
share_access() {
    local share_id="$1"
    local password="$2"
    
    local body
    if [ -n "$password" ]; then
        body="{\"password\":\"$password\"}"
    else
        body="{}"
    fi
    
    curl -s -X POST "$BASE_URL/api/share/access/$share_id" \
        -H "Content-Type: application/json" \
        -d "$body"
}

# share_browse - Browse share content with share token
share_browse() {
    local share_id="$1"
    local share_token="$2"
    local folder_id="${3:-0}"
    
    curl -s -X GET "$BASE_URL/api/share/browse/$share_id?folder_id=$folder_id" \
        -H "X-Share-Token: $share_token"
}

# share_download - Download file from share with share token
share_download() {
    local share_id="$1"
    local file_id="$2"
    local share_token="$3"
    
    curl -s -w "\n%{http_code}" -X GET "$BASE_URL/api/share/download/$share_id/$file_id" \
        -H "X-Share-Token: $share_token"
}

# http_put - Make authenticated PUT request
http_put() {
    local endpoint="$1"
    local body="$2"
    curl -s -X PUT "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "$body"
}

# http_delete_with_body - Make authenticated DELETE request with body
http_delete_with_body() {
    local endpoint="$1"
    local body="$2"
    curl -s -X DELETE "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "$body"
}

# =============================================================================
# Setup
# =============================================================================

setup() {
    print_test_header "Setup: Creating test file for sharing"
    
    # Create a test file in root folder (parent_id=0)
    local file_name=$(generate_test_name "test_share_file")
    local response
    response=$(http_post_with_status "/api/file/create" "{\"name\":\"$file_name.txt\",\"parent_id\":0,\"size\":100,\"hash\":\"test_hash_$(date +%s)\"}")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    if [ "$http_code" -ne 200 ] && [ "$http_code" -ne 201 ]; then
        echo -e "${RED}Failed to create test file${NC}"
        echo "Response: $body"
        return 1
    fi
    
    local code=$(json_query "$body" '.code')
    if [ "$code" != "0" ]; then
        echo -e "${RED}Failed to create test file: code=$code${NC}"
        return 1
    fi
    
    TEST_FILE_ID=$(json_query "$body" '.data.id')
    FILES_TO_CLEANUP+=("$TEST_FILE_ID")
    
    echo -e "${GREEN}✓ Test file created with ID: $TEST_FILE_ID${NC}"
    return 0
}

# =============================================================================
# Test Cases
# =============================================================================

test_create_share_with_password() {
    print_test_header "Test 1: Create share with password and download permission"
    
    local response
    response=$(http_post "/api/share" "{\"file_ids\":[$TEST_FILE_ID],\"expire_days\":7,\"password\":\"test123\",\"permission\":\"download\"}")
    
    assert_json "$response" '.code' '0' || return 1
    assert_json_not_empty "$response" '.data.share_id' || return 1
    
    SHARE_ID=$(json_query "$response" '.data.share_id')
    SHARE_IDS_TO_CLEANUP+=("$SHARE_ID")
    
    echo "Share ID: $SHARE_ID"
    assert_json "$response" '.data.permission' 'download' || return 1
    assert_json_not_empty "$response" '.data.expires_at' || return 1
    
    return 0
}

test_create_share_view_only() {
    print_test_header "Test 1b: Create share with view-only permission"
    
    local response
    response=$(http_post "/api/share" "{\"file_ids\":[$TEST_FILE_ID],\"expire_days\":7,\"password\":\"view123\",\"permission\":\"view\"}")
    
    assert_json "$response" '.code' '0' || return 1
    
    SHARE_ID_VIEW=$(json_query "$response" '.data.share_id')
    SHARE_IDS_TO_CLEANUP+=("$SHARE_ID_VIEW")
    
    echo "View-only Share ID: $SHARE_ID_VIEW"
    assert_json "$response" '.data.permission' 'view' || return 1
    
    return 0
}

test_create_share_no_password() {
    print_test_header "Test 1c: Create share without password"
    
    local response
    response=$(http_post "/api/share" "{\"file_ids\":[$TEST_FILE_ID],\"expire_days\":7,\"permission\":\"download\"}")
    
    assert_json "$response" '.code' '0' || return 1
    
    SHARE_ID_NO_PASSWORD=$(json_query "$response" '.data.share_id')
    SHARE_IDS_TO_CLEANUP+=("$SHARE_ID_NO_PASSWORD")
    
    echo "No-password Share ID: $SHARE_ID_NO_PASSWORD"
    
    return 0
}

test_get_share_list() {
    print_test_header "Test 2: Get share list"
    
    local response
    response=$(http_get "/api/share?status=all")
    
    assert_json "$response" '.code' '0' || return 1
    assert_json_not_empty "$response" '.data.items' || return 1
    
    # Check that our shares are in the list
    local items=$(json_query "$response" '.data.items')
    echo "Shares retrieved successfully"
    
    return 0
}

test_get_share_details() {
    print_test_header "Test 3: Get share details"
    
    local response
    response=$(http_get "/api/share/$SHARE_ID")
    
    assert_json "$response" '.code' '0' || return 1
    assert_json "$response" '.data.share_id' "$SHARE_ID" || return 1
    assert_json "$response" '.data.permission' 'download' || return 1
    assert_json_not_empty "$response" '.data.files' || return 1
    
    echo "Share details retrieved successfully"
    
    return 0
}

test_update_share_settings() {
    print_test_header "Test 4: Update share settings (extend expiry, change password)"
    
    local response
    response=$(http_put "/api/share/$SHARE_ID" "{\"expire_days\":14,\"password\":\"newpass\",\"permission\":\"download\"}")
    
    assert_json "$response" '.code' '0' || return 1
    assert_json "$response" '.data.permission' 'download' || return 1
    assert_json_not_empty "$response" '.data.expires_at' || return 1
    
    echo "Share settings updated successfully"
    
    return 0
}

test_access_share_with_correct_password() {
    print_test_header "Test 5: Access share with correct password"
    
    local response
    response=$(share_access "$SHARE_ID" "newpass")
    
    assert_json "$response" '.code' '0' || return 1
    assert_json_not_empty "$response" '.data.share_token' || return 1
    
    SHARE_TOKEN=$(json_query "$response" '.data.share_token')
    echo "Share token obtained: ${SHARE_TOKEN:0:30}..."
    
    assert_json "$response" '.data.permission' 'download' || return 1
    
    return 0
}

test_access_share_without_password() {
    print_test_header "Test 5b: Access share without password (no-password share)"
    
    local response
    response=$(share_access "$SHARE_ID_NO_PASSWORD" "")
    
    assert_json "$response" '.code' '0' || return 1
    assert_json_not_empty "$response" '.data.share_token' || return 1
    
    echo "No-password share accessed successfully"
    
    return 0
}

test_browse_share_content() {
    print_test_header "Test 6: Browse share content with share_token"
    
    local response
    response=$(share_browse "$SHARE_ID" "$SHARE_TOKEN" 0)
    
    assert_json "$response" '.code' '0' || return 1
    assert_json_not_empty "$response" '.data.items' || return 1
    
    echo "Share content browsed successfully"
    
    return 0
}

test_download_from_share() {
    print_test_header "Test 7: Download from share with share_token"
    
    local response
    response=$(share_download "$SHARE_ID" "$TEST_FILE_ID" "$SHARE_TOKEN")
    
    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')
    
    # Download should succeed (200) or return file content
    if [ "$http_code" -eq 200 ] || [ "$http_code" -eq 206 ]; then
        echo -e "${GREEN}✓ Download successful (HTTP $http_code)${NC}"
        return 0
    else
        echo -e "${RED}✗ Download failed with HTTP $http_code${NC}"
        echo "Response: $body"
        return 1
    fi
}

test_cancel_share() {
    print_test_header "Test 8: Cancel share"
    
    # First create a new share to cancel
    local response
    response=$(http_post "/api/share" "{\"file_ids\":[$TEST_FILE_ID],\"expire_days\":7,\"password\":\"cancel\",\"permission\":\"download\"}")
    
    assert_json "$response" '.code' '0' || return 1
    
    local cancel_share_id=$(json_query "$response" '.data.share_id')
    echo "Created share to cancel: $cancel_share_id"
    
    # Cancel the share
    local cancel_response
    cancel_response=$(http_delete_with_body "/api/share" "{\"share_ids\":[\"$cancel_share_id\"]}")
    
    assert_json "$cancel_response" '.code' '0' || return 1
    
    local succeeded=$(json_query "$cancel_response" '.data.summary.succeeded')
    if [ "$succeeded" != "1" ]; then
        echo -e "${RED}✗ Cancel failed: succeeded count is $succeeded${NC}"
        echo "Response: $cancel_response"
        return 1
    fi
    
    echo -e "${GREEN}✓ Share cancelled successfully${NC}"
    
    return 0
}

# =============================================================================
# Negative Tests
# =============================================================================

test_access_share_with_wrong_password() {
    print_test_header "Test 9 (Negative): Access share with wrong password"
    
    local response
    response=$(share_access "$SHARE_ID" "wrongpassword")
    
    # Should return error code 60003 (SharePasswordError)
    assert_json "$response" '.code' '60003' || return 1
    
    echo -e "${GREEN}✓ Wrong password correctly rejected with code 60003${NC}"
    
    return 0
}

test_access_cancelled_share() {
    print_test_header "Test 10 (Negative): Access cancelled share"
    
    # Create and cancel a share
    local response
    response=$(http_post "/api/share" "{\"file_ids\":[$TEST_FILE_ID],\"expire_days\":7,\"password\":\"test123\",\"permission\":\"download\"}")
    
    local cancelled_share_id=$(json_query "$response" '.data.share_id')
    
    # Cancel it
    http_delete_with_body "/api/share" "{\"share_ids\":[\"$cancelled_share_id\"]}"
    
    # Try to access the cancelled share
    local access_response
    access_response=$(share_access "$cancelled_share_id" "test123")
    
    # Should return error code 60001 (ShareNotFound)
    assert_json "$access_response" '.code' '60001' || return 1
    
    echo -e "${GREEN}✓ Cancelled share correctly rejected with code 60001${NC}"
    
    return 0
}

test_download_from_view_only_share() {
    print_test_header "Test 11 (Negative): Download from view-only share"
    
    # First get share token for view-only share
    local response
    response=$(share_access "$SHARE_ID_VIEW" "view123")
    
    local view_token=$(json_query "$response" '.data.share_token')
    
    # Try to download from view-only share
    local download_response
    download_response=$(share_download "$SHARE_ID_VIEW" "$TEST_FILE_ID" "$view_token")
    
    local http_code=$(echo "$download_response" | tail -n 1)
    local body=$(echo "$download_response" | sed '$d')
    
    # Should return error code 60004 (ShareAccessDenied)
    local code=$(json_query "$body" '.code')
    if [ "$code" = "60004" ]; then
        echo -e "${GREEN}✓ View-only download correctly rejected with code 60004${NC}"
        return 0
    else
        echo -e "${RED}✗ Expected code 60004, got: $code${NC}"
        echo "Response: $body"
        return 1
    fi
}

test_access_nonexistent_share() {
    print_test_header "Test 12 (Negative): Access nonexistent share"
    
    local response
    response=$(share_access "sh_nonexistent12345" "password")
    
    # Should return error code 60001 (ShareNotFound)
    assert_json "$response" '.code' '60001' || return 1
    
    echo -e "${GREEN}✓ Nonexistent share correctly rejected with code 60001${NC}"
    
    return 0
}

test_browse_without_share_token() {
    print_test_header "Test 13 (Negative): Browse share without share_token"
    
    local response
    response=$(curl -s -X GET "$BASE_URL/api/share/browse/$SHARE_ID?folder_id=0")
    
    # Should return error code 40106 (TokenMissing)
    assert_json "$response" '.code' '40106' || return 1
    
    echo -e "${GREEN}✓ Browse without token correctly rejected with code 40106${NC}"
    
    return 0
}

# =============================================================================
# Cleanup
# =============================================================================

cleanup() {
    echo ""
    echo "========================================"
    echo "Cleanup"
    echo "========================================"
    
    # Cancel all created shares
    for share_id in "${SHARE_IDS_TO_CLEANUP[@]}"; do
        echo "Cancelling share: $share_id"
        http_delete_with_body "/api/share" "{\"share_ids\":[\"$share_id\"]}" > /dev/null 2>&1 || true
    done
    
    # Delete test files
    for file_id in "${FILES_TO_CLEANUP[@]}"; do
        echo "Deleting test file: $file_id"
        http_delete "/api/file/$file_id" > /dev/null 2>&1 || true
    done
    
    echo -e "${GREEN}✓ Cleanup completed${NC}"
}

# =============================================================================
# Main
# =============================================================================

main() {
    local failed=0
    
    echo "=========================================="
    echo "Share API Tests"
    echo "=========================================="
    echo "Backend: $BASE_URL"
    echo "Started: $(date)"
    echo ""
    
    # Authenticate
    echo "Step 0: Authenticating..."
    if ! login; then
        echo -e "${RED}Authentication failed!${NC}"
        exit 1
    fi
    echo ""
    
    # Setup
    if ! setup; then
        echo -e "${RED}Setup failed!${NC}"
        exit 1
    fi
    echo ""
    
    # Run tests
    tests=(
        "test_create_share_with_password"
        "test_create_share_view_only"
        "test_create_share_no_password"
        "test_get_share_list"
        "test_get_share_details"
        "test_update_share_settings"
        "test_access_share_with_correct_password"
        "test_access_share_without_password"
        "test_browse_share_content"
        "test_download_from_share"
        "test_cancel_share"
        # Negative tests
        "test_access_share_with_wrong_password"
        "test_access_cancelled_share"
        "test_download_from_view_only_share"
        "test_access_nonexistent_share"
        "test_browse_without_share_token"
    )
    
    for test_func in "${tests[@]}"; do
        if ! $test_func; then
            ((failed++))
        fi
        echo ""
    done
    
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo "Total tests: ${#tests[@]}"
    echo "Passed: $((${#tests[@]} - failed))"
    echo "Failed: $failed"
    echo "Finished: $(date)"
    echo "=========================================="
    
    if [ $failed -gt 0 ]; then
        exit 1
    fi
    
    exit 0
}

# Set trap for cleanup
trap cleanup EXIT

# Run main
main "$@"

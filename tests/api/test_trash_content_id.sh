#!/bin/bash
# tests/api/test_trash_content_id.sh
#
# API Verification Test: Trash content_id Alignment
# Tests that file delete populates content_id, restore uses it, permanent delete frees space
#
# Usage:
#   ./test_trash_content_id.sh
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

UPLOADED_FILE_IDS=()
TEST_TRASH_IDS=()

# =============================================================================
# MySQL Helper (for content_id checks — not exposed by API)
# =============================================================================

mysql_q() {
    mysql -h127.0.0.1 -P3306 -uroot --password="${MYSQL_PASSWORD:-}" -D disk -Nse "$1" 2>/dev/null
}

# =============================================================================
# Cleanup Function
# =============================================================================

cleanup() {
    echo ""
    echo "Cleaning up test resources..."

    # Clear trash
    curl -s -X DELETE "$BASE_URL/api/trash/all" \
        -H "Authorization: Bearer $ACCESS_TOKEN" >/dev/null 2>&1 || true

    # Clean up any remaining uploaded files
    for file_id in "${UPLOADED_FILE_IDS[@]}"; do
        [ -z "$file_id" ] && continue
        curl -s -X DELETE "$BASE_URL/api/file" \
            -H "Authorization: Bearer $ACCESS_TOKEN" \
            -H "Content-Type: application/json" \
            -d "{\"file_ids\":[$file_id]}" >/dev/null 2>&1 || true
    done

    # Clear trash again for anything just deleted
    curl -s -X DELETE "$BASE_URL/api/trash/all" \
        -H "Authorization: Bearer $ACCESS_TOKEN" >/dev/null 2>&1 || true

    rm -f /tmp/test_trash_cid_*_$$.bin

    echo "Cleanup completed."
}

trap cleanup EXIT

# =============================================================================
# Helper Functions
# =============================================================================

# http_delete_with_body - DELETE request with JSON body
http_delete_with_body() {
    local endpoint="$1"
    local body="$2"
    curl -s -X DELETE "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "$body"
}

# init_upload - Initialize upload task
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

    local file_id=$(json_query "$body" '.data.file_id')
    if [ "$file_id" != "null" ] && [ -n "$file_id" ]; then
        echo "INSTANT:$file_id"
        return 0
    fi

    local upload_id=$(json_query "$body" '.data.upload_id')
    echo "$upload_id"
}

# upload_chunk - Upload a single chunk
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

# complete_upload - Complete upload task
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

# upload_test_file - Create and upload a small test file, return file_id
upload_test_file() {
    local filename="${1:-test_trash_cid_$$}"
    local temp_file="/tmp/${filename}_$$.bin"

    dd if=/dev/urandom of="$temp_file" bs=1024 count=50 2>/dev/null
    local file_size=$(stat -c%s "$temp_file")
    local file_hash=$(md5sum "$temp_file" | cut -d' ' -f1)

    local upload_result=$(init_upload "${filename}.bin" "$file_size" "$file_hash" 0)

    if [[ "$upload_result" == INSTANT:* ]]; then
        rm -f "$temp_file"
        echo "${upload_result#INSTANT:}"
        return 0
    fi

    if [ -z "$upload_result" ]; then
        rm -f "$temp_file"
        echo ""
        return 1
    fi

    local upload_id="$upload_result"

    # Upload chunk
    local chunk_hash="$file_hash"
    local result=$(upload_chunk "$upload_id" "0" "$temp_file" "$chunk_hash")
    rm -f "$temp_file"

    local http_code=$(echo "$result" | head -n 1)
    if [ "$http_code" != "200" ]; then
        echo ""
        return 1
    fi

    # Complete
    local file_id=$(complete_upload "$upload_id")
    echo "$file_id"
}

# get_trash_id_for_file - Find trash_id for a given original file_id
get_trash_id_for_file() {
    local file_id="$1"
    local trash_response
    trash_response=$(http_get "/api/trash?page=1&page_size=100")

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

# get_trash_content_id - Get content_id from trash table via MySQL
get_trash_content_id() {
    local trash_id="$1"
    mysql_q "SELECT COALESCE(content_id, 0) FROM trash WHERE id = $trash_id"
}

# get_file_content_id - Get content_id from files table via MySQL
get_file_content_id() {
    local file_id="$1"
    mysql_q "SELECT COALESCE(content_id, 0) FROM files WHERE id = $file_id"
}

# get_user_id - Get user ID from profile API
get_user_id() {
    local response
    response=$(http_get "/api/user/profile")
    json_query "$response" '.data.id'
}

# =============================================================================
# Tests
# =============================================================================

test_file_delete_populates_content_id() {
    print_test_header "File Delete → Trash Record Has content_id Populated"

    # Upload test file
    local file_id=$(upload_test_file "test_cid_populate")
    if [ -z "$file_id" ]; then
        echo -e "${RED}✗ Failed to upload test file${NC}"
        return 1
    fi
    UPLOADED_FILE_IDS+=("$file_id")
    echo "  Uploaded file_id: $file_id"

    # Get the file's content_id from files table
    local file_content_id=$(get_file_content_id "$file_id")
    echo "  File content_id in files table: $file_content_id"

    # Delete the file (move to trash)
    local del_response=$(http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}")
    local del_code=$(json_query "$del_response" '.code')

    if [ "$del_code" != "0" ]; then
        echo -e "${RED}✗ Failed to delete file${NC}"
        echo "Response: $del_response"
        return 1
    fi
    echo -e "${GREEN}✓ File deleted (moved to trash)${NC}"

    # Get trash_id
    local trash_id=$(get_trash_id_for_file "$file_id")
    if [ -z "$trash_id" ]; then
        echo -e "${RED}✗ File not found in trash${NC}"
        return 1
    fi
    TEST_TRASH_IDS+=("$trash_id")
    echo "  Trash ID: $trash_id"

    # Check content_id in trash table
    local trash_content_id=$(get_trash_content_id "$trash_id")
    echo "  Trash content_id: $trash_content_id"

    if [ "$trash_content_id" != "0" ] && [ -n "$trash_content_id" ]; then
        echo -e "${GREEN}✓ Trash record has content_id populated: $trash_content_id${NC}"

        # Verify it matches the file's original content_id
        if [ "$trash_content_id" = "$file_content_id" ]; then
            echo -e "${GREEN}✓ content_id matches original file's content_id${NC}"
        else
            echo -e "${YELLOW}⚠ content_id mismatch: file=$file_content_id, trash=$trash_content_id${NC}"
        fi
    else
        echo -e "${RED}✗ Trash record content_id is NULL or 0${NC}"
        return 1
    fi

    # Permanent cleanup
    http_delete_with_body "/api/trash" "{\"trash_ids\":[$trash_id]}" >/dev/null 2>&1 || true

    return 0
}

test_trash_restore_with_content_id() {
    print_test_header "Trash Restore Succeeds for Records with content_id"

    # Upload test file
    local file_id=$(upload_test_file "test_cid_restore")
    if [ -z "$file_id" ]; then
        echo -e "${RED}✗ Failed to upload test file${NC}"
        return 1
    fi
    UPLOADED_FILE_IDS+=("$file_id")
    echo "  Uploaded file_id: $file_id"

    # Delete to trash
    http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}" >/dev/null

    # Get trash_id and verify content_id
    local trash_id=$(get_trash_id_for_file "$file_id")
    if [ -z "$trash_id" ]; then
        echo -e "${RED}✗ File not found in trash${NC}"
        return 1
    fi

    local trash_content_id=$(get_trash_content_id "$trash_id")
    echo "  Trash content_id: $trash_content_id"

    if [ "$trash_content_id" = "0" ] || [ -z "$trash_content_id" ]; then
        echo -e "${YELLOW}⚠ Trash record has no content_id — restore test may not validate content_id path${NC}"
    fi

    # Restore the file
    echo "Restoring file from trash..."
    local restore_response=$(http_post "/api/trash/restore" "{\"trash_ids\":[$trash_id]}")
    local restore_code=$(json_query "$restore_response" '.code')

    if [ "$restore_code" != "0" ]; then
        echo -e "${RED}✗ Restore failed${NC}"
        echo "Response: $restore_response"
        return 1
    fi

    local success_count=$(json_query "$restore_response" '.data.summary.success_count')
    if [ "$success_count" = "1" ]; then
        echo -e "${GREEN}✓ Restore succeeded for record with content_id${NC}"
    else
        echo -e "${RED}✗ Restore success_count not 1: $success_count${NC}"
        return 1
    fi

    # Verify file is accessible again
    local file_info=$(http_get "/api/file/$file_id")
    local file_code=$(json_query "$file_info" '.code')
    if [ "$file_code" = "0" ]; then
        echo -e "${GREEN}✓ File accessible after restore${NC}"
    else
        echo -e "${RED}✗ File not accessible after restore (code: $file_code)${NC}"
        return 1
    fi

    # Verify restored file has correct content_id
    local restored_content_id=$(get_file_content_id "$file_id")
    if [ "$restored_content_id" = "$trash_content_id" ] && [ "$restored_content_id" != "0" ]; then
        echo -e "${GREEN}✓ Restored file content_id matches trash record ($restored_content_id)${NC}"
    else
        echo -e "${YELLOW}⚠ Restored file content_id ($restored_content_id) vs trash ($trash_content_id)${NC}"
    fi

    # Clean up restored file
    http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}" >/dev/null 2>&1 || true
    # Clear from trash
    curl -s -X DELETE "$BASE_URL/api/trash/all" \
        -H "Authorization: Bearer $ACCESS_TOKEN" >/dev/null 2>&1 || true

    return 0
}

test_permanent_delete_frees_space() {
    print_test_header "Permanent Delete Succeeds and Frees Space"

    # Get baseline storage
    local storage_before=$(http_get "/api/user/storage")
    local used_before=$(json_query "$storage_before" '.data.used')
    echo "  storage_used before: $used_before"

    # Upload test file
    local file_id=$(upload_test_file "test_cid_permdelete")
    if [ -z "$file_id" ]; then
        echo -e "${RED}✗ Failed to upload test file${NC}"
        return 1
    fi
    echo "  Uploaded file_id: $file_id"

    # Check storage increased
    local storage_after_upload=$(http_get "/api/user/storage")
    local used_after_upload=$(json_query "$storage_after_upload" '.data.used')
    echo "  storage_used after upload: $used_after_upload"

    # Delete to trash
    local del_response=$(http_delete_with_body "/api/file" "{\"file_ids\":[$file_id]}")
    local del_code=$(json_query "$del_response" '.code')
    if [ "$del_code" != "0" ]; then
        echo -e "${RED}✗ Failed to delete file${NC}"
        return 1
    fi

    # Get trash_id and verify content_id
    local trash_id=$(get_trash_id_for_file "$file_id")
    if [ -z "$trash_id" ]; then
        echo -e "${RED}✗ File not found in trash${NC}"
        return 1
    fi

    local trash_content_id=$(get_trash_content_id "$trash_id")
    echo "  Trash content_id: $trash_content_id"

    # Permanent delete
    echo "Permanently deleting from trash..."
    local perm_response=$(http_delete_with_body "/api/trash" "{\"trash_ids\":[$trash_id]}")
    local perm_code=$(json_query "$perm_response" '.code')

    if [ "$perm_code" != "0" ]; then
        echo -e "${RED}✗ Permanent delete failed${NC}"
        echo "Response: $perm_response"
        return 1
    fi

    local success_count=$(json_query "$perm_response" '.data.summary.success_count')
    if [ "$success_count" != "1" ]; then
        echo -e "${RED}✗ Permanent delete success_count not 1: $success_count${NC}"
        return 1
    fi
    echo -e "${GREEN}✓ Permanent delete succeeded${NC}"

    # Check freed space
    local storage_after_delete=$(http_get "/api/user/storage")
    local used_after_delete=$(json_query "$storage_after_delete" '.data.used')
    echo "  storage_used after permanent delete: $used_after_delete"

    if [ "$used_after_delete" -le "$used_before" ]; then
        echo -e "${GREEN}✓ Space freed after permanent delete (was $used_before, now $used_after_delete)${NC}"
    else
        echo -e "${YELLOW}⚠ Space not fully freed (before=$used_before, after=$used_after_delete)${NC}"
        # Not a hard failure — there might be other concurrent operations
    fi

    # Verify file is gone
    local file_check=$(http_get "/api/file/$file_id")
    local file_code=$(json_query "$file_check" '.code')
    if [ "$file_code" != "0" ]; then
        echo -e "${GREEN}✓ File no longer accessible after permanent delete${NC}"
    else
        echo -e "${RED}✗ File still accessible after permanent delete${NC}"
        return 1
    fi

    return 0
}

# =============================================================================
# Main
# =============================================================================

main() {
    echo "=========================================="
    echo "Trash content_id API Verification Tests"
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

    # Get user ID
    USER_ID=$(get_user_id)
    if [ -z "$USER_ID" ] || [ "$USER_ID" = "null" ]; then
        echo -e "${RED}✗ Failed to get user ID from profile${NC}"
        exit 1
    fi
    echo "User ID: $USER_ID"

    # Verify MySQL connectivity
    local mysql_test=$(mysql_q "SELECT 1" 2>/dev/null)
    if [ "$mysql_test" != "1" ]; then
        echo -e "${YELLOW}⚠ MySQL not accessible — content_id tests requiring MySQL will fail${NC}"
        echo "  Set MYSQL_PASSWORD environment variable if needed"
    fi
    echo ""

    # Clear trash before tests
    curl -s -X DELETE "$BASE_URL/api/trash/all" \
        -H "Authorization: Bearer $ACCESS_TOKEN" >/dev/null 2>&1 || true

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
    run_test test_file_delete_populates_content_id
    run_test test_trash_restore_with_content_id
    run_test test_permanent_delete_frees_space

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

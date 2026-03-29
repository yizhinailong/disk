#!/bin/bash
# tests/api/test_upload_quota.sh
#
# API Verification Test: Upload Quota Reservation & Lifecycle
# Tests quota reservation, reserved→used transfer, cancel release, and quota exceeded
#
# Usage:
#   ./test_upload_quota.sh
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
ORIGINAL_QUOTA=""

# =============================================================================
# MySQL Helper (only for storage_reserved checks — not exposed by API)
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

    # Restore original quota if we changed it
    if [ -n "$ORIGINAL_QUOTA" ] && [ -n "$USER_ID" ]; then
        echo "  Restoring original storage quota..."
        mysql_q "UPDATE users SET storage_quota = $ORIGINAL_QUOTA, storage_reserved = GREATEST(storage_reserved, 0) WHERE id = $USER_ID" 2>/dev/null || true
    fi

    # Clean up uploaded files
    for file_id in "${UPLOADED_FILE_IDS[@]}"; do
        [ -z "$file_id" ] && continue
        echo "  Cleaning up file $file_id..."
        # Move to trash then permanent delete
        curl -s -X DELETE "$BASE_URL/api/file" \
            -H "Authorization: Bearer $ACCESS_TOKEN" \
            -H "Content-Type: application/json" \
            -d "{\"file_ids\":[$file_id]}" >/dev/null 2>&1 || true
    done

    # Clean up temp files
    rm -f /tmp/test_quota_chunk_*_$$_*.bin /tmp/test_quota_*_$$.bin

    echo "Cleanup completed."
}

trap cleanup EXIT

# =============================================================================
# Upload Helper Functions (copied from test_upload.sh — not in test_helper.sh)
# =============================================================================

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
        # Return error info for diagnostics
        echo "HTTP_ERROR:$http_code:$body"
        return 1
    fi

    local code=$(json_query "$body" '.code')
    if [ "$code" != "0" ]; then
        echo "API_ERROR:$code:$body"
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

# upload_chunk - Upload a single chunk
# Args: $1=upload_id, $2=chunk_index, $3=chunk_file, $4=chunk_hash
# Output: http_code on first line, body on remaining lines
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
# Output: response body
cancel_upload() {
    local upload_id="$1"
    curl -s -X DELETE "$BASE_URL/api/file/upload/$upload_id" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json"
}

# cancel_upload_with_status - Cancel upload with HTTP status code
# Args: $1=upload_id
# Output: http_code on first line, body on remaining lines
cancel_upload_with_status() {
    local upload_id="$1"
    curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL/api/file/upload/$upload_id" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json"
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
        return 1
    fi

    # Get trash_id from response
    local trash_id=$(json_query "$del_body" '.data.results[0].trash_id')
    if [ -z "$trash_id" ] || [ "$trash_id" = "null" ]; then
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
        return 1
    fi
}

# get_storage_info - Get storage info from API
# Output: used, quota values (printed as "used quota")
get_storage_info() {
    local response
    response=$(http_get "/api/user/storage")

    local code=$(json_query "$response" '.code')
    if [ "$code" != "0" ]; then
        echo "0 0"
        return 1
    fi

    local used=$(json_query "$response" '.data.used')
    local quota=$(json_query "$response" '.data.quota')
    echo "$used $quota"
}

# get_storage_reserved - Get storage_reserved from MySQL
# Output: reserved value
get_storage_reserved() {
    local result=$(mysql_q "SELECT COALESCE(storage_reserved, 0) FROM users WHERE id = $USER_ID")
    echo "${result:-0}"
}

# get_user_id - Get user ID from profile API
# Output: user_id
get_user_id() {
    local response
    response=$(http_get "/api/user/profile")

    local code=$(json_query "$response" '.code')
    if [ "$code" != "0" ]; then
        echo ""
        return 1
    fi

    local uid=$(json_query "$response" '.data.id')
    echo "$uid"
}

# do_full_upload - Helper: init + upload chunk + complete
# Args: $1=filename, $2=file_size, $3=file_hash, $4=chunk_file, $5=chunk_hash
# Returns: file_id
do_full_upload() {
    local filename="$1"
    local file_size="$2"
    local file_hash="$3"
    local chunk_file="$4"
    local chunk_hash="$5"

    local upload_result=$(init_upload "$filename" "$file_size" "$file_hash" 0)
    if [[ "$upload_result" == HTTP_ERROR:* ]] || [[ "$upload_result" == API_ERROR:* ]]; then
        echo ""
        return 1
    fi

    if [[ "$upload_result" == INSTANT:* ]]; then
        local fid="${upload_result#INSTANT:}"
        echo "$fid"
        return 0
    fi

    local upload_id="$upload_result"

    # Upload chunk
    local result=$(upload_chunk "$upload_id" "0" "$chunk_file" "$chunk_hash")
    local http_code=$(echo "$result" | head -n 1)
    local body=$(echo "$result" | tail -n +2)

    if [ "$http_code" != "200" ]; then
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        echo ""
        return 1
    fi

    local code=$(json_query "$body" '.code')
    if [ "$code" != "0" ]; then
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        echo ""
        return 1
    fi

    # Complete upload
    local file_id=$(complete_upload "$upload_id")
    echo "$file_id"
}

# =============================================================================
# Tests
# =============================================================================

test_chunk_idempotency() {
    print_test_header "Chunk Idempotency (upload same chunk twice)"

    # Create small test file (100KB)
    local test_file="/tmp/test_quota_chunk_idem_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)
    local chunk_hash="$file_hash"

    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_chunk_idem_$$.bin" "$file_size" "$file_hash" 0)

    if [[ "$upload_result" == HTTP_ERROR:* ]] || [[ "$upload_result" == API_ERROR:* ]]; then
        rm -f "$test_file"
        echo -e "${RED}✗ Failed to initialize upload: $upload_result${NC}"
        return 1
    fi

    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ File already exists (instant upload), skipping${NC}"
        rm -f "$test_file"
        local file_id="${upload_result#INSTANT:}"
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 0
    fi

    local upload_id="$upload_result"
    echo "  Upload ID: $upload_id"

    # Upload chunk first time
    echo "Uploading chunk (first time)..."
    local result1=$(upload_chunk "$upload_id" "0" "$test_file" "$chunk_hash")
    local http_code1=$(echo "$result1" | head -n 1)
    local body1=$(echo "$result1" | tail -n +2)

    if [ "$http_code1" != "200" ]; then
        rm -f "$test_file"
        echo -e "${RED}✗ First chunk upload failed with HTTP $http_code1${NC}"
        echo "Response: $body1"
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        return 1
    fi

    local code1=$(json_query "$body1" '.code')
    if [ "$code1" != "0" ]; then
        rm -f "$test_file"
        echo -e "${RED}✗ First chunk upload failed with code $code1${NC}"
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        return 1
    fi
    echo -e "${GREEN}✓ First chunk upload succeeded${NC}"

    # Upload same chunk second time (should succeed via INSERT IGNORE)
    echo "Uploading chunk (second time — idempotent)..."
    local result2=$(upload_chunk "$upload_id" "0" "$test_file" "$chunk_hash")
    local http_code2=$(echo "$result2" | head -n 1)
    local body2=$(echo "$result2" | tail -n +2)

    rm -f "$test_file"

    if [ "$http_code2" != "200" ]; then
        echo -e "${RED}✗ Second chunk upload failed with HTTP $http_code2${NC}"
        echo "Response: $body2"
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        return 1
    fi

    local code2=$(json_query "$body2" '.code')
    if [ "$code2" != "0" ]; then
        echo -e "${RED}✗ Second chunk upload failed with code $code2${NC}"
        echo "Response: $body2"
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        return 1
    fi
    echo -e "${GREEN}✓ Second chunk upload succeeded (idempotent)${NC}"

    # Complete upload
    local file_id=$(complete_upload "$upload_id")
    if [ -z "$file_id" ]; then
        echo -e "${RED}✗ Failed to complete upload${NC}"
        return 1
    fi

    UPLOADED_FILE_IDS+=("$file_id")
    echo -e "${GREEN}✓ Upload completed, file_id: $file_id${NC}"

    # Cleanup
    delete_file_permanently "$file_id" 2>/dev/null || true

    return 0
}

test_quota_reservation_on_init() {
    print_test_header "Quota Reservation on Init (storage_reserved increases)"

    # Create small test file (100KB)
    local test_file="/tmp/test_quota_reserve_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)

    # Get baseline storage_reserved
    local reserved_before=$(get_storage_reserved)
    echo "  storage_reserved before init: $reserved_before"

    # Initialize upload
    echo "Initializing upload (file_size=$file_size)..."
    local upload_result=$(init_upload "test_quota_reserve_$$.bin" "$file_size" "$file_hash" 0)

    if [[ "$upload_result" == HTTP_ERROR:* ]] || [[ "$upload_result" == API_ERROR:* ]]; then
        rm -f "$test_file"
        echo -e "${RED}✗ Failed to initialize upload: $upload_result${NC}"
        return 1
    fi

    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ File already exists (instant upload), skipping${NC}"
        rm -f "$test_file"
        local file_id="${upload_result#INSTANT:}"
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 0
    fi

    local upload_id="$upload_result"
    echo "  Upload ID: $upload_id"

    # Check storage_reserved increased by file_size
    local reserved_after=$(get_storage_reserved)
    echo "  storage_reserved after init: $reserved_after"

    local expected_reserved=$((reserved_before + file_size))
    if [ "$reserved_after" -ge "$expected_reserved" ]; then
        echo -e "${GREEN}✓ storage_reserved increased by ~$file_size bytes${NC}"
    else
        echo -e "${RED}✗ storage_reserved did not increase as expected${NC}"
        echo "  Expected >= $expected_reserved, got $reserved_after"
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        rm -f "$test_file"
        return 1
    fi

    # Cancel to release quota
    cancel_upload "$upload_id" >/dev/null 2>&1 || true
    rm -f "$test_file"

    return 0
}

test_complete_transfers_reserved_to_used() {
    print_test_header "Complete Transfer: reserved→used"

    # Create small test file (100KB)
    local test_file="/tmp/test_quota_complete_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)
    local chunk_hash="$file_hash"

    # Get baseline values
    local storage_info_before=$(get_storage_info)
    local used_before=$(echo "$storage_info_before" | cut -d' ' -f1)
    local reserved_before=$(get_storage_reserved)
    echo "  Before: used=$used_before, reserved=$reserved_before"

    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_quota_complete_$$.bin" "$file_size" "$file_hash" 0)

    if [[ "$upload_result" == HTTP_ERROR:* ]] || [[ "$upload_result" == API_ERROR:* ]]; then
        rm -f "$test_file"
        echo -e "${RED}✗ Failed to initialize upload: $upload_result${NC}"
        return 1
    fi

    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ File already exists (instant upload), skipping${NC}"
        rm -f "$test_file"
        local file_id="${upload_result#INSTANT:}"
        UPLOADED_FILE_IDS+=("$file_id")
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 0
    fi

    local upload_id="$upload_result"

    # Upload chunk
    local result=$(upload_chunk "$upload_id" "0" "$test_file" "$chunk_hash")
    local http_code=$(echo "$result" | head -n 1)

    if [ "$http_code" != "200" ]; then
        rm -f "$test_file"
        echo -e "${RED}✗ Chunk upload failed${NC}"
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        return 1
    fi

    # Check reserved increased after init
    local reserved_after_init=$(get_storage_reserved)
    echo "  After init: reserved=$reserved_after_init"

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

    # Check reserved decreased and used increased
    local reserved_after_complete=$(get_storage_reserved)
    local storage_info_after=$(get_storage_info)
    local used_after=$(echo "$storage_info_after" | cut -d' ' -f1)

    echo "  After complete: used=$used_after, reserved=$reserved_after_complete"

    # Reserved should decrease back (or be less than after init)
    if [ "$reserved_after_complete" -le "$reserved_after_init" ]; then
        echo -e "${GREEN}✓ storage_reserved decreased after complete${NC}"
    else
        echo -e "${RED}✗ storage_reserved did not decrease (was $reserved_after_init, now $reserved_after_complete)${NC}"
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 1
    fi

    # Used should increase
    if [ "$used_after" -ge "$((used_before + file_size))" ] || [ "$used_after" -gt "$used_before" ]; then
        echo -e "${GREEN}✓ storage_used increased after complete${NC}"
    else
        echo -e "${RED}✗ storage_used did not increase (was $used_before, now $used_after)${NC}"
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 1
    fi

    # Cleanup
    delete_file_permanently "$file_id" 2>/dev/null || true

    return 0
}

test_cancel_releases_reserved_quota() {
    print_test_header "Cancel Releases Reserved Quota"

    # Create small test file (100KB)
    local test_file="/tmp/test_quota_cancel_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)

    # Get baseline
    local reserved_before=$(get_storage_reserved)
    echo "  storage_reserved before init: $reserved_before"

    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_quota_cancel_$$.bin" "$file_size" "$file_hash" 0)
    rm -f "$test_file"

    if [[ "$upload_result" == HTTP_ERROR:* ]] || [[ "$upload_result" == API_ERROR:* ]]; then
        echo -e "${RED}✗ Failed to initialize upload: $upload_result${NC}"
        return 1
    fi

    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ File already exists (instant upload), skipping${NC}"
        local file_id="${upload_result#INSTANT:}"
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 0
    fi

    local upload_id="$upload_result"

    # Check reserved increased
    local reserved_after_init=$(get_storage_reserved)
    echo "  storage_reserved after init: $reserved_after_init"

    if [ "$reserved_after_init" -le "$reserved_before" ]; then
        echo -e "${RED}✗ storage_reserved did not increase after init${NC}"
        cancel_upload "$upload_id" >/dev/null 2>&1 || true
        return 1
    fi

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

    # Check reserved decreased back
    local reserved_after_cancel=$(get_storage_reserved)
    echo "  storage_reserved after cancel: $reserved_after_cancel"

    if [ "$reserved_after_cancel" -le "$reserved_before" ]; then
        echo -e "${GREEN}✓ storage_reserved decreased back to baseline after cancel${NC}"
    else
        echo -e "${RED}✗ storage_reserved did not release (before=$reserved_before, after=$reserved_after_cancel)${NC}"
        return 1
    fi

    return 0
}

test_cancel_idempotency() {
    print_test_header "Cancel Idempotency (second cancel returns error 50008)"

    # Create small test file (100KB)
    local test_file="/tmp/test_quota_cancel_idem_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)

    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_quota_cancel_idem_$$.bin" "$file_size" "$file_hash" 0)
    rm -f "$test_file"

    if [[ "$upload_result" == HTTP_ERROR:* ]] || [[ "$upload_result" == API_ERROR:* ]]; then
        echo -e "${RED}✗ Failed to initialize upload: $upload_result${NC}"
        return 1
    fi

    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ File already exists (instant upload), skipping${NC}"
        local file_id="${upload_result#INSTANT:}"
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 0
    fi

    local upload_id="$upload_result"

    # First cancel — should succeed
    echo "First cancel..."
    local cancel1_response=$(cancel_upload "$upload_id")
    local cancel1_code=$(json_query "$cancel1_response" '.code')

    if [ "$cancel1_code" != "0" ]; then
        echo -e "${RED}✗ First cancel failed${NC}"
        echo "Response: $cancel1_response"
        return 1
    fi
    echo -e "${GREEN}✓ First cancel succeeded${NC}"

    # Second cancel — should return error 50008
    echo "Second cancel (should fail with 50008)..."
    local cancel2_result=$(cancel_upload_with_status "$upload_id")
    local cancel2_http=$(echo "$cancel2_result" | tail -n 1)
    local cancel2_body=$(echo "$cancel2_result" | sed '$d')
    local cancel2_code=$(json_query "$cancel2_body" '.code')

    if [ "$cancel2_code" = "50008" ]; then
        echo -e "${GREEN}✓ Second cancel correctly returned error 50008 (UploadTaskNotFound)${NC}"
    else
        echo -e "${RED}✗ Second cancel did not return 50008 (got code=$cancel2_code, http=$cancel2_http)${NC}"
        echo "Response: $cancel2_body"
        return 1
    fi

    return 0
}

test_cancel_after_complete() {
    print_test_header "Already-completed Task Immunity (cancel after complete returns 50008)"

    # Create small test file (100KB)
    local test_file="/tmp/test_quota_cancel_comp_$$.bin"
    dd if=/dev/urandom of="$test_file" bs=1024 count=100 2>/dev/null
    local file_size=$(stat -c%s "$test_file")
    local file_hash=$(md5sum "$test_file" | cut -d' ' -f1)
    local chunk_hash="$file_hash"

    # Initialize upload
    echo "Initializing upload..."
    local upload_result=$(init_upload "test_quota_cancel_comp_$$.bin" "$file_size" "$file_hash" 0)

    if [[ "$upload_result" == HTTP_ERROR:* ]] || [[ "$upload_result" == API_ERROR:* ]]; then
        rm -f "$test_file"
        echo -e "${RED}✗ Failed to initialize upload: $upload_result${NC}"
        return 1
    fi

    if [[ "$upload_result" == INSTANT:* ]]; then
        echo -e "${YELLOW}⚠ File already exists (instant upload), skipping${NC}"
        rm -f "$test_file"
        local file_id="${upload_result#INSTANT:}"
        UPLOADED_FILE_IDS+=("$file_id")
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 0
    fi

    local upload_id="$upload_result"

    # Upload chunk and complete
    upload_chunk "$upload_id" "0" "$test_file" "$chunk_hash" >/dev/null
    rm -f "$test_file"

    echo "Completing upload..."
    local file_id=$(complete_upload "$upload_id")

    if [ -z "$file_id" ]; then
        echo -e "${RED}✗ Failed to complete upload${NC}"
        return 1
    fi

    UPLOADED_FILE_IDS+=("$file_id")
    echo -e "${GREEN}✓ Upload completed, file_id: $file_id${NC}"

    # Try to cancel the completed upload — should return 50008
    echo "Attempting to cancel completed upload..."
    local cancel_result=$(cancel_upload_with_status "$upload_id")
    local cancel_http=$(echo "$cancel_result" | tail -n 1)
    local cancel_body=$(echo "$cancel_result" | sed '$d')
    local cancel_code=$(json_query "$cancel_body" '.code')

    if [ "$cancel_code" = "50008" ]; then
        echo -e "${GREEN}✓ Cancel correctly returned error 50008 for completed task${NC}"
    else
        echo -e "${RED}✗ Expected error 50008, got code=$cancel_code (http=$cancel_http)${NC}"
        echo "Response: $cancel_body"
        delete_file_permanently "$file_id" 2>/dev/null || true
        return 1
    fi

    # Cleanup
    delete_file_permanently "$file_id" 2>/dev/null || true

    return 0
}

test_quota_exceeded_rejection() {
    print_test_header "Quota Exceeded Rejection (init with size exceeding quota)"

    # Save original quota
    ORIGINAL_QUOTA=$(mysql_q "SELECT storage_quota FROM users WHERE id = $USER_ID")
    echo "  Original quota: $ORIGINAL_QUOTA"

    # Set a very low quota (1KB = 1024 bytes)
    local tiny_quota=1024
    echo "  Setting quota to $tiny_quota bytes..."
    mysql_q "UPDATE users SET storage_quota = $tiny_quota WHERE id = $USER_ID"

    # Try to init upload with 10KB file (should exceed quota)
    local file_size=10240
    local file_hash="aabbccdd11223344aabbccdd11223344"

    echo "Attempting to init upload with size=$file_size (quota=$tiny_quota)..."
    local response
    response=$(http_post_with_status "/api/file/upload/init" \
        "{\"filename\":\"test_quota_exceeded_$$.bin\",\"file_size\":$file_size,\"file_hash\":\"$file_hash\",\"parent_id\":0}")

    local http_code=$(echo "$response" | head -n 1)
    local body=$(echo "$response" | tail -n +2)
    local code=$(json_query "$body" '.code')

    echo "  Response: code=$code, http=$http_code"

    # Restore quota immediately (even if test fails)
    echo "  Restoring original quota..."
    mysql_q "UPDATE users SET storage_quota = $ORIGINAL_QUOTA WHERE id = $USER_ID"
    ORIGINAL_QUOTA=""  # Prevent double-restore in cleanup

    if [ "$code" = "50004" ]; then
        echo -e "${GREEN}✓ Init correctly rejected with error 50004 (StorageQuotaExceeded)${NC}"
    else
        echo -e "${RED}✗ Expected error 50004 (StorageQuotaExceeded), got code=$code${NC}"
        echo "Response: $body"
        return 1
    fi

    return 0
}

# =============================================================================
# Main
# =============================================================================

main() {
    echo "=========================================="
    echo "Upload Quota API Verification Tests"
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

    # Get user ID for MySQL queries
    USER_ID=$(get_user_id)
    if [ -z "$USER_ID" ] || [ "$USER_ID" = "null" ]; then
        echo -e "${RED}✗ Failed to get user ID from profile${NC}"
        exit 1
    fi
    echo "User ID: $USER_ID"

    # Verify MySQL connectivity
    local mysql_test=$(mysql_q "SELECT 1" 2>/dev/null)
    if [ "$mysql_test" != "1" ]; then
        echo -e "${YELLOW}⚠ MySQL not accessible — quota tests requiring MySQL will fail${NC}"
        echo "  Set MYSQL_PASSWORD environment variable if needed"
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
    run_test test_chunk_idempotency
    run_test test_quota_reservation_on_init
    run_test test_complete_transfers_reserved_to_used
    run_test test_cancel_releases_reserved_quota
    run_test test_cancel_idempotency
    run_test test_cancel_after_complete
    run_test test_quota_exceeded_rejection

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

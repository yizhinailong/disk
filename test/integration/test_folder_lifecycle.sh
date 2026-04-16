#!/bin/bash
#
# test/integration/test_folder_lifecycle.sh
# Integration tests for folder lifecycle: create, tree, breadcrumb
#
# Prerequisites:
#   - Server running on localhost:8080
#   - MySQL database configured
#   - Redis configured
#
# Usage:
#   ./test/integration/test_folder_lifecycle.sh
#
# Environment variables:
#   BASE_URL    - Server URL (default: http://127.0.0.1:8080)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"
source "$SCRIPT_DIR/lib/http.sh"
source "$SCRIPT_DIR/lib/auth.sh"

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
EVIDENCE_DIR="${EVIDENCE_DIR:-.sisyphus/evidence}"

# Global variables for test data
TIMESTAMP_PID="$(date +%s)_$$"
PARENT_FOLDER_NAME="TstParent_${TIMESTAMP_PID}"
CHILD_FOLDER_NAME="TstChild_${TIMESTAMP_PID}"
PARENT_FOLDER_ID=""
CHILD_FOLDER_ID=""
TOKEN=""

# Helper: Create folder
do_create_folder() {
    local token="$1"
    local name="$2"
    local parent_id="$3"
    local body
    body=$(python3 - "$name" "$parent_id" <<'PY'
import json
import sys
print(json.dumps({"name": sys.argv[1], "parent_id": int(sys.argv[2])}))
PY
    )

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "$BASE_URL/api/folder/create" \
        -H "Authorization: Bearer $token" \
        -H "Content-Type: application/json" \
        -d "$body")

    CREATE_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    CREATE_BODY=$(printf '%s\n' "$response" | sed '$d')
}

# Helper: Get folder tree
do_get_tree() {
    local token="$1"
    local parent_id="${2:-0}"

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/folder/tree?parent_id=$parent_id" \
        -H "Authorization: Bearer $token")

    TREE_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    TREE_BODY=$(printf '%s\n' "$response" | sed '$d')
}

# Helper: Get breadcrumb
do_get_breadcrumb() {
    local token="$1"
    local folder_id="$2"

    local response
    response=$(curl -sS -w "\n%{http_code}" -X GET "$BASE_URL/api/folder/${folder_id}/breadcrumb" \
        -H "Authorization: Bearer $token")

    BREADCRUMB_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    BREADCRUMB_BODY=$(printf '%s\n' "$response" | sed '$d')
}

# Test 1: Create parent folder
test_create_parent_folder() {
    log_info "Testing parent folder creation..."

    do_login

    TOKEN=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
        log_fail "Login failed for parent folder test"
        printf '%s\n' "$LOGIN_BODY"
        exit 1
    fi

    do_create_folder "$TOKEN" "$PARENT_FOLDER_NAME" "0"

    local code
    code=$(json_field "$CREATE_BODY" "code")

    if [ "$CREATE_HTTP_CODE" = "200" ] && [ "$code" = "0" ]; then
        PARENT_FOLDER_ID=$(json_field "$CREATE_BODY" "data.id")
        local name
        name=$(json_field "$CREATE_BODY" "data.name")

        if [ -n "$PARENT_FOLDER_ID" ] && [ "$PARENT_FOLDER_ID" != "null" ] && [ "$name" = "$PARENT_FOLDER_NAME" ]; then
            log_pass "Parent folder created: id=$PARENT_FOLDER_ID, name=$PARENT_FOLDER_NAME"
            save_evidence "create_parent_folder_response.json" "$CREATE_BODY"
        else
            log_fail "Parent folder creation failed: missing id or name mismatch"
            printf '%s\n' "$CREATE_BODY"
            exit 1
        fi
    else
        log_fail "Parent folder creation failed: HTTP $CREATE_HTTP_CODE, code=$code"
        printf '%s\n' "$CREATE_BODY"
        exit 1
    fi
}

# Test 2: Create child folder
test_create_child_folder() {
    log_info "Testing child folder creation..."

    if [ -z "$PARENT_FOLDER_ID" ]; then
        log_fail "Parent folder ID not available for child creation"
        exit 1
    fi

    do_create_folder "$TOKEN" "$CHILD_FOLDER_NAME" "$PARENT_FOLDER_ID"

    local code
    code=$(json_field "$CREATE_BODY" "code")

    if [ "$CREATE_HTTP_CODE" = "200" ] && [ "$code" = "0" ]; then
        CHILD_FOLDER_ID=$(json_field "$CREATE_BODY" "data.id")
        local name
        name=$(json_field "$CREATE_BODY" "data.name")
        local parent_id
        parent_id=$(json_field "$CREATE_BODY" "data.parent_id")

        if [ -n "$CHILD_FOLDER_ID" ] && [ "$CHILD_FOLDER_ID" != "null" ] && \
           [ "$name" = "$CHILD_FOLDER_NAME" ] && [ "$parent_id" = "$PARENT_FOLDER_ID" ]; then
            log_pass "Child folder created: id=$CHILD_FOLDER_ID, name=$CHILD_FOLDER_NAME, parent_id=$PARENT_FOLDER_ID"
            save_evidence "create_child_folder_response.json" "$CREATE_BODY"
        else
            log_fail "Child folder creation failed: missing id, name mismatch, or parent_id mismatch"
            printf '%s\n' "$CREATE_BODY"
            exit 1
        fi
    else
        log_fail "Child folder creation failed: HTTP $CREATE_HTTP_CODE, code=$code"
        printf '%s\n' "$CREATE_BODY"
        exit 1
    fi
}

# Test 3: Tree contains both parent and child folders
test_tree_contains_both_folders() {
    log_info "Testing folder tree contains both created folders..."

    do_get_tree "$TOKEN" "0"

    local code
    code=$(json_field "$TREE_BODY" "code")

    if [ "$TREE_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_fail "Tree request failed: HTTP $TREE_HTTP_CODE, code=$code"
        printf '%s\n' "$TREE_BODY"
        exit 1
    fi

    # Verify tree contains both folder names (nested structure)
    if ! echo "$TREE_BODY" | python3 -c "
import json, sys
data = json.load(sys.stdin)
children = data.get('data', {}).get('children', [])
names = []
def collect_names(nodes):
    for n in nodes:
        names.append(n.get('name', ''))
        collect_names(n.get('children', []))
collect_names(children)
if '$PARENT_FOLDER_NAME' not in names:
    sys.exit(1)
if '$CHILD_FOLDER_NAME' not in names:
    sys.exit(1)
"; then
        log_fail "Tree does not contain both folder names"
        printf '%s\n' "$TREE_BODY"
        exit 1
    fi

    log_pass "Tree contains both parent and child folders"
    save_evidence "folder_tree_response.json" "$TREE_BODY"
}

# Test 4: Breadcrumb order matches hierarchy
test_breadcrumb_order() {
    log_info "Testing breadcrumb navigation order..."

    if [ -z "$CHILD_FOLDER_ID" ]; then
        log_fail "Child folder ID not available for breadcrumb test"
        exit 1
    fi

    do_get_breadcrumb "$TOKEN" "$CHILD_FOLDER_ID"

    local code
    code=$(json_field "$BREADCRUMB_BODY" "code")

    if [ "$BREADCRUMB_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_fail "Breadcrumb request failed: HTTP $BREADCRUMB_HTTP_CODE, code=$code"
        printf '%s\n' "$BREADCRUMB_BODY"
        exit 1
    fi

    # Verify path is an array and has entries
    local path
    path=$(json_field "$BREADCRUMB_BODY" "data.path")

    if [ -z "$path" ] || [ "$path" = "null" ]; then
        log_fail "Breadcrumb path is missing or null"
        printf '%s\n' "$BREADCRUMB_BODY"
        exit 1
    fi

    # Verify path ordering: root → parent → child
    if ! echo "$BREADCRUMB_BODY" | python3 -c "
import json, sys
data = json.load(sys.stdin)
path = data.get('data', {}).get('path', [])
if len(path) < 2:
    sys.exit(1)
# Verify order: last entry should be child folder
if path[-1].get('name') != '$CHILD_FOLDER_NAME':
    sys.exit(1)
# Verify parent appears before child
parent_found = False
for item in path:
    if item.get('name') == '$PARENT_FOLDER_NAME':
        parent_found = True
        break
if not parent_found:
    sys.exit(1)
"; then
        log_fail "Breadcrumb path order is incorrect"
        printf '%s\n' "$BREADCRUMB_BODY"
        exit 1
    fi

    log_pass "Breadcrumb navigation order is correct"
    save_evidence "breadcrumb_response.json" "$BREADCRUMB_BODY"
}

# Test 5: Nonexistent folder breadcrumb rejected
test_nonexistent_folder_breadcrumb() {
    log_info "Testing breadcrumb for nonexistent folder is rejected..."

    local nonexistent_id="99999999"
    do_get_breadcrumb "$TOKEN" "$nonexistent_id"

    local code
    code=$(json_field "$BREADCRUMB_BODY" "code")

    # Should return either non-200 HTTP or non-zero code (404 + 50006)
    if [ "$BREADCRUMB_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Nonexistent folder breadcrumb correctly rejected: HTTP $BREADCRUMB_HTTP_CODE, code=$code"
        save_evidence "nonexistent_folder_breadcrumb_response.json" "$BREADCRUMB_BODY"
    else
        log_fail "Nonexistent folder breadcrumb should be rejected but succeeded"
        printf '%s\n' "$BREADCRUMB_BODY"
        exit 1
    fi
}

# Test 6: Invalid folder name rejected
test_invalid_folder_name() {
    log_info "Testing invalid folder name is rejected..."

    local invalid_name="invalid/name"
    do_create_folder "$TOKEN" "$invalid_name" "0"

    local code
    code=$(json_field "$CREATE_BODY" "code")

    # Should return either non-200 HTTP or non-zero code (400 + 50001)
    if [ "$CREATE_HTTP_CODE" != "200" ] || [ "$code" != "0" ]; then
        log_pass "Invalid folder name correctly rejected: HTTP $CREATE_HTTP_CODE, code=$code"
        save_evidence "invalid_folder_name_response.json" "$CREATE_BODY"
    else
        log_fail "Invalid folder name should be rejected but succeeded"
        printf '%s\n' "$CREATE_BODY"
        exit 1
    fi
}

# Test 7: Cleanup note
test_cleanup_note() {
    log_info "Cleanup note: Test folders created during this run..."

    # Note: No delete folder API available, so just log the created folders
    log_info "Created folders for this test session:"
    log_info "  Parent: $PARENT_FOLDER_NAME (ID: $PARENT_FOLDER_ID)"
    log_info "  Child: $CHILD_FOLDER_NAME (ID: $CHILD_FOLDER_ID)"

    # Save cleanup info to evidence
    cat > "$EVIDENCE_DIR/folder_lifecycle_cleanup.txt" <<EOF
Folder Lifecycle Test - Created Folders
========================================
Parent: $PARENT_FOLDER_NAME (ID: $PARENT_FOLDER_ID)
Child: $CHILD_FOLDER_NAME (ID: $CHILD_FOLDER_ID)

Note: These folders were created during integration testing and remain in the database.
Manual cleanup may be required.
EOF

    log_pass "Cleanup information saved to evidence"
}

main() {
    printf '==========================================\n'
    printf 'Folder Lifecycle Integration Tests\n'
    printf '==========================================\n\n'

    if ! command -v curl >/dev/null 2>&1; then
        log_fail "curl is required"
        exit 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        log_fail "python3 is required"
        exit 1
    fi

    # Use check_server (not ensure_server) - server lifecycle managed by other scripts
    check_server

    log_section "Test Folder Names"
    log_info "Parent: $PARENT_FOLDER_NAME"
    log_info "Child: $CHILD_FOLDER_NAME"
    echo ""

    test_create_parent_folder
    test_create_child_folder
    test_tree_contains_both_folders
    test_breadcrumb_order
    test_nonexistent_folder_breadcrumb
    test_invalid_folder_name
    test_cleanup_note

    print_summary
}

main "$@"

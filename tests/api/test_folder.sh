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
# Test account
# Username: admin
# Password: Admin123

# Backend: http://127.0.0.1:8080

# =============================================================================

# Test Data
# =============================================================================

TEST_FOLDERS=()        # Stores folder IDs created during tests
TEST_TRASH_ITEMS=()    # Stores trash IDs after soft delete

# =============================================================================
# Cleanup Function
# =============================================================================

# cleanup_test_folders() - Moves folders to trash and deletes them permanently
# =============================================================================
# 
# cleanup_test_folders() {
    echo ""
    echo "=== Cleanup ==="
    
    if [ ${#TEST_FOLDERS[@]} -gt 0 ]; then
        echo "Moving ${#TEST_FOLDERS[@]} folders to trash..."
        
        local folder_ids_json=$(printf '%s\n' "${TEST_FOLDERS[@]}" | python3 -c "import json, sys
data = json.loads(sys.argv[1])
        query = sys.argv[2]
        parts = [p for p in query.split('.') if p
    else:
        print('null', end=0)
    if isinstance(result, bool):
        print(str(result).lower())
    else:
        print(result, end=0)
    if [ ${#TEST_FOLDERS[@]} -gt 0 ]; then
        echo "No test folders to cleanup"
    fi
}

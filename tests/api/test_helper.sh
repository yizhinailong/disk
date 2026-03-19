#!/bin/bash
# tests/api/test_helper.sh
#
# API Test Helper Functions
# Provides common utilities for API verification tests
#
# Usage:
#   source tests/api/test_helper.sh
#   login
#   assert_status 200 "$response_code"
#   assert_json "$response" '.code' '0'

set -e

# =============================================================================
# Configuration
# =============================================================================

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
ACCESS_TOKEN=""
REFRESH_TOKEN=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# =============================================================================
# JSON Helper (Python-based, jq alternative)
# =============================================================================

# json_query - Extract value from JSON using Python
# Usage: json_query "$json" '.field.path'
json_query() {
    local json="$1"
    local query="$2"
    python3 -c "
import json, sys
data = json.loads(sys.argv[1])
query = sys.argv[2]
parts = [p for p in query.split('.') if p]
result = data
for part in parts:
    if part.isdigit():
        result = result[int(part)]
    elif part in result:
        result = result[part]
    else:
        print('null', end='')
        sys.exit(0)
if result is None:
    print('null', end='')
elif isinstance(result, bool):
    print(str(result).lower(), end='')
else:
    print(result, end='')
" "$json" "$query" 2>/dev/null
}

# =============================================================================
# Authentication Functions
# =============================================================================

# login - Authenticate and retrieve JWT tokens
#
# Description:
#   Sends login request to /api/auth/login endpoint and stores tokens
#   in global variables ACCESS_TOKEN and REFRESH_TOKEN
#
# Arguments:
#   $1 - account (optional, default: admin)
#   $2 - password (optional, default: Admin123)
#
# Returns:
#   0 - Login successful
#   1 - Login failed
#
# Example:
#   login                      # Use default credentials
#   login "testuser" "pass123" # Use custom credentials
#
login() {
    local account="${1:-admin}"
    local password="${2:-Admin123}"

    echo -e "${YELLOW}Authenticating as '${account}'...${NC}"

    local response
    response=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "{\"account\":\"$account\",\"password\":\"$password\"}")

    local http_code=$(echo "$response" | tail -n 1)
    local body=$(echo "$response" | sed '$d')

    if [ "$http_code" -ne 200 ]; then
        echo -e "${RED}✗ Login failed with HTTP $http_code${NC}"
        echo "Response: $body"
        return 1
    fi

    local code=$(json_query "$body" '.code')
    if [ "$code" != "0" ]; then
        local msg=$(json_query "$body" '.message')
        echo -e "${RED}✗ Login failed: ${msg:-Unknown error}${NC}"
        echo "Response: $body"
        return 1
    fi

    ACCESS_TOKEN=$(json_query "$body" '.data.access_token')
    REFRESH_TOKEN=$(json_query "$body" '.data.refresh_token')

    if [ -z "$ACCESS_TOKEN" ] || [ "$ACCESS_TOKEN" = "null" ]; then
        echo -e "${RED}✗ Login failed: No access_token in response${NC}"
        echo "Response: $body"
        return 1
    fi

    echo -e "${GREEN}✓ Login successful${NC}"
    echo "  Access token: ${ACCESS_TOKEN:0:50}..."
    return 0
}

# =============================================================================
# Assertion Functions
# =============================================================================

# assert_status - Verify HTTP status code matches expected
#
# Description:
#   Compares actual HTTP status code with expected value
#
# Arguments:
#   $1 - expected status code (e.g., 200, 404, 500)
#   $2 - actual status code
#
# Returns:
#   0 - Status codes match
#   1 - Status codes don't match
#
# Example:
#   assert_status 200 "$http_code"
#
assert_status() {
    local expected="$1"
    local actual="$2"

    if [ "$expected" -eq "$actual" ]; then
        echo -e "${GREEN}✓ Status code: $actual${NC}"
        return 0
    else
        echo -e "${RED}✗ Status code mismatch: expected $expected, got $actual${NC}"
        return 1
    fi
}

# assert_json - Verify JSON field value matches expected
#
# Description:
#   Uses Python to extract and compare JSON field value
#
# Arguments:
#   $1 - JSON string
#   $2 - query path (e.g., '.code', '.data.user.id')
#   $3 - expected value
#
# Returns:
#   0 - Values match
#   1 - Values don't match or query failed
#
# Example:
#   assert_json "$response" '.code' '0'
#   assert_json "$response" '.data.user.username' 'admin'
#
assert_json() {
    local json="$1"
    local query="$2"
    local expected="$3"

    local actual
    actual=$(json_query "$json" "$query")

    if [ "$expected" = "$actual" ]; then
        echo -e "${GREEN}✓ JSON assertion: $query = $actual${NC}"
        return 0
    else
        echo -e "${RED}✗ JSON assertion failed: $query${NC}"
        echo "  Expected: '$expected'"
        echo "  Actual:   '$actual'"
        return 1
    fi
}

# assert_json_contains - Verify JSON field contains expected substring
#
# Description:
#   Checks if the extracted JSON value contains the expected substring
#
# Arguments:
#   $1 - JSON string
#   $2 - query path
#   $3 - expected substring
#
# Returns:
#   0 - Substring found
#   1 - Substring not found
#
# Example:
#   assert_json_contains "$response" '.message' 'success'
#
assert_json_contains() {
    local json="$1"
    local query="$2"
    local expected="$3"

    local actual
    actual=$(json_query "$json" "$query")

    if [[ "$actual" == *"$expected"* ]]; then
        echo -e "${GREEN}✓ JSON contains: '$expected' in $query${NC}"
        return 0
    else
        echo -e "${RED}✗ JSON contains failed: '$expected' not found in '$actual'${NC}"
        return 1
    fi
}

# assert_json_not_empty - Verify JSON field is not empty/null
#
# Description:
#   Checks if the extracted JSON value exists and is not null/empty
#
# Arguments:
#   $1 - JSON string
#   $2 - query path
#
# Returns:
#   0 - Field has a value
#   1 - Field is empty or null
#
# Example:
#   assert_json_not_empty "$response" '.data.access_token'
#
assert_json_not_empty() {
    local json="$1"
    local query="$2"

    local actual
    actual=$(json_query "$json" "$query")

    if [ -z "$actual" ] || [ "$actual" = "null" ] || [ "$actual" = "" ]; then
        echo -e "${RED}✗ JSON field empty or null: $query${NC}"
        return 1
    else
        echo -e "${GREEN}✓ JSON field has value: $query${NC}"
        return 0
    fi
}

# =============================================================================
# HTTP Request Helpers
# =============================================================================

# http_get - Make authenticated GET request
#
# Arguments:
#   $1 - endpoint path (e.g., /api/user/profile)
#
# Output:
#   JSON response body
#
# Example:
#   response=$(http_get "/api/user/profile")
#
http_get() {
    local endpoint="$1"
    curl -s -X GET "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json"
}

# http_post - Make authenticated POST request
#
# Arguments:
#   $1 - endpoint path
#   $2 - JSON body
#
# Output:
#   JSON response body
#
# Example:
#   response=$(http_post "/api/folder/create" '{"name":"test","parent_id":null}')
#
http_post() {
    local endpoint="$1"
    local body="$2"
    curl -s -X POST "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "$body"
}

# http_post_with_status - Make authenticated POST request with status code
#
# Arguments:
#   $1 - endpoint path
#   $2 - JSON body
#
# Output:
#   HTTP status code on first line, JSON body on remaining lines
#
# Example:
#   response=$(http_post_with_status "/api/auth/login" '{"account":"admin","password":"Admin123"}')
#   http_code=$(echo "$response" | head -n 1)
#   body=$(echo "$response" | tail -n +2)
#
http_post_with_status() {
    local endpoint="$1"
    local body="$2"
    curl -s -w "\n%{http_code}" -X POST "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json" \
        -d "$body"
}

# http_delete - Make authenticated DELETE request
#
# Arguments:
#   $1 - endpoint path
#
# Output:
#   JSON response body
#
http_delete() {
    local endpoint="$1"
    curl -s -X DELETE "$BASE_URL$endpoint" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/json"
}

# =============================================================================
# Cleanup Functions
# =============================================================================

# cleanup - Clean up test resources
#
# Description:
#   Called automatically on script exit via trap.
#   Override this function in individual test scripts to clean up
#   test-specific resources (e.g., test files, folders).
#
# Example override in test script:
#   cleanup() {
#       echo "Cleaning up test files..."
#       http_delete "/api/file/$test_file_id"
#   }
#
cleanup() {
    :
}

# =============================================================================
# Utility Functions
# =============================================================================

# generate_test_name - Generate unique test file/folder name
#
# Description:
#   Creates a unique name using timestamp and random suffix
#
# Arguments:
#   $1 - prefix (e.g., "test_folder", "test_file")
#
# Output:
#   Unique name string
#
# Example:
#   name=$(generate_test_name "test_folder")
#   # Output: test_folder_20260319_abc123
#
generate_test_name() {
    local prefix="$1"
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local random=$(head /dev/urandom | tr -dc 'a-z0-9' | head -c 6)
    echo "${prefix}_${timestamp}_${random}"
}

# print_test_header - Print formatted test section header
#
# Arguments:
#   $1 - test name/description
#
print_test_header() {
    echo ""
    echo "========================================"
    echo "TEST: $1"
    echo "========================================"
}

# print_response - Pretty print JSON response
#
# Arguments:
#   $1 - JSON string
#
print_response() {
    python3 -m json.tool 2>/dev/null <<< "$1" || echo "$1"
}

# =============================================================================
# Trap Setup
# =============================================================================

trap cleanup EXIT

# =============================================================================
# Dependency Check
# =============================================================================

if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Error: 'python3' is required but not installed.${NC}"
    exit 1
fi

if ! command -v curl &> /dev/null; then
    echo -e "${RED}Error: 'curl' is required but not installed.${NC}"
    exit 1
fi

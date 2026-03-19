#!/bin/bash
# tests/api/test_system.sh
#
# System API Tests
# Tests health check, system info, and operation logs endpoints
#
# Prerequisites:
#   - Backend running at http://127.0.0.1:8080
#   - Test user: admin / Admin123
#
# Usage:
#   ./tests/api/test_system.sh
#

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/test_helper.sh"

# =============================================================================
# Test Configuration
# =============================================================================

FAIL_COUNT=0
TOTAL_TESTS=0

run_test() {
    local test_name="$1"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo ""
    echo "=== Test $TOTAL_TESTS: $test_name ==="
}

record_failure() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    echo -e "${RED}✗ Test failed${NC}"
}

# =============================================================================
# Test Suite
# =============================================================================

echo "========================================"
echo "System API Tests"
echo "========================================"
echo "Base URL: $BASE_URL"
echo ""

# -----------------------------------------------------------------------------
# Test 1: Health check (no authentication required)
# -----------------------------------------------------------------------------
run_test "Health check returns healthy status"

HEALTH_RESP=$(curl -s "$BASE_URL/api/health")
assert_json "$HEALTH_RESP" '.code' '0' || record_failure
assert_json "$HEALTH_RESP" '.data.overall_status' 'healthy' || record_failure
assert_json "$HEALTH_RESP" '.data.components.database.status' 'healthy' || record_failure
assert_json "$HEALTH_RESP" '.data.components.redis.status' 'healthy' || record_failure

UPTIME=$(json_query "$HEALTH_RESP" '.data.uptime')
if [ "$UPTIME" -ge 0 ] 2>/dev/null; then
    echo -e "${GREEN}✓ Uptime >= 0: $UPTIME seconds${NC}"
else
    echo -e "${RED}✗ Uptime is not a valid non-negative integer: $UPTIME${NC}"
    record_failure
fi

assert_json_not_empty "$HEALTH_RESP" '.data.version' || record_failure

# -----------------------------------------------------------------------------
# Test 2: System info with authentication (positive test)
# -----------------------------------------------------------------------------
run_test "System info returns version and uptime (with auth)"

login || { echo -e "${RED}✗ Cannot continue tests - login failed${NC}"; exit 1; }

INFO_RESP=$(http_get "/api/system/info")
assert_json "$INFO_RESP" '.code' '0' || record_failure

assert_json_not_empty "$INFO_RESP" '.data.version' || record_failure
assert_json_not_empty "$INFO_RESP" '.data.drogon_version' || record_failure
assert_json_not_empty "$INFO_RESP" '.data.build_time' || record_failure

UPTIME=$(json_query "$INFO_RESP" '.data.uptime')
if [ "$UPTIME" -ge 0 ] 2>/dev/null; then
    echo -e "${GREEN}✓ Uptime >= 0: $UPTIME seconds${NC}"
else
    echo -e "${RED}✗ Uptime is not a valid non-negative integer: $UPTIME${NC}"
    record_failure
fi

CURRENT_CONN=$(json_query "$INFO_RESP" '.data.connections.current')
if [ "$CURRENT_CONN" -ge 0 ] 2>/dev/null; then
    echo -e "${GREEN}✓ Current connections >= 0: $CURRENT_CONN${NC}"
else
    echo -e "${RED}✗ Current connections is not a valid non-negative integer: $CURRENT_CONN${NC}"
    record_failure
fi

PEAK_CONN=$(json_query "$INFO_RESP" '.data.connections.peak')
if [ "$PEAK_CONN" -ge 0 ] 2>/dev/null; then
    echo -e "${GREEN}✓ Peak connections >= 0: $PEAK_CONN${NC}"
else
    echo -e "${RED}✗ Peak connections is not a valid non-negative integer: $PEAK_CONN${NC}"
    record_failure
fi

# -----------------------------------------------------------------------------
# Test 3: Operation logs with pagination (positive test)
# -----------------------------------------------------------------------------
run_test "Operation logs return paginated results"

LOGS_RESP=$(http_get "/api/logs?page=1&page_size=20")
assert_json "$LOGS_RESP" '.code' '0' || record_failure

assert_json "$LOGS_RESP" '.data.page' '1' || record_failure
assert_json "$LOGS_RESP" '.data.page_size' '20' || record_failure

TOTAL=$(json_query "$LOGS_RESP" '.data.total')
if [ "$TOTAL" -ge 0 ] 2>/dev/null; then
    echo -e "${GREEN}✓ Total records >= 0: $TOTAL${NC}"
else
    echo -e "${RED}✗ Total records is not a valid non-negative integer: $TOTAL${NC}"
    record_failure
fi

ITEMS=$(json_query "$LOGS_RESP" '.data.items')
if [ "$ITEMS" != "null" ]; then
    echo -e "${GREEN}✓ Items array exists${NC}"
else
    echo -e "${RED}✗ Items array is missing or null${NC}"
    record_failure
fi

# -----------------------------------------------------------------------------
# Test 4: System info without authentication (negative test)
# -----------------------------------------------------------------------------
run_test "System info without auth returns 40106 (TokenMissing)"

SAVED_TOKEN="$ACCESS_TOKEN"
ACCESS_TOKEN=""

INFO_NO_AUTH=$(curl -s -w "\n%{http_code}" -X GET "$BASE_URL/api/system/info" \
    -H "Content-Type: application/json")

HTTP_CODE=$(echo "$INFO_NO_AUTH" | tail -n 1)
BODY=$(echo "$INFO_NO_AUTH" | sed '$d')

assert_status 401 "$HTTP_CODE" || record_failure
assert_json "$BODY" '.code' '40106' || record_failure

ACCESS_TOKEN="$SAVED_TOKEN"

# -----------------------------------------------------------------------------
# Test 5: Operation logs without authentication (negative test)
# -----------------------------------------------------------------------------
run_test "Operation logs without auth returns 40106 (TokenMissing)"

ACCESS_TOKEN=""

LOGS_NO_AUTH=$(curl -s -w "\n%{http_code}" -X GET "$BASE_URL/api/logs?page=1&page_size=20" \
    -H "Content-Type: application/json")

HTTP_CODE=$(echo "$LOGS_NO_AUTH" | tail -n 1)
BODY=$(echo "$LOGS_NO_AUTH" | sed '$d')

assert_status 401 "$HTTP_CODE" || record_failure
assert_json "$BODY" '.code' '40106' || record_failure

ACCESS_TOKEN="$SAVED_TOKEN"

# -----------------------------------------------------------------------------
# Test 6: Operation logs with invalid page parameter (negative test)
# -----------------------------------------------------------------------------
run_test "Operation logs with invalid page returns 10001 (InvalidParameter)"

LOGS_INVALID=$(curl -s -w "\n%{http_code}" -X GET "$BASE_URL/api/logs?page=-1&page_size=20" \
    -H "Authorization: Bearer $ACCESS_TOKEN" \
    -H "Content-Type: application/json")

HTTP_CODE=$(echo "$LOGS_INVALID" | tail -n 1)
BODY=$(echo "$LOGS_INVALID" | sed '$d')

assert_status 400 "$HTTP_CODE" || record_failure
assert_json "$BODY" '.code' '10001' || record_failure

# =============================================================================
# Summary
# =============================================================================

echo ""
echo "========================================"
echo "Test Summary"
echo "========================================"
echo "Total tests: $TOTAL_TESTS"
echo "Passed:      $((TOTAL_TESTS - FAIL_COUNT))"
echo "Failed:      $FAIL_COUNT"
echo ""

if [ "$FAIL_COUNT" -eq 0 ]; then
    echo -e "${GREEN}✓ All system tests passed!${NC}"
    exit 0
else
    echo -e "${RED}✗ Some tests failed${NC}"
    exit 1
fi

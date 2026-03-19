#!/bin/bash
# tests/api/run_all.sh
#
# API Verification Test Suite Runner
# Runs all API test scripts in sequence
#
# Usage:
#   ./run_all.sh              # Run all tests
#   ./run_all.sh -v           # Verbose mode
#
# Exit codes:
#   0 - All tests passed
#   1 - One or more tests failed

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/test_helper.sh"

PASSED=0
FAILED=0
VERBOSE=0

if [ "$1" = "-v" ] || [ "$1" = "--verbose" ]; then
    VERBOSE=1
fi

echo "=========================================="
echo "API Verification Test Suite"
echo "=========================================="
echo "Backend: $BASE_URL"
echo "Started: $(date)"
echo ""

echo "Step 0: Authenticating..."
if ! login; then
    echo -e "${RED}Authentication failed! Cannot proceed with tests.${NC}"
    echo "Ensure backend is running at $BASE_URL"
    exit 1
fi
echo ""

run_test() {
    local test_script="$1"
    local test_name=$(basename "$test_script" .sh)

    echo "Running: $test_name"
    if [ $VERBOSE -eq 1 ]; then
        if bash "$test_script"; then
            ((PASSED++))
            echo -e "${GREEN}✓ $test_name passed${NC}"
        else
            ((FAILED++))
            echo -e "${RED}✗ $test_name failed${NC}"
        fi
    else
        if bash "$test_script" > /dev/null 2>&1; then
            ((PASSED++))
            echo -e "${GREEN}✓ $test_name passed${NC}"
        else
            ((FAILED++))
            echo -e "${RED}✗ $test_name failed${NC}"
        fi
    fi
    echo ""
}

echo "=========================================="
echo "Running Tests"
echo "=========================================="

# Test scripts will be added here after implementation
run_test "$SCRIPT_DIR/test_system.sh"
run_test "$SCRIPT_DIR/test_folder.sh"
run_test "$SCRIPT_DIR/test_upload.sh"
run_test "$SCRIPT_DIR/test_file_ops.sh"
run_test "$SCRIPT_DIR/test_share.sh"
run_test "$SCRIPT_DIR/test_trash.sh"

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

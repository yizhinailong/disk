#!/bin/bash
# test/integration/lib/assert.sh
# Test assertion helpers.
#
# Provides:
#   assert_status()              - Assert HTTP status code matches expected
#   assert_json_field()          - Assert JSON field equals expected value (jq-based)
#   assert_json_field_numeric_gt - Assert JSON field > minimum value (jq-based)
#   assert_json_array_not_empty  - Assert JSON array field has length > 0 (jq-based)
#   assert_header_contains()     - Assert HTTP header contains expected substring
#
# Usage:
#   source "$SCRIPT_DIR/lib/assert.sh"
#   (Requires: common.sh and http.sh sourced first)

assert_status() {
    local label="$1"
    local actual="$2"
    local expected="$3"
    if [ "$actual" = "$expected" ]; then
        return 0
    else
        log_fail "$label: expected HTTP $expected, got HTTP $actual"
        return 1
    fi
}

assert_json_field() {
    local label="$1"
    local body="$2"
    local field="$3"
    local expected="$4"
    local actual
    actual=$(echo "$body" | jq -r ".$field // empty")
    if [ "$actual" = "$expected" ]; then
        return 0
    else
        log_fail "$label: expected .$field = '$expected', got '$actual'"
        return 1
    fi
}

assert_json_field_numeric_gt() {
    local label="$1"
    local body="$2"
    local field="$3"
    local min_value="$4"
    local actual
    actual=$(echo "$body" | jq -r ".$field // 0")
    if [ "$actual" -gt "$min_value" ]; then
        return 0
    else
        log_fail "$label: expected .$field > $min_value, got '$actual'"
        return 1
    fi
}

assert_json_array_not_empty() {
    local label="$1"
    local body="$2"
    local field="$3"
    local length
    length=$(echo "$body" | jq -r ".$field | length // 0")
    if [ "$length" -gt 0 ]; then
        return 0
    else
        log_fail "$label: expected .$field array to be non-empty, got length=$length"
        return 1
    fi
}

assert_header_contains() {
    local label="$1"
    local headers="$2"
    local header_name="$3"
    local expected_value="$4"
    local actual
    actual=$(header_value "$headers" "$header_name")
    if echo "$actual" | grep -qF "$expected_value"; then
        return 0
    else
        log_fail "$label: expected $header_name containing '$expected_value', got '$actual'"
        return 1
    fi
}

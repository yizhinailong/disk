#!/bin/bash
# test/integration/lib/auth.sh
# Authentication and server management helpers.
#
# Provides:
#   server_ready()       - Silent check if server responds
#   check_server()       - Verbose server check (logs result)
#   ensure_server()      - Start server if not ready (sets SERVER_PID, MANAGED_SERVER)
#   cleanup()            - Kill managed server on exit
#   send_login_request() - POST login, sets LOGIN_HTTP_CODE + LOGIN_BODY
#   do_login()           - Full login flow, sets TOKEN
#
# Usage:
#   source "$SCRIPT_DIR/lib/auth.sh"
#   (Requires: common.sh and http.sh sourced first)

# ─── Server state ──────────────────────────────────────────────────────────────

SERVER_PID=""
MANAGED_SERVER=0

# ─── Server readiness ──────────────────────────────────────────────────────────

server_ready() {
    local http_code
    http_code=$(curl -s -o /dev/null -w "%{http_code}" "${BASE_URL:-http://127.0.0.1:8080}/api/auth/login" 2>/dev/null || printf "000")
    case "$http_code" in
        400|401|405)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

check_server() {
    log_info "Checking server at ${BASE_URL:-http://127.0.0.1:8080}..."
    if server_ready; then
        log_pass "Server is running"
        return 0
    else
        log_fail "Server not responding"
        return 1
    fi
}

# ─── Server lifecycle ──────────────────────────────────────────────────────────

ensure_server() {
    if server_ready; then
        log_info "Using existing server at ${BASE_URL:-http://127.0.0.1:8080}"
        return 0
    fi

    local server_bin="${SERVER_BIN:-./build/linux-debug-clang/src/disk}"
    if [ ! -x "$server_bin" ] && [ -x "./build/linux-debug-clang/disk" ]; then
        server_bin="./build/linux-debug-clang/disk"
    fi

    if [ ! -x "$server_bin" ]; then
        log_fail "Server binary not found: $server_bin"
        exit 1
    fi

    local server_log="${SERVER_LOG:-.sisyphus/evidence/server.log}"
    mkdir -p "$(dirname "$server_log")"
    log_info "Starting server with $server_bin"
    JWT_SECRET="${JWT_SECRET:-dev-only-jwt-secret-key-change-in-production-2024}" "$server_bin" >"$server_log" 2>&1 &
    SERVER_PID=$!
    MANAGED_SERVER=1

    for _ in $(seq 1 30); do
        if server_ready; then
            log_pass "Server started"
            return 0
        fi
        sleep 1
    done

    log_fail "Server did not become ready"
    if [ -f "$server_log" ]; then
        cat "$server_log"
    fi
    exit 1
}

cleanup() {
    if [ "$MANAGED_SERVER" -eq 1 ] && [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}

# ─── Login helpers ─────────────────────────────────────────────────────────────

send_login_request() {
    local account="$1"
    local password="$2"
    local body
    body=$(python3 - "$account" "$password" <<'PY'
import json
import sys
print(json.dumps({"account": sys.argv[1], "password": sys.argv[2]}))
PY
)

    local response
    response=$(curl -sS -w "\n%{http_code}" -X POST "${BASE_URL:-http://127.0.0.1:8080}/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "$body")

    LOGIN_HTTP_CODE=$(printf '%s\n' "$response" | tail -n 1)
    LOGIN_BODY=$(printf '%s\n' "$response" | sed '$d')
}

do_login() {
    local account="${1:-${VALID_ACCOUNT:-${TEST_USER:-admin}}}"
    local password="${2:-${VALID_PASS:-${TEST_PASS:-Admin123}}}"
    log_info "Logging in as $account..."
    send_login_request "$account" "$password"

    TOKEN=$(json_field "$LOGIN_BODY" "data.access_token")

    if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
        log_fail "Login failed"
        printf '%s\n' "$LOGIN_BODY"
        return 1
    fi

    log_pass "Login successful"
    return 0
}

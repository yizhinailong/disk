#!/bin/bash
# test/integration/lib/http.sh
# HTTP utilities: JSON parsing, curl helpers, Redis cleanup.
#
# Usage:
#   source "$SCRIPT_DIR/lib/http.sh"
#   (Requires: common.sh sourced first for EVIDENCE_DIR)

# ─── JSON parsing ──────────────────────────────────────────────────────────────

json_field() {
    local json="$1"
    local path="$2"

    JSON_INPUT="$json" python3 - "$path" <<'PY'
import json
import os
import sys

try:
    data = json.loads(os.environ["JSON_INPUT"])
except Exception:
    print("")
    raise SystemExit(0)

value = data
for part in sys.argv[1].split('.'):
    if isinstance(value, dict) and part in value:
        value = value[part]
    else:
        print("")
        raise SystemExit(0)

if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("")
else:
    print(value)
PY
}

json_value() {
    json_field "$1" "$2"
}

json_int() {
    local json="$1"
    local path="$2"
    json_field "$json" "$path"
}

# ─── curl helper ───────────────────────────────────────────────────────────────
# Usage: curl_fetch <output_var_prefix> <curl_args...>
# Produces: ${prefix}_status, ${prefix}_headers, ${prefix}_body

curl_fetch() {
    local prefix="$1"
    shift

    local tmp_headers
    tmp_headers=$(mktemp)
    local tmp_body
    tmp_body=$(mktemp)

    local http_code
    http_code=$(curl -s -o "$tmp_body" -w "%{http_code}" -D "$tmp_headers" "$@")

    eval "${prefix}_status=\$http_code"
    eval "${prefix}_headers=\$(cat \"\$tmp_headers\")"
    eval "${prefix}_body=\$(cat \"\$tmp_body\")"

    rm -f "$tmp_headers" "$tmp_body"
}

# Extract a single header value (case-insensitive) from a headers block
header_value() {
    local headers="$1"
    local name="$2"
    echo "$headers" | grep -i "^${name}:" | head -1 | sed "s/^[^:]*:[[:space:]]*//" | tr -d '\r'
}

# ─── Redis cleanup ─────────────────────────────────────────────────────────────

redis_delete_pattern() {
    local pattern="$1"
    local host="${REDIS_HOST:-127.0.0.1}"
    local port="${REDIS_PORT:-6379}"

    python3 - "$host" "$port" "$pattern" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
pattern = sys.argv[3]
parts = ["KEYS", pattern]
payload = "*{}\r\n".format(len(parts))
for part in parts:
    payload += "${}\r\n{}\r\n".format(len(part.encode()), part)

with socket.create_connection((host, port), timeout=5) as sock:
    sock.sendall(payload.encode())
    reply = sock.recv(4096).decode(errors="ignore")

keys = []
if reply.startswith("*"):
    lines = reply.split("\r\n")
    i = 1
    while i < len(lines):
        if lines[i].startswith("$"):
            length = int(lines[i][1:])
            key = lines[i + 1] if i + 1 < len(lines) else ""
            if len(key.encode()) == length:
                keys.append(key)
            i += 2
        else:
            i += 1

if keys:
    del_parts = ["DEL"] + keys
    del_payload = "*{}\r\n".format(len(del_parts))
    for part in del_parts:
        del_payload += "${}\r\n{}\r\n".format(len(part.encode()), part)
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(del_payload.encode())
        sock.recv(1024)
PY
}

redis_delete_key() {
    local key="$1"
    local host="${REDIS_HOST:-127.0.0.1}"
    local port="${REDIS_PORT:-6379}"

    python3 - "$host" "$port" "$key" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
key = sys.argv[3]
parts = ["DEL", key]
payload = "*{}\r\n".format(len(parts))
for part in parts:
    payload += "${}\r\n{}\r\n".format(len(part.encode()), part)

with socket.create_connection((host, port), timeout=5) as sock:
    sock.sendall(payload.encode())
    reply = sock.recv(1024).decode(errors="ignore")

if not reply.startswith(":"):
    raise SystemExit("Unexpected Redis reply: {}".format(reply.strip()))
PY
}

#!/usr/bin/env bash
set -euo pipefail

web_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(cd "$web_root/../.." && pwd)"
server_bin="${DISK_E2E_SERVER_BIN:-$repo_root/build/linux-debug-clang/src/disk}"
backend_port="${DISK_E2E_BACKEND_PORT:-18080}"
web_port="${DISK_E2E_WEB_PORT:-15173}"
redis_port="${DISK_E2E_REDIS_PORT:-16379}"
runtime_dir=""
database_name=""
database_created=0
backend_pid=""
redis_pid=""

fail() {
  echo "[folder-tree-e2e] $1" >&2
  exit 1
}

for variable in DISK_E2E_USERNAME DISK_E2E_PASSWORD DISK_E2E_USER_EMAIL; do
  [[ -n "${!variable:-}" ]] || fail "missing required configuration: $variable"
done

for executable in bun curl createdb dropdb jq openssl psql redis-cli redis-server rg ss; do
  command -v "$executable" >/dev/null 2>&1 || fail "required executable not found: $executable"
done

[[ -x "$server_bin" ]] || fail "backend binary not found; build the linux-debug-clang preset first"

for port in "$backend_port" "$web_port" "$redis_port"; do
  if ss -ltn | awk '{print $4}' | rg -q ":${port}$"; then
    fail "required local port is already in use: $port"
  fi
done

runtime_dir="$(mktemp -d /tmp/disk-web-folder-tree-e2e.XXXXXX)"
database_name="disk_web_e2e_${$}_$(date +%s)"

db_host="${DISK_E2E_DB_HOST:-$(jq -r '.db_clients[0].host' "$repo_root/config.json")}"
db_port="${DISK_E2E_DB_PORT:-$(jq -r '.db_clients[0].port' "$repo_root/config.json")}"
db_user="${DISK_E2E_DB_USER:-$(jq -r '.db_clients[0].user' "$repo_root/config.json")}"
db_password="${DISK_E2E_DB_PASSWORD:-$(jq -r '.db_clients[0].passwd' "$repo_root/config.json")}"

cleanup() {
  local status=$?
  local cleanup_failed=0
  trap - EXIT

  if [[ -n "$backend_pid" ]]; then
    kill "$backend_pid" >/dev/null 2>&1 || true
    wait "$backend_pid" >/dev/null 2>&1 || true
  fi
  if [[ -n "$redis_pid" ]]; then
    kill "$redis_pid" >/dev/null 2>&1 || true
    wait "$redis_pid" >/dev/null 2>&1 || true
  fi
  if [[ "$database_created" == "1" ]]; then
    if ! PGPASSWORD="$db_password" dropdb \
      --if-exists --host "$db_host" --port "$db_port" --username "$db_user" \
      "$database_name" >/dev/null 2>&1; then
      cleanup_failed=1
    fi
  fi
  if [[ "$runtime_dir" == /tmp/disk-web-folder-tree-e2e.* && -d "$runtime_dir" ]]; then
    rm -rf -- "$runtime_dir"
  fi
  if [[ -n "$runtime_dir" && -e "$runtime_dir" ]]; then
    cleanup_failed=1
  fi

  if [[ "$cleanup_failed" == "1" ]]; then
    echo "[folder-tree-e2e] owned runtime cleanup failed" >&2
    [[ "$status" == "0" ]] && status=1
  else
    echo "[folder-tree-e2e] owned runtime cleanup complete"
  fi
  exit "$status"
}
trap cleanup EXIT

PGPASSWORD="$db_password" createdb \
  --host "$db_host" --port "$db_port" --username "$db_user" \
  "$database_name"
database_created=1
PGPASSWORD="$db_password" psql \
  --host "$db_host" --port "$db_port" --username "$db_user" --dbname "$database_name" \
  --quiet --set ON_ERROR_STOP=1 --file "$repo_root/sql/init.sql" \
  >"$runtime_dir/schema.log" 2>&1

redis-server \
  --bind 127.0.0.1 --port "$redis_port" --save '' --appendonly no \
  --dir "$runtime_dir" >"$runtime_dir/redis.log" 2>&1 &
redis_pid=$!
for _ in $(seq 1 50); do
  if redis-cli -h 127.0.0.1 -p "$redis_port" ping >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
redis-cli -h 127.0.0.1 -p "$redis_port" ping >/dev/null 2>&1 \
  || fail "isolated Redis did not become ready"

jq \
  --argjson backend_port "$backend_port" \
  --arg db_host "$db_host" \
  --argjson db_port "$db_port" \
  --arg database_name "$database_name" \
  --arg db_user "$db_user" \
  --arg db_password "$db_password" \
  --argjson redis_port "$redis_port" \
  --arg storage_path "$runtime_dir/storage" \
  --arg temp_path "$runtime_dir/uploads" \
  '.listeners[0].address = "127.0.0.1"
   | .listeners[0].port = $backend_port
   | .app.upload_path = $storage_path
   | .custom_config.disk.storage_base_path = $storage_path
   | .custom_config.disk.temp_upload_path = $temp_path
   | .db_clients[0].host = $db_host
   | .db_clients[0].port = $db_port
   | .db_clients[0].dbname = $database_name
   | .db_clients[0].user = $db_user
   | .db_clients[0].passwd = $db_password
   | .redis_clients[0].host = "127.0.0.1"
   | .redis_clients[0].port = $redis_port' \
  "$repo_root/config.json" >"$runtime_dir/config.json"

jwt_secret="$(openssl rand -hex 32)"
(
  cd "$runtime_dir"
  JWT_SECRET="$jwt_secret" "$server_bin"
) >"$runtime_dir/backend.log" 2>&1 &
backend_pid=$!

backend_status=""
for _ in $(seq 1 100); do
  backend_status="$(curl --silent --output /dev/null --write-out '%{http_code}' \
    "http://127.0.0.1:${backend_port}/api/auth/login" || true)"
  if [[ "$backend_status" == "400" || "$backend_status" == "401" || "$backend_status" == "405" ]]; then
    break
  fi
  sleep 0.1
done
[[ "$backend_status" == "400" || "$backend_status" == "401" || "$backend_status" == "405" ]] \
  || fail "isolated backend did not become ready"

export DISK_E2E_API_URL="http://127.0.0.1:${backend_port}/api"
export DISK_E2E_BASE_URL="http://127.0.0.1:${web_port}"
export DISK_E2E_PROXY_TARGET="http://127.0.0.1:${backend_port}"
export DISK_E2E_WEB_SERVER_COMMAND="bun run preview -- --host 127.0.0.1 --port ${web_port} --strictPort"
export DISK_E2E_REUSE_WEB_SERVER=0
export DISK_E2E_SEED_USER=1
export DISK_E2E_OUTPUT_DIR="$runtime_dir/playwright-output"

playwright_log="$runtime_dir/playwright.log"
set +e
(
  cd "$web_root"
  bunx playwright test e2e/folder-tree.spec.ts \
    --project=chromium --repeat-each=2 --workers=1 --reporter=line
) >"$playwright_log" 2>&1
playwright_status=$?
set -e

sensitive_output=0
for sensitive_value in "$DISK_E2E_USERNAME" "$DISK_E2E_PASSWORD" "$DISK_E2E_USER_EMAIL"; do
  if rg --fixed-strings --quiet -- "$sensitive_value" "$playwright_log" "$DISK_E2E_OUTPUT_DIR" 2>/dev/null; then
    sensitive_output=1
  fi
done
if rg --quiet --ignore-case \
  'Bearer[[:space:]]+[A-Za-z0-9._-]+|eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}' \
  "$playwright_log" "$DISK_E2E_OUTPUT_DIR" 2>/dev/null; then
  sensitive_output=1
fi
[[ "$sensitive_output" == "0" ]] || fail "sensitive value detected in Playwright output"

sed -n '1,240p' "$playwright_log"
exit "$playwright_status"

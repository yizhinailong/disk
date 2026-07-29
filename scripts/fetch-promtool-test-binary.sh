#!/bin/sh

set -eu

PROMETHEUS_VERSION="3.13.1"
PROMETHEUS_ARCHIVE_SHA256="962b812371aff838d152b6ff2d56fdb7a6396f5542f48ebf73421b9721f0d103"
PROMTOOL_SHA256="d2344bad40fbd10b8e4dd9ae712e69bab7add68feeed22675cb0b6f1d9e741d8"
PROMETHEUS_ARCHIVE="prometheus-${PROMETHEUS_VERSION}.linux-amd64.tar.gz"
PROMETHEUS_URL="https://github.com/prometheus/prometheus/releases/download/v${PROMETHEUS_VERSION}/${PROMETHEUS_ARCHIVE}"
PROMTOOL_MEMBER="prometheus-${PROMETHEUS_VERSION}.linux-amd64/promtool"

fail() {
    printf 'ERROR: %s\n' "$1" >&2
    exit 1
}

usage() {
    printf 'Usage: %s OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
}

[ "$#" -eq 1 ] || usage
[ -n "$1" ] || usage
[ "$1" != "/" ] || fail "output directory must not be the filesystem root"

[ "$(uname -s)" = "Linux" ] || fail "only the reviewed Linux binary is supported"
case "$(uname -m)" in
    x86_64 | amd64) ;;
    *) fail "only the reviewed linux-amd64 binary is supported" ;;
esac

for required_command in curl sha256sum mktemp tar; do
    command -v "$required_command" >/dev/null 2>&1 ||
        fail "$required_command is required"
done

OUTPUT_DIRECTORY=$1
mkdir -p -- "$OUTPUT_DIRECTORY"
[ -d "$OUTPUT_DIRECTORY" ] || fail "output path is not a directory"
OUTPUT_DIRECTORY=$(CDPATH= cd "$OUTPUT_DIRECTORY" && pwd -P)
[ "$OUTPUT_DIRECTORY" != "/" ] || fail "resolved output directory must not be the filesystem root"

ARCHIVE_TEMP_PATH=""
BINARY_TEMP_PATH=""
cleanup() {
    if [ -n "$ARCHIVE_TEMP_PATH" ] && [ -e "$ARCHIVE_TEMP_PATH" ]; then
        rm -f -- "$ARCHIVE_TEMP_PATH"
    fi
    if [ -n "$BINARY_TEMP_PATH" ] && [ -e "$BINARY_TEMP_PATH" ]; then
        rm -f -- "$BINARY_TEMP_PATH"
    fi
}
trap cleanup EXIT HUP INT TERM

verify_digest() {
    verify_path=$1
    verify_expected=$2
    verify_actual=$(sha256sum "$verify_path")
    verify_actual=${verify_actual%% *}
    [ "$verify_actual" = "$verify_expected" ]
}

destination="$OUTPUT_DIRECTORY/promtool"
if [ -e "$destination" ] || [ -L "$destination" ]; then
    [ -f "$destination" ] && [ ! -L "$destination" ] ||
        fail "$destination must be a regular file"
    verify_digest "$destination" "$PROMTOOL_SHA256" ||
        fail "$destination has an unexpected SHA-256; refusing to overwrite it"
    chmod 0755 "$destination"
    printf 'Reused verified %s\n' "$destination"
    printf 'PROMTOOL_BIN=%s\n' "$destination"
    exit 0
fi

ARCHIVE_TEMP_PATH=$(mktemp "$OUTPUT_DIRECTORY/.prometheus.download.XXXXXX")
curl \
    --fail \
    --location \
    --silent \
    --show-error \
    --proto '=https' \
    --tlsv1.2 \
    --retry 3 \
    --output "$ARCHIVE_TEMP_PATH" \
    "$PROMETHEUS_URL"
verify_digest "$ARCHIVE_TEMP_PATH" "$PROMETHEUS_ARCHIVE_SHA256" ||
    fail "downloaded Prometheus archive has an unexpected SHA-256"

BINARY_TEMP_PATH=$(mktemp "$OUTPUT_DIRECTORY/.promtool.extract.XXXXXX")
tar \
    --extract \
    --gzip \
    --to-stdout \
    --file "$ARCHIVE_TEMP_PATH" \
    "$PROMTOOL_MEMBER" >"$BINARY_TEMP_PATH"
verify_digest "$BINARY_TEMP_PATH" "$PROMTOOL_SHA256" ||
    fail "extracted promtool has an unexpected SHA-256"
chmod 0755 "$BINARY_TEMP_PATH"

if ! ln "$BINARY_TEMP_PATH" "$destination" 2>/dev/null; then
    [ -f "$destination" ] && [ ! -L "$destination" ] ||
        fail "$destination appeared during download and is not a regular file"
    verify_digest "$destination" "$PROMTOOL_SHA256" ||
        fail "$destination appeared during download with an unexpected SHA-256"
fi
chmod 0755 "$destination"
rm -f -- "$ARCHIVE_TEMP_PATH" "$BINARY_TEMP_PATH"
ARCHIVE_TEMP_PATH=""
BINARY_TEMP_PATH=""

printf 'Prepared %s\n' "$destination"
printf 'PROMTOOL_BIN=%s\n' "$destination"

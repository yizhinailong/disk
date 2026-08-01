#!/bin/sh

set -eu

PGBOUNCER_VERSION="1.25.2"
PGBOUNCER_ARCHIVE="pgbouncer-${PGBOUNCER_VERSION}.tar.gz"
PGBOUNCER_ARCHIVE_SHA256="924ad35113fd0a71c8e2dbe85b5d03445532e2b7b37a9f8a48983beea238b332"
PGBOUNCER_URL="https://www.pgbouncer.org/downloads/files/${PGBOUNCER_VERSION}/${PGBOUNCER_ARCHIVE}"
PGBOUNCER_SOURCE_DIRECTORY="pgbouncer-${PGBOUNCER_VERSION}"

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
[ "$(uname -s)" = "Linux" ] || fail "only Linux source builds are supported"

OUTPUT_DIRECTORY=$1
mkdir -p -- "$OUTPUT_DIRECTORY"
[ -d "$OUTPUT_DIRECTORY" ] || fail "output path is not a directory"
OUTPUT_DIRECTORY=$(CDPATH= cd "$OUTPUT_DIRECTORY" && pwd -P)
[ "$OUTPUT_DIRECTORY" != "/" ] || fail "resolved output directory must not be the filesystem root"

verify_version() {
    verify_path=$1
    [ -f "$verify_path" ] && [ ! -L "$verify_path" ] && [ -x "$verify_path" ] || return 1
    verify_output=$("$verify_path" -V 2>&1) || return 1
    newline='
'
    verify_first_line=${verify_output%%"$newline"*}
    [ "$verify_first_line" = "PgBouncer ${PGBOUNCER_VERSION}" ]
}

destination="$OUTPUT_DIRECTORY/pgbouncer"
if [ -e "$destination" ] || [ -L "$destination" ]; then
    verify_version "$destination" ||
        fail "$destination is not the reviewed PgBouncer ${PGBOUNCER_VERSION}; refusing to overwrite it"
    chmod 0755 "$destination"
    printf 'Reused verified %s\n' "$destination"
    printf 'DISK_PGBOUNCER_BIN=%s\n' "$destination"
    exit 0
fi

for required_command in awk cc curl find make mktemp pkg-config sha256sum tar; do
    command -v "$required_command" >/dev/null 2>&1 ||
        fail "$required_command is required"
done
for required_package in libevent libcares openssl; do
    pkg-config --exists "$required_package" ||
        fail "pkg-config package $required_package is required"
done

ARCHIVE_TEMP_PATH=""
BUILD_ROOT=""
cleanup() {
    if [ -n "$ARCHIVE_TEMP_PATH" ] && [ -e "$ARCHIVE_TEMP_PATH" ]; then
        rm -f -- "$ARCHIVE_TEMP_PATH"
    fi
    if [ -n "$BUILD_ROOT" ] && [ -d "$BUILD_ROOT" ]; then
        find "$BUILD_ROOT" -depth -delete
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

ARCHIVE_TEMP_PATH=$(mktemp "$OUTPUT_DIRECTORY/.pgbouncer.download.XXXXXX")
curl \
    --fail \
    --location \
    --silent \
    --show-error \
    --proto '=https' \
    --tlsv1.2 \
    --retry 3 \
    --output "$ARCHIVE_TEMP_PATH" \
    "$PGBOUNCER_URL"
verify_digest "$ARCHIVE_TEMP_PATH" "$PGBOUNCER_ARCHIVE_SHA256" ||
    fail "downloaded PgBouncer archive has an unexpected SHA-256"

BUILD_ROOT=$(mktemp -d "$OUTPUT_DIRECTORY/.pgbouncer.build.XXXXXX")
ARCHIVE_LIST="$BUILD_ROOT/archive.list"
tar --list --gzip --file "$ARCHIVE_TEMP_PATH" >"$ARCHIVE_LIST"
awk -v expected="$PGBOUNCER_SOURCE_DIRECTORY" '
    BEGIN { found = 0 }
    {
        entry = $0
        if (entry == expected || entry == expected "/") {
            found = 1
            next
        }
        if (index(entry, expected "/") != 1) {
            exit 1
        }
        remainder = substr(entry, length(expected) + 2)
        count = split(remainder, components, "/")
        for (component_index = 1; component_index <= count; ++component_index) {
            if (components[component_index] == "..") {
                exit 1
            }
        }
        found = 1
    }
    END { if (!found) exit 1 }
' "$ARCHIVE_LIST" || fail "PgBouncer archive has an unexpected path layout"

tar \
    --extract \
    --gzip \
    --file "$ARCHIVE_TEMP_PATH" \
    --directory "$BUILD_ROOT" \
    --no-same-owner \
    --no-same-permissions
SOURCE_DIRECTORY="$BUILD_ROOT/$PGBOUNCER_SOURCE_DIRECTORY"
[ -f "$SOURCE_DIRECTORY/configure" ] || fail "PgBouncer configure script is missing"

(
    cd "$SOURCE_DIRECTORY"
    ./configure --prefix="$BUILD_ROOT/install" --with-cares
)
make -C "$SOURCE_DIRECTORY" pgbouncer

built_binary="$SOURCE_DIRECTORY/pgbouncer"
chmod 0755 "$built_binary"
verify_version "$built_binary" || fail "built PgBouncer has an unexpected version"

if ! ln "$built_binary" "$destination" 2>/dev/null; then
    verify_version "$destination" ||
        fail "$destination appeared during build with an unexpected version"
fi
chmod 0755 "$destination"
rm -f -- "$ARCHIVE_TEMP_PATH"
ARCHIVE_TEMP_PATH=""
find "$BUILD_ROOT" -depth -delete
BUILD_ROOT=""

printf 'Prepared %s\n' "$destination"
printf 'DISK_PGBOUNCER_BIN=%s\n' "$destination"

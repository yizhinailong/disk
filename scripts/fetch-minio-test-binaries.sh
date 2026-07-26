#!/bin/sh

set -eu

MINIO_RELEASE="RELEASE.2025-04-22T22-12-26Z"
MINIO_SHA256="53e2a2cb16c5366ea6fbbc479c19ddb4c6a0948273e752f740fb1fbf27bb817c"
MINIO_URL="https://dl.min.io/server/minio/release/linux-amd64/archive/minio.${MINIO_RELEASE}"

MC_RELEASE="RELEASE.2025-04-16T18-13-26Z"
MC_SHA256="ac90da87a35641be5a0ac75d49de5161ddb47d629b5ba01261b0ae9e00aea15f"
MC_URL="https://dl.min.io/client/mc/release/linux-amd64/archive/mc.${MC_RELEASE}"

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

[ "$(uname -s)" = "Linux" ] || fail "only the reviewed Linux binaries are supported"
case "$(uname -m)" in
    x86_64 | amd64) ;;
    *) fail "only the reviewed linux-amd64 binaries are supported" ;;
esac

for required_command in curl sha256sum mktemp; do
    command -v "$required_command" >/dev/null 2>&1 ||
        fail "$required_command is required"
done

OUTPUT_DIRECTORY=$1
mkdir -p -- "$OUTPUT_DIRECTORY"
[ -d "$OUTPUT_DIRECTORY" ] || fail "output path is not a directory"
OUTPUT_DIRECTORY=$(CDPATH= cd "$OUTPUT_DIRECTORY" && pwd -P)

TEMP_PATH=""
cleanup() {
    if [ -n "$TEMP_PATH" ] && [ -e "$TEMP_PATH" ]; then
        rm -f -- "$TEMP_PATH"
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

prepare_binary() {
    prepare_name=$1
    prepare_url=$2
    prepare_sha256=$3
    prepare_destination="$OUTPUT_DIRECTORY/$prepare_name"

    if [ -e "$prepare_destination" ] || [ -L "$prepare_destination" ]; then
        [ -f "$prepare_destination" ] && [ ! -L "$prepare_destination" ] ||
            fail "$prepare_destination must be a regular file"
        verify_digest "$prepare_destination" "$prepare_sha256" ||
            fail "$prepare_destination has an unexpected SHA-256; refusing to overwrite it"
        chmod 0755 "$prepare_destination"
        printf 'Reused verified %s\n' "$prepare_destination"
        return
    fi

    TEMP_PATH=$(mktemp "$OUTPUT_DIRECTORY/.${prepare_name}.download.XXXXXX")
    curl \
        --fail \
        --location \
        --silent \
        --show-error \
        --proto '=https' \
        --tlsv1.2 \
        --retry 3 \
        --output "$TEMP_PATH" \
        "$prepare_url"
    verify_digest "$TEMP_PATH" "$prepare_sha256" ||
        fail "downloaded $prepare_name has an unexpected SHA-256"
    chmod 0755 "$TEMP_PATH"

    if ! ln "$TEMP_PATH" "$prepare_destination" 2>/dev/null; then
        [ -f "$prepare_destination" ] && [ ! -L "$prepare_destination" ] ||
            fail "$prepare_destination appeared during download and is not a regular file"
        verify_digest "$prepare_destination" "$prepare_sha256" ||
            fail "$prepare_destination appeared during download with an unexpected SHA-256"
    fi
    chmod 0755 "$prepare_destination"
    rm -f -- "$TEMP_PATH"
    TEMP_PATH=""
    printf 'Prepared %s\n' "$prepare_destination"
}

prepare_binary "minio" "$MINIO_URL" "$MINIO_SHA256"
prepare_binary "mc" "$MC_URL" "$MC_SHA256"

printf 'DISK_MINIO_BIN=%s\n' "$OUTPUT_DIRECTORY/minio"
printf 'DISK_MC_BIN=%s\n' "$OUTPUT_DIRECTORY/mc"

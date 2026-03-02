#!/bin/sh
# verify_no_qml_logic.sh
# Scans QML files for forbidden business-logic patterns.
# Usage: verify_no_qml_logic.sh [directory]
# Default directory: ui/diskqml/qml/

TARGET_DIR="${1:-ui/diskqml/qml/}"

VIOLATION_FILE=$(mktemp)

# Find all .qml files recursively
qml_files=$(find "$TARGET_DIR" -name "*.qml" -type f)

if [ -z "$qml_files" ]; then
    echo "WARNING: No .qml files found in $TARGET_DIR"
    exit 0
fi

check_pattern() {
    pattern="$1"
    while IFS= read -r filepath; do
        matches=$(grep -n "$pattern" "$filepath" 2>/dev/null)
        if [ -n "$matches" ]; then
            echo "FAIL: Forbidden pattern '$pattern' found in: $filepath"
            echo "$matches"
            echo 1 > "$VIOLATION_FILE"
        fi
    done <<FILELIST
$qml_files
FILELIST
}

check_pattern 'XMLHttpRequest'
check_pattern 'fetch('
check_pattern 'WebSocket'
check_pattern 'WorkerScript'
check_pattern 'QtQuick\.LocalStorage'
check_pattern 'Qt\.labs\.settings'
check_pattern 'import QtWebSockets'
check_pattern 'import QtQml\.WorkerScript'

if [ -s "$VIOLATION_FILE" ]; then
    rm -f "$VIOLATION_FILE"
    exit 1
fi
rm -f "$VIOLATION_FILE"

echo "PASS: No forbidden business logic patterns found in QML files"
exit 0

#!/usr/bin/env bash
set -euo pipefail

# Check for deprecated C++ APIs in model files
# Scans src/models/*.cpp for wstring_convert and codecvt_utf8_utf16

MODELS_DIR="src/models"
DEPRECATED_PATTERNS=(
    "std::wstring_convert"
    "std::codecvt_utf8_utf16"
    "std::codecvt_utf8"
)

FOUND_DEPRECATED=0

echo "Checking for deprecated API patterns in ${MODELS_DIR}..."
echo ""

if [ ! -d "${MODELS_DIR}" ]; then
    echo "Error: Directory ${MODELS_DIR} not found"
    exit 1
fi

# Check each deprecated pattern
for pattern in "${DEPRECATED_PATTERNS[@]}"; do
    # Extract token name (the last part after ::)
    token_name=$(echo "${pattern}" | sed 's/.*:://')

    # Search for the pattern in all .cpp files
    results=$(grep -rn "${pattern}" "${MODELS_DIR}"/*.cpp 2>/dev/null || true)

    if [ -n "${results}" ]; then
        FOUND_DEPRECATED=1
        echo "✗ Deprecated API found: ${pattern}"
        echo "${results}" | while read -r line; do
            file=$(echo "${line}" | cut -d: -f1)
            line_num=$(echo "${line}" | cut -d: -f2)
            echo "  → ${file}:${line_num}"
        done
        echo ""
    fi
done

if [ ${FOUND_DEPRECATED} -eq 0 ]; then
    echo "✓ No deprecated API patterns found in model files"
    exit 0
else
    echo "✗ Found deprecated API usage in model files"
    echo "Deprecated APIs should be replaced with modern alternatives:"
    echo "  - std::wstring_convert → use std::wstring_convert replacement (e.g., <codecvt> is deprecated)"
    echo "  - std::codecvt_utf8_utf16 → use std::mbstowcs / wcstombs or ICU library"
    exit 1
fi

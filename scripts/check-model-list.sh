#!/bin/bash
# Check if CMakeLists.txt model lists match actual generated files in src/models/

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODELS_DIR="$PROJECT_ROOT/src/models"
CMAKE_FILE="$PROJECT_ROOT/src/CMakeLists.txt"

echo "Checking model list synchronization..."
echo "Project root: $PROJECT_ROOT"
echo

# Get list of actual model headers from src/models/
echo "1. Scanning actual model files in $MODELS_DIR..."
actual_models=$(ls -1 "$MODELS_DIR"/*.hpp 2>/dev/null | xargs -n1 basename 2>/dev/null | sed 's/\.hpp$//' | sort)
actual_count=$(echo "$actual_models" | wc -l)
echo "   Found $actual_count models:"
echo "$actual_models" | sed 's/^/     - /'
echo

# Extract Models_HEADERS from CMakeLists.txt
echo "2. Extracting Models_HEADERS from $CMAKE_FILE..."
cmake_headers=$(grep -A 20 "^set(Models_HEADERS" "$CMAKE_FILE" | grep "models/.*\.hpp" | sed 's/.*models\///;s/\.hpp.*//' | sort)
cmake_headers_count=$(echo "$cmake_headers" | wc -l)
echo "   Found $cmake_headers_count models in Models_HEADERS:"
echo "$cmake_headers" | sed 's/^/     - /'
echo

# Extract Models_SOURCES from CMakeLists.txt
echo "3. Extracting Models_SOURCES from $CMAKE_FILE..."
cmake_sources=$(grep -A 20 "^set(Models_SOURCES" "$CMAKE_FILE" | grep "models/.*\.cpp" | sed 's/.*models\///;s/\.cpp.*//' | sort)
cmake_sources_count=$(echo "$cmake_sources" | wc -l)
echo "   Found $cmake_sources_count models in Models_SOURCES:"
echo "$cmake_sources" | sed 's/^/     - /'
echo

# Compare sets
echo "4. Comparing sets..."

# Models in actual but NOT in CMakeLists.txt
missing_in_cmake=$(comm -23 <(echo "$actual_models") <(echo "$cmake_headers"))
if [ -n "$missing_in_cmake" ]; then
    echo "   ❌ Models in src/models/ but MISSING from CMakeLists.txt:"
    echo "$missing_in_cmake" | sed 's/^/     - /'
    sync_status=1
else
    echo "   ✓ All models in src/models/ are in CMakeLists.txt"
fi

# Models in CMakeLists.txt but NOT in actual files
missing_in_actual=$(comm -13 <(echo "$actual_models") <(echo "$cmake_headers"))
if [ -n "$missing_in_actual" ]; then
    echo "   ❌ Models in CMakeLists.txt but MISSING from src/models/:"
    echo "$missing_in_actual" | sed 's/^/     - /'
    sync_status=1
else
    echo "   ✓ All models in CMakeLists.txt exist in src/models/"
fi

# Compare Headers and Sources match
if [ "$cmake_headers" = "$cmake_sources" ]; then
    echo "   ✓ Models_HEADERS and Models_SOURCES match"
else
    echo "   ❌ Models_HEADERS and Models_SOURCES do NOT match:"
    headers_only=$(comm -23 <(echo "$cmake_headers") <(echo "$cmake_sources"))
    if [ -n "$headers_only" ]; then
        echo "     Only in Models_HEADERS:"
        echo "$headers_only" | sed 's/^/       - /'
    fi
    sources_only=$(comm -13 <(echo "$cmake_headers") <(echo "$cmake_sources"))
    if [ -n "$sources_only" ]; then
        echo "     Only in Models_SOURCES:"
        echo "$sources_only" | sed 's/^/       - /'
    fi
    sync_status=1
fi

echo
echo "5. Final result:"
if [ -z "$sync_status" ]; then
    echo "   ✅ IN SYNC - CMakeLists.txt matches src/models/"
    exit 0
else
    echo "   ❌ OUT OF SYNC - discrepancies found"
    exit 1
fi

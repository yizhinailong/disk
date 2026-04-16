#!/bin/bash
# Canonical Regeneration Contract for ORM Models
# Usage: ./regenerate-models.sh [--dry-run]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MODELS_DIR="$PROJECT_ROOT/src/models"

# Canonical model file list - expected generated models
declare -a EXPECTED_MODELS=(
    "FileContents"
    "Files"
    "Folders"
    "OperationLogs"
    "ShareFiles"
    "Shares"
    "Trash"
    "UploadTaskChunks"
    "UploadTasks"
    "Users"
)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Drift detection function
detect_drift() {
    local has_drift=0

    log_info "Detecting model drift in $MODELS_DIR"
    echo ""

    for model in "${EXPECTED_MODELS[@]}"; do
        local cpp_file="$MODELS_DIR/${model}.cpp"
        local hpp_file="$MODELS_DIR/${model}.hpp"

        if [[ ! -f "$cpp_file" ]]; then
            log_error "Missing model file: ${model}.cpp"
            has_drift=1
        fi

        if [[ ! -f "$hpp_file" ]]; then
            log_error "Missing model file: ${model}.hpp"
            has_drift=1
        fi
    done

    for file in "$MODELS_DIR"/*.cpp; do
        if [[ -f "$file" ]]; then
            local basename=$(basename "$file" .cpp)
            local is_expected=0

            for model in "${EXPECTED_MODELS[@]}"; do
                if [[ "$basename" == "$model" ]]; then
                    is_expected=1
                    break
                fi
            done

            if [[ $is_expected -eq 0 ]]; then
                log_warn "Extra model file detected: ${basename}.cpp"
                has_drift=1
            fi
        fi
    done

    for file in "$MODELS_DIR"/*.hpp; do
        if [[ -f "$file" ]]; then
            local basename=$(basename "$file" .hpp)
            local is_expected=0

            for model in "${EXPECTED_MODELS[@]}"; do
                if [[ "$basename" == "$model" ]]; then
                    is_expected=1
                    break
                fi
            done

            if [[ $is_expected -eq 0 ]]; then
                log_warn "Extra model file detected: ${basename}.hpp"
                has_drift=1
            fi
        fi
    done

    echo ""

    if [[ $has_drift -eq 0 ]]; then
        log_info "✓ Model files are in sync (no drift detected)"
        return 0
    else
        log_error "✗ Model drift detected"
        return 1
    fi
}

regenerate_models() {
    log_info "Checking for drogon_ctl..."
    if ! command -v drogon_ctl &> /dev/null; then
        log_error "drogon_ctl not found. Please install Drogon framework."
        return 1
    fi

    log_info "Regenerating models using drogon_ctl..."
    log_info "Config: $MODELS_DIR/model.json"

    log_info "Cleaning stale generated files (.cc/.h)..."
    rm -f "$MODELS_DIR"/*.cc "$MODELS_DIR"/*.h

    if ! drogon_ctl create model "$MODELS_DIR"; then
        log_error "Model generation failed"
        return 1
    fi

    log_info "✓ Models regenerated successfully"

    echo ""
    log_info "Normalizing file extensions (.cc → .cpp, .h → .hpp)..."
    for cc_file in "$MODELS_DIR"/*.cc; do
        [[ -f "$cc_file" ]] || continue
        cpp_file="${cc_file%.cc}.cpp"
        mv -f "$cc_file" "$cpp_file"
    done
    for h_file in "$MODELS_DIR"/*.h; do
        [[ -f "$h_file" ]] || continue
        hpp_file="${h_file%.h}.hpp"
        mv -f "$h_file" "$hpp_file"
    done

    log_info "Updating include directives (.h → .hpp)..."
    for model_cpp in "$MODELS_DIR"/*.cpp; do
        [[ -f "$model_cpp" ]] || continue
        sed -i 's/#include "\(.*\)\.h"/#include "\1.hpp"/g' "$model_cpp"
    done

    log_info "Applying post-generation patch for deprecated UTF conversion"

    for model_cpp in "$MODELS_DIR"/*.cpp; do
        [[ -f "$model_cpp" ]] || continue
        perl -0777 -i -pe 's/if\(pJson\.isString\(\) && std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>\{\}\n\s+\.from_bytes\(pJson\.asCString\(\)\)\.size\(\) > (\d+)\)/if(pJson.isString() \&\& std::string(pJson.asCString()).size() > $1)/g' "$model_cpp"
    done

    if grep -q -E 'wstring_convert|codecvt_utf8_utf16' "$MODELS_DIR"/*.cpp; then
        log_error "Post-generation patch failed: deprecated UTF conversion still exists"
        grep -n -E 'wstring_convert|codecvt_utf8_utf16' "$MODELS_DIR"/*.cpp || true
        return 1
    fi

    log_info "✓ Post-generation patch applied successfully"

    return 0
}

main() {
    local dry_run=false

    while [[ $# -gt 0 ]]; do
        case $1 in
            --dry-run)
                dry_run=true
                shift
                ;;
            *)
                log_error "Unknown option: $1"
                echo "Usage: $0 [--dry-run]"
                exit 1
                ;;
        esac
    done

    echo "=========================================="
    echo "  ORM Model Regeneration Contract"
    echo "=========================================="
    echo ""

    detect_drift
    local drift_status=$?

    if [[ "$dry_run" == true ]]; then
        echo ""
        log_info "Dry-run mode: Skipping actual regeneration"
        exit $drift_status
    fi

    # If drift detected, ask for confirmation
    if [[ $drift_status -ne 0 ]]; then
        echo ""
        log_warn "Drift detected. Continue with regeneration? (y/n)"
        read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then
            log_info "Aborting regeneration"
            exit 1
        fi
    fi

    echo ""
    regenerate_models
    local regenerate_status=$?

    if [[ $regenerate_status -eq 0 ]]; then
        echo ""
        log_info "✓ Regeneration completed successfully"
        exit 0
    else
        echo ""
        log_error "✗ Regeneration failed"
        exit 1
    fi
}

main "$@"

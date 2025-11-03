#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Convenient wrapper for running clang-tidy analysis on the project
# This script provides easy access to clang-tidy with common options

set -euo pipefail

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Usage information
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] [PATHS...]

Run clang-tidy analysis on the project codebase.

OPTIONS:
    --fix               Apply automatic fixes
    --check-only        Only check without fixing (default)
    --src-only          Analyze only src/ directory (exclude tests/)
    --tests-only        Analyze only tests/ directory
    --stats             Show statistics after analysis
    -j, --jobs N        Number of parallel jobs (default: nproc)
    -h, --help          Show this help message

PATHS:
    If no paths specified, analyzes: src/daemon src/krunner src/config src/shared
    You can specify custom paths to analyze specific files/directories

EXAMPLES:
    # Analyze main source code
    $(basename "$0")

    # Analyze with automatic fixes
    $(basename "$0") --fix

    # Analyze only tests
    $(basename "$0") --tests-only

    # Analyze specific file
    $(basename "$0") src/daemon/services/yubikey_service.cpp

    # Show statistics
    $(basename "$0") --stats

NOTES:
    - Requires compile_commands.json in build/ directory
    - Uses configuration from .clang-tidy file
    - Respects WarningsAsErrors setting
    - Generated files (autogen/, moc_*.cpp) are excluded
EOF
}

# Check prerequisites
check_prerequisites() {
    if ! command -v run-clang-tidy &> /dev/null; then
        echo -e "${RED}Error: run-clang-tidy not found in PATH${NC}" >&2
        echo "Install clang-tools package" >&2
        exit 1
    fi

    if [[ ! -f "$COMPILE_COMMANDS" ]]; then
        echo -e "${RED}Error: compile_commands.json not found${NC}" >&2
        echo "Run 'cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON' first" >&2
        exit 1
    fi

    # Check if GCC flags have been fixed
    if grep -q -- '-mno-direct-extern-access' "$COMPILE_COMMANDS" 2>/dev/null; then
        echo -e "${YELLOW}Warning: compile_commands.json contains GCC-specific flags${NC}" >&2
        echo -e "${YELLOW}Run: python3 scripts/fix_compile_commands.py${NC}" >&2
        echo ""
    fi
}

# Parse arguments
FIX_MODE=""
PATHS=()
JOBS="$(nproc)"
SHOW_STATS=false
SRC_ONLY=false
TESTS_ONLY=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fix)
            FIX_MODE="-fix"
            shift
            ;;
        --check-only)
            FIX_MODE=""
            shift
            ;;
        --src-only)
            SRC_ONLY=true
            shift
            ;;
        --tests-only)
            TESTS_ONLY=true
            shift
            ;;
        --stats)
            SHOW_STATS=true
            shift
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            echo -e "${RED}Unknown option: $1${NC}" >&2
            usage
            exit 1
            ;;
        *)
            PATHS+=("$1")
            shift
            ;;
    esac
done

# Determine paths to analyze
if [[ ${#PATHS[@]} -eq 0 ]]; then
    if [[ "$TESTS_ONLY" == true ]]; then
        PATHS=("tests/")
    elif [[ "$SRC_ONLY" == true ]]; then
        PATHS=("src/daemon" "src/krunner" "src/config" "src/shared")
    else
        # Default: analyze main source (exclude tests and generated files)
        PATHS=("src/daemon" "src/krunner" "src/config" "src/shared")
    fi
fi

# Run prerequisite checks
check_prerequisites

# Display configuration
echo -e "${BLUE}=== Clang-Tidy Analysis ===${NC}"
echo -e "Project root: ${PROJECT_ROOT}"
echo -e "Mode: ${FIX_MODE:-check-only}"
echo -e "Paths: ${PATHS[*]}"
echo -e "Jobs: ${JOBS}"
echo ""

# Build clang-tidy command
CMD=(
    run-clang-tidy
    -p "$BUILD_DIR"
    -j "$JOBS"
    -header-filter='src/.*\.h$'
)

# Add fix mode if requested
if [[ -n "$FIX_MODE" ]]; then
    CMD+=("$FIX_MODE")
    echo -e "${YELLOW}Warning: --fix will modify source files!${NC}"
    echo -e "Press Ctrl+C within 3 seconds to cancel..."
    sleep 3
fi

# Add paths
CMD+=("${PATHS[@]}")

# Run analysis
echo -e "${GREEN}Running: ${CMD[*]}${NC}"
echo ""

LOG_FILE="${PROJECT_ROOT}/clang-tidy-$(date +%Y%m%d-%H%M%S).log"
"${CMD[@]}" 2>&1 | tee "$LOG_FILE"

EXIT_CODE=${PIPESTATUS[0]}

# Show statistics if requested
if [[ "$SHOW_STATS" == true ]]; then
    echo ""
    echo -e "${BLUE}=== Analysis Statistics ===${NC}"

    TOTAL_WARNINGS=$(grep -c "warning:" "$LOG_FILE" || true)
    TOTAL_ERRORS=$(grep -c "error:" "$LOG_FILE" || true)

    echo -e "Total warnings: ${YELLOW}${TOTAL_WARNINGS}${NC}"
    echo -e "Total errors: ${RED}${TOTAL_ERRORS}${NC}"

    echo ""
    echo "Top 10 warning categories:"
    grep -o "\[.*\]$" "$LOG_FILE" | sort | uniq -c | sort -rn | head -10 || true
fi

echo ""
echo -e "Log saved to: ${LOG_FILE}"

if [[ $EXIT_CODE -eq 0 ]]; then
    echo -e "${GREEN}✓ Analysis completed successfully${NC}"
else
    echo -e "${RED}✗ Analysis found issues (exit code: $EXIT_CODE)${NC}"
fi

exit $EXIT_CODE

#!/bin/bash
# Generate code coverage report for krunner-yubikey
#
# Requirements:
#   - lcov (sudo apt install lcov or sudo pacman -S lcov)
#   - gcov (usually included with GCC)
#
# Usage:
#   ./scripts/generate-coverage.sh [build_dir]
#
# Example:
#   ./scripts/generate-coverage.sh build
#   ./scripts/generate-coverage.sh build-coverage

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="${1:-build}"
COVERAGE_OUTPUT_DIR="${BUILD_DIR}/coverage_html"
COVERAGE_INFO="${BUILD_DIR}/coverage.info"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo -e "${GREEN}=== KRunner YubiKey Code Coverage Generator ===${NC}"
echo "Project root: ${PROJECT_ROOT}"
echo "Build directory: ${BUILD_DIR}"
echo ""

# Check if lcov is installed
if ! command -v lcov &> /dev/null; then
    echo -e "${RED}Error: lcov is not installed${NC}"
    echo "Install with: sudo apt install lcov (Debian/Ubuntu)"
    echo "           or: sudo pacman -S lcov (Arch)"
    exit 1
fi

# Check if build directory exists
if [ ! -d "${PROJECT_ROOT}/${BUILD_DIR}" ]; then
    echo -e "${RED}Error: Build directory '${BUILD_DIR}' does not exist${NC}"
    echo "Create it with: cmake -B ${BUILD_DIR} -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug"
    exit 1
fi

# Check if project was built with coverage
if [ ! -f "${PROJECT_ROOT}/${BUILD_DIR}/CMakeCache.txt" ]; then
    echo -e "${RED}Error: CMakeCache.txt not found in build directory${NC}"
    exit 1
fi

if ! grep -q "ENABLE_COVERAGE:BOOL=ON" "${PROJECT_ROOT}/${BUILD_DIR}/CMakeCache.txt"; then
    echo -e "${YELLOW}Warning: ENABLE_COVERAGE is not enabled in CMakeCache.txt${NC}"
    echo "Rebuild with: cmake -B ${BUILD_DIR} -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug"
    echo "Continuing anyway..."
fi

cd "${PROJECT_ROOT}/${BUILD_DIR}"

echo -e "${GREEN}Step 1: Capturing coverage data...${NC}"
lcov --capture \
     --directory . \
     --output-file "${COVERAGE_INFO}" \
     --rc lcov_branch_coverage=1

echo -e "${GREEN}Step 2: Removing system/external files...${NC}"
lcov --remove "${COVERAGE_INFO}" \
     '/usr/*' \
     '*/build/*' \
     '*/Qt6/*' \
     '*/KF6/*' \
     '*_autogen/*' \
     '*/moc_*.cpp' \
     --output-file "${COVERAGE_INFO}" \
     --rc lcov_branch_coverage=1

echo -e "${GREEN}Step 3: Generating HTML report...${NC}"
genhtml "${COVERAGE_INFO}" \
        --output-directory "${COVERAGE_OUTPUT_DIR}" \
        --title "KRunner YubiKey Code Coverage" \
        --legend \
        --show-details \
        --branch-coverage \
        --rc lcov_branch_coverage=1

echo ""
echo -e "${GREEN}=== Coverage report generated successfully! ===${NC}"
echo ""
echo "Summary:"
lcov --summary "${COVERAGE_INFO}" --rc lcov_branch_coverage=1
echo ""
echo "View the report:"
echo "  firefox ${COVERAGE_OUTPUT_DIR}/index.html"
echo "  or"
echo "  xdg-open ${COVERAGE_OUTPUT_DIR}/index.html"
echo ""
echo "Coverage file: ${COVERAGE_INFO}"
echo "HTML report: ${COVERAGE_OUTPUT_DIR}/"

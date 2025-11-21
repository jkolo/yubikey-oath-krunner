#!/bin/bash
# Container test runner script
# This script sets up isolated environment and runs all tests
# Should be executed inside Docker container

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}YubiKey OATH KRunner - Container Tests${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Verify we're in container (by checking for testuser or specific env vars)
if [[ "${CONTAINER:-}" != "true" ]] && [[ "$(id -u)" -eq 0 ]]; then
    echo -e "${YELLOW}Warning: This script is designed to run in Docker container${NC}"
    echo -e "${YELLOW}Consider using docker-compose or Dockerfile.test${NC}"
    echo ""
fi

# ==============================================================================
# Environment Isolation Setup
# ==============================================================================

echo -e "${BLUE}[1/7] Setting up isolated environment...${NC}"

# Create isolated XDG directories
export TEST_HOME="${TEST_HOME:-/tmp/test-home-$$}"
export XDG_DATA_HOME="${TEST_HOME}/.local/share"
export XDG_CONFIG_HOME="${TEST_HOME}/.config"
export XDG_CACHE_HOME="${TEST_HOME}/.cache"
export XDG_RUNTIME_DIR="${TEST_HOME}/.runtime"
export XDG_STATE_HOME="${TEST_HOME}/.local/state"

mkdir -p "${XDG_DATA_HOME}/krunner-yubikey" \
         "${XDG_DATA_HOME}/kwalletd" \
         "${XDG_CONFIG_HOME}" \
         "${XDG_CACHE_HOME}" \
         "${XDG_RUNTIME_DIR}" \
         "${XDG_STATE_HOME}"

chmod 700 "${XDG_RUNTIME_DIR}"

echo "  ✓ XDG directories created at: ${TEST_HOME}"

# ==============================================================================
# D-Bus Session Setup
# ==============================================================================

echo -e "${BLUE}[2/7] Starting isolated D-Bus session...${NC}"

# Kill any existing test D-Bus sessions
pkill -9 -f "dbus-daemon.*${XDG_RUNTIME_DIR}" 2>/dev/null || true

# Start D-Bus session daemon
DBUS_SOCKET="${XDG_RUNTIME_DIR}/dbus-session"
rm -f "${DBUS_SOCKET}"

dbus-daemon --session \
            --nofork \
            --print-address \
            --address="unix:path=${DBUS_SOCKET}" &
DBUS_PID=$!

# Wait for D-Bus to start
sleep 1

if ! kill -0 "${DBUS_PID}" 2>/dev/null; then
    echo -e "${RED}✗ Failed to start D-Bus daemon${NC}"
    exit 1
fi

export DBUS_SESSION_BUS_ADDRESS="unix:path=${DBUS_SOCKET}"
echo "  ✓ D-Bus session running (PID: ${DBUS_PID})"

# Cleanup function
cleanup() {
    local exit_code=$?
    echo ""
    echo -e "${BLUE}[Cleanup] Stopping services...${NC}"

    # Kill D-Bus
    if [[ -n "${DBUS_PID:-}" ]] && kill -0 "${DBUS_PID}" 2>/dev/null; then
        kill "${DBUS_PID}" 2>/dev/null || true
        wait "${DBUS_PID}" 2>/dev/null || true
        echo "  ✓ D-Bus daemon stopped"
    fi

    # Clean up test directories (if not preserving)
    if [[ "${PRESERVE_TEST_DATA:-}" != "true" ]]; then
        rm -rf "${TEST_HOME}" 2>/dev/null || true
        echo "  ✓ Test data cleaned up"
    else
        echo "  ℹ Test data preserved at: ${TEST_HOME}"
    fi

    exit ${exit_code}
}

trap cleanup EXIT INT TERM

# ==============================================================================
# Qt/KDE Environment
# ==============================================================================

echo -e "${BLUE}[3/7] Configuring Qt/KDE environment...${NC}"

# Use offscreen platform for Qt (no X11 required)
export QT_QPA_PLATFORM=offscreen

# Enable debug logging for our components only
export QT_LOGGING_RULES="*.debug=false;pl.jkolo.yubikey.oath.*=true"
export QT_LOGGING_TO_CONSOLE=1

# Disable KCrash (no crash handler in tests)
export KDE_DEBUG=1

# KWallet configuration (use test wallet, no prompts)
export KWALLETD_TESTMODE=1

echo "  ✓ Qt platform: ${QT_QPA_PLATFORM}"
echo "  ✓ Logging configured"
echo "  ✓ KWallet test mode enabled"

# ==============================================================================
# Build Configuration
# ==============================================================================

echo -e "${BLUE}[4/7] Configuring build...${NC}"

BUILD_DIR="${BUILD_DIR:-build-container-test}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
ENABLE_COVERAGE="${ENABLE_COVERAGE:-OFF}"

# Clean previous build if requested
if [[ "${CLEAN_BUILD:-}" == "true" ]]; then
    echo "  ℹ Cleaning previous build..."
    rm -rf "${BUILD_DIR}"
fi

# Configure with CMake
if [[ ! -d "${BUILD_DIR}" ]]; then
    cmake -B "${BUILD_DIR}" \
          -G Ninja \
          -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
          -DCMAKE_CXX_COMPILER=clang++ \
          -DCMAKE_C_COMPILER=clang \
          -DENABLE_CLANG_TIDY=OFF \
          -DENABLE_COVERAGE="${ENABLE_COVERAGE}" \
          -DCMAKE_INSTALL_PREFIX=/tmp/test-install

    echo "  ✓ CMake configuration complete"
else
    echo "  ℹ Using existing build directory"
fi

# ==============================================================================
# Build
# ==============================================================================

echo -e "${BLUE}[5/7] Building project...${NC}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "  ✓ Build complete"

# ==============================================================================
# Run Tests
# ==============================================================================

echo -e "${BLUE}[6/7] Running tests...${NC}"
echo ""

cd "${BUILD_DIR}"

# Run CTest with detailed output
CTEST_ARGS=(
    --output-on-failure
    --verbose
)

# Add parallel execution if not in coverage mode
if [[ "${ENABLE_COVERAGE}" != "ON" ]]; then
    CTEST_ARGS+=(--parallel "$(nproc)")
fi

# Run tests
if ctest "${CTEST_ARGS[@]}"; then
    TEST_RESULT=0
    echo ""
    echo -e "${GREEN}✓ All tests passed!${NC}"
else
    TEST_RESULT=1
    echo ""
    echo -e "${RED}✗ Some tests failed${NC}"
fi

cd ..

# ==============================================================================
# Coverage Report (if enabled)
# ==============================================================================

if [[ "${ENABLE_COVERAGE}" == "ON" ]] && [[ ${TEST_RESULT} -eq 0 ]]; then
    echo ""
    echo -e "${BLUE}[7/7] Generating coverage report...${NC}"

    cd "${BUILD_DIR}"

    # Generate coverage data
    lcov --capture \
         --directory . \
         --output-file coverage.info \
         --rc lcov_branch_coverage=1

    # Filter out system headers and test files
    lcov --remove coverage.info \
         '/usr/*' \
         '*/tests/*' \
         '*/mocks/*' \
         --output-file coverage_filtered.info \
         --rc lcov_branch_coverage=1

    # Generate HTML report
    genhtml coverage_filtered.info \
            --output-directory coverage_html \
            --branch-coverage \
            --legend

    echo "  ✓ Coverage report generated at: ${BUILD_DIR}/coverage_html/index.html"

    # Show coverage summary
    lcov --summary coverage_filtered.info --rc lcov_branch_coverage=1

    cd ..
else
    echo ""
    echo -e "${BLUE}[7/7] Coverage report skipped${NC}"
fi

# ==============================================================================
# Summary
# ==============================================================================

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Test Execution Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Build directory:    ${BUILD_DIR}"
echo "Test home:          ${TEST_HOME}"
echo "D-Bus socket:       ${DBUS_SOCKET}"
echo "Coverage enabled:   ${ENABLE_COVERAGE}"
echo ""

if [[ ${TEST_RESULT} -eq 0 ]]; then
    echo -e "${GREEN}All tests passed successfully! ✓${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed ✗${NC}"
    echo ""
    echo "To investigate failures:"
    echo "  1. Check test output above"
    echo "  2. Review logs in ${BUILD_DIR}/Testing/Temporary/"
    echo "  3. Re-run with PRESERVE_TEST_DATA=true to inspect test data"
    exit 1
fi

#!/bin/bash
# CI test runner script
# Runs tests in isolated D-Bus session with Xvfb for GitLab CI
# Usage: ./scripts/ci-test-runner.sh [unit|e2e|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-clang-release"

cd "$PROJECT_ROOT"

# Parse arguments
TEST_MODE="${1:-all}"

echo "======================================"
echo "CI Test Runner"
echo "======================================"
echo "Mode: $TEST_MODE"
echo "Build dir: $BUILD_DIR"
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: Build directory not found at $BUILD_DIR"
    echo "Please run CMake build first"
    exit 1
fi

# Ensure Xvfb is running for input tests
echo "Starting Xvfb..."
Xvfb :99 -screen 0 1024x768x24 -nolisten tcp &
XVFB_PID=$!
export DISPLAY=:99

# Give Xvfb time to start
sleep 2

# Cleanup function
cleanup() {
    local EXIT_CODE=$?
    echo ""
    echo "Cleaning up..."

    # Kill Xvfb
    if [ -n "$XVFB_PID" ]; then
        kill $XVFB_PID 2>/dev/null || true
    fi

    # Kill any remaining dbus-daemon processes from this session
    pkill -P $$ dbus-daemon 2>/dev/null || true

    exit $EXIT_CODE
}

trap cleanup EXIT INT TERM

# Run tests in isolated D-Bus session
run_tests() {
    local test_filter=$1
    local output_file=$2

    echo "Running tests with filter: $test_filter"
    echo "Output file: $output_file"
    echo ""

    cd "$BUILD_DIR"

    # Use dbus-run-session to isolate D-Bus environment
    dbus-run-session -- ctest \
        -R "$test_filter" \
        --output-on-failure \
        --no-compress-output \
        --output-junit "$output_file" \
        --parallel 4

    local result=$?

    cd "$PROJECT_ROOT"
    return $result
}

# Run tests based on mode
case "$TEST_MODE" in
    unit)
        echo "Running unit and service tests..."
        run_tests "test_(result|async_result|pcsc_worker|credential|icon|secure_memory|touch|notification|clipboard|action|match|password_service|device_lifecycle_service|credential_service|oath_database|secret_storage)" "$BUILD_DIR/test-results-unit.xml"
        ;;
    e2e)
        echo "Running E2E tests..."
        run_tests "test_e2e" "$BUILD_DIR/test-results-e2e.xml"
        ;;
    all)
        echo "Running all tests..."

        # Run unit tests first
        echo ""
        echo "======================================"
        echo "Phase 1: Unit and Service Tests"
        echo "======================================"
        run_tests "test_(result|async_result|pcsc_worker|credential|icon|secure_memory|touch|notification|clipboard|action|match|password_service|device_lifecycle_service|credential_service|oath_database|secret_storage)" "$BUILD_DIR/test-results-unit.xml"
        UNIT_RESULT=$?

        # Run E2E tests
        echo ""
        echo "======================================"
        echo "Phase 2: E2E Tests"
        echo "======================================"
        run_tests "test_e2e" "$BUILD_DIR/test-results-e2e.xml"
        E2E_RESULT=$?

        # Check results
        if [ $UNIT_RESULT -ne 0 ] || [ $E2E_RESULT -ne 0 ]; then
            echo ""
            echo "======================================"
            echo "Test Results: FAILED"
            echo "======================================"
            echo "Unit tests: $([ $UNIT_RESULT -eq 0 ] && echo "PASSED" || echo "FAILED")"
            echo "E2E tests: $([ $E2E_RESULT -eq 0 ] && echo "PASSED" || echo "FAILED")"
            exit 1
        fi
        ;;
    *)
        echo "ERROR: Unknown test mode '$TEST_MODE'"
        echo "Valid modes: unit, e2e, all"
        exit 1
        ;;
esac

echo ""
echo "======================================"
echo "Test Results: PASSED"
echo "======================================"
echo "All tests completed successfully!"

#!/bin/bash
# Run tests locally using podman-compose
# Usage: ./scripts/run-tests-podman.sh [test-filter]
# Examples:
#   ./scripts/run-tests-podman.sh         # Run all tests
#   ./scripts/run-tests-podman.sh unit    # Run unit tests only
#   ./scripts/run-tests-podman.sh e2e     # Run E2E tests only

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Parse arguments
TEST_FILTER="${1:-all}"

echo -e "${GREEN}Running tests with podman-compose${NC}"
echo "Test filter: $TEST_FILTER"
echo ""

# Check if podman-compose is available
if ! command -v podman-compose &> /dev/null; then
    echo -e "${RED}ERROR: podman-compose not found${NC}"
    echo "Install with: pip install podman-compose"
    exit 1
fi

# Build test image if needed
echo -e "${YELLOW}Building test container...${NC}"
podman-compose build test

# Run tests based on filter
case "$TEST_FILTER" in
    all)
        echo -e "${YELLOW}Running all tests...${NC}"
        podman-compose run --rm test
        ;;
    unit)
        echo -e "${YELLOW}Running unit tests only...${NC}"
        podman-compose run --rm test sh -c "cd build-clang-release && ctest -R 'test_(result|async_result|pcsc_worker|credential|icon|secure_memory|touch|notification|clipboard|action|match|password_service|device_lifecycle_service|credential_service|oath_database|secret_storage)' --output-on-failure"
        ;;
    e2e)
        echo -e "${YELLOW}Running E2E tests only...${NC}"
        podman-compose run --rm test sh -c "cd build-clang-release && ctest -R 'test_e2e' --output-on-failure"
        ;;
    *)
        echo -e "${YELLOW}Running tests matching: $TEST_FILTER${NC}"
        podman-compose run --rm test sh -c "cd build-clang-release && ctest -R '$TEST_FILTER' --output-on-failure"
        ;;
esac

TEST_RESULT=$?

if [ $TEST_RESULT -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✓ All tests passed!${NC}"

    # Check if coverage report was generated
    if [ -d "coverage-report" ]; then
        echo ""
        echo "Coverage report available at: coverage-report/index.html"
        echo "View with: xdg-open coverage-report/index.html"
    fi
else
    echo ""
    echo -e "${RED}✗ Tests failed with exit code $TEST_RESULT${NC}"
fi

exit $TEST_RESULT

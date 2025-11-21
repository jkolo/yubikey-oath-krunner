#!/bin/bash
# Host-side wrapper for running tests in Docker container
# Provides convenient interface for containerized testing

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

# ==============================================================================
# Configuration
# ==============================================================================

# Auto-detect container runtime (prefer Podman over Docker for better security)
if command -v podman &> /dev/null; then
    CONTAINER_RUNTIME="podman"
    COMPOSE_CMD="podman-compose"
elif command -v docker &> /dev/null && docker ps &> /dev/null 2>&1; then
    CONTAINER_RUNTIME="docker"
    COMPOSE_CMD="docker-compose"
else
    echo -e "${RED}Error: Neither Podman nor Docker found${NC}"
    echo "Please install Podman (recommended) or Docker to run containerized tests"
    echo ""
    echo "Arch Linux: sudo pacman -S podman podman-compose"
    exit 1
fi

COMPOSE_FILE="docker-compose.test.yml"
SERVICE_NAME="${SERVICE_NAME:-tests}"
DOCKER_COMPOSE="${COMPOSE_CMD} -f ${COMPOSE_FILE}"

echo -e "${BLUE}Using container runtime: ${CONTAINER_RUNTIME}${NC}"

# ==============================================================================
# Functions
# ==============================================================================

show_help() {
    cat << EOF
${BLUE}YubiKey OATH KRunner - Containerized Test Runner${NC}
${BLUE}Container Runtime: ${CONTAINER_RUNTIME:-auto-detect}${NC}

Usage: $0 [COMMAND] [OPTIONS]

Commands:
  test          Run tests in container (default)
  coverage      Run tests with coverage report
  release       Run tests with Release build
  shell         Open interactive shell in test container
  build         Build test container image
  clean         Remove containers and volumes
  help          Show this help message

Options:
  --clean       Clean build before running tests
  --preserve    Preserve test data after run
  --pull        Pull latest base image before build

Environment Variables:
  CLEAN_BUILD         Set to 'true' to clean build directory
  PRESERVE_TEST_DATA  Set to 'true' to keep test data
  BUILD_TYPE          Build type (Debug/Release)

Examples:
  # Run tests (default)
  $0 test

  # Run with coverage report
  $0 coverage

  # Run with clean build
  $0 test --clean

  # Open interactive shell
  $0 shell

  # Clean up everything
  $0 clean

EOF
}

build_image() {
    local pull_flag=""
    if [[ "${PULL_IMAGE:-false}" == "true" ]]; then
        pull_flag="--pull"
    fi

    echo -e "${BLUE}Building test container image...${NC}"
    ${DOCKER_COMPOSE} build ${pull_flag} tests
    echo -e "${GREEN}✓ Image built successfully${NC}"
}

run_tests() {
    local service="$1"
    shift
    local extra_args=()

    # Parse options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --clean)
                export CLEAN_BUILD=true
                shift
                ;;
            --preserve)
                export PRESERVE_TEST_DATA=true
                shift
                ;;
            *)
                echo -e "${RED}Unknown option: $1${NC}"
                exit 1
                ;;
        esac
    done

    echo -e "${BLUE}Running tests in container (service: ${service})...${NC}"
    echo ""

    # Run tests
    if ${DOCKER_COMPOSE} run --rm "${service}"; then
        echo ""
        echo -e "${GREEN}✓ Tests completed successfully!${NC}"
        return 0
    else
        echo ""
        echo -e "${RED}✗ Tests failed${NC}"
        return 1
    fi
}

open_shell() {
    echo -e "${BLUE}Opening interactive shell in test container...${NC}"
    echo ""
    ${DOCKER_COMPOSE} run --rm shell
}

clean_all() {
    echo -e "${YELLOW}Cleaning up containers and volumes...${NC}"

    # Stop and remove containers
    ${DOCKER_COMPOSE} down -v

    # Remove volumes explicitly
    ${CONTAINER_RUNTIME} volume rm yubikey-oath-build yubikey-oath-coverage yubikey-oath-release 2>/dev/null || true

    echo -e "${GREEN}✓ Cleanup complete${NC}"
}

copy_coverage_report() {
    local container_id
    container_id=$(${CONTAINER_RUNTIME} ps -a -q -f name=yubikey-oath-tests-coverage | head -1)

    if [[ -z "${container_id}" ]]; then
        echo -e "${YELLOW}No coverage container found${NC}"
        return 1
    fi

    local output_dir="${PROJECT_ROOT}/coverage-report"
    mkdir -p "${output_dir}"

    echo -e "${BLUE}Copying coverage report from container...${NC}"

    if ${CONTAINER_RUNTIME} cp "${container_id}:/home/testuser/yubikey-oath-krunner/build-container-coverage/coverage_html" "${output_dir}/"; then
        echo -e "${GREEN}✓ Coverage report copied to: ${output_dir}/coverage_html/index.html${NC}"
        echo ""
        echo "Open with: xdg-open ${output_dir}/coverage_html/index.html"
    else
        echo -e "${RED}✗ Failed to copy coverage report${NC}"
        return 1
    fi
}

# ==============================================================================
# Main
# ==============================================================================

COMMAND="${1:-test}"
shift || true

case "${COMMAND}" in
    test)
        run_tests tests "$@"
        ;;
    coverage)
        run_tests tests-coverage "$@"
        EXIT_CODE=$?
        if [[ ${EXIT_CODE} -eq 0 ]]; then
            copy_coverage_report || true
        fi
        exit ${EXIT_CODE}
        ;;
    release)
        run_tests tests-release "$@"
        ;;
    shell)
        open_shell
        ;;
    build)
        build_image
        ;;
    clean)
        clean_all
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        echo -e "${RED}Unknown command: ${COMMAND}${NC}"
        echo ""
        show_help
        exit 1
        ;;
esac

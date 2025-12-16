#!/bin/bash
# Build container images locally using podman
# Usage: ./scripts/build-container.sh [stage]
# Stages: builder (default), test, artifacts, all

set -e

REGISTRY="registry.kolosowscy.pl/krunner-yubikey-oath"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Parse arguments
STAGE="${1:-builder}"

echo -e "${GREEN}Building OCI container images for KRunner YubiKey OATH Plugin${NC}"
echo "Registry: $REGISTRY"
echo "Stage: $STAGE"
echo ""

# Build function
build_stage() {
    local stage=$1
    local tag="${REGISTRY}:${stage}-latest"

    echo -e "${YELLOW}Building stage: $stage${NC}"
    podman build \
        --format=oci \
        --target "$stage" \
        --tag "$tag" \
        --file Containerfile \
        --layers \
        .

    echo -e "${GREEN}✓ Built: $tag${NC}"
    echo ""
}

# Build requested stage(s)
case "$STAGE" in
    builder)
        build_stage "builder"
        ;;
    test)
        build_stage "builder"
        build_stage "test"
        ;;
    artifacts)
        build_stage "builder"
        build_stage "artifacts"
        ;;
    all)
        build_stage "builder"
        build_stage "test"
        build_stage "artifacts"
        ;;
    *)
        echo "ERROR: Unknown stage '$STAGE'"
        echo "Valid stages: builder, test, artifacts, all"
        exit 1
        ;;
esac

echo -e "${GREEN}Build completed successfully!${NC}"
echo ""
echo "Next steps:"
echo "  - Run tests: podman-compose run --rm test"
echo "  - Interactive shell: podman-compose run --rm dev"
echo "  - Push to registry: podman push $REGISTRY:builder-latest"

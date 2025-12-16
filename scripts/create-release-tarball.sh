#!/bin/bash
# Create release tarball from git tag
# Usage: ./scripts/create-release-tarball.sh v2.1.0

set -e

VERSION="$1"

if [ -z "$VERSION" ]; then
    echo "ERROR: Version argument required"
    echo "Usage: $0 v2.1.0"
    exit 1
fi

# Remove 'v' prefix if present for tarball name
VERSION_NUMBER="${VERSION#v}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Creating release tarball${NC}"
echo "Version: $VERSION"
echo "Tarball: krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz"
echo ""

# Check if tag exists
if ! git rev-parse "$VERSION" >/dev/null 2>&1; then
    echo "ERROR: Git tag '$VERSION' does not exist"
    echo "Available tags:"
    git tag -l | tail -5
    exit 1
fi

# Create tarball from git archive
echo -e "${YELLOW}Creating tarball from git archive...${NC}"
git archive \
    --format=tar.gz \
    --prefix="krunner-yubikey-oath-${VERSION_NUMBER}/" \
    --output="krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz" \
    "$VERSION"

# Generate SHA256 checksum
echo -e "${YELLOW}Generating SHA256 checksum...${NC}"
sha256sum "krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz" > "krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz.sha256"

# Display results
echo ""
echo -e "${GREEN}✓ Release tarball created successfully!${NC}"
echo ""
echo "Files created:"
echo "  - krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz"
echo "  - krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz.sha256"
echo ""
echo "Tarball size: $(du -h "krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz" | cut -f1)"
echo ""
echo "SHA256 checksum:"
cat "krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz.sha256"
echo ""
echo "Next steps:"
echo "  1. Upload to GitHub release: ./scripts/create-github-release.sh $VERSION"
echo "  2. Update AUR package: ./scripts/update-aur-package.sh $VERSION"

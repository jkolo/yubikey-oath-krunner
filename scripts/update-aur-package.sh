#!/bin/bash
# Update AUR package with new release
# Requires: SSH key for aur@aur.archlinux.org configured
# Usage: ./scripts/update-aur-package.sh v2.1.0

set -e

VERSION="$1"

if [ -z "$VERSION" ]; then
    echo "ERROR: Version argument required"
    echo "Usage: $0 v2.1.0"
    exit 1
fi

# Remove 'v' prefix
VERSION_NUMBER="${VERSION#v}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}Updating AUR package${NC}"
echo "Version: $VERSION_NUMBER"
echo "Package: krunner-yubikey-oath"
echo ""

# Check if SHA256 file exists
CHECKSUM_FILE="krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz.sha256"
if [ ! -f "$CHECKSUM_FILE" ]; then
    echo -e "${RED}ERROR: Checksum file not found: $CHECKSUM_FILE${NC}"
    exit 1
fi

# Extract SHA256 sum
SHA256=$(cut -d' ' -f1 "$CHECKSUM_FILE")
echo "SHA256: $SHA256"

# Get download URL (from GitHub release)
if [ -f ".github-release-url.txt" ]; then
    DOWNLOAD_URL=$(cat ".github-release-url.txt")
else
    DOWNLOAD_URL="https://github.com/jkolo/yubikey-oath-krunner/releases/download/$VERSION/krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz"
fi

echo "Download URL: $DOWNLOAD_URL"
echo ""

# Create temporary directory for AUR clone
AUR_DIR=$(mktemp -d)
echo -e "${YELLOW}Cloning AUR repository...${NC}"

# Clone AUR package
git clone ssh://aur@aur.archlinux.org/krunner-yubikey-oath.git "$AUR_DIR"

cd "$AUR_DIR"

# Configure git if AUR_GIT_* variables are set (for CI)
if [ -n "$AUR_GIT_EMAIL" ]; then
    git config user.email "$AUR_GIT_EMAIL"
fi
if [ -n "$AUR_GIT_NAME" ]; then
    git config user.name "$AUR_GIT_NAME"
fi

# Backup current PKGBUILD
cp PKGBUILD PKGBUILD.bak

echo -e "${YELLOW}Updating PKGBUILD...${NC}"

# Update PKGBUILD
sed -i "s/^pkgver=.*/pkgver=${VERSION_NUMBER}/" PKGBUILD
sed -i "s/^pkgrel=.*/pkgrel=1/" PKGBUILD
sed -i "s|^source=(.*|source=(\"${DOWNLOAD_URL}\")|" PKGBUILD
sed -i "s/^sha256sums=.*/sha256sums=('${SHA256}')/" PKGBUILD

# Generate .SRCINFO
echo -e "${YELLOW}Generating .SRCINFO...${NC}"
makepkg --printsrcinfo > .SRCINFO

# Show diff
echo ""
echo -e "${YELLOW}Changes to PKGBUILD:${NC}"
diff -u PKGBUILD.bak PKGBUILD || true
echo ""

# Commit and push
echo -e "${YELLOW}Committing changes...${NC}"
git add PKGBUILD .SRCINFO

# Check if there are changes to commit
if git diff --cached --quiet; then
    echo -e "${YELLOW}No changes to commit (already up to date)${NC}"
else
    git commit -m "Update to version ${VERSION_NUMBER}"

    echo -e "${YELLOW}Pushing to AUR...${NC}"
    git push origin master

    echo ""
    echo -e "${GREEN}✓ AUR package updated successfully!${NC}"
fi

# Cleanup
cd "$PROJECT_ROOT"
rm -rf "$AUR_DIR"

echo ""
echo "AUR package URL: https://aur.archlinux.org/packages/krunner-yubikey-oath"
echo ""
echo "Users will be notified of the update automatically."
echo "Build will be available after AUR processes the update."

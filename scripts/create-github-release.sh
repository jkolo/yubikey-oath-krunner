#!/bin/bash
# Create GitHub release and upload tarball
# Requires: gh CLI (GitHub CLI) and GITHUB_TOKEN environment variable
# Usage: ./scripts/create-github-release.sh v2.1.0

set -e

VERSION="$1"

if [ -z "$VERSION" ]; then
    echo "ERROR: Version argument required"
    echo "Usage: $0 v2.1.0"
    exit 1
fi

# Remove 'v' prefix for tarball name
VERSION_NUMBER="${VERSION#v}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}Creating GitHub release${NC}"
echo "Version: $VERSION"
echo "Repository: github.com/jkolo/yubikey-oath-krunner"
echo ""

# Check if gh CLI is installed
if ! command -v gh &> /dev/null; then
    echo -e "${RED}ERROR: GitHub CLI (gh) not found${NC}"
    echo "Install from: https://cli.github.com/"
    echo "Or via package manager: pacman -S github-cli"
    exit 1
fi

# Check if GITHUB_TOKEN is set
if [ -z "$GITHUB_TOKEN" ]; then
    echo -e "${RED}ERROR: GITHUB_TOKEN environment variable not set${NC}"
    echo "Set it with: export GITHUB_TOKEN=ghp_your_token_here"
    exit 1
fi

# Check if tarball exists
TARBALL="krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz"
CHECKSUM="krunner-yubikey-oath-${VERSION_NUMBER}.tar.gz.sha256"

if [ ! -f "$TARBALL" ]; then
    echo -e "${RED}ERROR: Tarball not found: $TARBALL${NC}"
    echo "Create it first with: ./scripts/create-release-tarball.sh $VERSION"
    exit 1
fi

if [ ! -f "$CHECKSUM" ]; then
    echo -e "${RED}ERROR: Checksum file not found: $CHECKSUM${NC}"
    exit 1
fi

# Extract release notes from CHANGELOG.md if it exists
RELEASE_NOTES=""
if [ -f "CHANGELOG.md" ]; then
    echo -e "${YELLOW}Extracting release notes from CHANGELOG.md...${NC}"

    # Extract section for this version
    # Looks for: ## [2.1.0] - 2024-11-17 or ## v2.1.0
    RELEASE_NOTES=$(awk "
        /^## \[?${VERSION#v}\]?/ { found=1; next }
        /^## / { if (found) exit }
        found { print }
    " CHANGELOG.md || echo "")

    if [ -z "$RELEASE_NOTES" ]; then
        echo -e "${YELLOW}No release notes found in CHANGELOG.md for $VERSION${NC}"
        RELEASE_NOTES="Release version $VERSION_NUMBER

See full changelog at: https://git.kolosowscy.pl/jkolo/krunner-yubikey-oath/-/blob/$VERSION/CHANGELOG.md"
    fi
else
    RELEASE_NOTES="Release version $VERSION_NUMBER"
fi

# Create temporary notes file
NOTES_FILE=$(mktemp)
echo "$RELEASE_NOTES" > "$NOTES_FILE"
echo "" >> "$NOTES_FILE"
echo "---" >> "$NOTES_FILE"
echo "" >> "$NOTES_FILE"
echo "**SHA256 Checksum:**" >> "$NOTES_FILE"
echo '```' >> "$NOTES_FILE"
cat "$CHECKSUM" >> "$NOTES_FILE"
echo '```' >> "$NOTES_FILE"

# Create GitHub release
echo -e "${YELLOW}Creating GitHub release...${NC}"
gh release create "$VERSION" \
    --repo "jkolo/yubikey-oath-krunner" \
    --title "Release $VERSION_NUMBER" \
    --notes-file "$NOTES_FILE" \
    "$TARBALL" \
    "$CHECKSUM"

# Cleanup
rm -f "$NOTES_FILE"

echo ""
echo -e "${GREEN}✓ GitHub release created successfully!${NC}"
echo ""
echo "Release URL: https://github.com/jkolo/yubikey-oath-krunner/releases/tag/$VERSION"
echo ""

# Get download URL for AUR
echo -e "${YELLOW}Getting download URL for AUR...${NC}"
DOWNLOAD_URL="https://github.com/jkolo/yubikey-oath-krunner/releases/download/$VERSION/$TARBALL"
echo "Download URL: $DOWNLOAD_URL"
echo ""

# Save download URL to file for AUR script
echo "$DOWNLOAD_URL" > ".github-release-url.txt"
echo "✓ Download URL saved to .github-release-url.txt"
echo ""
echo "Next step: ./scripts/update-aur-package.sh $VERSION"

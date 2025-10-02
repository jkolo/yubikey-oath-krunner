#!/bin/bash
# SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Helper script to clean compile_commands.json for clang-tidy compatibility
#
# Problem: GCC-specific flags like -mno-direct-extern-access are not recognized by clang-tidy
# Solution: Remove these flags from the compilation database before running clang-tidy
#
# Usage:
#   cd build
#   ../scripts/clean-compile-commands.sh
#   cmake --build .

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$(pwd)}"
COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"

if [ ! -f "$COMPILE_COMMANDS" ]; then
    echo "Error: compile_commands.json not found at: $COMPILE_COMMANDS"
    echo "Make sure you're running this from the build directory or set BUILD_DIR environment variable"
    exit 1
fi

echo "Cleaning compile_commands.json..."
echo "File: $COMPILE_COMMANDS"

# Remove GCC-specific flags that clang-tidy doesn't understand
sed -i 's/-mno-direct-extern-access//g' "$COMPILE_COMMANDS"

echo "✓ Removed -mno-direct-extern-access"
echo "✓ compile_commands.json is now clean for clang-tidy"

#!/bin/bash
# Generate all icon sizes for hicolor theme from source PNG/SVG files
# This script is called automatically during CMake build process

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SOURCE_ICONS_DIR="$PROJECT_ROOT/src/shared/resources/models"
SOURCE_SVG="$PROJECT_ROOT/src/shared/resources/yubikey.svg"
OUTPUT_DIR="${CMAKE_BINARY_DIR:-$PROJECT_ROOT/build}/generated-icons/hicolor"

# Icon sizes to generate (freedesktop.org hicolor theme standard)
SIZES=(16 22 32 48 64 128 256)

echo "Generating icon sizes for hicolor theme..."
echo "Source: $SOURCE_ICONS_DIR"
echo "Output: $OUTPUT_DIR"

# Create output directory structure
for size in "${SIZES[@]}"; do
    mkdir -p "$OUTPUT_DIR/${size}x${size}/devices"
done
mkdir -p "$OUTPUT_DIR/scalable/devices"

# Process all PNG icon files
echo ""
echo "Processing PNG icons..."
find "$SOURCE_ICONS_DIR" -name "*.png" -type f | while read -r source_file; do
    filename=$(basename "$source_file")
    icon_name="${filename%.png}"

    echo "  Processing: $icon_name"

    # Get source image dimensions
    dimensions=$(identify -format "%wx%h" "$source_file")

    # Generate each size
    for size in "${SIZES[@]}"; do
        output_file="$OUTPUT_DIR/${size}x${size}/devices/$filename"

        # Use high-quality Lanczos filter for resizing
        magick "$source_file" \
            -resize "${size}x${size}" \
            -quality 95 \
            "$output_file"

        # Optimize PNG (reduce file size without quality loss)
        if command -v optipng &> /dev/null; then
            # Run optipng and capture output; grep returns 0 if found lines, 1 if no matches
            # We want to show real errors but hide "already optimized" messages
            output=$(optipng -quiet -o7 "$output_file" 2>&1) || {
                echo "Warning: optipng failed for $output_file" >&2
            }
            # Show output only if it's not the "already optimized" message
            if [ -n "$output" ] && ! echo "$output" | grep -q "already optimized"; then
                echo "$output"
            fi
        fi
    done
done

# Copy SVG to scalable directory and rename to yubikey-oath
echo ""
echo "Processing SVG icons..."
if [ -f "$SOURCE_SVG" ]; then
    cp "$SOURCE_SVG" "$OUTPUT_DIR/scalable/devices/yubikey-oath.svg"
    echo "  Copied yubikey.svg → yubikey-oath.svg"
fi

# Count generated files
total_files=$(find "$OUTPUT_DIR" -type f | wc -l)
echo ""
echo "Icon generation complete!"
echo "  Generated: $total_files files"
echo "  Sizes: ${SIZES[*]}"
echo "  Output: $OUTPUT_DIR"

# Verify critical icons exist
echo ""
echo "Verifying critical icons..."
critical_icons=("yubikey-5c-nfc" "yubikey-5-nfc" "nitrokey-3c" "yubikey-oath")
for icon in "${critical_icons[@]}"; do
    if [ "$icon" = "yubikey-oath" ]; then
        if [ -f "$OUTPUT_DIR/scalable/devices/$icon.svg" ]; then
            echo "  ✓ $icon.svg (scalable)"
        else
            echo "  ✗ $icon.svg MISSING!"
            exit 1
        fi
    else
        if [ -f "$OUTPUT_DIR/256x256/devices/$icon.png" ]; then
            echo "  ✓ $icon.png (all sizes)"
        else
            echo "  ✗ $icon.png MISSING!"
            exit 1
        fi
    fi
done

echo ""
echo "All critical icons verified successfully!"

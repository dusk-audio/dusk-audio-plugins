#!/bin/bash
# Build plugins in container for glibc compatibility
# Produces binaries compatible with Debian 11+, Ubuntu 20.04+, and most modern Linux distros
# Uses Podman (preferred on Fedora) or Docker

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="luna-plugins-builder"

# Use podman if available, otherwise docker
if command -v podman &>/dev/null; then
    CONTAINER_CMD="podman"
elif command -v docker &>/dev/null; then
    CONTAINER_CMD="docker"
else
    echo "Error: Neither podman nor docker found. Please install one."
    exit 1
fi

echo "=== Luna Co. Audio Plugin Release Builder ==="
echo "Using: $CONTAINER_CMD"
echo "Building with Ubuntu 22.04 (glibc 2.35) for maximum compatibility"
echo ""

# Security options for Fedora/SELinux (needed for rootless podman)
SECURITY_OPTS=""
if [ "$CONTAINER_CMD" = "podman" ]; then
    SECURITY_OPTS="--security-opt label=disable"
fi

# Build image if it doesn't exist
if ! $CONTAINER_CMD image inspect "$IMAGE_NAME" &>/dev/null; then
    echo "Building container image..."
    $CONTAINER_CMD build $SECURITY_OPTS -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile.build" "$SCRIPT_DIR"
fi

# Create output directory
OUTPUT_DIR="$PROJECT_DIR/release"
mkdir -p "$OUTPUT_DIR"

# Run the build inside container
echo "Starting build..."
# Verify JUCE directory exists
JUCE_DIR="$PROJECT_DIR/../JUCE"
if [ ! -d "$JUCE_DIR" ]; then
    echo "Error: JUCE directory not found at $JUCE_DIR"
    exit 1
fi

# Run the build inside container
echo "Starting build..."
$CONTAINER_CMD run --rm $SECURITY_OPTS \
    -v "$PROJECT_DIR:/src:ro" \
    -v "$OUTPUT_DIR:/output:Z" \
    -v "$JUCE_DIR:/JUCE:ro" \
    "$IMAGE_NAME" \
    bash -c "
        set -e

        # Copy source to writable location (excluding build directory)
        mkdir -p /tmp/plugins
        cd /src
        find . -maxdepth 1 ! -name build ! -name . -exec cp -r {} /tmp/plugins/ \;
        cd /tmp/plugins

        # Update JUCE path in CMakeLists.txt for container
        sed -i "s|${ORIGINAL_JUCE_PATH:-/home/marc/projects/JUCE}|/JUCE|g" CMakeLists.txt

        # Create fresh build directory
        mkdir -p build && cd build

        # Configure
        cmake .. -DCMAKE_BUILD_TYPE=Release

        # Build all plugins
        cmake --build . -j\$(nproc)

        # Copy built plugins to output
        echo 'Copying VST3 plugins...'
        find . -name '*.vst3' -type d -exec cp -r {} /output/ \;

        echo 'Copying LV2 plugins...'
        find . -name '*.lv2' -type d -exec cp -r {} /output/ \;

        echo ''
        echo '=== Build complete! ==='
        echo 'Plugins are in: release/'
    "

echo ""
echo "Release build complete!"
echo "Output directory: $OUTPUT_DIR"
echo ""
echo "These binaries are compatible with:"
echo "  - Debian 12 (Bookworm) and newer"
echo "  - Ubuntu 22.04 LTS and newer"
echo "  - Most Linux distributions from 2022 onwards"
ls -la "$OUTPUT_DIR"

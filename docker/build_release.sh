#!/bin/bash
# Build plugins in container for glibc compatibility
# Produces binaries compatible with Debian 11+, Ubuntu 20.04+, and most modern Linux distros
# Uses Podman (preferred on Fedora) or Docker
#
# Usage:
#   ./build_release.sh              # Build all plugins
#   ./build_release.sh 4keq         # Build only 4K EQ
#   ./build_release.sh compressor   # Build only Universal Compressor
#   ./build_release.sh --help       # Show available targets

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="luna-plugins-builder"

# Plugin target mapping (shortname -> cmake target)
declare -A PLUGIN_TARGETS=(
    ["4keq"]="FourKEQ_All"
    ["eq"]="FourKEQ_All"
    ["compressor"]="UniversalCompressor_All"
    ["comp"]="UniversalCompressor_All"
    ["tape"]="TapeMachine_All"
    ["tapemachine"]="TapeMachine_All"
    ["echo"]="TapeEcho_All"
    ["tapeecho"]="TapeEcho_All"
    ["drummer"]="DrummerClone_All"
    ["drums"]="DrummerClone_All"
    ["harmonic"]="HarmonicGeneratorPlugin_All"
    ["convolution"]="ConvolutionReverb_All"
    ["impulse"]="ConvolutionReverb_All"
    ["ir"]="ConvolutionReverb_All"
    ["silkverb"]="SilkVerb_All"
    ["silk"]="SilkVerb_All"
    ["reverb"]="SilkVerb_All"
    ["verb"]="SilkVerb_All"
    ["multiq"]="MultiQ_All"
    ["multi-q"]="MultiQ_All"
    ["meq"]="MultiQ_All"
)

# Show help
show_help() {
    echo "Usage: $0 [plugin]"
    echo ""
    echo "Build Luna Co. Audio plugins in Docker/Podman container."
    echo ""
    echo "Options:"
    echo "  (no args)    Build all plugins"
    echo "  --help       Show this help message"
    echo ""
    echo "Plugin shortcuts:"
    echo "  4keq, eq           4K EQ"
    echo "  compressor, comp   Universal Compressor"
    echo "  tape, tapemachine  TapeMachine"
    echo "  echo, tapeecho     Vintage Tape Echo"
    echo "  drummer, drums     DrummerClone"
    echo "  harmonic           Harmonic Generator"
    echo "  convolution, impulse, ir  Convolution Reverb"
    echo "  silkverb, silk, reverb, verb  SilkVerb"
    echo "  multiq, multi-q, meq   Multi-Q (8-band parametric EQ)"
    echo ""
    echo "Examples:"
    echo "  $0              # Build all plugins"
    echo "  $0 4keq         # Build only 4K EQ"
    echo "  $0 compressor   # Build only Universal Compressor"
}

# Parse arguments
BUILD_TARGET=""
if [ $# -gt 0 ]; then
    case "$1" in
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            # Look up the target
            if [ -n "${PLUGIN_TARGETS[$1]}" ]; then
                BUILD_TARGET="${PLUGIN_TARGETS[$1]}"
                echo "Building single plugin: $1 -> $BUILD_TARGET"
            else
                echo "Error: Unknown plugin '$1'"
                echo "Run '$0 --help' for available options."
                exit 1
            fi
            ;;
    esac
fi

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

# Verify JUCE directory exists
JUCE_DIR="$PROJECT_DIR/../JUCE"
if [ ! -d "$JUCE_DIR" ]; then
    echo "Error: JUCE directory not found at $JUCE_DIR"
    exit 1
fi

# Determine build command
if [ -n "$BUILD_TARGET" ]; then
    BUILD_CMD="cmake --build . --target $BUILD_TARGET -j\$(nproc)"
else
    BUILD_CMD="cmake --build . -j\$(nproc)"
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
        sed -i \"s|/home/marc/projects/JUCE|/JUCE|g\" CMakeLists.txt

        # Create fresh build directory
        mkdir -p build && cd build

        # Configure
        cmake .. -DCMAKE_BUILD_TYPE=Release

        # Build (all or specific target)
        $BUILD_CMD

        # Copy built plugins to output
        echo \"Copying VST3 plugins...\"
        find . -name \"*.vst3\" -type d -exec cp -r {} /output/ \;

        echo \"Copying LV2 plugins...\"
        find . -name \"*.lv2\" -type d -exec cp -r {} /output/ \;

        echo \"\"
        echo \"=== Build complete! ===\"
        echo \"Plugins are in: release/\"
    "

echo ""
echo "Release build complete!"
echo "Output directory: $OUTPUT_DIR"
echo ""

# Install plugins to standard locations
echo "=== Installing plugins to standard locations ==="

# Create target directories if they don't exist
mkdir -p "$HOME/.vst3"
mkdir -p "$HOME/.lv2"

# Install VST3 plugins
echo "Installing VST3 plugins to ~/.vst3/"
for vst3 in "$OUTPUT_DIR"/*.vst3; do
    if [ -d "$vst3" ]; then
        plugin_name=$(basename "$vst3")
        echo "  - $plugin_name"
        rm -rf "$HOME/.vst3/$plugin_name"
        cp -r "$vst3" "$HOME/.vst3/"
    fi
done

# Install LV2 plugins
echo "Installing LV2 plugins to ~/.lv2/"
for lv2 in "$OUTPUT_DIR"/*.lv2; do
    if [ -d "$lv2" ]; then
        plugin_name=$(basename "$lv2")
        echo "  - $plugin_name"
        rm -rf "$HOME/.lv2/$plugin_name"
        cp -r "$lv2" "$HOME/.lv2/"
    fi
done

echo ""
echo "=== Installation complete! ==="
echo ""
echo "These binaries are compatible with:"
echo "  - Debian 12 (Bookworm) and newer"
echo "  - Ubuntu 22.04 LTS and newer"
echo "  - Most Linux distributions from 2022 onwards"
echo ""
echo "Installed plugins:"
for vst3 in "$HOME/.vst3/"*.vst3; do
    [ -d "$vst3" ] && echo "  VST3: $(basename "$vst3")"
done
for lv2 in "$HOME/.lv2/"*.lv2; do
    [ -d "$lv2" ] && echo "  LV2:  $(basename "$lv2")"
done

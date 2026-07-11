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
IMAGE_NAME="dusk-plugins-builder"

# DPF / DPF-Widgets SHAs — keep in sync with .github/workflows/dpf-release.yml.
DPF_SHA="4238e1c7f0351bbe488d79f0899c540543ac7583"
DPFWIDGETS_SHA="730da6397904da66d99667c1cb30fc77fc3d794a"

# Plugin lookup functions (compatible with bash 3.2 on macOS)
get_plugin_target() {
    case "$1" in
        4keq|eq) echo "FourKEQ_All" ;;
        multicomp|multi-comp|compressor|comp) echo "MultiComp_All" ;;
        tape|tapemachine) echo "TapeMachine_All" ;;
        groovemind|groove) echo "GrooveMind_All" ;;
        harmonic) echo "HarmonicGeneratorPlugin_All" ;;
        convolution|impulse|ir) echo "ConvolutionReverb_All" ;;
        multiq|multi-q|meq) echo "MultiQ_All" ;;
        tapeecho|tape-echo|echo) echo "TapeEcho_All" ;;
        chordanalyzer|chord-analyzer|chord|analyze) echo "ChordAnalyzer_All" ;;
        spectrumanalyzer|spectrum-analyzer|spectrum|span|fft) echo "SpectrumAnalyzer_All" ;;
        duskverb|dusk-verb|reverb) echo "DuskVerb_All" ;;
        duskamp|dusk-amp|amp) echo "DuskAmp_All" ;;
        multisynth|multi-synth|synth) echo "MultiSynth_All" ;;
        *) echo "" ;;
    esac
}

get_plugin_name() {
    case "$1" in
        4keq|eq) echo "4K EQ" ;;
        multicomp|multi-comp|compressor|comp) echo "Multi-Comp" ;;
        tape|tapemachine) echo "TapeMachine" ;;
        groovemind|groove) echo "GrooveMind" ;;
        harmonic) echo "Harmonic Generator" ;;
        convolution|impulse|ir) echo "Convolution Reverb" ;;
        multiq|multi-q|meq) echo "Multi-Q" ;;
        tapeecho|tape-echo|echo) echo "Tape Echo" ;;
        chordanalyzer|chord-analyzer|chord|analyze) echo "Chord Analyzer" ;;
        spectrumanalyzer|spectrum-analyzer|spectrum|span|fft) echo "Spectrum Analyzer" ;;
        duskverb|dusk-verb|reverb) echo "DuskVerb" ;;
        duskamp|dusk-amp|amp) echo "DuskAmp" ;;
        multisynth|multi-synth|synth) echo "Multi-Synth" ;;
        *) echo "" ;;
    esac
}

# Is this shortcut the DPF-based Sunset Circuits plugin? (built out-of-graph)
is_sunset() {
    case "$1" in
        sunset|sunset-circuits|sc) return 0 ;;
        *) return 1 ;;
    esac
}

# Build Sunset Circuits (DPF) in the container. Unlike the JUCE plugins this does
# NOT use the top-level JUCE build graph: it clones DISTRHO/DPF + DPF-Widgets at
# the pinned SHAs inside the container and builds plugins/sunset-circuits/dpf-plugin
# standalone (mirrors .github/workflows/dpf-release.yml). Produces VST3/CLAP/LV2.
build_sunset() {
    echo "=== Building Sunset Circuits (DPF) ==="
    echo "Using: $CONTAINER_CMD  (DPF ${DPF_SHA:0:12}, DPF-Widgets ${DPFWIDGETS_SHA:0:12})"

    if ! $CONTAINER_CMD image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building container image..."
        $CONTAINER_CMD build $SECURITY_OPTS -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile.build" "$SCRIPT_DIR"
    fi

    mkdir -p "$OUTPUT_DIR"

    $CONTAINER_CMD run --rm $SECURITY_OPTS \
        -v "$PROJECT_DIR:/src:ro" \
        -v "$OUTPUT_DIR:/output:Z" \
        "$IMAGE_NAME" \
        bash -c '
            set -e
            export DEBIAN_FRONTEND=noninteractive
            DPF_SHA="'"$DPF_SHA"'"
            DPFWIDGETS_SHA="'"$DPFWIDGETS_SHA"'"

            # DPF opengl UI deps not baked into the JUCE image.
            apt-get update
            apt-get install -y --no-install-recommends \
                ninja-build libxext-dev libglu1-mesa-dev mesa-common-dev libdbus-1-dev

            # Shallow-fetch each dependency at its exact pinned SHA.
            fetch_sha() {
                local url="$1" sha="$2" dir="$3"
                mkdir -p "$dir" && cd "$dir"
                git init -q
                git remote add origin "$url"
                git fetch -q --depth 1 origin "$sha"
                git checkout -q FETCH_HEAD
                git submodule update -q --init --recursive --depth 1 || true
                cd /tmp
            }
            cd /tmp
            fetch_sha https://github.com/DISTRHO/DPF.git         "$DPF_SHA"        /tmp/dpf
            fetch_sha https://github.com/DISTRHO/DPF-Widgets.git  "$DPFWIDGETS_SHA" /tmp/dpf-widgets

            cmake -S /src/plugins/sunset-circuits/dpf-plugin -B /tmp/scbuild -G Ninja \
                -DCMAKE_BUILD_TYPE=Release \
                -DDUSK_DPF_INSTALL_LOCAL=OFF \
                -DDPF_PATH=/tmp/dpf \
                -DDPFWIDGETS_PATH=/tmp/dpf-widgets
            cmake --build /tmp/scbuild \
                --target sunset_circuits-vst3 sunset_circuits-clap sunset_circuits-lv2 \
                -j"$(nproc)"

            echo "Copying artefacts to output..."
            cp -r /tmp/scbuild/bin/sunset_circuits.vst3 /output/
            cp -r /tmp/scbuild/bin/sunset_circuits.lv2  /output/
            cp    /tmp/scbuild/bin/sunset_circuits.clap /output/
            echo "=== Sunset Circuits build complete ==="
        '

    echo ""
    echo "=== Installing Sunset Circuits to standard locations ==="
    mkdir -p "$HOME/.vst3" "$HOME/.lv2" "$HOME/.clap"
    if [ -d "$OUTPUT_DIR/sunset_circuits.vst3" ]; then
        rm -rf "$HOME/.vst3/sunset_circuits.vst3"
        cp -r "$OUTPUT_DIR/sunset_circuits.vst3" "$HOME/.vst3/"
        echo "  VST3: sunset_circuits.vst3 -> ~/.vst3/"
    fi
    if [ -d "$OUTPUT_DIR/sunset_circuits.lv2" ]; then
        rm -rf "$HOME/.lv2/sunset_circuits.lv2"
        cp -r "$OUTPUT_DIR/sunset_circuits.lv2" "$HOME/.lv2/"
        echo "  LV2:  sunset_circuits.lv2 -> ~/.lv2/"
    fi
    if [ -f "$OUTPUT_DIR/sunset_circuits.clap" ]; then
        cp "$OUTPUT_DIR/sunset_circuits.clap" "$HOME/.clap/"
        echo "  CLAP: sunset_circuits.clap -> ~/.clap/"
    fi
    echo ""
    echo "=== Done. ==="
}

# Show help
show_help() {
    echo "Usage: $0 [plugin]"
    echo ""
    echo "Build Dusk Audio plugins in Docker/Podman container."
    echo ""
    echo "Options:"
    echo "  (no args)    Build all plugins"
    echo "  --help       Show this help message"
    echo ""
    echo "Plugin shortcuts:"
    echo "  4keq, eq           4K EQ"
    echo "  multicomp, multi-comp, compressor, comp   Multi-Comp"
    echo "  tape, tapemachine  TapeMachine"
    echo "  groovemind, groove GrooveMind"
    echo "  harmonic           Harmonic Generator"
    echo "  convolution, impulse, ir  Convolution Reverb"
    echo "  multiq, multi-q, meq   Multi-Q (Universal EQ)"
    echo "  tapeecho, tape-echo, echo   Tape Echo"
    echo "  chordanalyzer, chord-analyzer, chord, analyze   Chord Analyzer"
    echo "  spectrumanalyzer, spectrum-analyzer, spectrum, span, fft   Spectrum Analyzer"
    echo "  duskverb, dusk-verb, reverb   DuskVerb"
    echo "  duskamp, dusk-amp, amp   DuskAmp"
    echo "  multisynth, multi-synth, synth   Multi-Synth (legacy JUCE, unreleased)"
    echo "  sunset, sunset-circuits, sc   Sunset Circuits (DPF: VST3/CLAP/LV2)"
    echo ""
    echo "Examples:"
    echo "  $0              # Build all plugins"
    echo "  $0 4keq         # Build only 4K EQ"
    echo "  $0 multicomp    # Build only Multi-Comp"
}

# Parse arguments
BUILD_TARGET=""
PLUGIN_SHORTNAME=""
BUILD_SUNSET=0
if [ $# -gt 0 ]; then
    case "$1" in
        --help|-h)
            show_help
            exit 0
            ;;
        sunset|sunset-circuits|sc)
            # DPF plugin — handled by build_sunset() after the container backend
            # is selected below (out-of-graph, no JUCE required).
            BUILD_SUNSET=1
            echo "Building Sunset Circuits (DPF)"
            ;;
        *)
            # Look up the target
            BUILD_TARGET=$(get_plugin_target "$1")
            if [ -n "$BUILD_TARGET" ]; then
                PLUGIN_SHORTNAME="$1"
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

echo "=== Dusk Audio Plugin Release Builder ==="
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

# Sunset Circuits is a DPF plugin built out-of-graph (no JUCE) — dispatch here,
# before the JUCE_DIR check, so it doesn't require a JUCE checkout.
if [ "$BUILD_SUNSET" -eq 1 ]; then
    build_sunset
    exit 0
fi

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

        # Create fresh build directory
        mkdir -p build && cd build

        # Configure with JUCE path pointing to container mount
        CMAKE_OPTS=\"-DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=/JUCE\"
        if [ \"$BUILD_TARGET\" = \"GrooveMind_All\" ]; then
            CMAKE_OPTS=\"\$CMAKE_OPTS -DBUILD_GROOVEMIND=ON\"
        fi
        cmake .. \$CMAKE_OPTS

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

# Run pluginval for single-plugin builds
if [ -n "$PLUGIN_SHORTNAME" ]; then
    PLUGIN_DISPLAY_NAME=$(get_plugin_name "$PLUGIN_SHORTNAME")
    TEST_SCRIPT="$PROJECT_DIR/tests/run_plugin_tests.sh"

    if [ -f "$TEST_SCRIPT" ]; then
        echo ""
        echo "=== Running pluginval tests for $PLUGIN_DISPLAY_NAME ==="
        "$TEST_SCRIPT" --plugin "$PLUGIN_DISPLAY_NAME" --skip-audio
    else
        echo ""
        echo "Note: Test script not found at $TEST_SCRIPT, skipping validation"
    fi

    # Run Multi-Comp unit tests (includes comb filtering detection)
    if [ "$PLUGIN_SHORTNAME" = "multicomp" ] || [ "$PLUGIN_SHORTNAME" = "multi-comp" ] || [ "$PLUGIN_SHORTNAME" = "compressor" ] || [ "$PLUGIN_SHORTNAME" = "comp" ]; then
        MULTICOMP_TEST_SCRIPT="$PROJECT_DIR/tests/run_multicomp_tests.sh"
        if [ -f "$MULTICOMP_TEST_SCRIPT" ]; then
            echo ""
            echo "=== Running Multi-Comp unit tests ==="
            echo "  (Includes comb filtering detection with oversampling + mix knob)"
            if ! "$MULTICOMP_TEST_SCRIPT"; then
                echo ""
                echo "WARNING: Multi-Comp unit tests had failures!"
                echo "Check output above for details. Critical tests include:"
                echo "  - testMixKnobPhaseAlignment: Detects comb filtering"
                echo "  - testDSPStability: Ensures no NaN/Inf values"
                echo "  - testGainReduction: Validates compression behavior"
            fi
        fi
    fi
fi

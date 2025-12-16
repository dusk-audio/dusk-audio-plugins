#!/bin/bash
#==============================================================================
# Luna Co. Audio Plugin Test Suite
# Comprehensive automated testing for VST3/LV2 plugins
#==============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
VST3_DIR="$HOME/.vst3"
LV2_DIR="$HOME/.lv2"
TEST_OUTPUT_DIR="$SCRIPT_DIR/output"
SAMPLE_RATES=(44100 48000 96000)
BUFFER_SIZES=(64 128 256 512 1024)

# Test results tracking
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Plugins to test
PLUGINS=(
    "4K EQ"
    "Convolution Reverb"
    "DrummerClone"
    "Harmonic Generator"
    "Multi-Q"
    "SilkVerb"
    "Universal Compressor"
    "TapeMachine"
    "Vintage Tape Echo"
)

#------------------------------------------------------------------------------
# Utility Functions
#------------------------------------------------------------------------------

print_header() {
    echo ""
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}============================================================${NC}"
}

print_section() {
    echo ""
    echo -e "${BLUE}--- $1 ---${NC}"
}

print_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++)) || true
}

print_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((TESTS_FAILED++)) || true
}

print_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $1"
    ((TESTS_SKIPPED++)) || true
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

#------------------------------------------------------------------------------
# Plugin Existence Tests
#------------------------------------------------------------------------------

test_plugin_exists() {
    local plugin_name="$1"
    local vst3_path="$VST3_DIR/${plugin_name}.vst3"
    local lv2_path="$LV2_DIR/${plugin_name}.lv2"

    print_section "Checking plugin files: $plugin_name"

    if [ -d "$vst3_path" ]; then
        print_pass "VST3 exists: $vst3_path"
        # Check for required VST3 structure
        if [ -f "$vst3_path/Contents/x86_64-linux/${plugin_name}.so" ]; then
            print_pass "VST3 binary exists"
        else
            print_fail "VST3 binary missing"
        fi
    else
        print_fail "VST3 not found: $vst3_path"
    fi

    if [ -d "$lv2_path" ]; then
        print_pass "LV2 exists: $lv2_path"
        # Check for required LV2 files
        if [ -f "$lv2_path/manifest.ttl" ]; then
            print_pass "LV2 manifest.ttl exists"
        else
            print_fail "LV2 manifest.ttl missing"
        fi
    else
        print_fail "LV2 not found: $lv2_path"
    fi
}

#------------------------------------------------------------------------------
# Pluginval Tests (if available)
#------------------------------------------------------------------------------

run_pluginval_tests() {
    local plugin_name="$1"
    local vst3_path="$VST3_DIR/${plugin_name}.vst3"

    print_section "Pluginval validation: $plugin_name"

    if ! command -v pluginval &> /dev/null; then
        print_skip "pluginval not installed (install from https://github.com/Tracktion/pluginval)"
        return
    fi

    if [ ! -d "$vst3_path" ]; then
        print_skip "VST3 not found, skipping pluginval"
        return
    fi

    # Run pluginval with increasing strictness levels
    # Level 1: Basic sanity checks
    # Level 5: Recommended minimum for compatibility
    # Level 7: More thorough testing including edge cases
    # Level 10: Maximum strictness with fuzzing (can take longer)
    for level in 1 5 7 10; do
        print_info "Running pluginval at strictness level $level..."

        local output_file="$TEST_OUTPUT_DIR/pluginval_${plugin_name// /_}_level${level}.log"

        # Higher levels need more time, especially level 10 with fuzzing
        # Shell timeout is outer limit; pluginval timeout is per-test limit (set slightly lower)
        local timeout_secs=120
        local pluginval_timeout_ms=90000
        if [ "$level" -ge 7 ]; then
            timeout_secs=180
            pluginval_timeout_ms=150000
        fi
        if [ "$level" -eq 10 ]; then
            timeout_secs=300
            pluginval_timeout_ms=270000
        fi

        if timeout "$timeout_secs" pluginval --validate "$vst3_path" --strictness-level "$level" \
            --timeout-ms "$pluginval_timeout_ms" --verbose > "$output_file" 2>&1; then
            print_pass "Pluginval level $level passed"
        else
            print_fail "Pluginval level $level failed (see $output_file)"
        fi
    done
}

#------------------------------------------------------------------------------
# Binary Analysis Tests
#------------------------------------------------------------------------------

test_binary_symbols() {
    local plugin_name="$1"
    local vst3_path="$VST3_DIR/${plugin_name}.vst3/Contents/x86_64-linux/${plugin_name}.so"

    print_section "Binary analysis: $plugin_name"

    if [ ! -f "$vst3_path" ]; then
        print_skip "Binary not found"
        return
    fi

    # Check for VST3 entry point
    if nm -D "$vst3_path" 2>/dev/null | grep -q "GetPluginFactory"; then
        print_pass "VST3 GetPluginFactory symbol found"
    else
        print_fail "VST3 GetPluginFactory symbol missing"
    fi

    # Check for undefined symbols that might cause issues
    local undefined_count=$(nm -u "$vst3_path" 2>/dev/null | wc -l)
    print_info "Undefined symbols: $undefined_count (normal for dynamic linking)"

    # Check library dependencies
    print_info "Library dependencies:"
    ldd "$vst3_path" 2>/dev/null | head -10
}

#------------------------------------------------------------------------------
# Audio Processing Tests (using Python script)
#------------------------------------------------------------------------------

run_audio_tests() {
    local plugin_name="$1"

    print_section "Audio processing tests: $plugin_name"

    if [ ! -f "$SCRIPT_DIR/audio_analyzer.py" ]; then
        print_skip "audio_analyzer.py not found"
        return
    fi

    if ! command -v python3 &> /dev/null; then
        print_skip "Python3 not available"
        return
    fi

    # Run the Python audio analyzer
    python3 "$SCRIPT_DIR/audio_analyzer.py" --plugin "$plugin_name" --output-dir "$TEST_OUTPUT_DIR"
}

#------------------------------------------------------------------------------
# Main Test Runner
#------------------------------------------------------------------------------

main() {
    print_header "Luna Co. Audio Plugin Test Suite"

    echo "Project directory: $PROJECT_DIR"
    echo "VST3 directory: $VST3_DIR"
    echo "LV2 directory: $LV2_DIR"
    echo "Test output: $TEST_OUTPUT_DIR"

    # Create output directory
    mkdir -p "$TEST_OUTPUT_DIR"

    # Parse arguments
    local specific_plugin=""
    local skip_audio=false
    local skip_pluginval=false

    while [[ $# -gt 0 ]]; do
        case $1 in
            --plugin)
                specific_plugin="$2"
                shift 2
                ;;
            --skip-audio)
                skip_audio=true
                shift
                ;;
            --skip-pluginval)
                skip_pluginval=true
                shift
                ;;
            --help)
                echo "Usage: $0 [options]"
                echo "Options:"
                echo "  --plugin NAME     Test only the specified plugin"
                echo "  --skip-audio      Skip audio analysis tests"
                echo "  --skip-pluginval  Skip pluginval tests"
                echo "  --help            Show this help"
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    # Determine which plugins to test
    local plugins_to_test=()
    if [ -n "$specific_plugin" ]; then
        plugins_to_test=("$specific_plugin")
    else
        plugins_to_test=("${PLUGINS[@]}")
    fi

    # Run tests for each plugin
    for plugin in "${plugins_to_test[@]}"; do
        print_header "Testing: $plugin"

        test_plugin_exists "$plugin"
        test_binary_symbols "$plugin"

        if [ "$skip_pluginval" != true ]; then
            run_pluginval_tests "$plugin"
        fi

        if [ "$skip_audio" != true ]; then
            run_audio_tests "$plugin"
        fi
    done

    # Print summary
    print_header "Test Summary"
    echo -e "${GREEN}Passed:${NC}  $TESTS_PASSED"
    echo -e "${RED}Failed:${NC}  $TESTS_FAILED"
    echo -e "${YELLOW}Skipped:${NC} $TESTS_SKIPPED"
    echo ""

    if [ $TESTS_FAILED -gt 0 ]; then
        echo -e "${RED}Some tests failed!${NC}"
        exit 1
    else
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    fi
}

main "$@"

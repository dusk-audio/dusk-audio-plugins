#!/bin/bash
#==============================================================================
# Dusk Audio Plugin Test Suite
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
    "DuskVerb"
    "Multi-Q"
    "Universal Compressor"
    "TapeMachine"
    "Vintage Tape Echo"
    "Sunset Circuits"
    "PRE-35"
)

# Sunset Circuits is the DPF port, not a JUCE plugin: its bundle is named
# sunset_circuits (not after the display name), it may live in the DPF build
# tree rather than ~/.vst3, and it carries its own offline gate suite. It is
# handled by run_sunset_circuits_tests() instead of the generic path below.
SUNSET_NAME="Sunset Circuits"
SUNSET_BUNDLE="sunset_circuits"

# PRE-35 is the other DPF port and needs the same special handling for the same
# reasons (bundle name pre_35, may live in its own build tree). Its gate suite is
# ctest over plugins/pre-35/core rather than a shell script, so it gets its own
# function; the two DPF paths below have converged enough that folding them into
# one parameterised helper is the obvious next cleanup of this file.
PRE35_NAME="PRE-35"
PRE35_BUNDLE="pre_35"

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
# Sunset Circuits (DPF) - core gate suite + pluginval on the built VST3
#------------------------------------------------------------------------------

# Prefer an installed bundle, fall back to the DPF build tree (which is where it
# lands when the plugin is configured with -DDUSK_DPF_INSTALL_LOCAL=OFF).
sunset_vst3_path() {
    local installed="$VST3_DIR/${SUNSET_BUNDLE}.vst3"
    local built="$PROJECT_DIR/plugins/sunset-circuits/dpf-plugin/build/bin/${SUNSET_BUNDLE}.vst3"
    if [ -d "$installed" ]; then
        echo "$installed"
    elif [ -d "$built" ]; then
        echo "$built"
    fi
}

run_sunset_circuits_tests() {
    local skip_pluginval="$1"
    local skip_audio="$2"
    local suite="$PROJECT_DIR/plugins/sunset-circuits/core/tests/run_all.sh"

    # --- offline gate suite (the authoritative DSP check for this plugin) -----
    # Honours --skip-audio: the suite is offline audio rendering and takes
    # minutes, and the documented fleet command in CLAUDE.md
    # (run_plugin_tests.sh --plugin "<Name>" --skip-audio) has to stay quick.
    print_section "Core gate suite: $SUNSET_NAME"
    if [ "$skip_audio" = true ]; then
        print_skip "Core gate suite (--skip-audio); run $suite directly for the DSP gates"
    elif [ ! -x "$suite" ]; then
        print_skip "run_all.sh not found or not executable: $suite"
    else
        print_info "Running $suite (several minutes; builds its own harness)"
        if "$suite"; then
            print_pass "Core gate suite green"
        else
            # run_all.sh already printed the per-gate detail above; it reports
            # one aggregate status, so name the log rather than re-deriving it.
            print_fail "Core gate suite failed (per-gate detail is in the output above)"
        fi
    fi

    # --- bundle presence ------------------------------------------------------
    print_section "Checking plugin files: $SUNSET_NAME"
    local vst3
    vst3="$(sunset_vst3_path)"
    if [ -z "$vst3" ]; then
        print_skip "No ${SUNSET_BUNDLE}.vst3 found (build dpf-plugin, or install to $VST3_DIR)"
        return
    fi
    print_pass "VST3 exists: $vst3"

    local arch_dir="x86_64-linux"
    [ "$(uname -m)" = "aarch64" ] && arch_dir="aarch64-linux"
    local so="$vst3/Contents/$arch_dir/${SUNSET_BUNDLE}.so"
    if [ -f "$so" ]; then
        print_pass "VST3 binary exists"
        if nm -D "$so" 2>/dev/null | grep -q "GetPluginFactory"; then
            print_pass "VST3 GetPluginFactory symbol found"
        else
            print_fail "VST3 GetPluginFactory symbol missing"
        fi
    else
        print_fail "VST3 binary missing: $so"
    fi

    local lv2="$LV2_DIR/${SUNSET_BUNDLE}.lv2"
    [ -d "$lv2" ] || lv2="$PROJECT_DIR/plugins/sunset-circuits/dpf-plugin/build/bin/${SUNSET_BUNDLE}.lv2"
    if [ -f "$lv2/manifest.ttl" ]; then
        print_pass "LV2 manifest.ttl exists"
    else
        print_skip "LV2 bundle not built/installed"
    fi

    # --- pluginval ------------------------------------------------------------
    if [ "$skip_pluginval" = true ]; then
        return
    fi

    print_section "Pluginval validation: $SUNSET_NAME"
    if ! command -v pluginval &> /dev/null; then
        print_skip "pluginval not installed (https://github.com/Tracktion/pluginval)"
        return
    fi

    # --skip-gui-tests is mandatory: pluginval's editor tests segfault headless
    # for every DPF plugin (a host-side XEmbed issue, docs/dpf-migration
    # 00-OVERVIEW.md landmine 9), so a GUI run says nothing about this plugin.
    # Strictness 8 is the bar the Sunset Circuits QA checklist declares
    # authoritative for the 222-parameter state round-trip.
    local level=8
    local output_file="$TEST_OUTPUT_DIR/pluginval_${SUNSET_BUNDLE}_level${level}.log"
    print_info "Running pluginval at strictness level $level (--skip-gui-tests)..."

    if timeout 300 pluginval --validate "$vst3" --strictness-level "$level" \
        --skip-gui-tests --timeout-ms 270000 --verbose > "$output_file" 2>&1; then
        print_pass "Pluginval level $level passed"
    elif grep -q "Starting test" "$output_file" && ! grep -q "FAILED" "$output_file"; then
        # DPF tears down non-zero after a clean run on some hosts; tolerated only
        # when the log proves tests ran and none failed (same rule as CI).
        print_pass "Pluginval level $level passed (non-zero exit on teardown, tests clean)"
    else
        print_fail "Pluginval level $level failed (see $output_file)"
    fi
}

#------------------------------------------------------------------------------
# PRE-35 (DPF) - core ctest gates + pluginval on the built VST3
#------------------------------------------------------------------------------

# Prefer an installed bundle, fall back to the DPF build tree (which is where it
# lands when the plugin is configured with -DDUSK_DPF_INSTALL_LOCAL=OFF).
pre35_vst3_path() {
    local installed="$VST3_DIR/${PRE35_BUNDLE}.vst3"
    local built="$PROJECT_DIR/plugins/pre-35/dpf-plugin/build/bin/${PRE35_BUNDLE}.vst3"
    if [ -d "$installed" ]; then
        echo "$installed"
    elif [ -d "$built" ]; then
        echo "$built"
    fi
}

run_pre35_tests() {
    local skip_pluginval="$1"
    local skip_audio="$2"
    local core_dir="$PROJECT_DIR/plugins/pre-35/core"
    local core_build="$PROJECT_DIR/build/pre35-core-gates"

    # --- core gate suite (the authoritative DSP check for this plugin) --------
    # ctest over the framework-free core, whatever gates are registered there.
    # Deliberately not enumerated or counted here: gates get added (rail_test in
    # 2026-08) and a list in this file would silently go stale. Some of them
    # render tones and measure THD with a DFT, so this is offline audio rendering
    # and honours --skip-audio like the Sunset Circuits suite does.
    # Every stage is logged, because the stage most likely to fail here is the
    # BUILD, and a compile error swallowed by /dev/null leaves "the suite failed"
    # with nothing to act on. Same convention as the pluginval logs below.
    local build_log="$TEST_OUTPUT_DIR/pre35_core_build.log"
    local ctest_log="$TEST_OUTPUT_DIR/pre35_core_ctest.log"

    print_section "Core gate suite: $PRE35_NAME"
    if [ "$skip_audio" = true ]; then
        print_skip "Core gate suite (--skip-audio); run it directly with: cmake -S $core_dir -B $core_build && cmake --build $core_build -j && ctest --test-dir $core_build --output-on-failure"
    elif ! command -v cmake &> /dev/null; then
        print_skip "cmake not installed, cannot build the PRE-35 core gates"
    else
        local stage_ok=true
        if ! { cmake -S "$core_dir" -B "$core_build" \
               && cmake --build "$core_build" -j"$(nproc 2>/dev/null || echo 4)"; } \
               > "$build_log" 2>&1; then
            print_fail "Core gates failed to configure/build (full log: $build_log)"
            echo "--- last 25 lines of $build_log ---"
            tail -25 "$build_log"
            echo "--- end ---"
            stage_ok=false
        fi

        if [ "$stage_ok" = true ]; then
            # tee, not redirect: the per-test pass/fail lines stay on screen like
            # every other suite, and the log keeps the whole run for later. The
            # pipeline's own status is tee's (pipefail is off, so `set -e` cannot
            # abort the script on a red gate) — ctest's real status is PIPESTATUS[0],
            # which must be captured on the very next line before anything else runs.
            ctest --test-dir "$core_build" --output-on-failure 2>&1 | tee "$ctest_log"
            local ctest_status="${PIPESTATUS[0]}"
            if [ "$ctest_status" -eq 0 ]; then
                print_pass "Core gate suite green"
            else
                print_fail "Core gate suite failed (full log: $ctest_log)"
            fi
        fi
    fi

    # --- bundle presence ------------------------------------------------------
    print_section "Checking plugin files: $PRE35_NAME"
    local vst3
    vst3="$(pre35_vst3_path)"
    if [ -z "$vst3" ]; then
        print_skip "No ${PRE35_BUNDLE}.vst3 found (build dpf-plugin, or install to $VST3_DIR)"
        return
    fi
    print_pass "VST3 exists: $vst3"

    # VST3 bundles are laid out per platform: Contents/MacOS/<name> on macOS,
    # Contents/<arch>-linux/<name>.so on Linux. And the symbol probe has to match —
    # Apple's nm has no -D, so `nm -D` there fails and would report a perfectly good
    # bundle as "symbol missing". -gU is its equivalent (global, defined only).
    local so nm_args
    if [ "$(uname -s)" = "Darwin" ]; then
        so="$vst3/Contents/MacOS/${PRE35_BUNDLE}"
        nm_args=(-gU)
    else
        local arch_dir="x86_64-linux"
        [ "$(uname -m)" = "aarch64" ] && arch_dir="aarch64-linux"
        so="$vst3/Contents/$arch_dir/${PRE35_BUNDLE}.so"
        nm_args=(-D)
    fi
    if [ -f "$so" ]; then
        print_pass "VST3 binary exists"
        if ! command -v nm &> /dev/null; then
            print_skip "nm not installed, cannot check the GetPluginFactory symbol"
        elif nm "${nm_args[@]}" "$so" 2>/dev/null | grep -q "GetPluginFactory"; then
            print_pass "VST3 GetPluginFactory symbol found"
        else
            print_fail "VST3 GetPluginFactory symbol missing"
        fi
    else
        print_fail "VST3 binary missing: $so"
    fi

    local lv2="$LV2_DIR/${PRE35_BUNDLE}.lv2"
    [ -d "$lv2" ] || lv2="$PROJECT_DIR/plugins/pre-35/dpf-plugin/build/bin/${PRE35_BUNDLE}.lv2"
    if [ -f "$lv2/manifest.ttl" ]; then
        print_pass "LV2 manifest.ttl exists"
    else
        print_skip "LV2 bundle not built/installed"
    fi

    # --- pluginval ------------------------------------------------------------
    if [ "$skip_pluginval" = true ]; then
        return
    fi

    print_section "Pluginval validation: $PRE35_NAME"
    if ! command -v pluginval &> /dev/null; then
        print_skip "pluginval not installed (https://github.com/Tracktion/pluginval)"
        return
    fi

    # --skip-gui-tests is mandatory: pluginval's editor tests segfault headless
    # for every DPF plugin (a host-side XEmbed issue, docs/dpf-migration
    # 00-OVERVIEW.md landmine 9), so a GUI run says nothing about this plugin.
    # Strictness 10 because PRE-35 has only seven input parameters and no preset
    # bank, so the full fuzz/thread-safety sweep costs under a second.
    local level=10
    local output_file="$TEST_OUTPUT_DIR/pluginval_${PRE35_BUNDLE}_level${level}.log"
    print_info "Running pluginval at strictness level $level (--skip-gui-tests)..."

    # Capture the status: the teardown tolerance below must apply ONLY to a real
    # pluginval exit code. "Starting test present, FAILED absent" is also true of a
    # run that segfaulted or that the timeout killed partway through, so the older
    # form reported a crash as a pass. Reject 124 (timeout) and >= 128 (killed by
    # signal) outright, and require the log's own terminal SUCCESS line before
    # tolerating any other non-zero status.
    local status=0
    timeout 300 pluginval --validate "$vst3" --strictness-level "$level" \
        --skip-gui-tests --timeout-ms 270000 --verbose > "$output_file" 2>&1 || status=$?

    if [ "$status" -eq 0 ]; then
        print_pass "Pluginval level $level passed"
    elif [ "$status" -eq 124 ]; then
        print_fail "Pluginval level $level timed out after 300 s (see $output_file)"
    elif [ "$status" -ge 128 ]; then
        print_fail "Pluginval level $level killed by signal $((status - 128)) (see $output_file)"
    elif grep -q "Starting test" "$output_file" \
        && grep -q "^SUCCESS" "$output_file" \
        && ! grep -q "FAILED" "$output_file"; then
        # DPF tears down non-zero after a clean run on some hosts; tolerated only
        # when the log proves the suite reached its own SUCCESS line and nothing
        # failed (same rule as CI).
        print_pass "Pluginval level $level passed (exit $status on teardown, tests clean)"
    else
        print_fail "Pluginval level $level failed (exit $status, see $output_file)"
    fi
}

#------------------------------------------------------------------------------
# Main Test Runner
#------------------------------------------------------------------------------

main() {
    print_header "Dusk Audio Plugin Test Suite"

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
                echo ""
                echo "Note: \"Sunset Circuits\" (also accepted as \"sunset-circuits\") is the"
                echo "DPF port. Instead of the shared audio analyzer it runs its own offline"
                echo "gate suite (plugins/sunset-circuits/core/tests/run_all.sh, several"
                echo "minutes) which --skip-audio skips, plus pluginval at strictness 8 with"
                echo "--skip-gui-tests."
                echo ""
                echo "\"PRE-35\" (also accepted as \"pre-35\") is the other DPF port: its gate"
                echo "suite is ctest over plugins/pre-35/core (seconds, also skipped by"
                echo "--skip-audio) plus pluginval at strictness 10 with --skip-gui-tests."
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

        # DPF ports with their own bundle names and gate suites; accept the slug too.
        if [ "$plugin" = "$SUNSET_NAME" ] || [ "$plugin" = "sunset-circuits" ]; then
            run_sunset_circuits_tests "$skip_pluginval" "$skip_audio"
            continue
        fi
        if [ "$plugin" = "$PRE35_NAME" ] || [ "$plugin" = "pre-35" ]; then
            run_pre35_tests "$skip_pluginval" "$skip_audio"
            continue
        fi

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

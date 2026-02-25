#!/bin/bash
#==============================================================================
# Dusk Audio Regression Check
# Checks staged (or all) files for known bug patterns from closed issues
#==============================================================================
#
# Usage:
#   ./tests/regression_check.sh          # Check staged files only
#   ./tests/regression_check.sh --all    # Check entire codebase
#
# Exit codes:
#   0 = no regressions found
#   2 = regression(s) found (stderr contains details)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Colors (only when stderr is a terminal)
if [ -t 2 ]; then
    RED='\033[0;31m'
    YELLOW='\033[1;33m'
    GREEN='\033[0;32m'
    CYAN='\033[0;36m'
    NC='\033[0m'
else
    RED='' YELLOW='' GREEN='' CYAN='' NC=''
fi

MODE="staged"
if [[ "${1:-}" == "--all" ]]; then
    MODE="all"
fi

FAILURES=0
WARNINGS=0

#==============================================================================
# Helpers
#==============================================================================

fail() {
    local check_id="$1"
    local description="$2"
    local location="$3"
    local fix="$4"
    FAILURES=$((FAILURES + 1))
    echo -e "${RED}[FAIL]${NC} ${check_id}" >&2
    echo -e "  $description" >&2
    echo -e "  ${CYAN}File:${NC} $location" >&2
    echo -e "  ${CYAN}Fix:${NC}  $fix" >&2
    echo "" >&2
}

warn() {
    local check_id="$1"
    local description="$2"
    local location="$3"
    local fix="$4"
    WARNINGS=$((WARNINGS + 1))
    echo -e "${YELLOW}[WARN]${NC} ${check_id}" >&2
    echo -e "  $description" >&2
    echo -e "  ${CYAN}File:${NC} $location" >&2
    echo -e "  ${CYAN}Fix:${NC}  $fix" >&2
    echo "" >&2
}

# Get files to check, filtered by glob patterns
# Usage: get_files "*.cpp" "*.h"
get_files() {
    local all_files=""

    if [[ "$MODE" == "staged" ]]; then
        all_files=$(cd "$PROJECT_DIR" && git diff --cached --name-only 2>/dev/null || true)
    else
        all_files=$(cd "$PROJECT_DIR" && find plugins/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "CMakeLists.txt" -o -name "*.sh" -o -name "*.md" \) 2>/dev/null || true)
    fi

    # Exclude third-party code (NAM modules, JUCE, etc.)
    all_files=$(echo "$all_files" | grep -v '/Modules/' | grep -v '/JUCE/' || true)

    [[ -z "$all_files" ]] && return

    local filtered=""
    for pattern in "$@"; do
        local matches
        matches=$(echo "$all_files" | grep -E "$(echo "$pattern" | sed 's/\./\\./g; s/\*/.*/g')" 2>/dev/null || true)
        if [[ -n "$matches" ]]; then
            filtered="${filtered}${filtered:+$'\n'}${matches}"
        fi
    done

    # Convert to absolute paths
    echo "$filtered" | sort -u | while read -r f; do
        [[ -n "$f" ]] && echo "$PROJECT_DIR/$f"
    done
}

#==============================================================================
# Header
#==============================================================================

echo -e "${CYAN}============================================================${NC}" >&2
echo -e "${CYAN}  Dusk Audio Regression Check${NC}" >&2
echo -e "${CYAN}============================================================${NC}" >&2
if [[ "$MODE" == "staged" ]]; then
    STAGED_COUNT=$(cd "$PROJECT_DIR" && git diff --cached --name-only 2>/dev/null | wc -l | tr -d ' ')
    if [[ "$STAGED_COUNT" -eq 0 ]]; then
        echo -e "No staged files to check." >&2
        exit 0
    fi
    echo -e "Mode: staged files only ($STAGED_COUNT files)" >&2
else
    echo -e "Mode: full codebase scan" >&2
fi
echo "" >&2

#==============================================================================
# CHECK 1: DEPRECATED_CONFIGURE_KNOB (Issues #4, #5, #12, #20)
# The configureKnob function caused knob jumping during fine control.
# DuskSlider handles fine drag internally — configureKnob must not be used.
#==============================================================================

check_deprecated_configure_knob() {
    local files
    files=$(get_files "*.cpp" "*.h")
    [[ -z "$files" ]] && return

    while IFS= read -r f; do
        [[ -z "$f" || ! -f "$f" ]] && continue
        # Match configureKnob( as a call, exclude comment lines
        local matches
        matches=$(grep -n 'configureKnob\s*(' "$f" 2>/dev/null | grep -v '^\s*//' | grep -v 'no need for configureKnob' || true)
        if [[ -n "$matches" ]]; then
            local rel
            rel=$(echo "$f" | sed "s|$PROJECT_DIR/||")
            local line
            line=$(echo "$matches" | head -1 | cut -d: -f1)
            fail "DEPRECATED_CONFIGURE_KNOB (Issues #4/#5/#12/#20)" \
                 "Deprecated configureKnob() causes knob jumping during fine control" \
                 "$rel:$line" \
                 "Use DuskSlider from shared/DuskLookAndFeel.h instead"
        fi
    done <<< "$files"
}

#==============================================================================
# CHECK 2: MISSING_VST3_AUTO_MANIFEST (Issues #10, #11)
# Without VST3_AUTO_MANIFEST FALSE, vst3_helper.exe hangs on Windows CI
# and causes Ableton crashes.
#==============================================================================

check_missing_vst3_auto_manifest() {
    local files
    files=$(get_files "CMakeLists.txt")
    [[ -z "$files" ]] && return

    while IFS= read -r f; do
        [[ -z "$f" || ! -f "$f" ]] && continue
        # Only check plugin CMakeLists (not root)
        echo "$f" | grep -q "plugins/" || continue
        if grep -q 'juce_add_plugin' "$f" && ! grep -q 'VST3_AUTO_MANIFEST\s*FALSE' "$f"; then
            local rel
            rel=$(echo "$f" | sed "s|$PROJECT_DIR/||")
            fail "MISSING_VST3_AUTO_MANIFEST (Issues #10/#11)" \
                 "juce_add_plugin() without VST3_AUTO_MANIFEST FALSE" \
                 "$rel" \
                 "Add VST3_AUTO_MANIFEST FALSE to juce_add_plugin() — vst3_helper.exe hangs on Windows CI without it"
        fi
    done <<< "$files"
}

#==============================================================================
# CHECK 3: LTO_FLAGS_ENABLED (Issue #10)
# juce_recommended_lto_flags enables /GL + /LTCG on MSVC which caused crashes.
#==============================================================================

check_lto_flags() {
    local files
    files=$(get_files "CMakeLists.txt")
    [[ -z "$files" ]] && return

    while IFS= read -r f; do
        [[ -z "$f" || ! -f "$f" ]] && continue
        echo "$f" | grep -q "plugins/" || continue
        # Match uncommented juce_recommended_lto_flags
        local matches
        matches=$(grep -n 'juce_recommended_lto_flags' "$f" 2>/dev/null | grep -v '^\s*[0-9]*:\s*#' || true)
        if [[ -n "$matches" ]]; then
            local rel
            rel=$(echo "$f" | sed "s|$PROJECT_DIR/||")
            local line
            line=$(echo "$matches" | head -1 | cut -d: -f1)
            fail "LTO_FLAGS_ENABLED (Issue #10)" \
                 "juce_recommended_lto_flags is active — causes crashes on MSVC" \
                 "$rel:$line" \
                 "Comment out juce_recommended_lto_flags in target_link_libraries()"
        fi
    done <<< "$files"
}

#==============================================================================
# CHECK 4: MULTI_TAG_PUSH (Issue #32)
# GitHub silently drops all push events when >3 tags pushed in single command.
# Tags must be pushed one at a time.
#==============================================================================

check_multi_tag_push() {
    local files
    files=$(get_files "*.sh" "*.md")
    [[ -z "$files" ]] && return

    while IFS= read -r f; do
        [[ -z "$f" || ! -f "$f" ]] && continue
        local rel
        rel=$(echo "$f" | sed "s|$PROJECT_DIR/||")

        # Check for git push --tags
        local match
        match=$(grep -n 'git push.*--tags' "$f" 2>/dev/null || true)
        if [[ -n "$match" ]]; then
            local line
            line=$(echo "$match" | head -1 | cut -d: -f1)
            fail "MULTI_TAG_PUSH (Issue #32)" \
                 "git push --tags pushes all tags at once" \
                 "$rel:$line" \
                 "Push tags one at a time: git push origin <tag>; sleep 2"
        fi

        # Check for multiple v-tags in single push
        match=$(grep -n 'git push' "$f" 2>/dev/null | grep -E '\S+-v\S+\s+\S+-v\S+' || true)
        if [[ -n "$match" ]]; then
            local line
            line=$(echo "$match" | head -1 | cut -d: -f1)
            fail "MULTI_TAG_PUSH (Issue #32)" \
                 "Multiple tags in single git push command" \
                 "$rel:$line" \
                 "Push tags one at a time with sleep 2 between each"
        fi
    done <<< "$files"
}

#==============================================================================
# CHECK 5: MISSING_SCALABLE_EDITOR (Issue #9)
# AudioProcessorEditor subclasses must use ScalableEditorHelper for proper
# resize behavior across DAWs.
#==============================================================================

check_missing_scalable_editor() {
    local files
    files=$(get_files "*Editor*.h" "*Editor.h")
    [[ -z "$files" ]] && return

    while IFS= read -r f; do
        [[ -z "$f" || ! -f "$f" ]] && continue
        echo "$f" | grep -q "plugins/" || continue
        # Skip shared code and look-and-feel files
        echo "$f" | grep -q "shared/" && continue
        echo "$f" | grep -q "LookAndFeel" && continue

        if grep -q 'AudioProcessorEditor' "$f" && ! grep -q 'ScalableEditorHelper' "$f"; then
            local rel
            rel=$(echo "$f" | sed "s|$PROJECT_DIR/||")
            fail "MISSING_SCALABLE_EDITOR (Issue #9)" \
                 "AudioProcessorEditor without ScalableEditorHelper" \
                 "$rel" \
                 "Add ScalableEditorHelper from shared/ScalableEditorHelper.h for proper resize"
        fi
    done <<< "$files"
}

#==============================================================================
# CHECK 6: JUCE_DRY_WET_MIXER (Issues #14, #21)
# juce::dsp::DryWetMixer causes comb filtering with IIR filters.
# The mix knob was removed from TapeMachine because of this.
#==============================================================================

check_juce_dry_wet_mixer() {
    local files
    files=$(get_files "*.cpp" "*.h")
    [[ -z "$files" ]] && return

    while IFS= read -r f; do
        [[ -z "$f" || ! -f "$f" ]] && continue
        echo "$f" | grep -q "plugins/" || continue
        local matches
        matches=$(grep -n 'juce::dsp::DryWetMixer' "$f" 2>/dev/null || true)
        if [[ -n "$matches" ]]; then
            local rel
            rel=$(echo "$f" | sed "s|$PROJECT_DIR/||")
            local line
            line=$(echo "$matches" | head -1 | cut -d: -f1)
            fail "JUCE_DRY_WET_MIXER (Issues #14/#21)" \
                 "juce::dsp::DryWetMixer causes comb filtering with IIR filters" \
                 "$rel:$line" \
                 "Use DuskAudio::DryWetMixer from shared/DryWetMixer.h instead"
        fi
    done <<< "$files"
}

#==============================================================================
# CHECK 7: PRESET_SYNC_MISSING (Issue #42)
# Plugins with presets must sync their UI dropdown with the DAW's preset menu.
# Editor must poll getCurrentProgram() in timerCallback, and call
# updateHostDisplay() when the user changes presets in the plugin UI.
#==============================================================================

check_preset_sync() {
    local files
    files=$(get_files "*.cpp")
    [[ -z "$files" ]] && return

    while IFS= read -r f; do
        [[ -z "$f" || ! -f "$f" ]] && continue
        echo "$f" | grep -q "plugins/" || continue
        # Skip editor files — we're looking for processor files with presets
        echo "$f" | grep -qi "editor" && continue

        # Does this processor have a real preset system?
        grep -q 'getNumPrograms' "$f" || continue
        grep -q 'setCurrentProgram' "$f" || continue

        # Check if getNumPrograms returns > 1 (has actual presets)
        local num_check
        num_check=$(grep -A2 'getNumPrograms' "$f" 2>/dev/null | grep -o 'return [0-9]*' | head -1 | grep -o '[0-9]*' || echo "0")
        [[ "${num_check:-0}" -le 1 ]] && continue

        # Find corresponding editor file
        local dir
        dir=$(dirname "$f")
        local editor_files
        editor_files=$(find "$dir" -maxdepth 2 -name "*Editor*.cpp" 2>/dev/null || true)

        for ef in $editor_files; do
            [[ -z "$ef" || ! -f "$ef" ]] && continue
            if ! grep -q 'getCurrentProgram' "$ef"; then
                local rel
                rel=$(echo "$ef" | sed "s|$PROJECT_DIR/||")
                warn "PRESET_SYNC_MISSING (Issue #42)" \
                     "Processor has presets but editor doesn't poll getCurrentProgram()" \
                     "$rel" \
                     "Poll getCurrentProgram() in timerCallback and call updateHostDisplay() on preset changes"
            fi
        done
    done <<< "$files"
}

#==============================================================================
# CHECK 8: SIDECHAIN_AUTO_GAIN (Issue #26)
# When external sidechain is active, auto-gain/auto-makeup must be disabled
# because it counteracts the ducking effect.
#==============================================================================

check_sidechain_auto_gain() {
    local files
    files=$(get_files "*.cpp")
    [[ -z "$files" ]] && return

    while IFS= read -r f; do
        [[ -z "$f" || ! -f "$f" ]] && continue
        echo "$f" | grep -q "plugins/" || continue
        # Only check processor files (where the DSP logic lives)
        echo "$f" | grep -qi "editor" && continue

        # Check if file references both sidechain and auto-gain
        grep -qi 'sidechain\|extSc' "$f" 2>/dev/null || continue
        grep -qi 'auto.*makeup\|autoMakeup' "$f" 2>/dev/null || continue

        # Check for the disabling pattern
        if ! grep -qE 'auto.*&&.*!.*sidechain|auto.*&&.*!.*extSc|autoMakeup.*&&.*!.*extSc' "$f"; then
            local rel
            rel=$(echo "$f" | sed "s|$PROJECT_DIR/||")
            warn "SIDECHAIN_AUTO_GAIN (Issue #26)" \
                 "File has sidechain + auto-gain but may not disable auto-gain for sidechain" \
                 "$rel" \
                 "Add guard: bool autoMakeup = autoMakeupRaw && !extScEnabled;"
        fi
    done <<< "$files"
}

#==============================================================================
# Run all checks
#==============================================================================

check_deprecated_configure_knob
check_missing_vst3_auto_manifest
check_lto_flags
check_multi_tag_push
check_missing_scalable_editor
check_juce_dry_wet_mixer
check_preset_sync
check_sidechain_auto_gain

#==============================================================================
# Report
#==============================================================================

if [[ $FAILURES -gt 0 ]]; then
    echo -e "${RED}============================================================${NC}" >&2
    echo -e "${RED}  REGRESSION CHECK FAILED: $FAILURES issue(s) found${NC}" >&2
    if [[ $WARNINGS -gt 0 ]]; then
        echo -e "${YELLOW}  Plus $WARNINGS warning(s)${NC}" >&2
    fi
    echo -e "${RED}============================================================${NC}" >&2
    exit 2
fi

if [[ $WARNINGS -gt 0 ]]; then
    echo -e "${YELLOW}Regression check passed with $WARNINGS warning(s)${NC}" >&2
    exit 0
fi

echo -e "${GREEN}Regression check passed — no known bug patterns found${NC}" >&2
exit 0

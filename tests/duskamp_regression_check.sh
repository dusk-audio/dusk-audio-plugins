#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# DuskAmp factory-preset audio regression check.
#
# Renders all factory presets through the synthetic DI and bit-compares
# against the committed golden set in tests/golden_renders/.
#
#   PASS = output is bit-identical = no DSP regression.
#   FAIL = either an unintended audio change OR an intentional DSP change
#          that requires re-running the renderer to bless new goldens.
#
# Re-bless workflow when an intentional DSP change lands:
#   1. Build: cmake --build build --target duskamp_golden_render -j8
#   2. Re-render: ./build/tests/duskamp_golden_render/duskamp_golden_render
#   3. Inspect diff (audio listening, waveform compare).
#   4. Commit the updated tests/golden_renders/*.wav.
#
# Usage:
#   ./tests/duskamp_regression_check.sh           # use existing build
#   ./tests/duskamp_regression_check.sh --build   # rebuild renderer first

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RENDERER="$PROJECT_DIR/build/tests/duskamp_golden_render/duskamp_golden_render"
GOLDEN_DIR="$PROJECT_DIR/tests/golden_renders"

if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; NC=''
fi

if [[ "${1:-}" == "--build" ]] || [ ! -x "$RENDERER" ]; then
    echo "Building renderer..."
    if [ ! -d "$PROJECT_DIR/build" ]; then
        mkdir "$PROJECT_DIR/build"
        (cd "$PROJECT_DIR/build" && cmake .. -DBUILD_DUSKAMP_TESTS=ON > /dev/null)
    fi
    cmake --build "$PROJECT_DIR/build" --config Release --target duskamp_golden_render -j8
fi

if [ ! -d "$GOLDEN_DIR" ] || [ -z "$(ls -A "$GOLDEN_DIR"/*.wav 2>/dev/null)" ]; then
    echo -e "${YELLOW}No golden renders found in $GOLDEN_DIR${NC}"
    echo ""
    echo "Golden WAVs are not committed — each developer renders them locally."
    echo "Bless your local baseline with:"
    echo "  $RENDERER"
    echo ""
    echo "See tests/golden_renders/README.md for the full workflow."
    exit 1
fi

TEMP_DIR="$(mktemp -d -t duskamp_regression.XXXXXX)"
trap 'rm -rf "$TEMP_DIR"' EXIT

echo "Rendering to temp dir..."
"$RENDERER" "$TEMP_DIR" > /dev/null

# Hash by basename so the absolute path doesn't poison the comparison.
hash_dir() {
    local dir="$1"
    (cd "$dir" && for f in *.wav; do
        if [ -f "$f" ]; then
            md5 -q "$f" | awk -v name="$f" '{ print $1 "  " name }'
        fi
    done | sort)
}

GOLDEN_HASHES=$(hash_dir "$GOLDEN_DIR")
FRESH_HASHES=$(hash_dir "$TEMP_DIR")

GOLDEN_COUNT=$(echo "$GOLDEN_HASHES" | wc -l | tr -d ' ')
FRESH_COUNT=$(echo "$FRESH_HASHES" | wc -l | tr -d ' ')

if [ "$GOLDEN_HASHES" = "$FRESH_HASHES" ]; then
    echo -e "${GREEN}PASS${NC}: all $GOLDEN_COUNT presets bit-identical to golden set"
    exit 0
fi

echo -e "${RED}FAIL${NC}: regression detected"
echo "Golden count: $GOLDEN_COUNT, fresh count: $FRESH_COUNT"
echo ""
echo "Differing files:"
diff <(echo "$GOLDEN_HASHES") <(echo "$FRESH_HASHES") || true
echo ""
echo "If this was an intentional DSP change:"
echo "  1. Listen to old vs new for the affected presets."
echo "  2. If the change is desired, re-bless: $RENDERER"
echo "  3. Commit the updated tests/golden_renders/*.wav."
exit 2

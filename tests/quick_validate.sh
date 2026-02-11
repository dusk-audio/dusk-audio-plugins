#!/bin/bash
#==============================================================================
# Dusk Audio - Quick Plugin Validation
# Fast sanity checks for all plugins
#==============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

VST3_DIR="$HOME/.vst3"
LV2_DIR="$HOME/.lv2"

PLUGINS=(
    "4K EQ"
    "Universal Compressor"
    "TapeMachine"
    "Plate Reverb"
    "Vintage Tape Echo"
    "DrummerClone"
)

echo -e "${BLUE}Dusk Audio - Quick Plugin Validation${NC}"
echo "=========================================="
echo ""

passed=0
failed=0

for plugin in "${PLUGINS[@]}"; do
    vst3_path="$VST3_DIR/${plugin}.vst3"
    lv2_path="$LV2_DIR/${plugin}.lv2"

    echo -n "Checking $plugin... "

    vst3_ok=false
    lv2_ok=false
    status=""

    # Check VST3
    if [ -d "$vst3_path" ] && [ -f "$vst3_path/Contents/x86_64-linux/${plugin}.so" ]; then
        vst3_ok=true
        status+="VST3:OK "
    else
        status+="VST3:MISSING "
    fi

    # Check LV2
    if [ -d "$lv2_path" ] && [ -f "$lv2_path/manifest.ttl" ]; then
        lv2_ok=true
        status+="LV2:OK"
    else
        status+="LV2:MISSING"
    fi

    # Pass if at least VST3 is present (LV2 is optional for some plugins)
    if $vst3_ok; then
        echo -e "${GREEN}OK${NC} ($status)"
        ((passed++)) || true
    else
        echo -e "${RED}FAIL${NC} ($status)"
        ((failed++)) || true
    fi
done

echo ""
echo "=========================================="
echo -e "Results: ${GREEN}$passed passed${NC}, ${RED}$failed failed${NC}"

# Check for pluginval
echo ""
if command -v pluginval &> /dev/null; then
    echo -e "${GREEN}pluginval is available${NC} - run './run_plugin_tests.sh' for full validation"
else
    echo -e "${YELLOW}pluginval not installed${NC}"
    echo "  Install from: https://github.com/Tracktion/pluginval/releases"
    echo "  Or: Download AppImage and chmod +x"
fi

# Check for Python and scipy
echo ""
if python3 -c "import numpy, scipy" 2>/dev/null; then
    echo -e "${GREEN}Python audio analysis available${NC}"
else
    echo -e "${YELLOW}Python numpy/scipy not installed${NC}"
    echo "  Install with: pip3 install numpy scipy"
fi

exit $failed

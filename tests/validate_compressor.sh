#!/bin/bash
#
# Universal Compressor Validation Script
# ======================================
# Tests compressor behavior against hardware reference values
# Uses audio_analyzer.py for THD, envelope timing, and transfer curve measurements
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_NAME="Universal Compressor"
OUTPUT_DIR="${SCRIPT_DIR}/output/compressor_validation"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=============================================="
echo "  Universal Compressor Validation Suite"
echo "=============================================="
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if audio analyzer exists
if [ ! -f "${SCRIPT_DIR}/audio_analyzer.py" ]; then
    echo -e "${RED}Error: audio_analyzer.py not found${NC}"
    exit 1
fi

# Check Python dependencies
if ! python3 -c "import numpy, scipy" 2>/dev/null; then
    echo -e "${YELLOW}Warning: numpy and scipy required. Installing...${NC}"
    pip3 install numpy scipy --quiet
fi

echo "Running validation tests..."
echo ""

# Test 1: THD Measurement at 1kHz
echo "=== Test 1: THD Measurement at 1kHz ==="
echo "Generating test signal..."
python3 "${SCRIPT_DIR}/audio_analyzer.py" --plugin "${PLUGIN_NAME}" \
    --output-dir "${OUTPUT_DIR}" \
    --test thd \
    --frequency 1000 \
    2>&1 | tee "${OUTPUT_DIR}/thd_results.log"

# Test 2: Attack/Release Time Validation (FET mode)
echo ""
echo "=== Test 2: Attack/Release Envelope Times ==="
echo "Testing FET mode envelope response..."
python3 "${SCRIPT_DIR}/audio_analyzer.py" --plugin "${PLUGIN_NAME}" \
    --output-dir "${OUTPUT_DIR}" \
    --test envelope \
    --mode FET \
    2>&1 | tee "${OUTPUT_DIR}/envelope_results.log"

# Test 3: All-Buttons Transfer Curve Measurement
echo ""
echo "=== Test 3: FET All-Buttons Transfer Curve ==="
echo "Measuring compression transfer curve..."
python3 "${SCRIPT_DIR}/audio_analyzer.py" --plugin "${PLUGIN_NAME}" \
    --output-dir "${OUTPUT_DIR}" \
    --test transfer-curve \
    --mode FET \
    --ratio all-buttons \
    2>&1 | tee "${OUTPUT_DIR}/transfer_curve_results.log"

# Test 4: Noise Floor
echo ""
echo "=== Test 4: Noise Floor ==="
echo "Measuring self-noise..."
python3 "${SCRIPT_DIR}/audio_analyzer.py" --plugin "${PLUGIN_NAME}" \
    --output-dir "${OUTPUT_DIR}" \
    --test noise-floor \
    2>&1 | tee "${OUTPUT_DIR}/noise_floor_results.log"

# Test 5: Null Test (Bypass)
echo ""
echo "=== Test 5: Bypass Null Test ==="
echo "Verifying bypass transparency..."
python3 "${SCRIPT_DIR}/audio_analyzer.py" --plugin "${PLUGIN_NAME}" \
    --output-dir "${OUTPUT_DIR}" \
    --test null \
    2>&1 | tee "${OUTPUT_DIR}/null_test_results.log"

echo ""
echo "=============================================="
echo "  Validation Complete"
echo "=============================================="
echo ""
echo "Results saved to: ${OUTPUT_DIR}"
echo ""

# Parse results and display summary
echo "=== Summary ==="
if [ -f "${OUTPUT_DIR}/thd_results.log" ]; then
    THD=$(grep -oP 'THD: \K[0-9.]+' "${OUTPUT_DIR}/thd_results.log" 2>/dev/null || echo "N/A")
    if [ "$THD" != "N/A" ]; then
        if (( $(echo "$THD < 0.5" | bc -l 2>/dev/null || echo "0") )); then
            echo -e "THD @ 1kHz: ${GREEN}${THD}%${NC} (PASS < 0.5%)"
        else
            echo -e "THD @ 1kHz: ${YELLOW}${THD}%${NC} (Check < 0.5%)"
        fi
    else
        echo "THD @ 1kHz: Results pending manual processing"
    fi
fi

echo ""
echo "For detailed analysis, run:"
echo "  python3 ${SCRIPT_DIR}/audio_analyzer.py --plugin '${PLUGIN_NAME}' --analyze"
echo ""

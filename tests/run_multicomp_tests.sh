#!/bin/bash
# Run Multi-Comp unit tests including comb filtering detection
# This script builds and runs the Multi-Comp unit tests
#
# Usage:
#   ./tests/run_multicomp_tests.sh
#   ./tests/run_multicomp_tests.sh --verbose

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=== Multi-Comp Unit Tests ==="
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Build directory not found. Creating...${NC}"
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Configure with tests enabled
echo "Configuring with tests enabled..."
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_MULTI_COMP_TESTS=ON

# Build the test executable
echo ""
echo "Building MultiCompTests..."
cmake --build . --target MultiCompTests -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Check if test executable exists
TEST_EXE="$BUILD_DIR/plugins/multi-comp/MultiCompTests"
if [ ! -f "$TEST_EXE" ]; then
    # Try alternate location
    TEST_EXE=$(find "$BUILD_DIR" -name "MultiCompTests" -type f -perm +111 2>/dev/null | head -1)
fi

if [ ! -f "$TEST_EXE" ]; then
    echo -e "${RED}Error: MultiCompTests executable not found${NC}"
    exit 1
fi

echo ""
echo "Running tests..."
echo ""

# Run the tests
VERBOSE=""
if [ "$1" = "--verbose" ] || [ "$1" = "-v" ]; then
    VERBOSE="--verbose"
fi

if "$TEST_EXE" --all $VERBOSE; then
    echo ""
    echo -e "${GREEN}All tests PASSED!${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}Some tests FAILED!${NC}"
    echo ""
    echo "Critical tests include:"
    echo "  - testMixKnobPhaseAlignment: Detects comb filtering with oversampling + mix knob"
    echo "  - testDSPStability: Ensures no NaN/Inf values"
    echo "  - testGainReduction: Validates compression behavior"
    exit 1
fi

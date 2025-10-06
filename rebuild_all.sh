#!/bin/bash

#==============================================================================
# Comprehensive Plugin Build Script for Luna Co. Audio
#
# This script performs a complete clean, build, and install of all audio plugins
# Usage: ./rebuild_all.sh [options]
# Options:
#   --debug     Build in Debug mode
#   --release   Build in Release mode (default)
#   --clean     Clean only, don't build
#   --fast      Use ccache and ninja if available
#   --parallel  Number of parallel jobs (default: auto-detect)
#==============================================================================

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default settings
BUILD_TYPE="Release"
CLEAN_ONLY=false
USE_FAST=false
NUM_JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
BUILD_DIR="build"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --clean)
            CLEAN_ONLY=true
            shift
            ;;
        --fast)
            USE_FAST=true
            shift
            ;;
        --parallel)
            NUM_JOBS="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [--debug|--release] [--clean] [--fast] [--parallel N]"
            exit 1
            ;;
    esac
done

# Function to print colored status messages
print_status() {
    echo -e "${CYAN}==>${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Header
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}   Luna Co. Audio - Plugin Build System${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo

# Display configuration
print_status "Build Configuration:"
echo "  • Build Type: ${BUILD_TYPE}"
echo "  • Parallel Jobs: ${NUM_JOBS}"
echo "  • Build Directory: ${BUILD_DIR}"

# Check for optional build tools
if [ "$USE_FAST" = true ]; then
    print_status "Checking for optimization tools..."

    if command_exists ccache; then
        export CC="ccache gcc"
        export CXX="ccache g++"
        print_success "ccache found - will use for compilation caching"

        # Show ccache stats
        echo -e "${CYAN}  Cache stats:${NC}"
        ccache -s | grep "cache hit rate" || true
    else
        print_warning "ccache not found - consider installing for faster rebuilds"
        echo "  Install with: sudo dnf install ccache"
    fi

    if command_exists ninja; then
        CMAKE_GENERATOR="-GNinja"
        BUILD_COMMAND="ninja"
        print_success "ninja found - will use for faster builds"
    else
        CMAKE_GENERATOR=""
        BUILD_COMMAND="make"
        print_warning "ninja not found - using make"
        echo "  Install with: sudo dnf install ninja-build"
    fi
else
    CMAKE_GENERATOR=""
    BUILD_COMMAND="make"
fi

echo

# Clean build directory
if [ "$CLEAN_ONLY" = true ] || [ ! -d "$BUILD_DIR" ]; then
    print_status "Cleaning build directory..."
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"/*
        print_success "Build directory cleaned"
    else
        mkdir -p "$BUILD_DIR"
        print_success "Build directory created"
    fi

    if [ "$CLEAN_ONLY" = true ]; then
        print_success "Clean complete!"
        exit 0
    fi
fi

# Enter build directory
cd "$BUILD_DIR"

# Configure with CMake
print_status "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    ${CMAKE_GENERATOR} \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_C_COMPILER="${CC:-gcc}" \
    -DCMAKE_CXX_COMPILER="${CXX:-g++}"

if [ $? -eq 0 ]; then
    print_success "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

echo

# List of all plugins to build
PLUGINS=(
    "FourKEQ_All"
    "TapeMachine_All"
    "UniversalCompressor_All"
    "PlateReverb_All"
    "TapeEcho_All"
)

# Build all plugins
print_status "Building all plugins with ${NUM_JOBS} parallel jobs..."
echo

FAILED_PLUGINS=()
SUCCESSFUL_PLUGINS=()

for PLUGIN in "${PLUGINS[@]}"; do
    PLUGIN_NAME=$(echo $PLUGIN | sed 's/_All//')
    echo -e "${CYAN}Building ${PLUGIN_NAME}...${NC}"

    if ${BUILD_COMMAND} ${PLUGIN} -j${NUM_JOBS} > /tmp/${PLUGIN_NAME}_build.log 2>&1; then
        print_success "${PLUGIN_NAME} built successfully"
        SUCCESSFUL_PLUGINS+=("${PLUGIN_NAME}")
    else
        print_error "${PLUGIN_NAME} build failed (see /tmp/${PLUGIN_NAME}_build.log for details)"
        FAILED_PLUGINS+=("${PLUGIN_NAME}")

        # Show last few lines of error log
        echo -e "${RED}Last 10 lines of error log:${NC}"
        tail -n 10 /tmp/${PLUGIN_NAME}_build.log
        echo
    fi
done

echo
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}   Build Summary${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Show summary
if [ ${#SUCCESSFUL_PLUGINS[@]} -gt 0 ]; then
    echo -e "${GREEN}Successfully built (${#SUCCESSFUL_PLUGINS[@]}):${NC}"
    for plugin in "${SUCCESSFUL_PLUGINS[@]}"; do
        echo "  ✓ $plugin"
    done
fi

if [ ${#FAILED_PLUGINS[@]} -gt 0 ]; then
    echo -e "${RED}Failed to build (${#FAILED_PLUGINS[@]}):${NC}"
    for plugin in "${FAILED_PLUGINS[@]}"; do
        echo "  ✗ $plugin"
    done
fi

echo

# Check installation
print_status "Checking plugin installations..."
echo

VST3_DIR="$HOME/.vst3"
LV2_DIR="$HOME/.lv2"

if [ -d "$VST3_DIR" ]; then
    VST3_COUNT=$(ls -1 "$VST3_DIR"/*.vst3 2>/dev/null | wc -l)
    echo -e "${GREEN}VST3 plugins installed:${NC} $VST3_COUNT"
    ls -1 "$VST3_DIR"/*.vst3 2>/dev/null | sed 's/.*\//  • /'
fi

if [ -d "$LV2_DIR" ]; then
    LV2_COUNT=$(ls -1d "$LV2_DIR"/*.lv2 2>/dev/null | wc -l)
    echo -e "${GREEN}LV2 plugins installed:${NC} $LV2_COUNT"
    ls -1d "$LV2_DIR"/*.lv2 2>/dev/null | sed 's/.*\//  • /'
fi

echo

# Final status
if [ ${#FAILED_PLUGINS[@]} -eq 0 ]; then
    print_success "All plugins built and installed successfully! 🎉"

    # Show cache stats if using ccache
    if [ "$USE_FAST" = true ] && command_exists ccache; then
        echo
        print_status "Build cache statistics:"
        ccache -s | grep -E "cache hit rate|cache size" || true
    fi
else
    print_error "Some plugins failed to build. Check the log files for details."
    exit 1
fi

echo
echo -e "${CYAN}Plugins are ready to use in your DAW!${NC}"
echo -e "${CYAN}Company: Luna Co. Audio${NC}"

# Return to original directory
cd ..
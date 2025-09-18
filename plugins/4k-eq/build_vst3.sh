#!/bin/bash
# Build script for SSL 4000 EQ - VST3 version only

set -e

echo "========================================"
echo "SSL 4000 EQ - VST3 Build"
echo "========================================"
echo ""

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf build_vst3
mkdir -p build_vst3
cd build_vst3

# Create CMakeLists.txt for VST3 only
echo -e "${YELLOW}Creating build configuration...${NC}"
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.15)
project(SSL4KEQ_VST3 VERSION 1.0.0)

# Add JUCE
add_subdirectory(/home/marc/Projects/JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

# Define the plugin - VST3 ONLY
juce_add_plugin(SSL4KEQ
    COMPANY_NAME "SSLEmulation"
    BUNDLE_ID "com.sslemulation.ssl4keq"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE SSLm
    PLUGIN_CODE SSL4
    FORMATS VST3
    PRODUCT_NAME "SSL 4000 EQ"
    VST3_CATEGORIES "Fx" "EQ"
)

# Generate JuceHeader.h FIRST
juce_generate_juce_header(SSL4KEQ)

# Add the generated JuceHeader location to include directories
target_include_directories(SSL4KEQ
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/SSL4KEQ_artefacts/JuceLibraryCode
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Add sources AFTER setting up includes
target_sources(SSL4KEQ
    PRIVATE
        ../SSL4KEQ.cpp
        ../PluginEditor.cpp
        ../SSLLookAndFeel.cpp
        ../LV2InlineDisplay_stub.cpp
)

# Compile definitions
target_compile_definitions(SSL4KEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Build_VST3=1
        JucePlugin_Build_LV2=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_Standalone=0
)

# Link libraries
target_link_libraries(SSL4KEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Set C++ standard
target_compile_features(SSL4KEQ PRIVATE cxx_std_17)

# Set optimization for release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(SSL4KEQ PRIVATE -O3)
endif()

# Ensure VST3 symbols are properly exported
target_compile_options(SSL4KEQ_VST3 PRIVATE
    -fvisibility=hidden
)

target_compile_definitions(SSL4KEQ_VST3 PRIVATE
    JUCE_DLL_BUILD=1
)
EOF

# Configure with CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
cmake . -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE

# First, generate the JuceHeader by building the juce_vst3_helper
echo -e "${YELLOW}Generating JUCE headers...${NC}"
make juce_vst3_helper -j$(nproc)

# Check if JuceHeader.h was generated
if [ -f "SSL4KEQ_artefacts/JuceLibraryCode/JuceHeader.h" ]; then
    echo -e "${GREEN}✓ JuceHeader.h generated successfully${NC}"
else
    echo -e "${RED}✗ JuceHeader.h not found, build may fail${NC}"
fi

# Now build the actual VST3 plugin
echo -e "${YELLOW}Building VST3 plugin...${NC}"
if make -j$(nproc) 2>&1 | tee build.log | grep -E "(\[.*%\]|Built target|error)" ; then
    echo -e "${GREEN}✓ Build completed${NC}"
else
    echo -e "${RED}Build failed - check build.log for details${NC}"
    tail -20 build.log
    exit 1
fi

# Find the built VST3
echo ""
echo -e "${YELLOW}Looking for built VST3...${NC}"
VST3_BUILD=$(find . -name "*.vst3" -type d | grep -v CMakeFiles | head -1)

if [ -n "$VST3_BUILD" ] && [ -d "$VST3_BUILD" ]; then
    # Check if the VST3 actually contains a binary
    VST3_BINARY=$(find "$VST3_BUILD" -name "*.so" -type f | head -1)

    if [ -n "$VST3_BINARY" ] && [ -f "$VST3_BINARY" ]; then
        echo -e "${GREEN}✓ VST3 plugin built successfully${NC}"
        echo "  Location: $VST3_BUILD"
        echo "  Binary: $VST3_BINARY"
        echo "  Size: $(du -h "$VST3_BINARY" | cut -f1)"

        # Install to user VST3 directory
        VST3_DEST="$HOME/.vst3/SSL_4000_EQ.vst3"
        echo ""
        echo -e "${YELLOW}Installing VST3...${NC}"
        mkdir -p "$HOME/.vst3"
        rm -rf "$VST3_DEST"
        cp -r "$VST3_BUILD" "$VST3_DEST"
        echo -e "${GREEN}✓ VST3 installed to $VST3_DEST${NC}"

        # Success summary
        echo ""
        echo "========================================"
        echo -e "${GREEN}        VST3 Build Successful!${NC}"
        echo "========================================"
        echo ""
        echo "The SSL 4000 EQ VST3 plugin has been installed to:"
        echo "  $VST3_DEST"
        echo ""
        echo "You can now use it in:"
        echo "  • Reaper"
        echo "  • Bitwig Studio"
        echo "  • FL Studio"
        echo "  • Ardour (as VST3)"
        echo "  • Any VST3-compatible DAW"
        echo ""
        echo "The plugin features:"
        echo "  • Full JUCE GUI with SSL-styled interface"
        echo "  • 4-band parametric EQ"
        echo "  • HPF/LPF filters"
        echo "  • Brown/Black EQ modes"
        echo "  • Analog saturation"
        echo ""
    else
        echo -e "${RED}✗ VST3 directory found but no binary inside${NC}"
        echo "Contents of $VST3_BUILD:"
        find "$VST3_BUILD" -type f
        exit 1
    fi
else
    echo -e "${RED}✗ No VST3 build found${NC}"
    echo "Build artifacts:"
    find . -name "*.so" -o -name "*.vst3" 2>/dev/null
    exit 1
fi

cd ..
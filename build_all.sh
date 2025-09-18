#!/bin/bash

# Build all Luna Co. audio plugins

echo "Building Luna Co. Audio Plugins..."
echo "================================="

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build all plugins
echo "Building..."
make -j$(nproc)

echo "================================="
echo "Build complete!"
echo ""
echo "Plugins installed to:"
echo "  VST3: ~/.vst3/"
echo "  LV2:  ~/.lv2/"
echo ""
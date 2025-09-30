#!/bin/bash

# Studio Verb Build Script
# Copyright (c) 2024 Luna CO. Audio

echo "========================================="
echo "  Studio Verb - Professional Reverb"
echo "  Luna CO. Audio"
echo "========================================="
echo ""

# Check if JUCE is available
if [ ! -d "JUCE" ]; then
    echo "JUCE not found. Cloning JUCE repository..."
    git clone https://github.com/juce-framework/JUCE.git
    cd JUCE
    git checkout 7.0.9  # Use stable version
    cd ..
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring project with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "Building Studio Verb..."
cmake --build . --config Release -j$(nproc)

echo ""
echo "========================================="
echo "Build complete!"
echo ""
echo "Binaries location:"
echo "  VST3: build/StudioVerb_artefacts/VST3/"
echo "  AU:   build/StudioVerb_artefacts/AU/"
echo "  Standalone: build/StudioVerb_artefacts/Standalone/"
echo "========================================="
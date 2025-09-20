#!/bin/bash

# Rebuild all plugins with proper configuration
set -e

echo "========================================"
echo "Rebuilding All Audio Plugins"
echo "========================================"

# Clean everything
echo "Cleaning old builds..."
rm -rf build
rm -rf ~/.vst3/*.vst3

# Create fresh build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_4K_EQ=OFF \
    -DBUILD_UNIVERSAL_COMPRESSOR=ON \
    -DBUILD_HARMONIC_GENERATOR=ON \
    -DBUILD_TAPE_MACHINE=OFF

# Build plugins (limit parallelism to avoid memory issues)
echo "Building plugins..."
make -j2

# Find and install VST3 plugins
echo "Installing VST3 plugins..."
mkdir -p ~/.vst3

# Copy Universal Compressor
if [ -d "plugins/universal-compressor/UniversalCompressor_artefacts/Release/VST3/Universal Compressor.vst3" ]; then
    cp -r "plugins/universal-compressor/UniversalCompressor_artefacts/Release/VST3/Universal Compressor.vst3" ~/.vst3/
    echo "Installed Universal Compressor.vst3"
fi

# Copy Harmonic Generator
if [ -d "plugins/harmonic-generator/HarmonicGeneratorPlugin_artefacts/Release/VST3/HarmonicGenerator.vst3" ]; then
    cp -r "plugins/harmonic-generator/HarmonicGeneratorPlugin_artefacts/Release/VST3/HarmonicGenerator.vst3" ~/.vst3/
    echo "Installed HarmonicGenerator.vst3"
fi

# Copy TapeMachine if built
if [ -d "lib/VST3/TapeMachine.vst3" ]; then
    cp -r "lib/VST3/TapeMachine.vst3" ~/.vst3/
    echo "Installed TapeMachine.vst3"
fi

echo ""
echo "========================================"
echo "Checking installed plugins..."
echo "========================================"

# Verify installations
for plugin in ~/.vst3/*.vst3; do
    if [ -d "$plugin" ]; then
        name=$(basename "$plugin")
        so_file="$plugin/Contents/x86_64-linux/${name%.vst3}.so"
        if [ -f "$so_file" ]; then
            size=$(stat -c%s "$so_file")
            if [ $size -gt 1000000 ]; then
                echo "✓ $name - OK ($(numfmt --to=iec-i --suffix=B $size))"
            else
                echo "✗ $name - File too small ($(numfmt --to=iec-i --suffix=B $size))"
            fi
        else
            echo "✗ $name - Missing .so file"
        fi
    fi
done

echo ""
echo "Build complete!"
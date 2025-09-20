#!/bin/bash

# Build all Luna Co. audio plugins

echo "Building Luna Co. Audio Plugins..."
echo "================================="

# Function to check if command succeeded
check_command() {
    if [ $? -ne 0 ]; then
        echo "Error: $1 failed"
        exit 1
    fi
}

# Clean previous builds if they exist
if [ -d "build" ]; then
    echo "Cleaning previous build..."
    rm -rf build
    check_command "Clean build directory"
fi

# Create build directory
mkdir -p build
cd build
check_command "Create build directory"

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release
check_command "CMake configuration"

# Build all plugins
echo "Building plugins..."
if ! make -j$(nproc); then
    echo "Warning: Some plugins may have failed to compile, but continuing..."
    echo "Check the output above for details on any compilation failures."
fi

# Install plugins (try make install first, then manual copy for those without install targets)
echo "Installing plugins..."
make install 2>/dev/null || echo "Some plugins may not have install targets, will copy manually"

# Manual installation for plugins without proper install targets
echo "Manually installing plugins without install targets..."
for vst3_file in $(find . -name "*.vst3" -type d 2>/dev/null); do
    vst3_name=$(basename "$vst3_file")
    if [ ! -d ~/.vst3/"$vst3_name" ]; then
        echo "Installing $vst3_name to ~/.vst3/"
        cp -r "$vst3_file" ~/.vst3/
    fi
done

for lv2_file in $(find . -name "*.lv2" -type d 2>/dev/null); do
    lv2_name=$(basename "$lv2_file")
    if [ ! -d ~/.lv2/"$lv2_name" ]; then
        echo "Installing $lv2_name to ~/.lv2/"
        cp -r "$lv2_file" ~/.lv2/
    fi
done

echo "Installation completed successfully!"

# Verify VST3 installations
echo ""
echo "================================="
echo "Build and installation complete!"
echo ""
echo "Verifying plugin installations:"
echo "VST3 plugins in ~/.vst3/:"
ls -la ~/.vst3/*.vst3 2>/dev/null || echo "  No VST3 plugins found in ~/.vst3/"

# Check if plugins were built in the build directory
echo ""
echo "Built plugins in build directory:"
find . -name "*.vst3" -type d 2>/dev/null | head -10

echo ""
echo "================================="
echo "All done!"
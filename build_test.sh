#!/bin/bash

# Build and run the StudioReverb test program

echo "Building StudioReverb test program..."

# Create build directory
mkdir -p build_test
cd build_test

# Configure with CMake
cmake -DCMAKE_BUILD_TYPE=Release ../CMakeLists_test.txt

# Build
make -j4

# Check if build succeeded
if [ $? -eq 0 ]; then
    echo "Build successful! Running tests..."
    echo ""
    ./test_reverb
else
    echo "Build failed!"
    exit 1
fi
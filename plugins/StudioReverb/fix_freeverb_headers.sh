#!/bin/bash

# Fix Freeverb3 headers for C++17 compatibility by removing deprecated exception specifications

FREEVERB_DIR="/home/marc/Projects/Luna/plugins/plugins/StudioReverb/Source/freeverb"

echo "Fixing Freeverb3 headers for C++17 compatibility..."

# Find all .hpp and .h files and remove throw() specifications
find "$FREEVERB_DIR" -type f \( -name "*.hpp" -o -name "*.h" \) -exec sed -i 's/throw()//g' {} \;

# Also remove throw(std::bad_alloc) and similar
find "$FREEVERB_DIR" -type f \( -name "*.hpp" -o -name "*.h" \) -exec sed -i 's/throw([^)]*)//g' {} \;

echo "Fixed all exception specifications in Freeverb3 headers"
echo "Headers are now C++17 compatible"
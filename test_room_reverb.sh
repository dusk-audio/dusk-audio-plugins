#!/bin/bash

echo "Building and testing Room reverb..."

# List all freeverb dependencies we need
FREEVERB_SRCS="
  plugins/StudioReverb/Source/freeverb/progenitor2.cpp
  plugins/StudioReverb/Source/freeverb/progenitor.cpp
  plugins/StudioReverb/Source/freeverb/revbase.cpp
  plugins/StudioReverb/Source/freeverb/utils.cpp
  plugins/StudioReverb/Source/freeverb/delay.cpp
  plugins/StudioReverb/Source/freeverb/delayline.cpp
  plugins/StudioReverb/Source/freeverb/comb.cpp
  plugins/StudioReverb/Source/freeverb/allpass.cpp
  plugins/StudioReverb/Source/freeverb/biquad.cpp
  plugins/StudioReverb/Source/freeverb/efilter.cpp
  plugins/StudioReverb/Source/freeverb/slot.cpp
  plugins/StudioReverb/Source/freeverb/modulator.cpp
"

# Compile
echo "Compiling test program..."
g++ -o minimal_test minimal_test.cpp \
  $FREEVERB_SRCS \
  -I. \
  -I./plugins/StudioReverb/Source \
  -DLIBFV3_FLOAT \
  -std=c++17 \
  -O2 -lm 2>&1 | tail -10

if [ $? -eq 0 ]; then
    echo "Compilation successful! Running test..."
    echo ""
    ./minimal_test
    RESULT=$?

    if [ $RESULT -eq 0 ]; then
        echo ""
        echo "✓ Room reverb is working correctly!"
        echo "The fix has been successful."
    else
        echo ""
        echo "✗ Room reverb is still not working."
        echo "Further investigation needed."
    fi

    rm -f minimal_test
    exit $RESULT
else
    echo "Compilation failed. Check the error messages above."
    exit 1
fi
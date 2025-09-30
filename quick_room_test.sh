#!/bin/bash

# Quick test script to verify Room reverb is working
# This script analyzes the debug output from the plugin

echo "Quick Room Reverb Test"
echo "====================="
echo ""

# Check if the plugin was built
if [ ! -f "build/plugins/StudioReverb/StudioReverb_artefacts/Release/Standalone/StudioReverb" ]; then
    echo "Error: StudioReverb standalone not found. Please build first."
    exit 1
fi

# Run the standalone briefly and capture debug output
echo "Running StudioReverb standalone briefly to capture debug output..."
timeout 3 ./build/plugins/StudioReverb/StudioReverb_artefacts/Release/Standalone/StudioReverb 2>&1 | grep -E "Room|maxInput|maxOutput|wet=|dry=|Room mixing" > room_test.log 2>&1 &

# Wait a moment for it to start
sleep 2

# Kill the standalone
pkill -f StudioReverb 2>/dev/null

# Analyze the log
echo ""
echo "Analyzing debug output..."

if [ -f "room_test.log" ] && [ -s "room_test.log" ]; then
    echo "Debug output captured:"
    cat room_test.log

    # Check for key indicators
    if grep -q "maxOutput: 0.000000" room_test.log; then
        echo ""
        echo "❌ WARNING: Room reverb may not be producing output (maxOutput is 0)"
        echo "   The fix may not have resolved the issue."
    elif grep -q "maxOutput:" room_test.log; then
        OUTPUT=$(grep "maxOutput:" room_test.log | head -1 | awk '{print $4}')
        echo ""
        echo "✓ Room reverb appears to be producing output: $OUTPUT"
        echo "  The fix seems to be working!"
    fi

    # Check mix levels
    if grep -q "lateLevel:" room_test.log; then
        echo ""
        echo "Late level values found in debug output:"
        grep "lateLevel:" room_test.log | head -3
    fi
else
    echo "No debug output captured. The plugin may not have started properly."
fi

# Clean up
rm -f room_test.log

echo ""
echo "Test complete."
echo ""
echo "To manually test:"
echo "1. Open a DAW (like Reaper or Carla)"
echo "2. Load the StudioReverb plugin from ~/.vst3/"
echo "3. Select 'Room' from the algorithm dropdown"
echo "4. Turn up the 'Late Level' knob"
echo "5. Play audio through it - you should hear reverb!"
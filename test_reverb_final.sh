#!/bin/bash

echo "================================="
echo "ROOM REVERB VALIDATION TEST"
echo "================================="
echo ""

# Run the standalone briefly to trigger debug output
echo "Running StudioReverb to capture debug output..."
timeout 1 /home/marc/projects/plugins/build/plugins/StudioReverb/StudioReverb_artefacts/Release/Standalone/StudioReverb 2>&1 | grep -A5 "ROOM LATE DEBUG"

echo ""
echo "If you see 'ROOM LATE DEBUG' output above with non-zero values,"
echo "then the reverb is processing correctly!"

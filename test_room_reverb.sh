#!/bin/bash

echo "Testing Room Reverb..."
timeout 3 ./build/plugins/StudioReverb/StudioReverb_artefacts/Release/Standalone/StudioReverb 2>&1 | grep -A5 "ROOM REVERB DEBUG" | head -30

# Check if we're getting non-zero output
OUTPUT=$(timeout 3 ./build/plugins/StudioReverb/StudioReverb_artefacts/Release/Standalone/StudioReverb 2>&1 | grep "Output from late reverb:" | head -5)

if echo "$OUTPUT" | grep -q "Output from late reverb: YES"; then
    echo -e "\n✅ SUCCESS: Room reverb is now producing output!"
else
    echo -e "\n❌ FAILED: Room reverb still not producing output"
    echo "$OUTPUT"
fi

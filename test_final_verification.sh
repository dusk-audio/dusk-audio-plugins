#!/bin/bash

echo "====================================="
echo "StudioReverb Room Algorithm Final Test"
echo "====================================="
echo ""
echo "This test will verify the Room reverb Late Level is working correctly"
echo ""

# Path to standalone plugin
PLUGIN="./build/plugins/StudioReverb/StudioReverb_artefacts/Release/Standalone/StudioReverb"

if [ ! -f "$PLUGIN" ]; then
    echo "Error: Standalone plugin not found. Please build first."
    exit 1
fi

echo "Test Instructions:"
echo "1. The plugin will open in standalone mode"
echo "2. Select 'Room' from the reverb type dropdown"
echo "3. Set parameters as follows:"
echo "   - Dry Level: 0% (all the way down)"
echo "   - Early Level: 0% (all the way down)"
echo "   - Late Level: 100% (all the way up)"
echo ""
echo "4. Play audio through the plugin (use the audio input routing in the plugin)"
echo "5. You should hear ONLY the reverb tail (no dry signal, no early reflections)"
echo ""
echo "Expected behavior:"
echo "- With Late Level at 100%, you should hear clear reverb"
echo "- The reverb should sound natural, not overly loud or distorted"
echo "- Adjusting Late Level should smoothly control the reverb amount"
echo ""
echo "Starting plugin now..."
echo ""

# Run the plugin
"$PLUGIN"

echo ""
echo "Test complete!"
echo ""
echo "If the Late Level produced no output, check the terminal for debug messages."
echo "Look for lines like:"
echo "  'ROOM REVERB DEBUG'"
echo "  'Input to late reverb: YES/NO'"
echo "  'Output from late reverb: YES/NO'"
#!/bin/bash
# Test script for Dragonfly Unified Reverb plugin

echo "Testing Dragonfly Unified Reverb VST3 plugin..."
echo "Plugin location: ~/.vst3/Dragonfly Unified Reverb.vst3"

# Check if plugin file exists
if [ -d ~/.vst3/"Dragonfly Unified Reverb.vst3" ]; then
    echo "✓ VST3 plugin found"
    ls -lh ~/.vst3/"Dragonfly Unified Reverb.vst3"/Contents/x86_64-linux/*.so
else
    echo "✗ VST3 plugin not found"
    exit 1
fi

echo ""
echo "Plugin successfully built and installed!"
echo "You can now test it in your DAW (Reaper, Carla, etc.)"
echo ""
echo "Available reverb types:"
echo "  - Room Reverb"
echo "  - Hall Reverb"
echo "  - Plate Reverb"
echo "  - Early Reflections"
echo "  - Chamber (placeholder for future)"
echo "  - Spring (placeholder for future)"
echo "  - Shimmer (placeholder for future)"

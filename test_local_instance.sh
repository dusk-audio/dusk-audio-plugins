#!/bin/bash
timeout 2 ./build/plugins/StudioReverb/StudioReverb_artefacts/Release/Standalone/StudioReverb 2>&1 | while read line; do
    echo "$line"
    if [[ "$line" == *"Local instance"* ]]; then
        echo "$line"
        sleep 0.1
        pkill -f StudioReverb
        break
    fi
done | head -200

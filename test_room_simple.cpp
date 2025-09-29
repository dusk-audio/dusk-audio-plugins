#include <iostream>
#include <cmath>
#include <cstring>

// Simple test that directly tests the progenitor2 processor
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"
#include "plugins/StudioReverb/Source/freeverb/fv3_defs.h"

int main() {
    std::cout << "\n=== DIRECT PROGENITOR2 TEST ===" << std::endl;

    fv3::progenitor2_f room;
    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    // Initialize exactly like RoomReverb
    room.setdryr(0.0f);
    room.setwetr(1.0f);
    room.setMuteOnChange(false);
    room.setReverbType(2); // Magic value from RoomReverb
    room.setSampleRate(sampleRate);

    // Set parameters
    room.setRSFactor(3.0f);
    room.setrt60(2.0f);

    // Create test buffers
    float* inputL = new float[blockSize];
    float* inputR = new float[blockSize];
    float* outputL = new float[blockSize];
    float* outputR = new float[blockSize];

    // Clear buffers
    std::memset(inputL, 0, blockSize * sizeof(float));
    std::memset(inputR, 0, blockSize * sizeof(float));

    // Add impulse
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;

    std::cout << "Processing impulse..." << std::endl;

    // Process
    room.processreplace(inputL, inputR, outputL, outputR, blockSize);

    // Check output
    float maxOutput = 0.0f;
    for (int i = 0; i < blockSize; ++i) {
        float mag = std::abs(outputL[i]) + std::abs(outputR[i]);
        if (mag > maxOutput) maxOutput = mag;
        if (mag > 0.0001f && i < 10) {
            std::cout << "  Sample " << i << ": L=" << outputL[i] 
                     << ", R=" << outputR[i] << std::endl;
        }
    }

    std::cout << "\nMax output: " << maxOutput << std::endl;

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    if (maxOutput > 0.001f) {
        std::cout << "✓ PASS: Progenitor2 produces output with setReverbType(2)" << std::endl;
        return 0;
    } else {
        std::cout << "✗ FAIL: No output from progenitor2!" << std::endl;
        return 1;
    }
}

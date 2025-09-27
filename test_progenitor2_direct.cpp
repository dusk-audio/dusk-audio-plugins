#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>

// Include the freeverb headers
#define FV3_FLOAT_SUPPORT 1
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

int main() {
    std::cout << "\n=== Direct test of progenitor2 with Dragonfly's library ===" << std::endl;

    const int sampleRate = 48000;
    const int bufferSize = 512;

    // Create Room reverb instance
    fv3::progenitor2_f room;

    // Initialize EXACTLY like Dragonfly
    room.setSampleRate(sampleRate);
    room.setMuteOnChange(false);
    room.setwet(0);    // 0dB
    room.setdryr(0);   // mute dry
    room.setwidth(1.0);

    // Set some basic parameters
    room.setRSFactor(3.0f);
    room.setrt60(2.0f);
    room.setidiffusion1(0.75f);
    room.setodiffusion1(0.75f);

    // Create buffers
    std::vector<float> inputL(bufferSize, 0.0f);
    std::vector<float> inputR(bufferSize, 0.0f);
    std::vector<float> outputL(bufferSize, 0.0f);
    std::vector<float> outputR(bufferSize, 0.0f);

    // Create impulse
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;

    std::cout << "Input impulse: L=" << inputL[0] << " R=" << inputR[0] << std::endl;

    // Process - pass pointers to the vector data
    room.processreplace(
        inputL.data(),
        inputR.data(),
        outputL.data(),
        outputR.data(),
        bufferSize
    );

    // Check output
    float maxOut = 0.0f;
    for (int i = 0; i < bufferSize; ++i) {
        maxOut = std::max(maxOut, std::abs(outputL[i]));
        maxOut = std::max(maxOut, std::abs(outputR[i]));
    }

    std::cout << "\nMax output magnitude: " << maxOut << std::endl;

    // Show first 10 samples
    std::cout << "\nFirst 10 output samples (L channel):" << std::endl;
    for (int i = 0; i < 10; ++i) {
        std::cout << "  [" << i << "]: " << outputL[i] << std::endl;
    }

    // Process multiple times to see if reverb builds up
    std::cout << "\n--- Processing 10 more blocks to check reverb tail ---" << std::endl;
    for (int block = 0; block < 10; ++block) {
        // Clear input (no more impulses)
        std::fill(inputL.begin(), inputL.end(), 0.0f);
        std::fill(inputR.begin(), inputR.end(), 0.0f);

        room.processreplace(
            inputL.data(),
            inputR.data(),
            outputL.data(),
            outputR.data(),
            bufferSize
        );

        float blockMax = 0.0f;
        for (int i = 0; i < bufferSize; ++i) {
            blockMax = std::max(blockMax, std::abs(outputL[i]));
        }
        std::cout << "Block " << block << " max output: " << blockMax << std::endl;
    }

    if (maxOut > 0.001f) {
        std::cout << "\n✓ progenitor2 is producing output!" << std::endl;
    } else {
        std::cout << "\n✗ progenitor2 is NOT working - no reverb output" << std::endl;
    }

    return 0;
}
#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>

// Include the freeverb headers
#define FV3_FLOAT_SUPPORT 1
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

int main() {
    std::cout << "\n=== Testing if progenitor2 needs priming ===" << std::endl;

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

    std::cout << "\n1. PRIMING: Processing 10 blocks of silence first..." << std::endl;
    for (int i = 0; i < 10; ++i) {
        room.processreplace(
            inputL.data(),
            inputR.data(),
            outputL.data(),
            outputR.data(),
            bufferSize
        );
    }

    std::cout << "\n2. Now sending impulse..." << std::endl;
    // Create impulse
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;

    room.processreplace(
        inputL.data(),
        inputR.data(),
        outputL.data(),
        outputR.data(),
        bufferSize
    );

    // Check output IN THE SAME BLOCK
    float maxOut = 0.0f;
    for (int i = 0; i < bufferSize; ++i) {
        maxOut = std::max(maxOut, std::abs(outputL[i]));
        maxOut = std::max(maxOut, std::abs(outputR[i]));
    }

    std::cout << "Max output in impulse block: " << maxOut << std::endl;

    // Show first samples
    std::cout << "\nFirst 20 output samples after impulse (L channel):" << std::endl;
    for (int i = 0; i < 20; ++i) {
        if (std::abs(outputL[i]) > 0.0001f) {
            std::cout << "  [" << i << "]: " << outputL[i] << " *** NON-ZERO ***" << std::endl;
        } else {
            std::cout << "  [" << i << "]: " << outputL[i] << std::endl;
        }
    }

    // Clear input and process more blocks
    std::cout << "\n3. Processing subsequent blocks (no input)..." << std::endl;
    inputL[0] = 0.0f;
    inputR[0] = 0.0f;

    for (int block = 0; block < 5; ++block) {
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

    std::cout << "\n=== TESTING WITH CONTINUOUS SIGNAL ===" << std::endl;

    // Reset and try with continuous signal
    room.mute();

    // Send continuous signal
    for (int i = 0; i < bufferSize; ++i) {
        inputL[i] = 0.1f * sin(2.0f * M_PI * 440.0f * i / sampleRate);
        inputR[i] = inputL[i];
    }

    std::cout << "Processing 5 blocks with 440Hz sine wave..." << std::endl;
    for (int block = 0; block < 5; ++block) {
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
        std::cout << "Block " << block << " with sine input, max output: " << blockMax << std::endl;
    }

    return 0;
}
#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>

// Include the freeverb headers
#define FV3_FLOAT_SUPPORT 1
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

int main() {
    std::cout << "\n=== Testing Room reverb with gain compensation ===" << std::endl;

    const int sampleRate = 48000;
    const int bufferSize = 512;

    // Create Room reverb instance
    fv3::progenitor2_f room;

    // Initialize
    room.setSampleRate(sampleRate);
    room.setMuteOnChange(false);
    room.setwet(0);    // 0dB
    room.setdryr(0);   // mute dry
    room.setwidth(1.0);
    room.setRSFactor(3.0f);
    room.setrt60(2.0f);

    // Create buffers
    std::vector<float> inputL(bufferSize, 0.0f);
    std::vector<float> inputR(bufferSize, 0.0f);
    std::vector<float> outputL(bufferSize, 0.0f);
    std::vector<float> outputR(bufferSize, 0.0f);

    // Create impulse
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;

    // Process
    room.processreplace(
        inputL.data(),
        inputR.data(),
        outputL.data(),
        outputR.data(),
        bufferSize
    );

    // Apply gain compensation
    const float roomLateGain = 250.0f;  // 48dB gain

    float maxRaw = 0.0f;
    float maxCompensated = 0.0f;
    for (int i = 0; i < bufferSize; ++i) {
        maxRaw = std::max(maxRaw, std::abs(outputL[i]));
        outputL[i] *= roomLateGain;
        outputR[i] *= roomLateGain;
        maxCompensated = std::max(maxCompensated, std::abs(outputL[i]));
    }

    std::cout << "\nRaw output max: " << maxRaw << " (" << 20*log10(maxRaw) << " dB)" << std::endl;
    std::cout << "Compensated output max: " << maxCompensated << " (" << 20*log10(maxCompensated) << " dB)" << std::endl;

    // Process more blocks to see reverb tail
    std::cout << "\nReverb tail (with gain compensation):" << std::endl;
    for (int block = 0; block < 5; ++block) {
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
            outputL[i] *= roomLateGain;
            outputR[i] *= roomLateGain;
            blockMax = std::max(blockMax, std::abs(outputL[i]));
        }
        std::cout << "  Block " << block << ": " << blockMax << " (" << 20*log10(blockMax) << " dB)" << std::endl;
    }

    if (maxCompensated > 0.1f) {
        std::cout << "\n✓ Room reverb with gain compensation is at usable level!" << std::endl;
    } else {
        std::cout << "\n✗ Still too quiet even with compensation" << std::endl;
    }

    return 0;
}
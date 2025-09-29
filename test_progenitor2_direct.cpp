// Direct test of progenitor2 outside JUCE
#include <iostream>
#include <cmath>
#include <cstring>

// Define the float type before including headers
#define LIBFV3_FLOAT

// Include freeverb headers
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

int main() {
    const float sampleRate = 48000.0f;
    const int blockSize = 512;

    std::cout << "Creating progenitor2_f instance..." << std::endl;

    // Create progenitor2 instance (using _f suffix for float version)
    fv3::progenitor2_f reverb;

    // Initialize like Dragonfly
    std::cout << "Initializing with sample rate " << sampleRate << "..." << std::endl;
    reverb.setSampleRate(sampleRate);
    reverb.setMuteOnChange(false);

    // Set parameters to match our JUCE plugin
    reverb.setwet(0);     // 0dB wet
    reverb.setdryr(0);    // 0dB dry
    reverb.setwidth(0.9f);
    reverb.setrt60(0.7f);
    reverb.setidiffusion1(0.75f);
    reverb.setodiffusion1(0.625f);

    std::cout << "Parameters set: wet=" << reverb.getwet()
              << ", dry=" << reverb.getdryr()
              << ", rt60=" << reverb.getrt60() << std::endl;

    // Create buffers
    float* leftIn = new float[blockSize];
    float* rightIn = new float[blockSize];
    float* leftOut = new float[blockSize];
    float* rightOut = new float[blockSize];

    // Generate impulse test signal
    std::memset(leftIn, 0, blockSize * sizeof(float));
    std::memset(rightIn, 0, blockSize * sizeof(float));
    leftIn[0] = 1.0f;   // Impulse
    rightIn[0] = 1.0f;

    std::cout << "\nProcessing impulse response..." << std::endl;

    // Process multiple blocks to capture reverb tail
    for (int block = 0; block < 5; block++) {
        // Copy input to output for processreplace
        if (block == 0) {
            std::memcpy(leftOut, leftIn, blockSize * sizeof(float));
            std::memcpy(rightOut, rightIn, blockSize * sizeof(float));
        } else {
            // Silence for subsequent blocks
            std::memset(leftOut, 0, blockSize * sizeof(float));
            std::memset(rightOut, 0, blockSize * sizeof(float));
        }

        // Process audio
        reverb.processreplace(leftOut, rightOut, leftOut, rightOut, blockSize);

        // Analyze output
        float maxL = 0, maxR = 0;
        float energyL = 0, energyR = 0;
        int nonZeroCount = 0;

        for (int i = 0; i < blockSize; i++) {
            float absL = std::abs(leftOut[i]);
            float absR = std::abs(rightOut[i]);

            if (absL > maxL) maxL = absL;
            if (absR > maxR) maxR = absR;

            energyL += absL;
            energyR += absR;

            if (absL > 0.00001f || absR > 0.00001f) {
                nonZeroCount++;
            }
        }

        std::cout << "Block " << block << ": ";
        std::cout << "Max[L=" << maxL << ", R=" << maxR << "] ";
        std::cout << "Energy[L=" << energyL << ", R=" << energyR << "] ";
        std::cout << "NonZero=" << nonZeroCount;

        // Show first few samples if there's output
        if (maxL > 0.0001f) {
            std::cout << " First samples: [";
            for (int i = 0; i < 5; i++) {
                std::cout << leftOut[i];
                if (i < 4) std::cout << ", ";
            }
            std::cout << "]";
        }

        std::cout << std::endl;

        // Clear input after first block
        if (block == 0) {
            std::memset(leftIn, 0, blockSize * sizeof(float));
            std::memset(rightIn, 0, blockSize * sizeof(float));
        }
    }

    // Test with pure wet signal
    std::cout << "\nTrying pure wet signal (wet=1.0 linear, dry muted)..." << std::endl;
    reverb.setwetr(1.0f);    // Linear wet level
    reverb.setdryr(-96.0f);  // Mute dry

    // Reset impulse
    leftIn[0] = 1.0f;
    rightIn[0] = 1.0f;

    std::memcpy(leftOut, leftIn, blockSize * sizeof(float));
    std::memcpy(rightOut, rightIn, blockSize * sizeof(float));

    reverb.processreplace(leftOut, rightOut, leftOut, rightOut, blockSize);

    float maxL = 0, maxR = 0;
    for (int i = 0; i < blockSize; i++) {
        float absL = std::abs(leftOut[i]);
        float absR = std::abs(rightOut[i]);
        if (absL > maxL) maxL = absL;
        if (absR > maxR) maxR = absR;
    }

    std::cout << "Output max: L=" << maxL << ", R=" << maxR << std::endl;

    if (maxL < 0.0001f && maxR < 0.0001f) {
        std::cout << "\n*** PROBLEM CONFIRMED: progenitor2 produces no output! ***" << std::endl;
        std::cout << "Even with impulse input and wet signal, output is silent." << std::endl;
    } else {
        std::cout << "\n*** SUCCESS: progenitor2 is working! ***" << std::endl;
        std::cout << "The issue must be in the JUCE integration." << std::endl;
    }

    // Cleanup
    delete[] leftIn;
    delete[] rightIn;
    delete[] leftOut;
    delete[] rightOut;

    return 0;
}

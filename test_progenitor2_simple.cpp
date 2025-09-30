// Minimal test for progenitor2 outside of JUCE
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>

// Add the include path for freeverb headers
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

int main() {
    const int sampleRate = 48000;
    const int blockSize = 512;

    // Create progenitor2 instance
    fv3::progenitor2_f reverb;

    // Initialize exactly like Dragonfly does
    std::cout << "Initializing progenitor2..." << std::endl;
    reverb.setSampleRate(sampleRate);
    reverb.setMuteOnChange(false);

    // Set parameters
    reverb.setwet(0);  // 0dB wet signal
    reverb.setdryr(0);  // 0dB dry
    reverb.setwidth(0.9f);
    reverb.setrt60(0.7f);
    reverb.setidiffusion1(0.75f);
    reverb.setodiffusion1(0.625f);

    // Create test signal
    float* inputL = new float[blockSize];
    float* inputR = new float[blockSize];
    float* outputL = new float[blockSize];
    float* outputR = new float[blockSize];

    // Fill with impulse
    std::memset(inputL, 0, blockSize * sizeof(float));
    std::memset(inputR, 0, blockSize * sizeof(float));
    inputL[0] = 1.0f;  // Impulse
    inputR[0] = 1.0f;

    std::cout << "Testing progenitor2 with impulse..." << std::endl;
    std::cout << "Sample rate: " << sampleRate << std::endl;
    std::cout << "Block size: " << blockSize << std::endl;
    std::cout << "wet=" << reverb.getwet() << ", dry=" << reverb.getdryr()
              << ", rt60=" << reverb.getrt60() << std::endl;

    // Process several blocks
    for (int block = 0; block < 5; block++) {
        // Copy input to output for processreplace
        if (block == 0) {
            std::memcpy(outputL, inputL, blockSize * sizeof(float));
            std::memcpy(outputR, inputR, blockSize * sizeof(float));
        } else {
            std::memset(outputL, 0, blockSize * sizeof(float));
            std::memset(outputR, 0, blockSize * sizeof(float));
        }

        // Process
        reverb.processreplace(outputL, outputR, outputL, outputR, blockSize);

        // Check output
        float maxL = 0, maxR = 0;
        float sumL = 0, sumR = 0;
        for (int i = 0; i < blockSize; i++) {
            float absL = std::abs(outputL[i]);
            float absR = std::abs(outputR[i]);
            if (absL > maxL) maxL = absL;
            if (absR > maxR) maxR = absR;
            sumL += absL;
            sumR += absR;
        }

        std::cout << "Block " << block << ": ";
        std::cout << "Max[" << maxL << "," << maxR << "] ";
        std::cout << "Sum[" << sumL << "," << sumR << "]";

        // Show first few samples
        if (maxL > 0.0001f) {
            std::cout << " First: [";
            for (int i = 0; i < 5; i++) {
                std::cout << outputL[i];
                if (i < 4) std::cout << ",";
            }
            std::cout << "]";
        }
        std::cout << std::endl;

        // Clear input for next blocks
        if (block == 0) {
            std::memset(inputL, 0, blockSize * sizeof(float));
            std::memset(inputR, 0, blockSize * sizeof(float));
        }
    }

    // Try with different wet/dry settings
    std::cout << "\nTrying with wet=1.0 (linear), dry=-96dB..." << std::endl;
    reverb.setwetr(1.0f);  // Linear wet level
    reverb.setdryr(-96.0f); // Mute dry

    // Reset impulse
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;

    std::memcpy(outputL, inputL, blockSize * sizeof(float));
    std::memcpy(outputR, inputR, blockSize * sizeof(float));
    reverb.processreplace(outputL, outputR, outputL, outputR, blockSize);

    float maxL = 0, maxR = 0;
    for (int i = 0; i < blockSize; i++) {
        if (std::abs(outputL[i]) > maxL) maxL = std::abs(outputL[i]);
        if (std::abs(outputR[i]) > maxR) maxR = std::abs(outputR[i]);
    }
    std::cout << "Output max: [" << maxL << "," << maxR << "]" << std::endl;

    // Cleanup
    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}
// Minimal standalone test for progenitor2 - testing outside of JUCE
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>

// Include progenitor2 directly
#include "plugins/StudioReverb/Source/freeverb/progenitor2.cpp"
#include "plugins/StudioReverb/Source/freeverb/revbase.cpp"
#include "plugins/StudioReverb/Source/freeverb/utils.cpp"
#include "plugins/StudioReverb/Source/freeverb/delay.cpp"
#include "plugins/StudioReverb/Source/freeverb/comb.cpp"
#include "plugins/StudioReverb/Source/freeverb/allpass.cpp"
#include "plugins/StudioReverb/Source/freeverb/biquad.cpp"
#include "plugins/StudioReverb/Source/freeverb/efilter.cpp"
#include "plugins/StudioReverb/Source/freeverb/delayline.cpp"
#include "plugins/StudioReverb/Source/freeverb/slot.cpp"
#include "plugins/StudioReverb/Source/freeverb/earlyref.cpp"

int main() {
    const int sampleRate = 48000;
    const int blockSize = 512;

    // Create progenitor2 instance
    fv3::progenitor2_f reverb;

    // Initialize exactly like Dragonfly does
    reverb.setSampleRate(sampleRate);
    reverb.setMuteOnChange(false);

    // Set reverb type first - Dragonfly uses setReverbType
    reverb.setReverbType(fv3::FV3_REVTYPE_SELF);  // 0

    // Set parameters like Dragonfly
    reverb.setwet(0);  // 0dB wet signal
    reverb.setdryr(0);  // 0dB dry
    reverb.setwidth(0.9f);
    reverb.setrt60(0.7f);
    reverb.setidiffusion1(0.75f);
    reverb.setodiffusion1(0.625f);

    // Create test signal - a short impulse followed by silence
    float** inputL = new float*[1];
    float** inputR = new float*[1];
    float** outputL = new float*[1];
    float** outputR = new float*[1];

    inputL[0] = new float[blockSize];
    inputR[0] = new float[blockSize];
    outputL[0] = new float[blockSize];
    outputR[0] = new float[blockSize];

    // Fill with test signal - impulse at the beginning
    std::memset(inputL[0], 0, blockSize * sizeof(float));
    std::memset(inputR[0], 0, blockSize * sizeof(float));
    inputL[0][0] = 1.0f;  // Impulse
    inputR[0][0] = 1.0f;

    std::cout << "Testing progenitor2 standalone..." << std::endl;
    std::cout << "Sample rate: " << sampleRate << std::endl;
    std::cout << "Block size: " << blockSize << std::endl;

    // Process several blocks to see if we get reverb tail
    for (int block = 0; block < 10; block++) {
        // Clear output buffers
        std::memset(outputL[0], 0, blockSize * sizeof(float));
        std::memset(outputR[0], 0, blockSize * sizeof(float));

        // Copy input to output first (for processreplace)
        if (block == 0) {
            std::memcpy(outputL[0], inputL[0], blockSize * sizeof(float));
            std::memcpy(outputR[0], inputR[0], blockSize * sizeof(float));
        }

        // Process
        reverb.processreplace(outputL[0], outputR[0], outputL[0], outputR[0], blockSize);

        // Check for non-zero output
        float maxL = 0, maxR = 0;
        for (int i = 0; i < blockSize; i++) {
            if (std::abs(outputL[0][i]) > maxL) maxL = std::abs(outputL[0][i]);
            if (std::abs(outputR[0][i]) > maxR) maxR = std::abs(outputR[0][i]);
        }

        std::cout << "Block " << block << " - Max L: " << maxL << ", Max R: " << maxR;

        // Show first few samples if non-zero
        if (maxL > 0.0001f || maxR > 0.0001f) {
            std::cout << " - First samples: L[";
            for (int i = 0; i < 5 && i < blockSize; i++) {
                std::cout << outputL[0][i] << " ";
            }
            std::cout << "]";
        }
        std::cout << std::endl;

        // Use silence for subsequent blocks
        if (block == 0) {
            std::memset(inputL[0], 0, blockSize * sizeof(float));
            std::memset(inputR[0], 0, blockSize * sizeof(float));
        }
    }

    // Test with different setReverbType
    std::cout << "\nTesting with setReverbType(31)..." << std::endl;
    reverb.setReverbType(31);  // FV3_REVTYPE_PROG2

    // Reset with impulse
    inputL[0][0] = 1.0f;
    inputR[0][0] = 1.0f;

    for (int block = 0; block < 3; block++) {
        std::memset(outputL[0], 0, blockSize * sizeof(float));
        std::memset(outputR[0], 0, blockSize * sizeof(float));

        if (block == 0) {
            std::memcpy(outputL[0], inputL[0], blockSize * sizeof(float));
            std::memcpy(outputR[0], inputR[0], blockSize * sizeof(float));
        }

        reverb.processreplace(outputL[0], outputR[0], outputL[0], outputR[0], blockSize);

        float maxL = 0, maxR = 0;
        for (int i = 0; i < blockSize; i++) {
            if (std::abs(outputL[0][i]) > maxL) maxL = std::abs(outputL[0][i]);
            if (std::abs(outputR[0][i]) > maxR) maxR = std::abs(outputR[0][i]);
        }

        std::cout << "Block " << block << " - Max L: " << maxL << ", Max R: " << maxR << std::endl;

        if (block == 0) {
            std::memset(inputL[0], 0, blockSize * sizeof(float));
            std::memset(inputR[0], 0, blockSize * sizeof(float));
        }
    }

    // Cleanup
    delete[] inputL[0];
    delete[] inputR[0];
    delete[] outputL[0];
    delete[] outputR[0];
    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}
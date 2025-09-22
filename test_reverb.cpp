#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include "plugins/Studio480/DSP/ReverbCore.h"

int main() {
    std::cout << "Testing Studio480 Reverb DSP..." << std::endl;

    ReverbCore reverb;
    const double sampleRate = 44100.0;
    const int blockSize = 512;
    const int testDuration = 2; // seconds
    const int totalSamples = sampleRate * testDuration;

    // Prepare reverb
    reverb.prepare(sampleRate, blockSize);
    reverb.setAlgorithm(ReverbCore::Hall);
    reverb.setMix(1.0f);    // 100% wet for testing
    reverb.setDecay(0.8f);  // High decay
    reverb.setSize(0.7f);   // Large room
    reverb.setDamping(0.3f);
    reverb.setDiffusion(0.8f);
    reverb.setWidth(1.0f);

    std::cout << "Reverb configured: Mix=1.0, Decay=0.8, Size=0.7" << std::endl;

    // Create test signal (impulse followed by silence to hear the tail)
    std::vector<float> inputL(totalSamples, 0.0f);
    std::vector<float> inputR(totalSamples, 0.0f);
    std::vector<float> outputL(totalSamples, 0.0f);
    std::vector<float> outputR(totalSamples, 0.0f);

    // Add a few impulses
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;
    inputL[sampleRate/4] = 0.5f;  // Another at 250ms
    inputR[sampleRate/4] = 0.5f;
    inputL[sampleRate/2] = 0.3f;  // Another at 500ms
    inputR[sampleRate/2] = 0.3f;

    // Process in blocks
    for (int i = 0; i < totalSamples; i += blockSize) {
        int samplesToProcess = std::min(blockSize, totalSamples - i);
        reverb.processBlock(&inputL[i], &inputR[i], &outputL[i], &outputR[i], samplesToProcess);
    }

    // Check if we got any output
    float maxOutput = 0.0f;
    float sumOutput = 0.0f;
    int nonZeroSamples = 0;

    for (int i = 0; i < totalSamples; ++i) {
        float absL = std::abs(outputL[i]);
        float absR = std::abs(outputR[i]);
        maxOutput = std::max(maxOutput, std::max(absL, absR));
        sumOutput += absL + absR;
        if (absL > 0.0001f || absR > 0.0001f) {
            nonZeroSamples++;
        }
    }

    std::cout << "\nResults:" << std::endl;
    std::cout << "Max output level: " << maxOutput << std::endl;
    std::cout << "Average output level: " << (sumOutput / (totalSamples * 2)) << std::endl;
    std::cout << "Non-zero samples: " << nonZeroSamples << " / " << totalSamples << std::endl;

    // Write first 1000 samples to see what's happening
    std::cout << "\nFirst 20 output samples (L channel):" << std::endl;
    for (int i = 0; i < 20; ++i) {
        std::cout << "Sample " << i << ": input=" << inputL[i] << " output=" << outputL[i] << std::endl;
    }

    // Save to WAV file for listening
    std::ofstream outFile("reverb_test.raw", std::ios::binary);
    if (outFile.is_open()) {
        for (int i = 0; i < totalSamples; ++i) {
            float sample = outputL[i];
            outFile.write(reinterpret_cast<char*>(&sample), sizeof(float));
        }
        outFile.close();
        std::cout << "\nWrote output to reverb_test.raw (32-bit float, 44100Hz, mono)" << std::endl;
        std::cout << "Convert with: sox -r 44100 -e float -b 32 reverb_test.raw reverb_test.wav" << std::endl;
    }

    if (maxOutput < 0.001f) {
        std::cout << "\n*** WARNING: No significant output detected! ***" << std::endl;
        std::cout << "The reverb may not be processing correctly." << std::endl;
        return 1;
    } else {
        std::cout << "\n*** Success: Reverb is producing output! ***" << std::endl;
        return 0;
    }
}
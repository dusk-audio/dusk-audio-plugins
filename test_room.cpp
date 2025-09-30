#include <iostream>
#include <cmath>
#include <vector>
#include "plugins/StudioReverb/Source/DSP/DragonflyReverb.h"

int main() {
    std::cout << "\n=== ROOM REVERB LATE LEVEL TEST ===" << std::endl;

    DragonflyReverb reverb;
    const double sampleRate = 48000.0;
    const int blockSize = 512;
    const int numBlocks = 20;  // Process ~200ms of audio

    // Initialize
    reverb.prepare(sampleRate, blockSize);
    reverb.setAlgorithm(DragonflyReverb::Algorithm::Room);

    // Configure for LATE ONLY output to isolate the issue
    reverb.setDryLevel(0.0f);      // No dry
    reverb.setEarlyLevel(0.0f);    // No early
    reverb.setEarlySend(0.0f);     // No early->late send
    reverb.setLateLevel(1.0f);     // Full late reverb

    // Set typical reverb parameters
    reverb.setSize(30.0f);
    reverb.setDecay(2.0f);
    reverb.setHighCut(16000.0f);

    // Create audio buffer
    juce::AudioBuffer<float> buffer(2, blockSize);

    float maxOutput = 0.0f;
    float totalEnergy = 0.0f;
    bool foundOutput = false;

    for (int block = 0; block < numBlocks; ++block) {
        buffer.clear();

        // Add impulse on first block
        if (block == 0) {
            buffer.setSample(0, 0, 1.0f);  // Left impulse
            buffer.setSample(1, 0, 1.0f);  // Right impulse
            std::cout << "Injecting impulse at block 0, sample 0" << std::endl;
        }

        // Process
        reverb.processBlock(buffer);

        // Analyze output
        for (int i = 0; i < blockSize; ++i) {
            float L = buffer.getSample(0, i);
            float R = buffer.getSample(1, i);
            float mag = std::abs(L) + std::abs(R);

            maxOutput = std::max(maxOutput, mag);
            totalEnergy += L*L + R*R;

            if (mag > 0.0001f && !foundOutput) {
                foundOutput = true;
                std::cout << "First output at block " << block << ", sample " << i
                         << " (time: " << ((block * blockSize + i) / sampleRate * 1000) << " ms)"
                         << " - L=" << L << ", R=" << R << std::endl;
            }
        }
    }

    std::cout << "\n=== RESULTS ===" << std::endl;
    std::cout << "Max output: " << maxOutput;
    if (maxOutput > 0.0f) {
        std::cout << " (" << 20*log10(maxOutput) << " dB)";
    }
    std::cout << std::endl;
    std::cout << "Total energy: " << totalEnergy << std::endl;

    if (maxOutput > 0.001f) {
        std::cout << "\n✓ PASS: Room reverb Late Level is working!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ FAIL: Room reverb Late Level produces NO output!" << std::endl;
        return 1;
    }
}

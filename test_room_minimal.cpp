#include <iostream>
#include <cmath>
#include <cstring>

// Include the actual plugin's reverb code
#include "plugins/StudioReverb/Source/DSP/DragonflyReverb.cpp"

int main() {
    std::cout << "\n=== Testing Room Reverb Directly ===" << std::endl;

    // Create the reverb processor
    DragonflyReverb reverb;

    // Set to Room algorithm
    reverb.setAlgorithm(DragonflyReverb::Algorithm::Room);

    // Prepare the processor
    const double sampleRate = 48000.0;
    const int blockSize = 512;
    reverb.prepare(sampleRate, blockSize);

    // Set parameters
    reverb.setDryLevel(0.8f);    // 80% dry
    reverb.setEarlyLevel(0.3f);  // 30% early
    reverb.setLateLevel(0.5f);   // 50% late
    reverb.setSize(30.0f);       // Medium size
    reverb.setDecay(2.0f);       // 2 second decay

    // Create test buffer with impulse
    juce::AudioBuffer<float> buffer(2, blockSize);
    buffer.clear();

    // Add impulse to both channels
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);

    // Process the buffer based on algorithm
    reverb.processRoom(buffer);

    // Calculate energy
    float energy = 0.0f;
    for (int ch = 0; ch < 2; ++ch) {
        const float* channelData = buffer.getReadPointer(ch);
        for (int i = 0; i < blockSize; ++i) {
            energy += channelData[i] * channelData[i];
        }
    }
    energy = std::sqrt(energy / (2 * blockSize));

    std::cout << "Output energy: " << energy << std::endl;

    // Check specific samples
    std::cout << "\nFirst 10 output samples (L channel):" << std::endl;
    const float* leftChannel = buffer.getReadPointer(0);
    for (int i = 0; i < 10; ++i) {
        std::cout << "  Sample[" << i << "]: " << leftChannel[i] << std::endl;
    }

    if (energy > 0.01f) {
        std::cout << "\n✓ Room reverb is working!" << std::endl;
    } else {
        std::cout << "\n✗ Room reverb is NOT producing output" << std::endl;
    }

    return 0;
}
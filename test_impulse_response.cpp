// Test progenitor2 impulse response to verify reverb tail
#include <iostream>
#include <cstring>
#include <cmath>
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"

int main() {
    std::cout << "\n=== PROGENITOR2 IMPULSE RESPONSE TEST ===" << std::endl;

    fv3::progenitor2_f room;
    const float sampleRate = 48000.0f;
    const int totalSamples = 48000; // 1 second

    // Initialize exactly like we do in the plugin
    room.setdryr(-90.0f);  // Mute dry
    room.setwetr(1.0f);    // Unity wet
    room.setReverbType(2); // Magic value
    room.setSampleRate(sampleRate);
    room.setRSFactor(3.0f);
    room.setrt60(2.0f);
    room.setMuteOnChange(false);

    // Create buffers
    float* inputL = new float[totalSamples];
    float* inputR = new float[totalSamples];
    float* outputL = new float[totalSamples];
    float* outputR = new float[totalSamples];

    // Clear all
    std::memset(inputL, 0, totalSamples * sizeof(float));
    std::memset(inputR, 0, totalSamples * sizeof(float));

    // Single impulse at start
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;

    // Process in blocks
    const int blockSize = 512;
    for (int offset = 0; offset < totalSamples; offset += blockSize) {
        int samples = std::min(blockSize, totalSamples - offset);
        room.processreplace(
            inputL + offset, inputR + offset,
            outputL + offset, outputR + offset,
            samples
        );
    }

    // Analyze the impulse response
    std::cout << "\nImpulse Response Analysis:" << std::endl;
    std::cout << "Time (ms) | Output Level | Energy" << std::endl;
    std::cout << "----------|--------------|--------" << std::endl;

    float totalEnergy = 0.0f;
    int lastNonZero = -1;

    // Check at various time points
    int checkPoints[] = {0, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 40000};

    for (int i = 0; i < 13; i++) {
        int sample = checkPoints[i];
        if (sample >= totalSamples) continue;

        float level = std::abs(outputL[sample]) + std::abs(outputR[sample]);
        float energy = outputL[sample] * outputL[sample] + outputR[sample] * outputR[sample];
        totalEnergy += energy;

        if (level > 0.00001f) {
            lastNonZero = sample;
        }

        float timeMs = (float)sample / sampleRate * 1000.0f;
        std::cout << std::fixed;
        std::cout.precision(1);
        std::cout << timeMs << " ms";
        std::cout.precision(6);
        std::cout << " | " << level << " | " << energy << std::endl;
    }

    // Find peak
    float peak = 0.0f;
    int peakSample = 0;
    for (int i = 0; i < totalSamples; i++) {
        float level = std::abs(outputL[i]) + std::abs(outputR[i]);
        if (level > peak) {
            peak = level;
            peakSample = i;
        }
    }

    std::cout << "\n=== RESULTS ===" << std::endl;
    std::cout << "Peak: " << peak << " at " << (peakSample / 48.0f) << " ms" << std::endl;
    std::cout << "Last non-zero: " << (lastNonZero / 48.0f) << " ms" << std::endl;
    std::cout << "Total energy: " << totalEnergy << std::endl;

    // Check if this looks like reverb
    bool hasDecay = (lastNonZero > 1000); // Should last > 20ms
    bool hasPeak = (peak > 0.001f);
    bool hasEnergy = (totalEnergy > 0.0001f);

    if (hasDecay && hasPeak && hasEnergy) {
        std::cout << "\n✓ This looks like REVERB (has decay tail)" << std::endl;
    } else {
        std::cout << "\n✗ This does NOT look like reverb!" << std::endl;
        if (!hasDecay) std::cout << "  - No decay tail" << std::endl;
        if (!hasPeak) std::cout << "  - Peak too low" << std::endl;
        if (!hasEnergy) std::cout << "  - No energy" << std::endl;
    }

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}
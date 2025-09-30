// Minimal test for progenitor2 reverb
#include <iostream>
#include <cmath>
#include <cstring>
#include "progenitor2.hpp"

int main() {
    const int sampleRate = 44100;
    const int bufferSize = 512;

    // Create reverb
    fv3::progenitor2_f reverb;

    // Initialize exactly like Dragonfly
    reverb.setSampleRate(sampleRate);
    reverb.setMuteOnChange(false);
    reverb.setwet(0);  // 0dB
    reverb.setdryr(0);  // mute dry
    reverb.setwidth(1.0);

    // Set parameters like Dragonfly Room default
    reverb.setRSFactor(3.0);  // size = 30 / 10
    reverb.setPreDelay(0.1);  // avoid zero
    reverb.setrt60(2.0);
    reverb.setidiffusion1(0.75 / 120.0);
    reverb.setodiffusion1(0.75 / 120.0);
    reverb.setdamp(10000.0);
    reverb.setoutputdamp(10000.0);
    reverb.setbassboost(0.1);
    reverb.setspin(0.5);
    reverb.setwander(0.2);

    // Create test signal (impulse)
    float inputL[bufferSize];
    float inputR[bufferSize];
    float outputL[bufferSize];
    float outputR[bufferSize];

    std::memset(inputL, 0, sizeof(float) * bufferSize);
    std::memset(inputR, 0, sizeof(float) * bufferSize);
    std::memset(outputL, 0, sizeof(float) * bufferSize);
    std::memset(outputR, 0, sizeof(float) * bufferSize);

    // Create impulse
    inputL[10] = 0.5f;
    inputR[10] = 0.5f;

    // Process
    reverb.processreplace(inputL, inputR, outputL, outputR, bufferSize);

    // Check output
    float maxOutput = 0.0f;
    for (int i = 0; i < bufferSize; ++i) {
        maxOutput = std::max(maxOutput, std::abs(outputL[i]) + std::abs(outputR[i]));
    }

    std::cout << "=== PROGENITOR2 TEST ===" << std::endl;
    std::cout << "Sample rate: " << sampleRate << std::endl;
    std::cout << "Buffer size: " << bufferSize << std::endl;
    std::cout << "Input impulse: 0.5 at sample 10" << std::endl;
    std::cout << "Max output: " << maxOutput << std::endl;
    std::cout << "Reverb wet: " << reverb.getwet() << " dB" << std::endl;
    std::cout << "Reverb dry: " << reverb.getdryr() << " dB" << std::endl;

    if (maxOutput > 0.0001f) {
        std::cout << "✅ SUCCESS: Progenitor2 produces output!" << std::endl;

        // Show first few output samples
        std::cout << "First 20 output samples (L channel):" << std::endl;
        for (int i = 0; i < 20; ++i) {
            if (std::abs(outputL[i]) > 0.00001f) {
                std::cout << "  [" << i << "]: " << outputL[i] << std::endl;
            }
        }
    } else {
        std::cout << "❌ FAILURE: Progenitor2 produces NO output!" << std::endl;

        // Try to understand why
        std::cout << "\nDiagnostics:" << std::endl;
        std::cout << "  rt60: " << reverb.getrt60() << std::endl;
        std::cout << "  width: " << reverb.getwidth() << std::endl;
        std::cout << "  damp: " << reverb.getdamp() << std::endl;
        std::cout << "  bassboost: " << reverb.getbassboost() << std::endl;
    }

    return 0;
}
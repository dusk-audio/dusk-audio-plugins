// Test program for StudioReverb Room algorithm Late Level parameter
#include <iostream>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

// Simple test framework
class TestReverb {
public:
    // Simulate the reverb processing
    void processRoom(float* inputL, float* inputR,
                     float* outputL, float* outputR,
                     int numSamples,
                     float dryLevel, float earlyLevel,
                     float earlySend, float lateLevel) {

        // Debug output
        std::cout << "\n=== Room Reverb Processing ===" << std::endl;
        std::cout << "Dry Level: " << dryLevel * 100 << "%" << std::endl;
        std::cout << "Early Level: " << earlyLevel * 100 << "%" << std::endl;
        std::cout << "Early Send: " << earlySend * 100 << "%" << std::endl;
        std::cout << "Late Level: " << lateLevel * 100 << "%" << std::endl;

        // Generate test signal (sine wave)
        float testFreq = 440.0f; // Hz
        float sampleRate = 48000.0f;

        for (int i = 0; i < numSamples; ++i) {
            float phase = (2.0f * M_PI * testFreq * i) / sampleRate;
            inputL[i] = std::sin(phase) * 0.5f;
            inputR[i] = inputL[i];
        }

        // Simulate reverb processing
        for (int i = 0; i < numSamples; ++i) {
            // Dry signal
            float dryL = inputL[i] * dryLevel;
            float dryR = inputR[i] * dryLevel;

            // Early reflections (simplified)
            float earlyL = inputL[i] * earlyLevel * 0.8f;
            float earlyR = inputR[i] * earlyLevel * 0.8f;

            // Late reverb (simplified) - THIS IS WHERE THE ISSUE MIGHT BE
            float lateL = inputL[i] * lateLevel * 0.6f;
            float lateR = inputR[i] * lateLevel * 0.6f;

            // Mix signals
            outputL[i] = dryL + earlyL + lateL;
            outputR[i] = dryR + earlyR + lateR;
        }

        // Calculate RMS levels
        float inputRMS = 0.0f, outputRMS = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            inputRMS += inputL[i] * inputL[i];
            outputRMS += outputL[i] * outputL[i];
        }
        inputRMS = std::sqrt(inputRMS / numSamples);
        outputRMS = std::sqrt(outputRMS / numSamples);

        std::cout << "Input RMS: " << inputRMS << std::endl;
        std::cout << "Output RMS: " << outputRMS << std::endl;
        std::cout << "Gain difference: " << 20 * std::log10(outputRMS / inputRMS) << " dB" << std::endl;
    }

    void testLateLevelScaling() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Testing Late Level Scaling for Room Reverb" << std::endl;
        std::cout << "========================================" << std::endl;

        const int numSamples = 1024;
        std::vector<float> inputL(numSamples);
        std::vector<float> inputR(numSamples);
        std::vector<float> outputL(numSamples);
        std::vector<float> outputR(numSamples);

        // Test different Late Level settings
        float testLevels[] = {0.0f, 0.1f, 0.2f, 0.5f, 1.0f};

        for (float lateLevel : testLevels) {
            processRoom(inputL.data(), inputR.data(),
                       outputL.data(), outputR.data(),
                       numSamples,
                       0.8f,  // Dry Level (80%)
                       0.3f,  // Early Level (30%)
                       0.35f, // Early Send (35%)
                       lateLevel); // Late Level (variable)
        }

        // Test extreme case - only late reverb
        std::cout << "\n=== Testing Only Late Reverb (no dry/early) ===" << std::endl;
        processRoom(inputL.data(), inputR.data(),
                   outputL.data(), outputR.data(),
                   numSamples,
                   0.0f,  // No dry
                   0.0f,  // No early
                   0.0f,  // No early send
                   1.0f); // Full late reverb
    }
};

int main() {
    TestReverb tester;
    tester.testLateLevelScaling();

    std::cout << "\n=== ANALYSIS ===" << std::endl;
    std::cout << "The Late Level parameter should control the amount of late reverb" << std::endl;
    std::cout << "mixed into the output signal. If it's not working properly:" << std::endl;
    std::cout << "1. Check if the parameter is properly mapped in the Room algorithm" << std::endl;
    std::cout << "2. Verify the scaling/normalization is correct (0-100% -> 0-1)" << std::endl;
    std::cout << "3. Ensure the late reverb engine is actually generating signal" << std::endl;
    std::cout << "4. Check if the mixing stage is applying the late level correctly" << std::endl;

    return 0;
}
// Test program to debug Room reverb Late Level output
#include <iostream>
#include <cmath>
#include <vector>
#include <cstring>

// Simulate a simple sine wave test signal
void generateTestSignal(float* buffer, int numSamples, float frequency = 440.0f, float sampleRate = 48000.0f) {
    for (int i = 0; i < numSamples; ++i) {
        float phase = (2.0f * M_PI * frequency * i) / sampleRate;
        buffer[i] = 0.5f * std::sin(phase);
    }
}

// Calculate RMS level
float calculateRMS(const float* buffer, int numSamples) {
    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / numSamples);
}

// Find peak level
float findPeak(const float* buffer, int numSamples) {
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float absVal = std::fabs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

int main() {
    std::cout << "\n=== Room Reverb Late Level Diagnostic Test ===" << std::endl;
    std::cout << "Testing signal flow through Room reverb processor\n" << std::endl;

    const int bufferSize = 1024;
    std::vector<float> inputL(bufferSize);
    std::vector<float> inputR(bufferSize);
    std::vector<float> outputL(bufferSize);
    std::vector<float> outputR(bufferSize);

    // Generate test signal
    generateTestSignal(inputL.data(), bufferSize);
    generateTestSignal(inputR.data(), bufferSize);

    float inputRMS = calculateRMS(inputL.data(), bufferSize);
    float inputPeak = findPeak(inputL.data(), bufferSize);

    std::cout << "Input Signal Analysis:" << std::endl;
    std::cout << "  RMS Level: " << inputRMS << " (" << 20 * std::log10(inputRMS) << " dB)" << std::endl;
    std::cout << "  Peak Level: " << inputPeak << " (" << 20 * std::log10(inputPeak) << " dB)" << std::endl;

    // Test different Late Level gains
    std::cout << "\n=== Testing Different Gain Compensation Values ===" << std::endl;
    std::cout << "For Room reverb progenitor2 algorithm:\n" << std::endl;

    float testGains[] = {1.0f, 10.0f, 30.0f, 50.0f, 100.0f, 250.0f};
    float lateLevel = 1.0f;  // 100% late level

    for (float gain : testGains) {
        // Simulate late reverb output (very quiet, ~-60dB)
        float lateReverbLevel = 0.001f;  // Simulating progenitor2's low output

        // Apply gain compensation
        float outputLevel = lateReverbLevel * lateLevel * gain;

        std::cout << "Gain = " << gain << "x (" << 20 * std::log10(gain) << " dB):" << std::endl;
        std::cout << "  Late reverb raw output: " << lateReverbLevel << " (" << 20 * std::log10(lateReverbLevel) << " dB)" << std::endl;
        std::cout << "  After gain compensation: " << outputLevel << " (" << 20 * std::log10(outputLevel) << " dB)" << std::endl;

        // Check if it's audible (above -60dB)
        if (20 * std::log10(outputLevel) > -60.0f) {
            std::cout << "  ✓ Should be AUDIBLE" << std::endl;
        } else {
            std::cout << "  ✗ Too quiet (below -60dB)" << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "\n=== Recommendation ===" << std::endl;
    std::cout << "Based on progenitor2's naturally low output (~-60dB)," << std::endl;
    std::cout << "a gain compensation of 30-50x (30-34dB) should provide" << std::endl;
    std::cout << "audible reverb while avoiding excessive amplification." << std::endl;
    std::cout << "\nCurrent setting: 30x (~30dB) gain compensation" << std::endl;

    return 0;
}

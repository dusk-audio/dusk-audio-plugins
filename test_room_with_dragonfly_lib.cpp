#include <iostream>
#include <cmath>
#include <cstring>
#include <memory>

// Include the freeverb headers
#define FV3_FLOAT_SUPPORT 1
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"
#include "plugins/StudioReverb/Source/freeverb/utils.hpp"

float calculateEnergy(float* buffer, int numSamples, int startSample = 0) {
    float energy = 0.0f;
    for (int i = startSample; i < numSamples; ++i) {
        energy += buffer[i] * buffer[i];
    }
    return std::sqrt(energy / (numSamples - startSample));
}

void testRoomReverb() {
    std::cout << "\n=== Testing Room Reverb with Dragonfly's freeverb library ===" << std::endl;

    const int sampleRate = 48000;
    const int bufferSize = 48000; // 1 second

    // Create Room reverb instance
    fv3::progenitor2_f room;

    // Initialize
    room.setSampleRate(sampleRate);

    // Configure exactly like Dragonfly
    room.setRSFactor(1.0f);
    room.setdryr(0);  // Mute dry signal - LINEAR value
    room.setwet(0);   // 0 dB wet = 1.0 linear
    room.setPreDelay(0);

    // Set room parameters
    room.setrt60(1.0f);
    room.setdiffusion1(0.5f);
    room.setdiffusion2(0.3f);
    room.setinputdamp(8000.0f);
    room.setdamp(6000.0f);
    room.setoutputdamp(8000.0f);
    room.setbassap(300.0f, 1.5f);  // frequency and feedback
    room.setspin(1.0f);
    room.setwander(0.0f);

    // Create test signal (impulse)
    float* inputL = new float[bufferSize];
    float* inputR = new float[bufferSize];
    float* outputL = new float[bufferSize];
    float* outputR = new float[bufferSize];

    std::memset(inputL, 0, sizeof(float) * bufferSize);
    std::memset(inputR, 0, sizeof(float) * bufferSize);
    std::memset(outputL, 0, sizeof(float) * bufferSize);
    std::memset(outputR, 0, sizeof(float) * bufferSize);

    // Create impulse
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;

    // Process
    room.processreplace(inputL, inputR, outputL, outputR, bufferSize);

    // Calculate energy in different time windows
    float earlyEnergy = calculateEnergy(outputL, 2000, 0);  // First 42ms
    float lateEnergy = calculateEnergy(outputL, bufferSize, 2000);  // After 42ms
    float totalEnergy = calculateEnergy(outputL, bufferSize, 0);

    std::cout << "\nResults with Dragonfly's freeverb library:" << std::endl;
    std::cout << "Early energy (0-42ms): " << earlyEnergy << std::endl;
    std::cout << "Late energy (42ms-1s): " << lateEnergy << std::endl;
    std::cout << "Total energy: " << totalEnergy << std::endl;

    // Check specific samples
    std::cout << "\nSample values:" << std::endl;
    std::cout << "Sample[100]: " << outputL[100] << std::endl;
    std::cout << "Sample[1000]: " << outputL[1000] << std::endl;
    std::cout << "Sample[5000]: " << outputL[5000] << std::endl;
    std::cout << "Sample[10000]: " << outputL[10000] << std::endl;
    std::cout << "Sample[20000]: " << outputL[20000] << std::endl;

    // Verify reverb tail exists
    bool hasReverbTail = false;
    for (int i = 10000; i < 30000; ++i) {
        if (std::abs(outputL[i]) > 0.001f) {
            hasReverbTail = true;
            break;
        }
    }

    std::cout << "\n=== VERIFICATION ===" << std::endl;
    if (lateEnergy > 0.05f && hasReverbTail) {
        std::cout << "✓ Room reverb is producing proper wet signal!" << std::endl;
        std::cout << "✓ Late energy is substantial: " << lateEnergy << std::endl;
        std::cout << "✓ Reverb tail detected" << std::endl;
    } else {
        std::cout << "✗ Room reverb still not producing proper wet signal" << std::endl;
        std::cout << "  Late energy too low: " << lateEnergy << " (expected > 0.05)" << std::endl;
        std::cout << "  Reverb tail: " << (hasReverbTail ? "present" : "missing") << std::endl;
    }

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;
}

int main() {
    testRoomReverb();
    return 0;
}
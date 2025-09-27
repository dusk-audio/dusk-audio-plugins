/*
  Accurately detect if output is DRY signal or REVERB
  Reverb has these characteristics:
  1. Signal continues AFTER input stops (decay tail)
  2. Signal is diffused/spread out in time
  3. Signal has different spectral content than input
*/

#include <iostream>
#include <cmath>
#include <cstring>
#include <iomanip>

#define LIBFV3_FLOAT

#include "freeverb/progenitor2.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

void analyzeOutput(const char* name, float* inputL, float* inputR,
                   float* outputL, float* outputR, int size) {

    // Find where input impulse is
    int impulsePos = -1;
    float maxInput = 0;
    for (int i = 0; i < size; i++) {
        float mag = std::abs(inputL[i]) + std::abs(inputR[i]);
        if (mag > maxInput) {
            maxInput = mag;
            impulsePos = i;
        }
    }

    std::cout << "\n" << name << ":\n";
    std::cout << "----------------------------------------\n";

    if (impulsePos < 0 || maxInput < 0.001f) {
        std::cout << "  No input detected!\n";
        return;
    }

    std::cout << "  Input impulse at sample " << impulsePos << " (magnitude " << maxInput << ")\n";

    // Check 1: Is output at impulse position same as input? (dry passthrough)
    float outputAtImpulse = std::abs(outputL[impulsePos]) + std::abs(outputR[impulsePos]);
    float ratio = outputAtImpulse / maxInput;

    std::cout << "  Output at impulse: " << outputAtImpulse;
    if (ratio > 0.9f && ratio < 1.1f) {
        std::cout << " (SAME as input - DRY SIGNAL!)";
    } else if (outputAtImpulse < maxInput * 0.1f) {
        std::cout << " (much less than input - processed)";
    }
    std::cout << "\n";

    // Check 2: Is there signal BEFORE the impulse? (pre-ringing from filters/reverb)
    float preEnergy = 0;
    int preStart = std::max(0, impulsePos - 100);
    for (int i = preStart; i < impulsePos; i++) {
        preEnergy += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }
    std::cout << "  Pre-impulse energy: " << preEnergy;
    if (preEnergy > 0.0001f) std::cout << " (has pre-ringing)";
    std::cout << "\n";

    // Check 3: Decay tail analysis - does signal continue after impulse?
    float energy1 = 0, energy2 = 0, energy3 = 0;
    int windowSize = 1000;

    // Window 1: Right after impulse
    for (int i = impulsePos + 10; i < impulsePos + windowSize && i < size; i++) {
        energy1 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    // Window 2: Mid decay
    for (int i = impulsePos + windowSize; i < impulsePos + 2*windowSize && i < size; i++) {
        energy2 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    // Window 3: Late decay
    for (int i = impulsePos + 2*windowSize; i < impulsePos + 3*windowSize && i < size; i++) {
        energy3 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    std::cout << "  Decay analysis:\n";
    std::cout << "    10-1000 samples: " << energy1 << "\n";
    std::cout << "    1000-2000 samples: " << energy2 << "\n";
    std::cout << "    2000-3000 samples: " << energy3 << "\n";

    // Check 4: Overall verdict
    bool hasDecayTail = (energy1 > 0.001f) || (energy2 > 0.001f);
    bool isJustDry = (ratio > 0.9f && ratio < 1.1f) && !hasDecayTail;
    bool isReverb = hasDecayTail && (energy1 > energy3); // Should decay over time

    std::cout << "\n  VERDICT: ";
    if (isJustDry) {
        std::cout << "✗ DRY SIGNAL ONLY (no reverb)\n";
    } else if (isReverb) {
        std::cout << "✓ REVERB DETECTED (has decay tail)\n";
    } else if (hasDecayTail) {
        std::cout << "⚠ Has tail but unclear if reverb\n";
    } else {
        std::cout << "✗ NO REVERB (no tail)\n";
    }
}

void testRoomConfiguration(const char* testName, float dryrValue) {
    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE / 4; // 0.25 seconds

    std::cout << "\n========================================\n";
    std::cout << testName << "\n";
    std::cout << "Testing with setdryr(" << dryrValue << ")\n";
    std::cout << "========================================\n";

    // Create buffers
    float* inputL = new float[TEST_SIZE];
    float* inputR = new float[TEST_SIZE];
    float* outputL = new float[TEST_SIZE];
    float* outputR = new float[TEST_SIZE];

    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));
    memset(outputL, 0, TEST_SIZE * sizeof(float));
    memset(outputR, 0, TEST_SIZE * sizeof(float));

    // Add short impulse
    inputL[1000] = 1.0f;
    inputR[1000] = 1.0f;

    // Initialize Room EXACTLY like Dragonfly supposedly does
    fv3::progenitor2_f room;
    room.setMuteOnChange(false);
    room.setwet(0);      // 0dB
    room.setdryr(dryrValue);  // Test different values
    room.setwidth(1.0);
    room.setSampleRate(SAMPLE_RATE);

    // Set basic reverb parameters
    room.setrt60(2.0f);
    room.setRSFactor(3.0f);
    room.setidiffusion1(0.75f);
    room.setodiffusion1(0.75f);

    std::cout << "Room settings:\n";
    std::cout << "  getwet() = " << room.getwet() << " dB\n";
    std::cout << "  getdryr() = " << room.getdryr() << " dB\n";
    std::cout << "  getrt60() = " << room.getrt60() << " seconds\n";

    // Process
    room.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

    // Analyze
    analyzeOutput("Room Reverb Output", inputL, inputR, outputL, outputR, TEST_SIZE);

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;
}

int main() {
    std::cout << "Accurate Reverb vs Dry Detection Test\n";
    std::cout << "======================================\n";

    // Test different setdryr values
    testRoomConfiguration("Test 1: setdryr(0) - What Dragonfly claims to use", 0);
    testRoomConfiguration("Test 2: setdryr(-70) - Mute dry", -70);
    testRoomConfiguration("Test 3: setdryr(-inf) - Complete mute", -std::numeric_limits<float>::infinity());

    // Now test what Dragonfly ACTUALLY does
    std::cout << "\n========================================\n";
    std::cout << "Test 4: Exact Dragonfly Room Init\n";
    std::cout << "========================================\n";

    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE / 4;

    float* inputL = new float[TEST_SIZE];
    float* inputR = new float[TEST_SIZE];
    float* outputL = new float[TEST_SIZE];
    float* outputR = new float[TEST_SIZE];

    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));
    memset(outputL, 0, TEST_SIZE * sizeof(float));
    memset(outputR, 0, TEST_SIZE * sizeof(float));

    inputL[1000] = 1.0f;
    inputR[1000] = 1.0f;

    // EXACT Dragonfly init sequence
    fv3::progenitor2_f late;
    late.setMuteOnChange(false);
    late.setwet(0); // 0dB
    late.setdryr(0); // Comment says "mute dry signal" but value is 0!
    late.setwidth(1.0);
    late.setSampleRate(SAMPLE_RATE);

    // Add minimal params to make it work
    late.setrt60(2.0f);
    late.setRSFactor(3.0f);

    std::cout << "Dragonfly exact init:\n";
    std::cout << "  late.setwet(0)\n";
    std::cout << "  late.setdryr(0)\n";
    std::cout << "  Result: wet=" << late.getwet() << " dB, dry=" << late.getdryr() << " dB\n";

    late.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

    analyzeOutput("Dragonfly Init Output", inputL, inputR, outputL, outputR, TEST_SIZE);

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}
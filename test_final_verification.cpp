/*
  Final verification that Room reverb works with our current settings
*/

#include <iostream>
#include <cmath>
#include <cstring>
#include <iomanip>

#define LIBFV3_FLOAT

#include "freeverb/progenitor2.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

int main() {
    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE / 2; // 0.5 seconds

    std::cout << "Final Room Reverb Verification\n";
    std::cout << "===============================\n\n";

    // Create test buffers
    float* inputL = new float[TEST_SIZE];
    float* inputR = new float[TEST_SIZE];
    float* outputL = new float[TEST_SIZE];
    float* outputR = new float[TEST_SIZE];

    // Clear and add impulse
    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));
    memset(outputL, 0, TEST_SIZE * sizeof(float));
    memset(outputR, 0, TEST_SIZE * sizeof(float));

    inputL[1000] = 1.0f;
    inputR[1000] = 1.0f;

    // Initialize Room EXACTLY as our plugin does now
    fv3::progenitor2_f room;
    room.setMuteOnChange(false);
    room.setSampleRate(SAMPLE_RATE);

    // Critical settings
    room.setwet(0);      // 0dB wet
    room.setdryr(-70);   // MUST be -70 for wet to work!
    room.setwidth(1.0);

    // Parameters
    float size = 30.0f;
    float decay = 2.0f;
    room.setRSFactor(size / 10.0f);
    room.setrt60(decay);
    room.setidiffusion1(0.75f);
    room.setodiffusion1(0.75f);

    // Damping
    float highCut = 10000.0f;
    float lowXover = 200.0f;
    room.setdamp(highCut);
    room.setoutputdamp(highCut);
    room.setdamp2(lowXover);

    // Bass boost
    float lowMult = 1.0f;
    float boostValue = lowMult / 20.0f / std::pow(decay, 1.5f) * (size / 10.0f);
    room.setbassboost(boostValue);

    // Modulation
    float spin = 1.0f;
    float wander = 15.0f;
    room.setspin(spin);
    room.setspin2(std::sqrt(100.0f - (10.0f - spin) * (10.0f - spin)) / 2.0f);
    room.setwander(wander / 200.0f + 0.1f);
    room.setwander2(wander / 200.0f + 0.1f);

    // Other params
    room.setbassap(150, 4);
    room.setmodulationnoise1(0.09f);
    room.setmodulationnoise2(0.06f);
    room.setcrossfeed(0.4f);

    std::cout << "Room Configuration:\n";
    std::cout << "  wet = " << room.getwet() << " dB\n";
    std::cout << "  dry = " << room.getdryr() << " dB\n";
    std::cout << "  rt60 = " << room.getrt60() << " seconds\n";
    std::cout << "  RSFactor = " << room.getRSFactor() << "\n\n";

    // Process
    room.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

    // Analyze output
    std::cout << "Output Analysis:\n";
    std::cout << "================\n";

    // 1. Check immediate response at impulse
    float impulseResponse = std::abs(outputL[1000]) + std::abs(outputR[1000]);
    std::cout << "1. Response at impulse (sample 1000): " << impulseResponse << "\n";
    if (impulseResponse > 1.8f && impulseResponse < 2.2f) {
        std::cout << "   ✗ Looks like DRY passthrough!\n";
    } else if (impulseResponse > 100) {
        std::cout << "   ⚠ Very high - might be amplified dry\n";
    } else {
        std::cout << "   ✓ Processed (not dry)\n";
    }

    // 2. Check for reverb tail
    float energy1 = 0, energy2 = 0, energy3 = 0;
    int window = 2000;

    for (int i = 1010; i < 1010 + window && i < TEST_SIZE; i++) {
        energy1 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }
    for (int i = 1010 + window; i < 1010 + 2*window && i < TEST_SIZE; i++) {
        energy2 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }
    for (int i = 1010 + 2*window; i < 1010 + 3*window && i < TEST_SIZE; i++) {
        energy3 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    std::cout << "\n2. Reverb Tail Energy:\n";
    std::cout << "   1010-3010 samples: " << energy1 << "\n";
    std::cout << "   3010-5010 samples: " << energy2 << "\n";
    std::cout << "   5010-7010 samples: " << energy3 << "\n";

    bool hasReverb = energy1 > 0.01f && energy2 > 0.01f;
    bool isDecaying = energy1 > energy2 && energy2 > energy3;

    std::cout << "\n3. Verdict:\n";
    if (!hasReverb) {
        std::cout << "   ✗ NO REVERB TAIL DETECTED\n";
    } else if (!isDecaying) {
        std::cout << "   ⚠ Has tail but NOT DECAYING properly\n";
    } else {
        std::cout << "   ✓ PROPER REVERB with decay!\n";
    }

    // 4. Mix test - simulate plugin mixing
    std::cout << "\n4. Plugin Mix Simulation:\n";

    float dryLevel = 0.0f;   // 0%
    float lateLevel = 1.0f;  // 100%

    float mixedL[TEST_SIZE];
    float mixedR[TEST_SIZE];

    for (int i = 0; i < TEST_SIZE; i++) {
        mixedL[i] = inputL[i] * dryLevel + outputL[i] * lateLevel;
        mixedR[i] = inputR[i] * dryLevel + outputR[i] * lateLevel;
    }

    float mixedImpulse = std::abs(mixedL[1000]) + std::abs(mixedR[1000]);
    float mixedEnergy = 0;
    for (int i = 1010; i < 1010 + window; i++) {
        mixedEnergy += mixedL[i]*mixedL[i] + mixedR[i]*mixedR[i];
    }

    std::cout << "   Dry=0%, Late=100%:\n";
    std::cout << "   Response at impulse: " << mixedImpulse << "\n";
    std::cout << "   Tail energy: " << mixedEnergy << "\n";

    if (mixedImpulse > 1.8f && mixedImpulse < 2.2f) {
        std::cout << "   ✗ OUTPUT IS DRY SIGNAL!\n";
    } else if (mixedEnergy > 0.01f) {
        std::cout << "   ✓ OUTPUT IS REVERB!\n";
    } else {
        std::cout << "   ✗ NO OUTPUT!\n";
    }

    std::cout << "\n===============================\n";
    std::cout << "FINAL STATUS:\n";
    if (hasReverb && mixedEnergy > 0.01f && mixedImpulse < 1.8f) {
        std::cout << "✓ Room Reverb is WORKING CORRECTLY!\n";
        std::cout << "  The Late Level knob should produce reverb.\n";
    } else {
        std::cout << "✗ Room Reverb is NOT working properly.\n";
        if (mixedImpulse > 1.8f && mixedImpulse < 2.2f) {
            std::cout << "  Problem: Late Level is outputting DRY signal!\n";
            std::cout << "  This means setdryr(-70) is not working as expected.\n";
        }
    }
    std::cout << "===============================\n";

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}
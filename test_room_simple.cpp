/*
  Simple test to verify Room reverb is working after fixes
*/

#include <iostream>
#include <cmath>
#include <cstring>

#define LIBFV3_FLOAT

// Include freeverb headers
#include "freeverb/progenitor2.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

int main() {
    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE * 2;
    const int IMPULSE_POS = 1000;

    // Create buffers
    float* inputL = new float[TEST_SIZE];
    float* inputR = new float[TEST_SIZE];
    float* outputL = new float[TEST_SIZE];
    float* outputR = new float[TEST_SIZE];

    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));
    memset(outputL, 0, TEST_SIZE * sizeof(float));
    memset(outputR, 0, TEST_SIZE * sizeof(float));

    // Add impulse
    inputL[IMPULSE_POS] = 1.0f;
    inputR[IMPULSE_POS] = 1.0f;

    std::cout << "Testing Room reverb with Dragonfly-exact settings...\n";
    std::cout << "================================================\n\n";

    // Initialize Room EXACTLY like Dragonfly
    fv3::progenitor2_f room;
    room.setSampleRate(SAMPLE_RATE);
    
    // CRITICAL: Do NOT call setReverbType() - let it use default SELF
    // room.setReverbType(FV3_REVTYPE_PROG2);  // DON'T DO THIS!
    
    // Set wet/dry EXACTLY like Dragonfly
    room.setwet(0);      // 0dB wet signal  
    room.setdryr(0);     // 0dB dry signal (Dragonfly uses 0, not -70!)
    room.setwidth(1.0);
    
    // Set reverb parameters
    room.setrt60(2.0f);
    room.setRSFactor(3.0f);
    room.setidiffusion1(0.75f);
    room.setodiffusion1(0.75f);
    room.setdamp(10000.0f);
    room.setdamp2(10000.0f);
    
    // Set modulation like Dragonfly
    room.setmodulationnoise1(0.09f);
    room.setmodulationnoise2(0.06f);
    room.setcrossfeed(0.4f);
    room.setbassap(150, 4);
    
    // Process
    room.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

    // Analyze output
    std::cout << "1. Checking for output signal:\n";
    
    float totalEnergy = 0;
    for (int i = IMPULSE_POS + SAMPLE_RATE/2; i < IMPULSE_POS + SAMPLE_RATE; i++) {
        if (i < TEST_SIZE) {
            totalEnergy += outputL[i]*outputL[i] + outputR[i]*outputR[i];
        }
    }
    
    std::cout << "   Total energy (500ms window): " << totalEnergy << "\n";
    std::cout << "   Status: " << (totalEnergy > 0.001f ? "✓ HAS OUTPUT" : "✗ NO OUTPUT") << "\n\n";

    // Check decay pattern
    std::cout << "2. Checking decay pattern:\n";
    
    const int windowSize = SAMPLE_RATE / 10;
    float energy1 = 0, energy2 = 0, energy3 = 0;
    
    for (int i = 0; i < windowSize; i++) {
        int idx1 = IMPULSE_POS + windowSize + i;     // 100-200ms
        int idx2 = IMPULSE_POS + 3*windowSize + i;   // 300-400ms
        int idx3 = IMPULSE_POS + 5*windowSize + i;   // 500-600ms
        
        if (idx1 < TEST_SIZE) energy1 += outputL[idx1]*outputL[idx1] + outputR[idx1]*outputR[idx1];
        if (idx2 < TEST_SIZE) energy2 += outputL[idx2]*outputL[idx2] + outputR[idx2]*outputR[idx2];
        if (idx3 < TEST_SIZE) energy3 += outputL[idx3]*outputL[idx3] + outputR[idx3]*outputR[idx3];
    }
    
    std::cout << "   100-200ms: " << energy1 << "\n";
    std::cout << "   300-400ms: " << energy2 << "\n";
    std::cout << "   500-600ms: " << energy3 << "\n";
    
    bool isDecaying = (energy1 > energy2) && (energy2 > energy3);
    std::cout << "   Decay: " << (isDecaying ? "✓ PROPER DECAY" : "✗ NOT DECAYING") << "\n\n";

    // Check for dry signal bleed
    std::cout << "3. Checking for dry signal:\n";
    
    float dryPeak = 0;
    for (int i = IMPULSE_POS - 10; i < IMPULSE_POS + 100; i++) {
        if (i >= 0 && i < TEST_SIZE) {
            float mag = std::abs(outputL[i]) + std::abs(outputR[i]);
            if (mag > dryPeak) dryPeak = mag;
        }
    }
    
    std::cout << "   Peak near impulse: " << dryPeak << "\n";
    std::cout << "   Status: " << (dryPeak < 0.5f ? "✓ NO DRY BLEED" : "✗ DRY SIGNAL PRESENT") << "\n\n";

    // Overall verdict
    std::cout << "================================================\n";
    if (totalEnergy > 0.001f && isDecaying && dryPeak < 0.5f) {
        std::cout << "✓ ROOM REVERB IS WORKING CORRECTLY!\n";
        std::cout << "  - Produces wet reverb signal\n";
        std::cout << "  - Has proper exponential decay\n";
        std::cout << "  - No dry signal bleeding through\n";
    } else {
        std::cout << "✗ ROOM REVERB STILL HAS ISSUES:\n";
        if (totalEnergy <= 0.001f) std::cout << "  - No reverb output\n";
        if (!isDecaying) std::cout << "  - Improper decay pattern\n";
        if (dryPeak >= 0.5f) std::cout << "  - Dry signal bleeding through\n";
    }
    std::cout << "================================================\n";

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}

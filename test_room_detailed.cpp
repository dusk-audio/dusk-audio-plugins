/*
  Detailed Room reverb test to diagnose the decay issue
  Tests different parameter configurations
*/

#include <iostream>
#include <cmath>
#include <cstring>
#include <iomanip>

#define LIBFV3_FLOAT

// Include freeverb
#include "freeverb/progenitor2.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

void testRoomConfig(const char* name, float rt60, float rsFactor, float diffusion) {
    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE * 2;
    const int IMPULSE_POS = 1000;

    std::cout << "\n" << name << ":\n";
    std::cout << "  RT60=" << rt60 << ", RSFactor=" << rsFactor << ", Diffusion=" << diffusion << "\n";

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

    // Initialize Room with PROG2
    fv3::progenitor2_f room;
    room.setSampleRate(SAMPLE_RATE);
    room.setReverbType(FV3_REVTYPE_PROG2);

    // Set parameters in specific order
    room.setbassap(150, 4);
    room.setmodulationnoise1(0.09f);
    room.setmodulationnoise2(0.06f);
    room.setcrossfeed(0.4f);

    room.setRSFactor(rsFactor);
    room.setrt60(rt60);
    room.setidiffusion1(diffusion);
    room.setodiffusion1(diffusion);
    room.setdamp(10000.0f);
    room.setdamp2(10000.0f);

    room.setspin(0.5f);
    room.setspin2(0.5f);
    room.setwander(0.25f);
    room.setwander2(0.25f);

    room.setwet(0);
    room.setdryr(-70);
    room.setwidth(1.0);

    // Process
    room.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

    // Measure energy at different points
    const int windowSize = SAMPLE_RATE / 10;

    float energy1 = 0, energy2 = 0, energy3 = 0, energy4 = 0;

    for (int i = 0; i < windowSize; i++) {
        int idx1 = IMPULSE_POS + windowSize + i;     // 100-200ms
        int idx2 = IMPULSE_POS + 3*windowSize + i;   // 300-400ms
        int idx3 = IMPULSE_POS + 5*windowSize + i;   // 500-600ms
        int idx4 = IMPULSE_POS + 8*windowSize + i;   // 800-900ms

        if (idx1 < TEST_SIZE) energy1 += outputL[idx1]*outputL[idx1] + outputR[idx1]*outputR[idx1];
        if (idx2 < TEST_SIZE) energy2 += outputL[idx2]*outputL[idx2] + outputR[idx2]*outputR[idx2];
        if (idx3 < TEST_SIZE) energy3 += outputL[idx3]*outputL[idx3] + outputR[idx3]*outputR[idx3];
        if (idx4 < TEST_SIZE) energy4 += outputL[idx4]*outputL[idx4] + outputR[idx4]*outputR[idx4];
    }

    energy1 /= 2.0f;
    energy2 /= 2.0f;
    energy3 /= 2.0f;
    energy4 /= 2.0f;

    std::cout << std::scientific << std::setprecision(4);
    std::cout << "  100-200ms: " << energy1 << "\n";
    std::cout << "  300-400ms: " << energy2 << "\n";
    std::cout << "  500-600ms: " << energy3 << "\n";
    std::cout << "  800-900ms: " << energy4 << "\n";

    bool isDecaying = (energy1 > energy2) && (energy2 > energy3) && (energy3 > energy4);
    std::cout << "  Decay: " << (isDecaying ? "✓ YES" : "✗ NO");

    if (!isDecaying && energy2 > energy1) {
        std::cout << " (Energy INCREASES from 100ms to 300ms!)";
    }
    std::cout << "\n";

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;
}

void testRoomWithDifferentTypes() {
    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE * 2;
    const int IMPULSE_POS = 1000;

    std::cout << "\nTesting different reverb types:\n";

    for (int type = 0; type <= 2; type++) {
        const char* typeName = (type == 0) ? "SELF" : (type == 1) ? "PROG" : "PROG2";
        std::cout << "\nType " << typeName << " (" << (type == 0 ? FV3_REVTYPE_SELF : type == 1 ? FV3_REVTYPE_PROG : FV3_REVTYPE_PROG2) << "):\n";

        float* inputL = new float[TEST_SIZE];
        float* inputR = new float[TEST_SIZE];
        float* outputL = new float[TEST_SIZE];
        float* outputR = new float[TEST_SIZE];

        memset(inputL, 0, TEST_SIZE * sizeof(float));
        memset(inputR, 0, TEST_SIZE * sizeof(float));
        memset(outputL, 0, TEST_SIZE * sizeof(float));
        memset(outputR, 0, TEST_SIZE * sizeof(float));

        inputL[IMPULSE_POS] = 1.0f;
        inputR[IMPULSE_POS] = 1.0f;

        fv3::progenitor2_f room;
        room.setSampleRate(SAMPLE_RATE);
        room.setReverbType(type == 0 ? FV3_REVTYPE_SELF : type == 1 ? FV3_REVTYPE_PROG : FV3_REVTYPE_PROG2);

        room.setrt60(2.0f);
        room.setRSFactor(3.0f);
        room.setidiffusion1(0.75f);
        room.setodiffusion1(0.75f);
        room.setwet(0);
        room.setdryr(-70);

        room.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

        // Check for any output
        float totalEnergy = 0;
        for (int i = IMPULSE_POS + SAMPLE_RATE/2; i < IMPULSE_POS + SAMPLE_RATE; i++) {
            if (i < TEST_SIZE) {
                totalEnergy += outputL[i]*outputL[i] + outputR[i]*outputR[i];
            }
        }

        std::cout << "  Total energy: " << std::scientific << totalEnergy;
        std::cout << " - " << (totalEnergy > 0.0001f ? "✓ Has output" : "✗ No output") << "\n";

        delete[] inputL;
        delete[] inputR;
        delete[] outputL;
        delete[] outputR;
    }
}

int main() {
    std::cout << "Detailed Room Reverb Diagnostic Test\n";
    std::cout << "====================================\n";

    std::cout << "\nTesting different parameter configurations:\n";

    testRoomConfig("Config 1: Default", 2.0f, 3.0f, 0.75f);
    testRoomConfig("Config 2: Small room", 1.0f, 1.5f, 0.5f);
    testRoomConfig("Config 3: Large room", 4.0f, 6.0f, 0.9f);
    testRoomConfig("Config 4: No diffusion", 2.0f, 3.0f, 0.0f);
    testRoomConfig("Config 5: Max diffusion", 2.0f, 3.0f, 1.0f);

    testRoomWithDifferentTypes();

    std::cout << "\n====================================\n";
    std::cout << "Diagnosis complete.\n";

    return 0;
}
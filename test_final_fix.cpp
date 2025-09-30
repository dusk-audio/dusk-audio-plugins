/*
  Final test - figure out why progenitor2 doesn't produce reverb
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
    const int TEST_SIZE = SAMPLE_RATE;

    std::cout << "Final Fix Test\n";
    std::cout << "==============\n\n";

    float* inputL = new float[TEST_SIZE];
    float* inputR = new float[TEST_SIZE];
    float* outputL = new float[TEST_SIZE];
    float* outputR = new float[TEST_SIZE];

    // Test 1: Verify setdryr(0) behavior
    std::cout << "Test 1: Understanding setdryr(0)\n";
    std::cout << "---------------------------------\n";

    fv3::progenitor2_f room1;
    room1.setSampleRate(SAMPLE_RATE);
    room1.setwet(0);    // 0dB = 1.0 linear
    room1.setdryr(0);   // 0 linear = mute
    room1.setwidth(1.0);

    std::cout << "After setwet(0), setdryr(0):\n";
    std::cout << "  getwet() returns: " << room1.getwet() << " dB\n";
    std::cout << "  getdryr() returns: " << room1.getdryr() << " (linear)\n";
    std::cout << "  getwetr() returns: " << room1.getwetr() << " (linear)\n\n";

    // Process with impulse
    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));
    memset(outputL, 0, TEST_SIZE * sizeof(float));
    memset(outputR, 0, TEST_SIZE * sizeof(float));
    inputL[1000] = 1.0f;
    inputR[1000] = 1.0f;

    // Need to set actual reverb parameters!
    room1.setrt60(2.0f);
    room1.setRSFactor(3.0f);
    room1.setidiffusion1(0.75f);
    room1.setodiffusion1(0.75f);

    room1.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

    float energy1 = 0;
    for (int i = 1000; i < 5000; i++) {
        energy1 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    std::cout << "Output energy (1000-5000): " << energy1 << "\n";
    std::cout << "Output at impulse: " << (outputL[1000] + outputR[1000]) << "\n";

    // Check if wet signal exists in later samples
    float lateEnergy = 0;
    for (int i = 5000; i < 10000; i++) {
        lateEnergy += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }
    std::cout << "Late energy (5000-10000): " << lateEnergy << "\n\n";

    // Test 2: Try setwetr instead of setwet
    std::cout << "Test 2: Using setwetr(1) instead of setwet(0)\n";
    std::cout << "----------------------------------------------\n";

    fv3::progenitor2_f room2;
    room2.setSampleRate(SAMPLE_RATE);
    room2.setwetr(1.0);  // 1.0 linear directly
    room2.setdryr(0);    // 0 linear = mute
    room2.setwidth(1.0);
    room2.setrt60(2.0f);
    room2.setRSFactor(3.0f);

    std::cout << "After setwetr(1.0), setdryr(0):\n";
    std::cout << "  getwet() returns: " << room2.getwet() << " dB\n";
    std::cout << "  getwetr() returns: " << room2.getwetr() << " (linear)\n\n";

    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));
    memset(outputL, 0, TEST_SIZE * sizeof(float));
    memset(outputR, 0, TEST_SIZE * sizeof(float));
    inputL[1000] = 1.0f;
    inputR[1000] = 1.0f;

    room2.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

    float energy2 = 0;
    for (int i = 1000; i < 5000; i++) {
        energy2 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    std::cout << "Output energy (1000-5000): " << energy2 << "\n";
    std::cout << "Output at impulse: " << (outputL[1000] + outputR[1000]) << "\n\n";

    // Test 3: Check if mute() is breaking things
    std::cout << "Test 3: Without ever calling mute()\n";
    std::cout << "------------------------------------\n";

    fv3::progenitor2_f room3;
    // DON'T call mute()
    room3.setSampleRate(SAMPLE_RATE);
    room3.setwet(0);
    room3.setdryr(0);
    room3.setwidth(1.0);
    room3.setrt60(2.0f);
    room3.setRSFactor(3.0f);
    room3.setidiffusion1(0.75f);
    room3.setodiffusion1(0.75f);

    // Set all the parameters Dragonfly sets
    room3.setdamp(10000.0f);
    room3.setoutputdamp(10000.0f);
    room3.setdamp2(200.0f);
    room3.setbassboost(0.1f);
    room3.setspin(1.0f);
    room3.setspin2(0.5f);
    room3.setwander(0.15f);
    room3.setwander2(0.15f);

    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));
    memset(outputL, 0, TEST_SIZE * sizeof(float));
    memset(outputR, 0, TEST_SIZE * sizeof(float));
    inputL[1000] = 1.0f;
    inputR[1000] = 1.0f;

    room3.processreplace(inputL, inputR, outputL, outputR, TEST_SIZE);

    float energy3 = 0;
    for (int i = 1000; i < 5000; i++) {
        energy3 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    std::cout << "Output energy (1000-5000): " << energy3 << "\n";
    std::cout << "Output at impulse: " << (outputL[1000] + outputR[1000]) << "\n\n";

    std::cout << "==============\n";
    std::cout << "CONCLUSION:\n";
    std::cout << "==============\n";

    if (energy1 > 0.1f || energy2 > 0.1f || energy3 > 0.1f) {
        std::cout << "✓ At least one configuration produces reverb!\n";
        if (energy1 > 0.1f) std::cout << "  Test 1 (setwet(0), setdryr(0)) works\n";
        if (energy2 > 0.1f) std::cout << "  Test 2 (setwetr(1), setdryr(0)) works\n";
        if (energy3 > 0.1f) std::cout << "  Test 3 (with all params) works\n";
    } else {
        std::cout << "✗ No configuration produces proper reverb\n";
        std::cout << "  The progenitor2 algorithm may need initialization\n";
        std::cout << "  or there's a fundamental issue with the library\n";
    }

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}
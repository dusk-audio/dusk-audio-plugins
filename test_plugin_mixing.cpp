/*
  Test the actual plugin mixing logic - dry=0, late level up
  This mimics how the plugin is used in a DAW
*/

#include <iostream>
#include <cmath>
#include <cstring>

#define LIBFV3_FLOAT

// Include freeverb headers
#include "freeverb/progenitor2.hpp"
#include "freeverb/earlyref.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

int main() {
    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE * 2;
    const int IMPULSE_POS = 1000;

    std::cout << "Testing Studio Reverb Room Algorithm\n";
    std::cout << "Settings: Dry Level = 0%, Late Level = 100%\n";
    std::cout << "================================================\n\n";

    // Create buffers
    float* inputL = new float[TEST_SIZE];
    float* inputR = new float[TEST_SIZE];

    // Separate buffers for early and late like the plugin uses
    float* earlyOutL = new float[TEST_SIZE];
    float* earlyOutR = new float[TEST_SIZE];
    float* lateOutL = new float[TEST_SIZE];
    float* lateOutR = new float[TEST_SIZE];
    float* lateInL = new float[TEST_SIZE];
    float* lateInR = new float[TEST_SIZE];

    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));
    memset(earlyOutL, 0, TEST_SIZE * sizeof(float));
    memset(earlyOutR, 0, TEST_SIZE * sizeof(float));
    memset(lateOutL, 0, TEST_SIZE * sizeof(float));
    memset(lateOutR, 0, TEST_SIZE * sizeof(float));
    memset(lateInL, 0, TEST_SIZE * sizeof(float));
    memset(lateInR, 0, TEST_SIZE * sizeof(float));

    // Add impulse
    inputL[IMPULSE_POS] = 1.0f;
    inputR[IMPULSE_POS] = 1.0f;

    // Initialize Early Reflections (like plugin does)
    fv3::earlyref_f early;
    early.setSampleRate(SAMPLE_RATE);
    early.loadPresetReflection(FV3_EARLYREF_PRESET_1);
    early.setwet(0);     // 0dB
    early.setdryr(-70);  // Mute dry
    early.setwidth(0.8f);
    early.setLRDelay(0.3f);
    early.setPreDelay(0);

    // Initialize Room reverb EXACTLY like the plugin
    fv3::progenitor2_f room;
    room.setSampleRate(SAMPLE_RATE);

    // DO NOT call setReverbType()

    // Set wet/dry like the plugin
    room.setwet(0);      // 0dB wet signal
    room.setdryr(0);     // 0dB dry signal
    room.setwidth(1.0);

    // Set parameters like plugin's prepare() does
    room.setbassap(150, 4);
    room.setmodulationnoise1(0.09f);
    room.setmodulationnoise2(0.06f);
    room.setcrossfeed(0.4f);

    // Basic parameters
    room.setrt60(2.0f);
    room.setRSFactor(30.0f / 10.0f);  // size=30, divided by 10
    room.setidiffusion1(0.75f);
    room.setodiffusion1(0.75f);
    room.setdamp(10000.0f);
    room.setdamp2(10000.0f);

    // Modulation
    room.setspin(0.5f);
    room.setspin2(0.5f);
    room.setwander(0.25f);
    room.setwander2(0.25f);

    std::cout << "1. Process early reflections...\n";
    early.processreplace(inputL, inputR, earlyOutL, earlyOutR, TEST_SIZE);

    // Check early output
    float earlyEnergy = 0;
    for (int i = IMPULSE_POS; i < IMPULSE_POS + SAMPLE_RATE/2; i++) {
        if (i < TEST_SIZE) {
            earlyEnergy += earlyOutL[i]*earlyOutL[i] + earlyOutR[i]*earlyOutR[i];
        }
    }
    std::cout << "   Early reflections energy: " << earlyEnergy;
    std::cout << (earlyEnergy > 0.0001f ? " ✓" : " ✗") << "\n\n";

    std::cout << "2. Prepare late reverb input (filtered input + early send)...\n";
    float earlySend = 0.5f;  // Plugin default

    // Copy input to late input buffers (in real plugin this would be filtered)
    memcpy(lateInL, inputL, TEST_SIZE * sizeof(float));
    memcpy(lateInR, inputR, TEST_SIZE * sizeof(float));

    // Add early send
    for (int i = 0; i < TEST_SIZE; i++) {
        lateInL[i] += earlyOutL[i] * earlySend;
        lateInR[i] += earlyOutR[i] * earlySend;
    }

    float lateInEnergy = 0;
    for (int i = IMPULSE_POS; i < IMPULSE_POS + 100; i++) {
        lateInEnergy += lateInL[i]*lateInL[i] + lateInR[i]*lateInR[i];
    }
    std::cout << "   Late input energy: " << lateInEnergy;
    std::cout << (lateInEnergy > 0.0001f ? " ✓" : " ✗") << "\n\n";

    std::cout << "3. Process late reverb with Room algorithm...\n";
    room.processreplace(lateInL, lateInR, lateOutL, lateOutR, TEST_SIZE);

    // Check late output
    float lateEnergy = 0;
    float maxLateSample = 0;
    for (int i = IMPULSE_POS; i < IMPULSE_POS + SAMPLE_RATE/2; i++) {
        if (i < TEST_SIZE) {
            lateEnergy += lateOutL[i]*lateOutL[i] + lateOutR[i]*lateOutR[i];
            float mag = std::abs(lateOutL[i]) + std::abs(lateOutR[i]);
            if (mag > maxLateSample) maxLateSample = mag;
        }
    }
    std::cout << "   Late reverb energy: " << lateEnergy;
    std::cout << (lateEnergy > 0.0001f ? " ✓ HAS OUTPUT" : " ✗ NO OUTPUT") << "\n";
    std::cout << "   Max late sample: " << maxLateSample << "\n\n";

    std::cout << "4. Mix final output (dry=0%, early=0%, late=100%)...\n";
    float dryLevel = 0.0f;   // 0% dry
    float earlyLevel = 0.0f; // 0% early
    float lateLevel = 1.0f;  // 100% late

    float* outputL = new float[TEST_SIZE];
    float* outputR = new float[TEST_SIZE];

    for (int i = 0; i < TEST_SIZE; i++) {
        outputL[i] = inputL[i] * dryLevel +
                     earlyOutL[i] * earlyLevel +
                     lateOutL[i] * lateLevel;
        outputR[i] = inputR[i] * dryLevel +
                     earlyOutR[i] * earlyLevel +
                     lateOutR[i] * lateLevel;
    }

    // Check final output
    float finalEnergy = 0;
    float maxFinalSample = 0;
    for (int i = IMPULSE_POS; i < IMPULSE_POS + SAMPLE_RATE/2; i++) {
        if (i < TEST_SIZE) {
            finalEnergy += outputL[i]*outputL[i] + outputR[i]*outputR[i];
            float mag = std::abs(outputL[i]) + std::abs(outputR[i]);
            if (mag > maxFinalSample) maxFinalSample = mag;
        }
    }

    std::cout << "   Final output energy: " << finalEnergy;
    std::cout << (finalEnergy > 0.0001f ? " ✓" : " ✗") << "\n";
    std::cout << "   Max output sample: " << maxFinalSample << "\n\n";

    std::cout << "================================================\n";

    bool lateWorks = lateEnergy > 0.0001f;
    bool finalWorks = finalEnergy > 0.0001f;

    if (!lateWorks) {
        std::cout << "✗ PROBLEM: Room reverb produces NO late output!\n";
        std::cout << "  The progenitor2 algorithm is not generating reverb.\n";
    } else if (!finalWorks) {
        std::cout << "✗ PROBLEM: Mixing issue - late has output but final doesn't!\n";
    } else {
        std::cout << "✓ Room reverb is working correctly!\n";
        std::cout << "  Late reverb produces output when late level is up.\n";
    }

    std::cout << "================================================\n";

    // Test what Dragonfly actually does with dry signal
    std::cout << "\n5. Testing dry signal behavior...\n";
    std::cout << "   room.getdryr() = " << room.getdryr() << " dB\n";
    std::cout << "   room.getwet() = " << room.getwet() << " dB\n";

    delete[] inputL;
    delete[] inputR;
    delete[] earlyOutL;
    delete[] earlyOutR;
    delete[] lateOutL;
    delete[] lateOutR;
    delete[] lateInL;
    delete[] lateInR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}
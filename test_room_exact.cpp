/*
  Test Room reverb EXACTLY as the plugin uses it
  This should reveal why late signal is not working
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
    const int BUFFER_SIZE = 256;
    const int TEST_SAMPLES = SAMPLE_RATE;

    std::cout << "Testing Room Reverb Initialization\n";
    std::cout << "===================================\n\n";

    // Create test signal
    float* inputL = new float[BUFFER_SIZE];
    float* inputR = new float[BUFFER_SIZE];
    float* outputL = new float[BUFFER_SIZE];
    float* outputR = new float[BUFFER_SIZE];

    memset(inputL, 0, BUFFER_SIZE * sizeof(float));
    memset(inputR, 0, BUFFER_SIZE * sizeof(float));
    memset(outputL, 0, BUFFER_SIZE * sizeof(float));
    memset(outputR, 0, BUFFER_SIZE * sizeof(float));

    // Add impulse
    inputL[10] = 1.0f;
    inputR[10] = 1.0f;

    // Test 1: Initialize EXACTLY like our plugin's constructor
    std::cout << "Test 1: Plugin Constructor Init\n";
    std::cout << "--------------------------------\n";
    fv3::progenitor2_f room1;
    room1.setMuteOnChange(false);
    room1.setSampleRate(SAMPLE_RATE);
    room1.setwet(0);  // 0dB
    room1.setdryr(0);  // 0dB - our current setting
    room1.setwidth(1.0);
    room1.setRSFactor(3.0f);
    room1.setrt60(2.0f);
    room1.setidiffusion1(0.75f);
    room1.setodiffusion1(0.75f);
    room1.setdamp(10000.0f);
    room1.setdamp2(10000.0f);
    room1.setbassap(150, 4);
    room1.setmodulationnoise1(0.09f);
    room1.setmodulationnoise2(0.06f);
    room1.setcrossfeed(0.4f);
    room1.setspin(0.5f);
    room1.setspin2(0.5f);
    room1.setwander(0.25f);
    room1.setwander2(0.25f);

    // Process
    room1.processreplace(inputL, inputR, outputL, outputR, BUFFER_SIZE);

    float energy1 = 0;
    for (int i = 0; i < BUFFER_SIZE; i++) {
        energy1 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    std::cout << "  Output energy: " << energy1;
    if (energy1 < 0.0001f) std::cout << " ✗ NO OUTPUT!";
    else std::cout << " ✓";
    std::cout << "\n";
    std::cout << "  getwet() = " << room1.getwet() << " dB\n";
    std::cout << "  getdryr() = " << room1.getdryr() << " dB\n\n";

    // Test 2: Try with setdryr(-70) like we thought it should be
    std::cout << "Test 2: With setdryr(-70)\n";
    std::cout << "-------------------------\n";

    // Reset input
    memset(inputL, 0, BUFFER_SIZE * sizeof(float));
    memset(inputR, 0, BUFFER_SIZE * sizeof(float));
    memset(outputL, 0, BUFFER_SIZE * sizeof(float));
    memset(outputR, 0, BUFFER_SIZE * sizeof(float));
    inputL[10] = 1.0f;
    inputR[10] = 1.0f;

    fv3::progenitor2_f room2;
    room2.setMuteOnChange(false);
    room2.setSampleRate(SAMPLE_RATE);
    room2.setwet(0);  // 0dB
    room2.setdryr(-70);  // MUTE dry
    room2.setwidth(1.0);
    room2.setRSFactor(3.0f);
    room2.setrt60(2.0f);
    room2.setidiffusion1(0.75f);
    room2.setodiffusion1(0.75f);
    room2.setdamp(10000.0f);
    room2.setdamp2(10000.0f);

    room2.processreplace(inputL, inputR, outputL, outputR, BUFFER_SIZE);

    float energy2 = 0;
    for (int i = 0; i < BUFFER_SIZE; i++) {
        energy2 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    std::cout << "  Output energy: " << energy2;
    if (energy2 < 0.0001f) std::cout << " ✗ NO OUTPUT!";
    else std::cout << " ✓";
    std::cout << "\n";
    std::cout << "  getwet() = " << room2.getwet() << " dB\n";
    std::cout << "  getdryr() = " << room2.getdryr() << " dB\n\n";

    // Test 3: Try WITHOUT calling setMuteOnChange(false)
    std::cout << "Test 3: Without setMuteOnChange(false)\n";
    std::cout << "---------------------------------------\n";

    // Reset input
    memset(inputL, 0, BUFFER_SIZE * sizeof(float));
    memset(inputR, 0, BUFFER_SIZE * sizeof(float));
    memset(outputL, 0, BUFFER_SIZE * sizeof(float));
    memset(outputR, 0, BUFFER_SIZE * sizeof(float));
    inputL[10] = 1.0f;
    inputR[10] = 1.0f;

    fv3::progenitor2_f room3;
    // DON'T call setMuteOnChange
    room3.setSampleRate(SAMPLE_RATE);
    room3.setwet(0);
    room3.setdryr(-70);
    room3.setwidth(1.0);
    room3.setrt60(2.0f);
    room3.setRSFactor(3.0f);

    room3.processreplace(inputL, inputR, outputL, outputR, BUFFER_SIZE);

    float energy3 = 0;
    for (int i = 0; i < BUFFER_SIZE; i++) {
        energy3 += outputL[i]*outputL[i] + outputR[i]*outputR[i];
    }

    std::cout << "  Output energy: " << energy3;
    if (energy3 < 0.0001f) std::cout << " ✗ NO OUTPUT!";
    else std::cout << " ✓";
    std::cout << "\n\n";

    // Test 4: Process multiple times (maybe it needs priming?)
    std::cout << "Test 4: Process Multiple Times\n";
    std::cout << "-------------------------------\n";

    // Reset
    memset(inputL, 0, BUFFER_SIZE * sizeof(float));
    memset(inputR, 0, BUFFER_SIZE * sizeof(float));
    inputL[10] = 1.0f;
    inputR[10] = 1.0f;

    fv3::progenitor2_f room4;
    room4.setSampleRate(SAMPLE_RATE);
    room4.setwet(0);
    room4.setdryr(-70);
    room4.setrt60(2.0f);
    room4.setRSFactor(3.0f);

    // Process multiple times
    for (int pass = 0; pass < 5; pass++) {
        memset(outputL, 0, BUFFER_SIZE * sizeof(float));
        memset(outputR, 0, BUFFER_SIZE * sizeof(float));

        room4.processreplace(inputL, inputR, outputL, outputR, BUFFER_SIZE);

        float energy = 0;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            energy += outputL[i]*outputL[i] + outputR[i]*outputR[i];
        }

        std::cout << "  Pass " << (pass+1) << " energy: " << energy;
        if (energy < 0.0001f) std::cout << " ✗";
        else std::cout << " ✓";
        std::cout << "\n";

        // Clear input after first pass
        if (pass == 0) {
            memset(inputL, 0, BUFFER_SIZE * sizeof(float));
            memset(inputR, 0, BUFFER_SIZE * sizeof(float));
        }
    }

    std::cout << "\n===================================\n";
    std::cout << "CONCLUSION:\n";
    std::cout << "===================================\n";

    if (energy1 > 0.0001f) {
        std::cout << "✓ Room reverb with setdryr(0) produces output\n";
    } else {
        std::cout << "✗ Room reverb with setdryr(0) produces NO output\n";
    }

    if (energy2 > 0.0001f) {
        std::cout << "✓ Room reverb with setdryr(-70) produces output\n";
    } else {
        std::cout << "✗ Room reverb with setdryr(-70) produces NO output\n";
    }

    delete[] inputL;
    delete[] inputR;
    delete[] outputL;
    delete[] outputR;

    return 0;
}
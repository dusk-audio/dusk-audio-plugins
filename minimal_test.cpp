/*
  Minimal test program for StudioReverb Room algorithm
  Tests the reverb processing directly
*/

#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>

// Define float type for freeverb
#define LIBFV3_FLOAT

// Simple test buffer
class TestBuffer {
public:
    float* dataL;
    float* dataR;
    int size;

    TestBuffer(int s) : size(s) {
        dataL = new float[size];
        dataR = new float[size];
        memset(dataL, 0, size * sizeof(float));
        memset(dataR, 0, size * sizeof(float));
    }

    ~TestBuffer() {
        delete[] dataL;
        delete[] dataR;
    }

    void addImpulse(int pos, float val = 1.0f) {
        if (pos < size) {
            dataL[pos] = val;
            dataR[pos] = val;
        }
    }

    float getRMS(int start, int len) {
        float sum = 0;
        int end = std::min(start + len, size);
        for (int i = start; i < end; i++) {
            sum += dataL[i] * dataL[i] + dataR[i] * dataR[i];
        }
        return std::sqrt(sum / (2.0f * len));
    }

    float getPeak(int start, int len) {
        float peak = 0;
        int end = std::min(start + len, size);
        for (int i = start; i < end; i++) {
            peak = std::max(peak, std::abs(dataL[i]));
            peak = std::max(peak, std::abs(dataR[i]));
        }
        return peak;
    }
};

// Include only what we need from freeverb
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"
#include "plugins/StudioReverb/Source/freeverb/fv3_type_float.h"
#include "plugins/StudioReverb/Source/freeverb/fv3_defs.h"

int main() {
    std::cout << "Minimal Room Reverb Test\n";
    std::cout << "========================\n\n";

    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE * 2; // 2 seconds

    // Create test buffer
    TestBuffer buffer(TEST_SIZE);

    // Add test impulse
    buffer.addImpulse(1000); // Impulse at 1000 samples

    std::cout << "Input: Impulse at sample 1000\n";

    // Create Room reverb (progenitor2)
    fv3::progenitor2_f room;

    // Initialize Room reverb
    room.setSampleRate(SAMPLE_RATE);
    room.setReverbType(FV3_REVTYPE_PROG2);  // Critical!

    // Set basic parameters
    room.setwet(0);     // 0dB wet signal
    room.setdryr(-70);  // Mute dry
    room.setwidth(1.0); // Full stereo

    // Set Room parameters
    room.setrt60(2.0f);           // 2 second decay
    room.setRSFactor(3.0f);       // Room size
    room.setidiffusion1(0.75f);  // Input diffusion
    room.setodiffusion1(0.75f);  // Output diffusion
    room.setdamp(10000.0f);       // HF damping

    // Process the buffer
    room.processreplace(buffer.dataL, buffer.dataR,
                       buffer.dataL, buffer.dataR, TEST_SIZE);

    // Analyze results
    std::cout << "\nResults:\n";

    // Check reverb tail (0.5s to 1.5s after impulse)
    int tailStart = SAMPLE_RATE / 2;
    int tailLen = SAMPLE_RATE;

    float tailRMS = buffer.getRMS(tailStart, tailLen);
    float tailPeak = buffer.getPeak(tailStart, tailLen);

    std::cout << "Reverb tail RMS:  " << tailRMS << "\n";
    std::cout << "Reverb tail Peak: " << tailPeak << "\n";

    if (tailRMS > 0.0001f) {
        std::cout << "\n✓ SUCCESS: Room reverb is producing output!\n";
        return 0;
    } else {
        std::cout << "\n✗ FAILURE: Room reverb is NOT producing output!\n";
        std::cout << "The reverb processor may not be working correctly.\n";
        return 1;
    }
}
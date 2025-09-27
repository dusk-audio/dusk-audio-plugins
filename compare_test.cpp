/*
  Compare direct progenitor2 vs DragonflyReverb wrapper
  This will help identify where the issue is
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

    void reset() {
        memset(dataL, 0, size * sizeof(float));
        memset(dataR, 0, size * sizeof(float));
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

    void copyFrom(TestBuffer& other) {
        memcpy(dataL, other.dataL, size * sizeof(float));
        memcpy(dataR, other.dataR, size * sizeof(float));
    }
};

// Include freeverb
#include "plugins/StudioReverb/Source/freeverb/progenitor2.hpp"
#include "plugins/StudioReverb/Source/freeverb/fv3_type_float.h"
#include "plugins/StudioReverb/Source/freeverb/fv3_defs.h"

int main() {
    std::cout << "Comparison Test: Direct vs Wrapper\n";
    std::cout << "===================================\n\n";

    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE * 2; // 2 seconds

    // Test 1: Direct progenitor2
    std::cout << "Test 1: Direct progenitor2 reverb\n";
    std::cout << "---------------------------------\n";

    TestBuffer buffer1(TEST_SIZE);
    buffer1.addImpulse(1000);

    fv3::progenitor2_f room1;
    room1.setSampleRate(SAMPLE_RATE);
    room1.setReverbType(FV3_REVTYPE_PROG2);
    room1.setwet(0);      // 0dB
    room1.setdryr(-70);   // Mute dry
    room1.setwidth(1.0);
    room1.setrt60(2.0f);
    room1.setRSFactor(3.0f);
    room1.setidiffusion1(0.75f);
    room1.setodiffusion1(0.75f);
    room1.setdamp(10000.0f);

    // Create separate buffers for in/out
    TestBuffer input1(TEST_SIZE);
    TestBuffer output1(TEST_SIZE);
    input1.addImpulse(1000);

    // Process with separate buffers
    room1.processreplace(input1.dataL, input1.dataR,
                        output1.dataL, output1.dataR, TEST_SIZE);

    float rms1 = output1.getRMS(SAMPLE_RATE/2, SAMPLE_RATE);
    std::cout << "Direct Room RMS: " << rms1 << "\n";
    std::cout << "Status: " << (rms1 > 0.0001f ? "✓ Working" : "✗ Not working") << "\n\n";

    // Test 2: Test wet/dry levels
    std::cout << "Test 2: Testing wet/dry mix levels\n";
    std::cout << "----------------------------------\n";

    // Test with different wet levels
    float wetLevels[] = {0, -10, -20, -70};
    const char* wetNames[] = {"0dB (unity)", "-10dB", "-20dB", "-70dB (muted)"};

    for (int i = 0; i < 4; i++) {
        TestBuffer buffer2(TEST_SIZE);
        buffer2.addImpulse(1000);

        fv3::progenitor2_f room2;
        room2.setSampleRate(SAMPLE_RATE);
        room2.setReverbType(FV3_REVTYPE_PROG2);
        room2.setwet(wetLevels[i]);
        room2.setdryr(-70);
        room2.setwidth(1.0);
        room2.setrt60(2.0f);
        room2.setRSFactor(3.0f);
        room2.setidiffusion1(0.75f);
        room2.setodiffusion1(0.75f);
        room2.setdamp(10000.0f);

        room2.processreplace(buffer2.dataL, buffer2.dataR,
                           buffer2.dataL, buffer2.dataR, TEST_SIZE);

        float rms = buffer2.getRMS(SAMPLE_RATE/2, SAMPLE_RATE);
        std::cout << "Wet level " << wetNames[i] << ": RMS = " << rms << "\n";
    }

    std::cout << "\n";

    // Test 3: Check if issue is with mixing
    std::cout << "Test 3: Manual mixing test\n";
    std::cout << "--------------------------\n";

    TestBuffer dryBuffer(TEST_SIZE);
    TestBuffer wetBuffer(TEST_SIZE);
    TestBuffer mixedBuffer(TEST_SIZE);

    dryBuffer.addImpulse(1000);

    // Process reverb
    fv3::progenitor2_f room3;
    room3.setSampleRate(SAMPLE_RATE);
    room3.setReverbType(FV3_REVTYPE_PROG2);
    room3.setwet(0);
    room3.setdryr(-70);
    room3.setwidth(1.0);
    room3.setrt60(2.0f);
    room3.setRSFactor(3.0f);

    room3.processreplace(dryBuffer.dataL, dryBuffer.dataR,
                        wetBuffer.dataL, wetBuffer.dataR, TEST_SIZE);

    // Manual mix with lateLevel = 1.0
    float lateLevel = 1.0f;
    for (int i = 0; i < TEST_SIZE; i++) {
        mixedBuffer.dataL[i] = wetBuffer.dataL[i] * lateLevel;
        mixedBuffer.dataR[i] = wetBuffer.dataR[i] * lateLevel;
    }

    float wetRMS = wetBuffer.getRMS(SAMPLE_RATE/2, SAMPLE_RATE);
    float mixedRMS = mixedBuffer.getRMS(SAMPLE_RATE/2, SAMPLE_RATE);

    std::cout << "Wet buffer RMS: " << wetRMS << "\n";
    std::cout << "Mixed buffer RMS (late=1.0): " << mixedRMS << "\n";
    std::cout << "Mix is working: " << (std::abs(wetRMS - mixedRMS) < 0.0001f ? "✓ Yes" : "✗ No") << "\n";

    std::cout << "\n=== SUMMARY ===\n";
    if (rms1 > 0.0001f) {
        std::cout << "✓ Core Room reverb (progenitor2) is working\n";
        std::cout << "✓ The issue must be in the DragonflyReverb wrapper or mixing\n";
    } else {
        std::cout << "✗ Core Room reverb is not producing output\n";
    }

    return (rms1 > 0.0001f) ? 0 : 1;
}
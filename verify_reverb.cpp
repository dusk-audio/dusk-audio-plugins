/*
  Verify Room reverb is actually producing REVERB (not just noise)
  This test checks for the characteristic decay pattern of reverb
*/

#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>
#include <iomanip>

// Define float type for freeverb
#define LIBFV3_FLOAT

// Test buffer with analysis functions
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

    // Calculate energy in a time window
    float getEnergy(int start, int length) {
        float energy = 0;
        int end = std::min(start + length, size);
        for (int i = start; i < end; i++) {
            energy += dataL[i] * dataL[i] + dataR[i] * dataR[i];
        }
        return energy / 2.0f;
    }

    // Check if signal has exponential decay (characteristic of reverb)
    bool hasExponentialDecay(int impulsePos, int sampleRate) {
        // Measure energy at different time points after impulse
        const int windowSize = sampleRate / 10; // 100ms windows

        float energy1 = getEnergy(impulsePos + windowSize, windowSize);     // 100-200ms
        float energy2 = getEnergy(impulsePos + windowSize * 3, windowSize); // 300-400ms
        float energy3 = getEnergy(impulsePos + windowSize * 5, windowSize); // 500-600ms
        float energy4 = getEnergy(impulsePos + windowSize * 8, windowSize); // 800-900ms

        std::cout << "  Energy at 100-200ms: " << energy1 << "\n";
        std::cout << "  Energy at 300-400ms: " << energy2 << "\n";
        std::cout << "  Energy at 500-600ms: " << energy3 << "\n";
        std::cout << "  Energy at 800-900ms: " << energy4 << "\n";

        // Check for decay pattern
        bool isDecaying = (energy1 > energy2) && (energy2 > energy3) && (energy3 > energy4);
        bool hasSignificantEnergy = energy1 > 0.0001f;

        return isDecaying && hasSignificantEnergy;
    }

    // Check stereo decorrelation (reverb should decorrelate L/R)
    float getStereoCorrelation(int start, int length) {
        float correlation = 0;
        float energyL = 0;
        float energyR = 0;
        int end = std::min(start + length, size);

        for (int i = start; i < end; i++) {
            correlation += dataL[i] * dataR[i];
            energyL += dataL[i] * dataL[i];
            energyR += dataR[i] * dataR[i];
        }

        if (energyL > 0 && energyR > 0) {
            return correlation / std::sqrt(energyL * energyR);
        }
        return 1.0f; // Perfect correlation if no signal
    }

    // Count zero crossings (reverb has many due to dense reflections)
    int countZeroCrossings(int start, int length) {
        int crossings = 0;
        int end = std::min(start + length - 1, size - 1);

        for (int i = start; i < end; i++) {
            if ((dataL[i] * dataL[i+1]) < 0) crossings++;
            if ((dataR[i] * dataR[i+1]) < 0) crossings++;
        }
        return crossings / 2; // Average of L and R
    }
};

// Include freeverb headers
#include "freeverb/progenitor2.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

bool testRoomReverb() {
    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE * 3; // 3 seconds
    const int IMPULSE_POS = SAMPLE_RATE / 10; // Impulse at 0.1s

    std::cout << "Creating test signal...\n";
    TestBuffer input(TEST_SIZE);
    TestBuffer output(TEST_SIZE);
    input.addImpulse(IMPULSE_POS);

    std::cout << "Initializing Room reverb (progenitor2)...\n";
    fv3::progenitor2_f room;
    room.setSampleRate(SAMPLE_RATE);
    room.setReverbType(FV3_REVTYPE_PROG2);  // Critical for Room
    room.setwet(0);      // 0dB wet signal
    room.setdryr(-70);   // Mute dry signal
    room.setwidth(1.0);  // Full stereo width
    room.setrt60(2.0f);  // 2 second decay
    room.setRSFactor(4.0f);  // Medium-large room
    room.setidiffusion1(0.75f);
    room.setodiffusion1(0.75f);
    room.setdamp(10000.0f);

    std::cout << "Processing audio through reverb...\n";
    room.processreplace(input.dataL, input.dataR,
                       output.dataL, output.dataR, TEST_SIZE);

    std::cout << "\n=== REVERB VERIFICATION ===\n";

    // Test 1: Check for exponential decay
    std::cout << "\n1. Checking for exponential decay pattern:\n";
    bool hasDecay = output.hasExponentialDecay(IMPULSE_POS, SAMPLE_RATE);
    std::cout << "  Result: " << (hasDecay ? "✓ Exponential decay detected" : "✗ No decay pattern") << "\n";

    // Test 2: Check stereo decorrelation
    std::cout << "\n2. Checking stereo decorrelation:\n";
    float correlation = output.getStereoCorrelation(IMPULSE_POS + SAMPLE_RATE/2, SAMPLE_RATE);
    std::cout << "  Correlation coefficient: " << correlation << "\n";
    bool isDecorrelated = correlation < 0.8f; // Reverb should decorrelate
    std::cout << "  Result: " << (isDecorrelated ? "✓ Stereo decorrelation present" : "✗ Too correlated") << "\n";

    // Test 3: Check reflection density
    std::cout << "\n3. Checking reflection density:\n";
    int zeroCrossings = output.countZeroCrossings(IMPULSE_POS + SAMPLE_RATE/4, SAMPLE_RATE/4);
    std::cout << "  Zero crossings in 250ms: " << zeroCrossings << "\n";
    bool isDense = zeroCrossings > 1000; // Reverb should have many crossings
    std::cout << "  Result: " << (isDense ? "✓ Dense reflections detected" : "✗ Too sparse") << "\n";

    // Test 4: Compare to dry signal
    std::cout << "\n4. Comparing to dry signal:\n";
    float dryEnergy = input.getEnergy(IMPULSE_POS + SAMPLE_RATE/2, SAMPLE_RATE);
    float wetEnergy = output.getEnergy(IMPULSE_POS + SAMPLE_RATE/2, SAMPLE_RATE);
    std::cout << "  Dry energy: " << dryEnergy << "\n";
    std::cout << "  Wet energy: " << wetEnergy << "\n";
    bool hasReverb = wetEnergy > dryEnergy * 10; // Reverb adds significant energy
    std::cout << "  Result: " << (hasReverb ? "✓ Reverb energy detected" : "✗ No reverb energy") << "\n";

    // Overall verdict
    bool isRealReverb = hasDecay && isDecorrelated && isDense && hasReverb;

    std::cout << "\n=== VERDICT ===\n";
    if (isRealReverb) {
        std::cout << "✓ CONFIRMED: Output is REAL REVERB with proper characteristics!\n";
        std::cout << "  - Exponential decay pattern\n";
        std::cout << "  - Stereo decorrelation\n";
        std::cout << "  - Dense reflections\n";
        std::cout << "  - Significant reverb tail energy\n";
    } else {
        std::cout << "✗ WARNING: Output does NOT have reverb characteristics!\n";
        if (!hasDecay) std::cout << "  - Missing exponential decay\n";
        if (!isDecorrelated) std::cout << "  - No stereo decorrelation\n";
        if (!isDense) std::cout << "  - Reflections too sparse\n";
        if (!hasReverb) std::cout << "  - Insufficient reverb energy\n";
    }

    return isRealReverb;
}

int main() {
    std::cout << "StudioReverb Room Algorithm Verification Test\n";
    std::cout << "==============================================\n";
    std::cout << "This test verifies that the output is actual reverb,\n";
    std::cout << "not just noise or unprocessed audio.\n\n";

    bool success = testRoomReverb();

    if (!success) {
        std::cout << "\nThe Room reverb is not producing proper reverb output.\n";
        std::cout << "This confirms the issue needs to be fixed.\n";
        return 1;
    }

    return 0;
}
/*
  Test ALL reverb algorithms (Room, Hall, Plate, Early Reflections)
  Verifies each one is producing proper reverb output
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

    void clear() {
        memset(dataL, 0, size * sizeof(float));
        memset(dataR, 0, size * sizeof(float));
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
    bool hasExponentialDecay(int impulsePos, int sampleRate, float* energies = nullptr) {
        const int windowSize = sampleRate / 10; // 100ms windows

        float energy1 = getEnergy(impulsePos + windowSize, windowSize);     // 100-200ms
        float energy2 = getEnergy(impulsePos + windowSize * 3, windowSize); // 300-400ms
        float energy3 = getEnergy(impulsePos + windowSize * 5, windowSize); // 500-600ms
        float energy4 = getEnergy(impulsePos + windowSize * 8, windowSize); // 800-900ms

        if (energies) {
            energies[0] = energy1;
            energies[1] = energy2;
            energies[2] = energy3;
            energies[3] = energy4;
        }

        // Check for decay pattern
        bool isDecaying = (energy1 > energy2) && (energy2 > energy3) && (energy3 > energy4);
        bool hasSignificantEnergy = energy1 > 0.0001f;

        return isDecaying && hasSignificantEnergy;
    }

    // Check stereo decorrelation
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
        return 1.0f;
    }

    // Count zero crossings
    int countZeroCrossings(int start, int length) {
        int crossings = 0;
        int end = std::min(start + length - 1, size - 1);

        for (int i = start; i < end; i++) {
            if ((dataL[i] * dataL[i+1]) < 0) crossings++;
            if ((dataR[i] * dataR[i+1]) < 0) crossings++;
        }
        return crossings / 2;
    }
};

// Include freeverb headers
#include "freeverb/progenitor2.hpp"
#include "freeverb/zrev2.hpp"
#include "freeverb/nrev.hpp"
#include "freeverb/nrevb.hpp"
#include "freeverb/strev.hpp"
#include "freeverb/earlyref.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

void testAlgorithm(const char* name, int algIndex) {
    const int SAMPLE_RATE = 44100;
    const int TEST_SIZE = SAMPLE_RATE * 3;
    const int IMPULSE_POS = SAMPLE_RATE / 10;

    std::cout << "\n================================================\n";
    std::cout << "Testing: " << name << " Algorithm (Index " << algIndex << ")\n";
    std::cout << "================================================\n";

    TestBuffer input(TEST_SIZE);
    TestBuffer output(TEST_SIZE);
    input.addImpulse(IMPULSE_POS);

    // Process based on algorithm type
    if (algIndex == 0) {  // Room
        std::cout << "Initializing Room reverb (progenitor2)...\n";
        fv3::progenitor2_f room;
        room.setSampleRate(SAMPLE_RATE);
        room.setReverbType(FV3_REVTYPE_PROG2);
        room.setwet(0);
        room.setdryr(-70);
        room.setwidth(1.0);
        room.setrt60(2.0f);
        room.setRSFactor(4.0f);
        room.setidiffusion1(0.75f);
        room.setodiffusion1(0.75f);
        room.setdamp(10000.0f);
        room.setmodulationnoise1(0.09f);
        room.setmodulationnoise2(0.06f);
        room.setcrossfeed(0.4f);
        room.setbassap(150, 4);

        room.processreplace(input.dataL, input.dataR,
                          output.dataL, output.dataR, TEST_SIZE);

    } else if (algIndex == 1) {  // Hall
        std::cout << "Initializing Hall reverb (zrev2)...\n";
        fv3::zrev2_f hall;
        hall.setSampleRate(SAMPLE_RATE);
        hall.setwet(0);
        hall.setdryr(-70);
        hall.setwidth(1.0);
        hall.setrt60(2.0f);
        hall.setRSFactor(2.5f);
        hall.setidiffusion1(0.75f);
        hall.setapfeedback(0.75f);

        hall.processreplace(input.dataL, input.dataR,
                          output.dataL, output.dataR, TEST_SIZE);

    } else if (algIndex == 2) {  // Plate
        std::cout << "Initializing Plate reverb (nrevb)...\n";
        fv3::nrevb_f plate;
        plate.setSampleRate(SAMPLE_RATE);
        plate.setwet(0);
        plate.setdryr(-70);
        plate.setwidth(1.0);
        plate.setrt60(2.0f);

        plate.processreplace(input.dataL, input.dataR,
                           output.dataL, output.dataR, TEST_SIZE);

    } else if (algIndex == 3) {  // Early Reflections
        std::cout << "Initializing Early Reflections only...\n";
        fv3::earlyref_f early;
        early.setSampleRate(SAMPLE_RATE);
        early.loadPresetReflection(FV3_EARLYREF_PRESET_1);
        early.setwet(0);
        early.setdryr(-70);
        early.setwidth(0.8f);
        early.setLRDelay(0.3f);

        early.processreplace(input.dataL, input.dataR,
                           output.dataL, output.dataR, TEST_SIZE);
    }

    std::cout << "Processing complete. Analyzing output...\n";

    // Analysis
    float energies[4];
    bool hasDecay = output.hasExponentialDecay(IMPULSE_POS, SAMPLE_RATE, energies);

    std::cout << "\n1. Energy measurements:\n";
    std::cout << "   100-200ms: " << std::scientific << std::setprecision(4) << energies[0] << "\n";
    std::cout << "   300-400ms: " << energies[1] << "\n";
    std::cout << "   500-600ms: " << energies[2] << "\n";
    std::cout << "   800-900ms: " << energies[3] << "\n";
    std::cout << "   Decay pattern: " << (hasDecay ? "✓ YES" : "✗ NO") << "\n";

    float correlation = output.getStereoCorrelation(IMPULSE_POS + SAMPLE_RATE/2, SAMPLE_RATE);
    std::cout << "\n2. Stereo correlation: " << std::fixed << std::setprecision(3) << correlation;
    std::cout << " (" << (correlation < 0.8f ? "✓ Decorrelated" : "✗ Too correlated") << ")\n";

    int crossings = output.countZeroCrossings(IMPULSE_POS + SAMPLE_RATE/4, SAMPLE_RATE/4);
    std::cout << "\n3. Zero crossings (250ms): " << crossings;
    std::cout << " (" << (crossings > 1000 ? "✓ Dense" : "✗ Sparse") << ")\n";

    float totalEnergy = output.getEnergy(IMPULSE_POS + SAMPLE_RATE/2, SAMPLE_RATE);
    std::cout << "\n4. Total reverb energy: " << std::scientific << totalEnergy;
    std::cout << " (" << (totalEnergy > 0.0001f ? "✓ Has energy" : "✗ No energy") << ")\n";

    // Overall verdict
    bool isReverb = (algIndex == 3) ?
                   (totalEnergy > 0.0001f && crossings > 500) :  // Early reflections only need energy and some density
                   (hasDecay && correlation < 0.8f && crossings > 1000 && totalEnergy > 0.0001f);

    std::cout << "\n=== VERDICT: ";
    if (isReverb) {
        std::cout << "✓ " << name << " is working correctly! ===\n";
    } else {
        std::cout << "✗ " << name << " is NOT working properly! ===\n";
    }
}

int main() {
    std::cout << "Complete Reverb Algorithm Test Suite\n";
    std::cout << "=====================================\n";
    std::cout << "Testing all 4 reverb algorithms to verify proper operation.\n";

    testAlgorithm("Room", 0);
    testAlgorithm("Hall", 1);
    testAlgorithm("Plate", 2);
    testAlgorithm("Early Reflections", 3);

    std::cout << "\n=====================================\n";
    std::cout << "Test complete. Check results above.\n";
    std::cout << "=====================================\n";

    return 0;
}
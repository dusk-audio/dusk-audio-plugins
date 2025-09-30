/*
  Comprehensive test for ALL signal paths in StudioReverb
  Tests dry, early, and late signals separately and mixed
  Exactly mimics plugin behavior
*/

#include <iostream>
#include <cmath>
#include <cstring>
#include <iomanip>

#define LIBFV3_FLOAT

// Include freeverb headers
#include "freeverb/progenitor2.hpp"
#include "freeverb/earlyref.hpp"
#include "freeverb/biquad.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

void printSignalAnalysis(const char* name, float* bufferL, float* bufferR, int startSample, int numSamples, int totalSize) {
    float energy = 0;
    float peak = 0;
    int zeroCrossings = 0;

    for (int i = startSample; i < startSample + numSamples && i < totalSize; i++) {
        float magL = std::abs(bufferL[i]);
        float magR = std::abs(bufferR[i]);
        energy += bufferL[i]*bufferL[i] + bufferR[i]*bufferR[i];

        float mag = magL + magR;
        if (mag > peak) peak = mag;

        if (i > startSample && i < totalSize - 1) {
            if (bufferL[i] * bufferL[i+1] < 0) zeroCrossings++;
            if (bufferR[i] * bufferR[i+1] < 0) zeroCrossings++;
        }
    }

    energy /= 2.0f; // Average L and R

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  " << name << ":\n";
    std::cout << "    Energy: " << energy;
    if (energy > 0.0001f) std::cout << " ✓";
    else std::cout << " ✗ NO SIGNAL";
    std::cout << "\n";
    std::cout << "    Peak: " << peak << "\n";
    std::cout << "    Zero crossings: " << zeroCrossings << "\n";
}

void testRoomSignalPaths() {
    const int SAMPLE_RATE = 44100;
    const int BUFFER_SIZE = 256;  // Dragonfly's buffer size
    const int TEST_SIZE = SAMPLE_RATE * 2;
    const int IMPULSE_POS = 1000;

    std::cout << "\n========================================\n";
    std::cout << "Testing Room Reverb Signal Paths\n";
    std::cout << "========================================\n\n";

    // Create main buffers
    float* inputL = new float[TEST_SIZE];
    float* inputR = new float[TEST_SIZE];
    memset(inputL, 0, TEST_SIZE * sizeof(float));
    memset(inputR, 0, TEST_SIZE * sizeof(float));

    // Add test impulse
    inputL[IMPULSE_POS] = 1.0f;
    inputR[IMPULSE_POS] = 1.0f;

    // Create processing buffers (like Dragonfly)
    float filteredInputBuffer[2][BUFFER_SIZE];
    float earlyOutBuffer[2][BUFFER_SIZE];
    float lateInBuffer[2][BUFFER_SIZE];
    float lateOutBuffer[2][BUFFER_SIZE];

    // Create full-length output buffers for accumulation
    float* earlyOutputL = new float[TEST_SIZE];
    float* earlyOutputR = new float[TEST_SIZE];
    float* lateOutputL = new float[TEST_SIZE];
    float* lateOutputR = new float[TEST_SIZE];
    memset(earlyOutputL, 0, TEST_SIZE * sizeof(float));
    memset(earlyOutputR, 0, TEST_SIZE * sizeof(float));
    memset(lateOutputL, 0, TEST_SIZE * sizeof(float));
    memset(lateOutputR, 0, TEST_SIZE * sizeof(float));

    // Initialize filters (like Dragonfly Room)
    fv3::biquad_f input_hpf_0, input_hpf_1;
    fv3::biquad_f input_lpf_0, input_lpf_1;

    // Set up high-pass filters (fc, bw, fs, mode)
    input_hpf_0.setHPF_RBJ(10.0f, 0.7071f, SAMPLE_RATE, 0);
    input_hpf_1.setHPF_RBJ(10.0f, 0.7071f, SAMPLE_RATE, 0);
    // Set up low-pass filters (fc, bw, fs, mode)
    input_lpf_0.setLPF_RBJ(16000.0f, 0.7071f, SAMPLE_RATE, 0);
    input_lpf_1.setLPF_RBJ(16000.0f, 0.7071f, SAMPLE_RATE, 0);

    // Initialize Early Reflections EXACTLY like Dragonfly Room
    std::cout << "1. Initializing Early Reflections (exactly like Dragonfly):\n";
    fv3::earlyref_f early;
    early.loadPresetReflection(FV3_EARLYREF_PRESET_1);
    early.setMuteOnChange(false);
    early.setdryr(0);    // CRITICAL: Dragonfly uses 0, not -70
    early.setwet(0);     // 0dB
    early.setwidth(0.8); // Dragonfly Room uses 0.8
    early.setLRDelay(0.3);
    early.setLRCrossApFreq(750, 4);
    early.setDiffusionApFreq(150, 4);
    early.setSampleRate(SAMPLE_RATE);

    std::cout << "  early.getdryr() = " << early.getdryr() << " dB\n";
    std::cout << "  early.getwet() = " << early.getwet() << " dB\n";
    std::cout << "  early.getwidth() = " << early.getwidth() << "\n\n";

    // Initialize Room (late) reverb EXACTLY like Dragonfly Room
    std::cout << "2. Initializing Room/Late Reverb (exactly like Dragonfly):\n";
    fv3::progenitor2_f room;
    room.setMuteOnChange(false);
    room.setwet(0);      // 0dB
    room.setdryr(0);     // CRITICAL: Dragonfly uses 0, not -70
    room.setwidth(1.0);
    room.setSampleRate(SAMPLE_RATE);

    // Set Room parameters
    room.setrt60(2.0f);
    room.setRSFactor(30.0f / 10.0f);  // size / 10.0 like Dragonfly
    room.setidiffusion1(0.75f);
    room.setodiffusion1(0.75f);
    room.setdamp(10000.0f);
    room.setdamp2(10000.0f);
    room.setbassap(150, 4);
    room.setmodulationnoise1(0.09f);
    room.setmodulationnoise2(0.06f);
    room.setcrossfeed(0.4f);
    room.setspin(0.5f);
    room.setspin2(0.5f);
    room.setwander(0.25f);
    room.setwander2(0.25f);

    std::cout << "  room.getdryr() = " << room.getdryr() << " dB\n";
    std::cout << "  room.getwet() = " << room.getwet() << " dB\n";
    std::cout << "  room.getwidth() = " << room.getwidth() << "\n\n";

    // Process in chunks like the plugin does
    std::cout << "3. Processing audio in " << BUFFER_SIZE << "-sample chunks...\n\n";

    float earlySend = 0.2f; // Dragonfly default
    int samplesProcessed = 0;

    while (samplesProcessed < TEST_SIZE) {
        int samplesToProcess = std::min(BUFFER_SIZE, TEST_SIZE - samplesProcessed);

        // Clear buffers
        memset(filteredInputBuffer[0], 0, sizeof(float) * samplesToProcess);
        memset(filteredInputBuffer[1], 0, sizeof(float) * samplesToProcess);
        memset(earlyOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        memset(earlyOutBuffer[1], 0, sizeof(float) * samplesToProcess);
        memset(lateInBuffer[0], 0, sizeof(float) * samplesToProcess);
        memset(lateInBuffer[1], 0, sizeof(float) * samplesToProcess);
        memset(lateOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        memset(lateOutBuffer[1], 0, sizeof(float) * samplesToProcess);

        // Filter input (like Dragonfly Room does)
        for (int i = 0; i < samplesToProcess; i++) {
            filteredInputBuffer[0][i] = input_lpf_0.process(
                input_hpf_0.process(inputL[samplesProcessed + i])
            );
            filteredInputBuffer[1][i] = input_lpf_1.process(
                input_hpf_1.process(inputR[samplesProcessed + i])
            );
        }

        // Process early reflections with FILTERED input
        early.processreplace(
            filteredInputBuffer[0],
            filteredInputBuffer[1],
            earlyOutBuffer[0],
            earlyOutBuffer[1],
            samplesToProcess
        );

        // Copy early output to full buffer
        for (int i = 0; i < samplesToProcess; i++) {
            earlyOutputL[samplesProcessed + i] = earlyOutBuffer[0][i];
            earlyOutputR[samplesProcessed + i] = earlyOutBuffer[1][i];
        }

        // Prepare late reverb input (filtered input + early send)
        for (int i = 0; i < samplesToProcess; i++) {
            lateInBuffer[0][i] = filteredInputBuffer[0][i] +
                                  earlyOutBuffer[0][i] * earlySend;
            lateInBuffer[1][i] = filteredInputBuffer[1][i] +
                                  earlyOutBuffer[1][i] * earlySend;
        }

        // Process late reverb
        room.processreplace(
            lateInBuffer[0],
            lateInBuffer[1],
            lateOutBuffer[0],
            lateOutBuffer[1],
            samplesToProcess
        );

        // Copy late output to full buffer
        for (int i = 0; i < samplesToProcess; i++) {
            lateOutputL[samplesProcessed + i] = lateOutBuffer[0][i];
            lateOutputR[samplesProcessed + i] = lateOutBuffer[1][i];
        }

        samplesProcessed += samplesToProcess;
    }

    std::cout << "4. Analyzing Signal Paths:\n";
    std::cout << "==========================\n\n";

    std::cout << "A. DRY SIGNAL (input):\n";
    printSignalAnalysis("Dry", inputL, inputR, IMPULSE_POS, SAMPLE_RATE/2, TEST_SIZE);

    std::cout << "\nB. EARLY REFLECTIONS OUTPUT:\n";
    printSignalAnalysis("Early", earlyOutputL, earlyOutputR, IMPULSE_POS, SAMPLE_RATE/2, TEST_SIZE);

    std::cout << "\nC. LATE REVERB OUTPUT:\n";
    printSignalAnalysis("Late", lateOutputL, lateOutputR, IMPULSE_POS, SAMPLE_RATE/2, TEST_SIZE);

    // Test different mix scenarios
    std::cout << "\n5. Testing Mix Scenarios:\n";
    std::cout << "=========================\n\n";

    float* mixedL = new float[TEST_SIZE];
    float* mixedR = new float[TEST_SIZE];

    // Scenario 1: Only Late (dry=0, early=0, late=1)
    std::cout << "Scenario 1: Dry=0%, Early=0%, Late=100%\n";
    for (int i = 0; i < TEST_SIZE; i++) {
        mixedL[i] = lateOutputL[i];
        mixedR[i] = lateOutputR[i];
    }
    printSignalAnalysis("Mixed", mixedL, mixedR, IMPULSE_POS, SAMPLE_RATE/2, TEST_SIZE);

    // Scenario 2: Only Early (dry=0, early=1, late=0)
    std::cout << "\nScenario 2: Dry=0%, Early=100%, Late=0%\n";
    for (int i = 0; i < TEST_SIZE; i++) {
        mixedL[i] = earlyOutputL[i];
        mixedR[i] = earlyOutputR[i];
    }
    printSignalAnalysis("Mixed", mixedL, mixedR, IMPULSE_POS, SAMPLE_RATE/2, TEST_SIZE);

    // Scenario 3: Early + Late (dry=0, early=0.5, late=0.5)
    std::cout << "\nScenario 3: Dry=0%, Early=50%, Late=50%\n";
    for (int i = 0; i < TEST_SIZE; i++) {
        mixedL[i] = earlyOutputL[i] * 0.5f + lateOutputL[i] * 0.5f;
        mixedR[i] = earlyOutputR[i] * 0.5f + lateOutputR[i] * 0.5f;
    }
    printSignalAnalysis("Mixed", mixedL, mixedR, IMPULSE_POS, SAMPLE_RATE/2, TEST_SIZE);

    // Final verdict
    std::cout << "\n========================================\n";
    std::cout << "FINAL VERDICT:\n";
    std::cout << "========================================\n";

    // Check if signals are present
    float earlyEnergy = 0, lateEnergy = 0;
    for (int i = IMPULSE_POS; i < IMPULSE_POS + SAMPLE_RATE/2; i++) {
        earlyEnergy += earlyOutputL[i]*earlyOutputL[i] + earlyOutputR[i]*earlyOutputR[i];
        lateEnergy += lateOutputL[i]*lateOutputL[i] + lateOutputR[i]*lateOutputR[i];
    }

    bool earlyWorks = earlyEnergy > 0.001f;
    bool lateWorks = lateEnergy > 0.001f;

    if (earlyWorks && lateWorks) {
        std::cout << "✓ SUCCESS: Both Early and Late reverb are producing output!\n";
        std::cout << "  Room reverb should work correctly in the plugin.\n";
    } else {
        if (!earlyWorks) {
            std::cout << "✗ PROBLEM: Early reflections not producing output!\n";
        }
        if (!lateWorks) {
            std::cout << "✗ PROBLEM: Late reverb not producing output!\n";
            std::cout << "  Check: room.getdryr() = " << room.getdryr() << " (should be 0)\n";
            std::cout << "  Check: room.getwet() = " << room.getwet() << " (should be 0)\n";
        }
    }

    std::cout << "========================================\n\n";

    // Cleanup
    delete[] inputL;
    delete[] inputR;
    delete[] earlyOutputL;
    delete[] earlyOutputR;
    delete[] lateOutputL;
    delete[] lateOutputR;
    delete[] mixedL;
    delete[] mixedR;
}

int main() {
    std::cout << "StudioReverb Complete Signal Path Test\n";
    std::cout << "=======================================\n";
    std::cout << "This test validates that all signal paths work correctly.\n";

    testRoomSignalPaths();

    return 0;
}
/*
  Test EXACTLY what Dragonfly does vs what we do
  Focus on understanding why late signal sounds dry
*/

#include <iostream>
#include <cmath>
#include <cstring>
#include <iomanip>

#define LIBFV3_FLOAT

#include "freeverb/progenitor2.hpp"
#include "freeverb/earlyref.hpp"
#include "freeverb/biquad.hpp"
#include "freeverb/fv3_type_float.h"
#include "freeverb/fv3_defs.h"

int main() {
    const int SAMPLE_RATE = 44100;
    const int BUFFER_SIZE = 256;
    const int TEST_BUFFERS = 50; // Process 50 buffers

    std::cout << "Testing Dragonfly EXACT Implementation\n";
    std::cout << "=======================================\n\n";

    // Create Dragonfly-style buffers
    float filtered_input_buffer[2][BUFFER_SIZE];
    float early_out_buffer[2][BUFFER_SIZE];
    float late_in_buffer[2][BUFFER_SIZE];
    float late_out_buffer[2][BUFFER_SIZE];

    // Initialize filters EXACTLY like Dragonfly
    fv3::biquad_f input_hpf[2];
    fv3::biquad_f input_lpf[2];

    // These would be set from parameters
    float hpf_freq = 10.0f;
    float lpf_freq = 16000.0f;

    input_hpf[0].setHPF_RBJ(hpf_freq, 0.7071f, SAMPLE_RATE, 0);
    input_hpf[1].setHPF_RBJ(hpf_freq, 0.7071f, SAMPLE_RATE, 0);
    input_lpf[0].setLPF_RBJ(lpf_freq, 0.7071f, SAMPLE_RATE, 0);
    input_lpf[1].setLPF_RBJ(lpf_freq, 0.7071f, SAMPLE_RATE, 0);

    // Initialize Early EXACTLY like Dragonfly
    fv3::earlyref_f early;
    early.loadPresetReflection(FV3_EARLYREF_PRESET_1);
    early.setMuteOnChange(false);
    early.setdryr(0); // mute dry signal - Dragonfly comment
    early.setwet(0); // 0dB
    early.setwidth(0.8);
    early.setLRDelay(0.3);
    early.setLRCrossApFreq(750, 4);
    early.setDiffusionApFreq(150, 4);
    early.setSampleRate(SAMPLE_RATE);
    float early_send = 0.20f;

    // Initialize Late EXACTLY like Dragonfly Room
    fv3::progenitor2_f late;
    late.setMuteOnChange(false);
    late.setwet(0); // 0dB
    late.setdryr(0); // mute dry signal - Dragonfly comment
    late.setwidth(1.0);
    late.setSampleRate(SAMPLE_RATE);

    // Set Room parameters (these would come from UI)
    float size = 30.0f;
    float decay = 2.0f;
    float predelay = 0.0f;
    float diffusion = 75.0f;
    float spin = 1.0f;
    float wander = 15.0f;
    float high_cut = 10000.0f;
    float high_xover = 8000.0f;
    float low_mult = 1.0f;
    float low_xover = 200.0f;
    float width = 100.0f;

    // Apply parameters EXACTLY like Dragonfly's run() method
    late.setRSFactor(size / 10.0f);
    late.setrt60(decay);
    late.setidiffusion1(diffusion / 100.0f);
    late.setodiffusion1(diffusion / 100.0f);

    // Bass boost formula from Dragonfly
    float boost = low_mult / 20.0f / std::pow(decay, 1.5f) * (size / 10.0f);
    late.setbassboost(boost);

    // Spin formulas from Dragonfly
    late.setspin(spin);
    late.setspin2(std::sqrt(100.0f - (10.0f - spin) * (10.0f - spin)) / 2.0f);

    // Wander formulas from Dragonfly
    late.setwander(wander / 200.0f + 0.1f);
    late.setwander2(wander / 200.0f + 0.1f);

    // Damping
    late.setdamp(high_cut);
    late.setoutputdamp(high_cut);
    late.setdamp2(low_xover);

    late.setPreDelay(predelay);
    late.setwidth(width / 100.0f);

    std::cout << "Settings:\n";
    std::cout << "  early.getdryr() = " << early.getdryr() << " dB\n";
    std::cout << "  early.getwet() = " << early.getwet() << " dB\n";
    std::cout << "  late.getdryr() = " << late.getdryr() << " dB\n";
    std::cout << "  late.getwet() = " << late.getwet() << " dB\n";
    std::cout << "  late.getrt60() = " << late.getrt60() << " seconds\n\n";

    // Process multiple buffers
    float totalInputEnergy = 0;
    float totalEarlyEnergy = 0;
    float totalLateEnergy = 0;

    for (int buf = 0; buf < TEST_BUFFERS; buf++) {
        // Clear all buffers
        memset(filtered_input_buffer[0], 0, BUFFER_SIZE * sizeof(float));
        memset(filtered_input_buffer[1], 0, BUFFER_SIZE * sizeof(float));
        memset(early_out_buffer[0], 0, BUFFER_SIZE * sizeof(float));
        memset(early_out_buffer[1], 0, BUFFER_SIZE * sizeof(float));
        memset(late_in_buffer[0], 0, BUFFER_SIZE * sizeof(float));
        memset(late_in_buffer[1], 0, BUFFER_SIZE * sizeof(float));
        memset(late_out_buffer[0], 0, BUFFER_SIZE * sizeof(float));
        memset(late_out_buffer[1], 0, BUFFER_SIZE * sizeof(float));

        // Create test input (impulse in first buffer only)
        float input[2][BUFFER_SIZE];
        memset(input[0], 0, BUFFER_SIZE * sizeof(float));
        memset(input[1], 0, BUFFER_SIZE * sizeof(float));

        if (buf == 0) {
            input[0][10] = 1.0f;
            input[1][10] = 1.0f;
        }

        // Step 1: Filter input (like Dragonfly)
        for (int i = 0; i < BUFFER_SIZE; i++) {
            filtered_input_buffer[0][i] = input_lpf[0].process(
                input_hpf[0].process(input[0][i])
            );
            filtered_input_buffer[1][i] = input_lpf[1].process(
                input_hpf[1].process(input[1][i])
            );
        }

        // Step 2: Process early reflections
        early.processreplace(
            filtered_input_buffer[0],
            filtered_input_buffer[1],
            early_out_buffer[0],
            early_out_buffer[1],
            BUFFER_SIZE
        );

        // Step 3: Prepare late input (filtered + early send)
        for (int i = 0; i < BUFFER_SIZE; i++) {
            late_in_buffer[0][i] = filtered_input_buffer[0][i] +
                                   early_out_buffer[0][i] * early_send;
            late_in_buffer[1][i] = filtered_input_buffer[1][i] +
                                   early_out_buffer[1][i] * early_send;
        }

        // Step 4: Process late reverb
        late.processreplace(
            late_in_buffer[0],
            late_in_buffer[1],
            late_out_buffer[0],
            late_out_buffer[1],
            BUFFER_SIZE
        );

        // Analyze
        for (int i = 0; i < BUFFER_SIZE; i++) {
            totalInputEnergy += input[0][i]*input[0][i] + input[1][i]*input[1][i];
            totalEarlyEnergy += early_out_buffer[0][i]*early_out_buffer[0][i] +
                               early_out_buffer[1][i]*early_out_buffer[1][i];
            totalLateEnergy += late_out_buffer[0][i]*late_out_buffer[0][i] +
                              late_out_buffer[1][i]*late_out_buffer[1][i];
        }

        // Print first few buffers
        if (buf < 5) {
            float bufEnergy = 0;
            for (int i = 0; i < BUFFER_SIZE; i++) {
                bufEnergy += late_out_buffer[0][i]*late_out_buffer[0][i] +
                            late_out_buffer[1][i]*late_out_buffer[1][i];
            }
            std::cout << "Buffer " << buf << " late output energy: " << bufEnergy;

            // Check if it looks like dry pass-through
            if (buf == 0) {
                float impulseResponse = std::abs(late_out_buffer[0][10]) +
                                       std::abs(late_out_buffer[1][10]);
                std::cout << ", response at impulse: " << impulseResponse;
                if (impulseResponse > 1.8f && impulseResponse < 2.2f) {
                    std::cout << " (LOOKS LIKE DRY!)";
                }
            }
            std::cout << "\n";
        }
    }

    std::cout << "\nTotal Energy Analysis:\n";
    std::cout << "  Input: " << totalInputEnergy << "\n";
    std::cout << "  Early: " << totalEarlyEnergy << "\n";
    std::cout << "  Late: " << totalLateEnergy << "\n";

    // Final verdict
    std::cout << "\nVERDICT:\n";
    if (totalLateEnergy < totalInputEnergy * 0.1f) {
        std::cout << "✗ Late reverb producing very low output\n";
    } else if (totalLateEnergy > totalInputEnergy * 50) {
        std::cout << "✓ Late reverb producing reverb tail\n";
    } else {
        std::cout << "⚠ Late reverb output unclear\n";
    }

    // Now test with setdryr(-70)
    std::cout << "\n========================================\n";
    std::cout << "Testing with setdryr(-70) instead\n";
    std::cout << "========================================\n";

    late.setdryr(-70); // Try with -70
    std::cout << "late.getdryr() = " << late.getdryr() << " dB\n";

    float altLateEnergy = 0;

    // Process one buffer with impulse
    memset(late_in_buffer[0], 0, BUFFER_SIZE * sizeof(float));
    memset(late_in_buffer[1], 0, BUFFER_SIZE * sizeof(float));
    late_in_buffer[0][10] = 1.0f;
    late_in_buffer[1][10] = 1.0f;

    memset(late_out_buffer[0], 0, BUFFER_SIZE * sizeof(float));
    memset(late_out_buffer[1], 0, BUFFER_SIZE * sizeof(float));

    late.processreplace(
        late_in_buffer[0],
        late_in_buffer[1],
        late_out_buffer[0],
        late_out_buffer[1],
        BUFFER_SIZE
    );

    for (int i = 0; i < BUFFER_SIZE; i++) {
        altLateEnergy += late_out_buffer[0][i]*late_out_buffer[0][i] +
                        late_out_buffer[1][i]*late_out_buffer[1][i];
    }

    std::cout << "Late output energy with -70: " << altLateEnergy << "\n";
    float impulseAt70 = std::abs(late_out_buffer[0][10]) +
                        std::abs(late_out_buffer[1][10]);
    std::cout << "Response at impulse: " << impulseAt70 << "\n";

    if (altLateEnergy > totalLateEnergy * 10) {
        std::cout << "✓ setdryr(-70) produces MORE reverb output!\n";
    }

    return 0;
}
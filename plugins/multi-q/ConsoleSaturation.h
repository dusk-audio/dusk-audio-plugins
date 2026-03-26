/*
  ==============================================================================

    ConsoleSaturation.h

    SSL 4000 console channel emulation with two voicings:
    - E-Series (Brown knobs): Fixed-Q EQ, dbx 202C VCA, grittier/more aggressive
    - G-Series (Black knobs): Proportional-Q EQ, dbx 2150 VCA, smoother/cleaner

    SSL 4000 is a transformerless channel design. Saturation character comes from:
    - TL072 op-amp stages (symmetric clipping → odd harmonics H3, H5, H7)
    - VCA coloration (signal-dependent dynamic distortion)
    - Slew-rate limiting at HF (TL072's 13 V/µs creates transient intermodulation)

    Unlike Neve (transformer-coupled, even-harmonic, Class-A), SSL is:
    - Transformerless per-channel (only mix bus has transformers)
    - Odd-harmonic dominant (Class-AB IC topology)
    - Cleaner at nominal levels (THD ~0.005%)
    - More distortion at HF than LF (slew-rate vs transformer physics)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <random>
#include "SafeFloat.h"

class ConsoleSaturation
{
public:
    enum class ConsoleType
    {
        ESeries,    // SSL 4000 E-Series (Brown knobs) - grittier, dbx 202C VCA
        GSeries     // SSL 4000 G-Series (Black knobs) - smoother, dbx 2150 VCA
    };

    // Constructor with optional seed for reproducible tests
    explicit ConsoleSaturation(unsigned int seed = 0)
    {
        // Fixed component tolerance values (deterministic for reproducible results)
        opAmpTolerance = 1.02f;
        vcaTolerance = 0.97f;

        unsigned int actualSeed = (seed != 0) ? seed
            : static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(this) ^ instanceCounter++);
        noiseGen = std::mt19937(actualSeed);
        noiseDist = std::uniform_real_distribution<float>(-1.0f, 1.0f);
    }

    void setConsoleType(ConsoleType type)
    {
        consoleType = type;
    }

    void setSampleRate(double newSampleRate)
    {
        sampleRate = newSampleRate;

        // DC blocker at ~5Hz
        const float cutoffFreq = 5.0f;
        const float RC = 1.0f / (juce::MathConstants<float>::twoPi * cutoffFreq);
        dcBlockerCoeff = RC / (RC + 1.0f / static_cast<float>(sampleRate));

        // HF content estimator for slew-rate modeling
        float hfCutoff = 300.0f;
        float hfOmega = juce::MathConstants<float>::twoPi * hfCutoff / static_cast<float>(sampleRate);
        hfEstimateCoeff = hfOmega / (hfOmega + 1.0f);
    }

    void reset()
    {
        dcBlockerX1_L = dcBlockerY1_L = 0.0f;
        dcBlockerX1_R = dcBlockerY1_R = 0.0f;
        lastSample_L = lastSample_R = 0.0f;
        highFreqEstimate_L = highFreqEstimate_R = 0.0f;
        slewState_L = slewState_R = 0.0f;
    }

    // Main processing function with drive amount (0.0 to 1.0)
    float processSample(float input, float drive, bool isLeftChannel)
    {
        juce::ScopedNoDenormals noDenormals;

        if (!safeIsFinite(input))
            return 0.0f;

        if (drive < 0.001f)
            return input;

        // Estimate HF content for slew-rate limiting
        float highFreqContent = estimateHighFrequencyContent(input, isLeftChannel);

        // Stage 1: TL072 op-amp gain stage
        // SSL's primary signal path — symmetric clipping, odd harmonics
        float opAmpOut = processTL072Stage(input, drive * opAmpTolerance, highFreqContent, isLeftChannel);

        // Stage 2: VCA coloration
        // dbx 202C (E-Series) or dbx 2150 (G-Series)
        float output = processVCAStage(opAmpOut, drive * vcaTolerance);

        // Console noise floor (-94dB for SSL, cleaner than Neve)
        float noiseLevel = 0.00002f * (1.0f + drive * 0.3f);
        output += noiseDist(noiseGen) * noiseLevel;

        // DC blocking
        output = processDCBlocker(output, isLeftChannel);

        // Output gain compensation
        float gainCompensation = 1.0f / (1.0f + drive * 0.15f);
        output *= gainCompensation;

        // Mix dry/wet based on drive
        float wetMix = juce::jlimit(0.0f, 1.0f, drive * 1.4f);
        float result = input * (1.0f - wetMix) + output * wetMix;

        if (!safeIsFinite(result))
            return input;

        return result;
    }

private:
    static inline std::atomic<unsigned int> instanceCounter{0};

    ConsoleType consoleType = ConsoleType::ESeries;
    double sampleRate = 44100.0;

    // DC blocker state
    float dcBlockerX1_L = 0.0f, dcBlockerY1_L = 0.0f;
    float dcBlockerX1_R = 0.0f, dcBlockerY1_R = 0.0f;
    float dcBlockerCoeff = 0.999f;

    // HF content estimation
    float lastSample_L = 0.0f, lastSample_R = 0.0f;
    float highFreqEstimate_L = 0.0f, highFreqEstimate_R = 0.0f;
    float hfEstimateCoeff = 0.04f;

    // Slew-rate limiting state
    float slewState_L = 0.0f, slewState_R = 0.0f;

    float highFreqScale = 3.0f;

    // Component tolerances
    float opAmpTolerance = 1.0f;
    float vcaTolerance = 1.0f;

    // Noise generation
    std::mt19937 noiseGen;
    std::uniform_real_distribution<float> noiseDist;

    // Estimate high-frequency content using differentiator
    float estimateHighFrequencyContent(float input, bool isLeftChannel)
    {
        float& lastSample = isLeftChannel ? lastSample_L : lastSample_R;
        float& estimate = isLeftChannel ? highFreqEstimate_L : highFreqEstimate_R;

        float difference = std::abs(input - lastSample);
        lastSample = input;

        estimate = estimate * (1.0f - hfEstimateCoeff) + difference * hfEstimateCoeff;

        return juce::jlimit(0.0f, 1.0f, estimate * highFreqScale);
    }

    // TL072 op-amp stage
    // JFET-input, Class-AB output. Key characteristics:
    // - Symmetric clipping against ±15V rails → odd harmonics (H3, H5, H7)
    // - Very clean in linear region (THD < 0.003%)
    // - Slew rate 13 V/µs causes TIM distortion on fast HF transients
    // - Open-loop gain decreases at HF → slightly more distortion above 10kHz
    float processTL072Stage(float input, float drive, float hfContent, bool isLeftChannel)
    {
        // SSL channels operate at high headroom — gain stage is clean until pushed.
        // Use an exponential curve so the knob gives usable range across its travel.
        // G-Series has higher headroom (cleaner VCA, better components) so it clips later.
        float maxGain = (consoleType == ConsoleType::ESeries) ? 11.0f : 8.0f;
        const float gainDrive = 1.0f + std::pow(drive, 0.6f) * maxGain;
        float driven = input * gainDrive;

        // TL072 slew-rate limiting: HF transients are softened
        // Real TL072 slew rate is 13 V/µs. At higher frequencies and levels,
        // the op-amp can't follow the signal, creating intermodulation distortion.
        // This adds subtle HF "edge" that's characteristic of SSL
        float& slewState = isLeftChannel ? slewState_L : slewState_R;
        float slewLimited = driven;
        if (hfContent > 0.1f && std::abs(driven) > 0.3f)
        {
            float slewAmount = hfContent * drive * 0.15f;
            float delta = driven - slewState;
            float maxSlew = (1.0f - slewAmount) + 0.01f; // prevent zero
            if (std::abs(delta) > maxSlew)
                slewLimited = slewState + std::copysign(maxSlew, delta);
            slewState = slewLimited;
        }
        else
        {
            slewState = driven;
        }

        // Symmetric soft-clip (TL072 clips symmetrically against both rails)
        // Very linear up to high levels, then relatively hard clipping
        float output;
        float abs_x = std::abs(slewLimited);

        if (abs_x < 1.0f)
        {
            // Linear region — SSL is very clean here
            output = slewLimited;
        }
        else if (abs_x < 1.6f)
        {
            // Soft compression approaching rails
            float excess = abs_x - 1.0f;
            float compressed = 1.0f + excess * (1.0f - excess * 0.3f);
            output = std::copysign(compressed, slewLimited);
        }
        else
        {
            // Hard clipping against rails — symmetric
            float excess = abs_x - 1.6f;
            float compressed = 1.4f + std::tanh(excess * 3.0f) * 0.15f;
            output = std::copysign(compressed, slewLimited);
        }

        // Odd-harmonic generation from symmetric clipping
        // SSL's TL072 stages produce predominantly H3, H5
        // E-Series is slightly more aggressive than G-Series
        float clipAmount = juce::jlimit(0.0f, 1.0f, (abs_x - 0.5f) / 1.0f);

        if (clipAmount > 0.0f)
        {
            if (consoleType == ConsoleType::ESeries)
            {
                // E-Series (Brown): more aggressive, grittier character
                // H3 dominant at ~-80dB below fundamental at nominal, rising when pushed
                output += output * output * output * (0.06f * clipAmount);  // H3
                output += output * output * output * output * output * (0.015f * clipAmount);  // H5 (subtle)
            }
            else
            {
                // G-Series (Black): smoother, cleaner
                // Same harmonic types but lower levels
                output += output * output * output * (0.035f * clipAmount);  // H3
                output += output * output * output * output * output * (0.008f * clipAmount);  // H5 (subtle)
            }
        }

        return output / gainDrive;
    }

    // VCA stage (dbx 202C for E-Series, dbx 2150 for G-Series)
    // The VCA is the primary "character" source in SSL consoles.
    // Log-antilog topology creates signal-dependent distortion that
    // varies with level — perceived as "punch" and "attitude".
    float processVCAStage(float input, float drive)
    {
        // VCA distortion is subtle at low drive, increases with level
        // The log-antilog conversion creates asymmetric artifacts
        float abs_x = std::abs(input);

        // VCA distortion threshold and amount differ between E/G
        float vcaThreshold, vcaAmount;
        if (consoleType == ConsoleType::ESeries)
        {
            // dbx 202C: grittier, more distortion (THD ~0.02-0.05%)
            vcaThreshold = 0.2f;
            vcaAmount = drive * 0.04f;
        }
        else
        {
            // dbx 2150: cleaner, smoother (THD ~0.01-0.02%)
            vcaThreshold = 0.3f;
            vcaAmount = drive * 0.02f;
        }

        if (abs_x > vcaThreshold && vcaAmount > 0.001f)
        {
            float excess = (abs_x - vcaThreshold) / (1.0f - vcaThreshold);
            excess = juce::jlimit(0.0f, 1.0f, excess);

            // VCA adds both H2 (from log-antilog asymmetry) and H3 (from clipping)
            // but H3 still dominates overall. The H2 component is what gives
            // the VCA its slight "warmth" compared to pure op-amp distortion.
            float h2 = input * abs_x * (vcaAmount * 0.3f * excess);  // subtle H2
            float h3 = input * input * input * (vcaAmount * excess);  // H3 dominant

            return input + h2 + h3;
        }

        return input;
    }

    // DC blocking filter
    float processDCBlocker(float input, bool isLeftChannel)
    {
        float& x1 = isLeftChannel ? dcBlockerX1_L : dcBlockerX1_R;
        float& y1 = isLeftChannel ? dcBlockerY1_L : dcBlockerY1_R;

        float output = input - x1 + dcBlockerCoeff * y1;
        x1 = input;
        y1 = output;

        return output;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConsoleSaturation)
};

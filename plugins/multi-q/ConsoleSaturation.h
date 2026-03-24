/*
  ==============================================================================

    ConsoleSaturation.h

    British console harmonic emulation with two voicings:
    - E-Series: Warmer character, predominantly 2nd harmonic
    - G-Series: Cleaner/tighter, more 3rd harmonic
    - Op-amp modeling with asymmetric clipping
    - Transformer saturation with asymmetric B-H curve and LF emphasis
    - Output gain compensation to maintain consistent level

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
        ESeries,    // E-Series VE (Brown knobs) - warmer, more 2nd harmonic
        GSeries     // G-Series (Black knobs) - cleaner, more 3rd harmonic
    };

    // Constructor with optional seed for reproducible tests
    // Default uses instance address for unique per-instance noise
    explicit ConsoleSaturation(unsigned int seed = 0)
    {
        // Fixed component tolerance values (deterministic for reproducible results)
        // Simulates typical vintage hardware with slight component variation
        transformerTolerance = 1.02f;
        opAmpTolerance = 0.97f;
        outputTransformerTolerance = 1.01f;

        // Initialize noise generator with unique seed per instance
        // If seed is 0, derive from instance address for unique noise across instances
        // Non-zero seeds allow reproducible results for testing
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

        // Update DC blocker coefficients
        // High-pass at ~5Hz to remove any DC offset from saturation
        const float cutoffFreq = 5.0f;
        const float RC = 1.0f / (juce::MathConstants<float>::twoPi * cutoffFreq);
        dcBlockerCoeff = RC / (RC + 1.0f / static_cast<float>(sampleRate));

        // LF content estimator: 1st-order LPF at ~300Hz
        // Extracts low-frequency envelope for frequency-dependent transformer saturation
        float lfCutoff = 300.0f;
        float lfOmega = juce::MathConstants<float>::twoPi * lfCutoff / static_cast<float>(sampleRate);
        lfEstimateCoeff = lfOmega / (lfOmega + 1.0f);
    }

    void reset()
    {
        dcBlockerX1_L = dcBlockerY1_L = 0.0f;
        dcBlockerX1_R = dcBlockerY1_R = 0.0f;
        lastSample_L = lastSample_R = 0.0f;
        highFreqEstimate_L = highFreqEstimate_R = 0.0f;
        lfEstimate_L = lfEstimate_R = 0.0f;
    }

    // Main processing function with drive amount (0.0 to 1.0)
    float processSample(float input, float drive, bool isLeftChannel)
    {
        // Suppress denormals for performance (prevents CPU spikes on older processors)
        juce::ScopedNoDenormals noDenormals;

        // NaN/Inf protection - return silence (0.0f) if input is invalid
        if (!safeIsFinite(input))
            return 0.0f;

        if (drive < 0.001f)
            return input;

        // Gentle pre-saturation limiting to prevent extreme aliasing
        // This soft clips peaks before they hit the transformer stage
        // Only active at very high levels (>0.95) to maintain console character
        float limited = input;
        float absInput = std::abs(input);
        if (absInput > 0.95f)
        {
            // Soft knee limiting using tanh for smooth transition
            float excess = absInput - 0.95f;
            float compressed = 0.95f + std::tanh(excess * 3.0f) * 0.05f;
            limited = (input > 0.0f) ? compressed : -compressed;
        }

        // Estimate frequency content for frequency-dependent saturation
        float highFreqContent = estimateHighFrequencyContent(limited, isLeftChannel);
        float lfContent = estimateLowFrequencyContent(limited, isLeftChannel);

        // Progressive HF drive reduction for anti-aliasing
        // Real console transformers naturally saturate less at HF
        float hfReduction = highFreqContent * (0.25f + drive * 0.35f);
        float effectiveDrive = drive * (1.0f - hfReduction);

        // LF drive emphasis: transformers saturate MORE at low frequencies
        // due to higher flux density in the iron core at LF.
        // Kept subtle (0.1) because the EQ signal already arrives boosted when
        // the user has raised the low shelf — stacking emphasis on top of a
        // boosted signal caused excessive distortion on bass-heavy material.
        float lfEmphasis = 1.0f + lfContent * drive * 0.1f;

        // Stage 1: Input transformer saturation
        // Console transformer saturation (vintage iron-core characteristics)
        float transformed = processInputTransformer(limited, effectiveDrive * lfEmphasis * transformerTolerance);

        // Stage 2: Op-amp gain stage (NE5534)
        // This is where most of the harmonic coloration happens
        float opAmpOut = processOpAmpStage(transformed, effectiveDrive * opAmpTolerance);

        // Stage 3: Output transformer (if applicable)
        // E-Series has output transformers, G-Series is transformerless
        float output = (consoleType == ConsoleType::ESeries)
            ? processOutputTransformer(opAmpOut, drive * 0.7f * outputTransformerTolerance)
            : opAmpOut;

        // Add console noise floor (-90dB RMS, typical for vintage consoles)
        // Noise increases slightly with drive (like real hardware)
        float noiseLevel = 0.00003162f * (1.0f + drive * 0.5f); // -90dB base, increases with drive
        output += noiseDist(noiseGen) * noiseLevel;

        // DC blocking filter to prevent DC offset buildup
        output = processDCBlocker(output, isLeftChannel);

        // Output gain compensation: saturation increases perceived loudness
        // Compensate to maintain consistent level for fair A/B comparison
        // Derived empirically: ~1.5dB reduction at full drive
        float gainCompensation = 1.0f / (1.0f + drive * 0.19f);
        output *= gainCompensation;

        // Mix with dry signal based on drive amount
        // At 100% drive, use 100% wet for maximum saturation effect
        float wetMix = juce::jlimit(0.0f, 1.0f, drive * 1.4f);
        float result = input * (1.0f - wetMix) + output * wetMix;

        // NaN/Inf protection - return clean input if saturation produced invalid output
        if (!safeIsFinite(result))
            return input;

        return result;
    }

private:
    // Static counter for unique instance seeding
    static inline std::atomic<unsigned int> instanceCounter{0};

    ConsoleType consoleType = ConsoleType::ESeries;
    double sampleRate = 44100.0;

    // DC blocker state
    float dcBlockerX1_L = 0.0f, dcBlockerY1_L = 0.0f;
    float dcBlockerX1_R = 0.0f, dcBlockerY1_R = 0.0f;
    float dcBlockerCoeff = 0.999f;

    // Frequency content estimation state
    float lastSample_L = 0.0f, lastSample_R = 0.0f;
    float highFreqEstimate_L = 0.0f, highFreqEstimate_R = 0.0f;

    // LF content estimation (1st-order LPF envelope follower)
    float lfEstimate_L = 0.0f, lfEstimate_R = 0.0f;
    float lfEstimateCoeff = 0.04f;  // Updated in setSampleRate()

    // Configurable high-frequency scaling factor
    float highFreqScale = 3.0f;

    // Component tolerance variation (±5% per instance)
    // Simulates real hardware component tolerances for unique analog character
    float transformerTolerance = 1.0f;
    float opAmpTolerance = 1.0f;
    float outputTransformerTolerance = 1.0f;

    // Noise generation for console noise floor
    std::mt19937 noiseGen;
    std::uniform_real_distribution<float> noiseDist;

    // Estimate high-frequency content using simple differentiator
    float estimateHighFrequencyContent(float input, bool isLeftChannel)
    {
        float& lastSample = isLeftChannel ? lastSample_L : lastSample_R;
        float& estimate = isLeftChannel ? highFreqEstimate_L : highFreqEstimate_R;

        float difference = std::abs(input - lastSample);
        lastSample = input;

        const float smoothing = 0.95f;
        estimate = estimate * smoothing + difference * (1.0f - smoothing);

        float normalized = juce::jlimit(0.0f, 1.0f, estimate * highFreqScale);
        return normalized;
    }

    // Estimate low-frequency content using 1st-order LPF envelope follower
    // Real iron-core transformers have higher flux density at LF, causing
    // more core saturation on bass-heavy material
    float estimateLowFrequencyContent(float input, bool isLeftChannel)
    {
        float& estimate = isLeftChannel ? lfEstimate_L : lfEstimate_R;

        // 1st-order LPF on absolute value (envelope follower at ~300Hz)
        float absVal = std::abs(input);
        estimate += lfEstimateCoeff * (absVal - estimate);

        // Normalize: typical music has LF envelope ~0.1-0.3
        return juce::jlimit(0.0f, 1.0f, estimate * 3.0f);
    }

    // Input transformer saturation with asymmetric B-H curve
    // Real iron-core transformers have slight magnetic asymmetry producing
    // stronger even-order (especially 2nd) harmonics
    float processInputTransformer(float input, float drive)
    {
        const float transformerDrive = 1.0f + drive * 7.0f;
        float driven = input * transformerDrive;

        float abs_x = std::abs(driven);

        // Asymmetric B-H curve: positive and negative half-cycles differ
        // Real transformers have ~5-8% asymmetry from residual magnetization
        // Positive half saturates slightly earlier (lower threshold)
        // This asymmetry is the primary source of even-order harmonics
        float posThreshold = 0.88f;   // Positive half saturates earlier
        float negThreshold = 0.92f;   // Negative half saturates later
        float threshold = (driven >= 0.0f) ? posThreshold : negThreshold;

        float saturated;
        if (abs_x < threshold)
        {
            // Linear region
            saturated = driven;
        }
        else if (abs_x < threshold + 0.6f)
        {
            // Gentle compression region — asymmetric knee
            float excess = abs_x - threshold;
            // Positive half compresses slightly more (tighter saturation knee)
            float kneeHardness = (driven >= 0.0f) ? 0.17f : 0.13f;
            float compressed = threshold + excess * (1.0f - excess * kneeHardness);
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }
        else
        {
            // Hard saturation region
            float excess = abs_x - (threshold + 0.6f);
            float compressed = (threshold + 0.6f) + std::tanh(excess * 1.5f) * 0.3f;
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }

        // Console-specific harmonic coloration
        // Threshold difference dominates harmonic behavior:
        // E-Series (0.6 threshold): Clean at low drive, strong harmonics when engaged
        // G-Series (0.05 threshold): Subtle harmonics across entire drive range
        // At low-to-moderate drive (0.1-0.5), G-Series produces MORE total harmonic
        // content due to much lower threshold, despite smaller coefficients.
        // E-Series delivers stronger saturation punch when driven hard (>0.6).
        float harmonicThreshold = (consoleType == ConsoleType::ESeries) ? 0.6f : 0.05f;

        if (abs_x > harmonicThreshold)
        {
            float saturationAmount = (abs_x - harmonicThreshold) / (1.2f - harmonicThreshold);
            saturationAmount = juce::jlimit(0.0f, 1.0f, saturationAmount);

            if (consoleType == ConsoleType::ESeries)
            {
                // E-Series (Brown): 2nd harmonic DOMINANT
                // x^2 is an even function: y = x + C*x^2 produces H2 (DC removed by
                // the downstream DC blocker). x*|x| is ODD and would produce H3 instead.
                saturated += saturated * saturated * (0.08f * saturationAmount);
            }
            else
            {
                // G-Series (Black): 3rd harmonic DOMINANT
                saturated += saturated * std::abs(saturated) * (0.020f * saturationAmount);  // 2nd (subtle)
                saturated += saturated * saturated * saturated * (0.045f * saturationAmount);  // 3rd DOMINANT
            }
        }

        return saturated / transformerDrive;
    }

    // NE5534 op-amp stage saturation
    // Models the actual op-amp clipping behavior
    // Creates both 2nd and 3rd harmonics, with asymmetric clipping
    // NE5534 THD: ~0.0008% at -18dB (essentially unmeasurable)
    float processOpAmpStage(float input, float drive)
    {
        const float opAmpDrive = 1.0f + drive * 9.0f;
        float driven = input * opAmpDrive;

        float output;

        // Positive half-cycle (toward V+ rail, ~+15V in the console)
        if (driven > 0.0f)
        {
            if (driven < 1.0f)
            {
                output = driven;
            }
            else if (driven < 1.8f)
            {
                float excess = driven - 1.0f;
                output = 1.0f + excess * (1.0f - excess * 0.2f);
            }
            else
            {
                float clipHardness = (consoleType == ConsoleType::ESeries) ? 1.5f : 2.0f;
                output = 1.5f + std::tanh((driven - 1.8f) * clipHardness) * 0.3f;
            }
        }
        // Negative half-cycle (toward V- rail, ~-15V in the console)
        else
        {
            if (driven > -1.0f)
            {
                output = driven;
            }
            else if (driven > -1.9f)
            {
                float excess = -driven - 1.0f;
                output = -1.0f - excess * (1.0f - excess * 0.18f);
            }
            else
            {
                float clipHardness = (consoleType == ConsoleType::ESeries) ? 1.5f : 2.0f;
                output = -1.55f + std::tanh((driven + 1.9f) * clipHardness) * 0.3f;
            }
        }

        // Console-specific harmonic shaping
        // E-Series (0.6 threshold): Clean at low drive, strong harmonics when engaged
        // G-Series (0.05 threshold): Subtle harmonics across entire drive range
        float threshold = (consoleType == ConsoleType::ESeries) ? 0.6f : 0.05f;

        if (std::abs(driven) > threshold)
        {
            float saturationAmount = (std::abs(driven) - threshold) / (1.5f - threshold);
            saturationAmount = juce::jlimit(0.0f, 1.0f, saturationAmount);

            if (consoleType == ConsoleType::ESeries)
            {
                // E-Series: 2nd harmonic DOMINANT via x^2 (even function → H2)
                // copysign(1, x)*x^2 = x*|x| is odd → H3; plain x^2 gives H2
                output += output * output * (0.06f * saturationAmount);
            }
            else
            {
                // G-Series: 3rd harmonic DOMINANT over 2nd
                output += output * output * std::copysign(0.022f * saturationAmount, output);
                output += output * output * output * (0.040f * saturationAmount);
            }
        }

        return output / opAmpDrive;
    }

    // Output transformer saturation (E-Series only)
    // Similar to input transformer but with less drive and asymmetric curve
    float processOutputTransformer(float input, float drive)
    {
        const float transformerDrive = 1.0f + drive * 2.0f;
        float driven = input * transformerDrive;

        float abs_x = std::abs(driven);

        // Asymmetric thresholds (output transformer has less asymmetry than input).
        // Raised from 0.48/0.52 — the original values saturated even moderate signals
        // loudly, causing harshness when the EQ boosted the low end before this stage.
        float threshold = (driven >= 0.0f) ? 0.72f : 0.76f;

        float saturated;
        if (abs_x < threshold)
        {
            saturated = driven;
        }
        else if (abs_x < threshold + 0.4f)
        {
            float excess = abs_x - threshold;
            float compressed = threshold + excess * (1.0f - excess * 0.25f);
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }
        else
        {
            float excess = abs_x - (threshold + 0.4f);
            float compressed = (threshold + 0.4f) + std::tanh(excess * 1.5f) * 0.15f;
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }

        // Subtle 2nd harmonic emphasis via x^2 (even function → H2, not x*|x| which is odd → H3)
        saturated += saturated * saturated * 0.08f;

        return saturated / transformerDrive;
    }

    // DC blocking filter to prevent DC offset accumulation
    float processDCBlocker(float input, bool isLeftChannel)
    {
        // Simple first-order high-pass filter at ~5Hz
        float& x1 = isLeftChannel ? dcBlockerX1_L : dcBlockerX1_R;
        float& y1 = isLeftChannel ? dcBlockerY1_L : dcBlockerY1_R;

        float output = input - x1 + dcBlockerCoeff * y1;
        x1 = input;
        y1 = output;

        return output;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConsoleSaturation)
};

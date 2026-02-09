/*
  ==============================================================================

    SSLSaturation.h

    Accurate SSL console harmonic emulation based on:
    - SSL E-Series (VE-type) channel strip characteristics
    - SSL G-Series (G+/G384) channel strip characteristics
    - NE5534 op-amp modeling
    - Marinair/Carnhill transformer saturation
    - Measured harmonic data from real SSL consoles

    References:
    - SSL E-Series: Predominantly 2nd harmonic, warm character
    - SSL G-Series: More 3rd harmonic, tighter/cleaner
    - NE5534 op-amp: Asymmetric clipping, ~0.1% THD typical
    - Transformers: Even-order harmonics, frequency-dependent

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <random>

class SSLSaturation
{
public:
    enum class ConsoleType
    {
        ESeries,    // E-Series VE (Brown knobs) - warmer, more 2nd harmonic
        GSeries     // G-Series (Black knobs) - cleaner, more 3rd harmonic
    };

    // Constructor with optional seed for reproducible tests
    // Default uses instance address for unique per-instance noise
    explicit SSLSaturation(unsigned int seed = 0)
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
    }

    void reset()
    {
        dcBlockerX1_L = dcBlockerY1_L = 0.0f;
        dcBlockerX1_R = dcBlockerY1_R = 0.0f;
        lastSample_L = lastSample_R = 0.0f;
        highFreqEstimate_L = highFreqEstimate_R = 0.0f;
    }

    // Main processing function with drive amount (0.0 to 1.0)
    float processSample(float input, float drive, bool isLeftChannel)
    {
        // Suppress denormals for performance (prevents CPU spikes on older processors)
        juce::ScopedNoDenormals noDenormals;

        // NaN/Inf protection - return silence (0.0f) if input is invalid
        if (!std::isfinite(input))
            return 0.0f;

        if (drive < 0.001f)
            return input;

        // Gentle pre-saturation limiting to prevent extreme aliasing
        // This soft clips peaks before they hit the transformer stage
        // Only active at very high levels (>0.95) to maintain SSL character
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
        // Real SSL hardware has subtle frequency-dependent behavior:
        // - Transformers saturate MORE at low frequencies (core saturation - physics-based)
        // - Op-amps have slew-rate limiting at high frequencies (only at extreme overdrive)
        // Modern enhancement: reduce HF saturation to prevent aliasing while maintaining character
        float highFreqContent = estimateHighFrequencyContent(limited, isLeftChannel);

        // Progressive HF drive reduction for anti-aliasing
        // This mimics real SSL behavior: transformers naturally saturate less at HF
        // Scaling increases with both drive amount and frequency content
        float hfReduction = highFreqContent * (0.25f + drive * 0.35f);  // 25-60% reduction based on drive
        float effectiveDrive = drive * (1.0f - hfReduction);

        // Stage 1: Input transformer saturation
        // SSL uses Marinair (E-Series) or Carnhill (G-Series) transformers
        // Apply component tolerance for per-instance variation
        float transformed = processInputTransformer(limited, effectiveDrive * transformerTolerance);

        // Stage 2: Op-amp gain stage (NE5534)
        // This is where most of the harmonic coloration happens
        // Apply same frequency-dependent drive reduction for consistency
        float opAmpOut = processOpAmpStage(transformed, effectiveDrive * opAmpTolerance);

        // Stage 3: Output transformer (if applicable)
        // E-Series has output transformers, G-Series is transformerless
        float output = (consoleType == ConsoleType::ESeries)
            ? processOutputTransformer(opAmpOut, drive * 0.7f * outputTransformerTolerance)
            : opAmpOut;

        // Add console noise floor (-90dB RMS, typical for SSL)
        // Noise increases slightly with drive (like real hardware)
        // This adds realism and subtle analog character
        float noiseLevel = 0.00003162f * (1.0f + drive * 0.5f); // -90dB base, increases with drive
        output += noiseDist(noiseGen) * noiseLevel;

        // DC blocking filter to prevent DC offset buildup
        output = processDCBlocker(output, isLeftChannel);

        // Mix with dry signal based on drive amount
        // At 100% drive, use 100% wet for maximum saturation effect
        float wetMix = juce::jlimit(0.0f, 1.0f, drive * 1.4f);  // Linear ramp, full wet at high drive
        float result = input * (1.0f - wetMix) + output * wetMix;

        // NaN/Inf protection - return clean input if saturation produced invalid output
        if (!std::isfinite(result))
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

    // Configurable high-frequency scaling factor
    // Can be tuned or exposed to tests/parameters for extreme test signals
    // Reduced from 4.0f to 3.0f to prevent saturation on very dynamic material
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
    // This provides a fast, computationally cheap estimate of spectral content
    // without requiring full FFT or filter bank analysis
    float estimateHighFrequencyContent(float input, bool isLeftChannel)
    {
        float& lastSample = isLeftChannel ? lastSample_L : lastSample_R;
        float& estimate = isLeftChannel ? highFreqEstimate_L : highFreqEstimate_R;

        // First-order difference approximates high-frequency content
        // Large differences = high frequency, small differences = low frequency
        float difference = std::abs(input - lastSample);
        lastSample = input;

        // Smooth the estimate with a simple one-pole lowpass (RC filter)
        // This prevents rapid fluctuations and provides a more stable estimate
        const float smoothing = 0.95f;  // Higher = more smoothing
        estimate = estimate * smoothing + difference * (1.0f - smoothing);

        // Normalize to 0-1 range (typical difference range is 0-0.5 for normalized audio)
        // Scale so that typical music content gives reasonable values
        // Use configurable highFreqScale instead of hardcoded value
        float normalized = juce::jlimit(0.0f, 1.0f, estimate * highFreqScale);

        return normalized;
    }

    // Input transformer saturation
    // Models Marinair/Carnhill transformer behavior
    // Predominantly even-order harmonics (2nd, 4th)
    // SSL is very clean at normal levels (-18dB), only saturates when driven hot
    float processInputTransformer(float input, float drive)
    {
        // SSL transformers are very linear at normal levels
        // Only apply saturation when driven hard (above ~0dB)
        // Drive range allows authentic SSL "pushed" sound without excessive aliasing
        // At 100% drive, allows ~18dB of headroom (8x gain) - reduced for cleaner operation
        const float transformerDrive = 1.0f + drive * 7.0f;  // Max 8x gain at full drive
        float driven = input * transformerDrive;

        // Transformer saturation using modified Jiles-Atherton approximation
        // This creates predominantly 2nd harmonic content

        // Soft saturation curve with even-order emphasis
        float abs_x = std::abs(driven);

        // Progressive saturation - SSL is linear until driven hard
        float saturated;
        if (abs_x < 0.9f)
        {
            // Linear region - no saturation (SSL operates here at -18dB)
            saturated = driven;
        }
        else if (abs_x < 1.5f)
        {
            // Gentle compression region - 2nd harmonic emerges
            float excess = abs_x - 0.9f;
            float compressed = 0.9f + excess * (1.0f - excess * 0.15f);
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }
        else
        {
            // Hard saturation region - more harmonics
            float excess = abs_x - 1.5f;
            float compressed = 1.5f + std::tanh(excess * 1.5f) * 0.3f;
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }

        // Add console-specific harmonic coloration
        // SSL transformers are very linear until driven moderately hard
        //
        // DESIGN DECISION: Threshold difference dominates harmonic behavior
        // E-Series (0.6 threshold): Clean at low drive, strong harmonics when engaged
        // G-Series (0.05 threshold): Subtle harmonics across entire drive range
        //
        // At low-to-moderate drive (0.1-0.5), G-Series produces MORE total harmonic
        // content due to much lower threshold, despite smaller coefficients.
        // E-Series delivers stronger saturation punch when driven hard (>0.6).
        float threshold = (consoleType == ConsoleType::ESeries) ? 0.6f : 0.05f;

        if (abs_x > threshold)
        {
            // Scale harmonic generation based on how hard we're driving
            float saturationAmount = (abs_x - threshold) / (1.2f - threshold);
            saturationAmount = juce::jlimit(0.0f, 1.0f, saturationAmount);

            if (consoleType == ConsoleType::ESeries)
            {
                // E-Series (Brown): 2nd harmonic DOMINANT (E-Series signature)
                // High threshold (0.6) + strong coefficients = clean low-end, saturated highs
                saturated += saturated * saturated * (0.12f * saturationAmount);
            }
            else
            {
                // G-Series (Black): 3rd harmonic DOMINANT (G-Series signature)
                // Low threshold (0.05) + subtle coefficients = gentle coloration throughout
                saturated += saturated * saturated * (0.025f * saturationAmount);  // 2nd harmonic (subtle)
                saturated += saturated * saturated * saturated * (0.050f * saturationAmount);  // 3rd harmonic DOMINANT
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
        // NE5534 has different characteristics than generic op-amps
        // SSL designs keep op-amps in linear region at normal levels
        // THD only becomes measurable when driven very hot
        // Drive range allows authentic SSL character without excessive aliasing
        // At 100% drive, allows ~20dB of headroom (10x gain) - reduced for cleaner operation

        const float opAmpDrive = 1.0f + drive * 9.0f;  // Max 10x gain at full drive
        float driven = input * opAmpDrive;

        // NE5534 specific characteristics:
        // - Asymmetric clipping (positive rail clips differently than negative)
        // - Soft knee entry into saturation
        // - Extremely low distortion at normal levels

        float output;

        // Positive half-cycle (toward V+ rail, ~+15V in SSL)
        if (driven > 0.0f)
        {
            if (driven < 1.0f)
            {
                // Linear region - SSL operates here at -18dB
                // Virtually no distortion
                output = driven;
            }
            else if (driven < 1.8f)
            {
                // Soft saturation region
                float excess = driven - 1.0f;
                output = 1.0f + excess * (1.0f - excess * 0.2f);
            }
            else
            {
                // Hard clipping region (supply rail)
                // E-Series clips softer, G-Series clips harder
                float clipHardness = (consoleType == ConsoleType::ESeries) ? 1.5f : 2.0f;
                output = 1.5f + std::tanh((driven - 1.8f) * clipHardness) * 0.3f;
            }
        }
        // Negative half-cycle (toward V- rail, ~-15V in SSL)
        else
        {
            if (driven > -1.0f)
            {
                // Linear region
                output = driven;
            }
            else if (driven > -1.9f)
            {
                // Soft saturation region (slightly different than positive)
                float excess = -driven - 1.0f;
                output = -1.0f - excess * (1.0f - excess * 0.18f);
            }
            else
            {
                // Hard clipping region
                float clipHardness = (consoleType == ConsoleType::ESeries) ? 1.5f : 2.0f;
                output = -1.55f + std::tanh((driven + 1.9f) * clipHardness) * 0.3f;
            }
        }

        // Console-specific harmonic shaping - SSL op-amps are very linear until driven hard
        //
        // DESIGN DECISION: Threshold difference dominates harmonic behavior
        // E-Series (0.6 threshold): Clean at low drive, strong harmonics when engaged
        // G-Series (0.05 threshold): Subtle harmonics across entire drive range
        //
        // At low-to-moderate drive (0.1-0.5), G-Series produces MORE total harmonic
        // content due to much lower threshold, despite smaller coefficients.
        // E-Series delivers stronger saturation punch when driven hard (>0.6).
        float threshold = (consoleType == ConsoleType::ESeries) ? 0.6f : 0.05f;

        if (std::abs(driven) > threshold)
        {
            // Scale harmonic generation based on drive level
            float saturationAmount = (std::abs(driven) - threshold) / (1.5f - threshold);
            saturationAmount = juce::jlimit(0.0f, 1.0f, saturationAmount);

            if (consoleType == ConsoleType::ESeries)
            {
                // E-Series: 2nd harmonic DOMINANT (E-Series signature)
                // High threshold (0.6) + strong coefficients = clean low-end, saturated highs
                output += output * output * std::copysign(0.10f * saturationAmount, output);
            }
            else
            {
                // G-Series: 3rd harmonic DOMINANT over 2nd (G-Series signature)
                // Low threshold (0.05) + subtle coefficients = gentle coloration throughout
                output += output * output * std::copysign(0.022f * saturationAmount, output);  // 2nd harmonic (subtle)
                output += output * output * output * (0.040f * saturationAmount);  // 3rd harmonic DOMINANT
            }
        }

        return output / opAmpDrive;
    }

    // Output transformer saturation (E-Series only)
    // Similar to input transformer but with less drive
    float processOutputTransformer(float input, float drive)
    {
        const float transformerDrive = 1.0f + drive * 2.0f;
        float driven = input * transformerDrive;

        // Output transformer saturates less than input transformer
        // Adds final touch of even-order harmonics
        float abs_x = std::abs(driven);

        float saturated;
        if (abs_x < 0.5f)
        {
            saturated = driven;
        }
        else if (abs_x < 0.9f)
        {
            float excess = abs_x - 0.5f;
            float compressed = 0.5f + excess * (1.0f - excess * 0.25f);
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }
        else
        {
            float excess = abs_x - 0.9f;
            float compressed = 0.9f + std::tanh(excess * 1.5f) * 0.15f;
            saturated = (driven > 0.0f) ? compressed : -compressed;
        }

        // Subtle 2nd harmonic emphasis
        saturated += saturated * saturated * 0.05f;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SSLSaturation)
};

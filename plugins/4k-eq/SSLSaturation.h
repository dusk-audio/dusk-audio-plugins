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
#include <cmath>

class SSLSaturation
{
public:
    enum class ConsoleType
    {
        ESeries,    // E-Series VE (Brown knobs) - warmer, more 2nd harmonic
        GSeries     // G-Series (Black knobs) - cleaner, more 3rd harmonic
    };

    SSLSaturation() = default;

    void setConsoleType(ConsoleType type)
    {
        consoleType = type;
    }

    void setSampleRate(double newSampleRate)
    {
        sampleRate = newSampleRate;

        // Update DC blocker coefficients
        // High-pass at ~5Hz to remove any DC offset from saturation
        const double cutoffFreq = 5.0;
        const double RC = 1.0 / (juce::MathConstants<double>::twoPi * cutoffFreq);
        dcBlockerCoeff = RC / (RC + 1.0 / sampleRate);
    }

    void reset()
    {
        dcBlockerX1_L = dcBlockerY1_L = 0.0f;
        dcBlockerX1_R = dcBlockerY1_R = 0.0f;
    }

    // Main processing function with drive amount (0.0 to 1.0)
    float processSample(float input, float drive, bool isLeftChannel)
    {
        if (drive < 0.001f)
            return input;

        // Stage 1: Input transformer saturation
        // SSL uses Marinair (E-Series) or Carnhill (G-Series) transformers
        float transformed = processInputTransformer(input, drive);

        // Stage 2: Op-amp gain stage (NE5534)
        // This is where most of the harmonic coloration happens
        float opAmpOut = processOpAmpStage(transformed, drive);

        // Stage 3: Output transformer (if applicable)
        // E-Series has output transformers, G-Series is transformerless
        float output = (consoleType == ConsoleType::ESeries)
            ? processOutputTransformer(opAmpOut, drive * 0.7f)  // Less drive on output
            : opAmpOut;

        // DC blocking filter to prevent DC offset buildup
        output = processDCBlocker(output, isLeftChannel);

        // Mix with dry signal based on drive amount
        // At 100% drive, use 100% wet for maximum saturation effect
        float wetMix = juce::jlimit(0.0f, 1.0f, drive * 1.4f);  // Linear ramp, full wet at high drive
        return input * (1.0f - wetMix) + output * wetMix;
    }

private:
    ConsoleType consoleType = ConsoleType::ESeries;
    double sampleRate = 44100.0;

    // DC blocker state
    float dcBlockerX1_L = 0.0f, dcBlockerY1_L = 0.0f;
    float dcBlockerX1_R = 0.0f, dcBlockerY1_R = 0.0f;
    double dcBlockerCoeff = 0.999;

    // Input transformer saturation
    // Models Marinair/Carnhill transformer behavior
    // Predominantly even-order harmonics (2nd, 4th)
    // SSL is very clean at normal levels (-18dB), only saturates when driven hot
    float processInputTransformer(float input, float drive)
    {
        // SSL transformers are very linear at normal levels
        // Only apply saturation when driven hard (above ~0dB)
        // Reduced multiplier for better anti-aliasing at high drive levels
        const float transformerDrive = 1.0f + drive * 4.0f;  // Gentler for alias-free processing
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
        // E-Series saturates earlier than G-Series (more colored)
        float threshold = (consoleType == ConsoleType::ESeries) ? 0.6f : 0.45f;

        if (abs_x > threshold)
        {
            // Scale harmonic generation based on how hard we're driving
            float saturationAmount = (abs_x - threshold) / (1.2f - threshold);
            saturationAmount = juce::jlimit(0.0f, 1.0f, saturationAmount);

            if (consoleType == ConsoleType::ESeries)
            {
                // E-Series (Brown): 2nd harmonic DOMINANT (E-Series signature)
                // Must be strong enough that 2nd > 3rd at all drive levels
                saturated += saturated * saturated * (0.12f * saturationAmount);
            }
            else
            {
                // G-Series (Black): 3rd harmonic DOMINANT (G-Series signature)
                // Black is ~2x cleaner overall, 3rd harmonic is the key differentiator
                saturated += saturated * saturated * (0.02f * saturationAmount);  // 2nd harmonic
                saturated += saturated * saturated * saturated * (0.10f * saturationAmount);  // 3rd harmonic DOMINANT
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
        // Reduced multiplier for better anti-aliasing at high drive levels

        const float opAmpDrive = 1.0f + drive * 5.0f;  // Gentler for alias-free processing
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
        // E-Series saturates earlier than G-Series (more colored)
        float threshold = (consoleType == ConsoleType::ESeries) ? 0.6f : 0.45f;

        if (std::abs(driven) > threshold)
        {
            // Scale harmonic generation based on drive level
            float saturationAmount = (std::abs(driven) - threshold) / (1.5f - threshold);
            saturationAmount = juce::jlimit(0.0f, 1.0f, saturationAmount);

            if (consoleType == ConsoleType::ESeries)
            {
                // E-Series: 2nd harmonic DOMINANT (E-Series signature)
                // Must be strong enough that 2nd > 3rd at all drive levels
                output += output * output * std::copysign(0.10f * saturationAmount, output);
            }
            else
            {
                // G-Series: 3rd harmonic DOMINANT over 2nd (G-Series signature)
                // Black is ~2x cleaner overall, 3rd harmonic is the distinguishing feature
                output += output * output * std::copysign(0.02f * saturationAmount, output);  // 2nd harmonic
                output += output * output * output * (0.10f * saturationAmount);  // 3rd harmonic DOMINANT
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

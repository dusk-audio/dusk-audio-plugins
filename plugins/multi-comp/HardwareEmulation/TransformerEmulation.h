/*
  ==============================================================================

    TransformerEmulation.h
    Audio transformer saturation modeling

    Models the non-linear behavior of audio transformers:
    - Frequency-dependent saturation (LF saturates more due to core physics)
    - Harmonic generation (primarily even harmonics)
    - High-frequency rolloff (inductance limiting)
    - DC blocking

    Based on measurements from Marinair, UTC, and Carnhill transformers.

  ==============================================================================
*/

#pragma once

#include "HardwareMeasurements.h"
#include "WaveshaperCurves.h"
#include <cmath>
#include <algorithm>

namespace HardwareEmulation {

class TransformerEmulation
{
public:
    TransformerEmulation() = default;

    void prepare(double sampleRate, int numChannels = 2)
    {
        this->sampleRate = sampleRate;
        this->numChannels = numChannels;

        // DC blocker coefficient (10Hz highpass)
        const float dcCutoff = 10.0f;
        dcBlockerCoeff = 1.0f - (6.283185f * dcCutoff / static_cast<float>(sampleRate));

        // HF rolloff filter coefficient (adjustable via profile)
        updateHFRolloff(20000.0f);

        // Reset state
        reset();
    }

    void reset()
    {
        // Always reset all available channels to avoid stale state
        // (processSample can access any channel 0-1 regardless of numChannels)
        for (int ch = 0; ch < 2; ++ch)
        {
            dcBlockerX1[ch] = 0.0f;
            dcBlockerY1[ch] = 0.0f;
            hfFilterState[ch] = 0.0f;
            lastSample[ch] = 0.0f;
            hfEstimate[ch] = 0.0f;
        }
    }
    void setProfile(const TransformerProfile& newProfile)
    {
        profile = newProfile;
        enabled = profile.hasTransformer;
        updateHFRolloff(profile.highFreqRolloff);
    }

    void setEnabled(bool shouldBeEnabled)
    {
        enabled = shouldBeEnabled && profile.hasTransformer;
    }

    bool isEnabled() const { return enabled; }

    // Process a single sample
    float processSample(float input, int channel)
    {
        if (!enabled)
            return input;

        channel = std::clamp(channel, 0, 1);

        // 1. Estimate high-frequency content for frequency-dependent saturation
        float hfContent = estimateHighFrequencyContent(input, channel);

        // 2. Calculate frequency-dependent drive
        // Low frequencies saturate more (transformer core physics)
        float lfMultiplier = profile.lowFreqSaturation * (1.0f - hfContent * 0.5f);

        // 3. Apply transformer saturation curve with drive
        float driven = input * lfMultiplier;
        float saturated = applyTransformerSaturation(driven);

        // 4. Blend based on saturation amount
        float output = input + (saturated - input) * profile.saturationAmount;

        // 5. Add harmonics based on profile
        output = addHarmonics(output, profile.harmonics);

        // 6. Apply high-frequency rolloff (transformer inductance)
        output = applyHFRolloff(output, channel);

        // 7. DC blocking
        output = processDCBlocker(output, channel);

        return output;
    }

    // Block processing for efficiency
    void processBlock(float* const* channelData, int numSamples)
    {
        if (!enabled)
            return;

        for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
        {
            float* data = channelData[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] = processSample(data[i], ch);
            }
        }
    }

private:
    TransformerProfile profile;
    double sampleRate = 44100.0;
    int numChannels = 2;
    bool enabled = false;

    // DC blocker state
    float dcBlockerCoeff = 0.999f;
    float dcBlockerX1[2] = {0.0f, 0.0f};
    float dcBlockerY1[2] = {0.0f, 0.0f};

    // HF rolloff filter
    float hfRolloffCoeff = 0.99f;
    float hfFilterState[2] = {0.0f, 0.0f};

    // HF content estimation
    float lastSample[2] = {0.0f, 0.0f};
    float hfEstimate[2] = {0.0f, 0.0f};

    void updateHFRolloff(float cutoffFreq)
    {
        // Simple one-pole lowpass coefficient
        float w = 6.283185f * cutoffFreq / static_cast<float>(sampleRate);
        hfRolloffCoeff = w / (w + 1.0f);
    }

    float estimateHighFrequencyContent(float input, int channel)
    {
        // Simple differentiator for HF estimation
        float diff = std::abs(input - lastSample[channel]);
        lastSample[channel] = input;

        // Smooth the estimate
        const float smoothing = 0.95f;
        hfEstimate[channel] = hfEstimate[channel] * smoothing + diff * (1.0f - smoothing);

        // Normalize (0-1 range, calibrated for typical audio)
        return std::clamp(hfEstimate[channel] * 3.0f, 0.0f, 1.0f);
    }

    float applyTransformerSaturation(float input)
    {
        // Use the shared waveshaper with transformer curve
        return getWaveshaperCurves().process(input, WaveshaperCurves::CurveType::Transformer);
    }

    float addHarmonics(float input, const HarmonicProfile& harmonics)
    {
        if (harmonics.h2 <= 0.0f && harmonics.h3 <= 0.0f && harmonics.h4 <= 0.0f)
            return input;

        float x = input;
        float x2 = x * x;
        float x3 = x2 * x;

        float output = x;

        // 2nd harmonic (even - creates asymmetry)
        // x² is always positive, adding it directly creates the asymmetric
        // transfer function characteristic of even harmonics
        output += harmonics.h2 * x2;

        // 3rd harmonic (odd - symmetric)
        output += harmonics.h3 * x3;

        // 4th harmonic (even)
        if (harmonics.h4 > 0.0f)
            output += harmonics.h4 * x2 * x2;

        return output;
    }
    float applyHFRolloff(float input, int channel)
    {
        // One-pole lowpass filter
        hfFilterState[channel] += hfRolloffCoeff * (input - hfFilterState[channel]);
        return hfFilterState[channel];
    }

    float processDCBlocker(float input, int channel)
    {
        // First-order highpass (DC blocking)
        float y = input - dcBlockerX1[channel] + dcBlockerCoeff * dcBlockerY1[channel];
        dcBlockerX1[channel] = input;
        dcBlockerY1[channel] = y;
        return y;
    }
};

} // namespace HardwareEmulation

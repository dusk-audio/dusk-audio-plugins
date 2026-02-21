/*
  ==============================================================================

    TransformerEmulation.h
    Audio transformer saturation modeling

    Models the non-linear behavior of audio transformers:
    - Frequency-dependent saturation (LF saturates more due to core physics)
    - Harmonic generation (primarily even harmonics)
    - High-frequency rolloff (inductance limiting)
    - DC blocking

    Based on measurements from classic audio transformers.

    This is the shared library version - all plugins should use this.

  ==============================================================================
*/

#pragma once

#include "HardwareProfiles.h"
#include "WaveshaperCurves.h"
#include "DCBlocker.h"
#include "HighFrequencyEstimator.h"
#include <cmath>
#include <algorithm>

namespace AnalogEmulation {

class TransformerEmulation
{
public:
    TransformerEmulation() = default;

    void prepare(double sampleRate, int numChannels = 2)
    {
        this->sampleRate = sampleRate;
        this->numChannels = numChannels;

        // Prepare per-channel processors
        for (int ch = 0; ch < 2; ++ch)
        {
            dcBlocker[ch].prepare(sampleRate, profile.dcBlockingFreq);
            hfEstimator[ch].prepare(sampleRate);
        }

        // HF rolloff filter coefficient
        updateHFRolloff(profile.highFreqRolloff);

        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            dcBlocker[ch].reset();
            hfEstimator[ch].reset();
            hfFilterState[ch] = 0.0f;
        }
    }

    void setProfile(const TransformerProfile& newProfile)
    {
        profile = newProfile;
        enabled = profile.hasTransformer;
        updateHFRolloff(profile.highFreqRolloff);

        // Update DC blocker cutoff
        for (int ch = 0; ch < 2; ++ch)
            dcBlocker[ch].prepare(sampleRate, profile.dcBlockingFreq);
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
        float hfContent = hfEstimator[channel].processSample(input);

        // 2. Calculate frequency-dependent drive
        // Low frequencies saturate more (transformer core physics)
        float lfMultiplier = profile.lowFreqSaturation * (1.0f - hfContent * 0.5f);

        // 3. Apply transformer saturation curve with drive
        float driven = input * lfMultiplier;
        float saturated = getWaveshaperCurves().process(driven, WaveshaperCurves::CurveType::Transformer);

        // 4. Blend based on saturation amount
        float output = input + (saturated - input) * profile.saturationAmount;

        // 5. Add harmonics based on profile
        output = addHarmonics(output, profile.harmonics);

        // 6. Apply high-frequency rolloff (transformer inductance)
        output = applyHFRolloff(output, channel);

        // 7. DC blocking
        output = dcBlocker[channel].processSample(output);

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

    // Per-channel state
    DCBlocker dcBlocker[2];
    HighFrequencyEstimator hfEstimator[2];
    float hfFilterState[2] = {0.0f, 0.0f};
    float hfRolloffCoeff = 0.99f;

    void updateHFRolloff(float cutoffFreq)
    {
        float w = 6.283185f * cutoffFreq / static_cast<float>(sampleRate);
        hfRolloffCoeff = w / (w + 1.0f);
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
        hfFilterState[channel] += hfRolloffCoeff * (input - hfFilterState[channel]);
        return hfFilterState[channel];
    }
};

} // namespace AnalogEmulation

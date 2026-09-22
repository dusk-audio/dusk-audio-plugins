// TransformerEmulation.h — Audio transformer saturation modeling

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

        // DC blocker coefficient from profile (default 10Hz if no profile set)
        updateDCBlocker(profile.dcBlockingFreq);

        // HF rolloff filter coefficient (adjustable via profile)
        updateHFRolloff(profile.highFreqRolloff);

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
            hysteresisState[ch] = 0.0f;
        }
    }
    void setProfile(const TransformerProfile& newProfile)
    {
        profile = newProfile;
        enabled = profile.hasTransformer;
        updateDCBlocker(profile.dcBlockingFreq);
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

        float hfContent = estimateHighFrequencyContent(input, channel);

        // LF saturates more (transformer core physics — subtle effect)
        float lfMultiplier = profile.lowFreqSaturation * (1.0f - hfContent * 0.15f);

        float driven = input * lfMultiplier;
        float saturated = applyTransformerSaturation(driven);
        float output = input + (saturated - input) * profile.saturationAmount;

        // Magnetic hysteresis
        if (profile.hysteresisAmount > 0.0f)
        {
            float hystDelta = profile.hysteresisAmount * hysteresisState[channel];
            output += hystDelta;
            hysteresisState[channel] = hysteresisState[channel] * 0.95f
                                     + (output - input) * 0.05f;
        }

        output = addHarmonics(output, profile.harmonics);

        if (hfRolloffEnabled)
            output = applyHFRolloff(output, channel);

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
    bool hfRolloffEnabled = true;
    float hfFilterState[2] = {0.0f, 0.0f};

    // HF content estimation
    float lastSample[2] = {0.0f, 0.0f};
    float hfEstimate[2] = {0.0f, 0.0f};

    // Magnetic hysteresis state
    float hysteresisState[2] = {0.0f, 0.0f};

    void updateDCBlocker(float cutoffFreq)
    {
        // Minimum 0.5Hz to always block true DC, default 10Hz
        float dcCutoff = std::max(0.5f, cutoffFreq);
        dcBlockerCoeff = 1.0f - (6.283185f * dcCutoff / static_cast<float>(sampleRate));
    }

    void updateHFRolloff(float cutoffFreq)
    {
        if (cutoffFreq <= 0.0f)
        {
            hfRolloffEnabled = false;
            hfRolloffCoeff = 1.0f;
            return;
        }
        hfRolloffEnabled = true;
        // One-pole lowpass coefficient (correct exponential form)
        float w = 6.283185f * cutoffFreq / static_cast<float>(sampleRate);
        hfRolloffCoeff = 1.0f - std::exp(-w);
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

        output += harmonics.h2 * x2;  // H2 (even — asymmetry/warmth)
        output += harmonics.h3 * x3;  // H3 (odd — symmetric)
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

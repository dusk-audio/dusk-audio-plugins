/*
  ==============================================================================

    WowFlutter.h
    Tape Echo - RE-201 Style Wow and Flutter

    Dual LFO system modeling tape transport instabilities:
    - Wow: Slow pitch drift (0.5-2Hz, +/-3-8 cents)
    - Flutter: Faster modulation (5-10Hz, +/-1-2 cents)
    With random modulation depth variation for natural character.

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <random>
#include <cmath>

namespace TapeEchoDSP
{

class WowFlutter
{
public:
    WowFlutter() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        currentSampleRate = sampleRate;

        // Reset phases
        wowPhase = 0.0f;
        flutterPhase = 0.0f;
        randomPhase = 0.0f;

        // Smoothed amount
        amountSmoothed.reset(sampleRate, 0.05);

        // Random generator for natural variation
        randomDist = std::uniform_real_distribution<float>(-1.0f, 1.0f);

        juce::ignoreUnused(maxBlockSize);
    }

    void reset()
    {
        wowPhase = 0.0f;
        flutterPhase = 0.0f;
        randomPhase = 0.0f;
    }

    // Set overall wow/flutter amount (0.0 to 1.0)
    void setAmount(float amount)
    {
        amountSmoothed.setTargetValue(amount);
    }

    // Set wow rate in Hz (default 0.5-2 Hz)
    void setWowRate(float rateHz)
    {
        wowRate = juce::jlimit(0.2f, 3.0f, rateHz);
    }

    // Set flutter rate in Hz (default 5-10 Hz)
    void setFlutterRate(float rateHz)
    {
        flutterRate = juce::jlimit(3.0f, 15.0f, rateHz);
    }

    // Get the current modulation value (in cents deviation)
    // Call once per sample and use to modulate delay time
    float getNextModulationCents()
    {
        const float amount = amountSmoothed.getNextValue();

        if (amount < 0.001f)
            return 0.0f;

        // Calculate wow component (slow, larger deviation)
        const float wowIncrement = wowRate / static_cast<float>(currentSampleRate);
        wowPhase += wowIncrement;
        if (wowPhase >= 1.0f) wowPhase -= 1.0f;

        // Wow uses a sine wave with slight nonlinearity for character
        float wowValue = std::sin(wowPhase * juce::MathConstants<float>::twoPi);
        wowValue = wowValue + 0.1f * wowValue * wowValue * wowValue;  // Add slight 3rd harmonic

        // Calculate flutter component (fast, smaller deviation)
        const float flutterIncrement = flutterRate / static_cast<float>(currentSampleRate);
        flutterPhase += flutterIncrement;
        if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;

        float flutterValue = std::sin(flutterPhase * juce::MathConstants<float>::twoPi);

        // Add random modulation for natural tape character
        randomPhase += 0.7f / static_cast<float>(currentSampleRate);  // ~0.7Hz random variation
        if (randomPhase >= 1.0f)
        {
            randomPhase -= 1.0f;
            lastRandomValue = targetRandomValue;
            targetRandomValue = randomDist(randomGen) * 0.5f;  // +/-50% random variation
        }

        // Smooth interpolation of random value
        float randomMod = lastRandomValue + (targetRandomValue - lastRandomValue) * randomPhase;

        // Combine modulations with amount scaling
        // Wow: +/-5 cents at max, Flutter: +/-1.5 cents at max
        const float wowCents = wowValue * 5.0f * amount * (1.0f + randomMod * 0.3f);
        const float flutterCents = flutterValue * 1.5f * amount * (1.0f + randomMod * 0.2f);

        return wowCents + flutterCents;
    }

    // Convert cents deviation to delay time multiplier
    static float centsToDelayMultiplier(float cents)
    {
        // 100 cents = 1 semitone = 2^(1/12) ratio
        // cents/1200 = semitones/12 = octaves
        return std::pow(2.0f, cents / 1200.0f);
    }

    // Get modulation as delay time multiplier (convenient form)
    float getNextDelayMultiplier()
    {
        return centsToDelayMultiplier(getNextModulationCents());
    }

private:
    double currentSampleRate = 44100.0;

    // LFO phases (0-1 range)
    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    float randomPhase = 0.0f;

    // LFO rates
    float wowRate = 1.0f;      // Hz
    float flutterRate = 7.0f;  // Hz

    // Random variation
    std::mt19937 randomGen { std::random_device{}() };
    std::uniform_real_distribution<float> randomDist { -1.0f, 1.0f };
    float lastRandomValue = 0.0f;
    float targetRandomValue = 0.0f;

    // Amount smoothing
    juce::SmoothedValue<float> amountSmoothed { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WowFlutter)
};

} // namespace TapeEchoDSP

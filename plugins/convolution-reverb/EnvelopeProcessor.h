/*
  ==============================================================================

    Convolution Reverb - Envelope Processor
    Attack/Decay/Length envelope for IR shaping
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

class EnvelopeProcessor
{
public:
    EnvelopeProcessor() = default;
    ~EnvelopeProcessor() = default;

    // Set attack time (0-1 normalized, maps to 0-500ms)
    void setAttack(float attackNormalized)
    {
        attack = juce::jlimit(0.0f, 1.0f, attackNormalized);
    }

    // Set decay shape (0-1 normalized)
    // 0 = instant drop after attack, 1 = natural decay preserved
    void setDecay(float decayNormalized)
    {
        decay = juce::jlimit(0.0f, 1.0f, decayNormalized);
    }

    // Set length (0-1 normalized, maps to 0-100% of original IR)
    void setLength(float lengthNormalized)
    {
        length = juce::jlimit(0.01f, 1.0f, lengthNormalized); // Min 1% to avoid empty IR
    }

    float getAttack() const { return attack; }
    float getDecay() const { return decay; }
    float getLength() const { return length; }

    // Get attack time in milliseconds
    float getAttackMs() const
    {
        return attack * 500.0f; // 0-500ms range
    }

    // Get length as percentage
    float getLengthPercent() const
    {
        return length * 100.0f;
    }

    // Process IR buffer in place
    void processIR(juce::AudioBuffer<float>& ir, double sampleRate) const
    {
        if (ir.getNumSamples() == 0)
            return;

        int numSamples = ir.getNumSamples();

        // Apply length truncation
        int newLength = static_cast<int>(numSamples * length);
        newLength = std::max(64, newLength); // Minimum 64 samples

        if (newLength < numSamples)
        {
            ir.setSize(ir.getNumChannels(), newLength, true, true, false);
            numSamples = newLength;
        }

        // Calculate attack samples
        float attackTimeSec = attack * 0.5f; // 0-500ms
        int attackSamples = static_cast<int>(attackTimeSec * sampleRate);
        attackSamples = std::min(attackSamples, numSamples);

        // Apply envelope to each channel
        for (int channel = 0; channel < ir.getNumChannels(); ++channel)
        {
            float* data = ir.getWritePointer(channel);

            for (int i = 0; i < numSamples; ++i)
            {
                float envelope = getEnvelopeValue(static_cast<float>(i) / numSamples,
                                                   static_cast<float>(attackSamples) / numSamples);
                data[i] *= envelope;
            }
        }
    }

    // Generate envelope curve for visualization
    std::vector<float> getEnvelopeCurve(int numPoints) const
    {
        std::vector<float> curve(numPoints);

        // Calculate normalized attack position
        float attackNormalizedPos = attack * 0.5f / 5.0f; // Assuming max IR is ~5 seconds

        for (int i = 0; i < numPoints; ++i)
        {
            float position = static_cast<float>(i) / static_cast<float>(numPoints - 1);

            // Only show envelope up to the length cutoff
            if (position > length)
            {
                curve[i] = 0.0f;
            }
            else
            {
                // Normalize position within the active length
                float normalizedPos = position / length;
                curve[i] = getEnvelopeValue(normalizedPos, attack * 0.25f); // Scaled for visualization
            }
        }

        return curve;
    }

private:
    float attack = 0.0f;  // 0-1, maps to 0-500ms fade in
    float decay = 1.0f;   // 0-1, decay shape (1 = natural)
    float length = 1.0f;  // 0-1, IR length percentage

    // Calculate envelope value at a given position
    float getEnvelopeValue(float position, float attackPosition) const
    {
        float envelope = 1.0f;

        // Attack phase (fade in with smooth curve)
        if (position < attackPosition && attackPosition > 0.0f)
        {
            float attackProgress = position / attackPosition;
            // Use smooth cosine curve for attack
            envelope = 0.5f * (1.0f - std::cos(attackProgress * juce::MathConstants<float>::pi));
        }
        // Decay phase
        else if (decay < 1.0f)
        {
            float decayStart = attackPosition;
            float denominator = 1.0f - decayStart;
            float decayPosition = (denominator > 1e-6f) ? (position - decayStart) / denominator : 1.0f;
            decayPosition = juce::jlimit(0.0f, 1.0f, decayPosition);

            // Apply decay curve - exponential-ish falloff
            // decay = 1.0 means no modification, decay = 0 means instant drop
            float decayPower = 2.0f - decay * 2.0f; // Maps to 0-2 exponent
            float decayMultiplier = std::pow(1.0f - decayPosition, decayPower);

            // Blend between natural (1.0) and shaped decay
            envelope = decay + (1.0f - decay) * decayMultiplier;
        }

        return juce::jlimit(0.0f, 1.0f, envelope);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopeProcessor)
};

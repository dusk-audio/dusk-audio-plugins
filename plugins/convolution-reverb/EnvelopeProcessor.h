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

    // Samples of an irNumSamples-long IR that survive processIR()'s length
    // truncation. Mirrors it exactly, including the 64-sample floor and the fact
    // that it only ever shrinks the buffer.
    int getActiveSamples(int irNumSamples) const
    {
        if (irNumSamples <= 0)
            return 0;
        return std::min(std::max(64, static_cast<int>(irNumSamples * length)), irNumSamples);
    }

    // The same thing as a fraction of the untruncated IR. Every drawing that
    // marks where the IR ends -- the envelope curve, the cutoff line, the shaded
    // truncated region -- must use THIS and not `length`, or they disagree with
    // each other and with the DSP once the 64-sample floor bites. With no IR
    // there is no sample count to derive it from, so fall back to the knob.
    float getActiveFraction(int irNumSamples) const
    {
        if (irNumSamples <= 0)
            return length;
        return static_cast<float>(getActiveSamples(irNumSamples)) / static_cast<float>(irNumSamples);
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

    // Generate envelope curve for visualization.
    //
    // Takes the UNTRUNCATED IR's sample count and rate because the drawn curve
    // has to reproduce processIR()'s arithmetic, not approximate it. The attack
    // is a fixed wall-clock span (0..500 ms), so the FRACTION of the IR it
    // occupies depends on how long that IR is: the same knob covers half of a 1 s
    // IR and a tenth of a 5 s one. This used to pass a flat attack * 0.25f
    // "scaled for visualization", which happens to be right only for a 2 s IR at
    // full length and drew a visibly wrong envelope for everything else.
    std::vector<float> getEnvelopeCurve(int numPoints, int irNumSamples, double irSampleRate) const
    {
        std::vector<float> curve(numPoints);

        // Mirror processIR() step for step, in samples: truncate to `length`,
        // floor the result at 64 samples, never extend past the original buffer,
        // then measure the attack against whatever survived. Working in seconds
        // instead would drop the 64-sample floor and draw the wrong ramp for a
        // short IR at a small length.
        const int activeSamples = getActiveSamples(irNumSamples);
        const float activeFraction = getActiveFraction(irNumSamples);

        // Zero, not one: with no usable sample rate processIR() computes zero
        // attack samples and applies NO fade, so the drawing must not paint one.
        // An attackFraction of 1.0 would ramp the whole curve up from silence.
        float attackFraction = 0.0f;
        if (activeSamples > 0 && irSampleRate > 0.0)
        {
            const int attackSamples = std::min(static_cast<int>(attack * 0.5f * irSampleRate),
                                               activeSamples);
            attackFraction = static_cast<float>(attackSamples) / static_cast<float>(activeSamples);
        }

        for (int i = 0; i < numPoints; ++i)
        {
            float position = static_cast<float>(i) / static_cast<float>(numPoints - 1);

            // Only show envelope up to the effective length cutoff
            if (position > activeFraction || activeFraction <= 0.0f)
            {
                curve[i] = 0.0f;
            }
            else
            {
                // Normalize position within the surviving length
                float normalizedPos = position / activeFraction;
                curve[i] = getEnvelopeValue(normalizedPos, attackFraction);
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

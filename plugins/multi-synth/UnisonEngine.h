#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// Unison engine: stacks multiple detuned copies of a voice across the stereo field.
namespace MultiSynthDSP
{

static constexpr int kMaxUnisonVoices = 8;

struct UnisonVoiceState
{
    float detuneAmount = 0.0f; // In cents, applied to oscillator
    float panPosition = 0.0f;  // -1 to +1
};

class UnisonEngine
{
public:
    void setVoiceCount(int count)
    {
        numVoices = juce::jlimit(1, kMaxUnisonVoices, count);
        recalculate();
    }

    void setDetune(float detuneMaxCents)
    {
        detuneCents = detuneMaxCents;
        recalculate();
    }

    void setStereoSpread(float spread)
    {
        stereoSpread = juce::jlimit(0.0f, 1.0f, spread);
        recalculate();
    }

    int getVoiceCount() const { return numVoices; }

    const UnisonVoiceState& getVoiceState(int index) const
    {
        return voices[static_cast<size_t>(index)];
    }

    // Mix unison samples into stereo output
    void mixToStereo(const float* monoSamples, int count, float& outL, float& outR) const
    {
        outL = 0.0f;
        outR = 0.0f;

        float gain = 1.0f / std::sqrt(static_cast<float>(numVoices));

        for (int i = 0; i < count && i < numVoices; ++i)
        {
            float pan = voices[static_cast<size_t>(i)].panPosition;
            // Constant-power panning
            float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            float l = std::cos(angle);
            float r = std::sin(angle);
            outL += monoSamples[i] * l * gain;
            outR += monoSamples[i] * r * gain;
        }
    }

private:
    void recalculate()
    {
        if (numVoices == 1)
        {
            voices[0].detuneAmount = 0.0f;
            voices[0].panPosition = 0.0f;
            return;
        }

        for (int i = 0; i < numVoices; ++i)
        {
            // Spread detune evenly: -max to +max
            float t = static_cast<float>(i) / static_cast<float>(numVoices - 1);
            voices[static_cast<size_t>(i)].detuneAmount = (t * 2.0f - 1.0f) * detuneCents;
            voices[static_cast<size_t>(i)].panPosition = (t * 2.0f - 1.0f) * stereoSpread;
        }
    }

    int numVoices = 1;
    float detuneCents = 10.0f;
    float stereoSpread = 1.0f;
    std::array<UnisonVoiceState, kMaxUnisonVoices> voices;
};

} // namespace MultiSynthDSP

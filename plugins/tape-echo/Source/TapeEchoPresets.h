#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace TapeEchoPresets
{

struct Preset
{
    const char* name;
    const char* category;

    int   mode         = 1;       // 1-12 (head combinations)
    float inputGain    = 0.0f;    // -12 to 12 dB
    float repeatRate   = 1.0f;    // 0.5 to 2.0
    float intensity    = 50.0f;   // 0 to 110%
    float echoVolume   = -12.0f;  // -60 to 0 dB
    float reverbVolume = -60.0f;  // -60 to 0 dB
    float bass         = 0.0f;    // -6 to 6 dB
    float treble       = 0.0f;    // -6 to 6 dB
    float wowFlutter   = 15.0f;   // 0 to 100%
    float dryWet       = 50.0f;   // 0 to 100%
    bool  tempoSync    = false;   // tempo sync on/off
    int   noteDivision = 0;       // 0-14 (note subdivision index)

    void applyTo (juce::AudioProcessorValueTreeState& apvts) const
    {
        auto setParam = [&apvts] (const juce::String& paramID, float actualValue)
        {
            if (auto* param = apvts.getParameter (paramID))
                param->setValueNotifyingHost (param->convertTo0to1 (actualValue));
        };

        setParam ("mode",         static_cast<float> (mode));
        setParam ("inputGain",    inputGain);
        setParam ("repeatRate",   repeatRate);
        setParam ("intensity",    intensity);
        setParam ("echoVolume",   echoVolume);
        setParam ("reverbVolume", reverbVolume);
        setParam ("bass",         bass);
        setParam ("treble",       treble);
        setParam ("wowFlutter",   wowFlutter);
        setParam ("dryWet",       dryWet);
        setParam ("tempoSync",    tempoSync ? 1.0f : 0.0f);
        setParam ("noteDivision", static_cast<float> (noteDivision));
    }
};

inline const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets = {
        // --- Classic ---
        { "Slapback", "Classic",
          1, 0.0f, 1.8f, 20.0f, -6.0f, -60.0f, 0.0f, 0.0f, 10.0f, 40.0f },

        { "Single Echo", "Classic",
          1, 0.0f, 1.0f, 40.0f, -8.0f, -60.0f, 0.0f, 0.0f, 15.0f, 50.0f },

        { "Long Delay", "Classic",
          1, 0.0f, 0.65f, 35.0f, -10.0f, -60.0f, -1.0f, -1.0f, 20.0f, 45.0f },

        // --- Multi-Head ---
        { "Dual Heads", "Multi-Head",
          5, 0.0f, 1.0f, 45.0f, -8.0f, -60.0f, 0.0f, 0.0f, 15.0f, 50.0f },

        { "Triple Echo", "Multi-Head",
          7, 0.0f, 1.2f, 40.0f, -10.0f, -60.0f, 0.0f, -1.0f, 12.0f, 45.0f },

        { "Rhythmic Pattern", "Multi-Head",
          11, 0.0f, 1.0f, 50.0f, -8.0f, -60.0f, 0.0f, 0.0f, 10.0f, 50.0f },

        // --- Ambient ---
        { "Space Echo", "Ambient",
          4, 0.0f, 0.8f, 75.0f, -6.0f, -10.0f, 1.0f, -2.0f, 25.0f, 55.0f },

        { "Dark Wash", "Ambient",
          7, 0.0f, 0.7f, 80.0f, -8.0f, -8.0f, 2.0f, -4.0f, 30.0f, 60.0f },

        { "Dub Echo", "Ambient",
          1, 2.0f, 0.75f, 95.0f, -4.0f, -12.0f, 3.0f, -3.0f, 35.0f, 55.0f },

        // --- Lo-Fi ---
        { "Worn Tape", "Lo-Fi",
          1, 0.0f, 1.0f, 45.0f, -10.0f, -60.0f, 2.0f, -3.0f, 60.0f, 45.0f },

        { "Warble", "Lo-Fi",
          5, 0.0f, 0.9f, 50.0f, -8.0f, -20.0f, 1.0f, -2.0f, 85.0f, 50.0f },

        { "Broken Machine", "Lo-Fi",
          11, 3.0f, 0.6f, 90.0f, -4.0f, -15.0f, 4.0f, -5.0f, 100.0f, 55.0f },

        // --- Mix-Ready ---
        { "Vocal Delay", "Mix-Ready",
          1, 0.0f, 1.0f, 30.0f, -10.0f, -60.0f, -1.0f, 1.0f, 8.0f, 35.0f },

        { "Guitar Ambience", "Mix-Ready",
          5, 0.0f, 1.3f, 25.0f, -14.0f, -20.0f, 0.0f, -1.0f, 12.0f, 30.0f },

        { "Stereo Width", "Mix-Ready",
          8, 0.0f, 1.1f, 35.0f, -12.0f, -60.0f, 0.0f, 0.0f, 10.0f, 40.0f },
    };
    return presets;
}

} // namespace TapeEchoPresets

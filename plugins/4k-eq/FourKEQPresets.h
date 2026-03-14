#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace FourKEQPresets
{

struct Preset
{
    const char* name;
    const char* category;

    float lf_gain     = 0.0f;
    float lf_freq     = 100.0f;
    float lf_bell     = 0.0f;      // 0 = shelf, 1 = bell
    float lm_gain     = 0.0f;
    float lm_freq     = 600.0f;
    float lm_q        = 0.7f;
    float hm_gain     = 0.0f;
    float hm_freq     = 2000.0f;
    float hm_q        = 0.7f;
    float hf_gain     = 0.0f;
    float hf_freq     = 8000.0f;
    float hf_bell     = 0.0f;      // 0 = shelf, 1 = bell
    float hpf_freq    = 20.0f;
    float lpf_freq    = 20000.0f;
    float saturation  = 0.0f;
    float output_gain = 0.0f;
    float input_gain  = 0.0f;
    float eq_type     = 0.0f;      // 0 = Brown, 1 = Black

    void applyTo (juce::AudioProcessorValueTreeState& apvts) const
    {
        auto setParam = [&apvts] (const juce::String& paramID, float actualValue)
        {
            if (auto* param = apvts.getParameter (paramID))
                param->setValueNotifyingHost (param->convertTo0to1 (actualValue));
        };

        setParam ("lf_gain",     lf_gain);
        setParam ("lf_freq",     lf_freq);
        setParam ("lf_bell",     lf_bell);
        setParam ("lm_gain",     lm_gain);
        setParam ("lm_freq",     lm_freq);
        setParam ("lm_q",        lm_q);
        setParam ("hm_gain",     hm_gain);
        setParam ("hm_freq",     hm_freq);
        setParam ("hm_q",        hm_q);
        setParam ("hf_gain",     hf_gain);
        setParam ("hf_freq",     hf_freq);
        setParam ("hf_bell",     hf_bell);
        setParam ("hpf_freq",    hpf_freq);
        setParam ("lpf_freq",    lpf_freq);
        setParam ("saturation",  saturation);
        setParam ("output_gain", output_gain);
        setParam ("input_gain",  input_gain);
        setParam ("eq_type",     eq_type);
    }
};

inline const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets = {
        // --- Vocals ---
        { "Vocal Presence", "Vocals",
          3.0f, 100.0f, 0.0f,
          -3.0f, 300.0f, 1.3f,
          4.0f, 3500.0f, 0.7f,
          2.0f, 8000.0f, 0.0f,
          80.0f, 20000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        // --- Drums ---
        { "Kick Punch", "Drums",
          4.0f, 50.0f, 0.0f,
          -2.5f, 200.0f, 0.8f,
          3.0f, 2000.0f, 1.5f,
          0.0f, 8000.0f, 0.0f,
          30.0f, 20000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        { "Snare Crack", "Drums",
          0.0f, 100.0f, 0.0f,
          4.0f, 250.0f, 0.7f,
          5.0f, 5000.0f, 1.2f,
          3.0f, 8000.0f, 1.0f,
          150.0f, 20000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        { "Drum Bus Punch", "Drums",
          4.0f, 70.0f, 0.0f,
          -3.0f, 350.0f, 0.6f,
          3.0f, 3500.0f, 1.0f,
          2.5f, 10000.0f, 0.0f,
          20.0f, 20000.0f, 25.0f, 0.0f, 0.0f, 1.0f },

        // --- Bass ---
        { "Bass Warmth", "Bass",
          4.0f, 80.0f, 0.0f,
          -3.0f, 400.0f, 0.7f,
          2.0f, 1500.0f, 0.7f,
          0.0f, 8000.0f, 0.0f,
          20.0f, 10000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        { "Bass Guitar Polish", "Bass",
          5.0f, 60.0f, 0.0f,
          -2.0f, 250.0f, 1.0f,
          3.0f, 1200.0f, 0.8f,
          2.0f, 4500.0f, 1.0f,
          35.0f, 20000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        // --- Guitar ---
        { "Acoustic Guitar", "Guitar",
          -2.0f, 100.0f, 0.0f,
          2.0f, 200.0f, 0.7f,
          3.0f, 2500.0f, 0.9f,
          4.0f, 12000.0f, 0.0f,
          80.0f, 20000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        // --- Keys ---
        { "Piano Brilliance", "Keys",
          2.0f, 80.0f, 0.0f,
          -2.5f, 500.0f, 0.8f,
          3.0f, 2000.0f, 0.7f,
          3.5f, 8000.0f, 0.0f,
          30.0f, 20000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        // --- Mix Bus ---
        { "Bright Mix", "Mix Bus",
          2.0f, 60.0f, 0.0f,
          0.0f, 600.0f, 0.7f,
          -2.0f, 2500.0f, 0.8f,
          2.5f, 10000.0f, 0.0f,
          20.0f, 20000.0f, 20.0f, 0.0f, 0.0f, 0.0f },

        { "Glue Bus", "Mix Bus",
          2.0f, 100.0f, 0.0f,
          0.0f, 600.0f, 0.7f,
          -1.5f, 3000.0f, 0.7f,
          2.0f, 10000.0f, 0.0f,
          20.0f, 20000.0f, 20.0f, 0.0f, 0.0f, 0.0f },

        // --- Creative ---
        { "Telephone EQ", "Creative",
          0.0f, 100.0f, 0.0f,
          6.0f, 1000.0f, 1.5f,
          0.0f, 2000.0f, 0.7f,
          0.0f, 8000.0f, 0.0f,
          300.0f, 3000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        { "Air & Silk", "Creative",
          0.0f, 100.0f, 0.0f,
          0.0f, 600.0f, 0.7f,
          3.0f, 7000.0f, 0.7f,
          4.0f, 15000.0f, 0.0f,
          20.0f, 20000.0f, 0.0f, 0.0f, 0.0f, 0.0f },

        // --- Mastering ---
        { "Master Sheen", "Mastering",
          0.0f, 100.0f, 0.0f,
          0.0f, 600.0f, 0.7f,
          1.0f, 5000.0f, 0.7f,
          1.5f, 16000.0f, 0.0f,
          20.0f, 20000.0f, 10.0f, 0.0f, 0.0f, 0.0f },

        { "Master Bus Sweetening", "Mastering",
          1.0f, 50.0f, 0.0f,
          -1.0f, 600.0f, 0.5f,
          0.5f, 4000.0f, 0.6f,
          1.5f, 15000.0f, 0.0f,
          20.0f, 20000.0f, 15.0f, -0.5f, 0.0f, 0.0f },
    };
    return presets;
}

} // namespace FourKEQPresets

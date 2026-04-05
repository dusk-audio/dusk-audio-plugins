#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>

class CabinetIR
{
public:
    CabinetIR();
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& buffer);
    void reset();

    void loadIR (const juce::File& file);
    void setEnabled (bool on);
    void setMix (float mix01);
    void setHiCut (float hz);
    void setLoCut (float hz);
    void setNormalize (bool on);

    bool isLoaded() const { return irLoaded_; }
    bool isNormalized() const { return normalize_; }
    juce::String getLoadedFileName() const { return loadedFileName_; }
    juce::File getLoadedFile() const { return loadedFile_; }

private:
    juce::dsp::ConvolutionMessageQueue messageQueue_;
    juce::dsp::Convolution convolution_;
    juce::dsp::ProcessSpec currentSpec_ {};

    bool enabled_ = true;
    bool irLoaded_ = false;
    bool normalize_ = true;
    float mix_ = 1.0f;
    float normGain_ = 1.0f; // Computed on IR load to normalize volume
    juce::String loadedFileName_;
    juce::File loadedFile_;

    // Post-cab EQ (manual biquad to avoid heap allocation from juce::dsp::IIR)
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x)
        {
            float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out;
            // Flush denormals
            if (std::abs(z1) < 1e-15f) z1 = 0.0f;
            if (std::abs(z2) < 1e-15f) z2 = 0.0f;
            return out;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    Biquad hiCutFilter_, loCutFilter_;
    float hiCutFreq_ = 12000.0f;
    float loCutFreq_ = 60.0f;
    bool filtersDirty_ = true;

    // Pre-allocated dry buffer for mix blending (avoids audio-thread allocation)
    juce::AudioBuffer<float> dryBuffer_;

    void updateFilters();
};

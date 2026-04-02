#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

class CabinetIR
{
public:
    CabinetIR();
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& buffer);
    void reset();

    void loadIR (const juce::File& file);
    void loadIRFromBinaryData (const void* data, size_t sizeInBytes, const juce::String& name);
    void setEnabled (bool on);
    void setMix (float mix01);
    void setHiCut (float hz);
    void setLoCut (float hz);
    void setAutoGain (bool on);

    bool isLoaded() const { return irLoaded_; }
    bool isFactoryIR() const { return isFactoryIR_; }
    bool isAutoGainEnabled() const { return autoGainEnabled_; }
    float getAutoGainDB() const { return autoGainDB_; }
    juce::String getLoadedFileName() const { return loadedFileName_; }
    juce::File getLoadedFile() const { return loadedFile_; }

    // IR waveform thumbnail (128 peak values, 0-1 normalized) for UI display
    static constexpr int kThumbnailSize = 128;
    const std::array<float, kThumbnailSize>& getThumbnail() const { return thumbnail_; }
    bool hasThumbnail() const { return thumbnailReady_; }

private:
    juce::dsp::ConvolutionMessageQueue messageQueue_;
    juce::dsp::Convolution convolution_;
    juce::dsp::ProcessSpec currentSpec_ {};

    bool enabled_ = true;
    bool irLoaded_ = false;
    bool isFactoryIR_ = false;
    float mix_ = 1.0f;
    juce::String loadedFileName_;
    juce::File loadedFile_;

    // Autogain: normalize IR loudness so different IRs play at similar levels
    bool autoGainEnabled_ = false;
    float autoGainDB_ = 0.0f;       // computed correction (for UI display)
    float autoGainLinear_ = 1.0f;   // applied gain

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

    static constexpr int kMaxChannels = 2;
    static constexpr int kFilterCrossfadeSamples = 32;

    Biquad hiCutFilter_[kMaxChannels], loCutFilter_[kMaxChannels];
    Biquad hiCutFilterOld_[kMaxChannels], loCutFilterOld_[kMaxChannels];
    int filterCrossfadeRemaining_ = 0;

    float hiCutFreq_ = 12000.0f;
    float loCutFreq_ = 60.0f;
    bool filtersDirty_ = true;

    // Pre-allocated dry buffer for mix blending (avoids audio-thread allocation)
    juce::AudioBuffer<float> dryBuffer_;

    // IR waveform thumbnail
    std::array<float, kThumbnailSize> thumbnail_ {};
    bool thumbnailReady_ = false;
    void buildThumbnail (const float* irData, int numSamples);

    void updateFilters();
    void computeAutoGain (const float* irData, int numSamples);
    void computeAutoGainFromFile (const juce::File& file);
    void computeAutoGainFromBinaryData (const void* data, size_t sizeInBytes);
};

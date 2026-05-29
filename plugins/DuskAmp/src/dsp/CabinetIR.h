#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <functional>

class CabinetIR
{
public:
    CabinetIR();
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& buffer);
    void reset();

    // Disk-loaded IR (user-picked file). Returns true if the file passed
    // pre-checks and the convolution loader accepted the request. JUCE's
    // loader runs async on its own worker, so true does not guarantee the
    // audio is up — but a false return always means the IR did NOT load.
    bool loadIR (const juce::File& file);

    // Memory-loaded IR (bundled BinaryData). `displayName` is shown in the
    // editor; the file accessor returns an invalid juce::File since the
    // source isn't on disk.
    void loadIR (const void* data, size_t sizeInBytes, const juce::String& displayName);

    // Last load error (empty on success). UI surfaces this in footer.
    juce::String getLastError() const { return lastError_; }

    void setEnabled (bool on);
    void setMix (float mix01);
    void setHiCut (float hz);
    void setLoCut (float hz);
    void setNormalize (bool on);

    bool isLoaded() const { return irLoaded_.load (std::memory_order_acquire); }
    juce::String getLoadedFileName() const { return loadedFileName_; }
    juce::File getLoadedFile() const { return loadedFile_; }

private:
    juce::dsp::ConvolutionMessageQueue messageQueue_;
    juce::dsp::Convolution convolution_;
    juce::dsp::ProcessSpec currentSpec_ {};

    bool enabled_ = true;
    // irLoaded_ + normalizeMakeup_ are written from the background cab-load
    // worker (DuskAmpProcessor::cabLoadPool_) and read by the audio thread
    // in process(). Plain bool/float would be a torn-read race; std::atomic
    // with relaxed ordering on the audio side keeps it lock-free.
    std::atomic<bool> irLoaded_ { false };
    float mix_ = 1.0f;
    juce::String loadedFileName_;
    juce::File loadedFile_;
    juce::String lastError_;

    // Loudness-match makeup: cab IRs are bandpass — lots of spectral content
    // gets cut. Computed at IR load time by running pink noise through a
    // separate juce::dsp::Convolution loaded with the same IR (so the
    // makeup is measured against JUCE's exact normalise+trim+convolve path)
    // and storing inputRms / outputRms as the broadband-RMS makeup gain.
    // Toggled by setNormalize(); 1.0 when off.
    bool  normalize_ = false;
    std::atomic<float> normalizeMakeup_ { 1.0f };

    // Loader is a callable that knows how to call loadImpulseResponse on
    // the given measureConv (file-based or memory-based). The rest of the
    // pink-noise-RMS measurement is loader-agnostic.
    void measureNormalizeMakeup (std::function<void (juce::dsp::Convolution&)> loader);

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

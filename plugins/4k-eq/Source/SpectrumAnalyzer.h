/*
  ==============================================================================

    SpectrumAnalyzer.h - Professional spectrum analyzer for 4K EQ

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

class SpectrumAnalyzer : public juce::Component,
                         public juce::Timer
{
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void pushBuffer(const juce::AudioBuffer<float>& buffer);

    // Display modes
    enum class Mode
    {
        PreEQ,
        PostEQ,
        PrePost,
        Sidechain
    };

    void setMode(Mode newMode) { mode = newMode; }
    void setFrequencyRange(float minHz, float maxHz);
    void setDecibelRange(float minDb, float maxDb);

    // Visual settings
    void setShowGrid(bool show) { showGrid = show; }
    void setShowPeakHold(bool show) { showPeakHold = show; }
    void setAveraging(float amount) { averaging = juce::jlimit(0.0f, 0.99f, amount); }
    void setRefreshRate(int hz);

    // Band visualization
    struct EQBand
    {
        float frequency = 1000.0f;
        float gain = 0.0f;
        float q = 0.7f;
        bool bypassed = false;
        juce::Colour colour;
    };

    void updateEQBands(const std::array<EQBand, 4>& bands);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Musical note detection
    void enableNoteDetection(bool enable) { noteDetection = enable; }
    float getDetectedFrequency() const { return detectedFrequency; }
    juce::String getDetectedNote() const;

private:
    // FFT setup
    static constexpr int fftOrder = 12;  // 4096 samples
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int scopeSize = fftSize / 2;

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    // Buffers
    std::array<float, fftSize> fftData;
    std::array<float, scopeSize> scopeData;
    std::array<float, scopeSize> scopeDataSmoothed;
    std::array<float, scopeSize> peakHoldData;
    std::array<int, scopeSize> peakHoldCountdown;

    juce::AudioBuffer<float> fifoBuffer;
    int fifoIndex = 0;

    // Processing
    void processFFT();
    void drawFrame(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawSpectrum(juce::Graphics& g, const std::array<float, scopeSize>& data,
                      const juce::Colour& colour, float alpha = 1.0f);
    void drawEQCurve(juce::Graphics& g);
    void drawFrequencyLabels(juce::Graphics& g);

    // Display settings
    Mode mode = Mode::PrePost;
    float minFreq = 20.0f;
    float maxFreq = 20000.0f;
    float minDb = -60.0f;
    float maxDb = 12.0f;
    bool showGrid = true;
    bool showPeakHold = true;
    float averaging = 0.5f;
    bool noteDetection = false;

    // EQ visualization
    std::array<EQBand, 4> eqBands;
    bool showEQCurve = true;

    // Paths for drawing
    juce::Path spectrumPath;
    juce::Path eqPath;

    // Musical note detection
    std::atomic<float> detectedFrequency{0.0f};
    void detectFundamentalFrequency();

    // Sample rate
    double sampleRate = 44100.0;

    // Coordinate mapping
    float mapFrequencyToX(float freq) const;
    float mapMagnitudeToY(float magnitude) const;
    float getFrequencyForX(float x) const;
    float getMagnitudeForY(float y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};

//==============================================================================
// Mid/Side processor for 4K EQ
//==============================================================================
class MidSideProcessor
{
public:
    MidSideProcessor();

    enum class Mode
    {
        Stereo,
        MidSide,
        LeftRight,
        MidOnly,
        SideOnly
    };

    void setMode(Mode newMode) { mode = newMode; }
    Mode getMode() const { return mode; }

    void processToMidSide(const float* left, const float* right,
                         float* mid, float* side, int numSamples);

    void processFromMidSide(const float* mid, const float* side,
                           float* left, float* right, int numSamples);

    // Width control (-100 to +100)
    void setStereoWidth(float widthPercent);

    // Get correlation
    float getCorrelation() const { return correlation.load(); }

private:
    Mode mode = Mode::Stereo;
    float width = 100.0f;

    std::atomic<float> correlation{0.0f};
    juce::dsp::IIR::Filter<float> correlationFilter;

    void updateCorrelation(const float* left, const float* right, int numSamples);
};

//==============================================================================
// Dynamic EQ processor
//==============================================================================
class DynamicEQBand
{
public:
    DynamicEQBand();

    void prepare(double sampleRate);
    void reset();

    // EQ parameters
    void setFrequency(float hz);
    void setGain(float db);
    void setQ(float q);

    // Dynamic parameters
    void setThreshold(float db);
    void setRatio(float ratio);
    void setAttack(float ms);
    void setRelease(float ms);
    void setDynamicEnabled(bool enabled) { dynamicEnabled = enabled; }

    // Sidechain
    void setSidechainInput(const float* sidechain, int numSamples);
    void setSidechainFilterFreq(float hz);

    float processSample(float input);

    // Get current gain reduction
    float getGainReduction() const { return currentGainReduction; }

private:
    // EQ filter
    juce::dsp::IIR::Filter<float> eqFilter;

    // Dynamics
    bool dynamicEnabled = false;
    float threshold = 0.0f;
    float ratio = 4.0f;
    float attack = 1.0f;
    float release = 100.0f;

    // Envelope
    float envelope = 0.0f;
    float currentGainReduction = 0.0f;

    // Sidechain
    juce::dsp::IIR::Filter<float> sidechainFilter;
    const float* sidechainBuffer = nullptr;
    int sidechainSamples = 0;

    double sampleRate = 44100.0;

    void updateCoefficients();
    float computeGainReduction(float inputLevel);
};
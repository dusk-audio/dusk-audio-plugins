#pragma once

#include "../../shared-dpf/dsp/DuskFft.hpp"
#include "SpectrumRing.hpp"

#include <array>
#include <atomic>
#include <vector>

//==============================================================================
/**
    FFT Spectrum Analyzer Processor

    Features:
    - Configurable FFT resolution (2048/4096/8192)
    - Thread-safe FIFO for audio capture
    - Logarithmic frequency mapping (20Hz - 20kHz)
    - Spectrum smoothing
    - dB/octave slope adjustment
    - Peak hold with configurable decay

    Framework-free port of the JUCE original: juce::dsp::FFT is replaced by
    duskaudio::FFTr2 (shared-dpf/dsp/DuskFft.hpp) plus an explicit magnitude
    pass, the Hann window table reproduces juce::dsp::WindowingFunction's
    normalise=true behaviour (coefficients scaled by size/sum so a full-scale
    sine reads 0 dB), and the two juce::AbstractFifo instances became
    SpectrumRing. The display math (log bin mapping, smoothing, slope, decay,
    peak hold) is unchanged.
*/
class FFTProcessor
{
public:
    static constexpr int DISPLAY_BINS = 2048;
    static constexpr int MAX_FFT_ORDER = 13;  // 8192

    enum class Resolution
    {
        Low = 11,     // 2048
        Medium = 12,  // 4096
        High = 13     // 8192
    };

    FFTProcessor();
    ~FFTProcessor() = default;

    //==========================================================================
    // Setup
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    //==========================================================================
    // Audio thread: push samples to FIFO
    void pushSamples(const float* left, const float* right, int numSamples);

    //==========================================================================
    // Timer thread: process FFT and update magnitudes
    void processFFT();

    //==========================================================================
    // Settings
    void setResolution(Resolution res);
    void setSmoothing(float smoothing);      // 0-1 (0=none, 1=max)
    void setSlope(float dbPerOctave);        // -4.5 to +4.5
    void setDecayRate(float dbPerSecond);    // 3-60
    void setPeakHoldEnabled(bool enabled);
    void setPeakHoldTime(float seconds);

    //==========================================================================
    // Data access (for UI)
    const std::array<float, DISPLAY_BINS>& getMagnitudes() const { return displayMagnitudes; }
    const std::array<float, DISPLAY_BINS>& getPeakHold() const { return peakHoldMagnitudes; }
    bool isDataReady() const { return dataReady.load(); }
    void clearDataReady() { dataReady.store(false); }

    //==========================================================================
    // Coordinate helpers
    static float getFrequencyForBin(int bin);
    static int getBinForFrequency(float freq);

private:
    void updateFFTSize(Resolution resolution);

    //==========================================================================
    double sampleRate = 44100.0;
    int currentFFTSize = 4096;
    Resolution currentResolution = Resolution::Medium;

    // FFT + window (framework-free)
    duskaudio::FFTr2 fft;
    bool fftPrepared = false;
    std::vector<float> hannWindow;   // normalised like JUCE (DC gain of one)

    // Thread-safe FIFO for stereo audio
    SpectrumRing fifoL { 16384 };
    SpectrumRing fifoR { 16384 };

    // FFT working buffers
    std::vector<float> fftInputL;
    std::vector<float> fftInputR;
    std::vector<float> fftRe;
    std::vector<float> fftIm;

    // Output magnitudes
    std::array<float, DISPLAY_BINS> displayMagnitudes{};
    std::array<float, DISPLAY_BINS> peakHoldMagnitudes{};
    std::array<float, DISPLAY_BINS> smoothedMagnitudes{};

    // Peak hold timing
    std::array<int, DISPLAY_BINS> peakHoldCounters{};

    // Settings
    float smoothingFactor = 0.5f;
    float slopeDbPerOctave = 0.0f;
    float decayRate = 20.0f;
    bool peakHoldEnabled = true;
    float peakHoldTime = 2.0f;
    int peakHoldSamples = 60;  // At 30 Hz refresh

    std::atomic<bool> dataReady{false};

    // Frequency mapping constants
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
};

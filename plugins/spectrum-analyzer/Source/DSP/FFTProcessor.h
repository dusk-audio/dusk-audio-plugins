#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <atomic>

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
    void applySlope(float* magnitudes, int numBins);

    //==========================================================================
    double sampleRate = 44100.0;
    int currentFFTSize = 4096;
    Resolution currentResolution = Resolution::Medium;

    // FFT objects
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    // Thread-safe FIFO for stereo audio
    juce::AbstractFifo fifoL{16384};
    juce::AbstractFifo fifoR{16384};
    std::vector<float> audioBufferL;
    std::vector<float> audioBufferR;

    // FFT working buffers
    std::vector<float> fftInputL;
    std::vector<float> fftInputR;
    std::vector<float> fftWorkBuffer;

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

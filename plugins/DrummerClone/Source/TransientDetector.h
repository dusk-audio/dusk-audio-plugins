#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <deque>

/**
 * TransientDetector - Detects audio transients/onsets for Follow Mode
 *
 * Uses a combination of:
 * - High-pass filtering (100Hz) to focus on attack transients
 * - RMS envelope following with sliding window
 * - Peak picking with adaptive threshold
 * - Debouncing to prevent multiple triggers
 */
class TransientDetector
{
public:
    TransientDetector();
    ~TransientDetector() = default;

    /**
     * Prepare the detector for playback
     * @param sampleRate The audio sample rate
     */
    void prepare(double sampleRate);

    /**
     * Process an audio buffer and detect transients
     * @param buffer The audio buffer to analyze
     * @return Vector of onset times in seconds (relative to buffer start)
     */
    std::vector<double> process(const juce::AudioBuffer<float>& buffer);

    /**
     * Set detection sensitivity (affects threshold)
     * @param sensitivity 0.1 (less sensitive) to 0.8 (more sensitive)
     */
    void setSensitivity(float sensitivity);

    /**
     * Get the current RMS level (for UI metering)
     */
    float getCurrentRMS() const { return currentRMS; }

    /**
     * Get the number of transients detected in the last analysis window
     */
    int getRecentTransientCount() const { return recentTransientCount; }

    /**
     * Reset the detector state
     */
    void reset();

private:
    // Sample rate
    double sampleRate = 44100.0;

    // High-pass filter (100Hz, 2-pole IIR)
    juce::dsp::IIR::Filter<float> highPassFilter;
    juce::dsp::IIR::Coefficients<float>::Ptr highPassCoeffs;

    // RMS calculation
    static constexpr int RMS_WINDOW_MS = 5;  // 5ms sliding window
    int rmsWindowSamples = 220;
    std::deque<float> rmsBuffer;
    float currentRMS = 0.0f;
    float previousRMS = 0.0f;

    // Onset detection parameters
    float sensitivity = 0.5f;
    float threshold = 0.1f;           // Dynamic threshold
    float thresholdRiseDB = 3.0f;     // Energy rise required (in dB)

    // Debouncing
    static constexpr int DEBOUNCE_MS = 50;  // 50ms minimum between onsets
    int debounceSamples = 2205;
    int samplesSinceLastOnset = 0;

    // Ring buffer for 2-second analysis window
    static constexpr int BUFFER_SECONDS = 2;
    std::vector<float> audioRingBuffer;
    int ringBufferWritePos = 0;

    // Recent transient tracking
    int recentTransientCount = 0;
    std::vector<double> recentOnsets;

    // Helper methods
    void updateHighPassFilter();
    float calculateRMS(const float* samples, int numSamples);
    bool isOnset(float currentEnergy, float previousEnergy);
    void addToRingBuffer(const float* samples, int numSamples);
};

/**
 * Onset information structure
 */
struct OnsetInfo
{
    double timeSeconds;      // Time of onset relative to playhead
    float strength;          // Onset strength (0.0 - 1.0)
    float velocity;          // Estimated velocity (for MIDI conversion)
};
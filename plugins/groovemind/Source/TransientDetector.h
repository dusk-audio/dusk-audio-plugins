/*
  ==============================================================================

    TransientDetector.h
    Detects drum onsets (transients) from audio input for Follow Mode

    Uses a combination of spectral flux and amplitude envelope detection
    to identify drum hits with low latency.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

//==============================================================================
/**
 * Detected transient event with timing and classification info
 */
struct TransientEvent
{
    double timeInSamples = 0.0;      // When the transient occurred
    float strength = 0.0f;           // Transient strength (0-1)
    int instrumentCategory = -1;     // 0=kick, 1=snare, 2=hihat, 3=tom, 4=cymbal, 5=other
    float lowEnergy = 0.0f;          // Energy in low frequencies (for kick detection)
    float midEnergy = 0.0f;          // Energy in mid frequencies (for snare detection)
    float highEnergy = 0.0f;         // Energy in high frequencies (for hihat detection)
};

//==============================================================================
/**
 * Real-time transient detector for drum audio
 *
 * Uses multiple detection methods:
 * 1. Amplitude envelope follower with attack detection
 * 2. Spectral flux for broad transient detection
 * 3. Frequency band analysis for instrument classification
 */
class TransientDetector
{
public:
    TransientDetector();
    ~TransientDetector() = default;

    // Prepare for processing
    void prepare(double sampleRate, int maxBlockSize);

    // Reset state
    void reset();

    // Process audio block and detect transients
    // Returns vector of detected transients in this block
    std::vector<TransientEvent> process(const float* audioData, int numSamples);

    // Process stereo audio (sums to mono internally)
    std::vector<TransientEvent> processStereo(const float* leftChannel,
                                               const float* rightChannel,
                                               int numSamples);

    // Parameters
    void setSensitivity(float sensitivity);  // 0-1, higher = more sensitive
    void setThreshold(float threshold);      // Minimum transient strength (0-1)
    void setHoldTime(float holdTimeMs);      // Minimum time between detections

    // Get current detection state
    float getCurrentEnvelope() const { return envelope; }
    bool isDetecting() const { return inTransient; }

private:
    double sampleRate = 44100.0;

    // Detection parameters
    float sensitivity = 0.5f;
    float threshold = 0.2f;
    float holdTimeMs = 30.0f;
    int holdTimeSamples = 1323;  // ~30ms at 44.1kHz

    // Envelope follower
    float envelope = 0.0f;
    float attackCoeff = 0.0f;   // Fast attack
    float releaseCoeff = 0.0f;  // Slower release

    // Transient detection state
    float previousEnvelope = 0.0f;
    float envelopeDelta = 0.0f;
    bool inTransient = false;
    int holdCounter = 0;
    int64_t totalSamplesProcessed = 0;

    // Spectral analysis (simple 3-band)
    static constexpr int NUM_BANDS = 3;
    std::array<float, NUM_BANDS> bandEnergies = {0.0f, 0.0f, 0.0f};
    std::array<float, NUM_BANDS> prevBandEnergies = {0.0f, 0.0f, 0.0f};

    // Band filters (simple one-pole filters for efficiency)
    float lowpassState = 0.0f;
    float bandpassLowState = 0.0f;
    float bandpassHighState = 0.0f;
    float highpassState = 0.0f;

    // Filter coefficients
    float lowCutoff = 0.0f;   // ~200 Hz
    float midCutoff = 0.0f;   // ~2000 Hz
    float highCutoff = 0.0f;  // ~8000 Hz

    // Classify instrument based on frequency content
    int classifyInstrument(float lowEnergy, float midEnergy, float highEnergy) const;

    // Calculate filter coefficients
    void updateFilterCoefficients();

    // Apply simple one-pole lowpass
    float applyLowpass(float input, float& state, float coeff) const
    {
        state = state + coeff * (input - state);
        return state;
    }

    // Apply simple one-pole highpass
    float applyHighpass(float input, float& state, float coeff) const
    {
        float lowpassed = applyLowpass(input, state, coeff);
        return input - lowpassed;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientDetector)
};

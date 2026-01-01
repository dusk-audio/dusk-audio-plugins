#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

//==============================================================================
/**
    EBU R128 / ITU-R BS.1770-4 LUFS Meter

    Implements:
    - K-weighting pre-filter
    - Momentary loudness (400ms sliding window)
    - Short-term loudness (3s sliding window)
    - Integrated loudness (gated program loudness)
    - Loudness range (LRA)
*/
class LUFSMeter
{
public:
    LUFSMeter() = default;

    void prepare(double sampleRate, int numChannels);
    void reset();
    void process(const float* left, const float* right, int numSamples);

    //==========================================================================
    // Loudness values (all in LUFS)
    float getMomentaryLUFS() const { return momentaryLUFS; }
    float getShortTermLUFS() const { return shortTermLUFS; }
    float getIntegratedLUFS() const { return integratedLUFS; }
    float getLoudnessRange() const { return loudnessRange; }

    // Statistics
    float getMaxMomentary() const { return maxMomentary; }
    float getMaxShortTerm() const { return maxShortTerm; }

    // Reset integrated measurement
    void resetIntegrated();

private:
    //==========================================================================
    // K-weighting filter coefficients (ITU-R BS.1770-4)
    void initKWeighting(double sampleRate);
    float applyKWeighting(float sample, int channel);

    // Mean square calculation
    float calculateMeanSquare(const std::vector<float>& buffer) const;

    // Convert mean square to LUFS
    static float meanSquareToLUFS(float meanSquare);

    // Calculate gated integrated loudness
    void updateIntegratedLoudness();

    // Calculate loudness range (LRA)
    void updateLoudnessRange();

    //==========================================================================
    double sampleRate = 48000.0;
    int channels = 2;

    // K-weighting filters (Stage 1: High shelf, Stage 2: High-pass)
    // Per-channel biquad states
    struct BiquadState
    {
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    struct BiquadCoeffs
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
    };

    BiquadCoeffs highShelfCoeffs;
    BiquadCoeffs highPassCoeffs;
    std::array<BiquadState, 2> highShelfState;  // L, R
    std::array<BiquadState, 2> highPassState;   // L, R

    //==========================================================================
    // Momentary loudness (400ms window)
    std::vector<float> momentaryBuffer;
    int momentaryWritePos = 0;
    int momentarySamples = 0;  // Samples for 400ms

    // Short-term loudness (3s window)
    std::vector<float> shortTermBuffer;
    int shortTermWritePos = 0;
    int shortTermSamples = 0;  // Samples for 3s

    // Gated blocks for integrated loudness (100ms blocks)
    std::deque<float> gatedBlocks;
    std::vector<float> blockBuffer;
    int blockWritePos = 0;
    int blockSamples = 0;  // Samples for 100ms

    //==========================================================================
    // Output values
    float momentaryLUFS = -100.0f;
    float shortTermLUFS = -100.0f;
    float integratedLUFS = -100.0f;
    float loudnessRange = 0.0f;

    float maxMomentary = -100.0f;
    float maxShortTerm = -100.0f;

    //==========================================================================
    // Gating thresholds (EBU R128)
    static constexpr float ABSOLUTE_GATE = -70.0f;  // LUFS
    static constexpr float RELATIVE_GATE = -10.0f;  // LU below ungated mean
};

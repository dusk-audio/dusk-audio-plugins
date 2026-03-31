#pragma once

#include "TwoBandDamping.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// 4-tank cross-coupled allpass reverb for Hall algorithm.
//
// Extends the DattorroTank concept from 2 tanks to 4, providing 4x more
// output tap points (14 per channel vs 7) and circular cross-coupling
// (0→1→2→3→0) for naturally smooth tails without discrete recirculation peaks.
//
// Each tank: modulated allpass → delay → 3 density allpasses → damping →
//            static allpass → delay → cross-feed to next tank.
//
// The allpass-embedded topology produces continuous smooth decay (no discrete
// bumps in the delay buffers) — unlike the FDN where 16 parallel delay lines
// each produce visible recirculation peaks.
//
// Target: match Valhalla VintageVerb's ~20 early peaks (0-200ms) for
// Hall presets, vs FDN's ~130 peaks that no output processing can eliminate.
class QuadTank
{
public:
    QuadTank();

    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);
    void setModDepth (float depth);
    void setModRate (float hz);
    void setSize (float size);
    void setFreeze (bool frozen);
    void setLateGainScale (float scale);
    void setSizeRange (float min, float max);
    void clearBuffers();

private:
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float kSafetyClip = 32.0f;  // Raised from 4.0 to avoid THD from hard clipping at hot inputs
    static constexpr int kNumTanks = 4;
    static constexpr int kNumOutputTaps = 48;
    static constexpr int kNumDensityAPs = 3;

    // -----------------------------------------------------------------------
    // Hall-scale delay constants for 4 tanks (all prime, mutually coprime).
    // Total loops: ~250-300ms per tank for 1-12s RT60 range.
    struct TankConfig
    {
        int ap1Base;
        int del1Base;
        int densityAPBase[3];
        int ap2Base;
        int del2Base;
    };

    static constexpr TankConfig kTankConfigs[kNumTanks] = {
        { 709,  4507, { 307, 421, 577 }, 1871, 3769 },  // Tank 0 (~275ms)
        { 953,  4219, { 337, 461, 541 }, 2749, 3299 },  // Tank 1 (~285ms)
        { 797,  4637, { 317, 433, 563 }, 2111, 3583 },  // Tank 2 (~280ms)
        { 1049, 3989, { 347, 449, 557 }, 2393, 3067 },  // Tank 3 (~270ms)
    };

    static constexpr int kMaxBaseDelay = 4637;  // Largest delay across all tanks

    // -----------------------------------------------------------------------
    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;

        void allocate (int maxSamples);
        void clear();

        void write (float sample)
        {
            buffer[static_cast<size_t> (writePos)] = sample;
            writePos = (writePos + 1) & mask;
        }

        float read (int delaySamples) const
        {
            return buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
        }

        float readInterpolated (float delaySamples) const;
    };

    struct Allpass
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;
        int delaySamples = 0;

        void allocate (int maxSamples);
        void clear();

        float process (float input, float g)
        {
            float vd = buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
            float vn = input + g * vd;
            buffer[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }
    };

    // -----------------------------------------------------------------------
    struct Tank
    {
        DelayLine ap1Buffer;
        int ap1BaseDelay = 0;
        float ap1DelaySamples = 0;

        DelayLine delay1;
        int delay1BaseDelay = 0;
        float delay1Samples = 0;

        Allpass densityAP[3];
        int densityAPBase[3] = {};

        TwoBandDamping damping;

        Allpass ap2;
        int ap2BaseDelay = 0;

        DelayLine delay2;
        int delay2BaseDelay = 0;
        float delay2Samples = 0;

        float crossFeedState = 0.0f;

        float lfoPhase = 0.0f;
        float lfoPhaseInc = 0.0f;
        uint32_t lfoPRNG = 0;
        uint32_t noiseState = 0;
    };

    Tank tanks_[kNumTanks];

    // -----------------------------------------------------------------------
    // Output taps: 14 per channel from all 4 tanks.
    // Buffer indices: 0-3=Delay1 (tanks 0-3), 4-7=Delay2 (tanks 0-3),
    //                 8-11=AP2 (tanks 0-3)
    struct OutputTap
    {
        int bufferIndex;
        float positionFrac;
        float sign;
    };

    // Left output: 12 taps per tank (4×del1 + 4×del2 + 4×AP2), 48 total.
    // Dense fractional reads maximize averaging for peak suppression.
    static constexpr OutputTap kLeftOutputTaps[kNumOutputTaps] = {
        // Tank 0: 12 taps
        { 0, 0.13f,  1.0f },  { 0, 0.38f, -1.0f },  { 0, 0.62f,  1.0f },  { 0, 0.87f, -1.0f },
        { 4, 0.18f, -1.0f },  { 4, 0.43f,  1.0f },  { 4, 0.68f, -1.0f },  { 4, 0.93f,  1.0f },
        { 8, 0.15f,  1.0f },  { 8, 0.40f, -1.0f },  { 8, 0.65f,  1.0f },  { 8, 0.90f, -1.0f },
        // Tank 1: 12 taps
        { 1, 0.11f,  1.0f },  { 1, 0.36f, -1.0f },  { 1, 0.61f,  1.0f },  { 1, 0.86f, -1.0f },
        { 5, 0.16f, -1.0f },  { 5, 0.41f,  1.0f },  { 5, 0.66f, -1.0f },  { 5, 0.91f,  1.0f },
        { 9, 0.13f,  1.0f },  { 9, 0.38f, -1.0f },  { 9, 0.63f,  1.0f },  { 9, 0.88f, -1.0f },
        // Tank 2: 12 taps
        { 2, 0.15f,  1.0f },  { 2, 0.40f, -1.0f },  { 2, 0.65f,  1.0f },  { 2, 0.90f, -1.0f },
        { 6, 0.20f, -1.0f },  { 6, 0.45f,  1.0f },  { 6, 0.70f, -1.0f },  { 6, 0.95f,  1.0f },
        {10, 0.17f,  1.0f },  {10, 0.42f, -1.0f },  {10, 0.67f,  1.0f },  {10, 0.92f, -1.0f },
        // Tank 3: 12 taps
        { 3, 0.09f,  1.0f },  { 3, 0.34f, -1.0f },  { 3, 0.59f,  1.0f },  { 3, 0.84f, -1.0f },
        { 7, 0.14f, -1.0f },  { 7, 0.39f,  1.0f },  { 7, 0.64f, -1.0f },  { 7, 0.89f,  1.0f },
        {11, 0.11f,  1.0f },  {11, 0.36f, -1.0f },  {11, 0.61f,  1.0f },  {11, 0.86f, -1.0f },
    };

    // Right output: offset positions for L/R decorrelation
    static constexpr OutputTap kRightOutputTaps[kNumOutputTaps] = {
        // Tank 0: 12 taps (offset from L by ~0.12)
        { 0, 0.25f,  1.0f },  { 0, 0.50f, -1.0f },  { 0, 0.75f,  1.0f },  { 0, 0.95f, -1.0f },
        { 4, 0.30f, -1.0f },  { 4, 0.55f,  1.0f },  { 4, 0.80f, -1.0f },  { 4, 0.10f,  1.0f },
        { 8, 0.27f,  1.0f },  { 8, 0.52f, -1.0f },  { 8, 0.77f,  1.0f },  { 8, 0.08f, -1.0f },
        // Tank 1: 12 taps
        { 1, 0.23f,  1.0f },  { 1, 0.48f, -1.0f },  { 1, 0.73f,  1.0f },  { 1, 0.93f, -1.0f },
        { 5, 0.28f, -1.0f },  { 5, 0.53f,  1.0f },  { 5, 0.78f, -1.0f },  { 5, 0.08f,  1.0f },
        { 9, 0.25f,  1.0f },  { 9, 0.50f, -1.0f },  { 9, 0.75f,  1.0f },  { 9, 0.06f, -1.0f },
        // Tank 2: 12 taps
        { 2, 0.27f,  1.0f },  { 2, 0.52f, -1.0f },  { 2, 0.77f,  1.0f },  { 2, 0.07f, -1.0f },
        { 6, 0.32f, -1.0f },  { 6, 0.57f,  1.0f },  { 6, 0.82f, -1.0f },  { 6, 0.12f,  1.0f },
        {10, 0.29f,  1.0f },  {10, 0.54f, -1.0f },  {10, 0.79f,  1.0f },  {10, 0.09f, -1.0f },
        // Tank 3: 12 taps
        { 3, 0.21f,  1.0f },  { 3, 0.46f, -1.0f },  { 3, 0.71f,  1.0f },  { 3, 0.91f, -1.0f },
        { 7, 0.26f, -1.0f },  { 7, 0.51f,  1.0f },  { 7, 0.76f, -1.0f },  { 7, 0.06f,  1.0f },
        {11, 0.23f,  1.0f },  {11, 0.48f, -1.0f },  {11, 0.73f,  1.0f },  {11, 0.03f, -1.0f },
    };

    // -----------------------------------------------------------------------
    static float nextDrift (uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
    }

    // -----------------------------------------------------------------------
    double sampleRate_ = 44100.0;
    float decayTime_ = 1.0f;
    float bassMultiply_ = 1.0f;
    float trebleMultiply_ = 0.5f;
    float crossoverFreq_ = 1000.0f;
    float modDepthSamples_ = 8.0f;
    float lastModDepthRaw_ = 0.5f;
    float modRateHz_ = 1.0f;
    float sizeParam_ = 0.5f;
    float sizeRangeMin_ = 0.5f;
    float sizeRangeMax_ = 1.5f;
    float lateGainScale_ = 1.0f;
    bool frozen_ = false;
    bool prepared_ = false;

    float decayDiff1_ = 0.70f;
    float decayDiff2_ = 0.50f;
    float densityDiffCoeff_ = 0.25f;
    float noiseModDepth_ = 2.0f;

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();

    float readOutputTap (const OutputTap& tap) const;
};

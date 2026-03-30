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
    static constexpr float kSafetyClip = 4.0f;
    static constexpr int kNumTanks = 4;
    static constexpr int kNumOutputTaps = 14;
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

    // Left output: reads from tanks 1,2,3 (cross-read) + tank 0
    static constexpr OutputTap kLeftOutputTaps[kNumOutputTaps] = {
        { 1, 0.120f,  1.0f },   // tank1 delay1 early
        { 1, 0.680f,  1.0f },   // tank1 delay1 late
        { 9, 0.480f, -1.0f },   // tank1 AP2
        { 2, 0.450f,  1.0f },   // tank2 delay1 mid
        { 6, 0.540f, -1.0f },   // tank2 delay2 mid
        {10, 0.210f, -1.0f },   // tank2 AP2
        { 7, 0.310f, -1.0f },   // tank3 delay2 early
        { 3, 0.380f,  1.0f },   // tank3 delay1 mid
        {11, 0.550f, -1.0f },   // tank3 AP2
        { 0, 0.250f,  1.0f },   // tank0 delay1 early
        { 0, 0.720f, -1.0f },   // tank0 delay1 late
        { 4, 0.430f,  1.0f },   // tank0 delay2 mid
        { 5, 0.620f, -1.0f },   // tank1 delay2 mid
        { 8, 0.350f,  1.0f },   // tank0 AP2
    };

    // Right output: reads from tanks 0,2,3 (cross-read) + tank 1
    static constexpr OutputTap kRightOutputTaps[kNumOutputTaps] = {
        { 0, 0.140f,  1.0f },   // tank0 delay1 early
        { 0, 0.710f,  1.0f },   // tank0 delay1 late
        { 8, 0.520f, -1.0f },   // tank0 AP2
        { 3, 0.410f,  1.0f },   // tank3 delay1 mid
        { 7, 0.580f, -1.0f },   // tank3 delay2 mid
        {11, 0.240f, -1.0f },   // tank3 AP2
        { 6, 0.350f, -1.0f },   // tank2 delay2 early
        { 2, 0.420f,  1.0f },   // tank2 delay1 mid
        {10, 0.610f, -1.0f },   // tank2 AP2
        { 1, 0.280f,  1.0f },   // tank1 delay1 early
        { 1, 0.750f, -1.0f },   // tank1 delay1 late
        { 5, 0.470f,  1.0f },   // tank1 delay2 mid
        { 4, 0.660f, -1.0f },   // tank0 delay2 mid
        { 9, 0.390f,  1.0f },   // tank1 AP2
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

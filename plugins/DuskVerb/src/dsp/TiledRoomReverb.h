#pragma once

#include "DspUtils.h"
#include "TwoBandDamping.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Custom reverb engine for the "Tiled Room" preset.
// Two-stage serial cascade: Stage A (8 short delays) feeds Stage B (8 long delays).
// Each stage has its own neighbour-pair feedback loop.
// Serial topology creates progressive density buildup (VV's body rises over 200ms).
// Sinusoidal modulation on delay reads for modulation metric matching.
class TiledRoomReverb
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);
    void setHighCrossoverFreq (float hz);
    void setAirDampingScale (float scale);
    void setModDepth (float depth);
    void setModRate (float hz);
    void setSize (float size);
    void setFreeze (bool frozen);
    void setNoiseModDepth (float depth);
    void setDecayBoost (float boost);
    void setTerminalDecay (float thresholdDB, float factor);
    void setStereoCoupling (float amount);
    void clearBuffers();

private:
    static constexpr int kStageSize = 8;  // Channels per stage
    static constexpr int N = kStageSize * 2;  // Total channels
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float kSafetyClip = 4.0f;

    // Stage A: short delays (onset density, fast recirculation)
    static constexpr int kStageADelays[kStageSize] = { 106, 317, 331, 336, 346, 584, 759, 933 };
    // Stage B: long delays (tail sustain, progressive buildup from Stage A feed)
    static constexpr int kStageBDelays[kStageSize] = { 1088, 1361, 2044, 2396, 2606, 2981, 3204, 3470 };

    // Output tap signs (decorrelated stereo, applied to both stages)
    static constexpr float kLeftSigns[kStageSize]  = {  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f };
    static constexpr float kRightSigns[kStageSize] = { -1.0f,  1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f };

    // Cross-feed from Stage A → Stage B (controls density buildup rate)
    static constexpr float kSerialFeedLevel = 0.70f;  // High: strong serial feed for tail energy

    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;
    };

    DelayLine delayA_[kStageSize];  // Stage A
    DelayLine delayB_[kStageSize];  // Stage B
    float delayLenA_[kStageSize] = {};
    float delayLenB_[kStageSize] = {};

    ThreeBandDamping dampA_[kStageSize];
    ThreeBandDamping dampB_[kStageSize];

    // Per-channel LFO + noise
    float lfoPhase_[N] = {};
    float lfoPhaseInc_[N] = {};
    uint32_t lfoPRNG_[N] = {};
    uint32_t noiseState_[N] = {};

    // Parameters
    double sampleRate_ = 44100.0;
    float decayTime_ = 3.84f;
    float bassMultiply_ = 1.25f;
    float trebleMultiply_ = 1.0f;
    float crossoverFreq_ = 2850.0f;
    float highCrossoverFreq_ = 6000.0f;
    float airDampingScale_ = 0.25f;
    float lastModDepthRaw_ = 0.25f;
    float modDepthSamples_ = 4.0f;
    float modRateHz_ = 0.78f;
    float sizeParam_ = 0.107f;
    float sizeRangeMin_ = 0.5f;
    float sizeRangeMax_ = 1.5f;
    float decayBoost_ = 1.0f;
    float noiseModDepth_ = 1.2f;
    float maxNoiseModDepth_ = 32.0f;  // Set in prepare() from modulation headroom
    float stereoCoupling_ = 0.15f;

    float terminalDecayThresholdDB_ = -40.0f;
    float terminalDecayFactor_ = 1.0f;
    float terminalLinearThreshold_ = 100.0f;  // 10^(-(-40dB)/20) — amplitude ratio for peak/current RMS  // 10^(-thresholdDB * 0.1) — precomputed
    float peakRMS_ = 0.0f;
    float currentRMS_ = 0.0f;

    // Sample-rate-invariant smoothing coefficients for terminal decay detector
    // Computed in prepare() from time constants so behaviour is identical at any rate
    float rmsAlpha_ = 0.9995f;   // currentRMS_ retention per sample
    float rmsBeta_ = 0.0005f;    // currentRMS_ input weight (1 - rmsAlpha_)
    float peakDecay_ = 0.99999f; // peakRMS_ decay per sample

    bool frozen_ = false;
    bool prepared_ = false;

    // DC blocker
    float dcX1_[N] = {};
    float dcY1_[N] = {};
    static constexpr float kDCCoeff = 0.9995f;

    static void householderInPlace8 (float* x)
    {
        float sum = 0.0f;
        for (int i = 0; i < 8; ++i) sum += x[i];
        sum *= (2.0f / 8.0f);
        for (int i = 0; i < 8; ++i) x[i] -= sum;
    }

    static float nextDrift (uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
    }

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();
};

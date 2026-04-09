#pragma once

#include "TwoBandDamping.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Dattorro-inspired cross-coupled tank reverb for Room algorithm.
//
// Enhanced topology: two asymmetric feedback loops cross-coupled in a figure-8.
// Each loop contains: modulated allpass → delay → density cascade (3 allpasses)
// → two-band damping → static allpass → delay.
//
// The density cascade multiplies echo density by ~8× per loop pass compared
// to the basic Dattorro topology (which only has 2 allpasses per loop).
// This is critical for room-scale delays where mode spacing is wider.
//
// Output is formed by summing 7 signed taps from various internal
// points in both tanks, creating naturally decorrelated stereo.
//
// Reference: Dattorro, "Effect Design Part 1" (JAES 1997), adapted
// for room-scale delays, extended density cascades, and per-sample
// noise modulation for aggressive mode smearing.
class DattorroTank
{
public:
    DattorroTank();

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
    void setLateGainScale (float scale);
    void setSizeRange (float min, float max);
    void setHallScale (bool enable);   // Switch to hall-scale delay lengths (2x room)
    // Output tap specification (moved to public for per-algorithm configuration)
    struct OutputTap
    {
        int bufferIndex;     // Which delay buffer (0-5: L/R delay1/delay2/AP2)
        float positionFrac;  // Fractional position (0.0-1.0) within the delay
        float sign;          // ±1.0 for decorrelation
        float gain;          // Per-tap amplitude (0.0-2.0). Shapes onset envelope.
                             // < 1.0 attenuates (slows onset), > 1.0 boosts (faster onset).
                             // Default 1.0 = equal weight (original behavior).
    };

    void setOutputTaps (const OutputTap* left, const OutputTap* right);
    void applyTapGains (const float* leftGains, const float* rightGains);  // Update gain field only
    void setNoiseModDepth (float samples);
    void setDelayScale (float scale);  // Multiplies ALL base delays (controls loop length)
    void setSoftOnsetMs (float ms);    // Output onset smoothing time (0 = off)
    void setLimiter (float thresholdDb, float releaseMs);  // Peak limiter (0 thresholdDb = off)
    void setDecayBoost (float boost);
    void setCrossoverModDepth (float depth);
    void setStructuralHFDamping (float hz);
    void setTerminalDecay (float thresholdDB, float factor);
    void clearBuffers();

private:
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr double kBaseSampleRate = 44100.0;

    // Safety clamp (~+12dBFS), matches FDNReverb
    static constexpr float kSafetyClip = 4.0f;

    // Output tap count per channel (Dattorro uses 7)
    static constexpr int kNumOutputTaps = 7;

    // -----------------------------------------------------------------------
    // Base delay lengths at 44100 Hz (all prime, room-scaled from Dattorro plate).
    // Asymmetric L/R prevents correlated modal peaks.

    // Left tank
    static constexpr int kLeftAP1Base  = 331;   // ~7.5ms  modulated allpass
    static constexpr int kLeftDel1Base = 2203;   // ~50ms   delay
    static constexpr int kLeftAP2Base  = 887;    // ~20ms   static allpass
    static constexpr int kLeftDel2Base = 1831;   // ~41.5ms delay

    // Right tank
    static constexpr int kRightAP1Base  = 443;   // ~10ms   modulated allpass
    static constexpr int kRightDel1Base = 2081;   // ~47ms   delay
    static constexpr int kRightAP2Base  = 1307;   // ~29.6ms static allpass
    static constexpr int kRightDel2Base = 1559;   // ~35.3ms delay

    // Worst-case base delay for buffer allocation (hall-scale is larger)
    static constexpr int kMaxBaseDelay = 18028;  // 4507 * 4 (max delayScale=4.0)

    // -----------------------------------------------------------------------
    // Circular delay line with power-of-2 masking.
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

        // Read at a fixed integer offset behind the write head.
        float read (int delaySamples) const
        {
            return buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
        }

        // Read at a fractional offset (cubic Hermite interpolation).
        float readInterpolated (float delaySamples) const;
    };

    // Non-modulated Schroeder allpass (same as FDNReverb::InlineAllpass).
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
    // Density cascade: 3 additional allpasses between delay1 and damping.
    // Multiplies echo density ~8× per loop pass (each AP doubles mode count).
    // Delays are prime and coprime to all other elements.
    static constexpr int kNumDensityAPs = 3;

    // Left tank density AP delays (at 44100 Hz)
    static constexpr int kLeftDensityAPBase[kNumDensityAPs] = { 137, 199, 281 };
    // Right tank density AP delays (at 44100 Hz)
    static constexpr int kRightDensityAPBase[kNumDensityAPs] = { 149, 211, 263 };

    // Hall-scale delays: ~2x room for 1-12s RT60 (all prime, coprime to room delays)
    // Total loop: left=12161 (275.8ms), right=12559 (284.8ms)
    // Per-algorithm temporal scaling is done via sizeRange in AlgorithmConfig,
    // NOT by changing these base constants.
    static constexpr int kLeftAP1BaseHall  = 709;    // ~16.1ms
    static constexpr int kLeftDel1BaseHall = 4507;   // ~102.2ms
    static constexpr int kLeftAP2BaseHall  = 1871;   // ~42.4ms
    static constexpr int kLeftDel2BaseHall = 3769;   // ~85.4ms
    static constexpr int kRightAP1BaseHall  = 953;   // ~21.6ms
    static constexpr int kRightDel1BaseHall = 4219;  // ~95.7ms
    static constexpr int kRightAP2BaseHall  = 2749;  // ~62.3ms
    static constexpr int kRightDel2BaseHall = 3299;  // ~74.8ms
    static constexpr int kLeftDensityAPBaseHall[kNumDensityAPs]  = { 307, 421, 577 };
    static constexpr int kRightDensityAPBaseHall[kNumDensityAPs] = { 337, 461, 541 };

    // -----------------------------------------------------------------------
    // Each cross-coupled feedback loop.
    struct Tank
    {
        // Modulated allpass (decay diffusion 1)
        DelayLine ap1Buffer;       // Buffer for modulated allpass
        int ap1BaseDelay = 0;      // Base delay at 44100 Hz
        float ap1DelaySamples = 0; // Current delay (scaled by rate + size)

        // First delay line
        DelayLine delay1;
        int delay1BaseDelay = 0;
        float delay1Samples = 0;

        // Density cascade: 3 allpasses for echo density multiplication
        Allpass densityAP[kNumDensityAPs];
        int densityAPBase[kNumDensityAPs] = {};

        // Three-band damping (bass / mid / air with independent per-band decay)
        ThreeBandDamping damping;

        // Static allpass (decay diffusion 2)
        Allpass ap2;
        int ap2BaseDelay = 0;

        // Second delay line
        DelayLine delay2;
        int delay2BaseDelay = 0;
        float delay2Samples = 0;

        // Cross-feed state (output of this tank feeds other tank's input)
        float crossFeedState = 0.0f;

        // LFO for modulated allpass
        float lfoPhase = 0.0f;
        float lfoPhaseInc = 0.0f;
        uint32_t lfoPRNG = 0;        // Shared: LFO drift + noise jitter
        uint32_t noiseState = 0;      // Dedicated PRNG for per-sample delay jitter
    };

    Tank leftTank_;
    Tank rightTank_;

    // -----------------------------------------------------------------------
    // Default output tap positions (early Dattorro-style, read from both tanks).
    // Tap indices: 0=leftDelay1, 1=leftDelay2, 2=leftAP2,
    //              3=rightDelay1, 4=rightDelay2, 5=rightAP2
    // 7 taps per channel, tapping from BOTH tanks for stereo decorrelation.
    // Positions are fractions of each delay's current length, so they scale with size.
    static constexpr OutputTap kLeftOutputTaps[kNumOutputTaps] = {
        { 3, 0.120f,  1.0f, 1.0f },  // right delay1, early
        { 3, 0.675f,  1.0f, 1.0f },  // right delay1, late
        { 5, 0.480f, -1.0f, 1.0f },  // right AP2
        { 0, 0.450f,  1.0f, 1.0f },  // left delay1, mid
        { 1, 0.540f, -1.0f, 1.0f },  // left delay2, mid
        { 2, 0.210f, -1.0f, 1.0f },  // left AP2
        { 4, 0.310f, -1.0f, 1.0f },  // right delay2, early
    };

    static constexpr OutputTap kRightOutputTaps[kNumOutputTaps] = {
        { 0, 0.140f,  1.0f, 1.0f },  // left delay1, early
        { 0, 0.710f,  1.0f, 1.0f },  // left delay1, late
        { 2, 0.520f, -1.0f, 1.0f },  // left AP2
        { 3, 0.410f,  1.0f, 1.0f },  // right delay1, mid
        { 4, 0.580f, -1.0f, 1.0f },  // right delay2, mid
        { 5, 0.240f, -1.0f, 1.0f },  // right AP2
        { 1, 0.350f, -1.0f, 1.0f },  // left delay2, early
    };

    // -----------------------------------------------------------------------
    // Cheap xorshift32 PRNG returning float in [-1, +1].
    // Used for aperiodic LFO drift ("Wander").
    static float nextDrift (uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
    }

    // -----------------------------------------------------------------------
    // Parameters
    double sampleRate_ = 44100.0;
    float decayTime_ = 1.0f;
    float bassMultiply_ = 1.0f;
    float trebleMultiply_ = 0.5f;
    float crossoverFreq_ = 1000.0f;
    float highCrossoverFreq_ = 20000.0f;  // Second crossover for 3-band damping (Hz)
    float airDampingScale_ = 1.0f;        // Independent HF air band damping multiplier
    float modDepthSamples_ = 8.0f;  // Peak excursion in samples
    float lastModDepthRaw_ = 0.5f;  // Raw 0-1 depth before sample rate scaling
    float modRateHz_ = 1.0f;
    float sizeParam_ = 0.5f;
    float sizeRangeMin_ = 0.5f;
    float sizeRangeMax_ = 1.5f;
    float lateGainScale_ = 1.0f;
    float delayScale_ = 1.0f;  // Global delay multiplier (set before prepare)
    float softOnsetMs_ = 0.0f;    // Output onset ramp time (ms). Smooths early transient spike.
    float softOnsetCoeff_ = 1.0f; // Per-sample increment for onset ramp
    float softOnsetEnvL_ = 0.0f;  // Current ramp value

    // Peak limiter: reduces transient peaks while preserving RMS (lowers crest factor).
    float limiterThreshold_ = 0.0f;      // 0 = disabled. Linear amplitude threshold.
    float limiterReleaseCoeff_ = 0.999f;  // One-pole release coefficient (~50ms at 48kHz)
    float limiterEnv_ = 0.0f;             // Current peak envelope

    bool frozen_ = false;
    bool prepared_ = false;

    float decayBoost_ = 1.0f;
    float crossoverModDepth_ = 0.0f;
    float baseLowCrossoverCoeff_ = 0.85f;
    float structHFCoeff_ = 0.0f;
    float structHFStateL_ = 0.0f;
    float structHFStateR_ = 0.0f;
    float terminalDecayThresholdDB_ = -40.0f;
    float terminalDecayFactor_ = 1.0f;
    float peakRMS_ = 0.0f;
    float currentRMS_ = 0.0f;
    bool terminalDecayActive_ = false;

    // Dattorro coefficients
    float decayDiff1_ = 0.70f;   // Modulated allpass feedback
    float decayDiff2_ = 0.50f;   // Static allpass feedback
    float densityDiffCoeff_ = 0.18f;  // Density cascade feedback (higher = more echo density)

    // Per-sample noise modulation: random jitter on delay reads.
    // Complements the slow sinusoidal LFO with fast aperiodic mode blurring.
    float noiseModDepth_ = 2.0f;  // Peak jitter in samples (at 44100 Hz base)
    float independentNoiseModDepth_ = -1.0f;  // Independent noise jitter (-1 = use modDepth-coupled value)

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();

    // Resolves an output tap to an actual sample offset for the current delay lengths.
    float readOutputTap (const OutputTap& tap) const;

    // Per-algorithm configurable output taps (override static defaults)
    OutputTap customLeftTaps_[kNumOutputTaps] {};
    OutputTap customRightTaps_[kNumOutputTaps] {};
    bool useCustomTaps_ = false;
};

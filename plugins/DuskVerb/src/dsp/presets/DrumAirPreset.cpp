// GENERATED FILE - do not edit by hand (use generate_preset_engines.py)
//
// Per-preset reverb engine for "Drum Air".
// Base engine: DattorroTank
//
// This file contains a full private copy of the DattorroTank DSP in an
// anonymous namespace, so modifications to this preset's DSP cannot affect
// any other preset. The class has been renamed to DrumAirPresetEngine to avoid
// ODR conflicts.

#include "DrumAirPreset.h"
#include "PresetEngineRegistry.h"

#include "../TwoBandDamping.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>


namespace {

    // Baked algorithm-config constants from kPresetDrumAir in AlgorithmConfig.h
    // at generation time. Editing these here has no effect on other presets.
    constexpr float kBakedLateGainScale      = 0.22f;
    constexpr float kBakedSizeRangeMin       = 0.5f;
    constexpr float kBakedSizeRangeMax       = 1.5f;
    constexpr float kBakedAirDampingScale    = 0.8f;
    constexpr float kBakedNoiseModDepth      = 8.0f;
    constexpr float kBakedTrebleMultScale    = 0.81f;
    constexpr float kBakedTrebleMultScaleMax = 1.5f;
    constexpr float kBakedBassMultScale      = 0.65f;
    constexpr float kBakedModDepthScale      = 0.75f;
    constexpr float kBakedModRateScale       = 13.0f;
    constexpr float kBakedDecayTimeScale     = 1.5f;

    // -----------------------------------------------------------------
    // Per-preset VV-derived structural constants
    // (from vv_topology_baked.json — derived from VV IR analysis).
    // These set the engine to a per-preset target at prepare() time;
    // runtime setters layer relative scaling on top of them.
    // -----------------------------------------------------------------
    constexpr float kVvCrossoverHz       = 1000.0f;
    constexpr float kVvHighCrossoverHz   = 6000.0f;
    constexpr float kVvAirDampingScale   = 0.969875f;
    constexpr float kVvDelayScale        = 0.411793f;
    constexpr float kVvTiltLowDb         = -1.04644f;
    constexpr float kVvTiltLowHz         = 400.0f;
    constexpr float kVvTiltHighDb        = -6.0000f;
    constexpr float kVvTiltHighHz        = 5000.0f;
    constexpr float kVvStereoWidth       = 0.867471f;
    // Decay-time correction: multiplied into the user knob in setDecayTime()
    // to bring the engine's actual RT60 in line with VV's measured RT60.
    // Derived by render-then-measure (see derive_decay_scale.py).
    // 1.0 = no correction; values < 1 shorten the tail, > 1 lengthen it.
    constexpr float kVvDecayTimeScale    = 0.353889f;
    // Per-preset 12-band corrective peaking EQ (from vv_correction_eq.json).
    // Derived by rendering DV at factory defaults and computing the per-band
    // dB delta vs VV. Applied post-engine in process() to push DV's spectral
    // character toward VV's. Coefficients are computed from these constants
    // in prepare() at the host sample rate so the EQ is correct at any rate.
    // Max correction magnitude for this preset: 8.47 dB
    // -----------------------------------------------------------------
    constexpr int kCorrEqBandCount = 12;
    constexpr float kCorrEqHz[kCorrEqBandCount] = { 100.0f, 158.0f, 251.0f, 397.0f, 632.0f, 1000.0f, 1581.0f, 2510.0f, 3969.0f, 6325.0f, 9798.0f, 15492.0f };
    constexpr float kCorrEqDb[kCorrEqBandCount] = { -12.0f, -12.0f, -12.0f, -12.0f, -12.0f, -12.0f, -12.0f, -12.0f, -0.120462f, -12.0f, -12.0f, 12.0f };
    constexpr float kCorrEqQ = 1.41f;  // moderate Q ≈ 1 octave bandwidth

    // -----------------------------------------------------------------
    // Per-preset FiveBandDamping: 5-band per-frequency-band RT60 control.
    // Band multipliers: g[band] = gBase^(1/mult[band]).
    // mult > 1.0 = longer decay, < 1.0 = shorter decay in that band.
    // Crossover frequencies define the band boundaries.
    // -----------------------------------------------------------------
    constexpr float kFiveBandMult[5] = { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f };
    constexpr float kFiveBandCrossoverHz[4] = { 150.0f, 600.0f, 2500.0f, 8000.0f };

// ==========================================================================
//  BEGIN PRIVATE COPY OF DattorroTank (DattorroTank.h)
// ==========================================================================

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
class DrumAirPresetEngine
{
public:
    DrumAirPresetEngine();

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
        FiveBandDamping damping;

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

        // Per-tank terminal decay tracking (avoids L/R interleaving artifacts)
        float currentRMS = 0.0f;
        float peakRMS = 0.0f;
        bool terminalDecayActive = false;
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
    float sizeRangeAllocatedMax_ = 4.0f;
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
    float structHFCoeff_ = 0.0f;
    float structHFStateL_ = 0.0f;
    float structHFStateR_ = 0.0f;
    float terminalDecayThresholdDB_ = -40.0f;
    float terminalDecayFactor_ = 1.0f;
    float rmsAlpha_ = 0.9995f;
    float peakDecayAlpha_ = 0.99999f;
    float terminalLinearThreshold_ = 10000.0f;  // 10^(-(-40dB)*0.1) — power ratio for peak/current RMS

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

// ==========================================================================
//  BEGIN PRIVATE COPY OF DattorroTank IMPLEMENTATION (DattorroTank.cpp)
// ==========================================================================

// -----------------------------------------------------------------------
// DelayLine helpers

void DrumAirPresetEngine::DelayLine::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void DrumAirPresetEngine::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float DrumAirPresetEngine::DelayLine::readInterpolated (float delaySamples) const
{
    float readPos = static_cast<float> (writePos) - delaySamples;
    int intIdx = static_cast<int> (std::floor (readPos));
    float frac = readPos - static_cast<float> (intIdx);
    return DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
}

// -----------------------------------------------------------------------
// Allpass helpers

void DrumAirPresetEngine::Allpass::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void DrumAirPresetEngine::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// -----------------------------------------------------------------------
// Constructor

DrumAirPresetEngine::DrumAirPresetEngine()
{
    leftTank_.ap1BaseDelay  = kLeftAP1Base;
    leftTank_.delay1BaseDelay = kLeftDel1Base;
    leftTank_.ap2BaseDelay  = kLeftAP2Base;
    leftTank_.delay2BaseDelay = kLeftDel2Base;

    rightTank_.ap1BaseDelay  = kRightAP1Base;
    rightTank_.delay1BaseDelay = kRightDel1Base;
    rightTank_.ap2BaseDelay  = kRightAP2Base;
    rightTank_.delay2BaseDelay = kRightDel2Base;

    // Density cascade base delays
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        leftTank_.densityAPBase[i]  = kLeftDensityAPBase[i];
        rightTank_.densityAPBase[i] = kRightDensityAPBase[i];
    }
}

// -----------------------------------------------------------------------

void DrumAirPresetEngine::setHallScale (bool enable)
{
    if (enable)
    {
        leftTank_.ap1BaseDelay     = kLeftAP1BaseHall;
        leftTank_.delay1BaseDelay  = kLeftDel1BaseHall;
        leftTank_.ap2BaseDelay     = kLeftAP2BaseHall;
        leftTank_.delay2BaseDelay  = kLeftDel2BaseHall;
        rightTank_.ap1BaseDelay    = kRightAP1BaseHall;
        rightTank_.delay1BaseDelay = kRightDel1BaseHall;
        rightTank_.ap2BaseDelay    = kRightAP2BaseHall;
        rightTank_.delay2BaseDelay = kRightDel2BaseHall;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            leftTank_.densityAPBase[i]  = kLeftDensityAPBaseHall[i];
            rightTank_.densityAPBase[i] = kRightDensityAPBaseHall[i];
        }
    }
    else
    {
        leftTank_.ap1BaseDelay     = kLeftAP1Base;
        leftTank_.delay1BaseDelay  = kLeftDel1Base;
        leftTank_.ap2BaseDelay     = kLeftAP2Base;
        leftTank_.delay2BaseDelay  = kLeftDel2Base;
        rightTank_.ap1BaseDelay    = kRightAP1Base;
        rightTank_.delay1BaseDelay = kRightDel1Base;
        rightTank_.ap2BaseDelay    = kRightAP2Base;
        rightTank_.delay2BaseDelay = kRightDel2Base;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            leftTank_.densityAPBase[i]  = kLeftDensityAPBase[i];
            rightTank_.densityAPBase[i] = kRightDensityAPBase[i];
        }
    }
}

// -----------------------------------------------------------------------

void DrumAirPresetEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    // Sample-rate-invariant terminal decay smoothing coefficients
    {
        constexpr float kRmsTauMs = 45.0f;
        constexpr float kPeakTauMs = 2270.0f;
        float sr = static_cast<float> (sampleRate_);
        rmsAlpha_ = std::exp (-1000.0f / (kRmsTauMs * sr));
        peakDecayAlpha_ = std::exp (-1000.0f / (kPeakTauMs * sr));
    }

    // FiveBandDamping: set inner crossover coefficients and band multipliers.
    {
        float fbCrossHz[4] = {
            kFiveBandCrossoverHz[0], kFiveBandCrossoverHz[1],
            kFiveBandCrossoverHz[2], kFiveBandCrossoverHz[3]
        };
        float fbMult[5] = {
            kFiveBandMult[0], kFiveBandMult[1], kFiveBandMult[2],
            kFiveBandMult[3], kFiveBandMult[4]
        };
        float coeffs[4];
        for (int b = 0; b < 4; ++b)
            coeffs[b] = std::exp (-6.283185307f * fbCrossHz[b]
                                  / static_cast<float> (sampleRate_));
        leftTank_.damping.setCrossovers (coeffs);
        leftTank_.damping.setBandMultipliers (fbMult);
        rightTank_.damping.setCrossovers (coeffs);
        rightTank_.damping.setBandMultipliers (fbMult);
    }
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);

    // Modulation headroom beyond the max scaled delay (scale with sample rate)
    const int maxModExcursion = static_cast<int> (std::ceil (32.0 * sampleRate / 44100.0));

    // Track allocation ceiling for runtime setSizeRange() bounds checking
    // Preserve headroom for post-prepare size-range overrides.
    sizeRangeAllocatedMax_ = std::max (sizeRangeAllocatedMax_, sizeRangeMax_);

    // Allocate all buffers
    auto prepareTank = [&] (Tank& tank)
    {
        float maxScale = sizeRangeAllocatedMax_ * delayScale_;
        int ap1Max = static_cast<int> (std::ceil (tank.ap1BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int del1Max = static_cast<int> (std::ceil (tank.delay1BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int ap2Max = static_cast<int> (std::ceil (tank.ap2BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int del2Max = static_cast<int> (std::ceil (tank.delay2BaseDelay * rateRatio * maxScale)) + maxModExcursion;

        tank.ap1Buffer.allocate (ap1Max);
        tank.delay1.allocate (del1Max);  // Extra headroom for noise jitter
        tank.ap2.allocate (ap2Max);
        tank.delay2.allocate (del2Max + maxModExcursion);

        // Density cascade allpasses. updateDelayLengths() scales these by
        // delayScale_ too, so the max allocation must include it to avoid
        // buffer underruns when delayScale_ > 1.
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            int dapMax = static_cast<int> (std::ceil (
                tank.densityAPBase[i] * rateRatio * sizeRangeAllocatedMax_ * delayScale_)) + 4;
            tank.densityAP[i].allocate (dapMax);
        }

        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    };

    prepareTank (leftTank_);
    prepareTank (rightTank_);

    // Initialize LFO and PRNG state with different seeds per tank
    leftTank_.lfoPhase = 0.0f;
    leftTank_.lfoPRNG = 0x12345678u;
    leftTank_.noiseState = 0xDEADBEEFu;
    rightTank_.lfoPhase = 1.5707963f;  // 90° offset for stereo decorrelation
    rightTank_.lfoPRNG = 0x87654321u;
    rightTank_.noiseState = 0xCAFEBABEu;

    structHFStateL_ = 0.0f;
    structHFStateR_ = 0.0f;

    prepared_ = true;

    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();

    // Re-apply mod depth scaled for the new sample rate
    setModDepth (lastModDepthRaw_);
    if (softOnsetMs_ > 0.0f)
        setSoftOnsetMs (softOnsetMs_);

    // Clear all stateful trackers (structural HF damping state, terminal
    // decay RMS history). Without this, a host re-prepare would start with
    // empty delay buffers but retain the previous run's tracker state.
    leftTank_.currentRMS = 0.0f;
    leftTank_.peakRMS = 0.0f;
    leftTank_.terminalDecayActive = false;
    rightTank_.currentRMS = 0.0f;
    rightTank_.peakRMS = 0.0f;
    rightTank_.terminalDecayActive = false;
    softOnsetEnvL_ = (softOnsetMs_ > 0.0f) ? 0.0f : 1.0f;
    limiterEnv_ = 0.0f;
}

// -----------------------------------------------------------------------

void DrumAirPresetEngine::process (const float* inputL, const float* inputR,
                            float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        // Mono sum of stereo input (Dattorro tank is internally mono,
        // stereo comes from decorrelated output tapping)
        float input = (inputL[i] + inputR[i]) * 0.5f;

        if (frozen_)
            input = 0.0f;

        // ------------------------------------------------------------------
        // Process both tanks. Each receives the other's cross-feed state.
        // Order: left first, then right. The one-sample delay in cross-feed
        // is intentional (Dattorro's figure-8 topology).

        auto processTank = [&] (Tank& tank, float otherCrossFeed)
        {
            // Tank input: new audio + cross-fed signal from the other tank
            float tankIn = input + otherCrossFeed;

            // --- Modulated allpass (decay diffusion 1) ---
            // LFO modulation with "Wander" drift (classic reverb technique)
            float mod = std::sin (tank.lfoPhase) * modDepthSamples_;
            float ap1ReadDelay = tank.ap1DelaySamples + mod;
            ap1ReadDelay = std::max (ap1ReadDelay, 1.0f);  // Never read ahead of write

            float ap1Delayed = tank.ap1Buffer.readInterpolated (ap1ReadDelay);
            float coeff1 = frozen_ ? 0.0f : decayDiff1_;
            float ap1In = tankIn + coeff1 * ap1Delayed;
            tank.ap1Buffer.write (ap1In);
            float ap1Out = ap1Delayed - coeff1 * ap1In;

            // Advance LFO with drift (frozen tail holds steady)
            if (! frozen_)
            {
                float drift = nextDrift (tank.lfoPRNG) * tank.lfoPhaseInc * 0.08f;
                tank.lfoPhase += tank.lfoPhaseInc + drift;
                if (tank.lfoPhase >= kTwoPi)
                    tank.lfoPhase -= kTwoPi;
                else if (tank.lfoPhase < 0.0f)
                    tank.lfoPhase += kTwoPi;
            }

            // --- Delay 1 (with per-sample noise jitter) ---
            float effectiveNoiseMod = (independentNoiseModDepth_ >= 0.0f)
                                    ? independentNoiseModDepth_ : noiseModDepth_;
            float jitter1 = frozen_ ? 0.0f : nextDrift (tank.noiseState) * effectiveNoiseMod;
            float del1Read = tank.delay1Samples + jitter1;
            del1Read = std::max (del1Read, 1.0f);
            float del1Out = tank.delay1.readInterpolated (del1Read);
            tank.delay1.write (ap1Out);

            // --- Density cascade: 3 allpasses to multiply echo density ---
            float dense = del1Out;
            if (! frozen_)
            {
                for (int d = 0; d < kNumDensityAPs; ++d)
                    dense = tank.densityAP[d].process (dense, densityDiffCoeff_);
            }

            // --- Two-band damping ---
            float damped = frozen_ ? dense : tank.damping.process (dense);

            // --- Structural HF damping ---
            if (structHFCoeff_ > 0.0f && ! frozen_)
            {
                float& hfState = (&tank == &leftTank_) ? structHFStateL_ : structHFStateR_;
                hfState = (1.0f - structHFCoeff_) * damped + structHFCoeff_ * hfState;
                damped = hfState;
            }

            // --- Static allpass (decay diffusion 2) ---
            float coeff2 = frozen_ ? 0.0f : decayDiff2_;
            float ap2Out = tank.ap2.process (damped, coeff2);

            // --- Delay 2 (with per-sample noise jitter) ---
            float jitter2 = frozen_ ? 0.0f : nextDrift (tank.noiseState) * effectiveNoiseMod;
            float del2Read = tank.delay2Samples + jitter2;
            del2Read = std::max (del2Read, 1.0f);
            float del2Out = tank.delay2.readInterpolated (del2Read);
            // Denormal prevention: tiny alternating bias
            float bias = frozen_ ? 0.0f
                                 : (((tank.delay2.writePos ^ 1) & 1)
                                        ? +DspUtils::kDenormalPrevention
                                        : -DspUtils::kDenormalPrevention);
            tank.delay2.write (ap2Out + bias);

            // Terminal decay: extra damping when tail is far below peak
            // Skipped when frozen — frozen tails must not attenuate.
            if (terminalDecayFactor_ < 1.0f && ! frozen_)
            {
                float sampleEnergy = del2Out * del2Out;
                tank.currentRMS = tank.currentRMS * rmsAlpha_ + sampleEnergy * (1.0f - rmsAlpha_);
                if (tank.currentRMS > tank.peakRMS) tank.peakRMS = tank.currentRMS;
                else tank.peakRMS *= peakDecayAlpha_;
                float ratio = tank.peakRMS / std::max (tank.currentRMS, 1e-20f);

                tank.terminalDecayActive = (ratio > terminalLinearThreshold_) && (tank.peakRMS > 1e-12f);
                if (tank.terminalDecayActive)
                    del2Out *= terminalDecayFactor_;
            }

            // Cross-feed output: end of this tank feeds the other tank's input
            tank.crossFeedState = del2Out;
        };

        // Save right tank's cross-feed state before left tank overwrites it
        float rightCrossFeed = rightTank_.crossFeedState;
        float leftCrossFeed = leftTank_.crossFeedState;

        processTank (leftTank_, rightCrossFeed);
        processTank (rightTank_, leftCrossFeed);

        // ------------------------------------------------------------------
        // Output: sum 7 signed taps from both tanks per channel.
        const OutputTap* lTaps = useCustomTaps_ ? customLeftTaps_ : kLeftOutputTaps;
        const OutputTap* rTaps = useCustomTaps_ ? customRightTaps_ : kRightOutputTaps;

        float outL = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outL += readOutputTap (lTaps[t]) * lTaps[t].sign * lTaps[t].gain;

        float outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outR += readOutputTap (rTaps[t]) * rTaps[t].sign * rTaps[t].gain;

        // Normalize 7-tap sum. The tank has much higher internal energy than
        // the FDN (2 loops vs 16 channels with Hadamard ÷4 normalization),
        // so we use a lower output scale to match FDN output levels.
        constexpr float kOutputScale = 0.14285714f;  // 1/7 — average of 7 taps
        const float outputGain = kOutputScale * lateGainScale_;

        float scaledL = outL * outputGain;
        float scaledR = outR * outputGain;

        // Soft output onset ramp: smooths the initial transient spike from early taps.
        // Ramps from 0→1 linearly over softOnsetMs_ after reset/preset change.
        if (softOnsetEnvL_ < 1.0f)
        {
            scaledL *= softOnsetEnvL_;
            scaledR *= softOnsetEnvL_;
            softOnsetEnvL_ = std::min (softOnsetEnvL_ + softOnsetCoeff_, 1.0f);
        }

        // Peak limiter: fast-attack / slow-release envelope follower with gain reduction.
        // Reduces transient peaks while preserving RMS level (lowers crest factor).
        // Attack: instant (0 samples). Release: ~50ms one-pole decay.
        // When peak exceeds limiterThreshold_, gain is reduced to keep output at threshold.
        if (limiterThreshold_ > 0.0f)
        {
            float peakLR = std::max (std::abs (scaledL), std::abs (scaledR));

            // Envelope: instant attack, slow release
            if (peakLR > limiterEnv_)
                limiterEnv_ = peakLR;  // Instant attack
            else
                limiterEnv_ = limiterReleaseCoeff_ * limiterEnv_
                            + (1.0f - limiterReleaseCoeff_) * peakLR;  // Slow release

            // Gain reduction: when envelope > threshold, reduce gain
            if (limiterEnv_ > limiterThreshold_)
            {
                float gain = limiterThreshold_ / limiterEnv_;
                scaledL *= gain;
                scaledR *= gain;
            }
        }

        outputL[i] = std::clamp (scaledL, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (scaledR, -kSafetyClip, kSafetyClip);
    }
}

// -----------------------------------------------------------------------
// Output tap reading

float DrumAirPresetEngine::readOutputTap (const OutputTap& tap) const
{
    // Map buffer index to the actual delay buffer:
    // 0=leftDelay1, 1=leftDelay2, 2=leftAP2,
    // 3=rightDelay1, 4=rightDelay2, 5=rightAP2
    const DelayLine* delayBuf = nullptr;
    float totalDelay = 0.0f;

    switch (tap.bufferIndex)
    {
        case 0: delayBuf = &leftTank_.delay1;  totalDelay = leftTank_.delay1Samples;  break;
        case 1: delayBuf = &leftTank_.delay2;  totalDelay = leftTank_.delay2Samples;  break;
        case 2:
        {
            // Read from AP2's internal buffer at a fractional position
            const auto& ap = leftTank_.ap2;
            int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
            tapOffset = std::max (tapOffset, 1);
            return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
        }
        case 3: delayBuf = &rightTank_.delay1; totalDelay = rightTank_.delay1Samples; break;
        case 4: delayBuf = &rightTank_.delay2; totalDelay = rightTank_.delay2Samples; break;
        case 5:
        {
            const auto& ap = rightTank_.ap2;
            int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
            tapOffset = std::max (tapOffset, 1);
            return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
        }
        default: return 0.0f;
    }

    // Read from delay line at fractional position
    float tapDelay = tap.positionFrac * totalDelay;
    tapDelay = std::max (tapDelay, 1.0f);
    return delayBuf->readInterpolated (tapDelay);
}

// -----------------------------------------------------------------------
// Parameter setters

void DrumAirPresetEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (seconds, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DrumAirPresetEngine::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DrumAirPresetEngine::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DrumAirPresetEngine::setCrossoverFreq (float hz)
{
    const float maxLow = std::max (20.0f, highCrossoverFreq_ - 1.0f);
    crossoverFreq_ = std::clamp (hz, 20.0f, maxLow);
    if (prepared_)
        updateDecayCoefficients();
}

void DrumAirPresetEngine::setHighCrossoverFreq (float hz)
{
    const float minHigh = std::max (1.0f, crossoverFreq_ + 1.0f);
    highCrossoverFreq_ = std::max (hz, minHigh);
    if (prepared_)
        updateDecayCoefficients();
}

void DrumAirPresetEngine::setAirDampingScale (float scale)
{
    airDampingScale_ = std::max (scale, 0.01f);
    if (prepared_)
        updateDecayCoefficients();
}

void DrumAirPresetEngine::setModDepth (float depth)
{
    lastModDepthRaw_ = std::clamp (depth, 0.0f, 2.0f);
    // Map 0-1 knob range to 0-16 samples peak excursion (Dattorro: 8 samples typical)
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = lastModDepthRaw_ * 16.0f * rateRatio;
    // Noise jitter scales with depth and sample rate
    noiseModDepth_ = lastModDepthRaw_ * 12.0f * rateRatio;  // 12 samples peak at depth=1.0
}

void DrumAirPresetEngine::setModRate (float hz)
{
    modRateHz_ = hz;
    if (prepared_)
        updateLFORates();
}

void DrumAirPresetEngine::setLimiter (float thresholdDb, float releaseMs)
{
    if (thresholdDb >= 0.0f)
    {
        limiterThreshold_ = 0.0f;  // Disabled
        return;
    }
    limiterThreshold_ = std::pow (10.0f, thresholdDb / 20.0f);
    if (prepared_)
    {
        float releaseSamples = releaseMs * 0.001f * static_cast<float> (sampleRate_);
        limiterReleaseCoeff_ = std::exp (-1.0f / std::max (releaseSamples, 1.0f));
    }
}

void DrumAirPresetEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DrumAirPresetEngine::setSoftOnsetMs (float ms)
{
    float newMs = std::max (ms, 0.0f);
    bool changed = (newMs > 0.0f) != (softOnsetMs_ > 0.0f)
                || std::abs (newMs - softOnsetMs_) > 0.01f;
    softOnsetMs_ = newMs;

    if (prepared_ && softOnsetMs_ > 0.0f)
    {
        // Per-sample increment for linear ramp from 0→1 over softOnsetMs
        float samples = softOnsetMs_ * 0.001f * static_cast<float> (sampleRate_);
        softOnsetCoeff_ = 1.0f / std::max (samples, 1.0f);
        // Only reset ramp when value actually changes (not on every processBlock call)
        if (changed)
            softOnsetEnvL_ = 0.0f;
    }
    else
    {
        softOnsetCoeff_ = 0.0f;  // Disabled
        softOnsetEnvL_ = 1.0f;   // Full gain immediately
    }
}

void DrumAirPresetEngine::setDelayScale (float scale)
{
    delayScale_ = std::clamp (scale, 0.25f, 4.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DrumAirPresetEngine::setNoiseModDepth (float samples)
{
    // Independent noise jitter, decoupled from LFO modDepth.
    // When set (>= 0), this overrides the modDepth-coupled noise jitter.
    // Critical for algorithms with low modDepth (e.g., Chamber=0.05) that
    // still need aggressive jitter to suppress modal resonances.
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float maxJitter = 32.0f * rateRatio;
    independentNoiseModDepth_ = std::clamp (samples * rateRatio, -1.0f, maxJitter);
}

void DrumAirPresetEngine::setOutputTaps (const OutputTap* left, const OutputTap* right)
{
    if (left && right)
    {
        for (int i = 0; i < kNumOutputTaps; ++i)
        {
            customLeftTaps_[i] = left[i];
            customRightTaps_[i] = right[i];
        }
        useCustomTaps_ = true;
    }
    else
    {
        useCustomTaps_ = false;
    }
}

void DrumAirPresetEngine::applyTapGains (const float* leftGains, const float* rightGains)
{
    if (leftGains && rightGains)
    {
        // If custom taps aren't already active, initialize from defaults
        // so buffer indices and positions are valid before applying gains.
        if (! useCustomTaps_)
        {
            for (int i = 0; i < kNumOutputTaps; ++i)
            {
                customLeftTaps_[i] = kLeftOutputTaps[i];
                customRightTaps_[i] = kRightOutputTaps[i];
            }
        }

        for (int i = 0; i < kNumOutputTaps; ++i)
        {
            customLeftTaps_[i].gain = leftGains[i];
            customRightTaps_[i].gain = rightGains[i];
        }
        useCustomTaps_ = true;
    }
}

void DrumAirPresetEngine::setFreeze (bool frozen)
{
    frozen_ = frozen;
    if (frozen)
    {
        leftTank_.currentRMS = 0.0f;
        leftTank_.peakRMS = 0.0f;
        leftTank_.terminalDecayActive = false;
        rightTank_.currentRMS = 0.0f;
        rightTank_.peakRMS = 0.0f;
        rightTank_.terminalDecayActive = false;
    }
}

void DrumAirPresetEngine::setLateGainScale (float scale)
{
    lateGainScale_ = std::max (scale, 0.0f);
}

void DrumAirPresetEngine::setSizeRange (float min, float max)
{
    float newMin = std::max (min, 0.0f);
    float newMax = std::max (max, newMin);
    if (prepared_)
    {
        newMin = std::min (newMin, sizeRangeAllocatedMax_);
        newMax = std::min (newMax, sizeRangeAllocatedMax_);
    }
    sizeRangeMin_ = newMin;
    sizeRangeMax_ = std::max (newMax, sizeRangeMin_);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DrumAirPresetEngine::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DrumAirPresetEngine::setStructuralHFDamping (float hz)
{
    if (hz <= 0.0f)
    {
        structHFCoeff_ = 0.0f;
        structHFStateL_ = 0.0f;
        structHFStateR_ = 0.0f;
        return;
    }
    structHFCoeff_ = std::exp (-kTwoPi * hz / static_cast<float> (sampleRate_));
}

void DrumAirPresetEngine::setTerminalDecay (float thresholdDB, float factor)
{
    terminalDecayThresholdDB_ = -std::abs (thresholdDB);
    terminalDecayFactor_ = std::clamp (factor, 0.0f, 1.0f);
    terminalLinearThreshold_ = std::pow (10.0f, -terminalDecayThresholdDB_ * 0.1f);
}

void DrumAirPresetEngine::clearBuffers()
{
    auto clearTank = [] (Tank& tank, uint32_t seed, uint32_t noiseStateSeed)
    {
        tank.ap1Buffer.clear();
        tank.delay1.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].clear();
        tank.ap2.clear();
        tank.delay2.clear();
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
        tank.currentRMS = 0.0f;
        tank.peakRMS = 0.0f;
        tank.terminalDecayActive = false;
        tank.lfoPhase = 0.0f;
        tank.lfoPRNG = seed;
        tank.noiseState = noiseStateSeed;
    };

    clearTank (leftTank_, 0x12345678u, 0xDEADBEEFu);
    clearTank (rightTank_, 0x87654321u, 0xCAFEBABEu);
    // Restore 90° L/R phase offset for stereo decorrelation
    leftTank_.lfoPhase = 0.0f;
    rightTank_.lfoPhase = 1.5707963f;  // pi/2
    // Reset soft onset ramp (starts from 0 if enabled, 1 if disabled)
    softOnsetEnvL_ = (softOnsetMs_ > 0.0f) ? 0.0f : 1.0f;
    limiterEnv_ = 0.0f;
    structHFStateL_ = 0.0f;
    structHFStateR_ = 0.0f;
}

// -----------------------------------------------------------------------
// Internal update methods

void DrumAirPresetEngine::updateDelayLengths()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;
    float totalScale = sizeScale * delayScale_;  // Combined size + per-algorithm delay scaling

    auto updateTank = [&] (Tank& tank)
    {
        tank.ap1DelaySamples = static_cast<float> (tank.ap1BaseDelay) * rateRatio * totalScale;
        tank.delay1Samples   = static_cast<float> (tank.delay1BaseDelay) * rateRatio * totalScale;
        tank.delay2Samples   = static_cast<float> (tank.delay2BaseDelay) * rateRatio * totalScale;

        // AP2 delay (integer, used by Allpass::process)
        tank.ap2.delaySamples = std::max (1, static_cast<int> (
            static_cast<float> (tank.ap2BaseDelay) * rateRatio * totalScale));

        // Density cascade allpass delays (integer, scaled by rate + size + delayScale)
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].delaySamples = std::max (1, static_cast<int> (
                static_cast<float> (tank.densityAPBase[i]) * rateRatio * totalScale));
    };

    updateTank (leftTank_);
    updateTank (rightTank_);
}

void DrumAirPresetEngine::updateDecayCoefficients()
{
    float sr = static_cast<float> (sampleRate_);
    float lowXoverCoeff = std::exp (-kTwoPi * crossoverFreq_ / sr);
    float highXoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / sr);

    auto updateTankDamping = [&] (Tank& tank)
    {
        float loopLength = tank.ap1DelaySamples
                         + tank.delay1Samples
                         + static_cast<float> (tank.ap2.delaySamples)
                         + tank.delay2Samples;
        for (int i = 0; i < kNumDensityAPs; ++i)
            loopLength += static_cast<float> (tank.densityAP[i].delaySamples);

        float gBase = std::pow (10.0f, -3.0f * loopLength / (decayTime_ * sr));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);

        // FiveBandDamping: combine baked VV-derived multipliers with runtime
        // bass/treble/air controls, then compute per-band gains from gBase.
        float combinedMult[5] = {
            kFiveBandMult[0] * bassMultiply_,                                          // sub-bass
            kFiveBandMult[1] * bassMultiply_,                                          // bass
            kFiveBandMult[2] * trebleMultiply_,                                        // mid
            kFiveBandMult[3] * trebleMultiply_,                                        // presence
            kFiveBandMult[4] * std::max (trebleMultiply_ * airDampingScale_, 0.01f)    // air
        };
        tank.damping.setBandMultipliers (combinedMult);
        tank.damping.computeGainsFromBase (gBase, lowXoverCoeff, highXoverCoeff);
    };

    updateTankDamping (leftTank_);
    updateTankDamping (rightTank_);

    // Restore baked 5-band crossovers after gain computation
    // (computeGainsFromBase overwrites lpCoeff_ with geometric interpolation)
    {
        float coeffs[4];
        for (int c = 0; c < 4; ++c)
            coeffs[c] = std::exp (-kTwoPi * kFiveBandCrossoverHz[c]
                                  / static_cast<float> (sampleRate_));
        leftTank_.damping.setCrossovers (coeffs);
        rightTank_.damping.setCrossovers (coeffs);
    }
}

void DrumAirPresetEngine::updateLFORates()
{
    float sr = static_cast<float> (sampleRate_);

    // Asymmetric rates: left at base, right at golden ratio offset
    // to prevent correlated modulation between tanks.
    leftTank_.lfoPhaseInc  = kTwoPi * modRateHz_ / sr;
    rightTank_.lfoPhaseInc = kTwoPi * modRateHz_ * 1.1180339887f / sr;  // × sqrt(5)/2
}

// ==========================================================================
//  END PRIVATE COPY
// ==========================================================================


class DrumAirPresetWrapper : public PresetEngineBase
{
public:
    DrumAirPresetWrapper() = default;
    ~DrumAirPresetWrapper() override = default;

    void prepare (double sampleRate, int maxBlockSize) override
    {
        sampleRate_ = sampleRate;
        // Apply sizing/scaling config BEFORE engine_.prepare() so buffers
        // allocate at the right size for the actual host sample rate AND for
        // the per-preset delay scale (Invariant 3).
        if (overrideSizeRangeMin_ >= 0.0f)
            engine_.setSizeRange (overrideSizeRangeMin_, overrideSizeRangeMax_);
        else
            engine_.setSizeRange (kBakedSizeRangeMin, kBakedSizeRangeMax);
        engine_.setDelayScale (kVvDelayScale);
        engine_.setLateGainScale (kBakedLateGainScale);
        // VV-derived crossovers (clamped >0 in setters per Invariant 4)
        engine_.setHighCrossoverFreq (kVvHighCrossoverHz);
        engine_.setCrossoverFreq     (kVvCrossoverHz);
        // Bass/treble defaults must be set BEFORE prepare so decay coefficients
        // are computed with the correct damping baseline.
        float savedBass = lastBass_;
        float savedTreble = lastTreble_;
        engine_.setBassMultiply (kBakedBassMultScale);
        lastBass_ = 1.0f;
        engine_.setTrebleMultiply (kBakedTrebleMultScale);
        lastTreble_ = kBakedTrebleMultScale;
        engine_.setAirDampingScale (kBakedAirDampingScale * kVvAirDampingScale);
        engine_.prepare (sampleRate, maxBlockSize);
        // setNoiseModDepth scales by the engine's stored sampleRate_, so it
        // must run AFTER prepare() or the baked jitter is locked to 44.1kHz.
        engine_.setNoiseModDepth (kBakedNoiseModDepth);

        // The bass/treble multipliers are LEFT at the legacy
        // AlgorithmConfig defaults (kBakedBassMultScale, kBakedTrebleMultScale)
        // because they were hand-calibrated against the existing engine and
        // mapping VV's per-band RT60 ratios into them produced regressions.
        // The runtime setters below still apply the legacy curves, so a
        // neutral user knob lands on the AlgorithmConfig default.
        //
        // The VV-derived structural correction is concentrated in:
        //   - kVvAirDampingScale → setAirDampingScale (real air-band control)
        //   - kVvDelayScale      → setSizeRange / setDelayScale
        //   - kVvTiltLow/High*   → post-engine tilt EQ
        //   - kVvStereoWidth     → post-engine mid/side mix
        //   - base engine pick   → topology selection
        // Replay any cached runtime overrides after prepare so they survive
        // re-preparation (e.g. DAW sample rate change). Without this, overrides
        // set by the host or optimizer would be silently reset to baked defaults.
        if (overrideAirDamping_ >= 0.0f)
            setAirDampingScale (overrideAirDamping_);
        if (overrideHighCrossover_ >= 0.0f)
            setHighCrossoverFreq (overrideHighCrossover_);
        if (overrideCrossover_ >= 0.0f)
            setCrossoverFreq (overrideCrossover_);
        if (overrideNoiseMod_ >= 0.0f)
            setNoiseModDepth (overrideNoiseMod_);
        if (overrideLateGain_ >= 0.0f)
            setLateGainScale (overrideLateGain_);
        if (overrideSizeRangeMin_ >= 0.0f)
            setSizeRange (overrideSizeRangeMin_, overrideSizeRangeMax_);
        if (lastTerminalFactor_ < 1.0f)
            setTerminalDecay (lastTerminalThresholdDb_, lastTerminalFactor_);
        if (frozen_)
            setFreeze (true);
        if (savedBass != 1.0f)
            setBassMultiply (savedBass);
        if (savedTreble != kBakedTrebleMultScale && savedTreble != 0.5f)
        {
            engine_.setTrebleMultiply (savedTreble);
            lastTreble_ = savedTreble;
        }
        if (lastStructHFHz_ > 0.0f)
            setStructuralHFDamping (lastStructHFHz_);

        // Pre-compute per-preset tilt EQ coefficients at the actual host
        // sample rate. These are derived from the VV IR's frequency
        // response (see derive_topology_from_vv.py) and replace the
        // earlier global -3dB/+3dB tilt that was applied uniformly to
        // every preset.
        const float twoPi = 6.283185307179586f;
        const float lowHz  = std::max (kVvTiltLowHz,  1.0f);
        const float highHz = std::max (kVvTiltHighHz, 1.0f);
        tiltLowCoeff_  = std::exp (-twoPi * lowHz  / static_cast<float> (sampleRate));
        tiltLowGain_   = std::pow (10.0f, kVvTiltLowDb / 20.0f) - 1.0f;
        tiltHighCoeff_ = std::exp (-twoPi * highHz / static_cast<float> (sampleRate));
        tiltHighGain_  = std::pow (10.0f, kVvTiltHighDb / 20.0f) - 1.0f;

        // -----------------------------------------------------------------
        // Per-preset corrective 12-band peaking EQ. Computes RBJ peaking
        // biquad coefficients from kCorrEqHz / kCorrEqDb at the host sample
        // rate. Each band's center is clamped to <0.45*sr to avoid
        // sub-Nyquist filter design issues at low sample rates.
        // -----------------------------------------------------------------
        const float corrSr = static_cast<float> (sampleRate);
        const float nyquistGuard = 0.45f * corrSr;
        for (int i = 0; i < kCorrEqBandCount; ++i)
        {
            const float gainDb = kCorrEqDb[i];
            // Bands above the Nyquist guard become neutral (bypass)
            // to avoid stacking multiple bands at the guard frequency.
            if (kCorrEqHz[i] >= nyquistGuard)
            {
                corrB0_[i] = 1.0f;
                corrB1_[i] = 0.0f;
                corrB2_[i] = 0.0f;
                corrA1_[i] = 0.0f;
                corrA2_[i] = 0.0f;
                continue;
            }
            const float fc     = kCorrEqHz[i];
            // RBJ peaking EQ formulas
            const float A     = std::pow (10.0f, gainDb / 40.0f);
            const float w0    = twoPi * fc / corrSr;
            const float cosW0 = std::cos (w0);
            const float alpha = std::sin (w0) / (2.0f * kCorrEqQ);
            const float a0_   = 1.0f + alpha / A;
            corrB0_[i] = (1.0f + alpha * A) / a0_;
            corrB1_[i] = (-2.0f * cosW0)    / a0_;
            corrB2_[i] = (1.0f - alpha * A) / a0_;
            corrA1_[i] = (-2.0f * cosW0)    / a0_;
            corrA2_[i] = (1.0f - alpha / A) / a0_;
        }

        // Note: do NOT call clearBuffers() here — engine_.prepare() already
        // initializes all buffers and randomizes LFO/PRNG state. Calling
        // clearBuffers() would erase the randomized modulation state.
        // clearBuffers() is called externally by DuskVerbEngine on algorithm switch.
        // Reset only the wrapper's own filter state (tilt EQ, corrective EQ).
        tiltLpL_ = tiltLpR_ = tiltHpLpL_ = tiltHpLpR_ = 0.0f;
        for (int b = 0; b < kCorrEqBandCount; ++b)
        {
            corrXl1_[b] = corrXl2_[b] = 0.0f;
            corrYl1_[b] = corrYl2_[b] = 0.0f;
            corrXr1_[b] = corrXr2_[b] = 0.0f;
            corrYr1_[b] = corrYr2_[b] = 0.0f;
        }
        // PATCH_POINT_PREPARE_END
    }

    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples) override
    {
        // PATCH_POINT_PRE_ENGINE
        engine_.process (inputL, inputR, outputL, outputR, numSamples);
        // ----- Per-preset tilt EQ + 12-band correction EQ + stereo width -----
        // 1. tilt shelves (broad VV character correction)
        // 2. 12-band peaking EQ (per-preset spectral_balance correction)
        // 3. mid/side stereo width
        constexpr float kStereoWidth = kVvStereoWidth;
        for (int i = 0; i < numSamples; ++i)
        {
            // ---- Stage 1: tilt shelves ----
            float xL = outputL[i];
            tiltLpL_ = (1.0f - tiltLowCoeff_) * xL + tiltLowCoeff_ * tiltLpL_;
            float lowCutL = xL + tiltLpL_ * tiltLowGain_;
            tiltHpLpL_ = (1.0f - tiltHighCoeff_) * lowCutL + tiltHighCoeff_ * tiltHpLpL_;
            float yL = lowCutL + (lowCutL - tiltHpLpL_) * tiltHighGain_;

            float xR = outputR[i];
            tiltLpR_ = (1.0f - tiltLowCoeff_) * xR + tiltLowCoeff_ * tiltLpR_;
            float lowCutR = xR + tiltLpR_ * tiltLowGain_;
            tiltHpLpR_ = (1.0f - tiltHighCoeff_) * lowCutR + tiltHighCoeff_ * tiltHpLpR_;
            float yR = lowCutR + (lowCutR - tiltHpLpR_) * tiltHighGain_;

            // ---- Stage 2: 12-band peaking EQ correction ----
            // Each band runs as a Direct-Form-1 biquad with per-channel state.
            for (int b = 0; b < kCorrEqBandCount; ++b)
            {
                const float xLb = yL;
                yL = corrB0_[b] * xLb
                   + corrB1_[b] * corrXl1_[b]
                   + corrB2_[b] * corrXl2_[b]
                   - corrA1_[b] * corrYl1_[b]
                   - corrA2_[b] * corrYl2_[b];
                corrXl2_[b] = corrXl1_[b];
                corrXl1_[b] = xLb;
                corrYl2_[b] = corrYl1_[b];
                corrYl1_[b] = yL;

                const float xRb = yR;
                yR = corrB0_[b] * xRb
                   + corrB1_[b] * corrXr1_[b]
                   + corrB2_[b] * corrXr2_[b]
                   - corrA1_[b] * corrYr1_[b]
                   - corrA2_[b] * corrYr2_[b];
                corrXr2_[b] = corrXr1_[b];
                corrXr1_[b] = xRb;
                corrYr2_[b] = corrYr1_[b];
                corrYr1_[b] = yR;
            }

            // ---- Stage 3: stereo width ----
            float mid  = 0.5f * (yL + yR);
            float side = 0.5f * (yL - yR);
            // Re-limit after corrective EQ + stereo width to prevent
            // clipping from up to +12dB of per-band boost.
            outputL[i] = std::clamp (mid + side * kStereoWidth, -32.0f, 32.0f);
            outputR[i] = std::clamp (mid - side * kStereoWidth, -32.0f, 32.0f);
        }
        // PATCH_POINT_POST_ENGINE
    }

    void clearBuffers() override
    {
        engine_.clearBuffers();
        tiltLpL_   = 0.0f;
        tiltLpR_   = 0.0f;
        tiltHpLpL_ = 0.0f;
        tiltHpLpR_ = 0.0f;
        for (int b = 0; b < kCorrEqBandCount; ++b)
        {
            corrXl1_[b] = corrXl2_[b] = 0.0f;
            corrYl1_[b] = corrYl2_[b] = 0.0f;
            corrXr1_[b] = corrXr2_[b] = 0.0f;
            corrYr1_[b] = corrYr2_[b] = 0.0f;
        }
        // PATCH_POINT_CLEAR
    }

    // --- Runtime setters ---
    // Each forwards the user parameter after applying any baked scale factor.

    void setDecayTime (float seconds) override
    {
        // The legacy kBakedDecayTimeScale matches the user-knob units the
        // wrapper was historically calibrated against. kVvDecayTimeScale
        // is the per-preset correction (derived from rendered DV vs VV
        // RT60) that brings the engine's actual tail length in line with
        // VV's measured RT60.
        engine_.setDecayTime (seconds * kBakedDecayTimeScale * kVvDecayTimeScale);
    }

    void setBassMultiply (float mult) override
    {
        // Use the legacy AlgorithmConfig scale — VV bass mapping was
        // attempted but produced regressions on the canonical Tiled Room
        // PASS case. The VV correction is applied via tilt EQ and air
        // damping instead.
        engine_.setBassMultiply (mult * kBakedBassMultScale);
        lastBass_ = mult;
    }

    void setTrebleMultiply (float mult) override
    {
        // Legacy nonlinear curve from DuskVerbEngine, preserved verbatim.
        // (See note in setBassMultiply.)
        const float curve = mult * mult;
        const float scaled = kBakedTrebleMultScale * (1.0f - curve)
                           + kBakedTrebleMultScaleMax * curve;
        engine_.setTrebleMultiply (scaled);
        // Cache the SCALED engine-facing value (Invariant 2) so any
        // structural HF damping replay uses the consistent value.
        lastTreble_ = scaled;
        if (lastStructHFHz_ > 0.0f)
            setStructuralHFDamping (lastStructHFHz_);
    }

    void setCrossoverFreq (float hz) override
    {
        overrideCrossover_ = hz;
        engine_.setCrossoverFreq (hz);
    }

    void setModDepth (float depth) override
    {
        engine_.setModDepth (depth * kBakedModDepthScale);
    }

    void setModRate (float hz) override
    {
        engine_.setModRate (hz * kBakedModRateScale);
    }

    void setSize (float size) override { engine_.setSize (size); }
    void setFreeze (bool frozen) override
    {
        frozen_ = frozen;
        engine_.setFreeze (frozen);
    }
    void setDecayBoost (float boost) override { engine_.setDecayBoost (boost); }
    void setTerminalDecay (float td, float f) override
    {
        lastTerminalThresholdDb_ = td;
        lastTerminalFactor_ = f;
        engine_.setTerminalDecay (td, f);
    }
    void setHighCrossoverFreq (float hz) override
    {
        overrideHighCrossover_ = hz;
        engine_.setHighCrossoverFreq (hz);
    }

    void setAirDampingScale (float scale) override
    {
        overrideAirDamping_ = scale;
        engine_.setAirDampingScale (kBakedAirDampingScale * kVvAirDampingScale * scale);
    }

    void setNoiseModDepth (float samples) override
    {
        overrideNoiseMod_ = samples;
        engine_.setNoiseModDepth (samples);
    }

    void setStructuralHFDamping (float hz) override
    {
        lastStructHFHz_ = hz;
        engine_.setStructuralHFDamping (hz);
    }

    void setSizeRange (float mn, float mx) override
    {
        overrideSizeRangeMin_ = mn;
        overrideSizeRangeMax_ = mx;
        engine_.setSizeRange (mn, mx);
    }
    void setLateGainScale (float scale) override
    {
        overrideLateGain_ = std::max (scale, 0.0f);
        engine_.setLateGainScale (overrideLateGain_ * kBakedLateGainScale);
    }

    // Reset hooks: restore baked defaults when override sentinel fires
    void resetAirDampingToDefault() override
    {
        overrideAirDamping_ = -1.0f;
        engine_.setAirDampingScale (kBakedAirDampingScale * kVvAirDampingScale);
    }
    void resetHighCrossoverToDefault() override
    {
        overrideHighCrossover_ = -1.0f;
        engine_.setHighCrossoverFreq (kVvHighCrossoverHz);
    }
    void resetLowCrossoverToDefault() override
    {
        overrideCrossover_ = -1.0f;
        engine_.setCrossoverFreq (kVvCrossoverHz);
    }
    void resetNoiseModToDefault() override
    {
        overrideNoiseMod_ = -1.0f;
        engine_.setNoiseModDepth (kBakedNoiseModDepth);
    }
    void resetLateGainToDefault() override
    {
        overrideLateGain_ = -1.0f;
        engine_.setLateGainScale (kBakedLateGainScale);
    }
    void resetSizeRangeToDefault() override
    {
        overrideSizeRangeMin_ = -1.0f;
        overrideSizeRangeMax_ = -1.0f;
        engine_.setSizeRange (kBakedSizeRangeMin, kBakedSizeRangeMax);
    }

    const char* getPresetName() const override { return "Drum Air"; }
    const char* getBaseEngineType() const override { return "DattorroTank"; }

    // Fix 4 (spectral): expose corrective EQ coefficients to DuskVerbEngine
    // so it can apply the same EQ to the ER path.
    int getCorrEQBandCount() const override { return kCorrEqBandCount; }
    bool getCorrEQCoeffs (float* b0, float* b1, float* b2,
                           float* a1, float* a2, int maxBands) const override
    {
        if (sampleRate_ == 0)
            return false;
        int n = std::min (kCorrEqBandCount, maxBands);
        for (int i = 0; i < n; ++i)
        {
            b0[i] = corrB0_[i];
            b1[i] = corrB1_[i];
            b2[i] = corrB2_[i];
            a1[i] = corrA1_[i];
            a2[i] = corrA2_[i];
        }
        return true;
    }

private:
    DrumAirPresetEngine engine_;
    float lastTreble_ = 0.5f;
    float lastBass_ = 1.0f;
    float lastStructHFHz_ = 0.0f;
    float lastTerminalThresholdDb_ = 0.0f;
    float lastTerminalFactor_ = 1.0f;  // 1.0 = disabled
    bool  frozen_ = false;
    double sampleRate_ = 48000.0;
    // Cached runtime overrides — replayed after prepare() to survive re-preparation.
    // Sentinel value -1.0 means "no override, use baked default".
    float overrideAirDamping_ = -1.0f;
    float overrideHighCrossover_ = -1.0f;
    float overrideCrossover_ = -1.0f;
    float overrideNoiseMod_ = -1.0f;
    float overrideLateGain_ = -1.0f;
    float overrideSizeRangeMin_ = -1.0f;
    float overrideSizeRangeMax_ = -1.0f;
    // Baked tilt EQ state (per-instance, not function-local statics)
    float tiltLowCoeff_ = 0.0f;
    float tiltLowGain_  = 0.0f;
    float tiltHighCoeff_ = 0.0f;
    float tiltHighGain_  = 0.0f;
    float tiltLpL_   = 0.0f;
    float tiltLpR_   = 0.0f;
    float tiltHpLpL_ = 0.0f;
    float tiltHpLpR_ = 0.0f;
    // 12-band corrective EQ — coefficients computed in prepare() at the
    // host sample rate so the EQ tracks the host correctly. State is
    // per-channel (L/R) Direct-Form-1 biquad memory.
    float corrB0_[kCorrEqBandCount] {};
    float corrB1_[kCorrEqBandCount] {};
    float corrB2_[kCorrEqBandCount] {};
    float corrA1_[kCorrEqBandCount] {};
    float corrA2_[kCorrEqBandCount] {};
    float corrXl1_[kCorrEqBandCount] {};
    float corrXl2_[kCorrEqBandCount] {};
    float corrYl1_[kCorrEqBandCount] {};
    float corrYl2_[kCorrEqBandCount] {};
    float corrXr1_[kCorrEqBandCount] {};
    float corrXr2_[kCorrEqBandCount] {};
    float corrYr1_[kCorrEqBandCount] {};
    float corrYr2_[kCorrEqBandCount] {};
    // PATCH_POINT_MEMBERS
};

} // anonymous namespace

std::unique_ptr<PresetEngineBase> createDrumAirPreset()
{
    return std::unique_ptr<PresetEngineBase> (new DrumAirPresetWrapper());
}

// Self-register at static init time so DuskVerbEngine can look up this preset by name
static PresetEngineRegistrar gDrumAirPresetRegistrar ("PresetDrumAir", &createDrumAirPreset);


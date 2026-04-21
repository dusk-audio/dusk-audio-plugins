// GENERATED FILE - do not edit by hand (use generate_preset_engines.py)
//
// Per-preset reverb engine for "Exciting Snare room".
// Base engine: DattorroTank
//
// This file contains a full private copy of the DattorroTank DSP in an
// anonymous namespace, so modifications to this preset's DSP cannot affect
// any other preset. The class has been renamed to ExcitingSnareRoomPresetEngine to avoid
// ODR conflicts.

#include "ExcitingSnareRoomPreset.h"
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

    // Baked algorithm-config constants from kPresetExcitingSnareRoom in AlgorithmConfig.h
    // at generation time. Editing these here has no effect on other presets.
    constexpr float kBakedLateGainScale      = 0.63f;
    constexpr float kBakedSizeRangeMin       = 0.2f;
    constexpr float kBakedSizeRangeMax       = 0.6f;
    constexpr float kBakedAirDampingScale    = 0.75f;
    constexpr float kBakedNoiseModDepth      = 25.0f;
    constexpr float kBakedTrebleMultScale    = 0.45f;
    constexpr float kBakedTrebleMultScaleMax = 0.82f;
    constexpr float kBakedBassMultScale      = 1.0f;  // un-baked from 0.85f — bass knob now shows real multiplier
    constexpr float kBakedModDepthScale      = 1.3f;
    constexpr float kBakedModRateScale       = 1.0f;
    constexpr float kBakedDecayTimeScale     = 1.0f;   // un-baked from 1.37f — factory decay now displays real RT60

    // Per-preset FiveBandDamping: attenuates 2.5-8kHz presence band to fix
    // +6dB spectral excess at 5-8kHz that the ±12dB corrective EQ can't reach.
    constexpr float kFiveBandMult[5] = { 1.00f, 1.00f, 1.05f, 5.00f, 0.45f };
    constexpr float kFiveBandCrossoverHz[4] = { 150.0f, 600.0f, 3500.0f, 9000.0f };


    // -----------------------------------------------------------------
    // Per-preset VV-derived structural constants
    // (from vv_topology_baked.json — derived from VV IR analysis).
    // These set the engine to a per-preset target at prepare() time;
    // runtime setters layer relative scaling on top of them.
    // -----------------------------------------------------------------
    constexpr float kVvCrossoverHz       = 1000.0f;
    constexpr float kVvHighCrossoverHz   = 6000.0f;
    constexpr float kVvAirDampingScale   = 1.02593f;
    constexpr float        kVvDelayScale        = 1.0f;  // un-baked from 0.4f — size range now absolute
    constexpr float kVvTiltLowDb         = -2.87411f;
    constexpr float kVvTiltLowHz         = 400.0f;
    constexpr float kVvTiltHighDb        = -6.0000f;
    constexpr float kVvTiltHighHz        = 5000.0f;
    constexpr float kVvStereoWidth       = 0.900246f;
    // Decay-time correction: multiplied into the user knob in setDecayTime()
    // to bring the engine's actual RT60 in line with VV's measured RT60.
    // Derived by render-then-measure (see derive_decay_scale.py).
    // 1.0 = no correction; values < 1 shorten the tail, > 1 lengthen it.
    constexpr float kVvDecayTimeScale    = 1.0f;   // un-baked from 0.336526f
    // Per-preset 12-band corrective peaking EQ (from vv_correction_eq.json).
    // Derived by rendering DV at factory defaults and computing the per-band
    // dB delta vs VV. Applied post-engine in process() to push DV's spectral
    // character toward VV's. Coefficients are computed from these constants
    // in prepare() at the host sample rate so the EQ is correct at any rate.
    // Max correction magnitude for this preset: 4.38 dB
    // -----------------------------------------------------------------
    constexpr int kCorrEqBandCount = 12;
    constexpr float kCorrEqHz[kCorrEqBandCount] = { 100.0f, 158.0f, 251.0f, 397.0f, 632.0f, 1000.0f, 1581.0f, 2510.0f, 3969.0f, 6325.0f, 9798.0f, 15492.0f };
    constexpr float kCorrEqDb[kCorrEqBandCount] = { -4.93441f, -8.78355f, 0.182755f, 2.38519f, 8.0f, -8.07375f, -2.90708f, -6.59967f, -12.0f, -24.0f, 2.476f, 12.0f };
    constexpr float kCorrEqQ = 1.41f;  // moderate Q ≈ 1 octave bandwidth

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
class ExcitingSnareRoomPresetEngine
{
public:
    ExcitingSnareRoomPresetEngine();

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
        float inputLpState = 0.0f;  // Input HF filter state

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
        float preDensityOut = 0.0f;  // Output before density cascade (for onset tap reading)

        // Five-band damping (per-band decay with presence attenuation)
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
    float limiterReleaseMs_ = 50.0f;      // Stored release time for recomputation on sample rate change
    float limiterEnv_ = 0.0f;             // Current peak envelope

    bool frozen_ = false;
    bool prepared_ = false;

    // Time-varying input HF filter: strong at onset, relaxes over ~80ms
    float onsetLpCoeff_ = 0.0f;       // exp(-2pi*fc/sr) for onset LP
    int onsetTotalSamples_ = 0;       // total onset duration in samples
    int onsetSamplesRemaining_ = 0;   // countdown

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

void ExcitingSnareRoomPresetEngine::DelayLine::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void ExcitingSnareRoomPresetEngine::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float ExcitingSnareRoomPresetEngine::DelayLine::readInterpolated (float delaySamples) const
{
    float readPos = static_cast<float> (writePos) - delaySamples;
    int intIdx = static_cast<int> (std::floor (readPos));
    float frac = readPos - static_cast<float> (intIdx);
    return DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
}

// -----------------------------------------------------------------------
// Allpass helpers

void ExcitingSnareRoomPresetEngine::Allpass::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void ExcitingSnareRoomPresetEngine::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// -----------------------------------------------------------------------
// Constructor

ExcitingSnareRoomPresetEngine::ExcitingSnareRoomPresetEngine()
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

void ExcitingSnareRoomPresetEngine::setHallScale (bool enable)
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

void ExcitingSnareRoomPresetEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    // Preserve headroom for post-prepare size-range overrides.
    sizeRangeAllocatedMax_ = std::max (sizeRangeAllocatedMax_, sizeRangeMax_);
    // Sample-rate-invariant terminal decay smoothing coefficients
    {
        constexpr float kRmsTauMs = 45.0f;
        constexpr float kPeakTauMs = 2270.0f;
        float sr = static_cast<float> (sampleRate_);
        rmsAlpha_ = std::exp (-1000.0f / (kRmsTauMs * sr));
        peakDecayAlpha_ = std::exp (-1000.0f / (kPeakTauMs * sr));
    }
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);

    // Modulation headroom beyond the max scaled delay (scale with sample rate)
    const int maxModExcursion = static_cast<int> (std::ceil (32.0 * sampleRate / 44100.0));

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

    // Time-varying input HF filter: LP at 4kHz, relaxes over 80ms onset
    onsetLpCoeff_ = std::exp (-6.283185307f * 4000.0f / static_cast<float> (sampleRate_));
    onsetTotalSamples_ = static_cast<int> (0.150f * static_cast<float> (sampleRate_));
    onsetSamplesRemaining_ = onsetTotalSamples_;
    leftTank_.inputLpState = 0.0f;
    rightTank_.inputLpState = 0.0f;

    // FiveBandDamping: set crossovers and band multipliers before decay computation
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

    // Reset soft-onset ramp and limiter envelope so a re-prepare starts clean
    softOnsetEnvL_ = (softOnsetMs_ > 0.0f) ? 0.0f : 1.0f;
    limiterEnv_ = 0.0f;

    // Recompute limiter release coefficient for the new sample rate
    if (limiterThreshold_ > 0.0f)
    {
        float releaseSamples = limiterReleaseMs_ * 0.001f * static_cast<float> (sampleRate_);
        limiterReleaseCoeff_ = std::exp (-1.0f / std::max (releaseSamples, 1.0f));
    }
}

// -----------------------------------------------------------------------

void ExcitingSnareRoomPresetEngine::process (const float* inputL, const float* inputR,
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

            // Time-varying input HF cut: strong LP at onset, relaxes to passthrough.
            // Fixes 5-8kHz onset excess where FDN fills instantly vs VV's gradual buildup.
            if (onsetSamplesRemaining_ > 0)
            {
                float envelope = static_cast<float> (onsetSamplesRemaining_) / static_cast<float> (onsetTotalSamples_);
                float hpAtten = 1.0f - envelope * 0.85f;  // starts at 0.15, ramps to 1.0
                tank.inputLpState = (1.0f - onsetLpCoeff_) * tankIn + onsetLpCoeff_ * tank.inputLpState;
                float hpPart = tankIn - tank.inputLpState;
                tankIn = tank.inputLpState + hpPart * hpAtten;
            }

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
            tank.preDensityOut = del1Out;  // Store pre-cascade for onset tap reading
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
            // Denormal prevention: suppressed during freeze to prevent tail mutation
            float bias = frozen_ ? 0.0f
                                 : (((tank.delay2.writePos ^ 1) & 1)
                                        ? +DspUtils::kDenormalPrevention
                                        : -DspUtils::kDenormalPrevention);
            tank.delay2.write (ap2Out + bias);

            // Terminal decay: extra damping when tail is far below peak
            if (!frozen_ && terminalDecayFactor_ < 1.0f)
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

        if (onsetSamplesRemaining_ > 0)
            --onsetSamplesRemaining_;

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
        // DuskVerbEngine applies lateGainScale_ externally — do NOT apply here.
        constexpr float kOutputScale = 0.14285714f;  // 1/7 — average of 7 taps

        float scaledL = outL * kOutputScale * lateGainScale_;
        float scaledR = outR * kOutputScale * lateGainScale_;

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

float ExcitingSnareRoomPresetEngine::readOutputTap (const OutputTap& tap) const
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

void ExcitingSnareRoomPresetEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (seconds, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void ExcitingSnareRoomPresetEngine::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void ExcitingSnareRoomPresetEngine::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void ExcitingSnareRoomPresetEngine::setCrossoverFreq (float hz)
{
    const float maxLow = std::max (20.0f, highCrossoverFreq_ - 1.0f);
    crossoverFreq_ = std::clamp (hz, 20.0f, maxLow);
    if (prepared_)
        updateDecayCoefficients();
}

void ExcitingSnareRoomPresetEngine::setHighCrossoverFreq (float hz)
{
    const float minHigh = std::max (1.0f, crossoverFreq_ + 1.0f);
    highCrossoverFreq_ = std::max (hz, minHigh);
    if (prepared_)
        updateDecayCoefficients();
}

void ExcitingSnareRoomPresetEngine::setAirDampingScale (float scale)
{
    airDampingScale_ = std::max (scale, 0.01f);
    if (prepared_)
        updateDecayCoefficients();
}

void ExcitingSnareRoomPresetEngine::setModDepth (float depth)
{
    lastModDepthRaw_ = std::clamp (depth, 0.0f, 2.0f);
    // Map 0-1 knob range to 0-16 samples peak excursion (Dattorro: 8 samples typical)
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = lastModDepthRaw_ * 16.0f * rateRatio;
    // Noise jitter scales with depth and sample rate
    noiseModDepth_ = lastModDepthRaw_ * 12.0f * rateRatio;  // 12 samples peak at depth=1.0
}

void ExcitingSnareRoomPresetEngine::setModRate (float hz)
{
    modRateHz_ = hz;
    if (prepared_)
        updateLFORates();
}

void ExcitingSnareRoomPresetEngine::setLimiter (float thresholdDb, float releaseMs)
{
    if (thresholdDb >= 0.0f)
    {
        limiterThreshold_ = 0.0f;  // Disabled
        return;
    }
    limiterThreshold_ = std::pow (10.0f, thresholdDb / 20.0f);
    limiterReleaseMs_ = releaseMs;
    if (prepared_)
    {
        float releaseSamples = releaseMs * 0.001f * static_cast<float> (sampleRate_);
        limiterReleaseCoeff_ = std::exp (-1.0f / std::max (releaseSamples, 1.0f));
    }
}

void ExcitingSnareRoomPresetEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void ExcitingSnareRoomPresetEngine::setSoftOnsetMs (float ms)
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

void ExcitingSnareRoomPresetEngine::setDelayScale (float scale)
{
    delayScale_ = std::clamp (scale, 0.25f, 4.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void ExcitingSnareRoomPresetEngine::setNoiseModDepth (float samples)
{
    // Independent noise jitter, decoupled from LFO modDepth.
    // When set (>= 0), this overrides the modDepth-coupled noise jitter.
    // Critical for algorithms with low modDepth (e.g., Chamber=0.05) that
    // still need aggressive jitter to suppress modal resonances.
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float maxJitter = 32.0f * rateRatio;
    independentNoiseModDepth_ = std::clamp (samples * rateRatio, -1.0f, maxJitter);
}

void ExcitingSnareRoomPresetEngine::setOutputTaps (const OutputTap* left, const OutputTap* right)
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

void ExcitingSnareRoomPresetEngine::applyTapGains (const float* leftGains, const float* rightGains)
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

void ExcitingSnareRoomPresetEngine::setFreeze (bool frozen)
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

void ExcitingSnareRoomPresetEngine::setLateGainScale (float scale)
{
    lateGainScale_ = std::max (scale, 0.0f);
}

void ExcitingSnareRoomPresetEngine::setSizeRange (float min, float max)
{
    sizeRangeMin_ = std::max (min, 0.0f);
    sizeRangeMax_ = std::max (max, sizeRangeMin_);
    // Prevent buffer overrun: don't grow beyond prepare()-allocated capacity
    if (prepared_)
    {
        if (sizeRangeMin_ > sizeRangeAllocatedMax_)
            sizeRangeMin_ = sizeRangeAllocatedMax_;
        if (sizeRangeMax_ > sizeRangeAllocatedMax_)
            sizeRangeMax_ = sizeRangeAllocatedMax_;
        if (sizeRangeMin_ > sizeRangeMax_)
            sizeRangeMin_ = sizeRangeMax_;
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void ExcitingSnareRoomPresetEngine::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void ExcitingSnareRoomPresetEngine::setStructuralHFDamping (float hz)
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

void ExcitingSnareRoomPresetEngine::setTerminalDecay (float thresholdDB, float factor)
{
    terminalDecayThresholdDB_ = -std::abs (thresholdDB);
    terminalDecayFactor_ = std::clamp (factor, 0.0f, 1.0f);
    terminalLinearThreshold_ = std::pow (10.0f, -terminalDecayThresholdDB_ * 0.1f);
}

void ExcitingSnareRoomPresetEngine::clearBuffers()
{
    auto clearTank = [] (Tank& tank)
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
    };

    clearTank (leftTank_);
    clearTank (rightTank_);
    leftTank_.inputLpState = 0.0f;
    rightTank_.inputLpState = 0.0f;
    onsetSamplesRemaining_ = onsetTotalSamples_;

    // Reset modulation state to deterministic seeds (must match prepare())
    leftTank_.lfoPhase    = 0.0f;
    leftTank_.lfoPRNG     = 0x12345678u;
    leftTank_.noiseState  = 0xDEADBEEFu;
    rightTank_.lfoPhase   = 1.5707963f;  // 90 deg offset for stereo decorrelation
    rightTank_.lfoPRNG    = 0x87654321u;
    rightTank_.noiseState = 0xCAFEBABEu;

    // Reset soft onset ramp (starts from 0 if enabled, 1 if disabled)
    softOnsetEnvL_ = (softOnsetMs_ > 0.0f) ? 0.0f : 1.0f;
    limiterEnv_ = 0.0f;
    structHFStateL_ = 0.0f;
    structHFStateR_ = 0.0f;
}

// -----------------------------------------------------------------------
// Internal update methods

void ExcitingSnareRoomPresetEngine::updateDelayLengths()
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

void ExcitingSnareRoomPresetEngine::updateDecayCoefficients()
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

        // 5-band damping: combine baked per-band multipliers with runtime controls
        float combinedMult[5] = {
            kFiveBandMult[0] * bassMultiply_,
            kFiveBandMult[1] * bassMultiply_,
            kFiveBandMult[2] * trebleMultiply_,
            kFiveBandMult[3] * trebleMultiply_,
            kFiveBandMult[4] * std::max (airDampingScale_, 0.01f)
        };
        tank.damping.setBandMultipliers (combinedMult);
        tank.damping.computeGainsFromBase (gBase, lowXoverCoeff, highXoverCoeff);
    };

    updateTankDamping (leftTank_);
    updateTankDamping (rightTank_);

    // Re-apply 5-band crossovers (computeGainsFromBase overwrites inner crossovers)
    float fbCoeffs[4];
    for (int b = 0; b < 4; ++b)
        fbCoeffs[b] = std::exp (-6.283185307f * kFiveBandCrossoverHz[b] / sr);
    leftTank_.damping.setCrossovers (fbCoeffs);
    rightTank_.damping.setCrossovers (fbCoeffs);
}

void ExcitingSnareRoomPresetEngine::updateLFORates()
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


class ExcitingSnareRoomPresetWrapper : public PresetEngineBase
{
public:
    ExcitingSnareRoomPresetWrapper() = default;
    ~ExcitingSnareRoomPresetWrapper() override = default;

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
        engine_.setAirDampingScale (kBakedAirDampingScale * kVvAirDampingScale);
        // Seed baked bass/treble BEFORE prepare() so updateDecayCoefficients()
        // inside prepare uses the calibrated values, not constructor defaults.
        {
            engine_.setBassMultiply (kBakedBassMultScale);
            engine_.setTrebleMultiply (kBakedTrebleMultScale);
        }
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
        // Note: lastBass_/lastTreble_ sentinels stay at -1.0f until a real
        // runtime override arrives — no neutral seeding needed.
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
        // Restore cached bass/treble only if a runtime override was actually
        // applied (sentinel -1.0 means "never overridden").
        if (lastBass_ >= 0.0f)
            setBassMultiply (lastBass_);
        if (lastTreble_ >= 0.0f)
        {
            engine_.setTrebleMultiply (lastTreble_);
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
        corrEqReady_ = true;

        // Reset wrapper-level EQ state without disturbing engine LFO/PRNG
        // (engine_.prepare() already cleared its audio buffers).
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

        // Notch filters disabled — 5-8kHz excess needs inner-loop damping fix
        notchB0_ = 1.0f; notchB1_ = 0.0f; notchB2_ = 0.0f;
        notchA1_ = 0.0f; notchA2_ = 0.0f;
        notch2B0_ = 1.0f; notch2B1_ = 0.0f; notch2B2_ = 0.0f;
        notch2A1_ = 0.0f; notch2A2_ = 0.0f;
        notchXl1_ = notchXl2_ = notchYl1_ = notchYl2_ = 0.0f;
        notchXr1_ = notchXr2_ = notchYr1_ = notchYr2_ = 0.0f;
        notch2Xl1_ = notch2Xl2_ = notch2Yl1_ = notch2Yl2_ = 0.0f;
        notch2Xr1_ = notch2Xr2_ = notch2Yr1_ = notch2Yr2_ = 0.0f;

        // Reset patch-local state so re-preparation doesn't carry stale
        // LFO / filter state across sample-rate changes or DAW resets.
        // add_late_tail_modulation state
        lateTailMod_lfoPhaseL      = 0.0f;
        lateTailMod_lfoPhaseR      = 1.5707963f;  // 90° offset for stereo
        lateTailMod_sampleSincePeak = 1000000;
        lateTailMod_runningPeak    = 0.0f;
        // add_multiband_eq filter state
        mbEq_0_xL1 = 0.0f; mbEq_0_xL2 = 0.0f; mbEq_0_yL1 = 0.0f; mbEq_0_yL2 = 0.0f;
        mbEq_0_xR1 = 0.0f; mbEq_0_xR2 = 0.0f; mbEq_0_yR1 = 0.0f; mbEq_0_yR2 = 0.0f;
        mbEq_1_xL1 = 0.0f; mbEq_1_xL2 = 0.0f; mbEq_1_yL1 = 0.0f; mbEq_1_yL2 = 0.0f;
        mbEq_1_xR1 = 0.0f; mbEq_1_xR2 = 0.0f; mbEq_1_yR1 = 0.0f; mbEq_1_yR2 = 0.0f;
        mbEq_2_xL1 = 0.0f; mbEq_2_xL2 = 0.0f; mbEq_2_yL1 = 0.0f; mbEq_2_yL2 = 0.0f;
        mbEq_2_xR1 = 0.0f; mbEq_2_xR2 = 0.0f; mbEq_2_yR1 = 0.0f; mbEq_2_yR2 = 0.0f;
        mbEq_3_xL1 = 0.0f; mbEq_3_xL2 = 0.0f; mbEq_3_yL1 = 0.0f; mbEq_3_yL2 = 0.0f;
        mbEq_3_xR1 = 0.0f; mbEq_3_xR2 = 0.0f; mbEq_3_yR1 = 0.0f; mbEq_3_yR2 = 0.0f;

        // Pre-compute multiband EQ coefficients at the host sample rate
        {
            constexpr float kQ = 1.5000f;
            const float sr = static_cast<float> (sampleRate_);
            constexpr float hz[4]     = { 100.0f, 160.0f, 630.0f, 1000.0f };
            constexpr float gainDb[4] = { 9.6131f, 9.1929f, -6.7715f, -6.2711f };
            for (int b = 0; b < 4; ++b)
            {
                const float A  = std::pow (10.0f, gainDb[b] / 40.0f);
                const float w0 = 6.283185307179586f * hz[b] / sr;
                const float cw = std::cos (w0);
                const float al = std::sin (w0) / (2.0f * kQ);
                const float a0 = 1.0f + al / A;
                mbEq_b0_[b] = (1.0f + al * A) / a0;
                mbEq_b1_[b] = (-2.0f * cw) / a0;
                mbEq_b2_[b] = (1.0f - al * A) / a0;
                mbEq_a1_[b] = (-2.0f * cw) / a0;
                mbEq_a2_[b] = (1.0f - al / A) / a0;
            }
        }
        onsetHfCutSamples_ = 0.0f;
        onsetHfCutEnv_ = 0.0f;
        onsetHfLpL_ = 0.0f;
        onsetHfLpR_ = 0.0f;
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

            // ---- Dual notch cuts at 5.5kHz + 7.5kHz to fix 5-8kHz spectral excess ----
            {
                float xLn = yL;
                yL = notchB0_ * xLn + notchB1_ * notchXl1_ + notchB2_ * notchXl2_
                   - notchA1_ * notchYl1_ - notchA2_ * notchYl2_;
                notchXl2_ = notchXl1_; notchXl1_ = xLn;
                notchYl2_ = notchYl1_; notchYl1_ = yL;

                float xRn = yR;
                yR = notchB0_ * xRn + notchB1_ * notchXr1_ + notchB2_ * notchXr2_
                   - notchA1_ * notchYr1_ - notchA2_ * notchYr2_;
                notchXr2_ = notchXr1_; notchXr1_ = xRn;
                notchYr2_ = notchYr1_; notchYr1_ = yR;

                // Second notch (7.5kHz)
                xLn = yL;
                yL = notch2B0_ * xLn + notch2B1_ * notch2Xl1_ + notch2B2_ * notch2Xl2_
                   - notch2A1_ * notch2Yl1_ - notch2A2_ * notch2Yl2_;
                notch2Xl2_ = notch2Xl1_; notch2Xl1_ = xLn;
                notch2Yl2_ = notch2Yl1_; notch2Yl1_ = yL;

                xRn = yR;
                yR = notch2B0_ * xRn + notch2B1_ * notch2Xr1_ + notch2B2_ * notch2Xr2_
                   - notch2A1_ * notch2Yr1_ - notch2A2_ * notch2Yr2_;
                notch2Xr2_ = notch2Xr1_; notch2Xr1_ = xRn;
                notch2Yr2_ = notch2Yr1_; notch2Yr1_ = yR;
            }

            // ---- Stage 3: stereo width ----
            float mid  = 0.5f * (yL + yR);
            float side = 0.5f * (yL - yR);
            outputL[i] = std::clamp (mid + side * kStereoWidth, -32.0f, 32.0f);
            outputR[i] = std::clamp (mid - side * kStereoWidth, -32.0f, 32.0f);
        }
        // PATCH_POINT_POST_ENGINE
        // <<< PATCH_BODY_BEGIN:add_late_tail_modulation >>>
        // Slow LFO mod on late tail — breaks modal ringing
        // Bypassed while frozen to preserve the infinite-hold tail intact.
        if (! frozen_)
        {
            constexpr float kRateHz = 0.7000f;
            constexpr float kDepth  = 0.2000f;
            constexpr int   kMaxSampleSince = 0x3FFFFFFF;  // saturate to avoid int overflow
            constexpr float kSafetyCeiling = 32.0f;
            const float inc = 6.283185307179586f * kRateHz
                            / static_cast<float> (sampleRate_);
            const int tailStart = static_cast<int> (0.25f * static_cast<float> (sampleRate_));
            for (int i = 0; i < numSamples; ++i)
            {
                float a = std::max (std::abs (outputL[i]), std::abs (outputR[i]));
                if (a > lateTailMod_runningPeak * 1.5f && a > 0.01f)
                {
                    lateTailMod_sampleSincePeak = 0;
                    lateTailMod_runningPeak = a;
                }
                else
                {
                    if (lateTailMod_sampleSincePeak < kMaxSampleSince)
                        ++lateTailMod_sampleSincePeak;
                    lateTailMod_runningPeak *= 0.99999f;
                }
                if (lateTailMod_sampleSincePeak > tailStart)
                {
                    float modL = 1.0f + kDepth * std::sin (lateTailMod_lfoPhaseL);
                    float modR = 1.0f + kDepth * std::sin (lateTailMod_lfoPhaseR);
                    float l = outputL[i] * modL;
                    float r = outputR[i] * modR;
                    outputL[i] = std::clamp (l, -kSafetyCeiling, kSafetyCeiling);
                    outputR[i] = std::clamp (r, -kSafetyCeiling, kSafetyCeiling);
                    lateTailMod_lfoPhaseL += inc;
                    lateTailMod_lfoPhaseR += inc;
                    if (lateTailMod_lfoPhaseL > 6.283185307179586f) lateTailMod_lfoPhaseL -= 6.283185307179586f;
                    if (lateTailMod_lfoPhaseR > 6.283185307179586f) lateTailMod_lfoPhaseR -= 6.283185307179586f;
                }
            }
        }
        // <<< PATCH_BODY_END:add_late_tail_modulation >>>

        // <<< PATCH_BODY_BEGIN:add_multiband_eq >>>
        // Multiband peaking EQ correction (coefficients precomputed in prepare())
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float xL = outputL[i];
                float xR = outputR[i];
                { float y = mbEq_b0_[0] * xL + mbEq_b1_[0] * mbEq_0_xL1 + mbEq_b2_[0] * mbEq_0_xL2 - mbEq_a1_[0] * mbEq_0_yL1 - mbEq_a2_[0] * mbEq_0_yL2;
                  mbEq_0_xL2 = mbEq_0_xL1; mbEq_0_xL1 = xL; mbEq_0_yL2 = mbEq_0_yL1; mbEq_0_yL1 = y; xL = y; }
                { float y = mbEq_b0_[0] * xR + mbEq_b1_[0] * mbEq_0_xR1 + mbEq_b2_[0] * mbEq_0_xR2 - mbEq_a1_[0] * mbEq_0_yR1 - mbEq_a2_[0] * mbEq_0_yR2;
                  mbEq_0_xR2 = mbEq_0_xR1; mbEq_0_xR1 = xR; mbEq_0_yR2 = mbEq_0_yR1; mbEq_0_yR1 = y; xR = y; }
                { float y = mbEq_b0_[1] * xL + mbEq_b1_[1] * mbEq_1_xL1 + mbEq_b2_[1] * mbEq_1_xL2 - mbEq_a1_[1] * mbEq_1_yL1 - mbEq_a2_[1] * mbEq_1_yL2;
                  mbEq_1_xL2 = mbEq_1_xL1; mbEq_1_xL1 = xL; mbEq_1_yL2 = mbEq_1_yL1; mbEq_1_yL1 = y; xL = y; }
                { float y = mbEq_b0_[1] * xR + mbEq_b1_[1] * mbEq_1_xR1 + mbEq_b2_[1] * mbEq_1_xR2 - mbEq_a1_[1] * mbEq_1_yR1 - mbEq_a2_[1] * mbEq_1_yR2;
                  mbEq_1_xR2 = mbEq_1_xR1; mbEq_1_xR1 = xR; mbEq_1_yR2 = mbEq_1_yR1; mbEq_1_yR1 = y; xR = y; }
                { float y = mbEq_b0_[2] * xL + mbEq_b1_[2] * mbEq_2_xL1 + mbEq_b2_[2] * mbEq_2_xL2 - mbEq_a1_[2] * mbEq_2_yL1 - mbEq_a2_[2] * mbEq_2_yL2;
                  mbEq_2_xL2 = mbEq_2_xL1; mbEq_2_xL1 = xL; mbEq_2_yL2 = mbEq_2_yL1; mbEq_2_yL1 = y; xL = y; }
                { float y = mbEq_b0_[2] * xR + mbEq_b1_[2] * mbEq_2_xR1 + mbEq_b2_[2] * mbEq_2_xR2 - mbEq_a1_[2] * mbEq_2_yR1 - mbEq_a2_[2] * mbEq_2_yR2;
                  mbEq_2_xR2 = mbEq_2_xR1; mbEq_2_xR1 = xR; mbEq_2_yR2 = mbEq_2_yR1; mbEq_2_yR1 = y; xR = y; }
                { float y = mbEq_b0_[3] * xL + mbEq_b1_[3] * mbEq_3_xL1 + mbEq_b2_[3] * mbEq_3_xL2 - mbEq_a1_[3] * mbEq_3_yL1 - mbEq_a2_[3] * mbEq_3_yL2;
                  mbEq_3_xL2 = mbEq_3_xL1; mbEq_3_xL1 = xL; mbEq_3_yL2 = mbEq_3_yL1; mbEq_3_yL1 = y; xL = y; }
                { float y = mbEq_b0_[3] * xR + mbEq_b1_[3] * mbEq_3_xR1 + mbEq_b2_[3] * mbEq_3_xR2 - mbEq_a1_[3] * mbEq_3_yR1 - mbEq_a2_[3] * mbEq_3_yR2;
                  mbEq_3_xR2 = mbEq_3_xR1; mbEq_3_xR1 = xR; mbEq_3_yR2 = mbEq_3_yR1; mbEq_3_yR1 = y; xR = y; }
                outputL[i] = xL;
                outputR[i] = xR;
            }
        }
        // <<< PATCH_BODY_END:add_multiband_eq >>>

        // Re-clamp after patch chain to enforce output ceiling
        for (int i = 0; i < numSamples; ++i)
        {
            outputL[i] = std::clamp (outputL[i], -32.0f, 32.0f);
            outputR[i] = std::clamp (outputR[i], -32.0f, 32.0f);
        }

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
        onsetHfCutSamples_ = 0.0f;
        onsetHfCutEnv_ = 0.0f;
        onsetHfLpL_ = 0.0f;
        onsetHfLpR_ = 0.0f;
        // PATCH_POINT_CLEAR
        // <<< PATCH_CLEAR_BEGIN:add_late_tail_modulation >>>
        lateTailMod_lfoPhaseL = 0.0f;
        lateTailMod_lfoPhaseR = 1.5707963f;
        lateTailMod_sampleSincePeak = 1000000;
        lateTailMod_runningPeak = 0.0f;
        // <<< PATCH_CLEAR_END:add_late_tail_modulation >>>

        // <<< PATCH_CLEAR_BEGIN:add_multiband_eq >>>
        mbEq_0_xL1 = mbEq_0_xL2 = mbEq_0_yL1 = mbEq_0_yL2 = 0.0f;
        mbEq_0_xR1 = mbEq_0_xR2 = mbEq_0_yR1 = mbEq_0_yR2 = 0.0f;
        mbEq_1_xL1 = mbEq_1_xL2 = mbEq_1_yL1 = mbEq_1_yL2 = 0.0f;
        mbEq_1_xR1 = mbEq_1_xR2 = mbEq_1_yR1 = mbEq_1_yR2 = 0.0f;
        mbEq_2_xL1 = mbEq_2_xL2 = mbEq_2_yL1 = mbEq_2_yL2 = 0.0f;
        mbEq_2_xR1 = mbEq_2_xR2 = mbEq_2_yR1 = mbEq_2_yR2 = 0.0f;
        mbEq_3_xL1 = mbEq_3_xL2 = mbEq_3_yL1 = mbEq_3_yL2 = 0.0f;
        mbEq_3_xR1 = mbEq_3_xR2 = mbEq_3_yR1 = mbEq_3_yR2 = 0.0f;
        // <<< PATCH_CLEAR_END:add_multiband_eq >>>

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
        // Pass-through: the old quadratic curve and kBakedTrebleMultScale*
        // interpolation were un-baked into the factory preset values, so
        // the user's knob position IS the actual engine multiplier.
        engine_.setTrebleMultiply (mult);
        lastTreble_ = mult;
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

    const char* getPresetName() const override { return "Exciting Snare Room"; }
    const char* getBaseEngineType() const override { return "DattorroTank"; }

    int getCorrEQBandCount() const override { return kCorrEqBandCount; }
    bool getCorrEQCoeffs (float* b0, float* b1, float* b2,
                           float* a1, float* a2, int maxBands) const override
    {
        // sampleRate_ defaults to 44.1/48 kHz, so it can't be used as a readiness
        // check. Use an explicit corrEqReady_ flag set at end of prepare().
        if (! corrEqReady_)
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
    ExcitingSnareRoomPresetEngine engine_;
    float lastTreble_ = -1.0f;  // Sentinel: -1.0 = no runtime override applied
    float lastBass_ = -1.0f;   // Sentinel: -1.0 = no runtime override applied
    float lastStructHFHz_ = 0.0f;
    float lastTerminalThresholdDb_ = 0.0f;
    float lastTerminalFactor_ = 1.0f;  // 1.0 = disabled
    bool  frozen_ = false;
    // Cached runtime overrides — replayed after prepare() to survive re-preparation.
    // Sentinel value -1.0 means "no override, use baked default".
    float overrideAirDamping_ = -1.0f;
    float overrideHighCrossover_ = -1.0f;
    float overrideCrossover_ = -1.0f;
    float overrideNoiseMod_ = -1.0f;
    float overrideLateGain_ = -1.0f;
    float overrideSizeRangeMin_ = -1.0f;
    float overrideSizeRangeMax_ = -1.0f;
    double sampleRate_ = 48000.0;
    bool   corrEqReady_ = false;
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
    // Onset HF cut state
    float onsetHfCutSamples_ = 0.0f;
    float onsetHfCutEnv_ = 0.0f;
    float onsetHfLpL_ = 0.0f;
    float onsetHfLpR_ = 0.0f;
    // PATCH_POINT_MEMBERS
    // <<< PATCH_MEMBERS_BEGIN:add_late_tail_modulation >>>
    float lateTailMod_lfoPhaseL = 0.0f;
    float lateTailMod_lfoPhaseR = 1.5707963f;
    int   lateTailMod_sampleSincePeak = 1000000;
    float lateTailMod_runningPeak = 0.0f;
    // <<< PATCH_MEMBERS_END:add_late_tail_modulation >>>

    // <<< PATCH_MEMBERS_BEGIN:add_multiband_eq >>>
    float mbEq_b0_[4] {}, mbEq_b1_[4] {}, mbEq_b2_[4] {};
    float mbEq_a1_[4] {}, mbEq_a2_[4] {};
    float mbEq_0_xL1 = 0.0f, mbEq_0_xL2 = 0.0f, mbEq_0_yL1 = 0.0f, mbEq_0_yL2 = 0.0f;
    float mbEq_0_xR1 = 0.0f, mbEq_0_xR2 = 0.0f, mbEq_0_yR1 = 0.0f, mbEq_0_yR2 = 0.0f;
    float mbEq_1_xL1 = 0.0f, mbEq_1_xL2 = 0.0f, mbEq_1_yL1 = 0.0f, mbEq_1_yL2 = 0.0f;
    float mbEq_1_xR1 = 0.0f, mbEq_1_xR2 = 0.0f, mbEq_1_yR1 = 0.0f, mbEq_1_yR2 = 0.0f;
    float mbEq_2_xL1 = 0.0f, mbEq_2_xL2 = 0.0f, mbEq_2_yL1 = 0.0f, mbEq_2_yL2 = 0.0f;
    float mbEq_2_xR1 = 0.0f, mbEq_2_xR2 = 0.0f, mbEq_2_yR1 = 0.0f, mbEq_2_yR2 = 0.0f;
    float mbEq_3_xL1 = 0.0f, mbEq_3_xL2 = 0.0f, mbEq_3_yL1 = 0.0f, mbEq_3_yL2 = 0.0f;
    float mbEq_3_xR1 = 0.0f, mbEq_3_xR2 = 0.0f, mbEq_3_yR1 = 0.0f, mbEq_3_yR2 = 0.0f;
    // <<< PATCH_MEMBERS_END:add_multiband_eq >>>

    // Dual peaking cuts spanning 5-8kHz — fixes spectral excess beyond corrective EQ cap
    float notchB0_ = 1.0f, notchB1_ = 0.0f, notchB2_ = 0.0f;
    float notchA1_ = 0.0f, notchA2_ = 0.0f;
    float notchXl1_ = 0.0f, notchXl2_ = 0.0f, notchYl1_ = 0.0f, notchYl2_ = 0.0f;
    float notchXr1_ = 0.0f, notchXr2_ = 0.0f, notchYr1_ = 0.0f, notchYr2_ = 0.0f;
    float notch2B0_ = 1.0f, notch2B1_ = 0.0f, notch2B2_ = 0.0f;
    float notch2A1_ = 0.0f, notch2A2_ = 0.0f;
    float notch2Xl1_ = 0.0f, notch2Xl2_ = 0.0f, notch2Yl1_ = 0.0f, notch2Yl2_ = 0.0f;
    float notch2Xr1_ = 0.0f, notch2Xr2_ = 0.0f, notch2Yr1_ = 0.0f, notch2Yr2_ = 0.0f;

};

} // anonymous namespace

std::unique_ptr<PresetEngineBase> createExcitingSnareRoomPreset()
{
    return std::unique_ptr<PresetEngineBase> (new ExcitingSnareRoomPresetWrapper());
}

// Self-register at static init time so DuskVerbEngine can look up this preset by name
static PresetEngineRegistrar gExcitingSnareRoomPresetRegistrar ("PresetExcitingSnareRoom", &createExcitingSnareRoomPreset);


// GENERATED FILE - do not edit by hand (use generate_preset_engines.py)
//
// Per-preset reverb engine for "Dark Vocal Room".
// Base engine: QuadTank
//
// This file contains a full private copy of the QuadTank DSP in an
// anonymous namespace, so modifications to this preset's DSP cannot affect
// any other preset. The class has been renamed to DarkVocalRoomPresetEngine to avoid
// ODR conflicts.

#include "DarkVocalRoomPreset.h"
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

    // Baked algorithm-config constants from kPresetDarkVocalRoom in AlgorithmConfig.h
    // at generation time. Editing these here has no effect on other presets.
    constexpr float kBakedLateGainScale      = 0.22f;
    constexpr float kBakedSizeRangeMin       = 1.00003f;
    constexpr float kBakedSizeRangeMax       = 3.00009f;
    constexpr float kBakedAirDampingScale    = 0.8f;
    constexpr float kBakedNoiseModDepth      = 8.0f;
    constexpr float kBakedTrebleMultScale    = 0.9f;
    constexpr float kBakedTrebleMultScaleMax = 1.5f;
    constexpr float kBakedBassMultScale      = 1.0f;  // un-baked from 0.97f — bass knob now shows real multiplier
    constexpr float kBakedModDepthScale      = 0.75f;
    constexpr float kBakedModRateScale       = 13.0f;
    constexpr float kBakedDecayTimeScale     = 1.0f;   // un-baked from 1.5f — factory decay now displays real RT60

    // -----------------------------------------------------------------
    // Per-preset VV-derived structural constants
    // (from vv_topology_baked.json — derived from VV IR analysis).
    // These set the engine to a per-preset target at prepare() time;
    // runtime setters layer relative scaling on top of them.
    // -----------------------------------------------------------------
    constexpr float kVvCrossoverHz       = 1000.0f;
    constexpr float kVvHighCrossoverHz   = 6000.0f;
    constexpr float kVvAirDampingScale   = 0.816898f;
    constexpr float        kVvDelayScale        = 1.0f;  // un-baked from 2.00006f — size range now absolute
    constexpr float kVvTiltLowDb         = 0.345703f;
    constexpr float kVvTiltLowHz         = 400.0f;
    constexpr float kVvTiltHighDb        = -2.5707f;
    constexpr float kVvTiltHighHz        = 5000.0f;
    constexpr float kVvStereoWidth       = 0.903975f;
    // Decay-time correction: multiplied into the user knob in setDecayTime()
    // to bring the engine's actual RT60 in line with VV's measured RT60.
    // Derived by render-then-measure (see derive_decay_scale.py).
    // 1.0 = no correction; values < 1 shorten the tail, > 1 lengthen it.
    constexpr float kVvDecayTimeScale    = 1.0f;   // un-baked from 0.910411f
    // Per-preset 12-band corrective peaking EQ (from vv_correction_eq.json).
    // Derived by rendering DV at factory defaults and computing the per-band
    // dB delta vs VV. Applied post-engine in process() to push DV's spectral
    // character toward VV's. Coefficients are computed from these constants
    // in prepare() at the host sample rate so the EQ is correct at any rate.
    // Max correction magnitude for this preset: 12.0 dB
    // -----------------------------------------------------------------
    constexpr int kCorrEqBandCount = 12;
    constexpr float kCorrEqHz[kCorrEqBandCount] = { 100.0f, 158.0f, 251.0f, 397.0f, 632.0f, 1000.0f, 1581.0f, 2510.0f, 3969.0f, 6325.0f, 9798.0f, 15492.0f };
    constexpr float kCorrEqDb[kCorrEqBandCount] = { -5.19055f, -1.31459f, -6.42272f, -0.299137f, -3.92964f, -1.61821f, -1.94008f, -1.83992f, -0.2004f, -2.03261f, -5.92863f, 0.650535f };
    constexpr float kCorrEqQ = 1.41f;  // moderate Q ≈ 1 octave bandwidth

// ==========================================================================
//  BEGIN PRIVATE COPY OF QuadTank (QuadTank.h)
// ==========================================================================

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
class DarkVocalRoomPresetEngine
{
public:
    DarkVocalRoomPresetEngine();

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
    void setDecayBoost (float boost);
    void setNoiseModDepth (float samples);
    void setStructuralHFDamping (float hz);
    void setTerminalDecay (float thresholdDB, float factor);
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

        ThreeBandDamping damping;

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

        // Per-tank terminal decay tracking
        float currentRMS = 0.0f;
        float peakRMS = 0.0f;
        bool terminalDecayActive = false;
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
    float highCrossoverFreq_ = 4000.0f;
    float airDampingScale_ = 0.70f;
    float modDepthSamples_ = 8.0f;
    float lastModDepthRaw_ = 0.5f;
    float modRateHz_ = 1.0f;
    float sizeParam_ = 0.5f;
    float sizeRangeMin_ = 0.5f;
    float sizeRangeMax_ = 1.5f;
    float sizeRangeAllocatedMax_ = 4.0f;
    float lateGainScale_ = 1.0f;
    bool frozen_ = false;
    bool prepared_ = false;

    float decayBoost_ = 1.0f;
    float structHFCoeff_ = 0.0f;
    float structHFState_[4] {};
    float terminalDecayThresholdDB_ = -40.0f;
    float terminalDecayFactor_ = 1.0f;
    float terminalLinearThreshold_ = 10000.0f;  // 10^(-(-40dB)*0.1) — power ratio for peak/current RMS
    float peakRMS_ = 0.0f;
    float currentRMS_ = 0.0f;
    bool terminalDecayActive_ = false;
    float rmsAlpha_ = 0.9995f;
    float peakDecayAlpha_ = 0.99999f;

    float decayDiff1_ = 0.70f;
    float decayDiff2_ = 0.50f;
    float densityDiffCoeff_ = 0.10f;  // Reduced from 0.25 to slow density buildup (100ms→~250ms)
    float noiseModDepth_ = 2.0f;
    float independentNoiseModDepth_ = -1.0f;  // -1 = use modDepth-coupled value

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();

    float readOutputTap (const OutputTap& tap) const;
};

// ==========================================================================
//  BEGIN PRIVATE COPY OF QuadTank IMPLEMENTATION (QuadTank.cpp)
// ==========================================================================

// -----------------------------------------------------------------------
// DelayLine helpers (same as DattorroTank)

void DarkVocalRoomPresetEngine::DelayLine::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void DarkVocalRoomPresetEngine::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float DarkVocalRoomPresetEngine::DelayLine::readInterpolated (float delaySamples) const
{
    float readPos = static_cast<float> (writePos) - delaySamples;
    int intIdx = static_cast<int> (std::floor (readPos));
    float frac = readPos - static_cast<float> (intIdx);
    return DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
}

// -----------------------------------------------------------------------
void DarkVocalRoomPresetEngine::Allpass::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void DarkVocalRoomPresetEngine::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// -----------------------------------------------------------------------
DarkVocalRoomPresetEngine::DarkVocalRoomPresetEngine()
{
    for (int t = 0; t < kNumTanks; ++t)
    {
        tanks_[t].ap1BaseDelay     = kTankConfigs[t].ap1Base;
        tanks_[t].delay1BaseDelay  = kTankConfigs[t].del1Base;
        tanks_[t].ap2BaseDelay     = kTankConfigs[t].ap2Base;
        tanks_[t].delay2BaseDelay  = kTankConfigs[t].del2Base;
        for (int i = 0; i < kNumDensityAPs; ++i)
            tanks_[t].densityAPBase[i] = kTankConfigs[t].densityAPBase[i];
    }
}

// -----------------------------------------------------------------------
void DarkVocalRoomPresetEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    // Preserve headroom for post-prepare size-range overrides.
    sizeRangeAllocatedMax_ = std::max (sizeRangeAllocatedMax_, sizeRangeMax_);
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
    const int maxModExcursion = static_cast<int> (std::ceil (32.0 * sampleRate / kBaseSampleRate));

    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];

        int ap1Max = static_cast<int> (std::ceil (tank.ap1BaseDelay * rateRatio * sizeRangeAllocatedMax_)) + maxModExcursion;
        int del1Max = static_cast<int> (std::ceil (tank.delay1BaseDelay * rateRatio * sizeRangeAllocatedMax_)) + maxModExcursion;
        int ap2Max = static_cast<int> (std::ceil (tank.ap2BaseDelay * rateRatio * sizeRangeAllocatedMax_)) + maxModExcursion;
        int del2Max = static_cast<int> (std::ceil (tank.delay2BaseDelay * rateRatio * sizeRangeAllocatedMax_)) + maxModExcursion;

        tank.ap1Buffer.allocate (ap1Max);
        tank.delay1.allocate (del1Max);
        tank.ap2.allocate (ap2Max);
        tank.delay2.allocate (del2Max + maxModExcursion);

        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            int dapMax = static_cast<int> (std::ceil (tank.densityAPBase[i] * rateRatio * sizeRangeAllocatedMax_)) + 4;
            tank.densityAP[i].allocate (dapMax);
        }

        tank.damping.reset();
        tank.crossFeedState = 0.0f;
        structHFState_[t] = 0.0f;
    }

    // Initialize LFO and PRNG with unique seeds per tank (90° phase offsets)
    static constexpr uint32_t kLFOSeeds[kNumTanks]   = { 0x12345678u, 0x87654321u, 0xABCDEF01u, 0x13579BDFu };
    static constexpr uint32_t kNoiseSeeds[kNumTanks]  = { 0xDEADBEEFu, 0xCAFEBABEu, 0xFEEDFACEu, 0xBAADF00Du };
    static constexpr float    kPhaseOffsets[kNumTanks] = { 0.0f, 1.5707963f, 3.1415927f, 4.7123890f };

    for (int t = 0; t < kNumTanks; ++t)
    {
        tanks_[t].lfoPhase  = kPhaseOffsets[t];
        tanks_[t].lfoPRNG   = kLFOSeeds[t];
        tanks_[t].noiseState = kNoiseSeeds[t];
    }

    prepared_ = true;

    // Sample-rate-invariant terminal decay smoothing coefficients
    {
        constexpr float kRmsTauMs = 45.0f;
        constexpr float kPeakTauMs = 2270.0f;
        float sr = static_cast<float> (sampleRate_);
        rmsAlpha_ = std::exp (-1000.0f / (kRmsTauMs * sr));
        peakDecayAlpha_ = std::exp (-1000.0f / (kPeakTauMs * sr));
    }

    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();
    setModDepth (lastModDepthRaw_);

    // Clear all per-tank stateful fields (RMS history, terminal-decay flags,
    // structural HF damping state). Without this, a host re-prepare would
    // start with empty delay buffers but retain the previous run's tracker
    // state, leaking session state across reconfigure.
    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];
        tank.currentRMS = 0.0f;
        tank.peakRMS = 0.0f;
        tank.terminalDecayActive = false;
    }
}

// -----------------------------------------------------------------------
void DarkVocalRoomPresetEngine::process (const float* inputL, const float* inputR,
                        float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    const float effectiveNoiseMod = (independentNoiseModDepth_ >= 0.0f)
                                  ? independentNoiseModDepth_ : noiseModDepth_;

    for (int i = 0; i < numSamples; ++i)
    {
        float input = (inputL[i] + inputR[i]) * 0.5f;
        if (frozen_)
            input = 0.0f;

        // Save all cross-feed states before processing
        float cf[kNumTanks];
        for (int t = 0; t < kNumTanks; ++t)
            cf[t] = tanks_[t].crossFeedState;

        // Process each tank with circular cross-coupling (0←3, 1←0, 2←1, 3←2)
        for (int t = 0; t < kNumTanks; ++t)
        {
            auto& tank = tanks_[t];
            float otherCrossFeed = cf[(t + kNumTanks - 1) % kNumTanks];
            float tankIn = input + otherCrossFeed;

            // --- Modulated allpass (decay diffusion 1) ---
            float mod = std::sin (tank.lfoPhase) * modDepthSamples_;
            float ap1ReadDelay = tank.ap1DelaySamples + mod;
            ap1ReadDelay = std::max (ap1ReadDelay, 1.0f);

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

            // --- Delay 1 with noise jitter ---
            float jitter1 = frozen_ ? 0.0f : nextDrift (tank.noiseState) * effectiveNoiseMod;
            float del1Read = tank.delay1Samples + jitter1;
            del1Read = std::max (del1Read, 1.0f);
            float del1Out = tank.delay1.readInterpolated (del1Read);
            tank.delay1.write (ap1Out);

            // --- Density cascade: 3 allpasses ---
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
                structHFState_[t] = (1.0f - structHFCoeff_) * damped + structHFCoeff_ * structHFState_[t];
                damped = structHFState_[t];
            }

            // --- Static allpass (decay diffusion 2) ---
            float coeff2 = frozen_ ? 0.0f : decayDiff2_;
            float ap2Out = tank.ap2.process (damped, coeff2);

            // --- Delay 2 with noise jitter ---
            float jitter2 = frozen_ ? 0.0f : nextDrift (tank.noiseState) * effectiveNoiseMod;
            float del2Read = tank.delay2Samples + jitter2;
            del2Read = std::max (del2Read, 1.0f);
            float del2Out = tank.delay2.readInterpolated (del2Read);
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

            // Cross-feed: feeds next tank
            tank.crossFeedState = del2Out;
        }

        // ------------------------------------------------------------------
        // Output: sum 14 signed taps from all 4 tanks per channel
        float outL = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outL += readOutputTap (kLeftOutputTaps[t]) * kLeftOutputTaps[t].sign;

        float outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outR += readOutputTap (kRightOutputTaps[t]) * kRightOutputTaps[t].sign;

        // Normalize output tap sum. With alternating signs on highly correlated
        // taps (same tank, adjacent delay positions), effective independence is
        // ~8 taps, not 48 — 1/sqrt(48) severely over-attenuates.
        // 0.35 ≈ 1/sqrt(8) compensates for sign cancellation and the slow
        // energy buildup of long-loop tanks vs shorter-loop FDN/DattorroTank.
        constexpr float kOutputScale = 0.35f;  // ~1/sqrt(8)
        const float outputGain = kOutputScale * lateGainScale_;

        outputL[i] = std::clamp (outL * outputGain, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (outR * outputGain, -kSafetyClip, kSafetyClip);
    }
}

// -----------------------------------------------------------------------
float DarkVocalRoomPresetEngine::readOutputTap (const OutputTap& tap) const
{
    // Buffer index mapping:
    //   0-3:  Delay1 from tanks 0-3
    //   4-7:  Delay2 from tanks 0-3
    //   8-11: AP2 from tanks 0-3
    int tankIdx = tap.bufferIndex % kNumTanks;
    int bufType = tap.bufferIndex / kNumTanks;  // 0=del1, 1=del2, 2=ap2

    const auto& tank = tanks_[tankIdx];

    if (bufType == 2)
    {
        // Read from AP2 internal buffer at fractional position
        const auto& ap = tank.ap2;
        int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
        tapOffset = std::max (tapOffset, 1);
        return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
    }

    const DelayLine* delayBuf = (bufType == 0) ? &tank.delay1 : &tank.delay2;
    float totalDelay = (bufType == 0) ? tank.delay1Samples : tank.delay2Samples;

    float tapDelay = tap.positionFrac * totalDelay;
    tapDelay = std::max (tapDelay, 1.0f);
    return delayBuf->readInterpolated (tapDelay);
}

// -----------------------------------------------------------------------
void DarkVocalRoomPresetEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (seconds, 0.1f);
    if (prepared_) updateDecayCoefficients();
}

void DarkVocalRoomPresetEngine::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (mult, 0.1f);
    if (prepared_) updateDecayCoefficients();
}

void DarkVocalRoomPresetEngine::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (mult, 0.1f);
    if (prepared_) updateDecayCoefficients();
}

void DarkVocalRoomPresetEngine::setCrossoverFreq (float hz)
{
    const float maxLow = std::max (20.0f, highCrossoverFreq_ - 1.0f);
    crossoverFreq_ = std::clamp (hz, 20.0f, maxLow);
    if (prepared_) updateDecayCoefficients();
}

void DarkVocalRoomPresetEngine::setModDepth (float depth)
{
    lastModDepthRaw_ = std::clamp (depth, 0.0f, 2.0f);
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = lastModDepthRaw_ * 16.0f * rateRatio;
    noiseModDepth_ = lastModDepthRaw_ * 12.0f * rateRatio;  // Match DattorroTank (12 samples peak)
}

void DarkVocalRoomPresetEngine::setNoiseModDepth (float samples)
{
    // Independent noise jitter, decoupled from LFO modDepth. When set (>= 0),
    // this overrides the modDepth-coupled noise jitter. Mirrors DattorroTank.
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float maxJitter = 32.0f * rateRatio;
    independentNoiseModDepth_ = std::clamp (samples * rateRatio, -1.0f, maxJitter);
}

void DarkVocalRoomPresetEngine::setModRate (float hz)
{
    modRateHz_ = hz;
    if (prepared_) updateLFORates();
}

void DarkVocalRoomPresetEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DarkVocalRoomPresetEngine::setFreeze (bool frozen)
{
    frozen_ = frozen;
    if (frozen)
        for (int t = 0; t < kNumTanks; ++t)
        {
            tanks_[t].currentRMS = 0.0f;
            tanks_[t].peakRMS = 0.0f;
            tanks_[t].terminalDecayActive = false;
        }
}
void DarkVocalRoomPresetEngine::setLateGainScale (float scale) { lateGainScale_ = std::max (scale, 0.0f); }

void DarkVocalRoomPresetEngine::setHighCrossoverFreq (float hz)
{
    const float minHigh = std::max (100.0f, crossoverFreq_ + 1.0f);
    highCrossoverFreq_ = std::max (hz, minHigh);
    if (prepared_)
        updateDecayCoefficients();
}

void DarkVocalRoomPresetEngine::setAirDampingScale (float scale)
{
    airDampingScale_ = std::max (scale, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DarkVocalRoomPresetEngine::setSizeRange (float min, float max)
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

void DarkVocalRoomPresetEngine::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DarkVocalRoomPresetEngine::setStructuralHFDamping (float hz)
{
    if (hz <= 0.0f)
    {
        structHFCoeff_ = 0.0f;
        for (int t = 0; t < kNumTanks; ++t)
            structHFState_[t] = 0.0f;
        return;
    }
    const float effectiveHz = hz * (0.5f + 0.5f * trebleMultiply_);
    structHFCoeff_ = std::exp (-kTwoPi * effectiveHz / static_cast<float> (sampleRate_));
}

void DarkVocalRoomPresetEngine::setTerminalDecay (float thresholdDB, float factor)
{
    terminalDecayThresholdDB_ = -std::abs (thresholdDB);
    // Accept the full [0.0, 1.0] range to match DattorroTank and the
    // wrapper's pass-through behavior. PluginProcessor already gates the
    // override path on factor>0.001, so an inadvertent factor=0 from the
    // automation layer cannot reach here in normal use; but if the
    // calibrator or a future API caller wants a value below 0.8, it
    // should be honored exactly rather than silently rounded up.
    terminalDecayFactor_ = std::clamp (factor, 0.0f, 1.0f);
    terminalLinearThreshold_ = std::pow (10.0f, -terminalDecayThresholdDB_ * 0.1f);
}

void DarkVocalRoomPresetEngine::clearBuffers()
{
    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];
        tank.ap1Buffer.clear();
        tank.delay1.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].clear();
        tank.ap2.clear();
        tank.delay2.clear();
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
        structHFState_[t] = 0.0f;
        tank.currentRMS = 0.0f;
        tank.peakRMS = 0.0f;
        tank.terminalDecayActive = false;
    }
    for (int t = 0; t < kNumTanks; ++t)
    {
        // Restore the same quadrature LFO offsets and PRNG seeds used in prepare()
        // so modulation decorrelation is preserved after clear.
        static constexpr float    kPhaseOffsets[] = { 0.0f, 1.5707963f, 3.1415927f, 4.7123890f };
        static constexpr uint32_t kLFOSeeds[]     = { 0x12345678u, 0x87654321u, 0xABCDEF01u, 0x13579BDFu };
        static constexpr uint32_t kNoiseSeeds[]   = { 0xDEADBEEFu, 0xCAFEBABEu, 0xFEEDFACEu, 0xBAADF00Du };
        tanks_[t].lfoPhase  = kPhaseOffsets[t];
        tanks_[t].lfoPRNG   = kLFOSeeds[t];
        tanks_[t].noiseState = kNoiseSeeds[t];
    }
}

// -----------------------------------------------------------------------
void DarkVocalRoomPresetEngine::updateDelayLengths()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;

    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];
        tank.ap1DelaySamples = static_cast<float> (tank.ap1BaseDelay) * rateRatio * sizeScale;
        tank.delay1Samples   = static_cast<float> (tank.delay1BaseDelay) * rateRatio * sizeScale;
        tank.delay2Samples   = static_cast<float> (tank.delay2BaseDelay) * rateRatio * sizeScale;

        tank.ap2.delaySamples = std::max (1, static_cast<int> (
            static_cast<float> (tank.ap2BaseDelay) * rateRatio * sizeScale));

        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].delaySamples = std::max (1, static_cast<int> (
                static_cast<float> (tank.densityAPBase[i]) * rateRatio * sizeScale));
    }
}

void DarkVocalRoomPresetEngine::updateDecayCoefficients()
{
    float sr = static_cast<float> (sampleRate_);
    float lowCrossoverCoeff = std::exp (-kTwoPi * crossoverFreq_ / sr);
    float highCrossoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / sr);

    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];

        float loopLength = tank.ap1DelaySamples
                         + tank.delay1Samples
                         + static_cast<float> (tank.ap2.delaySamples)
                         + tank.delay2Samples;
        for (int i = 0; i < kNumDensityAPs; ++i)
            loopLength += static_cast<float> (tank.densityAP[i].delaySamples);

        float gBase = std::pow (10.0f, -3.0f * loopLength / (decayTime_ * sr));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);
        float gLow  = std::clamp (std::pow (gBase, 1.0f / bassMultiply_), 0.001f, 0.9999f);
        float gMid  = gBase;  // mid band decays at natural rate
        float gHigh = std::clamp (std::pow (gBase, 1.0f / (trebleMultiply_ * airDampingScale_)), 0.001f, 0.9999f);

        tank.damping.setCoefficients (gLow, gMid, gHigh, lowCrossoverCoeff, highCrossoverCoeff);
    }
}

void DarkVocalRoomPresetEngine::updateLFORates()
{
    float sr = static_cast<float> (sampleRate_);
    // 4 asymmetric rates using irrational multipliers to prevent correlation
    static constexpr float kRateMultipliers[kNumTanks] = {
        1.0f, 1.1180339887f, 0.8944271910f, 1.2360679775f  // 1, √5/2, 2/√5, (1+√5)/2/φ
    };
    for (int t = 0; t < kNumTanks; ++t)
        tanks_[t].lfoPhaseInc = kTwoPi * modRateHz_ * kRateMultipliers[t] / sr;
}

// ==========================================================================
//  END PRIVATE COPY
// ==========================================================================


class DarkVocalRoomPresetWrapper : public PresetEngineBase
{
public:
    DarkVocalRoomPresetWrapper() = default;
    ~DarkVocalRoomPresetWrapper() override = default;

    void prepare (double sampleRate, int maxBlockSize) override
    {
        sampleRate_ = sampleRate;
        // Apply sizing/scaling config BEFORE engine_.prepare() so buffers
        // allocate at the right size for the actual host sample rate AND for
        // the per-preset delay scale (Invariant 3).
                if (overrideSizeRangeMin_ >= 0.0f)
            engine_.setSizeRange (overrideSizeRangeMin_ * kVvDelayScale, overrideSizeRangeMax_ * kVvDelayScale);
        else
            engine_.setSizeRange (kBakedSizeRangeMin * kVvDelayScale, kBakedSizeRangeMax * kVvDelayScale);
        // (no setDelayScale on this engine)
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
        if (savedTreble != kBakedTrebleMultScale && savedTreble != -1.0f)
        {
            engine_.setTrebleMultiply (savedTreble);
            lastTreble_ = savedTreble;
        }
        if (savedBass != 1.0f)
            setBassMultiply (savedBass);
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

    void setStructuralHFDamping (float hz) override
    {
        lastStructHFHz_ = hz;
        engine_.setStructuralHFDamping (hz);
    }

    void setSizeRange (float mn, float mx) override
    {
        overrideSizeRangeMin_ = mn;
        overrideSizeRangeMax_ = mx;
        engine_.setSizeRange (mn * kVvDelayScale, mx * kVvDelayScale);
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
        engine_.setSizeRange (kBakedSizeRangeMin * kVvDelayScale, kBakedSizeRangeMax * kVvDelayScale);
    }

    const char* getPresetName() const override { return "Dark Vocal Room"; }
    const char* getBaseEngineType() const override { return "QuadTank"; }

    // Fix 4 (spectral): expose corrective EQ coefficients to DuskVerbEngine
    // so it can apply the same EQ to the ER path.
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
    DarkVocalRoomPresetEngine engine_;
    float lastTreble_ = -1.0f;
    float lastBass_ = 1.0f;
    float lastStructHFHz_ = 0.0f;
    float lastTerminalThresholdDb_ = 0.0f;
    float lastTerminalFactor_ = 1.0f;  // 1.0 = disabled
    bool  frozen_ = false;
    double sampleRate_ = 48000.0;
    bool   corrEqReady_ = false;
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

std::unique_ptr<PresetEngineBase> createDarkVocalRoomPreset()
{
    return std::unique_ptr<PresetEngineBase> (new DarkVocalRoomPresetWrapper());
}

// Self-register at static init time so DuskVerbEngine can look up this preset by name
static PresetEngineRegistrar gDarkVocalRoomPresetRegistrar ("PresetDarkVocalRoom", &createDarkVocalRoomPreset);


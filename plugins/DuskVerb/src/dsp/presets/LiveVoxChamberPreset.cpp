// GENERATED FILE - do not edit by hand (use generate_preset_engines.py)
//
// Per-preset reverb engine for "Live Vox Chamber".
// Base engine: FDNReverb
//
// This file contains a full private copy of the FDNReverb DSP in an
// anonymous namespace, so modifications to this preset's DSP cannot affect
// any other preset. The class has been renamed to LiveVoxChamberPresetEngine to avoid
// ODR conflicts.

#include "LiveVoxChamberPreset.h"
#include "PresetEngineRegistry.h"

#include "../TwoBandDamping.h"

#include <random>
#include <vector>

#include "../AlgorithmConfig.h"
#include "../DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>


namespace {

    // Baked algorithm-config constants from kPresetLiveVoxChamber in AlgorithmConfig.h
    // at generation time. Editing these here has no effect on other presets.
    constexpr float kBakedLateGainScale      = 0.22f;
    constexpr float kBakedSizeRangeMin       = 0.5f;
    constexpr float kBakedSizeRangeMax       = 1.5f;
    constexpr float kBakedHighCrossoverHz    = 4000.0f;
    constexpr float kBakedAirDampingScale    = 0.8f;
    constexpr float kBakedNoiseModDepth      = 8.0f;
    constexpr float kBakedTrebleMultScale    = 0.91f;
    constexpr float kBakedTrebleMultScaleMax = 1.5f;
    constexpr float kBakedBassMultScale      = 1.15f;
    constexpr float kBakedModDepthScale      = 0.75f;
    constexpr float kBakedModRateScale       = 13.0f;
    constexpr float kBakedDecayTimeScale     = 1.5f;

    // -----------------------------------------------------------------
    // Per-preset VV-derived structural constants
    // (from vv_topology_baked.json — derived from VV IR analysis).
    // These set the engine to a per-preset target at prepare() time;
    // runtime setters layer relative scaling on top of them.
    // -----------------------------------------------------------------
    constexpr float kVvBassMultiply      = 1.10622f;
    constexpr float kVvTrebleMultiply    = 0.896938f;
    constexpr float kVvCrossoverHz       = 1000.0f;
    constexpr float kVvHighCrossoverHz   = 6000.0f;
    constexpr float kVvAirDampingScale   = 0.864365f;
    constexpr float kVvDelayScale        = 0.572084f;
    constexpr float kVvTiltLowDb         = 0.379854f;
    constexpr float kVvTiltLowHz         = 400.0f;
    constexpr float kVvTiltHighDb        = -2.52144f;
    constexpr float kVvTiltHighHz        = 5000.0f;
    constexpr float kVvStereoWidth       = 0.878386f;
    // Decay-time correction: multiplied into the user knob in setDecayTime()
    // to bring the engine's actual RT60 in line with VV's measured RT60.
    // Derived by render-then-measure (see derive_decay_scale.py).
    // 1.0 = no correction; values < 1 shorten the tail, > 1 lengthen it.
    constexpr float kVvDecayTimeScale    = 0.781378f;

    // -----------------------------------------------------------------
    // Per-preset 12-band corrective peaking EQ (from vv_correction_eq.json).
    // Derived by rendering DV at factory defaults and computing the per-band
    // dB delta vs VV. Applied post-engine in process() to push DV's spectral
    // character toward VV's. Coefficients are computed from these constants
    // in prepare() at the host sample rate so the EQ is correct at any rate.
    // Max correction magnitude for this preset: 6.90 dB
    // -----------------------------------------------------------------
    constexpr int kCorrEqBandCount = 12;
    constexpr float kCorrEqHz[kCorrEqBandCount] = { 100.0f, 158.0f, 251.0f, 397.0f, 632.0f, 1000.0f, 1581.0f, 2510.0f, 3969.0f, 6325.0f, 9798.0f, 15492.0f };
    constexpr float kCorrEqDb[kCorrEqBandCount] = { 6.33905f, 6.90367f, 0.336162f, 0.631056f, -0.983247f, -0.332358f, -0.0352936f, -0.303093f, 0.248572f, 1.26401f, 1.76964f, -0.635501f };
    constexpr float kCorrEqQ = 1.41f;  // moderate Q ≈ 1 octave bandwidth

    // -----------------------------------------------------------------
    // Per-preset onset envelope table (from VV reference audio).
    // Applied to FDN input in DuskVerbEngine to match VV's serial tank
    // energy buildup. Table is time-normalized 0→1 gain curve.
    // Duration: 300ms, 64 points.
    // -----------------------------------------------------------------
    constexpr float kOnsetDurationMs = 300.0f;
    constexpr int kOnsetTableSize = 64;
    constexpr float kOnsetEnvelope[kOnsetTableSize] = {
        1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.7742f, 0.4901f,
        0.3616f, 0.3222f, 0.3224f, 0.3501f, 0.2889f, 0.2902f, 0.2847f, 0.3506f,
        0.3582f, 0.4119f, 0.4384f, 0.4845f, 0.4691f, 0.4748f, 0.4687f, 0.4806f,
        0.5775f, 0.6050f, 0.6237f, 0.5927f, 0.6577f, 0.7775f, 0.8486f, 0.9068f,
        0.7828f, 0.7029f, 0.7253f, 0.8132f, 0.8401f, 0.7183f, 0.6985f, 0.7587f,
        0.8196f, 0.8049f, 0.7780f, 0.8155f, 0.8469f, 0.9499f, 0.8466f, 0.7774f,
        0.6832f, 0.6938f, 0.7246f, 0.8796f, 0.8917f, 0.9685f, 0.9497f, 0.9941f,
        0.9525f, 0.9254f, 0.9347f, 0.9005f, 0.9409f, 0.8531f, 0.8068f, 1.0000f
    };

// ==========================================================================
//  BEGIN PRIVATE COPY OF FDNReverb (FDNReverb.h)
// ==========================================================================

// Multi-point output tap: reads from a fractional position within a delay line.
// Inspired by Dattorro's 7-tap output topology — reading from delay interiors
// instead of just endpoints produces naturally denser, smoother tails.
struct FDNOutputTap
{
    int channelIndex;      // 0-15: which FDN delay line
    float positionFrac;    // 0.0-1.0: fractional position within delay (1.0 = full length)
    float sign;            // ±1.0 for stereo decorrelation
};

class LiveVoxChamberPresetEngine
{
public:
    LiveVoxChamberPresetEngine();

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

    void setBaseDelays (const int* delays);
    void setOutputTaps (const int* lt, const int* rt,
                        const float* ls, const float* rs);
    void setLateGainScale (float scale);
    void setSizeRange (float min, float max);
    void setInlineDiffusion (float coeff);
    void setUseShortInlineAP (bool use);
    void setMultiPointOutput (const FDNOutputTap* left, int numL,
                              const FDNOutputTap* right, int numR);
    void setMultiPointDensity (int tapsPerChannel);  // Generate taps dynamically
    void setModDepthFloor (float floor);
    void setNoiseModDepth (float samples);
    void setHadamardPerturbation (float amount);
    void setUseHouseholder (bool enable);
    void setUseWeightedGains (bool enable);
    void setHighCrossoverFreq (float hz);
    void setAirTrebleMultiply (float mult);
    void setStructuralHFDamping (float baseFreqHz, float trebleMultiply);
    void setStructuralLFDamping (float hz);
    void setDualSlope (float ratio, int fastCount, float fastGain);
    void setStereoCoupling (float amount);
    void setFeedbackModDepth (float depth);
    void setCrossoverModDepth (float depth);
    void setDecayBoost (float boost);
    void setTerminalDecay (float thresholdDB, float factor);
    void clearBuffers();

private:
    static constexpr int N = 16;
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr float kOutputLevel = 1.121f;     // 1/sqrt(8) * 2.0 * 1.585 — consolidated output scaling
    static constexpr float kSafetyClip  = 32.0f;     // Soft-clip ceiling — raised for dual-slope (high fast-tap gain)
    static constexpr int kNumOutputTaps = 8;

    // Worst-case base delay across all algorithms (for buffer allocation)
    static constexpr int kMaxBaseDelay = 5521;

    // Mutable delay and tap configuration (initialized to Hall defaults)
    int baseDelays_[N];
    int leftTaps_[8];
    int rightTaps_[8];
    float leftSigns_[8];
    float rightSigns_[8];

    // Multi-point output tapping (Dattorro-inspired)
    static constexpr int kMaxMultiTaps = 256;
    FDNOutputTap multiTapsL_[kMaxMultiTaps] {};
    FDNOutputTap multiTapsR_[kMaxMultiTaps] {};
    int numMultiTapsL_ = 0;
    int numMultiTapsR_ = 0;
    bool useMultiPointOutput_ = false;

    float lateGainScale_ = 1.0f;
    float sizeCompensation_ = 1.0f; // sqrt(sizeScale) — normalizes output level across sizes
    float sizeRangeMin_ = 0.5f;
    float sizeRangeMax_ = 1.5f;

    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;
    };

    // Non-modulated Schroeder allpass for inline FDN feedback diffusion.
    // Increases echo density per feedback cycle (Dattorro "decay diffusion").
    struct InlineAllpass
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;

        float process (float input, float g)
        {
            float vd = buffer[static_cast<size_t> (writePos)];
            float vn = input + g * vd;
            buffer[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }

        void clear()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }
    };

    // 16 prime delay lengths for inline allpasses (at 44.1kHz base rate).
    // All prime and coprime to the main delay lengths to avoid modal alignment.
    static constexpr int kInlineAPDelays[N] = {
        41, 47, 53, 59, 67, 71, 79, 83,
        89, 97, 101, 107, 109, 113, 127, 131
    };

    // Second cascade: longer primes for additional density multiplication.
    // Two cascaded allpasses give ~4x echo density per feedback cycle (vs ~2x with one).
    static constexpr int kInlineAPDelays2[N] = {
        151, 157, 163, 167, 173, 179, 181, 191,
        193, 197, 199, 211, 223, 227, 229, 233
    };

    // Third cascade: even longer primes for maximum density multiplication.
    // Three cascaded allpasses give ~8x echo density per feedback cycle.
    static constexpr int kInlineAPDelays3[N] = {
        251, 257, 263, 269, 271, 277, 281, 283,
        293, 307, 311, 313, 317, 331, 337, 347
    };

    // Short inline allpass delays (7-47 samples at 44.1kHz) for Hall.
    // Much shorter = nearly flat group delay → avoids spectral centroid shift.
    // Combined with multi-point output tapping for maximum density.
    static constexpr int kInlineAPDelaysShort[N] = {
        7, 11, 13, 17, 19, 23, 29, 31,
        37, 41, 43, 47, 7, 11, 13, 17
    };

    DelayLine delayLines_[N];
    InlineAllpass inlineAP_[N];
    InlineAllpass inlineAP2_[N];
    InlineAllpass inlineAP3_[N];
    InlineAllpass inlineAPShort_[N];
    bool useShortInlineAP_ = false;
    float inlineDiffCoeff_ = 0.0f;
    float inlineDiffCoeff2_ = 0.0f;
    float inlineDiffCoeff3_ = 0.0f;
    ThreeBandDamping dampFilter_[N];
    float lfoPhase_[N] {};
    float lfoPhaseInc_[N] {};
    uint32_t lfoPRNG_[N] {};   // Per-channel xorshift32 state for LFO drift
    float delayLength_[N] {};
    float inputGainScale_[N] {};  // Per-channel input gain: 1/sqrt(delay_length/min_delay) for uniform modal excitation
    float outputGainScale_[N] {}; // Per-channel output gain: same weighting for spectral flatness
    bool useWeightedGains_ = false; // Enable delay-weighted input/output gains
    float modDepthScale_[N] {}; // Per-delay mod scaling (proportional to delay length)
    float modDepthFloor_ = 0.35f; // Minimum mod depth scaling (per-algorithm)
    float noiseModDepthParam_ = 0.0f; // Raw noise mod depth (unscaled)
    float noiseModDepth_ = 0.0f;      // Per-sample random delay jitter (samples, scaled by rate)

    // Structural HF damping: gentle first-order LP modeling air absorption.
    // Per-algorithm, applied after TwoBandDamping in feedback loop.
    // Effective frequency scales with treble_multiply: lower treble → lower cutoff → more damping.
    float structHFState_[N] {};
    float structHFCoeff_ = 0.0f;
    float structHFBaseFreq_ = 0.0f;  // Stored for re-computation when treble changes
    bool structHFEnabled_ = false;

    // Structural LF damping: first-order highpass in feedback loop.
    // Reduces bass RT60 inflation (Room mode). Applied after structural HF damping.
    float structLFState_[N] {};
    float structLFCoeff_ = 0.0f;   // exp(-2π·f/sr), 0 = bypassed
    bool structLFEnabled_ = false;

    // Anti-alias LP: gentle first-order LP at ~17kHz inside feedback loop.
    // Accumulates across iterations to suppress modulation-induced aliasing
    // without killing air on the first pass (unlike the 6th-order output LP).
    float antiAliasState_[N] {};
    float antiAliasCoeff_ = 0.0f;  // exp(-2*pi*17000/sr), 0 = bypassed

    // Per-channel DC blocker (first-order highpass, ~5Hz).
    // Prevents DC accumulation inside the FDN feedback loop from
    // denormal bias and allpass filter drift.
    float dcX1_[N] {};   // Previous input
    float dcY1_[N] {};   // Previous output
    float dcCoeff_ = 0.9993f;  // R = 1 - 2*pi*5/sr

    // Cheap xorshift32 PRNG returning float in [-1, +1].
    // Used for aperiodic LFO drift ("Wander").
    static float nextDrift (uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
    }

    // Perturbed feedback mixing matrix (optional replacement for Hadamard).
    // Small random offsets break deterministic mode coupling that causes ringing.
    // Projected to nearest orthogonal matrix via polar decomposition for energy conservation.
    float perturbMatrix_[N][N] {};
    bool usePerturbedMatrix_ = false;
    bool useHouseholder_ = false;

    // Stereo split: channels 0-7 = L group, 8-15 = R group.
    // Two independent 8×8 Hadamards with controlled cross-coupling between groups.
    // coupling=0 → fully independent L/R (widest), coupling=0.5 → fully mixed (mono).
    float stereoCoupling_ = 0.0f;
    bool stereoSplitEnabled_ = false;

    // Dual-slope decay: channels [0, dualSlopeFastCount_) get shorter RT60
    // and boosted output tap gain to create double-slope decay (loud fast + quiet slow).
    float dualSlopeRatio_ = 0.0f;     // Fast RT60 as fraction of effective RT60 (0 = disabled)
    int   dualSlopeFastCount_ = 0;    // Number of fast-decay channels
    float outputTapGain_[N] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    double sampleRate_ = 44100.0;
    float decayTime_ = 1.0f;
    float bassMultiply_ = 1.0f;
    float trebleMultiply_ = 0.5f;
    float airTrebleMultiply_ = 1.0f;  // Independent air band damping (above highCrossoverFreq)
    float crossoverFreq_ = 1000.0f;
    float highCrossoverFreq_ = 20000.0f;
    float modDepth_ = 0.5f;
    float modRateHz_ = 1.0f;
    float modDepthSamples_ = 2.0f;
    float sizeParam_ = 1.0f;
    float feedbackModDepth_ = 0.0f;
    float crossoverModDepth_ = 0.0f;
    float baseLowCrossoverCoeff_ = 0.0f;
    float baseHighCrossoverCoeff_ = 0.0f;
    float decayBoost_ = 1.0f;
    float terminalDecayThresholdDB_ = -40.0f;
    float terminalDecayFactor_ = 1.0f;
    float peakRMS_ = 0.0f;
    float currentRMS_ = 0.0f;
    bool terminalDecayActive_ = false;
    bool frozen_ = false;
    bool prepared_ = false;

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();
    void updateModDepth();
};

// ==========================================================================
//  BEGIN PRIVATE COPY OF FDNReverb IMPLEMENTATION (FDNReverb.cpp)
// ==========================================================================

namespace {

// In-place fast Walsh-Hadamard transform for N=16, O(N log N).
// Normalization (1/sqrt(N) = 0.25) is folded into the final butterfly
// stage to eliminate a separate scaling pass.
void hadamardInPlace16 (float* data)
{
    constexpr int n = 16;
    constexpr int kLog2N = 4;

    for (int stage = 0; stage < kLog2N - 1; ++stage)
    {
        int len = 1 << stage;
        for (int i = 0; i < n; i += 2 * len)
        {
            for (int j = 0; j < len; ++j)
            {
                float a = data[i + j];
                float b = data[i + j + len];
                data[i + j]       = a + b;
                data[i + j + len] = a - b;
            }
        }
    }

    // Final stage with normalization folded in: 1/sqrt(16) = 0.25
    constexpr float kNorm = 0.25f;
    constexpr int lastLen = 1 << (kLog2N - 1); // 8
    for (int i = 0; i < n; i += 2 * lastLen)
    {
        for (int j = 0; j < lastLen; ++j)
        {
            float a = data[i + j];
            float b = data[i + j + lastLen];
            data[i + j]            = (a + b) * kNorm;
            data[i + j + lastLen]  = (a - b) * kNorm;
        }
    }
}

// In-place fast Walsh-Hadamard transform for N=8.
// Used in dual-slope mode: two independent 8×8 Hadamards decouple
// fast-decay and slow-decay channel groups so each maintains its own RT60.
// Normalization: 1/sqrt(8) ≈ 0.353553.
void hadamardInPlace8 (float* data)
{
    constexpr int n = 8;
    constexpr int kLog2N = 3;

    for (int stage = 0; stage < kLog2N - 1; ++stage)
    {
        int len = 1 << stage;
        for (int i = 0; i < n; i += 2 * len)
        {
            for (int j = 0; j < len; ++j)
            {
                float a = data[i + j];
                float b = data[i + j + len];
                data[i + j]       = a + b;
                data[i + j + len] = a - b;
            }
        }
    }

    // Final stage with normalization: 1/sqrt(8)
    constexpr float kNorm = 0.353553f;
    constexpr int lastLen = 1 << (kLog2N - 1); // 4
    for (int i = 0; i < n; i += 2 * lastLen)
    {
        for (int j = 0; j < lastLen; ++j)
        {
            float a = data[i + j];
            float b = data[i + j + lastLen];
            data[i + j]            = (a + b) * kNorm;
            data[i + j + lastLen]  = (a - b) * kNorm;
        }
    }
}

// In-place Householder reflection for N=16: H = I - (2/N) * ones * ones^T.
// Provides moderate inter-channel mixing (each output = input - mean),
// avoiding the eigentone clustering that maximum-mixing Hadamard causes.
// O(N) complexity: one sum + N subtracts. Energy-preserving (unitary matrix).
void householderInPlace16 (float* data)
{
    constexpr int n = 16;
    constexpr float kScale = 2.0f / static_cast<float> (n); // 0.125

    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += data[i];

    sum *= kScale;

    for (int i = 0; i < n; ++i)
        data[i] -= sum;
}

// In-place Householder reflection for N=8 (stereo split mode).
void householderInPlace8 (float* data)
{
    constexpr int n = 8;
    constexpr float kScale = 2.0f / static_cast<float> (n); // 0.25

    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += data[i];

    sum *= kScale;

    for (int i = 0; i < n; ++i)
        data[i] -= sum;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
LiveVoxChamberPresetEngine::LiveVoxChamberPresetEngine()
{
    // Initialize mutable config arrays from Hall defaults
    std::memcpy (baseDelays_, kHall.delayLengths, sizeof (baseDelays_));
    std::memcpy (leftTaps_,   kHall.leftTaps,     sizeof (leftTaps_));
    std::memcpy (rightTaps_,  kHall.rightTaps,    sizeof (rightTaps_));
    std::memcpy (leftSigns_,  kHall.leftSigns,    sizeof (leftSigns_));
    std::memcpy (rightSigns_, kHall.rightSigns,   sizeof (rightSigns_));
}

// ---------------------------------------------------------------------------
void LiveVoxChamberPresetEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    updateDelayLengths();

    // Allocate buffers for worst-case delay across ALL algorithms.
    // kMaxBaseDelay covers the longest line in any algorithm config.
    float maxSizeScale = std::max (sizeRangeMax_, 1.5f);
    float maxDelay = static_cast<float> (kMaxBaseDelay)
                   * static_cast<float> (sampleRate / kBaseSampleRate) * maxSizeScale;

    // +12 covers max modulation depth (modDepth 2.0 -> 8 samples) + cubic interp (2) + safety (2)
    int bufSize = DspUtils::nextPowerOf2 (static_cast<int> (std::ceil (maxDelay)) + 12);

    for (int i = 0; i < N; ++i)
    {
        delayLines_[i].buffer.assign (static_cast<size_t> (bufSize), 0.0f);
        delayLines_[i].writePos = 0;
        delayLines_[i].mask = bufSize - 1;
        dampFilter_[i].reset();
        structHFState_[i] = 0.0f;
        structLFState_[i] = 0.0f;
        antiAliasState_[i] = 0.0f;
        dcX1_[i] = 0.0f;
        dcY1_[i] = 0.0f;
    }
    peakRMS_ = 0.0f;
    currentRMS_ = 0.0f;
    terminalDecayActive_ = false;

    // Inline allpass diffusers: prime delays scaled by sample rate
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP_[i].writePos = 0;
        inlineAP_[i].mask = apBufSize - 1;
    }

    // Second inline allpass cascade: longer primes for density multiplication
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays2[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP2_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP2_[i].writePos = 0;
        inlineAP2_[i].mask = apBufSize - 1;
    }

    // Third inline allpass cascade: even longer primes for ~8x density per cycle
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelays3[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAP3_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAP3_[i].writePos = 0;
        inlineAP3_[i].mask = apBufSize - 1;
    }

    // Short inline allpass cascade for Hall
    for (int i = 0; i < N; ++i)
    {
        int apDelay = static_cast<int> (std::ceil (
            static_cast<float> (kInlineAPDelaysShort[i]) * rateRatio));
        int apBufSize = DspUtils::nextPowerOf2 (apDelay + 1);
        inlineAPShort_[i].buffer.assign (static_cast<size_t> (apBufSize), 0.0f);
        inlineAPShort_[i].writePos = 0;
        inlineAPShort_[i].mask = apBufSize - 1;
    }

    // Anti-alias LP coefficient: ~17kHz at any sample rate
    // At 44.1kHz: -3dB at 17kHz, -0.3dB at 12kHz (preserves air)
    // After 50 iterations: -15dB at 17kHz (kills aliasing), -15dB at 20kHz
    float antiAliasHz = std::min (17000.0f, static_cast<float> (sampleRate) * 0.45f);
    antiAliasCoeff_ = std::exp (-kTwoPi * antiAliasHz / static_cast<float> (sampleRate));
    std::memset (antiAliasState_, 0, sizeof (antiAliasState_));

    // DC blocker coefficient and state reset
    dcCoeff_ = 1.0f - (kTwoPi * 5.0f / static_cast<float> (sampleRate));
    std::memset (dcX1_, 0, sizeof (dcX1_));
    std::memset (dcY1_, 0, sizeof (dcY1_));

    // Randomize LFO starting phases so successive IR captures see different
    // modulation state (like VV's stochastic modulation). The evenly-spaced
    // offsets between channels are preserved; only the base phase is random.
    {
        std::random_device rd;
        float basePhase = std::uniform_real_distribution<float> (0.0f, kTwoPi) (rd);
        for (int i = 0; i < N; ++i)
            lfoPhase_[i] = std::fmod (basePhase + kTwoPi * static_cast<float> (i)
                                                         / static_cast<float> (N), kTwoPi);

        // Random PRNG seeds so drift pattern varies per prepare() call
        uint32_t baseSeed = rd();
        for (int i = 0; i < N; ++i)
            lfoPRNG_[i] = baseSeed + static_cast<uint32_t> (i * 1847);
    }

    updateModDepth();
    updateLFORates();
    updateDecayCoefficients();

    prepared_ = true;
}

// ---------------------------------------------------------------------------
void LiveVoxChamberPresetEngine::process (const float* inputL, const float* inputR,
                         float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = inputL[i];
        const float inR = inputR[i];

        // --- 1) Read from all delay lines with LFO-modulated fractional position ---
        float delayOut[N];
        for (int ch = 0; ch < N; ++ch)
        {
            auto& dl = delayLines_[ch];

            float mod = std::sin (lfoPhase_[ch]) * modDepthSamples_ * modDepthScale_[ch];
            // Per-sample random jitter: fast mode blurring complementing the slow LFO.
            // Each channel gets independent noise from its xorshift32 PRNG.
            float jitter = nextDrift (lfoPRNG_[ch]) * noiseModDepth_ * modDepthScale_[ch];
            float readDelay = delayLength_[ch] + mod + jitter;
            float readPos = static_cast<float> (dl.writePos) - readDelay;

            int intIdx = static_cast<int> (std::floor (readPos));
            float frac = readPos - static_cast<float> (intIdx);

            delayOut[ch] = DspUtils::cubicHermite (dl.buffer.data(), dl.mask, intIdx, frac);

            // Advance LFO with "Wander" drift (classic reverb technique).
            // Adds ±8% random perturbation to the phase increment each sample
            // so the modulation never exactly repeats — organic, not mechanical.
            float drift = nextDrift (lfoPRNG_[ch]) * lfoPhaseInc_[ch] * 0.08f;
            lfoPhase_[ch] += lfoPhaseInc_[ch] + drift;
            if (lfoPhase_[ch] >= kTwoPi)
                lfoPhase_[ch] -= kTwoPi;
            else if (lfoPhase_[ch] < 0.0f)
                lfoPhase_[ch] += kTwoPi;
        }

        // --- 1.25) Terminal decay: accelerate tail fadeout when below threshold ---
        if (terminalDecayFactor_ < 1.0f && ! frozen_)
        {
            float sampleEnergy = 0.0f;
            for (int ch = 0; ch < N; ++ch)
                sampleEnergy += delayOut[ch] * delayOut[ch];
            sampleEnergy *= (1.0f / static_cast<float> (N));

            currentRMS_ = currentRMS_ * 0.9995f + sampleEnergy * 0.0005f;
            if (currentRMS_ > peakRMS_) peakRMS_ = currentRMS_;
            else peakRMS_ *= 0.99999f;

            float rmsDB = 10.0f * std::log10 (std::max (currentRMS_, 1e-20f));
            float peakDB = 10.0f * std::log10 (std::max (peakRMS_, 1e-20f));
            terminalDecayActive_ = (peakDB - rmsDB > -terminalDecayThresholdDB_)
                                 && (peakRMS_ > 1e-12f);
            if (terminalDecayActive_)
            {
                for (int ch = 0; ch < N; ++ch)
                    delayOut[ch] *= terminalDecayFactor_;
            }
        }

        // --- 1.5) Inline allpass diffusion (Dattorro "decay diffusion") ---
        // Two cascaded Schroeder allpasses per channel. The second stage uses
        // a reduced coefficient (0.8x) to prevent over-diffusion/washiness
        // while multiplying echo density ~4x per feedback cycle.
        // Bypassed during freeze: allpasses alter loop magnitude response over time,
        // causing the frozen tail to spectrally evolve rather than truly sustaining.
        if (inlineDiffCoeff_ > 0.0f && ! frozen_)
        {
            if (useShortInlineAP_)
            {
                for (int ch = 0; ch < N; ++ch)
                    delayOut[ch] = inlineAPShort_[ch].process (delayOut[ch], inlineDiffCoeff_);
            }
            else
            {
                for (int ch = 0; ch < N; ++ch)
                    delayOut[ch] = inlineAP_[ch].process (delayOut[ch], inlineDiffCoeff_);
            }
        }
        if (! useShortInlineAP_ && inlineDiffCoeff2_ > 0.0f && ! frozen_)
        {
            for (int ch = 0; ch < N; ++ch)
                delayOut[ch] = inlineAP2_[ch].process (delayOut[ch], inlineDiffCoeff2_);
        }
        if (inlineDiffCoeff3_ > 0.0f && ! frozen_)
        {
            for (int ch = 0; ch < N; ++ch)
                delayOut[ch] = inlineAP3_[ch].process (delayOut[ch], inlineDiffCoeff3_);
        }

        // --- 2) Feedback mixing ---
        float feedback[N];
        if (useHouseholder_)
        {
            // Householder reflection: H = I - (2/N) * ones * ones^T.
            // Moderate mixing avoids eigentone clustering (better for plates).
            std::memcpy (feedback, delayOut, sizeof (feedback));
            if (stereoSplitEnabled_ && dualSlopeFastCount_ == 0)
            {
                householderInPlace8 (feedback);
                householderInPlace8 (feedback + 8);
                if (stereoCoupling_ > 0.0f)
                {
                    float sinC = stereoCoupling_;
                    float cosC = std::sqrt (1.0f - sinC * sinC);
                    for (int ch = 0; ch < 8; ++ch)
                    {
                        float l = feedback[ch];
                        float r = feedback[ch + 8];
                        feedback[ch]     =  l * cosC + r * sinC;
                        feedback[ch + 8] = -l * sinC + r * cosC;
                    }
                }
            }
            else if (dualSlopeFastCount_ == 8)
            {
                householderInPlace8 (feedback);
                householderInPlace8 (feedback + 8);
            }
            else
            {
                householderInPlace16 (feedback);
            }
        }
        else if (stereoSplitEnabled_ && ! usePerturbedMatrix_ && dualSlopeFastCount_ == 0)
        {
            // Stereo split: two independent 8×8 Hadamards for L (0-7) and R (8-15) groups.
            // Each group maintains its own spatial identity with controlled cross-coupling.
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace8 (feedback);      // L group: channels 0-7
            hadamardInPlace8 (feedback + 8);  // R group: channels 8-15

            // Rotation-based cross-coupling: orthogonal matrix guarantees
            // |L'|²+|R'|² = |L|²+|R|² regardless of signal correlation.
            // stereoCoupling_ = sin(θ), so L→R leakage = 20·log10(c) dB.
            if (stereoCoupling_ > 0.0f)
            {
                float sinC = stereoCoupling_;
                float cosC = std::sqrt (1.0f - sinC * sinC);
                for (int ch = 0; ch < 8; ++ch)
                {
                    float l = feedback[ch];
                    float r = feedback[ch + 8];
                    feedback[ch]     =  l * cosC + r * sinC;
                    feedback[ch + 8] = -l * sinC + r * cosC;
                }
            }
        }
        else if (usePerturbedMatrix_)
        {
            // Perturbed matrix: N×N multiply breaks deterministic modal coupling.
            // 256 multiply-adds per sample (vs 64 for fast Hadamard) — negligible overhead.
            for (int row = 0; row < N; ++row)
            {
                float sum = 0.0f;
                for (int col = 0; col < N; ++col)
                    sum += perturbMatrix_[row][col] * delayOut[col];
                feedback[row] = sum;
            }
        }
        else if (dualSlopeFastCount_ == 8)
        {
            // Dual-slope: two independent 8×8 Hadamards.
            // Decouples fast-decay (0-7) and slow-decay (8-15) channel groups
            // so each maintains its own RT60 without cross-contamination.
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace8 (feedback);
            hadamardInPlace8 (feedback + 8);
        }
        else
        {
            std::memcpy (feedback, delayOut, sizeof (feedback));
            hadamardInPlace16 (feedback);
        }

        // --- 3) Three-band damping + input injection -> write to delay lines ---
        for (int ch = 0; ch < N; ++ch)
        {
            auto& dl = delayLines_[ch];

            // When frozen, bypass damping (unity feedback) to sustain tail indefinitely
            float filtered = frozen_ ? feedback[ch] : dampFilter_[ch].process (feedback[ch]);

            // Structural HF damping: gentle air-absorption LP (per-algorithm)
            if (structHFEnabled_ && ! frozen_)
            {
                structHFState_[ch] = (1.0f - structHFCoeff_) * filtered
                                   + structHFCoeff_ * structHFState_[ch];
                filtered = structHFState_[ch];
            }

            // Structural LF damping: first-order highpass to reduce bass inflation (Room)
            if (structLFEnabled_ && ! frozen_)
            {
                float hp = filtered - structLFState_[ch];
                structLFState_[ch] = (1.0f - structLFCoeff_) * filtered
                                   + structLFCoeff_ * structLFState_[ch];
                filtered = hp;
            }

            // Anti-alias LP + DC blocker: bypass when frozen to prevent tail drift
            if (! frozen_)
            {
                antiAliasState_[ch] = (1.0f - antiAliasCoeff_) * filtered
                                    + antiAliasCoeff_ * antiAliasState_[ch];
                filtered = antiAliasState_[ch];

                float dcOut = filtered - dcX1_[ch] + dcCoeff_ * dcY1_[ch];
                dcX1_[ch] = filtered;
                dcY1_[ch] = dcOut;
                filtered = dcOut;
            }

            // Inject stereo input into delay lines.
            // When stereo split is active: channels 0-7 get L, 8-15 get R (group-based).
            // When disabled (legacy): even channels get L, odd channels get R (interleaved).
            // When frozen, mute new input to keep only the existing tail.
            float inputGain = frozen_ ? 0.0f : 0.25f * inputGainScale_[ch];
            float polarity = (ch & 1) ? -1.0f : 1.0f;
            float inputSample = stereoSplitEnabled_ ? ((ch < 8) ? inL : inR)
                                                    : ((ch & 1) ? inR : inL);
            // Suppress denormal bias when frozen to prevent tail mutation
            float denormalBias = frozen_ ? 0.0f
                                         : (((dl.writePos ^ ch) & 1)
                                                ? DspUtils::kDenormalPrevention
                                                : -DspUtils::kDenormalPrevention);
            dl.buffer[static_cast<size_t> (dl.writePos)] =
                filtered + inputSample * polarity * inputGain + denormalBias;

            dl.writePos = (dl.writePos + 1) & dl.mask;
        }

        // --- 4) Tap decorrelated stereo outputs ---
        float outL = 0.0f, outR = 0.0f;

        if (useMultiPointOutput_)
        {
            // Multi-point output: read from fractional positions within delay lines.
            // Each tap reads at positionFrac * delayLength, producing multiple virtual
            // output paths from the same delay structure — Dattorro-inspired density.
            // Standard multi-point fractional tap reading
            for (int t = 0; t < numMultiTapsL_; ++t)
            {
                const auto& tap = multiTapsL_[t];
                const auto& dl  = delayLines_[tap.channelIndex];
                float readDelay = delayLength_[tap.channelIndex] * tap.positionFrac;
                int   iDelay    = static_cast<int> (readDelay);
                float frac      = readDelay - static_cast<float> (iDelay);
                int   i0        = (dl.writePos - 1 - iDelay) & dl.mask;
                int   i1        = (i0 - 1) & dl.mask;
                float sample    = dl.buffer[static_cast<size_t> (i0)]
                                + frac * (dl.buffer[static_cast<size_t> (i1)]
                                        - dl.buffer[static_cast<size_t> (i0)]);
                outL += sample * tap.sign;
            }
            for (int t = 0; t < numMultiTapsR_; ++t)
            {
                const auto& tap = multiTapsR_[t];
                const auto& dl  = delayLines_[tap.channelIndex];
                float readDelay = delayLength_[tap.channelIndex] * tap.positionFrac;
                int   iDelay    = static_cast<int> (readDelay);
                float frac      = readDelay - static_cast<float> (iDelay);
                int   i0        = (dl.writePos - 1 - iDelay) & dl.mask;
                int   i1        = (i0 - 1) & dl.mask;
                float sample    = dl.buffer[static_cast<size_t> (i0)]
                                + frac * (dl.buffer[static_cast<size_t> (i1)]
                                        - dl.buffer[static_cast<size_t> (i0)]);
                outR += sample * tap.sign;
            }
            // Normalize by 1/N — arithmetic mean of tap reads.
            outL /= static_cast<float> (numMultiTapsL_);
            outR /= static_cast<float> (numMultiTapsR_);
        }
        else
        {
            // Standard 8-tap endpoint output with signed summation.
            // Per-channel outputTapGain_ enables dual-slope: fast channels are boosted
            // to create a loud initial burst, slow channels at unity form the quiet tail.
            for (int t = 0; t < kNumOutputTaps; ++t)
            {
                outL += delayOut[leftTaps_[t]] * leftSigns_[t] * outputTapGain_[leftTaps_[t]] * outputGainScale_[leftTaps_[t]];
                outR += delayOut[rightTaps_[t]] * rightSigns_[t] * outputTapGain_[rightTaps_[t]] * outputGainScale_[rightTaps_[t]];
            }
        }

        // Linear output: 1/sqrt(8) normalization + 6dB level match.
        // sizeCompensation_ = sqrt(sizeScale) normalizes steady-state energy
        // across sizes — shorter delays at small sizes pack more recirculations
        // per unit time, producing higher energy density without compensation.
        // Soft-clip output: fastTanh knee at ~±1.0, scaled by kSafetyClip for headroom.
        // Replaces hard clamp — smoother limiting prevents harsh artifacts on overloads.
        float rawL = outL * kOutputLevel * sizeCompensation_ * lateGainScale_;
        float rawR = outR * kOutputLevel * sizeCompensation_ * lateGainScale_;
        outputL[i] = DspUtils::fastTanh (rawL / kSafetyClip) * kSafetyClip;
        outputR[i] = DspUtils::fastTanh (rawR / kSafetyClip) * kSafetyClip;
    }
}

// ---------------------------------------------------------------------------
void LiveVoxChamberPresetEngine::setDecayTime (float seconds)
{
    decayTime_ = std::clamp (seconds, 0.2f, 600.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void LiveVoxChamberPresetEngine::setBassMultiply (float mult)
{
    bassMultiply_ = std::clamp (mult, 0.5f, 2.5f);
    if (prepared_)
        updateDecayCoefficients();
}

void LiveVoxChamberPresetEngine::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::clamp (mult, 0.1f, 1.5f);
    if (prepared_)
        updateDecayCoefficients();
}

void LiveVoxChamberPresetEngine::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::clamp (hz, 200.0f, 4000.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void LiveVoxChamberPresetEngine::setModDepth (float depth)
{
    modDepth_ = std::clamp (depth, 0.0f, 2.0f);
    if (prepared_)
        updateModDepth();
}

void LiveVoxChamberPresetEngine::setModRate (float hz)
{
    modRateHz_ = std::max (hz, 0.01f);
    if (prepared_)
        updateLFORates();
}

void LiveVoxChamberPresetEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void LiveVoxChamberPresetEngine::setFreeze (bool frozen)
{
    frozen_ = frozen;
    if (frozen)
    {
        for (int i = 0; i < N; ++i)
        {
            structHFState_[i] = 0.0f;
            structLFState_[i] = 0.0f;
        }
    }
}

void LiveVoxChamberPresetEngine::setBaseDelays (const int* delays)
{
    if (delays == nullptr)
        return;

    for (int i = 0; i < N; ++i)
        baseDelays_[i] = std::clamp (delays[i], 1, kMaxBaseDelay);

    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void LiveVoxChamberPresetEngine::setOutputTaps (const int* lt, const int* rt,
                                const float* ls, const float* rs)
{
    if (lt == nullptr || rt == nullptr || ls == nullptr || rs == nullptr)
        return;

    for (int i = 0; i < kNumOutputTaps; ++i)
    {
        leftTaps_[i]  = std::clamp (lt[i], 0, N - 1);
        rightTaps_[i] = std::clamp (rt[i], 0, N - 1);
    }
    std::memcpy (leftSigns_,  ls, sizeof (leftSigns_));
    std::memcpy (rightSigns_, rs, sizeof (rightSigns_));
}

void LiveVoxChamberPresetEngine::setLateGainScale (float scale)
{
    lateGainScale_ = std::max (scale, 0.0f);
}

void LiveVoxChamberPresetEngine::setSizeRange (float min, float max)
{
    sizeRangeMin_ = std::max (min, 0.0f);
    float newMax = std::max (max, sizeRangeMin_);
    if (prepared_)
        newMax = std::min (newMax, sizeRangeMax_);
    sizeRangeMax_ = newMax;
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void LiveVoxChamberPresetEngine::setInlineDiffusion (float coeff)
{
    inlineDiffCoeff_ = std::clamp (coeff, 0.0f, 0.75f);
    inlineDiffCoeff2_ = inlineDiffCoeff_ * 0.8f;
    // 3rd cascade disabled: extra feedback-loop phase accumulation worsens
    // ringing for longer-delay modes (Hall, Plate). Density is instead
    // improved via 3-stage output diffusion (post-FDN, no feedback impact).
    inlineDiffCoeff3_ = 0.0f;
}

void LiveVoxChamberPresetEngine::setUseShortInlineAP (bool use)
{
    useShortInlineAP_ = use;
}

void LiveVoxChamberPresetEngine::setMultiPointOutput (const FDNOutputTap* left, int numL,
                                     const FDNOutputTap* right, int numR)
{
    if (left == nullptr || numL <= 0 || right == nullptr || numR <= 0)
    {
        useMultiPointOutput_ = false;
        numMultiTapsL_ = 0;
        numMultiTapsR_ = 0;
        return;
    }
    numMultiTapsL_ = std::min (numL, static_cast<int> (kMaxMultiTaps));
    numMultiTapsR_ = std::min (numR, static_cast<int> (kMaxMultiTaps));
    for (int i = 0; i < numMultiTapsL_; ++i)
    {
        if (left[i].channelIndex < 0 || left[i].channelIndex >= N)
        {
            useMultiPointOutput_ = false;
            numMultiTapsL_ = 0;
            numMultiTapsR_ = 0;
            return;
        }
        multiTapsL_[i] = left[i];
    }
    for (int i = 0; i < numMultiTapsR_; ++i)
    {
        if (right[i].channelIndex < 0 || right[i].channelIndex >= N)
        {
            useMultiPointOutput_ = false;
            numMultiTapsL_ = 0;
            numMultiTapsR_ = 0;
            return;
        }
        multiTapsR_[i] = right[i];
    }
    useMultiPointOutput_ = true;
}

void LiveVoxChamberPresetEngine::setMultiPointDensity (int tapsPerChannel)
{
    int totalTaps = tapsPerChannel * N;
    if (totalTaps <= 0 || totalTaps > kMaxMultiTaps)
    {
        useMultiPointOutput_ = false;
        numMultiTapsL_ = 0;
        numMultiTapsR_ = 0;
        return;
    }

    numMultiTapsL_ = totalTaps;
    numMultiTapsR_ = totalTaps;

    // Generate evenly-spaced fractional positions with alternating signs.
    // L and R use offset positions for stereo decorrelation.
    for (int ch = 0; ch < N; ++ch)
    {
        for (int t = 0; t < tapsPerChannel; ++t)
        {
            int idx = ch * tapsPerChannel + t;
            float posL = 0.05f + 0.90f * (static_cast<float> (t) + 0.5f)
                                        / static_cast<float> (tapsPerChannel);
            float posR = 0.05f + 0.90f * (static_cast<float> (t) + 0.3f)
                                        / static_cast<float> (tapsPerChannel);
            float signL = (t % 2 == 0) ? 1.0f : -1.0f;
            float signR = (t % 2 == 1) ? 1.0f : -1.0f;  // opposite pattern

            multiTapsL_[idx] = { ch, posL, signL };
            multiTapsR_[idx] = { ch, posR, signR };
        }
    }
    useMultiPointOutput_ = true;
}

void LiveVoxChamberPresetEngine::setHadamardPerturbation (float amount)
{
    if (amount <= 0.0f)
    {
        usePerturbedMatrix_ = false;
        return;
    }

    usePerturbedMatrix_ = true;

    // Build 16x16 Hadamard matrix (Sylvester construction)
    float H[N][N];
    H[0][0] = 1.0f;
    for (int size = 1; size < N; size *= 2)
    {
        for (int i = 0; i < size; ++i)
        {
            for (int j = 0; j < size; ++j)
            {
                H[i][j + size]        =  H[i][j];
                H[i + size][j]        =  H[i][j];
                H[i + size][j + size] = -H[i][j];
            }
        }
    }

    // Apply deterministic perturbations (seeded PRNG for reproducibility)
    constexpr float kNorm = 0.25f; // 1/sqrt(16)
    uint32_t seed = 0xDEADBEEF;
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < N; ++j)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            float noise = static_cast<float> (static_cast<int32_t> (seed))
                        * (1.0f / 2147483648.0f);
            perturbMatrix_[i][j] = (H[i][j] + noise * amount) * kNorm;
        }
    }

    // Project to nearest orthogonal matrix via iterative polar decomposition
    // (Newton's method): M_{k+1} = 0.5 * (M_k + M_k^{-T}).
    // This guarantees a unitary mixing matrix, preventing energy drift in
    // freeze mode. Row normalization alone doesn't ensure column orthogonality.
    // For a 16x16 nearly-orthogonal matrix, 4 iterations suffice for convergence.
    constexpr int kPolarIters = 4;

    for (int iter = 0; iter < kPolarIters; ++iter)
    {
        // Copy current matrix M
        float M[N][N];
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                M[i][j] = perturbMatrix_[i][j];

        // Compute M^{-1} via Gauss-Jordan elimination on [M | I]
        float aug[N][2 * N];
        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                aug[i][j] = M[i][j];
                aug[i][j + N] = (i == j) ? 1.0f : 0.0f;
            }
        }

        for (int col = 0; col < N; ++col)
        {
            // Partial pivoting for numerical stability
            int pivotRow = col;
            float pivotVal = std::fabs (aug[col][col]);
            for (int row = col + 1; row < N; ++row)
            {
                float val = std::fabs (aug[row][col]);
                if (val > pivotVal)
                {
                    pivotVal = val;
                    pivotRow = row;
                }
            }
            if (pivotRow != col)
            {
                for (int k = 0; k < 2 * N; ++k)
                    std::swap (aug[col][k], aug[pivotRow][k]);
            }

            float diag = aug[col][col];
            if (std::fabs (diag) < 1e-12f)
                break; // Singular — bail out (shouldn't happen for perturbed Hadamard)

            float invDiag = 1.0f / diag;
            for (int k = 0; k < 2 * N; ++k)
                aug[col][k] *= invDiag;

            for (int row = 0; row < N; ++row)
            {
                if (row == col)
                    continue;
                float factor = aug[row][col];
                for (int k = 0; k < 2 * N; ++k)
                    aug[row][k] -= factor * aug[col][k];
            }
        }

        // M^{-1} is now in aug[i][j+N]. We need M^{-T} (transpose of inverse).
        // Compute M_{k+1} = 0.5 * (M + M^{-T})
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                perturbMatrix_[i][j] = 0.5f * (M[i][j] + aug[j][i + N]);
    }
}

void LiveVoxChamberPresetEngine::setUseHouseholder (bool enable)
{
    useHouseholder_ = enable;
}

void LiveVoxChamberPresetEngine::setUseWeightedGains (bool enable)
{
    useWeightedGains_ = enable;
    if (prepared_)
        updateDelayLengths();
}

void LiveVoxChamberPresetEngine::setDualSlope (float ratio, int fastCount, float fastGain)
{
    dualSlopeRatio_ = std::max (ratio, 0.0f);
    // Only 0 (disabled) and 8 (two independent 8×8 Hadamards) are supported.
    dualSlopeFastCount_ = (fastCount >= 8) ? 8 : 0;

    for (int i = 0; i < N; ++i)
        outputTapGain_[i] = (dualSlopeFastCount_ > 0 && i < dualSlopeFastCount_)
                                ? fastGain : 1.0f;

    if (prepared_)
        updateDecayCoefficients();
}

void LiveVoxChamberPresetEngine::setStereoCoupling (float amount)
{
    // Negative values disable stereo split (use full 16×16 Hadamard).
    // Zero or positive enables split; zero coupling = fully independent L/R (no leakage).
    if (amount < 0.0f)
    {
        stereoSplitEnabled_ = false;
        stereoCoupling_ = 0.0f;
    }
    else
    {
        stereoSplitEnabled_ = true;
        stereoCoupling_ = std::clamp (amount, 0.0f, 0.75f);
    }
}

void LiveVoxChamberPresetEngine::setNoiseModDepth (float samples)
{
    noiseModDepthParam_ = std::max (samples, 0.0f);
    if (prepared_)
        updateModDepth();
}

void LiveVoxChamberPresetEngine::setModDepthFloor (float floor)
{
    modDepthFloor_ = std::clamp (floor, 0.0f, 1.0f);
    if (prepared_)
        updateDelayLengths();
}

void LiveVoxChamberPresetEngine::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::clamp (hz, 1000.0f, 20000.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void LiveVoxChamberPresetEngine::setAirTrebleMultiply (float mult)
{
    airTrebleMultiply_ = std::clamp (mult, 0.1f, 1.5f);
    if (prepared_)
        updateDecayCoefficients();
}

void LiveVoxChamberPresetEngine::setStructuralHFDamping (float baseFreqHz, float trebleMultiply)
{
    structHFBaseFreq_ = baseFreqHz;
    if (baseFreqHz <= 0.0f)
    {
        structHFEnabled_ = false;
        structHFCoeff_ = 0.0f;
        return;
    }
    // Inverted treble scaling: dark presets (low treble) already have strong TwoBandDamping,
    // so structural damping is reduced (higher effectiveHz). Bright presets (high treble) have
    // weaker TwoBandDamping, so they get full structural damping (effectiveHz = baseFreqHz).
    // At treble=1.0: effectiveHz = baseFreqHz (full structural damping).
    // At treble=0.5: effectiveHz = baseFreqHz * 1.25 (reduced damping for dark presets).
    // At treble=0.1: effectiveHz = baseFreqHz * 1.45 (minimal damping for very dark presets).
    float effectiveHz = baseFreqHz * (1.5f - std::clamp (trebleMultiply, 0.1f, 1.0f) * 0.5f);
    structHFEnabled_ = true;
    structHFCoeff_ = std::exp (-kTwoPi * effectiveHz / static_cast<float> (sampleRate_));
}

void LiveVoxChamberPresetEngine::setStructuralLFDamping (float hz)
{
    if (hz <= 0.0f)
    {
        structLFEnabled_ = false;
        structLFCoeff_ = 0.0f;
        return;
    }
    structLFEnabled_ = true;
    structLFCoeff_ = std::exp (-kTwoPi * hz / static_cast<float> (sampleRate_));
}

void LiveVoxChamberPresetEngine::setCrossoverModDepth (float depth)
{
    crossoverModDepth_ = std::clamp (depth, 0.0f, 1.0f);
}

void LiveVoxChamberPresetEngine::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void LiveVoxChamberPresetEngine::setTerminalDecay (float thresholdDB, float factor)
{
    terminalDecayThresholdDB_ = thresholdDB;
    terminalDecayFactor_ = std::clamp (factor, 0.5f, 1.0f);
}

void LiveVoxChamberPresetEngine::clearBuffers()
{
    for (int i = 0; i < N; ++i)
    {
        std::fill (delayLines_[i].buffer.begin(), delayLines_[i].buffer.end(), 0.0f);
        delayLines_[i].writePos = 0;
        inlineAP_[i].clear();
        inlineAP2_[i].clear();
        inlineAP3_[i].clear();
        inlineAPShort_[i].clear();
        dampFilter_[i].reset();
        structHFState_[i] = 0.0f;
        structLFState_[i] = 0.0f;
        antiAliasState_[i] = 0.0f;
        dcX1_[i] = 0.0f;
        dcY1_[i] = 0.0f;
        // Deterministic LFO/PRNG reset for clean state on algorithm switch.
        // (Wrapper prepare() no longer calls clearBuffers(), so this does not
        // conflict with prepare()'s stochastic randomization.)
        lfoPhase_[i] = 0.0f;
        lfoPRNG_[i] = static_cast<uint32_t> (i + 1) * 2654435761u;
    }
    // Reset terminal decay RMS tracking
    peakRMS_ = 0.0f;
    currentRMS_ = 0.0f;
    terminalDecayActive_ = false;
}

// ---------------------------------------------------------------------------
void LiveVoxChamberPresetEngine::updateDelayLengths()
{
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);

    // Size-dependent gain compensation: shorter delays at small sizes pack
    // more feedback recirculations per unit time → higher energy density.
    // Power 0.4 balances attenuation at small sizes (~-2.0 dB at sizeScale=0.5)
    // with modest boost at large sizes (+1.4 dB at sizeScale=1.5).
    sizeCompensation_ = std::pow (sizeScale, 0.4f);

    float maxDelay = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        delayLength_[i] = static_cast<float> (baseDelays_[i]) * rateRatio * sizeScale;
        if (delayLength_[i] > maxDelay)
            maxDelay = delayLength_[i];
    }

    // Per-delay modulation depth scaling: proportional to delay length.
    // Prevents pitch wobble on short delays (Room) while preserving
    // full modulation on long delays (Hall/Ambient).
    // Floor is per-algorithm: Room uses 0.50 for more mode blurring on
    // short delays; others use 0.35 default.
    for (int i = 0; i < N; ++i)
    {
        float scale = (maxDelay > 0.0f) ? delayLength_[i] / maxDelay : 1.0f;
        modDepthScale_[i] = std::max (scale, modDepthFloor_);
    }

    // Per-channel input/output gain scaling for uniform modal excitation.
    // Weight by 1/sqrt(delay_length / min_delay) so shorter delays (which recirculate
    // more often) get higher gain, compensating for their naturally lower energy
    // accumulation per pass. This flattens the spectral envelope of the reverb tail.
    if (useWeightedGains_)
    {
        float minDelay = delayLength_[0];
        for (int i = 1; i < N; ++i)
            minDelay = std::min (minDelay, delayLength_[i]);

        float sumSqIn = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            inputGainScale_[i] = 1.0f / std::sqrt (delayLength_[i] / minDelay);
            sumSqIn += inputGainScale_[i] * inputGainScale_[i];
        }
        // Normalize so RMS of gain vector equals 1 (preserves overall input energy)
        float normIn = std::sqrt (static_cast<float> (N) / sumSqIn);
        for (int i = 0; i < N; ++i)
            inputGainScale_[i] *= normIn;

        // Output gains: same weighting (symmetry for spectral flatness)
        for (int i = 0; i < N; ++i)
            outputGainScale_[i] = inputGainScale_[i];
    }
    else
    {
        for (int i = 0; i < N; ++i)
        {
            inputGainScale_[i] = 1.0f;
            outputGainScale_[i] = 1.0f;
        }
    }
}

void LiveVoxChamberPresetEngine::updateDecayCoefficients()
{
    // Low crossover: bass/mid split (user-controlled crossover frequency, ~633Hz)
    float lowCrossoverCoeff = std::exp (-kTwoPi * crossoverFreq_
                                        / static_cast<float> (sampleRate_));

    // High crossover: mid/air split (per-algorithm, e.g. Room=6kHz, others=20kHz)
    float highCrossoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_
                                         / static_cast<float> (sampleRate_));

    for (int i = 0; i < N; ++i)
    {
        // Per-channel RT60: fast channels get shorter decay for dual-slope envelope
        float channelRT60 = decayTime_;
        if (dualSlopeFastCount_ > 0 && i < dualSlopeFastCount_ && dualSlopeRatio_ > 0.0f)
            channelRT60 = std::max (decayTime_ * dualSlopeRatio_, 0.2f);

        // Per-delay feedback gain for desired RT60
        // g_base = 10^(-3 * L / (RT60 * sr)) so that after RT60 seconds, signal is at -60 dB
        // Effective loop length includes inline allpass delays (they add latency to the
        // feedback path but aren't part of delayLength_). Without this, algorithms with
        // inline diffusion (Plate, Chamber) under-decay because the formula assumes a
        // shorter loop than the signal actually traverses.
        float effectiveLength = delayLength_[i];
        if (inlineDiffCoeff_ > 0.0f)
        {
            float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
            if (useShortInlineAP_)
                effectiveLength += static_cast<float> (kInlineAPDelaysShort[i]) * rateRatio;
            else
                effectiveLength += static_cast<float> (kInlineAPDelays[i]) * rateRatio;
            if (! useShortInlineAP_ && inlineDiffCoeff2_ > 0.0f)
                effectiveLength += static_cast<float> (kInlineAPDelays2[i]) * rateRatio;
            if (inlineDiffCoeff3_ > 0.0f)
                effectiveLength += static_cast<float> (kInlineAPDelays3[i]) * rateRatio;
        }
        float gBase = std::pow (10.0f, -3.0f * effectiveLength
                                       / (channelRT60 * static_cast<float> (sampleRate_)));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);

        // Bass Multiply: g_low = g_base^(1/bassMultiply)
        // bassMultiply > 1.0 → lows sustain longer (g_low > g_base)
        float gLow = std::pow (gBase, 1.0f / bassMultiply_);

        // Mid band (lowCrossover..highCrossover): same as old gHigh formula.
        // Preserves the matched 4kHz RT60 from TwoBandDamping calibration.
        float gMid = std::pow (gBase, 1.0f / trebleMultiply_);

        // Air band (> highCrossover): independent damping via airTrebleMultiply_.
        // gHigh = gBase^(1/airTrebleMultiply_): lower values = faster decay.
        // At airTrebleMultiply_=1.0: gHigh = gBase (natural rate, no extra damping).
        // At airTrebleMultiply_=0.70: gHigh = gBase^1.43 (significant extra damping).
        float gHigh = std::pow (gBase, 1.0f / std::max (airTrebleMultiply_, 0.01f));

        dampFilter_[i].setCoefficients (gLow, gMid, gHigh, lowCrossoverCoeff, highCrossoverCoeff);
    }
}

void LiveVoxChamberPresetEngine::updateModDepth()
{
    // Scale by sample rate ratio so modulation depth (in time) is consistent
    // across 44.1kHz, 48kHz, 96kHz etc.
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = modDepth_ * 4.0f * rateRatio;
    noiseModDepth_ = noiseModDepthParam_ * rateRatio;
}

void LiveVoxChamberPresetEngine::updateLFORates()
{
    // Irregularly-spaced rate factors prevent modulation beating.
    // Adjacent ratios avoid simple rational relationships so no two
    // channels ever re-align into audible patterns.
    static constexpr float kRateFactors[N] = {
        0.801f, 0.857f, 0.919f, 0.953f, 0.991f, 1.031f, 1.063f, 1.097f,
        1.127f, 1.163f, 1.193f, 1.223f, 1.259f, 1.289f, 1.319f, 1.361f
    };

    for (int i = 0; i < N; ++i)
    {
        float rateHz = modRateHz_ * kRateFactors[i];
        lfoPhaseInc_[i] = kTwoPi * rateHz / static_cast<float> (sampleRate_);
    }
}

// ==========================================================================
//  END PRIVATE COPY
// ==========================================================================


class LiveVoxChamberPresetWrapper : public PresetEngineBase
{
public:
    LiveVoxChamberPresetWrapper() = default;
    ~LiveVoxChamberPresetWrapper() override = default;

    void prepare (double sampleRate, int maxBlockSize) override
    {
        sampleRate_ = sampleRate;
        // Apply sizing/scaling config BEFORE engine_.prepare() so buffers
        // allocate at the right size for the actual host sample rate AND for
        // the per-preset delay scale (Invariant 3).
        engine_.setSizeRange (kBakedSizeRangeMin * kVvDelayScale, kBakedSizeRangeMax * kVvDelayScale);
        // (no setDelayScale on this engine)
        engine_.setLateGainScale (kBakedLateGainScale);
        // VV-derived crossovers (clamped >0 in setters per Invariant 4)
        engine_.setHighCrossoverFreq (kVvHighCrossoverHz);
        engine_.setCrossoverFreq     (kVvCrossoverHz);
        // Bass/treble defaults must be set BEFORE prepare so decay coefficients
        // are computed with the correct damping baseline.
        engine_.setBassMultiply (kBakedBassMultScale);
        engine_.setTrebleMultiply (kBakedTrebleMultScale);
        lastTreble_ = kBakedTrebleMultScale;
        engine_.setAirTrebleMultiply (kBakedAirDampingScale * kVvAirDampingScale);
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
        if (lastStructHFHz_ > 0.0f)
            setStructuralHFDamping (lastStructHFHz_);
        if (overrideAirDamping_ >= 0.0f)
            setAirDampingScale (overrideAirDamping_);
        if (overrideHighCrossover_ >= 0.0f)
            setHighCrossoverFreq (overrideHighCrossover_);
        if (overrideNoiseMod_ >= 0.0f)
            setNoiseModDepth (overrideNoiseMod_);
        if (overrideLateGain_ >= 0.0f)
            setLateGainScale (overrideLateGain_);
        if (lastTerminalFactor_ < 1.0f)
            setTerminalDecay (lastTerminalThresholdDb_, lastTerminalFactor_);
        if (frozen_)
            setFreeze (true);

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
            const float fc     = std::min (kCorrEqHz[i], nyquistGuard);
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

    void setCrossoverFreq (float hz) override { engine_.setCrossoverFreq (hz); }

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
        engine_.setAirTrebleMultiply (scale);
    }

    void setNoiseModDepth (float samples) override
    {
        overrideNoiseMod_ = samples;
        engine_.setNoiseModDepth (samples);
    }

    void setStructuralHFDamping (float hz) override
    {
        lastStructHFHz_ = hz;
        engine_.setStructuralHFDamping (hz, lastTreble_);
    }

    void setSizeRange (float mn, float mx) override { engine_.setSizeRange (mn, mx); }
    void setLateGainScale (float scale) override
    {
        overrideLateGain_ = scale;
        engine_.setLateGainScale (scale);
    }

    // Reset hooks: restore baked defaults when override sentinel fires
    void resetAirDampingToDefault() override
    {
        overrideAirDamping_ = -1.0f;
        engine_.setAirTrebleMultiply (kBakedAirDampingScale * kVvAirDampingScale);
    }
    void resetHighCrossoverToDefault() override
    {
        overrideHighCrossover_ = -1.0f;
        engine_.setHighCrossoverFreq (kVvHighCrossoverHz);
    }
    void resetNoiseModToDefault() override
    {
        overrideNoiseMod_ = -1.0f;
        engine_.setNoiseModDepth (kBakedNoiseModDepth);
    }

    const char* getPresetName() const override { return "Live Vox Chamber"; }
    const char* getBaseEngineType() const override { return "FDNReverb"; }

    // Fix 4 (spectral): expose corrective EQ coefficients to DuskVerbEngine
    // so it can apply the same EQ to the ER path.
    int getCorrEQBandCount() const override { return kCorrEqBandCount; }
    bool getCorrEQCoeffs (float* b0, float* b1, float* b2,
                           float* a1, float* a2, int maxBands) const override
    {
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

    // Fix 1 (decay_ratio): expose VV-derived onset envelope table
    const float* getOnsetEnvelopeTable() const override { return kOnsetEnvelope; }
    int getOnsetEnvelopeTableSize() const override { return kOnsetTableSize; }
    float getOnsetDurationMs() const override { return kOnsetDurationMs; }

private:
    LiveVoxChamberPresetEngine engine_;
    float lastTreble_ = 0.5f;
    float lastStructHFHz_ = 0.0f;
    float lastTerminalThresholdDb_ = 0.0f;
    float lastTerminalFactor_ = 1.0f;  // 1.0 = disabled
    bool  frozen_ = false;
    double sampleRate_ = 48000.0;
    // Cached runtime overrides — replayed after prepare() to survive re-preparation.
    // Sentinel value -1.0 means "no override, use baked default".
    float overrideAirDamping_ = -1.0f;
    float overrideHighCrossover_ = -1.0f;
    float overrideNoiseMod_ = -1.0f;
    float overrideLateGain_ = -1.0f;
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

std::unique_ptr<PresetEngineBase> createLiveVoxChamberPreset()
{
    return std::unique_ptr<PresetEngineBase> (new LiveVoxChamberPresetWrapper());
}

// Self-register at static init time so DuskVerbEngine can look up this preset by name
static PresetEngineRegistrar gLiveVoxChamberPresetRegistrar ("PresetLiveVoxChamber", &createLiveVoxChamberPreset);


#pragma once

#include "TwoBandDamping.h"

#include <random>
#include <vector>

class FDNReverb
{
public:
    FDNReverb();

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
    void setModDepthFloor (float floor);
    void setFeedbackLP (float hz);
    void setFeedbackLP4thOrder (bool enable);
    void setPerChannelLP (float strength, float exponent);
    void setPerChannelShelf (float strength);
    void setNoiseModDepth (float samples);
    void setHadamardPerturbation (float amount);
    void setUseHouseholder (bool enable);
    void setUseWeightedGains (bool enable);
    void setFeedbackShelf (float freqHz, float gainDB, float Q);
    void setHighCrossoverFreq (float hz);
    void setAirTrebleMultiply (float mult);
    void setStructuralHFDamping (float baseFreqHz, float trebleMultiply);
    void setStructuralLFDamping (float hz);
    void setDualSlope (float ratio, int fastCount, float fastGain);
    void setStereoCoupling (float amount);
    void clearBuffers();

private:
    static constexpr int N = 16;
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr float kOutputScale = 0.353553f; // 1/sqrt(8) — normalizes 8-tap sum
    static constexpr float kOutputGain  = 2.0f;      // +6dB compensation for level matching
    static constexpr float kOutputBoost = 1.585f;     // +4 dB global reverb output level boost
    static constexpr float kSafetyClip  = 32.0f;     // Safety clamp — raised for dual-slope (high fast-tap gain)
    static constexpr int kNumOutputTaps = 8;

    // Worst-case base delay across all algorithms (for buffer allocation)
    static constexpr int kMaxBaseDelay = 5521;

    // Mutable delay and tap configuration (initialized to Hall defaults)
    int baseDelays_[N];
    int leftTaps_[8];
    int rightTaps_[8];
    float leftSigns_[8];
    float rightSigns_[8];

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

    DelayLine delayLines_[N];
    InlineAllpass inlineAP_[N];
    InlineAllpass inlineAP2_[N];
    InlineAllpass inlineAP3_[N];
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

    // Per-channel Butterworth lowpass in the feedback path.
    // 2nd-order (12 dB/oct) or 4th-order Linkwitz-Riley (24 dB/oct) per algorithm.
    // Direct Form II Transposed: 2 state variables per biquad section per channel.
    float fbLPZ1_[N] {};  // Section 1 state
    float fbLPZ2_[N] {};
    float fbLPZ3_[N] {};  // Section 2 state (only used when fbLP4thOrder_)
    float fbLPZ4_[N] {};
    // Per-channel biquad coefficients: each channel gets its own LP cutoff
    // based on delay length ratio (shorter delays → lower cutoff → more HF damping).
    float fbLP_b0_[N] {}, fbLP_b1_[N] {}, fbLP_b2_[N] {};
    float fbLP_a1_[N] {}, fbLP_a2_[N] {};
    bool fbLPEnabled_ = false;
    bool fbLP4thOrder_ = false; // true = cascade two identical sections (L-R 24 dB/oct)
    float perChannelLPStrength_ = 0.0f;
    float perChannelLPExponent_ = 0.5f;

    // Feedback loop shelving EQ: per-channel biquad high shelf for cumulative
    // spectral darkening inside the feedback loop ("in-loop" damping topology).
    // Each recirculation applies a small HF cut that compounds over the tail,
    // matching the natural spectral evolution of vintage digital reverbs.
    // Per-channel coefficients: shorter delays get proportionally more shelf cut.
    float fbShelfB0_[N] {}, fbShelfB1_[N] {}, fbShelfB2_[N] {};
    float fbShelfA1_[N] {}, fbShelfA2_[N] {};
    float fbShelfZ1_[N] = {};  // per-channel biquad state
    float fbShelfZ2_[N] = {};
    bool  fbShelfEnabled_ = false;
    float perChannelShelfStrength_ = 0.0f;

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
    // Row-normalized to preserve approximate energy conservation.
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
    bool frozen_ = false;
    bool prepared_ = false;

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();
    void updateModDepth();
};

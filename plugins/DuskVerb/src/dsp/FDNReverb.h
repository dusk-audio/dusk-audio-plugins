#pragma once

#include "DspUtils.h"
#include "TwoBandDamping.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// Multi-point output tap: reads from a fractional position within a delay line.
// Inspired by Dattorro's 7-tap output topology — reading from delay interiors
// instead of just endpoints produces naturally denser, smoother tails.
struct FDNOutputTap
{
    int channelIndex;      // 0-15: which FDN delay line
    float positionFrac;    // 0.0-1.0: fractional position within delay (1.0 = full length)
    float sign;            // ±1.0 for stereo decorrelation
};

class FDNReverb
{
public:
    FDNReverb();

    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setMidMultiply (float mult);              // NEW: 3-band mid (default 1.0)
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);
    void setSaturation (float amount);             // NEW: 0..1 drive softClip
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
    // User-facing tank density. amount is the DIFFUSION knob value [0, 1].
    // Linear map to inline-AP coefficient: knob 0 → off (Hadamard-only density,
    // current behaviour), knob 1 → 0.55 (reference hardware hall-density convention).
    // Routes through setInlineDiffusion which also updates inlineDiffCoeff2_/3_.
    void setTankDiffusion (float amount);
    void setUseShortInlineAP (bool use);
    void setMultiPointOutput (const FDNOutputTap* left, int numL,
                              const FDNOutputTap* right, int numR);
    void setMultiPointDensity (int tapsPerChannel);  // Generate taps dynamically
    void setModDepthFloor (float floor);
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

    // Phase 2: switch modulation topology. RandomWalk = legacy per-line
    // independent random walks. CoherentLoop = single master sine, phase-
    // paired across the 16 delay lines for cohesive macro-envelope motion.
    void setModulationTopology (DspUtils::ModulationTopology t);
    void setCrossoverModDepth (float depth);
    void setDecayBoost (float boost);
    void clearBuffers();

private:
    static constexpr int N = 16;
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr float kOutputLevel = 1.121f;     // 1/sqrt(8) * 2.0 * 1.585 — consolidated output scaling
    static constexpr float kSafetyClip  = 32.0f;     // Soft-clip ceiling — raised for dual-slope (high fast-tap gain)
    static constexpr int kNumOutputTaps = 8;
    static constexpr int kMaxMultiTaps = 256;

    // Worst-case base delay across all algorithms (for buffer allocation)
    // Must be >= the largest value in any preset delay table.
    // Currently: kPresetFatSnareHall reaches 6613.
    static constexpr int kMaxBaseDelay = 6700;

    // =========================================================================
    // LiveParams — RT-safe parameter snapshot.
    //
    // Every value that processBlock reads in its inner loop lives here.
    // Setters (message thread) write into the pending slot, then atomically
    // publish the pointer. processBlock (RT thread) loads the pointer once at
    // block entry via memory_order_acquire and dereferences it for every
    // sample. No torn reads of multi-word state (perturb matrix, biquad
    // coefficients, multi-point tap config) are possible.
    //
    // Zero-tear damping: ThreeBandDamping::Coeffs lives in the snapshot.
    // Filter state (biquad z1/z2) stays in the dampFilter_ array on the
    // RT side. process() takes the coefficients by const-ref.
    // =========================================================================
    struct LiveParams
    {
        // Per-channel arrays (inner-loop hot)
        float delayLength       [N] {};
        float modDepthScale     [N] {};
        float inputGainScale    [N] {};
        float outputGainScale   [N] {};
        float outputTapGain     [N] { 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1 };

        // Tap routing (standard 8-tap path)
        int   leftTaps          [kNumOutputTaps] { 0,3,5,7,8,10,12,15 };
        int   rightTaps         [kNumOutputTaps] { 1,2,4,6,9,11,13,14 };
        float leftSigns         [kNumOutputTaps] { 1,-1,1,-1,1,-1,1,-1 };
        float rightSigns        [kNumOutputTaps] { -1,1,-1,1,-1,1,-1,1 };

        // Feedback mixing
        float perturbMatrix     [N][N] {};
        bool  usePerturbedMatrix = false;
        bool  useHouseholder     = false;

        // Stereo split / dual-slope routing
        bool  stereoSplitEnabled = false;
        float stereoCoupling     = 0.0f;
        int   dualSlopeFastCount = 0;

        // Output / inline diffusion
        float lateGainScale      = 1.0f;
        float sizeCompensation   = 1.0f;
        float inlineDiffCoeff    = 0.0f;
        float inlineDiffCoeff2   = 0.0f;
        float inlineDiffCoeff3   = 0.0f;
        bool  useShortInlineAP   = false;

        // Multi-point output tap config
        FDNOutputTap multiTapsL  [kMaxMultiTaps] {};
        FDNOutputTap multiTapsR  [kMaxMultiTaps] {};
        int   numMultiTapsL      = 0;
        int   numMultiTapsR      = 0;
        bool  useMultiPointOutput = false;

        // Damping coefficients (zero-tear: passed by const-ref each sample)
        ThreeBandDamping::Coeffs damping[N] {};

        // Structural / anti-alias / DC-blocker coefficients
        float structHFCoeff      = 0.0f;
        float structLFCoeff      = 0.0f;
        float antiAliasCoeff     = 0.0f;
        float dcCoeff            = 0.9993f;
        bool  structHFEnabled    = false;
        bool  structLFEnabled    = false;

        // Saturation drive (read in inner loop)
        float saturationAmount   = 0.0f;

        // Freeze flag (every-sample read)
        bool  frozen             = false;
    };

    std::array<LiveParams, 2> paramSlots_;
    std::atomic<LiveParams*>  liveParams_ { nullptr };
    int                       pendingSlot_ = 1;   // message-thread-owned

    LiveParams& pending()   { return paramSlots_[pendingSlot_]; }
    void publishPending();
    // Snapshot derivation: each writes only into the supplied LiveParams.
    void computeDelayLengths       (LiveParams& p);
    void computeDecayCoefficients  (LiveParams& p);
    void computeModDepth           ();

    // Raw input state (message-thread, feeds the snapshot)
    int   baseDelays_[N];
    int   leftTapsIn_[8];
    int   rightTapsIn_[8];
    float leftSignsIn_[8];
    float rightSignsIn_[8];

    // Storage for setMultiPoint* setters; copied into pending().multiTaps[L|R].
    FDNOutputTap multiTapsLIn_[kMaxMultiTaps] {};
    FDNOutputTap multiTapsRIn_[kMaxMultiTaps] {};
    int   numMultiTapsLIn_ = 0;
    int   numMultiTapsRIn_ = 0;
    bool  useMultiPointIn_ = false;

    // Stored perturb matrix (message-thread); copied into pending() on publish.
    float perturbMatrixIn_[N][N] {};
    bool  usePerturbedIn_  = false;

    // -----------------------------------------------------------------------
    // RT-side state (mutated each sample; never read by message thread)
    // -----------------------------------------------------------------------
    float sizeRangeMin_ = 0.5f;
    float sizeRangeMax_ = 1.5f;
    float sizeRangeAllocatedMax_ = 4.0f;

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
        int delaySamples = 0;

        float process (float input, float g)
        {
            int readIdx = (writePos - delaySamples) & mask;
            float vd = buffer[static_cast<size_t> (readIdx)];
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
    static constexpr int kInlineAPDelaysShort[N] = {
        7, 11, 13, 17, 19, 23, 29, 31,
        37, 41, 43, 47, 7, 11, 13, 17
    };

    DelayLine delayLines_[N];
    InlineAllpass inlineAP_[N];
    InlineAllpass inlineAP2_[N];
    InlineAllpass inlineAP3_[N];
    InlineAllpass inlineAPShort_[N];
    ThreeBandDamping dampFilter_[N];     // holds biquad state only; coeffs come from lp.damping[]
    DspUtils::RandomWalkLFO lfos_[N];
    // Phase 2: single master sine LFO for CoherentLoop topology. All 16
    // delay lines tap THIS one LFO at per-line phase offsets so they
    // move in coordinated, phase-paired motion (line ch and ch+8 are
    // 180° apart). Per-sample cost vs RandomWalk: identical (1× sin).
    DspUtils::CoherentSineLFO coherentLfo_;
    DspUtils::ModulationTopology modulationTopology_ = DspUtils::ModulationTopology::RandomWalk;

    // Per-channel structural/anti-alias/DC-blocker FILTER STATE
    float structHFState_[N] {};
    float structLFState_[N] {};
    float antiAliasState_[N] {};
    float dcX1_[N] {};
    float dcY1_[N] {};

    // -----------------------------------------------------------------------
    // Raw user-set values (input to snapshot derivation; not RT-read directly)
    // -----------------------------------------------------------------------
    double sampleRate_ = 44100.0;
    float decayTime_ = 1.0f;
    float bassMultiply_ = 1.0f;
    float midMultiply_ = 1.0f;
    float trebleMultiply_ = 0.5f;
    float airTrebleMultiply_ = 1.0f;
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
    float structHFBaseFreq_ = 0.0f;
    // Minimum per-line mod-depth scale. Lowered 0.35→0.10 so short delay
    // lines barely modulate; only the longest lines breathe noticeably.
    // Previous 0.35 floor forced short delays to wobble too — contributed
    // to the chorus-in-tail issue alongside the LFO rate spread.
    float modDepthFloor_ = 0.10f;
    float dualSlopeRatio_ = 0.0f;
    bool  useWeightedGains_ = false;
    bool  prepared_ = false;

    void updateLFORates();
    void updateModDepth();

    // Per-instance Householder reflector vectors. Seeded once at construction
    // from a process-wide atomic counter so two FDN instances on the same bus
    // do NOT share the same v·vᵀ axis — sharing produced convergent eigenmodes
    // and audibly correlated tails. Written only on the message thread (here);
    // read-only on the RT thread thereafter.
    float householderV16_[N] {};
    float householderV8_[N / 2] {};
    void seedHouseholderVectors (uint32_t seed);
};

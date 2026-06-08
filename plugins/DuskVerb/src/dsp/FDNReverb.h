#pragma once

#include "DspUtils.h"
#include "TwoBandDamping.h"
#include "TimeVaryingDamping.h"

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

    // Phase θ (2026-06-01): post-loop "Tail Spin/Wander" — a 16-tap amplitude
    // VCA on the OUTPUT taps (post-summation of the feedback matrix, so it
    // never touches loop decay/stability). One master sine read at per-line
    // phase offsets creates measurable ENVELOPE AM (vs the delay-line PHASE
    // mod above, which reads as a flat macro-envelope). depth 0 → gains rest
    // at exactly 1.0f → IEEE754-exact ×1.0 bypass + the LFO/interp work is
    // skipped (tailSpinActive_). Default 0 → every legacy preset bit-identical.
    void setTailSpinDepth (float depth);   // 0..1, modulation depth
    void setTailSpinRate  (float hz);      // base rate of the master spin LFO

    void setBaseDelays (const int* delays);
    void resetBaseDelays();   // restore the default log-spaced-prime table
    void setOutputTaps (const int* lt, const int* rt,
                        const float* ls, const float* rs);
    void setLateGainScale (float scale);
    void setSizeRange (float min, float max);

    // Phase ε (2026-05-29): in-loop narrow-Q peaking band on the per-line
    // feedback signal (after damping, before write-back to delay lines).
    // Reinforces a specific frequency inside the FDN loop so steady-state
    // input at that frequency builds extra gain — closes sine1k cold by
    // injecting a narrow ±N dB mode boost near 1 kHz.
    //
    // gainDb = 0 designs unity coefficients (designUnity in ParametricBand)
    // → bit-identical bypass on the per-line damping output → legacy presets
    // see no change.
    void setInLoopPeaking (float freqHz, float qFactor, float gainDb);

    // Phase η (2026-05-29): per-line dual-time-constant bass shelf with
    // envelope-aware fast/slow mix. Decouples early bass attenuation
    // (active input) from late-tail bass sustain (decay phase). Both gains
    // 0 dB → bit-identical bypass; legacy presets unaffected.
    void setDualBassShelf (float fastFc, float slowFc,
                            float fastGainDb, float slowGainDb,
                            float transitionMs);

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
    // Phase 2 FiveBandDamping (FDN only): two extra decay plateaus + their
    // crossovers. sub (<subX) and hi-mid (highX..airX) get independent decay
    // multipliers. Transparent when subMult==bassMult & hiMidMult==trebleMult
    // (the new boundaries then sit between equal-gain bands → legacy 3-band).
    void setSubMultiply (float mult);
    void setHiMidMultiply (float mult);
    void setSubCrossoverFreq (float hz);
    void setAirCrossoverFreq (float hz);
    // Low-Band Transient Shaper (FDN only, Phase A plumbing). Dynamic, signal-
    // dependent low-band damping: tight early transient → relax to the static
    // (FiveBandDamping) sustain. depth=0 → bit-exact bypass (block skipped).
    void setShaperDepth (float depth);     // 0..1, 0 = off
    void setShaperTimeMs (float ms);       // fast-env release = tight-window length
    void setShaperXoverHz (float hz);      // low-band split for the shaper
    void setShaperSens (float sens);       // transient detector sensitivity
    // Block 2: feed-forward input energy makeup. Shapes the dry input BEFORE
    // it is written into the delay lines (scales B) → outside the feedback
    // matrix → BIBO-stable, no pole excursion. Both 0 dB → bit-exact bypass.
    void setInputSubGainDb (float db);     // sub low-shelf on input (~120 Hz)
    void setInputMidGainDb (float db);     // mid bell on input (~900 Hz)
    void setInputHighGainDb (float db);    // air high-shelf on input (~5 kHz) —
                                           // completes the 3-band pre-emphasis so
                                           // per-band T60 (in-loop) decouples from
                                           // per-band level (this pre-gain ⊥ poles)
    void setStructuralHFDamping (float baseFreqHz, float trebleMultiply);
    void setStructuralLFDamping (float hz);
    void setDualSlope (float ratio, int fastCount, float fastGain);
    void setStereoCoupling (float amount);
    void setFeedbackModDepth (float depth);
    // Phase 3 (VH->0): per-line energy-following high-shelf (TimeVaryingDamping).
    // earlyMult applies to HF above crossoverHz when a line's circulating energy
    // is fresh (early decay), lateMult when decayed. earlyMult<lateMult => faster
    // EARLY hi decay (shortens edt hi) while preserving late T60. earlyMult==
    // lateMult => bit-identical bypass (call skipped via tvHiActive_).
    void setTimeVaryingHiDamp (float earlyMult, float lateMult, float crossoverHz,
                               float releaseSec, float refLevel);

    // Phase 2: switch modulation topology. RandomWalk = legacy per-line
    // independent random walks. CoherentLoop = single master sine, phase-
    // paired across the 16 delay lines for cohesive macro-envelope motion.
    void setModulationTopology (DspUtils::ModulationTopology t);
    void setCrossoverModDepth (float depth);
    void setDecayBoost (float boost);

    // Phase α (Path α): per-line frequency-indexed decay scaling. Tilts
    // RT60 per delay line by sorting lines by length and assigning a per-
    // line decay-time multiplier across [shortLineScale .. longLineScale].
    // Longer lines (low-band-dominant content) get longLineScale; shorter
    // lines (high-band-dominant) get shortLineScale.
    //
    // shortLineScale = longLineScale = 1.0 → backward identical (default).
    // Setting longLineScale > 1.0 + shortLineScale < 1.0 extends bass RT60
    // while shortening high-band RT60 — exactly what VVV Concert Hall
    // delivers structurally and what 3-band damping multiplexing alone
    // cannot reach (FDN modal mixing redistributes per-band gains into
    // an averaged shape).
    //
    // Applied at line 1304 of updateLiveParams as
    //   channelRT60 = decayTime_ * perLineRT60Scale_[i].
    // Per-sample cost: ZERO (folds into existing per-channel design pass
    // run at preset-apply time, not the audio thread).
    void setPerLineDecayTilt (float shortLineScale, float longLineScale);

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
        FiveBandDamping::Coeffs damping[N] {};

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
    void updateShaperCoeffs        ();   // TPT LPF g + env att/rel from params

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
    FiveBandDamping dampFilter_[N];      // holds biquad state only; coeffs come from lp.damping[]
    DspUtils::RandomWalkLFO lfos_[N];
    // Phase 2: single master sine LFO for CoherentLoop topology. All 16
    // delay lines tap THIS one LFO at per-line phase offsets so they
    // move in coordinated, phase-paired motion (line ch and ch+8 are
    // 180° apart). Per-sample cost vs RandomWalk: identical (1× sin).
    DspUtils::CoherentSineLFO coherentLfo_;

    // Phase ε (2026-05-29): per-line in-loop peaking band. One independent
    // ParametricBand per line (each carries its own L+R state — though FDN
    // lines are mono internally, the class uses processL for the single
    // channel). gainDb = 0 → designUnity → bit-identical bypass on the
    // damping output path. Coefficient design happens off-thread in
    // setInLoopPeaking; the audio loop only runs the 5-mul biquad.
    DspUtils::ParametricBand inLoopPeak_[N];
    bool inLoopPeakActive_ = false;     // true iff |gainDb| > 1e-6
    // Stored config so prepare() can RE-APPLY the band — ParametricBand::prepare
    // designUnity's the coeffs, so without this the in-loop peak silently dies
    // on every engine re-prepare (DAW transport reset, harness reset()).
    float inLoopPeakFreq_   = 1000.0f;
    float inLoopPeakQ_      = 2.0f;
    float inLoopPeakGainDb_ = 0.0f;

    // Phase η (2026-05-29): per-line dual-time-constant bass shelf. Applied
    // AFTER ThreeBandDamping → BEFORE structHF/LF stage. Default both gains
    // 0 dB → bit-identical bypass (anyActive_ guard inside class skips
    // processing entirely).
    DspUtils::DualTimeConstantBassShelf dualBassShelf_[N];
    bool dualBassShelfActive_ = false;
    // Stored shape so prepare() can re-apply it — DualTimeConstantBassShelf::
    // prepare() designs back to unity, so without replay the shelf silently
    // bypasses after every re-prepare (mirror of the inLoopPeak_ restore).
    float dualBassFastFc_       = 400.0f;
    float dualBassSlowFc_       = 200.0f;
    float dualBassFastGainDb_   = 0.0f;
    float dualBassSlowGainDb_   = 0.0f;
    float dualBassTransitionMs_ = 100.0f;

    // Phase 3 (VH->0): per-line energy-following high-shelf. Single instance,
    // 16 internal channel trackers. tvHiActive_ false => process() skipped in the
    // loop => bit-identical. Stored config re-applied in prepare() (designCoeffs
    // resets on re-prepare, mirror of inLoopPeak_/dualBassShelf_ restore).
    DspUtils::TimeVaryingDamping tvDampHi_;
    bool  tvHiActive_     = false;
    float tvHiEarlyMult_  = 1.0f;
    float tvHiLateMult_   = 1.0f;
    float tvHiCrossover_  = 2500.0f;
    float tvHiReleaseSec_ = 0.30f;
    float tvHiRefLevel_   = 0.50f;

    DspUtils::ModulationTopology modulationTopology_ = DspUtils::ModulationTopology::RandomWalk;

    // Phase θ/Phase 2: post-loop Tail Spin/Wander output VCA. A 16-phase sine
    // bank — one accumulator per delay line, each advancing at base rate ×
    // per-line multiplier kTailSpinRateMul[ch] (a fixed non-harmonic golden-
    // ratio spread ~0.85..1.15×). DISTINCT per-line frequencies (not just phase
    // offsets) are mandatory: 16 same-frequency LFOs sum to a single AM rate,
    // so only rate divergence surfaces the per-band-distinct AM the gates want.
    // Dedicated array (NOT the delay-mod spread): tail-spin AM rates must stay
    // decoupled from delay-mod retuning. Gains computed at control rate (every
    // kTailSpinBlock samples — a <10 Hz LFO is wildly oversampled) + per-sample
    // linear interp, so only 1/kTailSpinBlock of the 16 std::sin calls run.
    // Applied at output-tap summation; tailSpinCur_ rests at 1.0f → bit-exact
    // ×1.0 bypass. Phases seeded deterministically (reproducible renders).
    static constexpr int kTailSpinBlock = 32;
    float tailSpinPhase_[N] {};           // per-line phase accumulator (rad)
    float tailSpinInc_  [N] {};           // per-line phase advance (rad/sample) = base×γ_c
    float tailSpinRateMul_[N] {};         // γ_c, filled deterministically in prepare()
    float tailSpinDepth_   = 0.0f;        // 0 → bypass (default)
    float tailSpinRateHz_  = 1.0f;
    bool  tailSpinActive_  = false;       // tailSpinDepth_ > 1e-6
    int   tailSpinCounter_ = 0;           // control-rate block countdown
    float tailSpinCur_ [N];               // per-line gain (init 1.0f in prepare)
    float tailSpinStep_[N] {};            // per-sample interp increment

    // Phase 3: ModulatedDamping topology — slow master quadrature LFO modulates
    // the per-line ThreeBandDamping coefficients (no line-length mod). Pre-
    // computed "dark" + "bright" Coeffs sets at preset-apply time; per-sample
    // lerp between them via masterDampingLfoPhase_. Zero transcendentals on
    // the hot path. masterDampingLfoIncr_ is the per-sample phase advance
    // (rad/sample) for the configured rate. The cos/sin pair gives a 90°
    // quadrature option for future "stereo wander" extension.
    float masterDampingLfoPhase_ = 0.0f;
    float masterDampingLfoIncr_  = 0.0f;   // rad/sample, set in updateLFORates()
    static constexpr float kDampingModRateHz = 0.30f;  // very slow drift
    // Phase α post-fix: replaced raw-coefficient lerp (dampingDark_ ↔
    // dampingBright_) with a 32-step lookup table of pre-designed, stability-
    // validated Coeffs. Per-sample work is now O(1) integer-index lookup +
    // struct copy. Each table slot is a complete RBJ shelf design from a
    // scalar gain interpolation (gHigh_dark → gHigh_bright); linearly
    // lerping the SCALAR before design keeps the resulting coefficients
    // inside the biquad stability triangle. Eliminates the "garbage / mud /
    // noise" artefact caused by the prior raw-coefficient lerp briefly
    // pulling (a1, a2) outside the stability region twice per LFO cycle.
    static constexpr int kDampingSteps = 32;
    FiveBandDamping::Coeffs dampingModTable_[N][kDampingSteps] {};
    bool dampingModActive_ = false;        // true only when topology == ModulatedDamping
                                            // AND designCoeffs has populated the table

    // Phase α: per-line RT60 multiplier indexed by delay-line rank.
    // Rank 0 = shortest line (gets shortLineScale_), rank 15 = longest
    // (gets longLineScale_). Default 1.0 / 1.0 = backward-identical.
    // Recomputed inside updateLiveParams using current delayLength[] —
    // robust to setSize / sizeScale changes.
    float shortLineScale_ = 1.0f;
    float longLineScale_  = 1.0f;
    float perLineRT60Scale_[N] {};   // 0.0f means "not yet computed; fall back to 1.0"

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
    float subMultiply_      = 1.0f;     // FiveBandDamping sub-band decay mult
    float hiMidMultiply_    = 1.0f;     // FiveBandDamping hi-mid-band decay mult
    float subCrossoverFreq_ = 120.0f;   // sub ↔ low-mid boundary (Hz)
    float airCrossoverFreq_ = 8000.0f;  // hi-mid ↔ air boundary (Hz)

    // Low-Band Transient Shaper (Phase A). Coeffs precomputed on param change;
    // per-line state below. shaperActive_ gates the whole block → depth 0 is
    // bit-exact bypass.
    float shaperDepth_   = 0.0f;        // 0 = off (default → bypass)
    float shaperTimeMs_  = 120.0f;
    float shaperXoverHz_ = 250.0f;
    float shaperSens_    = 1.5f;
    bool  shaperActive_  = false;       // shaperDepth_ > 0
    float shaperLpG_     = 0.0f;        // TPT one-pole: g/(1+g), precomputed
    float shaperAttF_ = 1.0f, shaperRelF_ = 0.0f;   // fast env coeffs
    float shaperAttS_ = 1.0f, shaperRelS_ = 0.0f;   // slow env coeffs
    float shaperLp_[N]   {};            // per-line TPT LPF state
    float shaperEnvF_[N] {};            // per-line fast envelope
    float shaperEnvS_[N] {};            // per-line slow envelope

    // Block 2: feed-forward input makeup (pre-delay-write, outside the loop).
    float inputSubGainDb_   = 0.0f;
    float inputMidGainDb_   = 0.0f;
    float inputHighGainDb_  = 0.0f;
    bool  inputMakeupActive_ = false;   // any gain != 0 → block runs
    ShelfBiquad inputSubL_, inputSubR_; // sub low-shelf, per-channel state
    DspUtils::ParametricBand inputMid_; // mid bell (processL/processR)
    ShelfBiquad inputHighL_, inputHighR_; // air high-shelf, per-channel state
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
    void updateTailSpinIncs();   // recompute per-line phase increments = 2π·base·γ_c/sr

    // Per-instance Householder reflector vectors. Seeded once at construction
    // from a process-wide atomic counter so two FDN instances on the same bus
    // do NOT share the same v·vᵀ axis — sharing produced convergent eigenmodes
    // and audibly correlated tails. Written only on the message thread (here);
    // read-only on the RT thread thereafter.
    float householderV16_[N] {};
    float householderV8_[N / 2] {};
    void seedHouseholderVectors (uint32_t seed);
};

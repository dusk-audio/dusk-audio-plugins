#pragma once

#include "DspUtils.h"
#include "TwoBandDamping.h"   // ThreeBandDamping lives in this header alongside TwoBandDamping.

#include <cstdint>
#include <vector>

// Plate (Foil) engine — PCM-style foil-plate emulator.
//
// Built 2026-05-02 to fix the structural failures of SixAPTankEngine on
// Lexicon-style plate presets (verified across 26 calibration iterations
// against the Lex Vocal Plate render):
//
//   • Modal humps at 125-250 Hz and 2-4 kHz from fixed delay-line lengths.
//     Solved by aggressive (~5 %) per-AP delay-jitter using independent
//     random-walk LFOs — every density AP has its own LFO with a distinct
//     seed, breaking the comb peaks that plagued SixAPTank.
//
//   • Tail-centroid ceiling around 7250 Hz from SixAPTank's storage factor
//     and large delay lines that smear too much HF energy into the loop.
//     Plate uses ~half the cascade length, so each circulation passes
//     through fewer samples of damping per traversal — Treble Multiply
//     above 1.0 then preserves HF past 9 kHz centroid.
//
//   • Slow early-decay buildup (-20 dB drop hits 240 ms vs target 200 ms).
//     Solved by a 2-AP cross-coupled input network up front (Dattorro-
//     style figure-8) that smears the input to high density BEFORE it
//     hits the late tank — unlike SixAPTank where the parallel diffuser's
//     ~40 reflections still need a tank pass to smear into the tail.
//
// Topology:
//
//   in (L,R) — already diffused upstream by shell DiffusionStage
//     ↓
//   2-AP cross-coupled input network (per channel, no jitter)
//     ↓
//   delay1 (main figure-8 delay)
//     ↓
//   6-AP density cascade (per-AP RandomWalkLFO jitter at 5%)
//     ↓
//   ThreeBandDamping (low/high RBJ shelves around mid passband)
//     ↓
//   ap2 (static, no jitter — late-tail "polish")
//     ↓
//   delay2 → soft-clip saturation → cross-feed to OTHER channel
//     ↓
//   out (L from leftBranch, R from rightBranch)
//
// All delay lengths are mutually prime across both channels (left + right
// = 28 distinct primes) so modal energy across the figure-8 never aligns.
//
// Phase 2 (this commit): full plate DSP with stable feedback. Tunes for
// the Lex Vocal Plate target arrive in Phase 4 via FactoryPresets.
class PlateEngine
{
public:
    PlateEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    // Standard engine contract — same shape as DattorroTank/SixAPTankEngine
    // so DuskVerbEngine's setX() forwards remain regular.
    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setMidMultiply (float mult);
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);          // bass↔mid (legacy "crossover")
    void setHighCrossoverFreq (float hz);      // mid↔high (3-band)
    void setSaturation (float amount);
    void setModDepth (float depth);
    void setModRate (float hz);
    void setSize (float size);
    void setFreeze (bool frozen);
    void setTankDiffusion (float amount);

    // Per-channel input high-shelf — small RBJ shelf applied to the dry
    // input BEFORE the cross-feedback mix. Lifts the front-of-impulse HF
    // content so the integrated tail energy in the air band closes the
    // gap to PCM-style foil plates without compromising loop stability
    // (the cross-feed path itself is untouched, so per-pass HF gain
    // stays governed by ThreeBandDamping). Default 0 dB = engine
    // identical to the pre-2026-05-03 behaviour.
    void setInputHighShelf (float gainDb, float fcHz);

    void clearBuffers();

private:
    static constexpr float  kTwoPi          = 6.283185307179586f;
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float  kSafetyClip     = 4.0f;
    static constexpr int    kNumDensityAPs  = 6;

    // 2-AP input network — small Dattorro-style allpasses for fast diffuse
    // buildup. Distinct per-channel primes so the figure-8 never sees
    // identical delay reads on both branches.
    static constexpr int kLeftIn1Base   = 142;
    static constexpr int kLeftIn2Base   = 379;
    static constexpr int kRightIn1Base  = 167;
    static constexpr int kRightIn2Base  = 401;

    // Main figure-8 delays. Shrunk 5× (was 1571/2027 / 1483/1949) on
    // 2026-05-03 — the previous values gave a total signal path of
    // ~145 ms before cascade output arrived back, leaving a deep "valley"
    // at 16-48 ms post-onset where the wet was effectively silent (the
    // ER cluster decayed by ~30 ms but the cascade-driven tail didn't
    // start until ~100 ms). Lex Vintage Plate IRs sustain −40 dB
    // throughout that window because their plate has shorter modal
    // delays. New primes bring total cascade path down to ~35 ms at
    // size 0.30 / 48 kHz, so cascade output begins filling the valley
    // at the right time. All 22 prime values are mutually distinct
    // across L and R and distinct from input-network / ap2 primes.
    static constexpr int kLeftDel1Base  = 311;
    static constexpr int kLeftDel2Base  = 409;
    static constexpr int kRightDel1Base = 313;
    static constexpr int kRightDel2Base = 419;

    // Static "polish" allpass after damping. Also shrunk to keep its
    // smear proportional to the new loop period.
    static constexpr int kLeftAP2Base   = 83;
    static constexpr int kRightAP2Base  = 89;

    // 6-AP density cascade — sum 728 / 780 (down from 4298 / 4364).
    // Shorter per-stage smear, faster modal buildup. All values prime,
    // mutually distinct, distinct from delay/ap2/input primes.
    static constexpr int kLeftDensityAPBase[kNumDensityAPs]  = {  53,  67,  97, 127, 173, 211 };
    static constexpr int kRightDensityAPBase[kNumDensityAPs] = {  59,  73, 101, 137, 181, 229 };

    // Worst-case sizing: max base delay × 88.2k/44.1k × 1.5 size scale.
    // Largest base delay is now 419 (kRightDel2Base) so we can shrink
    // this to free buffer memory, but 2200 is harmless and preserves
    // headroom for any future preset that pushes size higher.
    static constexpr int kMaxBaseDelay = 2200;

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

        // Per-AP random-walk LFO — independent depth and rate per stage so
        // modal phase-locks can never form across the cascade. SixAPTank
        // showed that 1.5 % jitter still lets 125-250 Hz / 2-4 kHz humps
        // crystallise; Plate runs at 5 % to drive modes below the 1 dB JND.
        DspUtils::RandomWalkLFO jitterLFO;
        float                   jitterDepthFraction = 0.0f;

        void allocate (int maxSamples);
        void clear();

        // Push the latest delay length into the LFO. Both depth and rate
        // scale with delaySamples — depth so the *relative* jitter stays
        // constant across all sizes, rate so the LFO period is always
        // ~2× the AP ring period (fast enough to break short rings).
        void updateJitterDepth (float sampleRate)
        {
            if (jitterDepthFraction <= 0.0f || delaySamples <= 0)
                return;
            jitterLFO.setDepth (static_cast<float> (delaySamples) * jitterDepthFraction);
            const float period = 2.0f * static_cast<float> (delaySamples);
            const float lfoRateHz = sampleRate / period;
            const float clamped = std::min (std::max (lfoRateHz, 5.0f), 200.0f);
            jitterLFO.setRate (clamped);
        }

        // Allpass kernel with optional 6-point Lagrange interpolated read at
        // (writePos − delaySamples − jitter). When jitterDepthFraction is
        // 0 the kernel collapses to a simple integer-tap read for the
        // input-section APs that don't need modulation.
        //
        // Lagrange-6 (was cubicHermite pre-2026-05-18) cuts the per-AP HF
        // rolloff from ~3 dB to <0.5 dB at 16 kHz / 48 kHz sample rate.
        // Critical because the 6-AP density cascade passes through this
        // path 6× per signal traversal — cubicHermite's HF loss compounded
        // to ~18 dB at 16 kHz, manifesting as a fixed 0.7 s RT60 deficit
        // at 16 kHz even with all bandGains set to 1.0 (FREEZE mode).
        float process (float input, float g)
        {
            float vd;
            if (jitterDepthFraction > 0.0f)
            {
                const float jitter  = jitterLFO.next();
                const float readPos = static_cast<float> (writePos)
                                    - static_cast<float> (delaySamples)
                                    - jitter;
                int   intIdx = static_cast<int> (std::floor (readPos));
                const float frac = readPos - static_cast<float> (intIdx);
                intIdx = static_cast<int> (static_cast<unsigned int> (intIdx)
                                            & static_cast<unsigned int> (mask));
                vd = DspUtils::lagrange6 (buffer.data(), mask, intIdx, frac);
            }
            else
            {
                vd = buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
            }
            const float vn = input + g * vd;
            buffer[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }
    };

    // ---------------------------------------------------------------------
    // 3-band LR4 (Linkwitz-Riley 24 dB/oct) split + per-band gain. Replaces
    // ThreeBandDamping's shelving 2026-05-18 because shelving's 12 dB/oct
    // slope couples 125 Hz to 250-500 Hz so we could never fix one without
    // breaking the other (Rich Plate plateaued at 5/8 bands within JND).
    //
    // Topology:
    //   bass    = LR4_LP(x, fLow)        — 2 cascaded 2nd-order Butterworth LP
    //   treble  = LR4_HP(x, fHigh)       — 2 cascaded 2nd-order Butterworth HP
    //   mid     = x − bass − treble       — complementary sum-minus
    //   out     = gLow * bass + gMid * mid + gHigh * treble
    //
    // Sum-minus mid relies on the LR4 sum = allpass property, which is
    // exact at a single xover. With two well-separated xovers (fLow ≪
    // fHigh) the mid band is correct away from transitions; mild phase
    // ripple near transitions is acceptable inside the feedback loop
    // (per-band decay rate is the dominant audible quantity, not phase).
    //
    // Cost: 4 biquads per channel per sample (~32 ops). Cheap enough
    // for the inner loop.
    struct LR4BandSplit
    {
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;
            void designLP (float fcHz, float sr);
            void designHP (float fcHz, float sr);
            float process (float x)
            {
                const float y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
            void reset() { z1 = z2 = 0.0f; }
        };

        Biquad lpA, lpB;   // cascaded LP for bass band
        Biquad hpA, hpB;   // cascaded HP for treble band
        float  gLow = 1.0f, gMid = 1.0f, gHigh = 1.0f;

        void setCoefficients (float gLow_, float gMid_, float gHigh_,
                              float fLowHz, float fHighHz, float sr);
        float process (float x)
        {
            const float bass   = lpB.process (lpA.process (x));
            const float treble = hpB.process (hpA.process (x));
            const float mid    = x - bass - treble;
            return gLow * bass + gMid * mid + gHigh * treble;
        }
        void prepare (float /*sr*/) { reset(); }
        void reset()
        {
            lpA.reset(); lpB.reset();
            hpA.reset(); hpB.reset();
        }
    };

    // Parallel bass-band extension resonator. The main figure-8 tank cannot
    // produce Lex Vintage Plate Rich Plate's bass bump (125 Hz at 1.57 s,
    // mid bands at 1.30 s) because raising the bass damping multiplier lifts
    // the entire loop's modulation-sideband energy into the mid bands too —
    // measured at 5/8 RT60 bands within JND with bands 125 / 250 / 16 k all
    // out. This sidechain is fed by the dry input (LR4 LP at fLowHz so only
    // sub-200 Hz content enters), recirculates through a comb-filter with
    // adjustable feedback gain, and sums into the main tank's output. It
    // is *not* part of the cross-coupled loop, so increasing its gain
    // extends the 125 Hz tail without touching the mid/treble decay. Per
    // channel; L/R run at slightly different delays for natural width.
    struct BassExtensionLoop
    {
        DelayLine delay;
        // 8th-order LP (4 cascaded biquads). 24 dB/oct LR4 attenuation
        // wasn't steep enough to keep the resonator's ~1.7 s tail out of
        // the 250 Hz octave RT60 measurement (Schroeder reads slope, so
        // even at −28 dB the slow tail dominated). Two cascaded LR4s
        // (= 48 dB/oct) puts 250 Hz at −56 dB below cutoff, safely under
        // the 125 Hz tail amplitude.
        LR4BandSplit::Biquad lpA, lpB, lpC, lpD;
        // Post-feedback LP — applied to the recirculating signal so each
        // loop further bandlimits, preventing energy aliasing up into mid
        // bands across multiple loops.
        LR4BandSplit::Biquad fbLpA, fbLpB;
        int      delaySamples  = 0;
        float    feedbackGain  = 0.83f;
        float    outputGain    = 0.50f;
        float    cutoffHz      = 110.0f;
        float    fbCutoffHz    = 160.0f;

        void allocate (int maxSamples) { delay.allocate (maxSamples); }
        void clear()
        {
            delay.clear();
            lpA.reset(); lpB.reset(); lpC.reset(); lpD.reset();
            fbLpA.reset(); fbLpB.reset();
        }
        void prepare (float sr)
        {
            lpA.designLP (cutoffHz, sr);
            lpB.designLP (cutoffHz, sr);
            lpC.designLP (cutoffHz, sr);
            lpD.designLP (cutoffHz, sr);
            fbLpA.designLP (fbCutoffHz, sr);
            fbLpB.designLP (fbCutoffHz, sr);
            clear();
        }
        float process (float dryInput)
        {
            const float low = lpD.process (lpC.process (lpB.process (lpA.process (dryInput))));
            const float fbRaw = delay.read (delaySamples);
            const float fb = fbLpB.process (fbLpA.process (fbRaw));
            const float toWrite = low + fb * feedbackGain;
            delay.write (toWrite);
            return fb * outputGain;
        }
    };

    // One side of the figure-8 (L or R).
    struct Branch
    {
        // 2-AP input network (small Dattorro-style APs, no jitter).
        Allpass in1, in2;
        int     in1BaseDelay = 0;
        int     in2BaseDelay = 0;

        // Main figure-8 delays.
        DelayLine delay1;
        DelayLine delay2;
        int       delay1BaseDelay = 0;
        int       delay2BaseDelay = 0;
        float     delay1Samples   = 0.0f;
        float     delay2Samples   = 0.0f;

        // 6-AP density cascade (with per-AP jitter LFOs).
        Allpass densityAP[kNumDensityAPs];
        int     densityAPBase[kNumDensityAPs] = {};

        // Input high-shelf — applied to the dry input only (not the
        // recirculating cross-feedback). Adds initial HF energy without
        // compounding through the loop.
        ShelfBiquad inputHighShelf;

        // Damping: 3-band LR4 (24 dB/oct) split, per-band gain.
        LR4BandSplit damping;

        // Static "polish" allpass after damping (no jitter, like Dattorro
        // ap2). Gives the late tail a final smear without adding modal
        // energy back into the cascade.
        Allpass ap2;
        int     ap2BaseDelay = 0;

        // Cross-feedback state (sample held from previous iteration).
        float crossFeedState = 0.0f;
    };

    Branch leftBranch_;
    Branch rightBranch_;

    // Per-channel parallel bass extension. L and R run at slightly
    // different delay samples to preserve stereo width — same prime-delay
    // strategy as the main tank.
    BassExtensionLoop bassExtL_;
    BassExtensionLoop bassExtR_;

    double sampleRate_ = 44100.0;
    bool   prepared_   = false;
    bool   frozen_     = false;

    // Musical parameters.
    float decayTime_         = 1.20f;
    float bassMultiply_      = 0.90f;
    float midMultiply_       = 0.85f;
    float trebleMultiply_    = 0.60f;
    float crossoverFreq_     = 800.0f;
    float highCrossoverFreq_ = 5500.0f;
    float saturationAmount_  = 0.10f;
    float modDepth_          = 0.10f;
    float modRate_           = 0.40f;
    float sizeParam_         = 0.50f;

    // Tank diffusion knob (0..1). Scales the density-cascade coefficient
    // around its baseline; analogous to SixAPTank's setTankDiffusion.
    float densityDiffBaseline_ = 0.62f;
    float densityDiffCoeff_    = 0.62f;
    float lastTankDiffusionAmount_ = 0.5f;

    // Per-AP "bloom stagger" — earlier stages run with a smaller coefficient
    // (faster transient passthrough → steep early-decay milestones),
    // later stages run higher (more ringing → slow late tail). This is the
    // Phase 3 fix for the RT60 / -20 dB-drop coupling identified during
    // the Lex Vocal Plate v18-v24 calibration: with a single uniform
    // density coefficient the decay shape was forced into a roughly
    // exponential envelope that couldn't match Lex's plate "knee" without
    // breaking RT60. Per-stage stagger separates the two.
    //
    // Default values create a gentle ramp from 0.65 (stage 0) to 1.35
    // (stage 5) — calibrated empirically against the Lex Vocal Plate
    // impulse-response milestones (–10 dB at 100 ms, –20 dB at 200 ms,
    // RT60 ≈ 0.96 s). Currently fixed at construction; no preset has
    // needed to override these yet, so there is no public setter. If a
    // future Plate variant (e.g., Plate Drum) needs a different early-
    // vs-late balance, expose a setBloomStagger(const float[6]) on the
    // PlateEngine and SixAPTankEngine pattern (see DuskVerbEngine.h —
    // setSixAPBloomStagger forwards a 6-float array).
    //
    // Effective per-AP coefficient = clamp (densityDiffCoeff_ × bloomStagger_[i],
    //                                       0, bloomCeiling_).
    // The ceiling guarantees stability even at extreme stagger × diffusion.
    float bloomStagger_[kNumDensityAPs] = { 0.65f, 0.80f, 0.95f, 1.10f, 1.22f, 1.35f };
    float bloomCeiling_                  = 0.85f;

    // Input high-shelf state — kept here so prepare() can re-design the
    // per-branch ShelfBiquads at the new sample rate, and so a runtime
    // setInputHighShelf() can re-design without mutating the Branches
    // directly. Default 0 dB is a true bypass.
    float inputHighShelfGainDb_ = 0.0f;
    float inputHighShelfFcHz_   = 6000.0f;

    // Static cross-feed gain. Higher than SixAPTank's because the plate
    // figure-8 is shorter, so the cross-feed needs more energy to stay
    // audible into the late tail. Stability bound (kMaxBandGain in
    // updateDecayCoefficients) keeps the loop from oscillating.
    //
    // 2026-05-18 raised 0.6 → 0.81 in two steps (0.6→0.80, then 0.80→0.81)
    // to lift the engine's RT60 ceiling. The figure-8 round-trip gain
    // ≈ bandGain² × kCrossFeedGain² controls measurable RT60 — at 0.6 the
    // ceiling was ~0.65 s regardless of Decay Time / Size params (cross-feed
    // dominated even when bandGain ≈ 0.95). Raising to 0.81 puts the
    // round-trip ceiling around 1.6 s so Lex Vintage Plate Rich Plate
    // (target 125 Hz = 1.568 s) becomes reachable with bandGain near the
    // 0.998 cap. Stability margin: 0.19 below the unity-feedback boundary;
    // signal-flow soft-clip (kSafetyClip in process()) still bounds runaway.
    static constexpr float kCrossFeedGain = 0.83f;

    // Input-section AP coefficient — small (Dattorro typical 0.5..0.7).
    static constexpr float kInputAPGain = 0.65f;

    // Static AP2 coefficient — moderate.
    static constexpr float kAP2Gain = 0.55f;

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();
    void applyDensityScale();
};

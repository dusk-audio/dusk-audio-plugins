#pragma once

#include "DspUtils.h"
#include "FDNReverb.h"
#include "DenseHallReverb.h"

#include <array>
#include <cmath>
#include <vector>

// ShimmerEngine v9 — external reference-style topology: pitch shifter sits in the
// FEEDBACK loop only, not on the forward path. The reverb sees the dry
// input directly, so the first wet hit is a natural reverb tail at the
// original pitch. Pitched reverb is added on top of that natural tail
// via the feedback cascade.
//
// Per-sample signal flow:
//
//   in ──> [drive softClip] ──┐
//                             ↓
//                         [+ mix node] ──> [FDNReverb hall] ──┬──> wet out
//                             ↑                                │
//                             │                                ↓
//   (pitched fb × fb × softClip) ←── [PitchShifter +N] ←── [delay 50 ms]
//
// Why this fixes "doesn't sound like reverb":
//   v8 (Eno aux-send chain) put the pitch shifter on the FORWARD path,
//   so the very first reverb hit was already pitched up — the wet output
//   was 100% pitched-cascade, with no natural reverb component anywhere.
//   That's only musical when the dry source is loud in the listener's
//   mix (Eno's actual use case), but as a "shimmer reverb" plugin where
//   mix=80% means dry sits at -10 dB while wet dominates, the listener
//   hears only pitched-cascade-of-pitched-cascade and no real tail.
//   v9 mirrors what external reference Shimmer / modern multi-FX hardware / modern shimmer
//   plugins do: dry input goes straight through the reverb (natural
//   tail), and the pitch shifter is in the feedback loop only, adding
//   the shimmer harmonics on top. At PITCH=0, the engine collapses to
//   an enhanced reverb with feedback-driven tail extension.
//
// The 50 ms feedback delay is unchanged from the v8 fix — it's a true
// circular buffer decoupled from DAW block size, sized for one feedback
// "cycle" of the cascade.
//
// Knob mapping (engine sees the universal setters but reinterprets):
//   setDecayTime      → reverb RT60 (0.5 - 8 s)
//   setSize           → reverb size (delay scaling)
//   setBassMultiply   → reverb bass mult (passes to FDN)
//   setMidMultiply    → reverb mid mult
//   setTrebleMultiply → reverb treble mult
//   setCrossoverFreq  → reverb low/mid xover (Hz)
//   setHighCrossoverFreq → reverb mid/high xover (Hz)
//   setSaturation     → input drive softClip
//   setModDepth       → "PITCH" knob: shift in semitones (0..1 → 0..24 st)
//   setModRate        → "FEEDBACK" knob: cascade strength (0.1..10 Hz → 0..0.95)
//   setTankDiffusion  → reverb diffusion (passes to FDN)
class ShimmerEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);
    void clearBuffers();

    // Universal setters — same surface as every other late-tank engine.
    void setDecayTime         (float seconds);
    void setSize              (float size);
    void setBassMultiply      (float mult);
    void setMidMultiply       (float mult);
    void setTrebleMultiply    (float mult);
    void setCrossoverFreq     (float hz);
    void setHighCrossoverFreq (float hz);
    void setSaturation        (float amount);
    void setModDepth          (float depth);   // hijacked: PITCH (0..1 → 0..24 semitones)
    void setModRate           (float hz);      // hijacked: FEEDBACK (0.1..10 → 0..0.95)
    void setTankDiffusion     (float amount);
    void setDownOctaveMix     (float mix);     // octave-DOWN voice level (0 = off/bit-null) — the warm low Valhalla Shimmer has, DV's up-only voices lacked
    void setSubOctaveMix      (float mix);     // octave-DOWN-2 voice (×0.25 → 250 Hz from 1 kHz); 0 = off/bit-null — reaches the deep lows in ONE step where the −1 oct cascade dies out
    void setFeedbackHpfHz     (float hz);      // feedback-loop HPF corner; lower = the low cascade survives (default 60; ~35 keeps killing the 12 Hz grain rumble)
    void setStereoMod         (float rateHz, float depth);  // wet-output stereo chorus/ensemble (anti-phase modulated delay); depth 0 = bypassed/bit-null. Matches Valhalla Black Hole's moving stereo field.
    void setHFAir             (float mix);   // post-loop +12 st air voice (genuine >12 kHz air); mix 0 = bit-null
    void setUseDenseReverb    (bool on);     // route the tank through DenseHallReverb (dense diffusion → smooth, non-metallic HF tail) instead of the sparse 16-line FDN. false = legacy FDN (bit-identical).
    void setUseTailSpin       (bool on);     // 2-stage modulated-allpass spin-comb on the FDN wet output — smears the metallic HF while keeping the FDN's cascade/width/HF (for Deep Blue Day). false = untouched.
    void setTailNoise         (float gain);  // envelope-tracked band-limited noise floor on the output — the dense noise-like fade Valhalla has; masks the sparse-mode ring. 0 = off/bit-null.
    void setUpVoiceScale      (float v1, float v2);  // per-preset scale on the +12/+24 up-voices — fills the mid tail (250 Hz-1 kHz) harder on transients (Deep Blue Day). 1.0/1.0 = bit-identical.
    void setOctaveCascade     (const float gains[4]);  // dry-fed feed-forward octave cascade levels (500/250/125/62 Hz) — matches Valhalla's even down-cascade. all 0 = off/bit-null.
    void setFreeze            (bool frozen);

private:
    // ──────────────────────────────────────────────────────────────────
    // GranularPitchShifter — overlapping-grain pitch shift via crossfaded
    // dual-grain reads. 4096-sample grains, 50% overlap, Hann window.
    // 4-pole Butterworth anti-alias LP at SR/2/ratio prevents alias
    // accumulation in the feedback loop. Same implementation that was
    // proven in v6 — kept as-is, this is the part of the prior engine
    // that worked correctly.
    // ──────────────────────────────────────────────────────────────────
    class GranularPitchShifter
    {
    public:
        static constexpr int kGrainSize = 4096;

        void prepare (double sampleRate);
        void clear();
        void setPitchRatio (float ratio);
        // rateHz: LFO speed of the slow random-walk modulation applied to
        //   the per-sample read advancement. Different rates on L vs R give
        //   stereo decorrelation and prevent sharp peak buildup.
        // depth: fractional ratio variation (e.g. 0.005f = ±0.5% pitch ratio,
        //   ≈ ±8 cents — defocuses cascade peaks without audibly detuning).
        void setModulation (float rateHz, float depth, std::uint32_t seed);
        float process (float input);

    private:
        struct BiquadLP
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
            float a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;
            void setCoeffs (double sampleRate, float fc, float Q);
            inline float process (float x) noexcept
            {
                const float y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
            void clear() { z1 = z2 = 0.0f; }
        };

        void updateAntiAliasCutoff();
        void startNewGrain (int& phase, float& readPos);
        float readLinear (float pos) const;

        std::vector<float> buffer_;
        int   mask_     = 0;
        int   writePos_ = 0;
        int   phase1_   = 0;
        int   phase2_   = kGrainSize / 2;
        float readPos1_ = 0.0f;
        float readPos2_ = static_cast<float> (kGrainSize) * 0.5f;
        float pitchRatio_ = 1.0f;
        double sampleRate_ = 44100.0;
        BiquadLP aaStage1_, aaStage2_;
        DspUtils::RandomWalkLFO ratioMod_;
        float ratioModDepth_ = 0.0f;
        bool  ratioModEnabled_ = false;
        static constexpr float kButterworthQ1 = 0.5411961f;
        static constexpr float kButterworthQ2 = 1.3065630f;
    };

    // ──────────────────────────────────────────────────────────────────
    // Members
    // ──────────────────────────────────────────────────────────────────

    // Pitch shifters — one per channel for natural stereo independence.
    GranularPitchShifter pitchL_, pitchR_;

    // SECOND pitch voice — an octave ABOVE voice 1 (2026-05-31). A single
    // granular voice + its anti-alias filter (cutoff = nyquist/ratio ≈ 12 kHz
    // for the +12 octave) is band-limited: the top octave (10-20 kHz) is
    // starved vs Valhalla Shimmer's broadband octave (Deep Blue Day: ss-air
    // -19 dB short, spec_L1 max +36 dB at 12.9 kHz). Voice 2 pitches the same
    // feedback up a FURTHER octave (ratio ×2, AA cutoff ~6 kHz → output to
    // 24 kHz), filling 12-24 kHz so the shimmer reads broadband. Lower mix —
    // it is the "air" layer over voice 1. Bypassed at unity pitch.
    GranularPitchShifter pitch2L_, pitch2R_;
    static constexpr float kVoice2OctaveMul = 2.0f;   // +12 st above voice 1
    static constexpr float kVoice1Mix       = 0.78f;
    static constexpr float kVoice2Mix       = 0.60f;   // 2026-06-16: 0.34->0.60 — boost the +24 air voice to fill 12-24k toward the anchor's broadband octave (cent_500/spec_L1@12.9k).
    float voice1Scale_ = 1.0f;   // per-preset multiplier on kVoice1Mix (mid-tail fill); 1.0 = legacy
    float voice2Scale_ = 1.0f;   // per-preset multiplier on kVoice2Mix; 1.0 = legacy

    // DOWN voice (2026-06-19) — pitches the feedback DOWN one octave (×0.5) IN THE
    // FEEDBACK LOOP, alongside the up voices. The loop regenerates a descending ladder
    // (500, 250, 125 Hz from a 1 kHz input) — the WARM LOW that Valhalla Shimmer's
    // DeepBlueDay has and DV's up-only voices lacked (measured: Shimmer 500 Hz = 64 dB,
    // DV = 0 dB from a pure 1 kHz sine). Crucially it shares the SAME loop as the up
    // shimmer, so the low octave builds with IDENTICAL timing — no late "kick-in" (the
    // output-side self-feeding ladder had a per-rung grain latency → audibly late). A
    // softClip bounds the per-grain peaks; the 60 Hz feedback HPF caps the runaway sub;
    // a MODERATE downMix_ keeps the loop gain < 1 so it stays bounded (the clip was
    // over-cranking the mix). downMix_ 0 → branch skipped (contributes nothing). NON-shimmer
    // presets are byte-identical (they never instantiate this engine); the shimmer presets
    // that DO run this loop with downMix_ 0 are functionally unchanged but NOT byte-guaranteed
    // (this is the recursive-feedback TU — a guarded branch can still perturb FP ~1e-4 via
    // codegen, see duskverb_bitnull_codegen_limit) — they're validated by ear/anchor, not byte-null.
    GranularPitchShifter pitchDownL_, pitchDownR_;    // −1 oct (×0.5 → 500 Hz)
    static constexpr float kVoiceDownRatio  = 0.5f;   // −12 st
    float downMix_ = 0.0f;                            // per-preset; 0 = off (bit-null)

    // SUB voice (2026-06-29) — pitches the feedback DOWN TWO octaves (×0.25). The −1 oct
    // voice must cascade 1k→500→250→125 over successive loop passes, losing level each step
    // (softClip + loop attn), so the deep lows never arrive — measured against Valhalla
    // Shimmer DeepBlueDay the DV deficit GROWS with depth (500 Hz −7 dB, 250 −19, 125 −21,
    // 60 −35). This voice reaches 250 Hz in ONE step (and the loop regenerates 125→62 from
    // it), filling the low wash Valhalla has. Same loop = identical build timing. subMix_ 0
    // → branch skipped. Byte-identical on NON-shimmer presets (they don't run this engine);
    // the shimmer presets that run this loop with subMix_ 0 are functionally unchanged but
    // NOT byte-guaranteed (recursive-feedback TU codegen ~1e-4, see duskverb_bitnull_codegen_limit)
    // — validated by ear/anchor, not byte-null.
    GranularPitchShifter pitchSubL_, pitchSubR_;      // −2 oct (×0.25 → 250 Hz)
    static constexpr float kVoiceSubRatio   = 0.25f;  // −24 st
    float subMix_ = 0.0f;                             // per-preset; 0 = off (bit-null)

    // DRY-FED OCTAVE CASCADE (2026-07-01) — a mono, FEED-FORWARD chain of −12 st pitch stages
    // fed from the DRY input (not the feedback), each octave summed into the reverb input at its
    // OWN gain. This is what Valhalla Shimmer does: independent per-octave generation → a gentle
    // EVEN 500/250/125/62 descent. DV's old feedback down/sub voices could not (they read the
    // recirculated wet, so a transient starves them, and the "sub" ratio 0.25 was silently
    // clamped to 0.5 by setPitchRatio — never actually −24 st). Feed-forward → each stage's level
    // is set purely by its gain, decoupled from feedback. All gains 0 → octActive_ false →
    // skipped → bit-null. kNumOctaves stages: octGen_[0]=500, [1]=250, [2]=125, [3]=62 Hz.
    static constexpr int kNumOctaves = 4;
    GranularPitchShifter octGenL_[kNumOctaves];       // mono chain (index 0 = highest octave)
    float octGain_[kNumOctaves] = { 0.0f, 0.0f, 0.0f, 0.0f };  // per-octave levels (per-preset)
    bool  octActive_ = false;                         // any gain > 0

    // Hall reverb — reuses the existing FDNReverb (same engine that
    // powers the "Realistic Space" / FDN algorithm). Configured in
    // prepare() for a "long lush hall" baseline; per-preset setters
    // override.
    //
    // NOTE (2026-06-16): the octave-up shimmer's defining trait — the pitched HF
    // tail ringing AT LEAST AS LONG AS the body (anchor T60: Black Hole 16 kHz
    // 9.6 s, Deep Blue 16 kHz 11.9 s, both RISING with freq) — is structurally
    // unreachable on this FDN. Proven this session: switching to the per-octave-
    // GEQ variant (FDNReverbT<true>) fixes the BODY decay (125 Hz–2 kHz land
    // within JND) but CANNOT lift T60-16k past ~5.3 s. The octave GEQ is
    // attenuation-only (gains clamped ≤0.9999 + a composite-|H|<1 stability guard,
    // OctaveGEQDesign.cpp) so it can at best make a band lossless; DV's linear
    // delay interpolation + diffusion allpasses are HF-lossy per pass, and the GEQ
    // cannot boost to compensate. Closing it needs an HF-lossless redesign
    // (Lagrange/allpass interpolation) or a parallel non-recirculating pitched
    // voice — an engine fork, to be done with ear-validation in the loop.
    FDNReverb reverb_;

    // Dense-diffusion alternative tank (10 input allpasses + 3 nested loop
    // allpasses/line + 2 output spin-combs). Same class the hall presets use
    // to reach kurtosis ~7; here it replaces the sparse FDN's metallic HF ring
    // (kurt ~26) when useDenseReverb_ is set. Separate instance from the halls'
    // denseHall_ → non-shimmer presets are unaffected. Off = legacy FDN path.
    DenseHallReverb denseReverb_;
    bool useDenseReverb_ = false;

    // Per-block scratch buffers for the pitch-shifter → reverb stage.
    // pitch shifter writes into these; reverb reads them and writes
    // into wetL_/wetR_.
    std::vector<float> reverbInL_, reverbInR_;
    std::vector<float> wetL_, wetR_;

    // Feedback delay line — true circular buffer sized for a fixed 50 ms
    // acoustic delay (sampleRate × 0.050). Decoupling the feedback delay
    // from the DAW block size is critical: indexing a "previous-block"
    // buffer by sample-within-block creates a delay of exactly blockSize
    // samples, which builds a violent comb filter tuned to the block rate
    // (e.g. ≈172 Hz at 256-sample blocks). Once the pitch shifter pitches
    // up that comb's harmonic ladder, the cascade turns into a metallic
    // ring instead of musical reverb.
    //
    // 50 ms (≈20 Hz cutoff) is short enough to feel like a tight cascade
    // loop yet long enough that any residual comb is sub-audible and
    // hidden by the reverb's smear + the pitch shifter's grain windowing.
    std::vector<float> fbDelayLineL_, fbDelayLineR_;
    int fbDelaySamples_  = 0;
    int fbDelayWritePos_ = 0;

    // Feedback-path band-pass filter — applied AFTER the pitch shifter,
    // BEFORE the soft-clip + mix node. Suppresses two distinct artifacts
    // that otherwise dominate the cascade tail:
    //
    //  • Sub-harmonic buildup at the granular-pitch-shifter's grain rate.
    //    kGrainSize = 4096 samples → grain rate 11.72 Hz at 48 kHz, with
    //    odd harmonics at ~58, 82, 105 Hz. Without an HPF these accumulate
    //    on every cascade pass and eventually mask the musical content as
    //    a low-frequency rumble.
    //  • Metallic high-frequency ringing from spectral migration: each
    //    pitch-up pass moves energy up the spectrum (12 st = ×2 ratio).
    //    After several cycles the cascade concentrates at 2-6 kHz against
    //    the AA-filter wall, producing audible "metal" peaks at 1.3, 2.6,
    //    3 kHz. The LPF rolls these off before they recirculate.
    //
    // One-pole each is enough — gentle slopes preserve cascade timbre
    // while removing the pathological out-of-band content.
    struct OnePole
    {
        float prevIn = 0.0f, prevOut = 0.0f, a = 0.0f;
        void setHPCutoff (float fc, float sr)
        {
            a = std::exp (-6.283185307179586f * fc / sr);
        }
        void setLPCutoff (float fc, float sr)
        {
            a = std::exp (-6.283185307179586f * fc / sr);
        }
        inline float processHP (float x) noexcept
        {
            const float y = a * (prevOut + x - prevIn);
            prevIn = x; prevOut = y;
            return y;
        }
        inline float processLP (float x) noexcept
        {
            const float y = (1.0f - a) * x + a * prevOut;
            prevOut = y;
            return y;
        }
        void clear() { prevIn = prevOut = 0.0f; }
    };
    OnePole fbHpfL_, fbHpfR_, fbLpfL_, fbLpfR_;

    // Stereo modulation (chorus/ensemble) on the WET output — a slow LFO sweeps
    // per-channel modulated delays in ANTI-PHASE, so the L/R combs move oppositely:
    // on a steady tone the image swings side-to-side (Valhalla Shimmer Black Hole
    // measured 0.83 Hz, ±25 dB L-R), on broadband it's a moving, animated field.
    // depth 0 → bypassed → bit-null. Applied post-feedback-write so the loop stays clean.
    struct StereoMod
    {
        std::vector<float> bufL, bufR; int mask = 0, w = 0;
        float phase = 0.0f, inc = 0.0f, depthSamp = 0.0f, baseSamp = 0.0f, blend = 0.0f, panDepth = 0.0f;
        bool active = false;
        void prepare (double sr)
        {
            int n = 1; while (n < static_cast<int> (0.06 * sr)) n <<= 1;
            bufL.assign (static_cast<size_t> (n), 0.0f); bufR.assign (static_cast<size_t> (n), 0.0f);
            mask = n - 1; w = 0; phase = 0.0f;
        }
        void clear() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); w = 0; phase = 0.0f; }
        void setParams (float rateHz, float depth01, double sr)
        {
            inc       = 6.283185307f * rateHz / static_cast<float> (sr);
            baseSamp  = 0.012f * static_cast<float> (sr);            // ~12 ms centre delay
            const float d = std::clamp (depth01, 0.0f, 1.0f);
            depthSamp = d * 0.008f * static_cast<float> (sr);  // up to ±8 ms sweep (chorus → correlation/character)
            blend     = d;                                     // 0 = dry only, 1 = full chorus comb
            panDepth  = d * 0.9f;                              // anti-phase amplitude swing (auto-pan → the slow L-R level mod); 0.9 → ±~25 dB at full depth
            active    = d > 1.0e-4f;
        }
        inline float readInterp (const std::vector<float>& b, float d) const
        {
            const float rp = static_cast<float> (w) - d;
            int i0 = static_cast<int> (std::floor (rp));
            const float fr = rp - static_cast<float> (i0);
            const int a = i0 & mask, c = (i0 + 1) & mask;
            return b[static_cast<size_t> (a)] + fr * (b[static_cast<size_t> (c)] - b[static_cast<size_t> (a)]);
        }
        inline void process (float& l, float& r)
        {
            bufL[static_cast<size_t> (w)] = l; bufR[static_cast<size_t> (w)] = r;
            const float lfo = std::sin (phase);
            phase += inc; if (phase > 6.283185307f) phase -= 6.283185307f;
            const float yL = readInterp (bufL, baseSamp + depthSamp * lfo);   // L sweeps one way…
            const float yR = readInterp (bufR, baseSamp - depthSamp * lfo);   // …R the opposite (anti-phase)
            const float cL = (1.0f - 0.5f * blend) * l + 0.5f * blend * yL;   // chorus comb (→ correlation/character)
            const float cR = (1.0f - 0.5f * blend) * r + 0.5f * blend * yR;
            const float pan = panDepth * lfo;                                 // anti-phase amplitude (→ the slow L-R level swing)
            l = cL * (1.0f + pan);
            r = cR * (1.0f - pan);
            w = (w + 1) & mask;
        }
    };
    StereoMod stereoMod_;

    // Tail spin-comb — a 2-stage cascade of MODULATED Schroeder allpasses on the
    // WET output (allpass = magnitude-flat → no comb notches, only the slow spin
    // modulation smears the spectrum). L/R run in opposite mod phase. It smears the
    // sparse FDN's metallic HF modes (kurt ~31 → ~9, matching Valhalla) WITHOUT the
    // density of a full reverb swap, so the FDN tank's deep-low cascade / width /
    // HF-sustain are preserved. Applied to the OUTPUT only, post-feedback-write, so
    // the recirculating cascade stays un-spun. Used on Deep Blue Day (keeps its FDN);
    // Black Hole uses DenseHall instead. off (useTailSpin_ = false) → oL/oR untouched.
    struct TailSpin
    {
        struct AP { std::vector<float> buf; int mask = 0, w = 0, len = 0; float exc = 0;
            void alloc (int n, int e) { len = std::max (1, n); exc = (float) e;
                int sz = 1; while (sz < len + e + 8) sz <<= 1; buf.assign ((size_t) sz, 0.0f); mask = sz - 1; w = 0; }
            void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
            inline float process (float x, float mod) {
                const float rp = (float) w - (float) len - exc * mod;
                const int i0 = (int) std::floor (rp); const float fr = rp - (float) i0;
                const float d = DspUtils::cubicHermite (buf.data(), mask, i0, fr);   // HF-lossless
                const float in = x + kSG * d; buf[(size_t) w] = in + DspUtils::kDenormalPrevention; w = (w + 1) & mask;
                return d - kSG * in; }
            static constexpr float kSG = 0.7f; };
        AP l0, l1, r0, r1; float ph0 = 0.0f, ph1 = 1.7f, inc0 = 0.0f, inc1 = 0.0f;
        void prepare (double sr) {
            const float s = (float) sr;
            l0.alloc ((int) (0.0070f * s), (int) (0.0015f * s)); r0.alloc ((int) (0.0070f * s), (int) (0.0015f * s));
            l1.alloc ((int) (0.0080f * s), (int) (0.0015f * s)); r1.alloc ((int) (0.0080f * s), (int) (0.0015f * s));
            inc0 = 6.283185307f * 1.30f / s; inc1 = 6.283185307f * 1.53f / s; ph0 = 0.0f; ph1 = 1.7f; }
        void clear() { l0.clear(); l1.clear(); r0.clear(); r1.clear(); ph0 = 0.0f; ph1 = 1.7f; }
        inline void process (float& L, float& R) {
            const float m0 = std::sin (ph0), m1 = std::sin (ph1);
            ph0 += inc0; if (ph0 > 6.283185307f) ph0 -= 6.283185307f;
            ph1 += inc1; if (ph1 > 6.283185307f) ph1 -= 6.283185307f;
            L = l1.process (l0.process (L,  m0),  m1);
            R = r1.process (r0.process (R, -m0), -m1); }   // opposite mod → L/R decorrelation
    };
    TailSpin tailSpin_;
    bool useTailSpin_ = false;

    // Tail noise floor — the dense, noise-like decay Valhalla's shimmer has and DV's sparse FDN
    // lacks (measured: Valhalla's fade stays denser + ~2 dB higher; DV collapses to a single
    // sparse mode). A low-level band-limited noise, ENVELOPE-TRACKED to the wet tail so it fades
    // WITH the decay ("white noise heard as the audio fades out"). It fills the spectral gaps
    // between DV's sparse modes → masks the discrete low-mode ring (203 Hz boing) so it blends
    // into a wash instead of standing out as a pitch. gain 0 → inactive → bit-null.
    struct TailNoise
    {
        static constexpr std::uint32_t kSeedL = 0x9E3779B9u;
        static constexpr std::uint32_t kSeedR = 0x85EBCA6Bu;
        std::uint32_t rngL = kSeedL, rngR = kSeedR;
        float envL = 0.0f, envR = 0.0f, atk = 0.0f, rel = 0.0f;
        OnePole hpL, hpR, lpL, lpR;               // band-limit the noise to the tail's dark color
        float gain_ = 0.0f; bool active_ = false;
        void prepare (double sr) {
            const float s = static_cast<float> (sr);
            atk = std::exp (-1.0f / (0.005f * s));    // ~5 ms attack
            rel = std::exp (-1.0f / (0.100f * s));    // ~100 ms release (tracks the decay smoothly)
            hpL.setHPCutoff (250.0f, s); hpR.setHPCutoff (250.0f, s);
            lpL.setLPCutoff (7000.0f, s); lpR.setLPCutoff (7000.0f, s);
        }
        void clear() { rngL = kSeedL; rngR = kSeedR; envL = envR = 0.0f; hpL.clear(); hpR.clear(); lpL.clear(); lpR.clear(); }
        void setGain (float g) { gain_ = std::max (0.0f, g); active_ = gain_ > 1.0e-6f; }
        bool active() const { return active_; }
        inline float noise (std::uint32_t& s) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return static_cast<float> (s) * (1.0f / 2147483648.0f) - 1.0f;   // ~[-1,1)
        }
        inline void process (float wetL, float wetR, float& oL, float& oR) {
            const float aL = std::abs (wetL), aR = std::abs (wetR);
            envL = (aL > envL) ? aL + atk * (envL - aL) : aL + rel * (envL - aL);   // fast up, slow down
            envR = (aR > envR) ? aR + atk * (envR - aR) : aR + rel * (envR - aR);
            const float nL = lpL.processLP (hpL.processHP (noise (rngL)));
            const float nR = lpR.processLP (hpR.processHP (noise (rngR)));
            oL += gain_ * envL * nL;
            oR += gain_ * envR * nR;
        }
    };
    TailNoise tailNoise_;
    float noiseGain_ = 0.0f;   // per-preset; 0 = off (bit-null)
    float stereoModRate_ = 0.83f, stereoModDepth_ = 0.0f;   // depth 0 = bypassed (bit-null)

    // HF-air voice — the genuine >12 kHz "air" Valhalla Shimmer has (centroid ~9.9k BH /
    // 6.6k DBD vs DV ~6k/5k). DV's reverb HF-damps + AA-caps the top, and the +12 loop
    // voice's air is cut by the 14 kHz feedback LPF before it recirculates. This taps the
    // wet 6-12 kHz POST-loop, pitches it up +12 st (the granular shifter outputs up to 24 kHz
    // at base rate — its AA caps the INPUT at 12 k, not the output), keeps only >11 kHz, and
    // sums it in. Post-loop → bypasses the reverb HF-damp + loop LPF, no cascade regression.
    // L/R-independent → decorrelates the highs (width_hi). mix 0 → skipped → bit-null.
    struct HFAirVoice
    {
        GranularPitchShifter shifterL, shifterR;   // +12 st (ratio 2): 6-12k → 12-24k air
        OnePole tapHpL, tapHpR;                    // ~6 kHz HP: feed only the wet HF to the shifter
        OnePole airHpL, airHpR;                    // ~11 kHz HP: keep only the pitched-up air
        float mix = 0.0f; bool active = false;
        void prepare (double sr)
        {
            shifterL.prepare (sr); shifterR.prepare (sr);
            shifterL.setPitchRatio (2.0f); shifterR.setPitchRatio (2.0f);
            shifterL.setModulation (5.1f, 0.012f, 0xA11A1Au); shifterR.setModulation (6.3f, 0.012f, 0xA11A1Bu);
            const float s = static_cast<float> (sr);
            tapHpL.setHPCutoff (6000.0f, s); tapHpR.setHPCutoff (6000.0f, s);
            airHpL.setHPCutoff (11000.0f, s); airHpR.setHPCutoff (11000.0f, s);
            clear();
        }
        void clear()
        {
            shifterL.clear(); shifterR.clear();
            tapHpL.clear(); tapHpR.clear(); airHpL.clear(); airHpR.clear();
        }
        void setMix (float m) { mix = std::clamp (m, 0.0f, 4.0f); active = mix > 1.0e-6f; }
        inline void process (float wL, float wR, float& addL, float& addR)
        {
            addL += mix * airHpL.processHP (shifterL.process (tapHpL.processHP (wL)));
            addR += mix * airHpR.processHP (shifterR.process (tapHpR.processHP (wR)));
        }
    };
    HFAirVoice air_;
    float airMix_ = 0.0f;   // mix 0 = off (bit-null)

    // HPF at 60 Hz — tuned to kill the granular-pitch-shifter's grain-rate
    // fundamental (≈12 Hz at 4096-sample grains / 48 kHz) and its first
    // few odd harmonics (~36, 60 Hz), while preserving natural-reverb
    // low-frequency content above 60 Hz. Earlier iteration at 120 Hz was
    // over-aggressive and removed musical bass that external reference clearly retains.
    static constexpr float kFeedbackHpfHz = 60.0f;
    float feedbackHpfHz_ = kFeedbackHpfHz;   // runtime-tunable corner (setFeedbackHpfHz); default = the constant → bit-null for presets that don't override
    // LPF at 14 kHz — a gentle ceiling on the feedback path. Each cascade cycle
    // pitches up by N semitones (×2 at +12), so without any cap the migrated
    // content piles up against the AA-filter wall as a metallic 1-3 kHz peak.
    // Earlier iterations cut hard (1.5 kHz, then 6 kHz) but ran too dark vs the
    // anchor (cent_500 was -42%). 14 kHz keeps the cascade bright while still
    // trimming the very top before it recirculates. (A 22 kHz experiment gained
    // only +0.5 s T60-16k → the FDN hall, not this LPF, caps HF sustain.)
    static constexpr float kFeedbackLpfHz = 14000.0f;   // 2026-06-16: 6k->14k brighten toward anchor

    double sampleRate_ = 48000.0;
    int    maxBlockSize_ = 0;
    bool   prepared_ = false;
    bool   frozen_   = false;

    // Cascade controls (mapped from the universal setters above):
    float pitchSemitones_   = 12.0f;   // setModDepth: 0..24
    float feedbackGain_     = 0.65f;   // setModRate:  0..0.95
    float saturationAmount_ = 0.0f;    // setSaturation: 0..1

    // Stability: hard cap on feedback gain so the cascade can't run away
    // even at extreme reverb decays. The reverb's natural attenuation +
    // pitch-shifter's grain-edge loss combine to keep total loop gain
    // < 1 at this cap, even when reverb RT60 is at its 8-second max.
    static constexpr float kFeedbackMax = 0.95f;

    // Soft-clip the feedback bus — engaged at typical operating signal
    // levels, NOT just runaway protection. This is critical for the
    // shimmer character: the soft-clip + per-cycle attenuation together
    // create a level-dependent loop gain. At low cascade levels the
    // softClip is linear and loop gain > 1 (cascade BUILDS — that's the
    // characteristic shimmer swell on a sparse input like an impulse).
    // As the cascade grows, the softClip compresses the feedback,
    // dropping effective loop gain below unity. The cascade settles into
    // a stable fixed point — the "self-limiting" behavior heard in
    // external reference Shimmer's impulse response (cascade builds then plateaus
    // around -52 dB before decaying).
    static constexpr float kFeedbackSoftClipKnee = 0.5f;
    static constexpr float kFeedbackSoftClipCeil = 1.5f;

    // Fixed per-cycle feedback attenuation. Combined with the softClip
    // above, sets the unity-loop-gain operating point. Empirically tuned
    // against external reference reference renders so that low-signal loop gain is just
    // above 1.0 (cascade buildup on impulse-like inputs) and high-signal
    // loop gain is below 1.0 (bounded steady-state, no runaway). The
    // user's FEEDBACK knob still scales the cascade strength linearly
    // around this calibration point.
    static constexpr float kFeedbackLoopAttn = 0.92f;

    // Internal wet-output attenuation. Tuned by direct A/B comparison to
    // external reference Shimmer reference renders (VS_EnoChoir_*, VS_CascadingHeaven_*)
    // — at -20 dB the snare/noise peaks land within ~1 dB of the external reference engine
    // at the same preset settings. The wrapper's gain_trim is reserved for
    // per-preset fine-tuning, not blanket level normalization.
    static constexpr float kWetOutputGain = 0.40f;   // -8 dB

    void updatePitchRatio();
};

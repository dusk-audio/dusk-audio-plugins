#pragma once

#include "DspUtils.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace DspUtils {

// =============================================================================
// VintageTankEngine — first-principles Griesinger / Lexicon tank topology
//
// Replaces the 16-line Hadamard FDN whose unitary feedback matrix scatters
// energy instantly. Instead implements a recirculating, modulated all-pass
// figure-8 loop that builds modal density and lateral bloom over time —
// the architecture all classic 80s/90s reverb hardware uses.
//
// Signal path:
//
//   Input (L+R)/2
//        │
//        ▼
//   [Input Diffusion: 4 static APs in series, increasing delay]
//        │
//        ▼ diffused
//   ┌────┴─────┐
//   │ Branch L │   Branch R (symmetric, asymmetric delays for modal density)
//   │          │
//   │ + xfb_R  │   + xfb_L          ← cross-coupled feedback from sister branch
//   │ ModAP 0  │   ModAP 0          ← LFO-modulated all-pass (slow lush motion)
//   │ DelayP   │   DelayP           ← primary static delay (room size base)
//   │ ModAP 1  │   ModAP 1          ← second mod AP (different LFO rate)
//   │ DelayS   │   DelayS           ← secondary delay (asymmetric — modal Δ)
//   │ Damp LP  │   Damp LP          ← in-loop damping BEFORE cross-coupling
//   │   │      │     │              ← state feeds next sample's other branch
//   └───┴──────┘─────┴──┐
//                       ▼ sparse output taps from primary + secondary lines
//                  L_out / R_out (interleaved cross-taps)
//
// All-allocation-free on the audio thread. Buffers sized in prepare().
// Per-sample cost: ~25 multiplies + 4 sin() per stereo sample.
// =============================================================================
class VintageTankEngine
{
public:
    // ─── Lifecycle ───────────────────────────────────────────────────────────
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void clearBuffers();

    // ─── Audio-thread process (allocation-free, stereo in-place) ─────────────
    void processBlock (float* L, float* R, int numSamples);

    // ─── Voicing setters (call from message thread / preset apply) ───────────
    void setDecay         (float decaySeconds);   // RT60-style target
    void setDamping       (float hz);             // legacy 1-pole cutoff — now no-op (replaced by 3-band)
    void setModRate       (float hz);             // no-op (per-LFO rates set explicitly)
    void setModDepth      (float samples);        // mod excursion in samples
    void setInputDiffusion(float coef);           // 0..0.85 (input APs)
    void setTankDiffusion (float coef);           // 0..0.85 (loop APs)
    void setCrossCoupling (float amount);         // 0..1 — L↔R feedback share

    // ─── 3-band in-loop damping matrix ───────────────────────────────────────
    // Replaces the single 1-pole LP. Each branch's feedback path passes
    // through a cascade of [low-shelf @ lowFc] → [peaking @ midFc, Q=midQ]
    // → [high-shelf @ highFc] BEFORE the figure-8 cross-coupling matrix.
    //
    // bassMultiply / midMultiply / trebleMultiply are 0.5..2.0-ish ratios
    // (matching the existing FDN-engine convention). 1.0 = unity (band
    // bypassed); >1.0 boosts band — longer decay in that region; <1.0 cuts
    // band — faster decay. Internally mapped to shelf gain in dB:
    //   gainDb = 20 × log10 (multiplier)
    void setBassMultiply   (float multiplier);
    void setMidMultiply    (float multiplier);
    void setTrebleMultiply (float multiplier);
    void setHiCut          (float hz);            // high-shelf corner
    void setLowCrossover   (float hz);            // low-shelf corner

    // ─── Post-tank output gain (linear multiplier, separate from tap scale) ──
    // Lets the engine glue normalise wet output to anchor level without
    // perturbing internal tap math (which controls cross-correlation
    // structure between channels). Default 1.0 = pass-through.
    void setOutputGain (float linear);

    // ─── Per-LFO rate (un-hardcode the modulation matrix) ────────────────────
    // index 0..3 → lfo_[index]. Default seed values applied in prepare()
    // remain the sorted hardware-matched rates, but the staged_tuner can
    // override each LFO independently via APVTS.
    void setLFORate (int index, float hz);

    // ─── Engine-glue aliases (match DuskVerbEngine forwarder names) ──────────
    void setDecayTime    (float decaySeconds) { setDecay (decaySeconds); }
    void setLoopDamping  (float hz)           { setHiCut (hz); }

    // setSize scales the EFFECTIVE delay-line read positions (tap & feedback
    // delay reads) by a multiplier sizeScale ∈ [0.25, 1.00]. Buffer
    // allocation stays at full prepared length so this is allocation-free.
    // 1.0 = full reference room; 0.25 = small box. Smoothly tunable from
    // the audio thread (next sample uses the new sizeScale_).
    void setSize         (float normalized);

    // Stereo input variant of process — kept as a thin convenience for the
    // DuskVerbEngine dispatch site (which works in juce::AudioBuffer terms).
    void process (juce::AudioBuffer<float>& buffer);

private:
    // ─── Internal building blocks (kept private, explicit, no matrices) ─────

    // Power-of-two-sized circular delay line. Read by integer or fractional
    // sample count, write per sample, advance per sample. No allocation
    // outside prepare(); mask ensures branchless index wrap.
    struct DelayLine
    {
        std::vector<float> buf;
        int writeIdx = 0;
        int mask = 0;

        void prepare (int maxLenSamples);
        void clear();

        inline float readInt  (int   d) const noexcept;
        inline float readFrac (float d) const noexcept;   // linear interp
        inline void  write    (float v)       noexcept;
        inline void  advance()                noexcept { writeIdx = (writeIdx + 1) & mask; }
    };

    // Schroeder 1-stage all-pass with STATIC delay length. Diffuses transients
    // without changing magnitude response. Cascade 4 of these = high-density
    // input diffuser per the Dattorro plate convention.
    struct AllPassStatic
    {
        DelayLine line;
        int   delay = 0;
        float coef  = 0.5f;

        void  prepare (int delayLenSamples);
        inline float process (float x) noexcept;
    };

    // Schroeder 1-stage all-pass with LFO-MODULATED delay length. Each branch
    // gets 2 of these, each driven by an independent slow LFO. Modulating
    // the delay length inside an all-pass produces lush pitch-stable motion
    // (the magnitude is flat at every instant; only phase wobbles), unlike
    // modulating a plain delay which gives Doppler shift.
    struct AllPassModulated
    {
        DelayLine line;
        float baseDelay = 0.0f;       // center of mod sweep (samples)
        float coef      = 0.5f;

        void  prepare (float baseDelaySamples, int maxExcursionSamples);
        inline float process (float x, float modSamples) noexcept;
    };

    // 1-pole low-pass damping filter — kept as a fallback / silent member,
    // no longer wired into the process loop (replaced by ThreeBandDamper).
    struct OnePoleLP
    {
        float a = 1.0f;
        float z = 0.0f;

        void  setCutoff (float hz, float sampleRate) noexcept;
        inline float process (float x) noexcept;
    };

    // 3-band damping matrix — cascade of LowShelf + Peaking + HighShelf
    // biquads. Sits inside each branch's feedback path BEFORE the figure-8
    // cross-coupling sum, so each pass through the loop can lift / cut
    // bass / mid / treble band gain independently — exactly what the
    // FDN's Bass/Mid/Treble Multiply convention exposes.
    struct ThreeBandDamper
    {
        DspUtils::LowShelfBand   lowShelf;
        DspUtils::ParametricBand peakingMid;
        DspUtils::HighShelfBand  highShelf;

        // Cached shape state — coefficients recompute on any setter call
        // so per-band gain can be tweaked independently.
        float lowFc      = 250.0f;
        float midFc      = 1000.0f;
        float midQ       = 0.7f;
        float highFc     = 4000.0f;
        float bassDb     = 0.0f;
        float midDb      = 0.0f;
        float trebleDb   = 0.0f;

        void  prepare (float sampleRate) noexcept;
        void  reset()                    noexcept;
        void  applyCoeffs()              noexcept;

        inline float processL (float x) noexcept
        {
            x = lowShelf.processL (x);
            x = peakingMid.processL (x);
            x = highShelf.processL (x);
            return x;
        }
        inline float processR (float x) noexcept
        {
            x = lowShelf.processR (x);
            x = peakingMid.processR (x);
            x = highShelf.processR (x);
            return x;
        }
    };

    // Pre-computed-increment sine LFO. 4 of these, each independent rate and
    // starting phase so the 4 mod APs sweep non-coincidently.
    struct LFO
    {
        float phase = 0.0f;
        float incr  = 0.0f;
        float depth = 0.0f;

        void  setRate  (float hz, float sampleRate) noexcept;
        inline float tick() noexcept;  // returns sin(phase) × depth
    };

    // ─── Topology layout ─────────────────────────────────────────────────────
    static constexpr int kNumInputAPs       = 4;     // series diffusion cascade
    static constexpr int kNumModAPsPerChan  = 2;     // per branch
    static constexpr int kNumChannels       = 2;     // figure-8 (L + R)
    static constexpr int kNumLFOs           = kNumChannels * kNumModAPsPerChan;
    static constexpr int kOutputTapsPerLine = 3;     // sparse taps per delay

    // Input diffusion cascade — 4 static APs in series.
    std::array<AllPassStatic, kNumInputAPs> inputAP_;

    // One tank branch per channel. Mod APs interleaved with static delays
    // is the canonical Griesinger order (AP → Delay → AP → Delay → Damp).
    struct TankBranch
    {
        std::array<AllPassModulated, kNumModAPsPerChan> modAP;
        DelayLine primaryDelay;       // long base-room-size delay
        DelayLine secondaryDelay;     // shorter delay, adds modal density
        OnePoleLP dampingLP;           // broadband baseline damping floor
        ThreeBandDamper damper;        // 3-band damping matrix on top of baseline
        int primaryLen   = 0;
        int secondaryLen = 0;

        // Sparse output taps — 3 fractional positions along each delay
        // line, asymmetric ratios so the comb-filter pattern is irregular
        // and the late tail accumulates naturally.
        std::array<int, kOutputTapsPerLine> tapPrimary   { 0, 0, 0 };
        std::array<int, kOutputTapsPerLine> tapSecondary { 0, 0, 0 };
    };
    std::array<TankBranch, kNumChannels> branch_;

    // 4 independent LFOs (2 per branch), staggered rates + phases so no two
    // mod APs sweep coincidently — keeps modal density rolling instead of
    // pulsing in sync.
    std::array<LFO, kNumLFOs> lfo_;

    // Feedback state — last sample of each branch's loop output, fed into
    // the OTHER branch's input on the next sample. This is the figure-8.
    float lastBranchOut_[kNumChannels] { 0.0f, 0.0f };

    // ─── Voicing state ───────────────────────────────────────────────────────
    float sampleRate_     = 48000.0f;
    float decayGain_      = 0.70f;
    float dampingHz_      = 6000.0f;
    float modRateBaseHz_  = 1.0f;
    float modDepthSamples_= 8.0f;
    float inputDiffCoef_  = 0.625f;
    float tankDiffCoef_   = 0.625f;
    float crossCoupling_  = 0.50f;
    float sizeScale_      = 1.00f;     // 0.25..1.00 — multiplies effective delay reads
    float outputGain_     = 0.74f;     // post-tank linear multiplier (wet exit gate)
    float bassMult_       = 1.00f;     // → low-shelf gain (× → dB)
    float midMult_        = 1.00f;     // → peaking-mid gain
    float trebleMult_     = 1.00f;     // → high-shelf gain
    float lowCrossover_   = 250.0f;    // 3-band damper low-shelf corner
    float hiCutHz_        = 4000.0f;   // 3-band damper high-shelf corner
};

// ─── Inline hot-path bodies ──────────────────────────────────────────────────
// Placed here (not in .cpp) so the optimizer inlines them inside the per-
// sample loop without LTO. Keeps the audio path one tight function call.

inline float VintageTankEngine::DelayLine::readInt (int d) const noexcept
{
    return buf[static_cast<size_t> ((writeIdx - d) & mask)];
}

inline float VintageTankEngine::DelayLine::readFrac (float d) const noexcept
{
    const int   d0   = static_cast<int> (d);
    const float frac = d - static_cast<float> (d0);
    const float a    = buf[static_cast<size_t> ((writeIdx - d0)     & mask)];
    const float b    = buf[static_cast<size_t> ((writeIdx - d0 - 1) & mask)];
    return a + frac * (b - a);
}

inline void VintageTankEngine::DelayLine::write (float v) noexcept
{
    buf[static_cast<size_t> (writeIdx)] = v;
}

inline float VintageTankEngine::AllPassStatic::process (float x) noexcept
{
    // Standard Schroeder one-stage AP:
    //   v = x + g * z^{-D}(v)
    //   y = z^{-D}(v) - g * v
    const float delayed = line.readInt (delay);
    const float v       = x + coef * delayed;
    const float y       = delayed - coef * v;
    line.write (v);
    line.advance();
    return y;
}

inline float VintageTankEngine::AllPassModulated::process (float x, float modSamples) noexcept
{
    // Mod-AP: same Schroeder form, fractional delay length swept by LFO.
    // Magnitude response stays flat (true all-pass); only phase wobbles →
    // pitch-stable lush motion vs. Doppler-shifted plain-delay mod.
    const float d       = std::max (1.0f, baseDelay + modSamples);
    const float delayed = line.readFrac (d);
    const float v       = x + coef * delayed;
    const float y       = delayed - coef * v;
    line.write (v);
    line.advance();
    return y;
}

inline float VintageTankEngine::OnePoleLP::process (float x) noexcept
{
    z += a * (x - z);
    return z;
}

inline float VintageTankEngine::LFO::tick() noexcept
{
    static constexpr float kTwoPi = 6.283185307179586f;
    phase += incr;
    if (phase >= kTwoPi) phase -= kTwoPi;
    return std::sin (phase) * depth;
}

} // namespace DspUtils

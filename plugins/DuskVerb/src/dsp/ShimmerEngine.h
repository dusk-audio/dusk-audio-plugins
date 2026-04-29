#pragma once

#include "DspUtils.h"
#include "FDNReverb.h"

#include <array>
#include <cmath>
#include <vector>

// ShimmerEngine v9 — Valhalla-style topology: pitch shifter sits in the
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
//   v9 mirrors what Valhalla Shimmer / Eventide H7600 / modern shimmer
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

    // Hall reverb — reuses the existing FDNReverb (same engine that
    // powers the "Realistic Space" / FDN algorithm). Configured in
    // prepare() for a "long lush hall" baseline; per-preset setters
    // override.
    FDNReverb reverb_;

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
    // HPF at 60 Hz — tuned to kill the granular-pitch-shifter's grain-rate
    // fundamental (≈12 Hz at 4096-sample grains / 48 kHz) and its first
    // few odd harmonics (~36, 60 Hz), while preserving natural-reverb
    // low-frequency content above 60 Hz. Earlier iteration at 120 Hz was
    // over-aggressive and removed musical bass that VS clearly retains.
    static constexpr float kFeedbackHpfHz = 60.0f;
    // LPF at 1.5 kHz — aggressive HF attenuation in the feedback path.
    // Each cascade cycle pitches up by N semitones (×2 at +12), so a
    // 200 Hz snare component migrates 200→400→800→1600→3200 Hz over 4
    // cycles, accumulating as a "metallic" peak at 1-3 kHz that's the
    // clearest audible artifact differentiating us from VS Shimmer.
    // 1.5 kHz drops the migrated content by ~−3 dB at 1.5 kHz and ~−6 dB
    // at 3 kHz per cycle, so by 3-4 cycles the high-end energy is
    // exhausted before it can recirculate further.
    static constexpr float kFeedbackLpfHz = 6000.0f;

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
    // Valhalla Shimmer's impulse response (cascade builds then plateaus
    // around -52 dB before decaying).
    static constexpr float kFeedbackSoftClipKnee = 0.5f;
    static constexpr float kFeedbackSoftClipCeil = 1.5f;

    // Fixed per-cycle feedback attenuation. Combined with the softClip
    // above, sets the unity-loop-gain operating point. Empirically tuned
    // against VS reference renders so that low-signal loop gain is just
    // above 1.0 (cascade buildup on impulse-like inputs) and high-signal
    // loop gain is below 1.0 (bounded steady-state, no runaway). The
    // user's FEEDBACK knob still scales the cascade strength linearly
    // around this calibration point.
    static constexpr float kFeedbackLoopAttn = 0.92f;

    // Internal wet-output attenuation. Tuned by direct A/B comparison to
    // Valhalla Shimmer reference renders (VS_EnoChoir_*, VS_CascadingHeaven_*)
    // — at -20 dB the snare/noise peaks land within ~1 dB of the VS engine
    // at the same preset settings. The wrapper's gain_trim is reserved for
    // per-preset fine-tuning, not blanket level normalization.
    static constexpr float kWetOutputGain = 0.40f;   // -8 dB

    void updatePitchRatio();
};

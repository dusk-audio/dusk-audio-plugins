#include "ShimmerEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr float kTwoPi = 6.283185307179586f;

    // Hadamard 8×8 sequency-ordered ±1 entries. Multiply each by 1/√8 at
    // mix time. Each row is orthogonal to every other row.
    constexpr int kHadamardSign[8][8] = {
        { +1, +1, +1, +1, +1, +1, +1, +1 },
        { +1, -1, +1, -1, +1, -1, +1, -1 },
        { +1, +1, -1, -1, +1, +1, -1, -1 },
        { +1, -1, -1, +1, +1, -1, -1, +1 },
        { +1, +1, +1, +1, -1, -1, -1, -1 },
        { +1, -1, +1, -1, -1, +1, -1, +1 },
        { +1, +1, -1, -1, -1, -1, +1, +1 },
        { +1, -1, -1, +1, -1, +1, +1, -1 },
    };
}

// ============================================================================
// GranularPitchShifter — overlapping-grain pitch shift
// ============================================================================

void ShimmerEngine::GranularPitchShifter::prepare()
{
    // Buffer = 8 × grain size for plenty of lookback headroom across the
    // 1.0–4.0 pitch-ratio range. At ratio 4.0 (+2 oct), each grain consumes
    // 4 × kGrainSize input samples per kGrainSize output samples → max
    // lookback needed ≈ 3 × kGrainSize.
    const int bufSize = DspUtils::nextPowerOf2 (8 * kGrainSize);
    buffer_.assign (static_cast<size_t> (bufSize), 0.0f);
    mask_ = bufSize - 1;
    clear();
}

void ShimmerEngine::GranularPitchShifter::clear()
{
    std::fill (buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    phase1_   = 0;
    phase2_   = kGrainSize / 2;
    readPos1_ = 0.0f;
    readPos2_ = static_cast<float> (kGrainSize) * 0.5f;
}

void ShimmerEngine::GranularPitchShifter::setPitchRatio (float ratio)
{
    pitchRatio_ = std::clamp (ratio, 0.5f, 4.0f);
}

void ShimmerEngine::GranularPitchShifter::startNewGrain (int& phase, float& readPos)
{
    phase = 0;
    // Look back enough samples that the new grain won't catch the write
    // head before its next reset. Headroom of 64 samples to avoid edge cases
    // when pitchRatio_ moves under automation.
    const float lookback = std::ceil ((pitchRatio_ - 1.0f) * static_cast<float> (kGrainSize)) + 64.0f;
    readPos = static_cast<float> (writePos_) - lookback;
}

float ShimmerEngine::GranularPitchShifter::readLinear (float pos) const
{
    const int   intPos  = static_cast<int> (std::floor (pos));
    const float frac    = pos - static_cast<float> (intPos);
    const int   idx0    = intPos & mask_;
    const int   idx1    = (intPos + 1) & mask_;
    return buffer_[static_cast<size_t> (idx0)] * (1.0f - frac)
         + buffer_[static_cast<size_t> (idx1)] *         frac;
}

float ShimmerEngine::GranularPitchShifter::process (float input)
{
    // Write current input to buffer.
    buffer_[static_cast<size_t> (writePos_)] = input + DspUtils::kDenormalPrevention;

    // Read both grains with linear interpolation.
    const float v1 = readLinear (readPos1_);
    const float v2 = readLinear (readPos2_);

    // Hann window per grain. Values at integer phase positions are pre-
    // computed (could be cached as a LUT later if profiling demands it).
    const float p1 = static_cast<float> (phase1_) / static_cast<float> (kGrainSize);
    const float p2 = static_cast<float> (phase2_) / static_cast<float> (kGrainSize);
    const float w1 = 0.5f - 0.5f * std::cos (kTwoPi * p1);
    const float w2 = 0.5f - 0.5f * std::cos (kTwoPi * p2);

    const float out = v1 * w1 + v2 * w2;

    // Advance write head, both read heads (at pitch ratio), both phases.
    writePos_ = (writePos_ + 1) & mask_;
    readPos1_ += pitchRatio_;
    readPos2_ += pitchRatio_;
    ++phase1_;
    ++phase2_;

    // Reset grains when phase wraps. Two grains stay 50 % out of phase so
    // their Hann windows crossfade — at any moment exactly one is at peak
    // window value while the other is fading, summing to a constant 1.0
    // window envelope across grain boundaries.
    if (phase1_ >= kGrainSize) startNewGrain (phase1_, readPos1_);
    if (phase2_ >= kGrainSize) startNewGrain (phase2_, readPos2_);

    return out;
}

// ============================================================================
// DelayLine helpers
// ============================================================================

void ShimmerEngine::DelayLine::allocate (int maxSamples)
{
    const int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 8, 64));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

void ShimmerEngine::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// ============================================================================
// ShimmerEngine
// ============================================================================

void ShimmerEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    constexpr float kMaxSizeScale = 1.5f;
    const float rateRatio = static_cast<float> (sampleRate / 44100.0);

    for (int i = 0; i < kNumChannels; ++i)
    {
        const int reserve = static_cast<int> (
            static_cast<float> (kBaseDelays[i]) * kMaxSizeScale * rateRatio + 16.0f);
        delays_[i].allocate (reserve);
    }

    pitchL_.prepare();
    pitchR_.prepare();

    // ── Init the 8 in-loop LFOs with distinct seeds AND distinct rates.
    // Distinct rates (0.20, 0.27, 0.35, 0.42, 0.50, 0.57, 0.64, 0.70 Hz)
    // ensure no two delays beat against each other — independent slow drift
    // per channel produces the decorrelated wash that real Eno shimmer has.
    // Depth = 0.8 % of each delay's length. Originally 0.3 %, bumped after
    // VV comparison showed Valhalla's centroid spreads to fill the entire
    // band up to hi-cut while ours stayed narrow at ~3 kHz; the wider modulation
    // forces the granular pitch shifter to choose more random splice points,
    // spreading sidebands per the Valhalla 2010 decorrelation writeup. Above
    // ~1 % the modulation becomes audible as pitch warble on sustained tones.
    static constexpr float kLfoRatesHz[8]  = { 0.20f, 0.27f, 0.35f, 0.42f, 0.50f, 0.57f, 0.64f, 0.70f };
    static constexpr std::uint32_t kSeeds[8] = {
        0xC0FFEEu, 0xBADBEEFu, 0xDEADBEEFu, 0xFEEDFACEu,
        0xCAFEBABEu, 0xABCDEFu, 0x12345678u, 0x87654321u
    };
    for (int i = 0; i < kNumChannels; ++i)
    {
        lfos_[static_cast<size_t> (i)].prepare (static_cast<float> (sampleRate), kSeeds[i]);
        lfos_[static_cast<size_t> (i)].setRate  (kLfoRatesHz[i]);
        // Depth set below in updateDelays() — needs delaySamples populated first.
    }

    updateDelays();   // populates delaySamples_ AND sets LFO depths to 0.8 % of each
    updateFeedback();
    updateDamping();
    updatePitchRatio();

    prepared_ = true;
}

void ShimmerEngine::clearBuffers()
{
    for (auto& d : delays_) d.clear();
    pitchL_.clear();
    pitchR_.clear();
    dampStateOutL_ = 0.0f;
    dampStateOutR_ = 0.0f;
}

// ── Universal setters ──────────────────────────────────────────────────────

void ShimmerEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, seconds);
    if (prepared_) updateFeedback();
}

void ShimmerEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelays();
        updateFeedback();
    }
}

void ShimmerEngine::setBassMultiply  (float mult) { bassMult_ = std::clamp (mult, 0.1f, 4.0f); }
void ShimmerEngine::setMidMultiply   (float mult) { midMult_  = std::clamp (mult, 0.1f, 4.0f); }

void ShimmerEngine::setTrebleMultiply (float mult)
{
    trebleMult_ = std::clamp (mult, 0.05f, 4.0f);
    if (prepared_) updateDamping();
}

void ShimmerEngine::setCrossoverFreq     (float hz) { crossoverHz_     = std::clamp (hz,  100.0f,  8000.0f); }
void ShimmerEngine::setHighCrossoverFreq (float hz) { highCrossoverHz_ = std::clamp (hz, 1000.0f, 12000.0f); }
void ShimmerEngine::setSaturation        (float a)  { saturationAmount_ = std::clamp (a, 0.0f, 1.0f); }

void ShimmerEngine::setModDepth (float depth)
{
    // Hijack: 0..1 → 0..24 semitones (0 cents to +2 octaves).
    pitchSemitones_ = std::clamp (depth, 0.0f, 1.0f) * 24.0f;
    if (prepared_) updatePitchRatio();
}

void ShimmerEngine::setModRate (float hz)
{
    // Hijack: 0.1..10 Hz range → 0..1 shimmer mix. The "Hz" display unit is
    // a known wart (the slider's textFromValueFunction is set globally and
    // can't be swapped per-engine without invasive plumbing) but the user
    // sees the relabeled "MIX" name and tooltip, and the function is clear:
    // more rotation right = more pitched feedback.
    const float clamped = std::clamp (hz, 0.1f, 10.0f);
    shimmerMix_ = (clamped - 0.1f) / 9.9f;
    // Mix changes the pitch-shifter loop loss compensation in updateFeedback,
    // so the feedback gain must be recomputed any time mix changes.
    if (prepared_) updateFeedback();
}

void ShimmerEngine::setTankDiffusion (float /*amount*/)
{
    // No-op: the FDN's Hadamard mixing IS the diffusion stage. Adding the
    // global series diffuser on top would smear the pitched feedback before
    // it cascades, blurring the shimmer character.
}

void ShimmerEngine::setFreeze (bool frozen)
{
    frozen_ = frozen;
    if (prepared_) updateFeedback();
}

// ── Update helpers ─────────────────────────────────────────────────────────

void ShimmerEngine::updateDelays()
{
    // Same size-scale curve as the other engines for muscle-memory consistency.
    const float sizeScale = 0.5f + sizeParam_ * 1.0f;
    const float rateRatio = static_cast<float> (sampleRate_ / 44100.0);

    for (int i = 0; i < kNumChannels; ++i)
    {
        const int target = static_cast<int> (
            static_cast<float> (kBaseDelays[i]) * sizeScale * rateRatio);
        delays_[i].delaySamples = std::min (target, delays_[i].mask - 8);
        // LFO depth tracks delay length (0.8 % of each delay's length).
        // Set here so it stays in sync when the user moves the SIZE knob —
        // previously this was set once in prepare() against zero-initialised
        // delaySamples, which silently disabled all in-loop modulation.
        lfos_[static_cast<size_t> (i)].setDepth (0.008f * static_cast<float> (delays_[i].delaySamples));
    }
}

void ShimmerEngine::updateFeedback()
{
    // Mean delay across the 8 channels in seconds → solve for per-cycle
    // gain that produces the requested RT60.
    if (frozen_)
    {
        feedbackGain_ = 1.0f;
        return;
    }

    float meanDelaySamples = 0.0f;
    for (auto& d : delays_) meanDelaySamples += static_cast<float> (d.delaySamples);
    meanDelaySamples /= static_cast<float> (kNumChannels);
    const float loopPeriodSec = meanDelaySamples / static_cast<float> (sampleRate_);

    feedbackGain_ = std::pow (10.0f, -3.0f * loopPeriodSec / std::max (decayTime_, 0.05f));

    // Series-loop pitch-shifter compensation. The granular pitch shifter
    // loses ~2 dB of energy per loop iteration (Hann-window crossfade
    // overlap integral + grain-reset transients), and the equal-power
    // crossfade contributes another small loss for the partially-correlated
    // (y[i], pitched) signal pair. Together these compound to make
    // measured RT60 about 2.7× shorter than the math predicts unless we
    // pre-boost the math-derived feedback gain.
    //
    // The compensation ramps with shimmerMix_: at mix=0 (pure FDN reverb)
    // there's no pitched signal in the loop, so no compensation needed
    // and decayTime → fb math is exact. At mix=1 the loop is fully
    // pitched, so the maximum 2 dB compensation kicks in.
    //
    // Final clamp lifted from 0.97 to 0.99 to give the compensated value
    // somewhere to live. Stability is enforced by softClip inside the
    // per-sample loop.
    // pitchLossDB scales linearly with mix, but the actual per-cycle loss
    // is nonlinear (small at mix=0, grows with the wet-path weight, but
    // also depends on pitch ratio and cycle period). Empirically a 1.5 dB
    // ceiling at mix=1 strikes a balance: at the canonical Eno-style
    // mix=0.5 it brings RT60 close to the dialled-in decayTime, and at
    // higher mix it doesn't overshoot Valhalla-like 7-8 s tails by an
    // unreasonable margin. Per-preset tuning of decayTime can fine-tune
    // any remaining gap.
    const float pitchLossDB        = 1.5f * shimmerMix_;
    const float pitchCompensation  = std::pow (10.0f, pitchLossDB / 20.0f);
    feedbackGain_ *= pitchCompensation;
    feedbackGain_ = std::clamp (feedbackGain_, 0.0f, 0.99f);
}

void ShimmerEngine::updateDamping()
{
    // Output-side 1-pole LP. trebleMult = 1.0 → ~16 kHz cutoff — i.e. for
    // most of the user-facing range the LP is essentially open and the
    // shell's hi_cut is the real brightness ceiling. With the v4 series
    // topology each loop iteration shifts content UP by `mix·shift`
    // semitones; even moderate mix (0.5) at +12-semi pushes accumulated
    // content well above a 5–10 kHz cutoff within a few cycles. Damping
    // there killed the tail (RT60 collapsed to 0.8s) — pulling the
    // baseline up lets the cascade live in the upper octaves long enough
    // to actually be heard before it decays out of the loop on its own.
    // At trebleMult = 0.55 (Eno Choir) this lands at 8.8 kHz, right around
    // Valhalla's default 8 kHz high-cut.
    // Settled on 16 kHz baseline — diagnostic with 22 kHz showed identical
    // RT60, so damping is no longer the bottleneck (the pitch-shifter loop
    // loss dominates). 16 kHz keeps a gentle high-frequency tame for the
    // top of the user's trebleMult range.
    const float fc = std::clamp (16000.0f * trebleMult_, 200.0f,
                                 0.45f * static_cast<float> (sampleRate_));
    dampCoeff_ = std::exp (-kTwoPi * fc / static_cast<float> (sampleRate_));
}

void ShimmerEngine::updatePitchRatio()
{
    // semitones → ratio: r = 2^(semitones/12)
    const float ratio = std::pow (2.0f, pitchSemitones_ / 12.0f);
    pitchL_.setPitchRatio (ratio);
    pitchR_.setPitchRatio (ratio);
}

// ── Process ────────────────────────────────────────────────────────────────

void ShimmerEngine::process (const float* inL, const float* inR,
                             float* outL, float* outR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;

    // Output normaliser — sum of 4 uncorrelated channels per side ≈ scales
    // by √4 = 2; halve to bring back to unit RMS.
    constexpr float kOutputScale = 0.5f;

    // Input distribution: L feeds even-indexed delays, R feeds odd-indexed.
    // Each side's inputs scaled by 1/√4 to balance against the 4-way sum.
    constexpr float kInputScale  = 0.5f;

    // Local cache to avoid repeated atomic-style reads inside the per-sample loop
    const float fb     = feedbackGain_;
    const float mix    = shimmerMix_;
    const float damp   = dampCoeff_;

    // Equal-power crossfade gains for the series loop. y[i] (unpitched FDN
    // feedback) and pitched are largely uncorrelated (different fundamental
    // frequencies after the +N-semi shift), so equal-power keeps total loop
    // RMS roughly constant across the mix range.
    //
    // Tried boosting the loop with a (1 + 0.5·mix) makeup factor to compensate
    // for the pitch-shifter's ~1.25 dB per-pass loss, but it interacts
    // destructively with softClip at the canonical Eno mix (0.5–0.7) — the
    // boosted signal saturates early, dynamic range compresses, and the
    // cascade flattens (Cascading Heaven cascade Δ went +670 → −2 Hz).
    // Instead the softClip ceiling is raised (1.0/2.0 → 1.5/3.0) and the
    // feedback gain clamp is lifted (0.97 → 0.99) to give the loop more
    // headroom; matched-RMS makeup is left for a future revision.
    const float dryGain = std::sqrt (1.0f - mix);
    const float wetGain = std::sqrt (mix);

    for (int n = 0; n < numSamples; ++n)
    {
        // Drive softClip on input.
        const float xL = DspUtils::softClip (inL[n], satThreshold, satCeiling);
        const float xR = DspUtils::softClip (inR[n], satThreshold, satCeiling);

        // 1) Read the 8 delay outputs WITH per-channel LFO modulation. The
        //    LFOs run at distinct slow rates (0.20–0.70 Hz) so each delay
        //    drifts independently → granular pitch shifter sees decorrelated
        //    input → cascade develops the lush spread Eno shimmer needs.
        float x[kNumChannels];
        for (int i = 0; i < kNumChannels; ++i)
        {
            const float lfoOffset = lfos_[static_cast<size_t> (i)].next();
            x[i] = delays_[i].read (lfoOffset);
        }

        // 2) Outputs (pre-Hadamard) — the natural FDN tail. Even indices →
        //    L, odd indices → R. Energy-normalised by kOutputScale.
        float wetL = 0.0f, wetR = 0.0f;
        for (int i = 0; i < kNumChannels; ++i)
            (i % 2 == 0 ? wetL : wetR) += x[i];
        outL[n] = wetL * kOutputScale;
        outR[n] = wetR * kOutputScale;

        // 3) Apply Hadamard 8×8 mix to the channel signals → y[i]. This is
        //    what scrambles the feedback so each delay sees a fresh
        //    decorrelated sum of all 8.
        float y[kNumChannels];
        for (int i = 0; i < kNumChannels; ++i)
        {
            float acc = 0.0f;
            for (int j = 0; j < kNumChannels; ++j)
                acc += static_cast<float> (kHadamardSign[i][j]) * x[j];
            y[i] = acc * kHadamardScale;
        }

        // 4) Aggregate per-side mixed signal → feed into the L/R pitch
        //    shifters. Average so the pitch shifter sees a balanced signal,
        //    not the sum (which would be hot enough to clip the grains).
        float aggL = 0.0f, aggR = 0.0f;
        for (int i = 0; i < kNumChannels; ++i)
            (i % 2 == 0 ? aggL : aggR) += y[i];
        aggL *= 0.25f;   // 1/4 = average over 4 even-indexed channels
        aggR *= 0.25f;
        const float pitchedL = pitchL_.process (aggL);
        const float pitchedR = pitchR_.process (aggR);

        // 5) Per-delay feedback write — SERIES topology (canonical Eno/
        //    Lanois). The pitched feedback REPLACES a fraction `mix` of the
        //    unpitched FDN feedback rather than adding on top. Each pass
        //    through the loop the loopSignal gets pitched up by `mix` of an
        //    octave, so the cascade compounds on every iteration. At mix=1
        //    every loop iteration shifts up the full pitchSemitones; at mix=0
        //    the engine is a plain modulated FDN reverb with no shimmer.
        //
        //    History:
        //      v1 linear crossfade ((1-mix)·y + mix·pitched, no norm)  →
        //          halved each path's energy → RT60 cratered to 0.5s
        //      v2 equal-power crossfade (√(1-mix)·y + √mix·pitched)    →
        //          fixed total RMS but still attenuated dry → RT60 1s
        //      v3 additive ((y + pitched·mix) / √(1+mix²))             →
        //          dry stayed at unity, pitched added on top, but the
        //          1/√(1+mix²) normaliser cut the *effective* feedback
        //          gain at common mix=0.5 from 0.94 → 0.78, dropping
        //          measured RT60 to ~2.5s vs Valhalla's 7.5s (and
        //          centroid ceiling stuck at 3 kHz vs theirs at 6.3 kHz)
        //      v4 series (this) — single loop element, blend is a true
        //          crossfade so total energy stays constant for all mix
        //          settings. feedbackGain_ now governs RT60 directly per
        //          its math; cascade compounds on every pass since pitched
        //          signal IS the loop element, not a parallel sidechain.
        for (int i = 0; i < kNumChannels; ++i)
        {
            const float pitched = (i % 2 == 0 ? pitchedL : pitchedR);
            const float input_i = (i % 2 == 0 ? xL : xR) * kInputScale;
            const float loopSignal = dryGain * y[i] + wetGain * pitched;

            // softClip on the feedback bus is a stability safety net. Even
            // with the series crossfade the granular pitch shifter can
            // produce small amplitude transients during grain crossover that
            // briefly exceed unity; without the clip these accumulate at
            // sub-bass via aliasing on extreme presets (Cascading Heaven
            // at +1.6 oct). softClip(x, 1, 2) starts shaping at 1.0 and
            // saturates at 2.0 — transparent for normal levels.
            // softClip ceiling raised 1.0/2.0 → 1.5/3.0. With feedback now
            // clamped to 0.99 (vs the old 0.97) the per-sample loop levels
            // run hotter, and the previous 1.0 threshold was kicking in on
            // legitimate cascade content rather than just on grain-crossover
            // transients — measurably flattening the cascade without
            // helping stability.
            const float clippedFb = DspUtils::softClip (fb * loopSignal, 1.5f, 3.0f);
            const float toWrite   = input_i + clippedFb + DspUtils::kDenormalPrevention;
            delays_[i].write (toWrite);
        }

        // 6) Output-side HF damping. Applied to the wet sum AFTER it leaves
        //    the feedback loop. Same trebleMultiply control surface, same
        //    user expectation ("turn TREBLE down → wet gets darker"), but
        //    the feedback path stays full-bandwidth so cascades develop.
        dampStateOutL_ = (1.0f - damp) * outL[n] + damp * dampStateOutL_;
        dampStateOutR_ = (1.0f - damp) * outR[n] + damp * dampStateOutR_;
        outL[n] = dampStateOutL_;
        outR[n] = dampStateOutR_;
    }

    // Bass post-shelf (same trivial flat scale used in the other engines).
    if (std::abs (bassMult_ - 1.0f) > 1e-3f)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            outL[n] *= bassMult_;
            outR[n] *= bassMult_;
        }
    }
}

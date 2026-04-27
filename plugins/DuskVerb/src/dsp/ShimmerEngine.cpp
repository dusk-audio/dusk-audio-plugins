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

void ShimmerEngine::GranularPitchShifter::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    // Buffer = 8 × grain size for plenty of lookback headroom across the
    // 1.0–4.0 pitch-ratio range. At ratio 4.0 (+2 oct), each grain consumes
    // 4 × kGrainSize input samples per kGrainSize output samples → max
    // lookback needed ≈ 3 × kGrainSize.
    const int bufSize = DspUtils::nextPowerOf2 (8 * kGrainSize);
    buffer_.assign (static_cast<size_t> (bufSize), 0.0f);
    mask_ = bufSize - 1;
    clear();
    updateAntiAliasCutoff();
}

// RBJ Audio EQ Cookbook biquad LP coefficients (Direct Form II Transposed
// in process()). See https://www.w3.org/TR/audio-eq-cookbook/.
void ShimmerEngine::GranularPitchShifter::BiquadLP::setCoeffs (double sampleRate, float fc, float Q)
{
    const float omega = 2.0f * 3.14159265358979323846f * fc / static_cast<float> (sampleRate);
    const float cosw  = std::cos (omega);
    const float sinw  = std::sin (omega);
    const float alpha = sinw / (2.0f * Q);

    const float a0 = 1.0f + alpha;
    b0 = (1.0f - cosw) * 0.5f / a0;
    b1 = (1.0f - cosw) / a0;
    b2 = (1.0f - cosw) * 0.5f / a0;
    a1 = -2.0f * cosw / a0;
    a2 = (1.0f - alpha) / a0;
}

void ShimmerEngine::GranularPitchShifter::updateAntiAliasCutoff()
{
    // Cutoff = min(SR/2 × 0.9, SR/2 / pitchRatio_). At pitchRatio = 1.0 (no
    // shift) the LP sits at 0.9 × Nyquist (essentially open). At pitchRatio
    // = 4.0 (+2 oct, Cascading Heaven extreme), cutoff = SR/2 / 4 = 6 kHz
    // at 48 kHz SR — safely below the alias-fold frequency. Two cascaded
    // Butterworth biquads = 4-pole / 24 dB-per-octave roll-off, steep
    // enough that anything above the cutoff is attenuated by > 30 dB
    // before the pitch shifter sees it.
    const float nyquist = 0.5f * static_cast<float> (sampleRate_);
    const float fc      = std::min (nyquist * 0.9f,
                                    nyquist / std::max (pitchRatio_, 1.0f));
    aaStage1_.setCoeffs (sampleRate_, fc, kButterworthQ1);
    aaStage2_.setCoeffs (sampleRate_, fc, kButterworthQ2);
}

void ShimmerEngine::GranularPitchShifter::clear()
{
    std::fill (buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    phase1_   = 0;
    phase2_   = kGrainSize / 2;
    readPos1_ = 0.0f;
    readPos2_ = static_cast<float> (kGrainSize) * 0.5f;
    aaStage1_.clear();
    aaStage2_.clear();
}

void ShimmerEngine::GranularPitchShifter::setPitchRatio (float ratio)
{
    pitchRatio_ = std::clamp (ratio, 0.5f, 4.0f);
    updateAntiAliasCutoff();
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
    // Phase 2 — anti-alias the input before it hits the grain buffer.
    // 4-pole Butterworth at min(SR/2·0.9, SR/2/pitchRatio) prevents the
    // aliasing-recirculation cascade collapse that was producing sub-bass
    // artifacts at extreme pitch ratios. The LP is unity-gain in the
    // passband so cascade level isn't affected — only the > Nyquist/ratio
    // content that would alias gets removed.
    const float filtered = aaStage2_.process (aaStage1_.process (input));

    // Write filtered input to buffer.
    buffer_[static_cast<size_t> (writePos_)] = filtered + DspUtils::kDenormalPrevention;

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

    pitchL_.prepare (sampleRate);
    pitchR_.prepare (sampleRate);

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

    // Phase 3 — envelope-follower smoothing constants. exp(-1/(τ·sr)) is the
    // standard 1-pole release coefficient where τ is the time constant. With
    // attack 50 ms / release 400 ms we react quickly to cascade buildup but
    // recover smoothly enough that the gain compensation doesn't pump.
    // The gain itself is additionally slewed at 100 ms to avoid zipper noise
    // from per-sample compensation jumps.
    const float sr = static_cast<float> (sampleRate);
    envAttackCoeff_  = std::exp (-1.0f / (0.050f * sr));
    envReleaseCoeff_ = std::exp (-1.0f / (0.400f * sr));
    gainSlewCoeff_   = std::exp (-1.0f / (0.100f * sr));
    autoLevelEnvPre_  = 0.0f;
    autoLevelEnvPost_ = 0.0f;
    autoLevelGain_    = 1.0f;

    prepared_ = true;
}

void ShimmerEngine::clearBuffers()
{
    for (auto& d : delays_) d.clear();
    pitchL_.clear();
    pitchR_.clear();
    dampStateOutL_ = 0.0f;
    dampStateOutR_ = 0.0f;
    autoLevelEnvPre_  = 0.0f;
    autoLevelEnvPost_ = 0.0f;
    autoLevelGain_    = 1.0f;
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

    // Phase 3 — pitch-shifter loss compensation is now LIVE (envelope-
    // follower based, see process()). The empirical static model that
    // used to live here was wrong: it assumed a 1.5 dB × shimmerMix_
    // linear loss, but actual loss depends on pitch ratio + cycle period
    // + Hann overlap integral and varies non-linearly. Net effect was
    // either underdamping (runaway → softClip) or overdamping (cascade
    // dies). Replaced with a real-time follower that measures the actual
    // loss between pre-pitch and post-pitch envelopes and feeds back the
    // measured ratio as a per-sample gain compensation.
    //
    // Clamp held at 0.99 so the compensated feedback × autoLevelGain has
    // somewhere to live. The loop is now linear (softClip moved to output
    // in Phase 4) so equal-power crossfade math actually holds.
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

        // 4b) Phase 3 — measure the actual pitch-shifter loss in real time
        //     and compute a feedback-gain compensation. The follower tracks
        //     |signal| with attack/release smoothing; the ratio preEnv/postEnv
        //     IS the loss factor (post < pre because the granular shifter
        //     drops energy at grain seams). Average L/R envelopes so a single
        //     compensation gain feeds the whole shared FDN loop.
        const float preMag  = 0.5f * (std::abs (aggL)     + std::abs (aggR));
        const float postMag = 0.5f * (std::abs (pitchedL) + std::abs (pitchedR));

        const float preCoeff  = (preMag  > autoLevelEnvPre_)  ? envAttackCoeff_ : envReleaseCoeff_;
        const float postCoeff = (postMag > autoLevelEnvPost_) ? envAttackCoeff_ : envReleaseCoeff_;
        autoLevelEnvPre_  = preCoeff  * autoLevelEnvPre_  + (1.0f - preCoeff)  * preMag;
        autoLevelEnvPost_ = postCoeff * autoLevelEnvPost_ + (1.0f - postCoeff) * postMag;

        // Loss factor = pre / post (capped 1..2 = 0..6 dB max boost). At
        // silence both envelopes are tiny and the ratio collapses toward
        // 1.0 (no boost). At cascade buildup, post lags pre and the ratio
        // rises toward the measured loss, then settles to the steady-state
        // compensation. Cap at 6 dB because anything more either reflects
        // a numerical edge case OR represents a loss too aggressive to
        // truly compensate without making the loop unstable.
        const float targetGain = std::clamp (
            (autoLevelEnvPre_ + 1e-6f) / (autoLevelEnvPost_ + 1e-6f),
            1.0f, 2.0f);
        autoLevelGain_ = gainSlewCoeff_ * autoLevelGain_
                       + (1.0f - gainSlewCoeff_) * targetGain;

        // The crossfaded loop signal (dryGain·y + wetGain·pitched) for the
        // CORRELATED (y, pitched) pair can exceed the per-channel RMS by
        // up to ~6 % at mix=0.5 (math: 0.5+0.125·eff²+0.5·ρ·eff at ρ=1
        // gives 1.125 = +6 dB ratio). To keep loop gain < 1 we apply a
        // 0.93 attenuation here so the effective per-cycle = effectiveFb
        // × 1.06 × 0.93 ≤ 0.99 even at fb very close to unity. This
        // factor lets us push effectiveFb up to ~0.97 without runaway
        // (extending the practical RT60 from ~1.4 s at the prior 0.85 cap
        // to a usable ~5 s).
        constexpr float kLoopStabilityScale = 0.93f;
        const float effectiveFb = std::min (fb * autoLevelGain_, 0.97f) * kLoopStabilityScale;

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
        // Phase 4 — LINEAR feedback bus. softClip is gone from the loop:
        //   • Phase 2 (anti-alias LP) prevents aliasing accumulation
        //   • Phase 3 (auto-level) bounds the loop signal in real time
        // Together they make the loop stable WITHOUT a nonlinear clipper,
        // which means the equal-power crossfade math actually holds (softClip
        // was destroying the linearity it depends on, flattening cascades).
        // The ONLY safety net is the output-stage softClip below.
        for (int i = 0; i < kNumChannels; ++i)
        {
            const float pitched = (i % 2 == 0 ? pitchedL : pitchedR);
            const float input_i = (i % 2 == 0 ? xL : xR) * kInputScale;
            const float loopSignal = dryGain * y[i] + wetGain * pitched;
            const float toWrite    = input_i + effectiveFb * loopSignal
                                   + DspUtils::kDenormalPrevention;
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

        // 7) Phase 4 — output-stage safety softClip. The only nonlinearity
        //    in the engine. Knee at 0.95, ceiling at 1.5: anything below
        //    0.95 passes untouched, then a smooth tanh-like curve shapes
        //    the rest. Catches any peaks from extreme cascade development
        //    without imposing on normal-level operation.
        outL[n] = DspUtils::softClip (outL[n], 0.95f, 1.5f);
        outR[n] = DspUtils::softClip (outR[n], 0.95f, 1.5f);
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

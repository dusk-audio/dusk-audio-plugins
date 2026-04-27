#include "NonLinearEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr float kTwoPi = 6.283185307179586f;

    // 4×4 Hadamard mixing matrix (sequency-ordered, ±0.5 = ±1/√4).
    // Each row is orthogonal to every other row → energy-preserving mix
    // that decorrelates the 4 FDN channels every sample.
    constexpr float kFDNHadamard[4][4] = {
        {  0.5f,  0.5f,  0.5f,  0.5f },
        {  0.5f, -0.5f,  0.5f, -0.5f },
        {  0.5f,  0.5f, -0.5f, -0.5f },
        {  0.5f, -0.5f, -0.5f,  0.5f },
    };
}

// ============================================================================
// FDNDelay helpers
// ============================================================================

void NonLinearEngine::FDNDelay::allocate (int maxSamples)
{
    const int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 8, 64));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

void NonLinearEngine::FDNDelay::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void NonLinearEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Per-channel circular buffer = max length × sample rate, rounded to
    // power-of-2 for AND-mask wrap. +16 sample headroom for the worst-case
    // tap landing slightly beyond `lengthSamples_` after pseudo-random jitter.
    const int maxSamples = static_cast<int> (std::ceil (
        kMaxLengthMs * 0.001f * static_cast<float> (sampleRate))) + 16;
    const int bufSize = DspUtils::nextPowerOf2 (maxSamples);

    bufL_.assign (static_cast<size_t> (bufSize), 0.0f);
    bufR_.assign (static_cast<size_t> (bufSize), 0.0f);
    mask_ = bufSize - 1;
    writePos_ = 0;

    // Release coefficient for the peak follower: τ = 10 ms → coeff = exp(-1/τ).
    releaseCoeff_ = std::exp (-1.0f / (0.010f * static_cast<float> (sampleRate)));

    // Anti-click ramp samples (2 ms → ~96 samples at 48 k).
    rampSamples_ = std::max (4, static_cast<int> (kAntiClickRampMs * 0.001f * sampleRate));

    rebuildTaps();

    // ── Parallel small-room FDN allocation ──
    // Reserve worst-case (16 samples headroom above the base × rateRatio)
    const float rateRatio = static_cast<float> (sampleRate / 44100.0);
    for (int i = 0; i < kNumFDNChannels; ++i)
    {
        const int target = static_cast<int> (
            static_cast<float> (kFDNBaseDelays[i]) * rateRatio + 16.0f);
        fdnDelays_[i].allocate (target);
        fdnDelays_[i].delaySamples = std::min (target - 8, fdnDelays_[i].mask - 8);
    }

    // FDN per-channel HF damping: 1-pole LP at ~12 kHz cutoff (very light).
    // Heavier damping (e.g. 5 kHz cutoff) eats the feedback energy fast
    // enough to slash the effective RT60 from the intended 600 ms down to
    // ~125 ms — verified by VVV comparison: with heavy damping the post-
    // gate tail was 35 dB quieter than the VVV reference. 12 kHz cutoff
    // tames just the brittle top while preserving mid-band sustain.
    fdnDampCoeff_ = std::exp (-kTwoPi * 12000.0f / static_cast<float> (sampleRate));

    prepared_ = true;
}

void NonLinearEngine::clearBuffers()
{
    std::fill (bufL_.begin(), bufL_.end(), 0.0f);
    std::fill (bufR_.begin(), bufR_.end(), 0.0f);
    writePos_          = 0;
    envFollower_       = 0.0f;
    wasAboveThreshold_ = false;
    gatePhase_         = -1;

    // Parallel FDN state
    for (auto& d : fdnDelays_) d.clear();
    fdnDampStates_.fill (0.0f);
}

// ── Universal setters ──────────────────────────────────────────────────────

void NonLinearEngine::setDecayTime (float seconds)
{
    const float ms = std::clamp (seconds * 1000.0f, kMinLengthMs, kMaxLengthMs);
    const float newLength = ms * 0.001f * static_cast<float> (sampleRate_);
    if (std::abs (newLength - lengthSamples_) < 1.0f) return;
    lengthSamples_ = newLength;
    if (prepared_) rebuildTaps();
}

void NonLinearEngine::setSize              (float /*size*/) {}
void NonLinearEngine::setBassMultiply      (float mult)     { bassMult_ = std::clamp (mult, 0.1f, 4.0f); }
void NonLinearEngine::setMidMultiply       (float /*mult*/) {}
void NonLinearEngine::setTrebleMultiply    (float /*mult*/) {}
void NonLinearEngine::setCrossoverFreq     (float /*hz*/)   {}
void NonLinearEngine::setHighCrossoverFreq (float /*hz*/)   {}
void NonLinearEngine::setSaturation        (float amount)   { saturationAmount_ = std::clamp (amount, 0.0f, 1.0f); }
void NonLinearEngine::setModDepth          (float /*depth*/){}
void NonLinearEngine::setModRate           (float /*hz*/)   {}

void NonLinearEngine::setTankDiffusion (float amount)
{
    const auto newShape = shapeFromAmount (std::clamp (amount, 0.0f, 1.0f));
    if (newShape == currentShape_) return;
    currentShape_ = newShape;
    if (prepared_) rebuildTaps();
}

void NonLinearEngine::setFreeze (bool frozen)
{
    // Freeze pins the gate open so the dense tap output sustains forever.
    frozen_ = frozen;
}

NonLinearEngine::EnvelopeShape NonLinearEngine::shapeFromAmount (float amount) noexcept
{
    if (amount < kShapeGatedMax)   return EnvelopeShape::Gated;
    if (amount < kShapeReverseMax) return EnvelopeShape::Reverse;
    return EnvelopeShape::Decaying;
}

// ============================================================================
// Tap layout — 256 quasi-uniform positions with deterministic xorshift jitter
// for natural ER density. Per-tap GAIN follows the SHAPE selector. Energy
// normalised by 1/√N so the sustained-input output level is musical.
// ============================================================================

void NonLinearEngine::rebuildTaps()
{
    std::uint32_t seed = 0xC0FFEEu;
    auto next01 = [&seed]()
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float> (seed) * (1.0f / 4294967296.0f);
    };

    constexpr float invN = 1.0f / static_cast<float> (kNumTaps);

    // 1/√N × √2 ≈ +3 dB lift over pure energy normalisation. Pure 1/√N
    // gives sustained-noise output ≈ input RMS (unity); +3 dB matches the
    // measured VVV Large Gated Snare reference plateau (−19 dB peak vs our
    // pre-lift −22 dB peak). The lift sits below the satCeiling = 2.0 head-
    // room so even worst-case correlated transients don't clip.
    const float perTapNorm = 1.41421356f / std::sqrt (static_cast<float> (kNumTaps));

    for (int i = 0; i < kNumTaps; ++i)
    {
        // Quasi-uniform position with jitter (avoids comb-peak at perfect grid)
        const float jitter = (next01() - 0.5f) * 0.6f * invN;
        const float t = std::clamp ((static_cast<float> (i) + 0.5f) * invN + jitter,
                                    0.001f, 0.999f);

        taps_[i].delaySamples = std::max (4, static_cast<int> (t * lengthSamples_));

        // SHAPE-driven base gain. Note: this is per-tap weight, NOT the
        // output envelope. The output envelope (gate) is a separate
        // multiplier. For Gated shape the per-tap gain is constant, so
        // the output level depends entirely on the gate. For Reverse and
        // Decaying, the per-tap gain shapes the IR's energy distribution
        // independently of the gate.
        float baseGain = 1.0f;
        switch (currentShape_)
        {
            case EnvelopeShape::Gated:    baseGain = 1.0f;                 break;
            case EnvelopeShape::Reverse:  baseGain = t;                    break;
            case EnvelopeShape::Decaying: baseGain = std::exp (-3.0f * t); break;
        }

        const float scaledGain = baseGain * perTapNorm;

        // Per-tap stereo decorrelation via small ±20% L/R offset
        const float lrOffset = (next01() - 0.5f) * 0.4f;
        taps_[i].gainL = scaledGain * (1.0f + lrOffset);
        taps_[i].gainR = scaledGain * (1.0f - lrOffset);
    }
}

// ============================================================================
// Gate envelope — Gated/Reverse/Decaying with 2 ms anti-click ramp at end
// ============================================================================

inline float NonLinearEngine::computeGate (int phase) const noexcept
{
    const int lengthInt = static_cast<int> (lengthSamples_);
    if (phase < 0 || phase >= lengthInt)
        return 0.0f;

    const float t = static_cast<float> (phase) / lengthSamples_;

    // ── BASE shape ──
    float base = 0.0f;
    switch (currentShape_)
    {
        case EnvelopeShape::Gated:    base = 1.0f;                 break;
        case EnvelopeShape::Reverse:  base = t;                    break;
        case EnvelopeShape::Decaying: base = std::exp (-2.0f * t); break;
    }

    // ── ATTACK ramp (first 7% of window, ~25 ms @ 350 ms length) ──
    // Without this, the gate snaps from 0 to 1 in one sample at trigger,
    // producing a clicky onset. VVV Large Gated Snare measurement shows a
    // ~125 ms natural buildup before reaching peak; this ~25 ms attack
    // gives that buildup a musical edge without losing the snap.
    constexpr float kAttackFraction = 0.07f;
    if (t < kAttackFraction)
        base *= (t / kAttackFraction);

    // ── RELEASE — exponential decay across last 35% of window ──
    // Switched from a 25%-linear ramp (felt like a digital brick wall) to a
    // 35%-exponential decay after VVV Large Gated Snare comparison: VVV's
    // measured release spans ~200 ms with a gradual exp-like fade, not the
    // sharp cliff a linear ramp gives. The −4 decay constant lands the gate
    // at exp(−4) ≈ 0.018 (≈ −35 dB) at the end of the release region —
    // matches VVV's measured −50 dB plateau-end level (TDL output is
    // already attenuated by the soft tap-readout decay too).
    //
    // After the release region a final 2 ms linear ramp closes the
    // remaining 0.018 → 0 step so there's no click on the last sample.
    constexpr float kReleaseFraction = 0.35f;
    const float releaseStart = 1.0f - kReleaseFraction;
    if (t > releaseStart)
    {
        const float releasePhase = (t - releaseStart) / kReleaseFraction;
        base *= std::exp (-4.0f * releasePhase);
    }

    // Final 2 ms anti-click ramp ensures the gate truly closes to zero
    // without leaving a discontinuous step at length. The exp decay above
    // is at ~0.018 by the time we hit this region, so the residual drop
    // is small (-35 dB → -∞) and the linear fade smooths it.
    const int rampStart = lengthInt - rampSamples_;
    if (phase >= rampStart)
    {
        const float rampPhase = static_cast<float> (phase - rampStart)
                              / static_cast<float> (rampSamples_);
        base *= (1.0f - rampPhase);
    }

    return base;
}

// ============================================================================
// process — feed-forward TDL: write input, sum 256 weighted reads × gate
// ============================================================================

void NonLinearEngine::process (const float* inL, const float* inR,
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
    const int   lengthInt    = static_cast<int> (lengthSamples_);

    for (int n = 0; n < numSamples; ++n)
    {
        // ── 1) Drive softClip on input, write to circular buffer ──
        const float xL = DspUtils::softClip (inL[n], satThreshold, satCeiling);
        const float xR = DspUtils::softClip (inR[n], satThreshold, satCeiling);

        if (! frozen_)
        {
            bufL_[static_cast<size_t> (writePos_)] = xL;
            bufR_[static_cast<size_t> (writePos_)] = xR;
        }

        // ── 2) Peak follower for trigger detection ──
        const float inLevel = std::max (std::abs (xL), std::abs (xR));
        envFollower_ = std::max (inLevel, envFollower_ * releaseCoeff_);

        // ── 3) Trigger detection — rising edge above threshold ──
        const bool aboveThreshold = envFollower_ > kTriggerThreshold;
        if (aboveThreshold && ! wasAboveThreshold_)
            gatePhase_ = 0;            // (re-)trigger
        wasAboveThreshold_ = aboveThreshold;

        // ── 4) Sum all 256 taps. 256 reads × 2 channels per sample =
        //      ~25 M reads/sec at 48 k = ~3 % single-core. Acceptable. ──
        float sumL = 0.0f, sumR = 0.0f;
        for (int t = 0; t < kNumTaps; ++t)
        {
            const int readPos = (writePos_ - taps_[t].delaySamples) & mask_;
            const float sL = bufL_[static_cast<size_t> (readPos)];
            const float sR = bufR_[static_cast<size_t> (readPos)];
            sumL += sL * taps_[t].gainL;
            sumR += sR * taps_[t].gainR;
        }

        // ── 5) Apply gate envelope to TDL output. Freeze pins gate at 1.0 ──
        const float gate = frozen_ ? 1.0f : computeGate (gatePhase_);
        const float tdlL = sumL * gate;
        const float tdlR = sumR * gate;

        // ── 6) Parallel small-room FDN — always running, low-level mix ──
        // 4 delays read → output sum (pre-Hadamard) → Hadamard mix → damped
        // → write-back with feedback. The FDN is NEVER gated; its constant
        // low-level output sums into the wet so when the TDL gate closes
        // the listener still hears a quiet room tail (~−29 dB body level).
        float fx[kNumFDNChannels];
        for (int i = 0; i < kNumFDNChannels; ++i)
            fx[i] = fdnDelays_[i].read();

        // Output (pre-Hadamard): even ch → L, odd → R (× 0.5 normaliser
        // for sum of 2 uncorrelated channels per side).
        const float fdnWetL = (fx[0] + fx[2]) * 0.5f;
        const float fdnWetR = (fx[1] + fx[3]) * 0.5f;

        // Hadamard 4×4 mix → fy[0..3]
        float fy[kNumFDNChannels];
        for (int i = 0; i < kNumFDNChannels; ++i)
        {
            float acc = 0.0f;
            for (int j = 0; j < kNumFDNChannels; ++j)
                acc += kFDNHadamard[i][j] * fx[j];
            fy[i] = acc;
        }

        // Per-channel damping LP + feedback write. fb pinned at 1.0 on
        // freeze for infinite-room sustain (matches the gate behaviour).
        const float fdnFb = frozen_ ? 1.0f : kFDNFeedback;
        for (int i = 0; i < kNumFDNChannels; ++i)
        {
            const float damped = (1.0f - fdnDampCoeff_) * fy[i]
                               + fdnDampCoeff_ * fdnDampStates_[static_cast<size_t> (i)];
            fdnDampStates_[static_cast<size_t> (i)] = damped;

            const float input_i = (i % 2 == 0 ? xL : xR) * 0.5f;
            fdnDelays_[i].write (input_i + fdnFb * damped + DspUtils::kDenormalPrevention);
        }

        // ── 7) Sum gated TDL + parallel FDN room body ──
        outL[n] = (tdlL + fdnWetL * kFDNTailLevel) * bassMult_;
        outR[n] = (tdlR + fdnWetR * kFDNTailLevel) * bassMult_;

        // ── 8) Advance write head and gate phase ──
        writePos_ = (writePos_ + 1) & mask_;
        if (gatePhase_ >= 0 && gatePhase_ < lengthInt)
            ++gatePhase_;
    }
}

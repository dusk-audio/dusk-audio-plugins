#include "HallReverb.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

// Out-of-line storage for the static constexpr arrays. C++17 makes these
// inline by default for in-class definitions, but emitting the addresses
// once here is harmless and keeps older toolchains happy.
constexpr float HallReverb::kTapTimesMs[HallReverb::kNumPredelayTaps];
constexpr float HallReverb::kTapWeights[HallReverb::kNumPredelayTaps];

namespace
{
    // Per-band base delay primes (at 44.1 kHz). Distinct prime sets so the
    // bass / mid / treble sub-tanks don't share modal frequencies — each
    // band's modal density lives within its own LR4 passband and never
    // bleeds into neighbouring measurement bands. Sized for "medium hall"
    // character at sizeScale 1.0; SubTank scales by sampleRate ratio and
    // sizeScale at prepare/setSize time.
    //
    //   Bass:   2.31–3.65 s longest loop fundamentals — supports slow
    //           decay tails that ring an octave or two below 250 Hz.
    //   Mid:    540–810 sample loops, fundamentals 54–82 Hz — Hadamard
    //           density at vocal-fundamental frequencies.
    //   Treble: 211–331 sample loops, fundamentals 133–209 Hz — the HP
    //           crossover removes the fundamental band; what's audible is
    //           the dense modal cloud above the crossover corner.
    //
    // Primes are mutually coprime within each band so no two channels in
    // a SubTank share a modal period (Hadamard mixing depends on this for
    // its decorrelation guarantee).
    constexpr int kBassDelays  [8] = { 1543, 1607, 1721, 1847, 1993, 2143, 2281, 2417 };
    constexpr int kMidDelays   [8] = {  541,  571,  613,  643,  683,  719,  761,  809 };
    constexpr int kTrebleDelays[8] = {  211,  227,  239,  257,  271,  283,  307,  331 };

    // Anti-correlated LFO phase offsets per band — bass / mid / treble each
    // see a different sub-tank modulation pattern. Keeps periodicity from
    // building up across bands (would manifest as a "wash" at the modulation
    // period if all bands shared a phase).
    constexpr float kBassBandPhase   = 0.0f;
    constexpr float kMidBandPhase    = 1.047f;   // ≈ 2π/6
    constexpr float kTrebleBandPhase = 2.094f;   // ≈ 4π/6
}

HallReverb::HallReverb() = default;

void HallReverb::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_       = sampleRate;
    scratchBlockSize_ = std::max (maxBlockSize, 256);

    splitL_.prepare (static_cast<float> (sampleRate));
    splitR_.prepare (static_cast<float> (sampleRate));
    bassPostL_  .prepare (static_cast<float> (sampleRate));
    bassPostR_  .prepare (static_cast<float> (sampleRate));
    midPostL_   .prepare (static_cast<float> (sampleRate));
    midPostR_   .prepare (static_cast<float> (sampleRate));
    treblePostL_.prepare (static_cast<float> (sampleRate));
    treblePostR_.prepare (static_cast<float> (sampleRate));
    updateCrossovers();

    bassTank_  .setLFOPhaseOffset (kBassBandPhase);
    midTank_   .setLFOPhaseOffset (kMidBandPhase);
    trebleTank_.setLFOPhaseOffset (kTrebleBandPhase);

    bassTank_  .prepare (sampleRate, kBassDelays,   scratchBlockSize_);
    midTank_   .prepare (sampleRate, kMidDelays,    scratchBlockSize_);
    trebleTank_.prepare (sampleRate, kTrebleDelays, scratchBlockSize_);

    // Predelay ring buffer — size to fit longest tap + modulation/safety
    // headroom, rounded up to next power of 2 for mask-and addressing.
    const float maxTapSec     = kTapTimesMs[kNumPredelayTaps - 1] * 0.001f;
    const int   maxTapSamples = static_cast<int> (std::ceil (maxTapSec
                              * static_cast<float> (sampleRate))) + 64;
    const int   predelaySize  = DspUtils::nextPowerOf2 (std::max (maxTapSamples, 1024));
    predelayL_.assign (static_cast<size_t> (predelaySize), 0.0f);
    predelayR_.assign (static_cast<size_t> (predelaySize), 0.0f);
    predelayMask_     = predelaySize - 1;
    predelayWritePos_ = 0;
    recomputePredelayTaps();

    const size_t sz = static_cast<size_t> (scratchBlockSize_);
    bassInL_  .assign (sz, 0.0f); bassInR_  .assign (sz, 0.0f);
    midInL_   .assign (sz, 0.0f); midInR_   .assign (sz, 0.0f);
    trebleInL_.assign (sz, 0.0f); trebleInR_.assign (sz, 0.0f);
    bassOutL_ .assign (sz, 0.0f); bassOutR_ .assign (sz, 0.0f);
    midOutL_  .assign (sz, 0.0f); midOutR_  .assign (sz, 0.0f);
    trebleOutL_.assign (sz, 0.0f); trebleOutR_.assign (sz, 0.0f);

    prepared_ = true;
    updateSubTankDecays();
    setBandDamping (dampingBass_, dampingMid_, dampingTreble_);
}

void HallReverb::recomputePredelayTaps()
{
    // Convert each tap's ms into sample count at the current sample rate.
    // Compute tapNorm so summed tap output has unity DC gain — keeps tank
    // input level identical to bare-tank case so the soft-clip threshold
    // and any preset gainTrim calibration carry over unchanged.
    float sumW = 0.0f;
    for (int t = 0; t < kNumPredelayTaps; ++t)
    {
        tapSamples_[t] = static_cast<int> (std::round (kTapTimesMs[t]
                                            * 0.001f * static_cast<float> (sampleRate_)));
        // Cap to (bufferSize - 1) defensively; predelay buffer is sized
        // above to fit the longest tap so this clamp shouldn't fire.
        if (tapSamples_[t] > predelayMask_) tapSamples_[t] = predelayMask_;
        sumW += kTapWeights[t];
    }
    tapNorm_ = (sumW > 0.0f) ? (1.0f / sumW) : 1.0f;
}

void HallReverb::clearBuffers()
{
    bassTank_.clear();
    midTank_.clear();
    trebleTank_.clear();
    splitL_.reset();
    splitR_.reset();
    bassPostL_.reset();   bassPostR_.reset();
    midPostL_.reset();    midPostR_.reset();
    treblePostL_.reset(); treblePostR_.reset();
    std::fill (predelayL_.begin(), predelayL_.end(), 0.0f);
    std::fill (predelayR_.begin(), predelayR_.end(), 0.0f);
    predelayWritePos_ = 0;
}

void HallReverb::updateCrossovers()
{
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    splitL_.setCrossovers      (crossoverFreq_, highCrossoverFreq_, sr);
    splitR_.setCrossovers      (crossoverFreq_, highCrossoverFreq_, sr);
    bassPostL_.setCrossovers   (crossoverFreq_, highCrossoverFreq_, sr);
    bassPostR_.setCrossovers   (crossoverFreq_, highCrossoverFreq_, sr);
    midPostL_.setCrossovers    (crossoverFreq_, highCrossoverFreq_, sr);
    midPostR_.setCrossovers    (crossoverFreq_, highCrossoverFreq_, sr);
    treblePostL_.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);
    treblePostR_.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);
}

void HallReverb::updateSubTankDecays()
{
    if (! prepared_) return;
    bassTank_  .setDecayTime (decayTime_ * bassMultiply_);
    midTank_   .setDecayTime (decayTime_ * midMultiply_);
    trebleTank_.setDecayTime (decayTime_ * trebleMultiply_);
}

void HallReverb::process (const float* inputL, const float* inputR,
                          float* outputL, float* outputR, int numSamples)
{
    if (! prepared_ || numSamples <= 0) return;

    int remaining = numSamples;
    int offset    = 0;

    while (remaining > 0)
    {
        const int n = std::min (remaining, scratchBlockSize_);

        // ── Multi-tap input injection → LR4 band split per channel ──
        // Each sample: write dry input to predelay ring, read N taps at
        // staggered delays, sum × unity-DC-norm, feed the result to the
        // band split. Tank input retains unity DC gain so the soft-clip
        // calibration carries over unchanged from the bare-tank baseline.
        for (int i = 0; i < n; ++i)
        {
            const float dryL = inputL[offset + i];
            const float dryR = inputR[offset + i];

            predelayL_[static_cast<size_t> (predelayWritePos_)] = dryL;
            predelayR_[static_cast<size_t> (predelayWritePos_)] = dryR;

            float sumL = 0.0f, sumR = 0.0f;
            for (int t = 0; t < kNumPredelayTaps; ++t)
            {
                const int readIdx = (predelayWritePos_ - tapSamples_[t]) & predelayMask_;
                sumL += predelayL_[static_cast<size_t> (readIdx)] * kTapWeights[t];
                sumR += predelayR_[static_cast<size_t> (readIdx)] * kTapWeights[t];
            }
            predelayWritePos_ = (predelayWritePos_ + 1) & predelayMask_;

            sumL *= tapNorm_;
            sumR *= tapNorm_;

            float bL, mL, tL, bR, mR, tR;
            splitL_.split (sumL, bL, mL, tL);
            splitR_.split (sumR, bR, mR, tR);
            bassInL_  [i] = bL; bassInR_  [i] = bR;
            midInL_   [i] = mL; midInR_   [i] = mR;
            trebleInL_[i] = tL; trebleInR_[i] = tR;
        }

        // ── Each sub-tank processes its own band ──
        bassTank_  .process (bassInL_  .data(), bassInR_  .data(),
                             bassOutL_ .data(), bassOutR_ .data(), n);
        midTank_   .process (midInL_   .data(), midInR_   .data(),
                             midOutL_  .data(), midOutR_  .data(), n);
        trebleTank_.process (trebleInL_.data(), trebleInR_.data(),
                             trebleOutL_.data(), trebleOutR_.data(), n);

        // ── Sum bands → M/S widener → soft-clip → output ──
        // Order matters: widener BEFORE soft-clip so the (nonlinear) clip
        // operates on the already-decorrelated signal. Clipping after the
        // widener preserves the matrix's stability guarantee on the linear
        // portion of the signal — important content that doesn't approach
        // the clip threshold sees a strictly linear transformation.
        const float sat = saturationAmount_;
        const bool  doSat = sat > 0.0001f;
        const float satDrive    = 1.0f + sat * 4.0f;
        const float invSatDrive = 1.0f / satDrive;
        const float b = stereoWidth_;

        const float gB = gainBass_, gM = gainMid_, gT = gainTreble_;

        for (int i = 0; i < n; ++i)
        {
            // Post-tank LR4 band isolation. Each SubTank's Hadamard mixing
            // produces broadband output (modal content spreads across the
            // full spectrum regardless of input frequency). Re-applying the
            // LR4 split to each band's output keeps the bass tank's upper-
            // harmonic content from polluting the mid passband, the mid
            // tank's modal residue out of bass/treble, etc. We discard the
            // off-band outputs and keep only the slot that matches the
            // SubTank's role.
            float bL_b, bL_m, bL_t, bR_b, bR_m, bR_t;
            float mL_b, mL_m, mL_t, mR_b, mR_m, mR_t;
            float tL_b, tL_m, tL_t, tR_b, tR_m, tR_t;
            bassPostL_  .split (bassOutL_  [i], bL_b, bL_m, bL_t);
            bassPostR_  .split (bassOutR_  [i], bR_b, bR_m, bR_t);
            midPostL_   .split (midOutL_   [i], mL_b, mL_m, mL_t);
            midPostR_   .split (midOutR_   [i], mR_b, mR_m, mR_t);
            treblePostL_.split (trebleOutL_[i], tL_b, tL_m, tL_t);
            treblePostR_.split (trebleOutR_[i], tR_b, tR_m, tR_t);

            const float wetL = bL_b * gB + mL_m * gM + tL_t * gT;
            const float wetR = bR_b * gB + mR_m * gM + tR_t * gT;

            float oL = wetL - b * wetR;
            float oR = wetR - b * wetL;

            if (doSat)
            {
                oL = std::tanh (oL * satDrive) * invSatDrive;
                oR = std::tanh (oR * satDrive) * invSatDrive;
            }
            outputL[offset + i] = oL;
            outputR[offset + i] = oR;
        }

        offset    += n;
        remaining -= n;
    }
}

// ── Setters ─────────────────────────────────────────────────────────

void HallReverb::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, seconds);
    updateSubTankDecays();
}

void HallReverb::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (0.1f, mult);
    updateSubTankDecays();
}

void HallReverb::setMidMultiply (float mult)
{
    midMultiply_ = std::max (0.1f, mult);
    updateSubTankDecays();
}

void HallReverb::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (0.1f, mult);
    updateSubTankDecays();
}

void HallReverb::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::clamp (hz, 20.0f, 20000.0f);
    updateCrossovers();
}

void HallReverb::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::clamp (hz, 20.0f, 20000.0f);
    updateCrossovers();
}

void HallReverb::setSize (float size)
{
    const float clamped = std::clamp (size, 0.5f, 2.0f);
    bassTank_  .setSize (clamped);
    midTank_   .setSize (clamped);
    trebleTank_.setSize (clamped);
    updateSubTankDecays();   // delay lengths changed → feedback gains recompute
}

void HallReverb::setModDepth (float depth)
{
    const float clamped = std::clamp (depth, 0.0f, 16.0f);
    bassTank_  .setModDepth (clamped);
    midTank_   .setModDepth (clamped);
    trebleTank_.setModDepth (clamped);
}

void HallReverb::setModRate (float hz)
{
    const float clamped = std::clamp (hz, 0.01f, 20.0f);
    bassTank_  .setModRate (clamped);
    midTank_   .setModRate (clamped);
    trebleTank_.setModRate (clamped);
}

void HallReverb::setFreeze (bool frozen)
{
    bassTank_  .setFreeze (frozen);
    midTank_   .setFreeze (frozen);
    trebleTank_.setFreeze (frozen);
}

void HallReverb::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
}

void HallReverb::setDamping (float amount)
{
    // Legacy uniform-damping path: broadcast the same coefficient to all
    // three SubTanks. Equivalent to setBandDamping(a, a, a).
    setBandDamping (amount, amount, amount);
}

void HallReverb::setBandDamping (float bass, float mid, float treble)
{
    dampingBass_   = std::clamp (bass,   0.0f, 0.95f);
    dampingMid_    = std::clamp (mid,    0.0f, 0.95f);
    dampingTreble_ = std::clamp (treble, 0.0f, 0.95f);
    bassTank_  .setDamping (dampingBass_);
    midTank_   .setDamping (dampingMid_);
    trebleTank_.setDamping (dampingTreble_);
}

void HallReverb::setBandGain (float bass, float mid, float treble)
{
    gainBass_   = std::max (0.0f, bass);
    gainMid_    = std::max (0.0f, mid);
    gainTreble_ = std::max (0.0f, treble);
}

void HallReverb::setBassDamping (float c)
{
    dampingBass_ = std::clamp (c, 0.0f, 0.95f);
    bassTank_.setDamping (dampingBass_);
}

void HallReverb::setMidDamping (float c)
{
    dampingMid_ = std::clamp (c, 0.0f, 0.95f);
    midTank_.setDamping (dampingMid_);
}

void HallReverb::setTrebleDamping (float c)
{
    dampingTreble_ = std::clamp (c, 0.0f, 0.95f);
    trebleTank_.setDamping (dampingTreble_);
}

void HallReverb::setBassGain   (float g) { gainBass_   = std::max (0.0f, g); }
void HallReverb::setMidGain    (float g) { gainMid_    = std::max (0.0f, g); }
void HallReverb::setTrebleGain (float g) { gainTreble_ = std::max (0.0f, g); }

void HallReverb::setInlineDiffusion (float coeff)
{
    // Uniform across all 3 sub-tanks. Per-band override available via
    // setBandInlineDiffusion if tuning needs it (not currently exposed).
    bassTank_  .setInlineDiffusion (coeff);
    midTank_   .setInlineDiffusion (coeff);
    trebleTank_.setInlineDiffusion (coeff);
}

void HallReverb::setStereoWidth (float b)
{
    // Clamp to [-0.4, +0.4]. Beyond ±0.5 the matrix approaches singular
    // (mid-side decoder territory) — channels start cancelling for
    // correlated content. The range here covers everything from strong
    // narrowing (b = -0.4 ≈ near-mono) to strong widening (b = +0.4 ≈
    // -0.6 stereo_correlation typical) without flipping into the
    // pathological zone.
    stereoWidth_ = std::clamp (b, -0.4f, 0.4f);
}

void HallReverb::setTankDiffusion (float /*amount*/)
{
    // No-op. Tank density comes from per-band Hadamard mixing; the inline
    // allpass diffusion that FDNReverb exposed via this setter is absent in
    // the 3-band parallel topology. Kept as a stub so DuskVerbEngine's
    // existing forwarder call into HallReverb compiles and silently no-ops
    // when the "Diffusion" knob is automated.
}

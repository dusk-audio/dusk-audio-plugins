#include "HallReverb.h"

#include <algorithm>
#include <cmath>

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
    updateCrossovers();

    bassTank_  .setLFOPhaseOffset (kBassBandPhase);
    midTank_   .setLFOPhaseOffset (kMidBandPhase);
    trebleTank_.setLFOPhaseOffset (kTrebleBandPhase);

    bassTank_  .prepare (sampleRate, kBassDelays,   scratchBlockSize_);
    midTank_   .prepare (sampleRate, kMidDelays,    scratchBlockSize_);
    trebleTank_.prepare (sampleRate, kTrebleDelays, scratchBlockSize_);

    const size_t sz = static_cast<size_t> (scratchBlockSize_);
    bassInL_  .assign (sz, 0.0f); bassInR_  .assign (sz, 0.0f);
    midInL_   .assign (sz, 0.0f); midInR_   .assign (sz, 0.0f);
    trebleInL_.assign (sz, 0.0f); trebleInR_.assign (sz, 0.0f);
    bassOutL_ .assign (sz, 0.0f); bassOutR_ .assign (sz, 0.0f);
    midOutL_  .assign (sz, 0.0f); midOutR_  .assign (sz, 0.0f);
    trebleOutL_.assign (sz, 0.0f); trebleOutR_.assign (sz, 0.0f);

    prepared_ = true;
    updateSubTankDecays();
    setDamping (dampingAmount_);
}

void HallReverb::clearBuffers()
{
    bassTank_.clear();
    midTank_.clear();
    trebleTank_.clear();
    splitL_.reset();
    splitR_.reset();
}

void HallReverb::updateCrossovers()
{
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    splitL_.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);
    splitR_.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);
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

        // ── LR4 band split per channel ──
        for (int i = 0; i < n; ++i)
        {
            const float xL = inputL[offset + i];
            const float xR = inputR[offset + i];
            float bL, mL, tL, bR, mR, tR;
            splitL_.split (xL, bL, mL, tL);
            splitR_.split (xR, bR, mR, tR);
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

        // ── Sum bands → output (optional soft-clip saturation) ──
        const float sat = saturationAmount_;
        const bool  doSat = sat > 0.0001f;
        const float satDrive    = 1.0f + sat * 4.0f;
        const float invSatDrive = 1.0f / satDrive;

        for (int i = 0; i < n; ++i)
        {
            float oL = bassOutL_[i] + midOutL_[i] + trebleOutL_[i];
            float oR = bassOutR_[i] + midOutR_[i] + trebleOutR_[i];
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
    dampingAmount_ = std::clamp (amount, 0.0f, 0.95f);
    bassTank_  .setDamping (dampingAmount_);
    midTank_   .setDamping (dampingAmount_);
    trebleTank_.setDamping (dampingAmount_);
}

void HallReverb::setTankDiffusion (float /*amount*/)
{
    // No-op. Tank density comes from per-band Hadamard mixing; the inline
    // allpass diffusion that FDNReverb exposed via this setter is absent in
    // the 3-band parallel topology. Kept as a stub so DuskVerbEngine's
    // existing forwarder call into HallReverb compiles and silently no-ops
    // when the "Diffusion" knob is automated.
}

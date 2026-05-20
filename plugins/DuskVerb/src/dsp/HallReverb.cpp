#include "HallReverb.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

// Out-of-line storage for the static constexpr arrays. C++17 makes these
// inline by default for in-class definitions, but emitting the addresses
// once here is harmless and keeps older toolchains happy.
constexpr float HallReverb::kDefaultTapTimesMs[HallReverb::kNumPredelayTaps];
constexpr float HallReverb::kDefaultTapWeights[HallReverb::kNumPredelayTaps];
constexpr float HallReverb::kDefaultSpecularTimesMs[HallReverb::kNumSpecularTaps];
constexpr float HallReverb::kDefaultSpecularWeights[HallReverb::kNumSpecularTaps];
constexpr float HallReverb::kSpecularSignL[HallReverb::kNumSpecularTaps];
constexpr float HallReverb::kSpecularSignR[HallReverb::kNumSpecularTaps];

// APVTS maximum for tap times (matches the hall_tap_N_ms NormalisableRange
// in PluginProcessor::createParameterLayout). The predelay ring buffer is
// sized for this value so per-tap delay changes never realloc on the
// audio thread.
static constexpr float kMaxTapTimeMs = 250.0f;

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
    // P12 16-channel expansion. Doubled per-band coprime prime sets;
    // additional primes extend each band's range upward to add longer
    // loops alongside the original short ones (richer modal spread and
    // longer EDT decorrelation per band). All primes mutually coprime
    // within each set + across sets to keep modal frequencies
    // non-overlapping; verified vs HallSubTank::kInlineAPDelays.
    //   Bass:   1543..2417  (orig 8) + 2549..3433  (new 8) — 1500..3400 range
    //   Mid:     541..809   (orig 8) +  829..1021  (new 8) — 540..1020 range
    //   Treble:  211..331   (orig 8) +  347..433   (new 8) — 210..435 range
    constexpr int kBassDelays  [16] = {
        1543, 1607, 1721, 1847, 1993, 2143, 2281, 2417,
        2549, 2671, 2789, 2917, 3037, 3169, 3299, 3433
    };
    constexpr int kMidDelays   [16] = {
         541,  571,  613,  643,  683,  719,  761,  809,
         829,  853,  881,  907,  937,  967,  991, 1021
    };
    constexpr int kTrebleDelays[16] = {
         211,  227,  239,  257,  271,  283,  307,  331,
         347,  359,  367,  379,  389,  401,  421,  433
    };

    // Anti-correlated LFO phase offsets per band — bass / mid / treble each
    // see a different sub-tank modulation pattern. Keeps periodicity from
    // building up across bands (would manifest as a "wash" at the modulation
    // period if all bands shared a phase).
    constexpr float kBassBandPhase   = 0.0f;
    constexpr float kMidBandPhase    = 1.047f;   // ≈ 2π/6
    constexpr float kTrebleBandPhase = 2.094f;   // ≈ 4π/6
}

HallReverb::HallReverb()
{
    // Seed mutable tap arrays from the defaults so APVTS setters have a
    // sensible starting point until the host pushes its own values.
    for (int t = 0; t < kNumPredelayTaps; ++t)
    {
        tapTimesMs_[t] = kDefaultTapTimesMs[t];
        tapWeights_[t] = kDefaultTapWeights[t];
    }
    for (int s = 0; s < kNumSpecularTaps; ++s)
    {
        specularTimesMs_[s] = kDefaultSpecularTimesMs[s];
        specularWeights_[s] = kDefaultSpecularWeights[s];
    }
}

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

    // Predelay ring buffer — sized for the APVTS maximum tap time (250 ms)
    // plus headroom, so any combination of per-tap setter values reachable
    // through the host parameter range fits without reallocation.
    const float maxTapSec     = kMaxTapTimeMs * 0.001f;
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
    specularInL_.assign (sz, 0.0f); specularInR_.assign (sz, 0.0f);

    prepared_ = true;
    updateSubTankDecays();
    setBandDamping (dampingBass_, dampingMid_, dampingTreble_);
    // Push per-band damping fc to each SubTank now that prepare() has
    // set sampleRate_ — recomputes the LP alpha from fc + sr.
    bassTank_  .setDampingFc (dampingFcBass_);
    midTank_   .setDampingFc (dampingFcMid_);
    trebleTank_.setDampingFc (dampingFcTreble_);
    recomputeSpecularTaps();
    recomputeSpecularLP();
    specularLP1StateL_ = specularLP1StateR_ = 0.0f;
    specularLP2StateL_ = specularLP2StateR_ = 0.0f;
    // Initialize wet Hi Cut biquad in bypass state. setWetHiCutHz() from
    // DuskVerbEngine will configure it to the user's hi_cut value.
    const float sr = static_cast<float> (sampleRate);
    wetHiCutBiquadL_.designLP (wetHiCutHz_, sr);
    wetHiCutBiquadR_.designLP (wetHiCutHz_, sr);
    wetHiCutBiquadL_.reset();
    wetHiCutBiquadR_.reset();

    // P10 peaking-EQ biquads — design at current (gain_dB, Q) so any
    // value pushed via APVTS before prepare is already in effect.
    bassEQL_  .designPeaking (bassEQFc_,   bassEQQ_,   bassEQGainDb_,   sr);
    bassEQR_  .designPeaking (bassEQFc_,   bassEQQ_,   bassEQGainDb_,   sr);
    midEQL_   .designPeaking (midEQFc_,    midEQQ_,    midEQGainDb_,    sr);
    midEQR_   .designPeaking (midEQFc_,    midEQQ_,    midEQGainDb_,    sr);
    trebleEQL_.designPeaking (trebleEQFc_, trebleEQQ_, trebleEQGainDb_, sr);
    trebleEQR_.designPeaking (trebleEQFc_, trebleEQQ_, trebleEQGainDb_, sr);
    bassEQL_.reset();   bassEQR_.reset();
    midEQL_.reset();    midEQR_.reset();
    trebleEQL_.reset(); trebleEQR_.reset();

    // P11 post-tank high-shelf biquads — same prepare pattern as the
    // peaking EQs above. Defaults gainDb=0 → flat passthrough; calibration
    // dials in negative gain on each band for HF rolloff that closes
    // c80/d50 without re-introducing the centroid_drift drift the
    // in-feedback damping LP causes.
    bassShelfL_  .designHighShelf (bassShelfFc_,   bassShelfGainDb_,   sr);
    bassShelfR_  .designHighShelf (bassShelfFc_,   bassShelfGainDb_,   sr);
    midShelfL_   .designHighShelf (midShelfFc_,    midShelfGainDb_,    sr);
    midShelfR_   .designHighShelf (midShelfFc_,    midShelfGainDb_,    sr);
    trebleShelfL_.designHighShelf (trebleShelfFc_, trebleShelfGainDb_, sr);
    trebleShelfR_.designHighShelf (trebleShelfFc_, trebleShelfGainDb_, sr);
    bassShelfL_.reset();   bassShelfR_.reset();
    midShelfL_.reset();    midShelfR_.reset();
    trebleShelfL_.reset(); trebleShelfR_.reset();
}

void HallReverb::recomputePredelayTaps()
{
    // Convert each tap's ms into sample count at the current sample rate
    // and recompute tapNorm so summed tap output has unity DC gain.
    // Reads from the mutable per-tap arrays (tapTimesMs_, tapWeights_)
    // which the APVTS setters update — keeps tank input level identical
    // to the bare-tank case so the soft-clip threshold and any preset
    // gainTrim calibration carry over unchanged regardless of tap config.
    float sumW = 0.0f;
    for (int t = 0; t < kNumPredelayTaps; ++t)
    {
        tapSamples_[t] = static_cast<int> (std::round (tapTimesMs_[t]
                                            * 0.001f * static_cast<float> (sampleRate_)));
        if (tapSamples_[t] > predelayMask_) tapSamples_[t] = predelayMask_;
        if (tapSamples_[t] < 0)             tapSamples_[t] = 0;
        sumW += tapWeights_[t];
    }
    tapNorm_ = (sumW > 0.0f) ? (1.0f / sumW) : 1.0f;
}

void HallReverb::setTapTimeMs (int index, float ms)
{
    if (index < 0 || index >= kNumPredelayTaps) return;
    tapTimesMs_[index] = std::clamp (ms, 0.0f, kMaxTapTimeMs);
    recomputePredelayTaps();
}

void HallReverb::setTapWeight (int index, float weight)
{
    if (index < 0 || index >= kNumPredelayTaps) return;
    tapWeights_[index] = std::max (0.0f, weight);
    recomputePredelayTaps();
}

void HallReverb::recomputeSpecularTaps()
{
    // Specular delays live in the same predelay ring as the multi-tap
    // injection — sized at prepare() for kMaxTapTimeMs so any specular
    // time in [0, 50] ms fits without realloc.
    for (int s = 0; s < kNumSpecularTaps; ++s)
    {
        specularTapSamples_[s] = static_cast<int> (std::round (
            specularTimesMs_[s] * 0.001f * static_cast<float> (sampleRate_)));
        if (specularTapSamples_[s] > predelayMask_) specularTapSamples_[s] = predelayMask_;
        if (specularTapSamples_[s] < 0)             specularTapSamples_[s] = 0;
    }
}

void HallReverb::recomputeSpecularLP()
{
    // Cascade of 2× 1-pole LPs (P8c — strictly monotonic impulse so the
    // peak_locations_ms detector doesn't pick up spurious rebound peaks).
    // Each stage uses alpha = 1 − exp(−2π · fc / sr) with the SAME
    // per-stage cutoff; the cascade is -6 dB at fc instead of the -3 dB
    // a single biquad would give at the same fc, which is acceptable for
    // a guardrail (exact corner doesn't matter, slope and impulse shape
    // do).
    const float fc = std::max (50.0f, std::min (specularHFCutHz_, 20000.0f));
    const float twoPiFcOverSr = 6.283185307179586f * fc
                              / static_cast<float> (std::max (sampleRate_, 1000.0));
    specularLPAlpha_ = 1.0f - std::exp (-twoPiFcOverSr);
}

void HallReverb::setWetHiCutHz (float hz)
{
    wetHiCutHz_ = std::clamp (hz, 50.0f, 20000.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    wetHiCutBiquadL_.designLP (wetHiCutHz_, sr);
    wetHiCutBiquadR_.designLP (wetHiCutHz_, sr);
}

void HallReverb::setBassEQ (float gainDb, float q)
{
    bassEQGainDb_ = std::clamp (gainDb, -18.0f, 18.0f);
    bassEQQ_      = std::clamp (q,        0.3f,  6.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    bassEQL_.designPeaking (bassEQFc_, bassEQQ_, bassEQGainDb_, sr);
    bassEQR_.designPeaking (bassEQFc_, bassEQQ_, bassEQGainDb_, sr);
}

void HallReverb::setMidEQ (float gainDb, float q)
{
    midEQGainDb_ = std::clamp (gainDb, -18.0f, 18.0f);
    midEQQ_      = std::clamp (q,        0.3f,  6.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    midEQL_.designPeaking (midEQFc_, midEQQ_, midEQGainDb_, sr);
    midEQR_.designPeaking (midEQFc_, midEQQ_, midEQGainDb_, sr);
}

void HallReverb::setTrebleEQ (float gainDb, float q)
{
    trebleEQGainDb_ = std::clamp (gainDb, -18.0f, 18.0f);
    trebleEQQ_      = std::clamp (q,        0.3f,  6.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    trebleEQL_.designPeaking (trebleEQFc_, trebleEQQ_, trebleEQGainDb_, sr);
    trebleEQR_.designPeaking (trebleEQFc_, trebleEQQ_, trebleEQGainDb_, sr);
}

// Per-band EQ centre-frequency setters. Each clamps to a band-local
// range so the optimizer can't push a band's EQ outside its sub-tank's
// passband. Range bounds picked to keep ±octave of headroom around
// the default while staying inside the LR4 split's bass/mid/treble
// territory (split fLow/fHigh defaults 600/4500).
void HallReverb::setBassEQFc (float hz)
{
    bassEQFc_ = std::clamp (hz, 50.0f, 800.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    bassEQL_.designPeaking (bassEQFc_, bassEQQ_, bassEQGainDb_, sr);
    bassEQR_.designPeaking (bassEQFc_, bassEQQ_, bassEQGainDb_, sr);
}

void HallReverb::setMidEQFc (float hz)
{
    midEQFc_ = std::clamp (hz, 200.0f, 5000.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    midEQL_.designPeaking (midEQFc_, midEQQ_, midEQGainDb_, sr);
    midEQR_.designPeaking (midEQFc_, midEQQ_, midEQGainDb_, sr);
}

void HallReverb::setTrebleEQFc (float hz)
{
    trebleEQFc_ = std::clamp (hz, 2000.0f, 18000.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    trebleEQL_.designPeaking (trebleEQFc_, trebleEQQ_, trebleEQGainDb_, sr);
    trebleEQR_.designPeaking (trebleEQFc_, trebleEQQ_, trebleEQGainDb_, sr);
}

void HallReverb::setSpecularTimeMs (int index, float ms)
{
    if (index < 0 || index >= kNumSpecularTaps) return;
    // Cap at 50 ms — specular peaks past that aren't peaks_locations_ms
    // candidates (the metric only scans 0-50 ms post-onset) and would
    // bleed into the sub-tank wet window's territory.
    specularTimesMs_[index] = std::clamp (ms, 0.0f, 50.0f);
    if (prepared_) recomputeSpecularTaps();
}

void HallReverb::setSpecularWeight (int index, float weight)
{
    if (index < 0 || index >= kNumSpecularTaps) return;
    specularWeights_[index] = std::max (0.0f, weight);
}

void HallReverb::setSpecularHFCutHz (float hz)
{
    specularHFCutHz_ = std::clamp (hz, 50.0f, 20000.0f);
    if (prepared_) recomputeSpecularLP();
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
    specularLP1StateL_ = specularLP1StateR_ = 0.0f;
    specularLP2StateL_ = specularLP2StateR_ = 0.0f;
    wetHiCutBiquadL_.reset();
    wetHiCutBiquadR_.reset();
    bassEQL_.reset();   bassEQR_.reset();
    midEQL_.reset();    midEQR_.reset();
    trebleEQL_.reset(); trebleEQR_.reset();
    bassShelfL_.reset();   bassShelfR_.reset();
    midShelfL_.reset();    midShelfR_.reset();
    trebleShelfL_.reset(); trebleShelfR_.reset();
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
                sumL += predelayL_[static_cast<size_t> (readIdx)] * tapWeights_[t];
                sumR += predelayR_[static_cast<size_t> (readIdx)] * tapWeights_[t];
            }
            // ── P8b specular reads happen HERE (per-sample) so they use
            // the per-sample write head; staging arrays consumed in the
            // band-sum loop below where one-pole LP + output mix happens.
            float specL = 0.0f, specR = 0.0f;
            for (int s = 0; s < kNumSpecularTaps; ++s)
            {
                const int sIdx = (predelayWritePos_ - specularTapSamples_[s]) & predelayMask_;
                specL += specularWeights_[s] * kSpecularSignL[s]
                       * predelayL_[static_cast<size_t> (sIdx)];
                specR += specularWeights_[s] * kSpecularSignR[s]
                       * predelayR_[static_cast<size_t> (sIdx)];
            }
            specularInL_[static_cast<size_t> (i)] = specL;
            specularInR_[static_cast<size_t> (i)] = specR;

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

            // P10 per-band peaking EQ — applied to each band's post-LR4
            // contribution before the band-gain sum. Each EQ only sees
            // content from its own band so cross-band spectral leakage
            // is minimised; gain shapes the bump centred at the band's
            // mid-octave fc. Default gain 0 dB = unity passthrough.
            const float eqBassL   = bassEQL_  .process (bL_b);
            const float eqBassR   = bassEQR_  .process (bR_b);
            const float eqMidL    = midEQL_   .process (mL_m);
            const float eqMidR    = midEQR_   .process (mR_m);
            const float eqTrebleL = trebleEQL_.process (tL_t);
            const float eqTrebleR = trebleEQR_.process (tR_t);

            // P11 post-tank high-shelf — decoupled HF rolloff per band.
            // Replaces in-feedback damping LP for closing c80/d50 without
            // re-introducing the centroid_drift drift that an in-loop LP
            // causes. Default gainDb=0 → flat passthrough.
            const float shBassL   = bassShelfL_  .process (eqBassL);
            const float shBassR   = bassShelfR_  .process (eqBassR);
            const float shMidL    = midShelfL_   .process (eqMidL);
            const float shMidR    = midShelfR_   .process (eqMidR);
            const float shTrebleL = trebleShelfL_.process (eqTrebleL);
            const float shTrebleR = trebleShelfR_.process (eqTrebleR);

            float wetL = shBassL * gB + shMidL * gM + shTrebleL * gT;
            float wetR = shBassR * gB + shMidR * gM + shTrebleR * gT;

            // ── P8c — wet-only Hi Cut (2-pole RBJ LP) ─────────────────
            // Replaces DuskVerbEngine's global Hi Cut filter for this
            // engine (DuskVerbEngine bypasses its filter when
            // currentEngine_ == Hall). Wet tank tail gets shaped here;
            // specular path stays untouched by this filter.
            wetL = wetHiCutBiquadL_.process (wetL);
            wetR = wetHiCutBiquadR_.process (wetR);

            float oL = wetL - b * wetR;
            float oR = wetR - b * wetL;

            // ── P8b/c — direct specular taps (bypass sub-tanks + widener
            // + wet Hi Cut). Per-sample raw specular sum was captured in
            // the injection loop while predelayWritePos_ tracked sample i.
            // Cascade of 2× 1-pole LPs gives 12 dB/oct with a strictly
            // monotonic impulse response — earlier 2-pole RBJ biquad's
            // small negative impulse lobes were detected by the
            // peak_locations_ms metric as ghost peaks.
            const float rawSpecL = specularInL_[static_cast<size_t> (i)];
            const float rawSpecR = specularInR_[static_cast<size_t> (i)];
            const float a1 = specularLPAlpha_;
            const float oma = 1.0f - a1;
            specularLP1StateL_ = a1 * rawSpecL + oma * specularLP1StateL_;
            specularLP1StateR_ = a1 * rawSpecR + oma * specularLP1StateR_;
            specularLP2StateL_ = a1 * specularLP1StateL_ + oma * specularLP2StateL_;
            specularLP2StateR_ = a1 * specularLP1StateR_ + oma * specularLP2StateR_;
            oL += specularLP2StateL_;
            oR += specularLP2StateR_;

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

void HallReverb::setBassDampingFc (float hz)
{
    dampingFcBass_ = std::clamp (hz, 100.0f, 20000.0f);
    bassTank_.setDampingFc (dampingFcBass_);
}

void HallReverb::setMidDampingFc (float hz)
{
    dampingFcMid_ = std::clamp (hz, 100.0f, 20000.0f);
    midTank_.setDampingFc (dampingFcMid_);
}

void HallReverb::setTrebleDampingFc (float hz)
{
    dampingFcTreble_ = std::clamp (hz, 100.0f, 20000.0f);
    trebleTank_.setDampingFc (dampingFcTreble_);
}

void HallReverb::setBassModDepth   (float samples) { bassTank_  .setModDepth (samples); }
void HallReverb::setBassModRate    (float hz)      { bassTank_  .setModRate  (hz); }
void HallReverb::setMidModDepth    (float samples) { midTank_   .setModDepth (samples); }
void HallReverb::setMidModRate     (float hz)      { midTank_   .setModRate  (hz); }
void HallReverb::setTrebleModDepth (float samples) { trebleTank_.setModDepth (samples); }
void HallReverb::setTrebleModRate  (float hz)      { trebleTank_.setModRate  (hz); }
void HallReverb::setBassModShape   (float shape)   { bassTank_  .setModShape (shape); }
void HallReverb::setMidModShape    (float shape)   { midTank_   .setModShape (shape); }
void HallReverb::setTrebleModShape (float shape)   { trebleTank_.setModShape (shape); }
void HallReverb::setBassChannelGainSpread   (float s) { bassTank_  .setChannelGainSpread (s); }
void HallReverb::setMidChannelGainSpread    (float s) { midTank_   .setChannelGainSpread (s); }
void HallReverb::setTrebleChannelGainSpread (float s) { trebleTank_.setChannelGainSpread (s); }

void HallReverb::setBassShelfGain (float dB)
{
    bassShelfGainDb_ = std::clamp (dB, -24.0f, 6.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    bassShelfL_.designHighShelf (bassShelfFc_, bassShelfGainDb_, sr);
    bassShelfR_.designHighShelf (bassShelfFc_, bassShelfGainDb_, sr);
}

void HallReverb::setBassShelfFc (float hz)
{
    bassShelfFc_ = std::clamp (hz, 100.0f, 16000.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    bassShelfL_.designHighShelf (bassShelfFc_, bassShelfGainDb_, sr);
    bassShelfR_.designHighShelf (bassShelfFc_, bassShelfGainDb_, sr);
}

void HallReverb::setMidShelfGain (float dB)
{
    midShelfGainDb_ = std::clamp (dB, -24.0f, 6.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    midShelfL_.designHighShelf (midShelfFc_, midShelfGainDb_, sr);
    midShelfR_.designHighShelf (midShelfFc_, midShelfGainDb_, sr);
}

void HallReverb::setMidShelfFc (float hz)
{
    midShelfFc_ = std::clamp (hz, 100.0f, 16000.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    midShelfL_.designHighShelf (midShelfFc_, midShelfGainDb_, sr);
    midShelfR_.designHighShelf (midShelfFc_, midShelfGainDb_, sr);
}

void HallReverb::setTrebleShelfGain (float dB)
{
    trebleShelfGainDb_ = std::clamp (dB, -24.0f, 6.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    trebleShelfL_.designHighShelf (trebleShelfFc_, trebleShelfGainDb_, sr);
    trebleShelfR_.designHighShelf (trebleShelfFc_, trebleShelfGainDb_, sr);
}

void HallReverb::setTrebleShelfFc (float hz)
{
    trebleShelfFc_ = std::clamp (hz, 100.0f, 16000.0f);
    if (! prepared_) return;
    const float sr = static_cast<float> (sampleRate_);
    trebleShelfL_.designHighShelf (trebleShelfFc_, trebleShelfGainDb_, sr);
    trebleShelfR_.designHighShelf (trebleShelfFc_, trebleShelfGainDb_, sr);
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

#include "PlateEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// =====================================================================
// DelayLine / Allpass internal helpers (mirror SixAPTank patterns).
// =====================================================================

void PlateEngine::DelayLine::allocate (int maxSamples)
{
    int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 4, 16));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

void PlateEngine::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float PlateEngine::DelayLine::readInterpolated (float delaySamples) const
{
    int   intPart  = static_cast<int> (delaySamples);
    float fracPart = delaySamples - static_cast<float> (intPart);
    int   readPos  = (writePos - intPart - 1) & mask;
    // 6-point 5th-order Lagrange. Holds 16 kHz amplitude at 48 kHz sample
    // rate (cubicHermite rolled it off ~3 dB, which appeared as a fixed
    // 0.3 s RT60 deficit at 16 kHz on the Lex Rich Plate match regardless
    // of Hi Cut / Treble Mult settings).
    return DspUtils::lagrange6 (buffer.data(), mask, readPos, 1.0f - fracPart);
}

void PlateEngine::Allpass::allocate (int maxSamples)
{
    int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 4, 16));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

void PlateEngine::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// =====================================================================
// LR4 3-band split (Linkwitz-Riley 24 dB/oct)
// =====================================================================
//
// RBJ-cookbook 2nd-order Butterworth (Q = 1/√2). LR4 = 2 cascaded copies
// of the same RBJ 2nd-order filter at the same fc — gives a 4th-order
// 24 dB/oct rolloff with a Q = 1/2 (critically damped) effective response,
// and the sum LR4_LP + LR4_HP at one xover is a unity-gain allpass.

namespace
{
constexpr float kTwoPiInternal = 6.283185307179586f;
constexpr float kSqrt12_LR4    = 0.7071067811865475f;
} // anonymous namespace

void PlateEngine::LR4BandSplit::Biquad::designLP (float fcHz, float sr)
{
    const float w0    = kTwoPiInternal * std::min (fcHz, 0.49f * sr) / sr;
    const float cosw  = std::cos (w0);
    const float sinw  = std::sin (w0);
    const float alpha = sinw / (2.0f * kSqrt12_LR4);
    const float a0    = 1.0f + alpha;
    b0 = (1.0f - cosw) * 0.5f / a0;
    b1 = (1.0f - cosw)        / a0;
    b2 = (1.0f - cosw) * 0.5f / a0;
    a1 = -2.0f * cosw         / a0;
    a2 = (1.0f - alpha)       / a0;
}

void PlateEngine::LR4BandSplit::Biquad::designHP (float fcHz, float sr)
{
    const float w0    = kTwoPiInternal * std::min (fcHz, 0.49f * sr) / sr;
    const float cosw  = std::cos (w0);
    const float sinw  = std::sin (w0);
    const float alpha = sinw / (2.0f * kSqrt12_LR4);
    const float a0    = 1.0f + alpha;
    b0 =  (1.0f + cosw) * 0.5f / a0;
    b1 = -(1.0f + cosw)        / a0;
    b2 =  (1.0f + cosw) * 0.5f / a0;
    a1 = -2.0f * cosw          / a0;
    a2 = (1.0f - alpha)        / a0;
}

void PlateEngine::LR4BandSplit::setCoefficients (float gLow_, float gMid_,
                                                 float gHigh_,
                                                 float fLowHz, float fHighHz,
                                                 float sr)
{
    gLow  = gLow_;
    gMid  = gMid_;
    gHigh = gHigh_;
    // Keep fHigh above fLow so the band assignment stays monotonic.
    const float fLowClamped  = std::clamp (fLowHz, 20.0f, 0.45f * sr);
    const float fHighClamped = std::max (fHighHz, fLowClamped + 10.0f);
    lpA.designLP (fLowClamped,  sr);
    lpB.designLP (fLowClamped,  sr);
    hpA.designHP (fHighClamped, sr);
    hpB.designHP (fHighClamped, sr);
}

// =====================================================================
// Construction / preparation
// =====================================================================

PlateEngine::PlateEngine() = default;

void PlateEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;

    auto setupBranch = [sampleRate] (Branch& b,
                                     int in1Base, int in2Base,
                                     int del1Base, int del2Base,
                                     int ap2Base,
                                     const int (&densBase)[kNumDensityAPs])
    {
        b.in1BaseDelay     = in1Base;
        b.in2BaseDelay     = in2Base;
        b.delay1BaseDelay  = del1Base;
        b.delay2BaseDelay  = del2Base;
        b.ap2BaseDelay     = ap2Base;
        for (int i = 0; i < kNumDensityAPs; ++i)
            b.densityAPBase[i] = densBase[i];

        const float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
        const int   reserve   = static_cast<int> (
            static_cast<float> (kMaxBaseDelay) * rateRatio + 64.0f);

        b.in1   .allocate (static_cast<int> (in1Base * rateRatio + 16.0f));
        b.in2   .allocate (static_cast<int> (in2Base * rateRatio + 16.0f));
        b.delay1.allocate (reserve);
        b.delay2.allocate (reserve);
        b.ap2   .allocate (static_cast<int> (ap2Base * rateRatio + 16.0f));

        // Density APs sized for max-size scale so the delay can grow with
        // the size knob without re-allocating on the audio thread.
        constexpr float kMaxSizeScale = 1.5f;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            b.densityAP[i].allocate (static_cast<int> (
                static_cast<float> (densBase[i]) * rateRatio * kMaxSizeScale + 16.0f));
            // Aggressive 5 % jitter — the design fix for the modal humps
            // that locked SixAPTank into 12/19 metrics on the Lex Vocal
            // Plate target. Each density AP wanders independently because
            // its LFO has a distinct seed (set below).
            b.densityAP[i].jitterDepthFraction = 0.05f;
        }

        b.damping.prepare (static_cast<float> (sampleRate));
        // Initial 3-band coefficients — overwritten by first
        // updateDecayCoefficients() call below. xover Hz instead of exp
        // coefficients now that LR4 split replaced ThreeBandDamping.
        b.damping.setCoefficients (0.99f, 0.95f, 0.5f,
                                   500.0f, 5000.0f,
                                   static_cast<float> (sampleRate));

        b.inputHighShelf.reset();
        b.crossFeedState = 0.0f;
    };

    setupBranch (leftBranch_,
                 kLeftIn1Base,  kLeftIn2Base,
                 kLeftDel1Base, kLeftDel2Base,
                 kLeftAP2Base,
                 kLeftDensityAPBase);

    setupBranch (rightBranch_,
                 kRightIn1Base, kRightIn2Base,
                 kRightDel1Base, kRightDel2Base,
                 kRightAP2Base,
                 kRightDensityAPBase);

    // Distinct LFO seeds per density AP × per L/R branch — none ever align
    // their random walks. Seeds chosen as large coprimes so their hashed
    // outputs decorrelate fast.
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        const std::uint32_t lSeed = 0xFEEDB0B0u + static_cast<std::uint32_t> (i * 41039);
        const std::uint32_t rSeed = 0xDECAFFEEu + static_cast<std::uint32_t> (i * 38873);
        leftBranch_ .densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), lSeed);
        rightBranch_.densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), rSeed);
    }

    updateDelayLengths();
    applyDensityScale();
    updateDecayCoefficients();

    // Internal pre-delay buffer — non-recursive 20 ms pre-delay on each
    // dry input. Lex Vintage Plate Rich Plate has late-energy-dominant
    // C80 / D50; this shifts the entire reverb onset back by 20 ms so
    // the 50 / 80 ms early-window contains less recently-injected energy.
    {
        const int pdSamplesAt48k = 960;  // 20 ms × 48 kHz
        // +1 compensates for write-then-read order in InternalPredelay
        // (write advances writePos, so the read at writePos-delay reads
        // delay-1 samples back; +1 here restores the exact 20 ms).
        const int pdDelay = static_cast<int> (
            static_cast<float> (pdSamplesAt48k) * (sampleRate / 48000.0)) + 1;
        const int reservePD = pdDelay + 64;
        internalPredelayL_.allocate (reservePD);
        internalPredelayR_.allocate (reservePD);
        internalPredelayL_.clear();
        internalPredelayR_.clear();
        // Bound delaySamples against the ring buffer's mask so a future
        // size-bump without growing reservePD wraps to the max valid
        // delay instead of reading random history via the mask AND.
        const int pdSafe = std::min (pdDelay, internalPredelayL_.delay.mask);
        internalPredelayL_.delaySamples = pdSafe;
        internalPredelayR_.delaySamples = pdSafe;
    }

    // Parallel bass extension resonator — per-channel. Slightly different
    // delay lengths to keep stereo width. Buffer sizes account for 88.2k
    // (rateRatio=2) plus slack. Prime delays at 48k: ~62 ms = 2971 L,
    // ~67 ms = 3221 R. Feedback / output gain tuned at first
    // updateDecayCoefficients() once decayTime_ is set by host.
    {
        const float rateRatioBE = static_cast<float> (sampleRate / 44100.0);
        const int reserveBE = std::max (4096,
            static_cast<int> (6400.0f * rateRatioBE) + 64);
        bassExtL_.allocate (reserveBE);
        bassExtR_.allocate (reserveBE);
        bassExtL_.prepare (static_cast<float> (sampleRate));
        bassExtR_.prepare (static_cast<float> (sampleRate));
        // Delays at ~62 ms / ~67 ms — long enough to sustain a 125 Hz
        // comb resonance over the 1.5 s+ target tail without the
        // resonance peaks crystallising into audible periodicity (slow
        // 16 Hz comb fundamental gets smeared by the input/feedback LP
        // chain). Distinct primes for L/R width.
        bassExtL_.delaySamples = static_cast<int> (2971.0f * rateRatioBE);
        bassExtR_.delaySamples = static_cast<int> (3221.0f * rateRatioBE);
    }

    // Design the per-branch input high-shelf at the new sample rate using
    // whatever gain/fc the host has already pushed in (or the defaults if
    // this is the first prepare() of the session).
    {
        const float gainLin = std::pow (10.0f, inputHighShelfGainDb_ / 20.0f);
        const float sr = static_cast<float> (sampleRate);
        leftBranch_ .inputHighShelf.designHighShelf (gainLin, inputHighShelfFcHz_, sr);
        rightBranch_.inputHighShelf.designHighShelf (gainLin, inputHighShelfFcHz_, sr);
    }

    prepared_ = true;
}

void PlateEngine::clearBuffers()
{
    auto clearBranch = [] (Branch& b)
    {
        b.in1.clear();
        b.in2.clear();
        b.delay1.clear();
        b.delay2.clear();
        b.ap2.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            b.densityAP[i].clear();
        b.damping.reset();
        b.inputHighShelf.reset();
        b.crossFeedState = 0.0f;
    };
    clearBranch (leftBranch_);
    clearBranch (rightBranch_);
    bassExtL_.clear();
    bassExtR_.clear();
    internalPredelayL_.clear();
    internalPredelayR_.clear();
}

// =====================================================================
// Setters
// =====================================================================

void PlateEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, seconds);
    if (prepared_)
        updateDecayCoefficients();
}

void PlateEngine::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (0.05f, mult);
    if (prepared_)
        updateDecayCoefficients();
}

void PlateEngine::setMidMultiply (float mult)
{
    midMultiply_ = std::max (0.05f, mult);
    if (prepared_)
        updateDecayCoefficients();
}

void PlateEngine::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (0.05f, mult);
    if (prepared_)
        updateDecayCoefficients();
}

void PlateEngine::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::clamp (hz, 50.0f, 8000.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void PlateEngine::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::clamp (hz, 500.0f, 18000.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void PlateEngine::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
}

void PlateEngine::setModDepth (float depth)
{
    // Plate's modulation IS the per-AP jitter — there is no separate chorus
    // LFO on the cascade reads. The user's Mod Depth knob scales the per-AP
    // jitter depth fraction within a sensible range; full depth (1.0) gets
    // ~7 % wander, zero depth disables jitter entirely (which exposes the
    // modal humps — useful only for A/B verification of the design fix).
    modDepth_ = std::clamp (depth, 0.0f, 1.0f);
    const float frac = 0.02f + 0.05f * modDepth_;   // 0.02..0.07 range
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        leftBranch_ .densityAP[i].jitterDepthFraction = frac;
        rightBranch_.densityAP[i].jitterDepthFraction = frac;
    }
    if (prepared_)
    {
        const float sr = static_cast<float> (sampleRate_);
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            leftBranch_ .densityAP[i].updateJitterDepth (sr);
            rightBranch_.densityAP[i].updateJitterDepth (sr);
        }
    }
}

void PlateEngine::setModRate (float hz)
{
    // The per-AP LFOs derive their rate from delay length (in
    // updateJitterDepth), so the user-facing Mod Rate is currently a no-op
    // for the plate engine. Retained for engine-contract compatibility.
    modRate_ = std::clamp (hz, 0.05f, 10.0f);
}

void PlateEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();   // loop length changed → recompute gains
    }
}

void PlateEngine::setFreeze (bool frozen)
{
    if (frozen == frozen_) return;
    frozen_ = frozen;
    if (prepared_)
        updateDecayCoefficients();
}

void PlateEngine::setTankDiffusion (float amount)
{
    lastTankDiffusionAmount_ = std::clamp (amount, 0.0f, 1.0f);
    applyDensityScale();
}

void PlateEngine::setInputHighShelf (float gainDb, float fcHz)
{
    inputHighShelfGainDb_ = std::clamp (gainDb, -24.0f, 24.0f);
    inputHighShelfFcHz_   = std::clamp (fcHz, 200.0f, 18000.0f);
    if (! prepared_)
        return;
    const float gainLin = std::pow (10.0f, inputHighShelfGainDb_ / 20.0f);
    const float sr      = static_cast<float> (sampleRate_);
    leftBranch_ .inputHighShelf.designHighShelf (gainLin, inputHighShelfFcHz_, sr);
    rightBranch_.inputHighShelf.designHighShelf (gainLin, inputHighShelfFcHz_, sr);
}

// =====================================================================
// Internal recompute helpers
// =====================================================================

void PlateEngine::updateDelayLengths()
{
    // Plate size knob has narrower musical range than hall — a "small
    // plate" still feels plate-y, just tighter. Map [0..1] → [0.6..1.2]×.
    const float sizeScale = 0.6f + 0.6f * sizeParam_;
    const float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);

    auto setBranchDelays = [this, sizeScale, rateRatio] (Branch& b)
    {
        // 2-AP input network — does NOT scale with size (it's the "input
        // diffuser" stage; its character should stay fixed across sizes).
        b.in1.delaySamples = std::min (
            static_cast<int> (b.in1BaseDelay * rateRatio), b.in1.mask);
        b.in2.delaySamples = std::min (
            static_cast<int> (b.in2BaseDelay * rateRatio), b.in2.mask);

        // Main figure-8 delays scale with size.
        b.delay1Samples = static_cast<float> (b.delay1BaseDelay) * sizeScale * rateRatio;
        b.delay2Samples = static_cast<float> (b.delay2BaseDelay) * sizeScale * rateRatio;

        // Polish AP2 — scales with size to keep its smear proportional.
        b.ap2.delaySamples = std::min (
            static_cast<int> (b.ap2BaseDelay * sizeScale * rateRatio), b.ap2.mask);

        // Density APs scale with size and refresh their jitter (depth +
        // rate) so the spin-and-wander tracks the new delay lengths.
        const float sr = static_cast<float> (sampleRate_);
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            b.densityAP[i].delaySamples = std::min (
                static_cast<int> (b.densityAPBase[i] * sizeScale * rateRatio),
                b.densityAP[i].mask);
            b.densityAP[i].updateJitterDepth (sr);
        }
    };

    setBranchDelays (leftBranch_);
    setBranchDelays (rightBranch_);
}

void PlateEngine::updateDecayCoefficients()
{
    const float srHz = static_cast<float> (sampleRate_);
    if (frozen_)
    {
        // Frozen plate: unity gain at all bands — tail rings forever. LR4
        // crossover frequencies arbitrary (gains all 1.0 → split is a sum
        // = allpass = unity). Use moderate xover values so the biquad
        // coefficients stay numerically clean.
        leftBranch_ .damping.setCoefficients (1.0f, 1.0f, 1.0f,
                                              500.0f, 5000.0f, srHz);
        rightBranch_.damping.setCoefficients (1.0f, 1.0f, 1.0f,
                                              500.0f, 5000.0f, srHz);
        return;
    }

    // Per-branch loop length = direct delays (delay1 + delay2 + density-AP
    // sum) plus a storage-factor compensation for the energy stored INSIDE
    // each AP. Plate's storage factor is smaller than SixAPTank's because
    // the cascade is shorter so per-AP energy contributes proportionally
    // less. Calibrated empirically: 1.85 at size=0.5, 1.40 at size=1.0.
    const float storageFactor = 2.30f - 0.90f * sizeParam_;

    auto loopLen = [storageFactor] (const Branch& b) {
        float densityLen = 0.0f;
        for (int i = 0; i < kNumDensityAPs; ++i)
            densityLen += static_cast<float> (b.densityAP[i].delaySamples);
        const float densityEffective = densityLen * storageFactor;
        return b.delay1Samples + b.delay2Samples
             + static_cast<float> (b.ap2.delaySamples)
             + densityEffective;
    };

    const float numerator = -3.0f * 2.302585092994046f
                          / static_cast<float> (sampleRate_);

    // Hard ceiling — guarantees provable stability of the cross-coupled
    // figure-8 even at extreme decay × bass mult settings. Raised
    // 2026-05-18 from 0.98 → 0.998 because Rich Plate's 125 Hz band
    // hit the 0.98 cap (frozen RT60 1.61 s with bandGain = 1.0, but
    // non-frozen capped at 1.39 s because the 0.98 ceiling triggered).
    // Stability margin: round-trip gain = bandGain² × kCrossFeedGain² =
    // 0.998² × 0.80² = 0.637 < 1, well below the oscillation boundary;
    // the softClip + kSafetyClip on the feedback tap bound any
    // numerical excursion.
    constexpr float kMaxBandGain = 0.9995f;

    auto bandGain = [numerator, kMaxBandGain, this] (float L, float multiplier) {
        const float effRt60 = std::max (decayTime_ * multiplier, 0.05f);
        return std::clamp (std::exp (numerator * L / effRt60), 0.0f, kMaxBandGain);
    };

    // LR4 split takes corner frequencies in Hz, not exp-form coefficients.
    const float sr = static_cast<float> (sampleRate_);

    auto applyToBranch = [&] (Branch& b)
    {
        const float L = loopLen (b);
        const float gLow = bandGain (L, bassMultiply_);
        const float gMid = bandGain (L, midMultiply_);
        const float gHi  = bandGain (L, trebleMultiply_);
        b.damping.setCoefficients (gLow, gMid, gHi,
                                   crossoverFreq_, highCrossoverFreq_, sr);
    };
    applyToBranch (leftBranch_);
    applyToBranch (rightBranch_);
}

void PlateEngine::applyDensityScale()
{
    // Map tank-diffusion knob [0..1] to a coefficient near the baseline,
    // capped well below 1.0 for AP stability. Range chosen so 0.5 →
    // baseline (matches "neutral"), 0 → very loose (low density),
    // 1 → very dense.
    const float baseline = densityDiffBaseline_;
    const float minCoeff = 0.30f;
    const float maxCoeff = 0.78f;
    const float t = lastTankDiffusionAmount_;
    if (t <= 0.5f)
        densityDiffCoeff_ = minCoeff + (baseline - minCoeff) * (t / 0.5f);
    else
        densityDiffCoeff_ = baseline + (maxCoeff - baseline) * ((t - 0.5f) / 0.5f);
}

// =====================================================================
// Audio thread — process()
// =====================================================================

void PlateEngine::process (const float* inputL, const float* inputR,
                           float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outputL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outputR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    // Drive-style soft-clip on the cross-feedback path. Threshold drops as
    // saturation rises, ceiling stays fixed — same envelope SixAPTank /
    // Dattorro use.
    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;

    auto softClip = [satThreshold, satCeiling] (float x) {
        if (x >  satThreshold) return satThreshold + (satCeiling - satThreshold)
                                        * std::tanh ((x - satThreshold)
                                                     / (satCeiling - satThreshold));
        if (x < -satThreshold) return -satThreshold - (satCeiling - satThreshold)
                                        * std::tanh ((-x - satThreshold)
                                                     / (satCeiling - satThreshold));
        return x;
    };

    // Hoist bass-extension active check out of the inner loop — gains
    // are constant across the block.
    const bool bassExtActive = (bassExtL_.outputGain != 0.0f)
                            || (bassExtR_.outputGain != 0.0f);

    const float densityG = densityDiffCoeff_;
    // Pre-compute per-stage staggered coefficients (clamped to ceiling).
    // Lifting this out of the inner loop saves 6 multiplies + clamps per
    // sample per branch.
    float stagged[kNumDensityAPs];
    for (int i = 0; i < kNumDensityAPs; ++i)
        stagged[i] = std::clamp (densityG * bloomStagger_[i],
                                 0.0f, bloomCeiling_);

    for (int n = 0; n < numSamples; ++n)
    {
        // -------- LEFT BRANCH --------
        // Internal pre-delay (20 ms) — non-recursive shift of dry input
        // before any further processing. Shapes Lex-style soft onset
        // (late-energy-dominant C80 / D50).
        const float lDryPredelay = internalPredelayL_.process (inputL[n]);
        // Apply the per-branch input high-shelf to the dry input ONLY
        // (not to the recirculating cross-feedback). With default gain
        // 0 dB the filter is a true bypass — see PlateEngine.h for why
        // this stays out of the loop.
        const float lDryShelved = leftBranch_.inputHighShelf.process (lDryPredelay);
        // Mix input + cross-feedback FROM right branch (figure-8 topology).
        float lIn = lDryShelved + leftBranch_.crossFeedState;

        // 2-AP input network — fast diffuse buildup.
        lIn = leftBranch_.in1.process (lIn, kInputAPGain);
        lIn = leftBranch_.in2.process (lIn, kInputAPGain);

        // Main delay 1 — write current, read delayed.
        leftBranch_.delay1.write (lIn);
        float lLoop = leftBranch_.delay1.readInterpolated (leftBranch_.delay1Samples);

        // 6-AP density cascade — each AP has its own jitter LFO and its
        // own per-stage coefficient (the Phase 3 "bloom stagger" — see
        // PlateEngine.h for rationale).
        for (int i = 0; i < kNumDensityAPs; ++i)
            lLoop = leftBranch_.densityAP[i].process (lLoop, stagged[i]);

        // 3-band damping (low-shelf + high-shelf around mid passband).
        lLoop = leftBranch_.damping.process (lLoop);

        // Polish AP2 (no jitter — final smear).
        lLoop = leftBranch_.ap2.process (lLoop, kAP2Gain);

        // Main delay 2 — write damped+polished signal, read delayed for
        // cross-feedback into the OTHER branch's next iteration.
        leftBranch_.delay2.write (lLoop);
        float lOut = leftBranch_.delay2.readInterpolated (leftBranch_.delay2Samples);

        // Saturation + safety clamp on the feedback tap (so a startup
        // transient or extreme size sweep can't blow the loop up before
        // the band-gain ceiling settles).
        const float lFeedback = std::clamp (
            softClip (lOut * kCrossFeedGain), -kSafetyClip, kSafetyClip);

        // -------- RIGHT BRANCH --------
        const float rDryPredelay = internalPredelayR_.process (inputR[n]);
        const float rDryShelved = rightBranch_.inputHighShelf.process (rDryPredelay);
        float rIn = rDryShelved + rightBranch_.crossFeedState;

        rIn = rightBranch_.in1.process (rIn, kInputAPGain);
        rIn = rightBranch_.in2.process (rIn, kInputAPGain);

        rightBranch_.delay1.write (rIn);
        float rLoop = rightBranch_.delay1.readInterpolated (rightBranch_.delay1Samples);

        for (int i = 0; i < kNumDensityAPs; ++i)
            rLoop = rightBranch_.densityAP[i].process (rLoop, stagged[i]);

        rLoop = rightBranch_.damping.process (rLoop);
        rLoop = rightBranch_.ap2.process (rLoop, kAP2Gain);

        rightBranch_.delay2.write (rLoop);
        float rOut = rightBranch_.delay2.readInterpolated (rightBranch_.delay2Samples);

        const float rFeedback = std::clamp (
            softClip (rOut * kCrossFeedGain), -kSafetyClip, kSafetyClip);

        // -------- Cross-feed exchange (figure-8) --------
        // Each branch's NEXT iteration sees the OTHER branch's current
        // delayed+saturated output. Exchanging at end-of-sample keeps the
        // figure-8 symmetric and free of one-sample bias.
        //
        // Polarity-flipped cross-feed on left branch (2026-05-18). Symmetric
        // cross-feed caused periodic stereo-correlation wander (image
        // cycled between mono and spread → audible "spinning"). Lex Rich
        // Plate measures stereo-correlation std 0.029 (rock-stable image);
        // symmetric DV measured 0.254. Inverting one branch breaks the
        // L↔R sync loop while preserving per-channel RT60 magnitude (only
        // the mono L+R sum reads slightly shorter due to partial late-tail
        // cancellation — per-channel decay rate is unchanged).
        leftBranch_ .crossFeedState = -rFeedback;
        rightBranch_.crossFeedState =  lFeedback;

        // -------- Output --------
        // Read each branch's main delay at slightly different points for
        // natural plate width — left output = left-branch-late, right
        // output = right-branch-late. The primary stereo decorrelation
        // comes from the L/R having distinct prime delays AND independent
        // jitter LFOs throughout the cascade.
        //
        // Optional parallel bass extension (right polarity-flipped for
        // L↔R decorrelation). Disabled-by-default for Rich Plate
        // (gains == 0); see PlateEngine.h::BassExtensionLoop. bassExtActive
        // hoisted from outputGain check before the loop.
        if (bassExtActive)
        {
            outputL[n] = lOut + bassExtL_.process (lDryPredelay);
            outputR[n] = rOut - bassExtR_.process (rDryPredelay);
        }
        else
        {
            outputL[n] = lOut;
            outputR[n] = rOut;
        }
    }
}

#include "SixAPTankEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void SixAPTankEngine::DelayLine::allocate (int maxSamples)
{
    int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 4, 16));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

void SixAPTankEngine::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float SixAPTankEngine::DelayLine::readInterpolated (float delaySamples) const
{
    int   intPart  = static_cast<int> (delaySamples);
    float fracPart = delaySamples - static_cast<float> (intPart);
    int   readPos  = (writePos - intPart - 1) & mask;
    return DspUtils::cubicHermite (buffer.data(), mask, readPos, 1.0f - fracPart);
}

void SixAPTankEngine::Allpass::allocate (int maxSamples)
{
    int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 4, 16));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;
    writePos = 0;
}

void SixAPTankEngine::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

SixAPTankEngine::SixAPTankEngine()
{
    // Distinct seeds so the L/R LFOs trace independent random paths from the
    // first sample; identical seeds would correlate the two tanks until they
    // drifted apart.
    leftTank_.lfo.prepare  (static_cast<float> (kBaseSampleRate), 0xC0FFEEu);
    rightTank_.lfo.prepare (static_cast<float> (kBaseSampleRate), 0xBADBEEFu);
}

void SixAPTankEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;

    // Pre-allocate scratch for the parallel-diffuser pass so the audio
    // thread never resizes a vector mid-process.
    diffusedL_.assign (static_cast<size_t> (std::max (maxBlockSize, 1)), 0.0f);
    diffusedR_.assign (static_cast<size_t> (std::max (maxBlockSize, 1)), 0.0f);

    auto setupTank = [sampleRate] (Tank& t,
                                    int ap1Base, int del1Base,
                                    int ap2Base, int del2Base,
                                    const int (&densBase)[kNumDensityAPs])
    {
        t.ap1BaseDelay   = ap1Base;
        t.delay1BaseDelay = del1Base;
        t.ap2BaseDelay   = ap2Base;
        t.delay2BaseDelay = del2Base;
        for (int i = 0; i < kNumDensityAPs; ++i)
            t.densityAPBase[i] = densBase[i];

        float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
        int reserve = static_cast<int> (static_cast<float> (kMaxBaseDelay) * rateRatio + 64.0f);

        t.ap1Buffer.allocate (reserve);
        t.delay1   .allocate (reserve);
        t.ap2      .allocate (reserve);
        t.delay2   .allocate (reserve);
        // Density-AP buffers must allow for the maximum sizeScale so the
        // delay can grow with the size knob; previously they were sized for
        // 1.0× only, which forced updateDelayLengths() to leave them
        // un-scaled (and produced a permanent ~44 ms diffusion path that
        // dominated the loop at low size, sounding like a discrete delay).
        constexpr float kMaxSizeScale = 1.5f;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            t.densityAP[i].allocate (static_cast<int> (
                static_cast<float> (densBase[i]) * rateRatio * kMaxSizeScale + 16.0f));
            // Lexicon "spin and wander". Each density AP gets a smoothed
            // random-walk modulator on its read position. Depth scales with
            // delay length (1.5 % of delaySamples), so the *relative*
            // modulation stays constant across all sizes. At small size
            // (LDH 0.75) the largest AP is ~666 samples → 10 sample jitter;
            // at large size (Cathedral/BR 0.95) it's ~814 → 12 sample jitter.
            // RandomWalkLFO smooths transitions via smoothstep so there is
            // NO white-noise hash on the read pointer — the offset glides.
            // updateJitterDepth() is called whenever delaySamples changes
            // (in updateDelayLengths) to keep the depth tracking the size.
            t.densityAP[i].jitterDepthFraction = 0.015f;  // 1.5% wander
        }

        t.damping.prepare (static_cast<float> (sampleRate));
        // Initial 3-band coefficients: gLow / gMid / gHigh + low/high crossover
        // coefficients (exp(-2π·fc/sr)). Will be overwritten by the first
        // updateDecayCoefficients() call.
        t.damping.setCoefficients (0.99f, 0.95f, 0.5f, 0.95f, 0.6f);
    };

    setupTank (leftTank_,  kLeftAP1Base,  kLeftDel1Base,  kLeftAP2Base,  kLeftDel2Base,  kLeftDensityAPBase);
    setupTank (rightTank_, kRightAP1Base, kRightDel1Base, kRightAP2Base, kRightDel2Base, kRightDensityAPBase);

    // Distinct LFO seeds per density AP (and per L/R tank) so the random
    // walks never align — every AP wanders independently. The LFO RATE is
    // not set here; updateJitterDepth() (called from updateDelayLengths
    // below) computes a per-AP rate based on the AP's current delay so the
    // LFO period is ~2× the ring period — fast enough to break short rings,
    // slow enough to stay below audibility for long rings.
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        const std::uint32_t lSeed = 0xBADBEEFu + static_cast<std::uint32_t> (i * 31337);
        const std::uint32_t rSeed = 0xC0FFEEu  + static_cast<std::uint32_t> (i * 27449);
        leftTank_ .densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), lSeed);
        rightTank_.densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), rSeed);
    }

    // Re-seed LFOs at the actual host sample rate so smoothing/period
    // calculations are correct.
    leftTank_.lfo.prepare  (static_cast<float> (sampleRate), 0xC0FFEEu);
    rightTank_.lfo.prepare (static_cast<float> (sampleRate), 0xBADBEEFu);


    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();
    setModDepth (lastModDepthRaw_);   // re-pushes depth to the new-rate-aware LFOs

    parallelDiffuser_.prepare (sampleRate);

    prepared_ = true;
}

void SixAPTankEngine::clearBuffers()
{
    auto clearTank = [] (Tank& t)
    {
        t.ap1Buffer.clear();
        t.delay1.clear();
        t.delay2.clear();
        t.ap2.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            t.densityAP[i].clear();
        t.damping.reset();
        t.crossFeedState = 0.0f;
    };
    clearTank (leftTank_);
    clearTank (rightTank_);
    parallelDiffuser_.clear();
}

void SixAPTankEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, seconds);
    if (prepared_)
        updateDecayCoefficients();
}

void SixAPTankEngine::setBassMultiply (float mult)
{
    bassMultiply_ = std::clamp (mult, 0.1f, 4.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void SixAPTankEngine::setMidMultiply (float mult)
{
    midMultiply_ = std::clamp (mult, 0.1f, 4.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void SixAPTankEngine::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::clamp (mult, 0.05f, 4.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void SixAPTankEngine::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::clamp (hz, 100.0f, 8000.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void SixAPTankEngine::setHighCrossoverFreq (float hz)
{
    // Clamp above the low crossover so the two filters don't invert.
    highCrossoverFreq_ = std::clamp (hz, std::max (crossoverFreq_ + 100.0f, 1000.0f), 12000.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void SixAPTankEngine::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
}

void SixAPTankEngine::setModDepth (float depth)
{
    lastModDepthRaw_ = std::clamp (depth, 0.0f, 1.0f);
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    // Normalised across all four engines: depth × 16 samples peak at 44.1 k.
    // (Was 8 — half the others — which made the same knob position feel
    // weaker on this engine than on Dattorro/QuadTank/FDN.)
    modDepthSamples_ = lastModDepthRaw_ * 16.0f * rateRatio;
    leftTank_.lfo.setDepth  (modDepthSamples_);
    rightTank_.lfo.setDepth (modDepthSamples_);
}

void SixAPTankEngine::setModRate (float hz)
{
    modRateHz_ = std::clamp (hz, 0.05f, 12.0f);
    if (prepared_)
        updateLFORates();
}

void SixAPTankEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void SixAPTankEngine::setTankDiffusion (float amount)
{
    // knob 0 → 0.5×, knob 0.5 → 1.0×, knob 0.85 (typical preset) → 1.095×,
    // knob 1.0 → 1.2×. Capped at bloomCoeffCeiling_ absolute for AP stability.
    float a = std::clamp (amount, 0.0f, 1.0f);
    lastTankDiffusionAmount_ = a;
    float scale = 0.5f + a * 0.7f;
    densityDiffCoeff_ = std::clamp (densityDiffBaseline_ * scale, 0.0f, bloomCoeffCeiling_);
}

void SixAPTankEngine::setDensityBaseline (float v)
{
    densityDiffBaseline_ = std::clamp (v, 0.30f, 0.90f);
    // Re-apply the diffusion-knob scaling around the new baseline so the
    // user-facing knob continues to behave consistently.
    setTankDiffusion (lastTankDiffusionAmount_);
}

void SixAPTankEngine::setBloomCeiling (float v)
{
    bloomCoeffCeiling_ = std::clamp (v, 0.70f, 0.95f);
    // Re-apply the diffusion-knob scaling so densityDiffCoeff_ respects the
    // new ceiling on the working coefficient as well.
    setTankDiffusion (lastTankDiffusionAmount_);
}

void SixAPTankEngine::setBloomStagger (const float values[6])
{
    for (int i = 0; i < kNumDensityAPs; ++i)
        bloomStagger_[i] = std::clamp (values[i], 0.0f, 2.0f);
}

void SixAPTankEngine::setEarlyMix (float v)
{
    earlyMix_ = std::clamp (v, 0.0f, 1.5f);
}

void SixAPTankEngine::setOutputTrim (float v)
{
    outputTrim_ = std::clamp (v, 0.5f, 2.0f);
}

void SixAPTankEngine::setFreeze (bool frozen)
{
    frozen_ = frozen;
    if (prepared_)
        updateDecayCoefficients();
}

void SixAPTankEngine::updateDelayLengths()
{
    // Size knob spans 0.2× to 1.5× of the baked tank delays. The minimum
    // 0.2× was 0.5× before; at the bottom of the range the loop is now
    // ~37 ms instead of ~114 ms, which lets the user dial in a genuine
    // tight room rather than a discrete ~10 Hz delay.
    float sizeScale = 0.2f + sizeParam_ * 1.3f;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);

    auto setTankDelays = [&] (Tank& t)
    {
        t.ap1DelaySamples = static_cast<float> (t.ap1BaseDelay) * sizeScale * rateRatio;
        t.delay1Samples   = static_cast<float> (t.delay1BaseDelay) * sizeScale * rateRatio;
        t.delay2Samples   = static_cast<float> (t.delay2BaseDelay) * sizeScale * rateRatio;

        t.ap2.delaySamples = std::min (
            static_cast<int> (static_cast<float> (t.ap2BaseDelay) * sizeScale * rateRatio),
            t.ap2.mask);

        // Density APs scale with size too (matching DattorroTank). Without
        // this, the 6-stage density cascade contributes a fixed ~44 ms path
        // through the loop at every size setting — at low size that fixed
        // delay dominates and the loop reads as a discrete echo rather than
        // a reverb wash. After updating delaySamples we MUST refresh the
        // jitter LFO (depth + rate) so the spin-and-wander modulation tracks
        // the new delay length — otherwise the largest APs at hall sizes
        // ring audibly because their relative jitter shrinks AND a slow
        // LFO can't break their short ring period.
        const float sr = static_cast<float> (sampleRate_);
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            t.densityAP[i].delaySamples = std::min (
                static_cast<int> (static_cast<float> (t.densityAPBase[i])
                                  * sizeScale * rateRatio),
                t.densityAP[i].mask);
            t.densityAP[i].updateJitterDepth (sr);
        }
    };

    setTankDelays (leftTank_);
    setTankDelays (rightTank_);
}

void SixAPTankEngine::updateDecayCoefficients()
{
    if (frozen_)
    {
        // 3-band: gLow / gMid / gHigh, lowXover / highXover coeffs.
        // Frozen = unity at every band, no shelving.
        leftTank_.damping.setCoefficients (1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
        rightTank_.damping.setCoefficients (1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
        return;
    }

    // Per-tank Sabine: g_loop = exp(-3 ln10 · loopLen / (sr · RT60)).
    //
    // Effective loop length = direct delays (ap1/delay1/ap2/delay2) PLUS the
    // sum of density-AP delays PLUS an empirical compensation for energy
    // stored *inside* each density AP. The compensation factor is NOT
    // constant — it depends on size:
    //   • At small size, the direct loop is short relative to the density
    //     cascade. The cascade's energy storage dominates the actual decay,
    //     so a large factor is needed to make the formula match.
    //   • At large size, the direct loop is long. Density storage matters
    //     proportionally less. Using the small-size factor over-compensates,
    //     producing too-fast decay (verified: factor 2.10 at size=0.95 gave
    //     5.84 s actual vs 6.5 s target = 10 % short).
    //
    // Empirical linear interpolation: factor = 2.10 at size=0.5, 1.55 at
    // size=1.0. Equivalent: 2.65 - 1.10·size.
    //   Calibrated against render-tool RT60 measurements at size=0.75
    //   (Lush Dark Hall) and size=0.95 (Cathedral / Blade Runner 224).
    //   We tried 2.45 to compensate for a +5-15 % systematic over-shoot,
    //   but that was measured BEFORE the input-diffuser jitter (which
    //   adds wider smear and itself extends measured RT60 by a few %).
    //   With both fixes active, 2.65 lands closer to UI target.
    const float storageFactor = 2.65f - 1.10f * sizeParam_;
    auto loopLen = [storageFactor] (const Tank& t) {
        float densityLen = 0.0f;
        for (int i = 0; i < kNumDensityAPs; ++i)
            densityLen += static_cast<float> (t.densityAP[i].delaySamples);
        const float densityEffective = densityLen * storageFactor;
        return static_cast<float> (t.ap1DelaySamples + t.delay1Samples
                                   + t.ap2.delaySamples + t.delay2Samples)
             + densityEffective;
    };

    float numerator = -3.0f * 2.302585092994046f / static_cast<float> (sampleRate_);

    // Hard ceiling on per-band loop gain — guarantees the cross-coupled tank
    // pair is provably stable even at decay = 30 s + bass-mult = 2.5. Ceiling
    // 0.98 corresponds to a worst-case RT60 of ~24 s at our typical loop
    // lengths, which is musically indistinguishable from longer settings but
    // mathematically prevents self-oscillation.
    constexpr float kMaxBandGain = 0.98f;
    auto bandGain = [numerator, kMaxBandGain, this] (float loopSamples, float multiplier) {
        float effRt60 = std::max (decayTime_ * multiplier, 0.05f);
        return std::clamp (std::exp (numerator * loopSamples / effRt60), 0.0f, kMaxBandGain);
    };

    auto lowXoverCoeff  = std::exp (-kTwoPi * crossoverFreq_     / static_cast<float> (sampleRate_));
    auto highXoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / static_cast<float> (sampleRate_));

    auto applyToTank = [&] (Tank& t)
    {
        float L = loopLen (t);
        float gLow = bandGain (L, bassMultiply_);
        float gMid = bandGain (L, midMultiply_);
        float gHi  = bandGain (L, trebleMultiply_);
        // 3-band shelving damping (matches Dattorro / QuadTank / FDN topology).
        // Pass ABSOLUTE per-band gains; the ThreeBandDamping internally derives
        // shelf gains relative to gMid (broadband multiplier).
        t.damping.setCoefficients (gLow, gMid, gHi, lowXoverCoeff, highXoverCoeff);
    };
    applyToTank (leftTank_);
    applyToTank (rightTank_);
}

void SixAPTankEngine::updateLFORates()
{
    // Slight L/R detune (1.13× on the right tank) so the two random walks
    // never settle into a beating pattern even if their seeds happen to align
    // briefly.
    leftTank_.lfo.setRate  (modRateHz_);
    rightTank_.lfo.setRate (modRateHz_ * 1.13f);
}

void SixAPTankEngine::process (const float* inputL, const float* inputR,
                                 float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outputL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outputR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    // Drive-style saturation (see DattorroTank for rationale).
    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;

    // ---- BLOOM stagger (now per-instance via bloomStagger_ / bloomCoeffCeiling_) ----
    // Earlier stages run gentler (more transient spread), later stages bite
    // harder (more late-tail ring). Each effective coefficient is hard-
    // clamped to bloomCoeffCeiling_ so even the largest late-stage
    // multiplier × densityDiffCoeff_ cannot push an AP toward self-
    // oscillation. Defaults preserve the historical {0.7..1.2}/0.85 values;
    // Black Hole opts in to a steeper stagger + higher ceiling for VS
    // BlackHole-character late-tail density.

    // ---- Step 0: shatter the input through the parallel diffuser ----
    // The 6-AP-specific input shatter replaces the global series diffuser
    // (bypassed for this engine in DuskVerbEngine::process). After this
    // pass, inL/inR samples carry ~40 dense reflections per channel from
    // the parallel-AP sum — enough density that the tank's 6-AP cascade
    // never sees a sharp transient and the listener never hears a discrete
    // echo cluster. We diffuse into scratch members (the input pointers
    // are const) so we don't mutate the caller's buffer; scratch is
    // pre-sized in prepare() so the audio thread does not allocate.
    const int diffuseN = std::min (numSamples, static_cast<int> (diffusedL_.size()));
    std::memcpy (diffusedL_.data(), inputL, sizeof (float) * static_cast<size_t> (diffuseN));
    std::memcpy (diffusedR_.data(), inputR, sizeof (float) * static_cast<size_t> (diffuseN));
    parallelDiffuser_.process (diffusedL_.data(), diffusedR_.data(), diffuseN);

    const float* in_L = diffusedL_.data();
    const float* in_R = diffusedR_.data();

    for (int n = 0; n < numSamples; ++n)
    {
        float inL = in_L[n];
        float inR = in_R[n];

        // ---- LEFT TANK ----
        // Random-walk LFO: smoothed-noise modulation of the AP1 read head.
        // Replaces the previous std::sin(phase) LFO so the modulation never
        // beats periodically against the tank's modal frequencies — gives
        // the "expensive shimmer" of high-end random-hall hardware.
        const float lfoMod = leftTank_.lfo.next();
        float ap1Read = leftTank_.ap1Buffer.readInterpolated (
            std::clamp (leftTank_.ap1DelaySamples + lfoMod, 1.0f,
                        static_cast<float> (leftTank_.ap1Buffer.mask)));
        float ap1In = inL + leftTank_.crossFeedState - decayDiff1_ * ap1Read;
        leftTank_.ap1Buffer.write (ap1In);
        float postAP1 = ap1Read + decayDiff1_ * ap1In;

        leftTank_.delay1.write (postAP1);
        float del1 = leftTank_.delay1.read (static_cast<int> (leftTank_.delay1Samples));

        float density = del1;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            const float coeff = std::min (densityDiffCoeff_ * bloomStagger_[i],
                                          bloomCoeffCeiling_);
            density = leftTank_.densityAP[i].process (density, coeff);
        }

        float damped = leftTank_.damping.process (density);
        float ap2Out = leftTank_.ap2.process (damped, decayDiff2_);
        leftTank_.delay2.write (ap2Out);
        float leftLoop = leftTank_.delay2.read (static_cast<int> (leftTank_.delay2Samples));

        // ---- RIGHT TANK ----
        const float lfoModR = rightTank_.lfo.next();
        float ap1ReadR = rightTank_.ap1Buffer.readInterpolated (
            std::clamp (rightTank_.ap1DelaySamples + lfoModR, 1.0f,
                        static_cast<float> (rightTank_.ap1Buffer.mask)));
        float ap1InR = inR + rightTank_.crossFeedState - decayDiff1_ * ap1ReadR;
        rightTank_.ap1Buffer.write (ap1InR);
        float postAP1R = ap1ReadR + decayDiff1_ * ap1InR;

        rightTank_.delay1.write (postAP1R);
        float del1R = rightTank_.delay1.read (static_cast<int> (rightTank_.delay1Samples));

        float densityR = del1R;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            const float coeff = std::min (densityDiffCoeff_ * bloomStagger_[i],
                                          bloomCoeffCeiling_);
            densityR = rightTank_.densityAP[i].process (densityR, coeff);
        }

        float dampedR = rightTank_.damping.process (densityR);
        float ap2OutR = rightTank_.ap2.process (dampedR, decayDiff2_);
        rightTank_.delay2.write (ap2OutR);
        float rightLoop = rightTank_.delay2.read (static_cast<int> (rightTank_.delay2Samples));

        // Cross-feed: each tank's loop output feeds the OTHER tank's input.
        // Two protections layered on the write:
        //   • softClip — analog-style soft saturation that engages only above
        //     ±1.0, adds harmonic warmth on transients without altering RT60
        //     of quiet tail samples;
        //   • alternating-sign denormal bias to prevent the loop from
        //     collapsing into denormalised floats (CPU-spike protection).
        //
        // MÖBIUS TWIST: the right-tank cross-feed is sign-inverted on the
        // write so the two tanks anti-correlate forever. Without this, an
        // 18-second tail eventually drifts toward mono as the left-into-
        // right and right-into-left feedback paths converge. Negating one
        // path turns the loop into a true 180° figure-8 — every recirc
        // flips polarity, mathematically guaranteed not to converge.
        const float l = DspUtils::softClip (rightLoop, satThreshold, satCeiling);
        const float r = DspUtils::softClip (leftLoop,  satThreshold, satCeiling);
        leftTank_.crossFeedState  = std::clamp ( l, -kSafetyClip, kSafetyClip)
                                  + DspUtils::kDenormalPrevention;
        rightTank_.crossFeedState = std::clamp (-r, -kSafetyClip, kSafetyClip)
                                  - DspUtils::kDenormalPrevention;

        // STEREO BIND: each output channel mixes its own tank's post-cascade
        // `damped` signal (primary) with a phase-inverted ½-amplitude slice
        // of the OPPOSITE tank's loop output. Without this, hard-panned
        // input material produces lopsided reverb (only one tank gets fed,
        // only one channel rings). The phase-inverted cross-mix guarantees
        // both output channels carry signal regardless of input panning,
        // and the inversion preserves the MöbiusTwist anti-correlation —
        // the cross terms partially cancel in mono so a mono sum stays
        // close to the dry-mid character rather than doubling.
        //
        // 2.0 → 1.3 trim because the cross-mix adds ~+4 dB of summed
        // energy on hard-mono input and we need to stay under unity ceiling.
        //
        // EARLY-FILL: mix the ParallelDiffuser's output directly into the
        // audio output. The cascade output (damped/dampedR) is mathematically
        // silent until the input has traversed delay1 (~90-140 ms depending
        // on size knob), so without this mix the listener perceives a long
        // dead period before any wet appears even at predelay = 0. The
        // ParallelDiffuser was producing dense AP-delay impulses (11, 16,
        // 23, 32, 45, 60 ms) the whole time but feeding only the tank input;
        // routing some of it to the output too gives instant early reflections
        // that bridge T+0 to T+~90 ms before the cascade tail kicks in.
        // EarlyMix and OutputTrim are now per-instance (earlyMix_ / outputTrim_)
        // so individual presets can opt in to brighter onsets / different
        // trim balance without affecting other presets sharing this engine.
        // Defaults preserve the historical 0.5 / 1.3 values.
        const float earlyL = diffusedL_[static_cast<size_t> (n)];
        const float earlyR = diffusedR_[static_cast<size_t> (n)];
        outputL[n] = (damped  + rightLoop * 0.5f + earlyL * earlyMix_) * outputTrim_;
        outputR[n] = (dampedR - leftLoop  * 0.5f + earlyR * earlyMix_) * outputTrim_;
    }
}

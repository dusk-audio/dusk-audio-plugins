#include "DattorroTank.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// -----------------------------------------------------------------------
// DelayLine helpers

void DattorroTank::DelayLine::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void DattorroTank::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float DattorroTank::DelayLine::readInterpolated (float delaySamples) const
{
    float readPos = static_cast<float> (writePos) - delaySamples;
    int intIdx = static_cast<int> (std::floor (readPos));
    float frac = readPos - static_cast<float> (intIdx);
    return DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
}

// -----------------------------------------------------------------------
// Allpass helpers

void DattorroTank::Allpass::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void DattorroTank::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// -----------------------------------------------------------------------
// Constructor

DattorroTank::DattorroTank()
{
    leftTank_.ap1BaseDelay  = kLeftAP1Base;
    leftTank_.delay1BaseDelay = kLeftDel1Base;
    leftTank_.ap2BaseDelay  = kLeftAP2Base;
    leftTank_.delay2BaseDelay = kLeftDel2Base;

    rightTank_.ap1BaseDelay  = kRightAP1Base;
    rightTank_.delay1BaseDelay = kRightDel1Base;
    rightTank_.ap2BaseDelay  = kRightAP2Base;
    rightTank_.delay2BaseDelay = kRightDel2Base;

    // Density cascade base delays
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        leftTank_.densityAPBase[i]  = kLeftDensityAPBase[i];
        rightTank_.densityAPBase[i] = kRightDensityAPBase[i];
    }
}

// -----------------------------------------------------------------------

void DattorroTank::setHallScale (bool enable)
{
    if (enable)
    {
        leftTank_.ap1BaseDelay     = kLeftAP1BaseHall;
        leftTank_.delay1BaseDelay  = kLeftDel1BaseHall;
        leftTank_.ap2BaseDelay     = kLeftAP2BaseHall;
        leftTank_.delay2BaseDelay  = kLeftDel2BaseHall;
        rightTank_.ap1BaseDelay    = kRightAP1BaseHall;
        rightTank_.delay1BaseDelay = kRightDel1BaseHall;
        rightTank_.ap2BaseDelay    = kRightAP2BaseHall;
        rightTank_.delay2BaseDelay = kRightDel2BaseHall;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            leftTank_.densityAPBase[i]  = kLeftDensityAPBaseHall[i];
            rightTank_.densityAPBase[i] = kRightDensityAPBaseHall[i];
        }
    }
    else
    {
        leftTank_.ap1BaseDelay     = kLeftAP1Base;
        leftTank_.delay1BaseDelay  = kLeftDel1Base;
        leftTank_.ap2BaseDelay     = kLeftAP2Base;
        leftTank_.delay2BaseDelay  = kLeftDel2Base;
        rightTank_.ap1BaseDelay    = kRightAP1Base;
        rightTank_.delay1BaseDelay = kRightDel1Base;
        rightTank_.ap2BaseDelay    = kRightAP2Base;
        rightTank_.delay2BaseDelay = kRightDel2Base;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            leftTank_.densityAPBase[i]  = kLeftDensityAPBase[i];
            rightTank_.densityAPBase[i] = kRightDensityAPBase[i];
        }
    }
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

// -----------------------------------------------------------------------
// #87 boing fix — load the HALL density bases into the 12-AP cascade while
// leaving the 4 MAIN lines at room scale. Adds close-spaced coprime modes that
// fill the sparse low-mid gap (the comb-coincidence resonance). NOT setHallScale
// (that would lengthen the room). gBase re-derives off the new loopLength so the
// per-band T60 / level are preserved. Default off → byte-identical.
void DattorroTank::setDensityRoomFill (bool enable)
{
    if (enable == densityRoomFill_) return;
    densityRoomFill_ = enable;
    const int* lBases = enable ? kLeftDensityAPBaseHall  : kLeftDensityAPBase;
    const int* rBases = enable ? kRightDensityAPBaseHall : kRightDensityAPBase;
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        leftTank_.densityAPBase[i]  = lBases[i];
        rightTank_.densityAPBase[i] = rBases[i];
    }
    if (prepared_) { updateDelayLengths(); updateDecayCoefficients(); }
}

// #87 boing fix — per-line incommensurate detune of the 4 main delay lines.
// Identity {1,1,1,1} (×1.0f, exact in IEEE-754) = bit-null. Capped ±10% — well
// inside the prepareTank allocation headroom (sizeRangeAllocatedMax_≥1.5 ×
// kMaxDelayScale 4.0).
void DattorroTank::setMainLineDetune (float lDel1, float lDel2, float rDel1, float rDel2)
{
    mainDetune_[0] = std::clamp (lDel1, 0.90f, 1.10f);
    mainDetune_[1] = std::clamp (lDel2, 0.90f, 1.10f);
    mainDetune_[2] = std::clamp (rDel1, 0.90f, 1.10f);
    mainDetune_[3] = std::clamp (rDel2, 0.90f, 1.10f);
    if (prepared_) { updateDelayLengths(); updateDecayCoefficients(); }
}

// -----------------------------------------------------------------------

void DattorroTank::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);

    // Modulation headroom beyond the max scaled delay (scale with sample rate)
    const int maxModExcursion = static_cast<int> (std::ceil (32.0 * sampleRate / kBaseSampleRate));

    // Track allocation ceiling for runtime setSizeRange() bounds checking
    sizeRangeAllocatedMax_ = std::max (sizeRangeAllocatedMax_, std::max (sizeRangeMax_, 1.5f));

    // Allocate all buffers
    auto prepareTank = [&] (Tank& tank)
    {
        constexpr float kMaxDelayScale = 4.0f;  // Must match setDelayScale() clamp
        float maxScale = sizeRangeAllocatedMax_ * kMaxDelayScale;
        int ap1Max = static_cast<int> (std::ceil (tank.ap1BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int del1Max = static_cast<int> (std::ceil (tank.delay1BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int ap2Max = static_cast<int> (std::ceil (tank.ap2BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int del2Max = static_cast<int> (std::ceil (tank.delay2BaseDelay * rateRatio * maxScale)) + maxModExcursion;

        tank.ap1Buffer.allocate (ap1Max);
        tank.delay1.allocate (del1Max + maxModExcursion);  // Extra headroom for noise jitter
        tank.ap2.allocate (ap2Max);
        tank.delay2.allocate (del2Max + maxModExcursion);

        // Density cascade allpasses. updateDelayLengths() scales these by
        // delayScale_ too, so the max allocation must include it to avoid
        // buffer underruns when delayScale_ > 1.
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            // #87: prepare() runs the ctor's ROOM bases, but setDensityRoomFill(true)
            // may later load the larger HALL bases (1303 vs 281). Size every buffer
            // for the MAX of all four base sets so whichever is selected fits — an
            // OOB interpolated read into unwritten buffer = garbage/NaN tail.
            // Over-allocation is harmless + bit-null (write/read indices unchanged).
            const int baseMaxSamples = std::max (
                std::max (kLeftDensityAPBase[i], kRightDensityAPBase[i]),
                std::max (kLeftDensityAPBaseHall[i], kRightDensityAPBaseHall[i]));
            const float baseMax =
                baseMaxSamples * rateRatio * sizeRangeAllocatedMax_ * kMaxDelayScale;
            // Jitter depth is 0.02 × delaySamples per the assignment below.
            // Add explicit headroom for the read offset (jitter) plus 4
            // samples for cubic Hermite (reads intIdx-1 .. intIdx+2) plus
            // a safety margin. Without this, a worst-case jitter sample
            // could read into not-yet-written buffer positions.
            const int extraSamples = static_cast<int> (std::ceil (baseMax * kMaxDensityJitterFraction));
            const int dapMax = static_cast<int> (std::ceil (baseMax))
                             + extraSamples + 4;
            tank.densityAP[i].allocate (dapMax);
            // Sub-audio (1.5 Hz) density-AP jitter — the implementation the
            // #87 fix's TODO comment anticipated. The audio-band variant
            // (5-200 Hz at 1.5 % depth) generated FM sidebands heard as
            // vibrato/bell artifacts; the slow random-walk variant gives the
            // same comb-tooth phase-lock breakage without any sidebands, so
            // plate presets stop ringing at their 28-30 ms tank loop period
            // (Vocal Plate, Vintage Vocal Plate, Fat Pop Plate were all
            // audibly ringing without this). 2 % depth = ±3-5 samples
            // wander on each density-AP delay line — enough to spread
            // 28-30 ms comb teeth on the worst-case plates while keeping
            // the residual pitch wobble inaudible on sustained content
            // (3 % was perceptible as chorus on Rich Plate).
            tank.densityAP[i].jitterDepthFraction = densityJitterFraction_;
        }

        tank.damping.prepare (static_cast<float> (sampleRate));
        tank.damping.reset();
        tank.octaveDamping.prepare (static_cast<float> (sampleRate));
        tank.octaveDamping.reset();
        tank.crossFeedState = 0.0f;
    };

    prepareTank (leftTank_);
    prepareTank (rightTank_);

    // Static output tonal-correction GEQ (stereo). Coeffs re-designed below.
    tonalCorrL_.prepare (static_cast<float> (sampleRate));
    tonalCorrR_.prepare (static_cast<float> (sampleRate));

    // Input diffusion cascade (mono, fixed length — scaled by sample rate only,
    // NOT by size/delayScale, so the initial smear time is constant). Static
    // allpasses: no jitter LFO, no size-dependent re-derivation in
    // updateDelayLengths(). delaySamples is set once here.
    for (int k = 0; k < kNumInputDiffusers; ++k)
    {
        const int d = std::max (1, static_cast<int> (
            std::ceil (kInputDiffuserBase[k] * rateRatio)));
        inputDiffuser_[k].allocate (d + 4);   // +4 safety; integer read, no interp
        inputDiffuser_[k].delaySamples       = d;
        inputDiffuser_[k].jitterDepthFraction = 0.0f;   // STATIC — initial diffusion is still
    }

    // Initialize LFOs with different seeds per tank and per delay tap so
    // every modulator wanders along an independent path. The two delay-tap
    // LFOs are derived from the AP1 seed via XOR-mixed constants — keeps
    // them deterministic and decorrelated from the AP1 LFO.
    leftTank_.lfo.prepare        (static_cast<float> (sampleRate), 0x12345678u);
    leftTank_.delay1Lfo.prepare  (static_cast<float> (sampleRate), 0x12345678u ^ 0xA5A5A5A5u);
    leftTank_.delay2Lfo.prepare  (static_cast<float> (sampleRate), 0x12345678u ^ 0x5A5A5A5Au);
    rightTank_.lfo.prepare       (static_cast<float> (sampleRate), 0x87654321u);
    rightTank_.delay1Lfo.prepare (static_cast<float> (sampleRate), 0x87654321u ^ 0xA5A5A5A5u);
    rightTank_.delay2Lfo.prepare (static_cast<float> (sampleRate), 0x87654321u ^ 0x5A5A5A5Au);

    // Mode-smear LFOs — distinct seeds again so the deep+slow wander is decorrelated
    // from the mode-breaking delay LFOs AND across taps/tanks. Depth/rate applied by
    // setModeSmear() below (re-applied here at the new sample rate).
    leftTank_ .smearLfo1.prepare (static_cast<float> (sampleRate), 0x12345678u ^ 0xDEADBEEFu);
    leftTank_ .smearLfo2.prepare (static_cast<float> (sampleRate), 0x12345678u ^ 0xBEEFDEADu);
    rightTank_.smearLfo1.prepare (static_cast<float> (sampleRate), 0x87654321u ^ 0xDEADBEEFu);
    rightTank_.smearLfo2.prepare (static_cast<float> (sampleRate), 0x87654321u ^ 0xBEEFDEADu);

    // Per-density-AP jitter LFOs — distinct seeds per stage and per L/R tank
    // so every AP wanders independently. Rate is set later in updateJitterDepth
    // (called from updateDelayLengths) based on actual delaySamples.
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        const std::uint32_t lSeed = 0xBADBEEFu + static_cast<std::uint32_t> (i * 31337);
        const std::uint32_t rSeed = 0xC0FFEEu  + static_cast<std::uint32_t> (i * 27449);
        leftTank_ .densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), lSeed);
        rightTank_.densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), rSeed);
    }

    prepared_ = true;

    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();

    // Re-apply mod depth scaled for the new sample rate. setModDepth() also
    // updates delay1Lfo / delay2Lfo depths (no separate noise-jitter setter
    // any more — the white-noise modulation has been replaced by the
    // smoothstep-interpolated delay LFOs).
    setModDepth (lastModDepthRaw_);
    setModeSmear (smearDepthSamples_, smearRateHz_);   // re-apply smear depth/rate at the new sample rate

    // Re-apply structural HF damping for new sample rate
    if (lastStructHFHz_ > 0.0f)
        setStructuralHFDamping (lastStructHFHz_);

    // Re-realize the in-loop mode notch at the new sample rate (also clears state)
    if (notchActive_)
        setModeNotch (mnHz_, mnCutDb_, mnQ_);

    // Re-derive the HF-sustain shelf at the new sample rate.
    setHFSustain (hfSusDb_, hfSusHz_);

    // Sustain-band limiter: re-derive coeffs at the new sample rate + clear state.
    susLimMidCfg_.updateCoeffs (sampleRate);
    susLimLowCfg_.updateCoeffs (sampleRate);
    susLimMidDet_.clear();  susLimLowDet_.clear();
    susLimMidSplitL_.clear(); susLimMidSplitR_.clear();
    susLimLowSplitL_.clear(); susLimLowSplitR_.clear();
    susLimKey_.prepare (sampleRate);
    susLimMidRed_ = susLimLowRed_ = susLimMidAcc_ = susLimLowAcc_ = 0.0f;

    // Recompute soft-onset coefficient for the new sample rate
    if (softOnsetMs_ > 0.0f)
        setSoftOnsetMs (softOnsetMs_);

    // Recompute bloom coeffs for the new sample rate + reset its envelope.
    setBloomAttackMs (bloomAttackMs_);
    bloomEnv_     = bloomActive_ ? 0.0f : 1.0f;
    bloomRampPos_ = bloomActive_ ? 0.0f : 1.0f;
    bloomQuietSamples_ = bloomRearmSamples_;  // armed: first onset triggers the swell

    // Re-design the static tonal-correction GEQ for the new sample rate.
    updateTonalCorr();

    // Clear stateful trackers. Without this, a host re-prepare would start
    // with empty delay buffers but retain the previous run's tracker state
    // (structural HF filter, AP1 mod source, soft-onset ramp, limiter
    // envelope). Soft-onset and limiter resets mirror clearBuffers().
    structHFStateL_ = 0.0f;
    structHFStateR_ = 0.0f;
    leftTank_ .savedAP1Mod = 0.0f;
    rightTank_.savedAP1Mod = 0.0f;
    softOnsetEnvL_ = (softOnsetMs_ > 0.0f) ? 0.0f : 1.0f;
    limiterEnv_    = 0.0f;
}

// -----------------------------------------------------------------------

void DattorroTank::process (const float* inputL, const float* inputR,
                            float* outputL, float* outputR, int numSamples,
                            const float* sourceSide)
{
    if (! prepared_)
        return;

    // Drive-style saturation: amount=0 → clean (threshold 1.0, no audible
    // effect on quiet tail); amount=1 → threshold 0.4 (aggressive, every loop
    // pass touches the soft knee). Ceiling stays at 2.0 so the softClip's
    // upper asymptote remains stable across drive settings.
    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Mono sum of stereo input (Dattorro tank is internally mono,
        // stereo comes from decorrelated output tapping)
        float input = (inputL[i] + inputR[i]) * 0.5f;
        const float side = (stereoInputActive_ && ! frozen_)
                         ? (sourceSide != nullptr ? sourceSide[i]
                                                  : (inputL[i] - inputR[i]) * 0.5f)
                         : 0.0f;

        if (frozen_)
            input = 0.0f;

        // Slow-attack bloom: follow input onset (raw, pre-diffusion), drive a
        // one-pole swell gain that opens over bloomAttackMs_ then holds (the
        // input-peak follower's slow release keeps the target latched through
        // the tail). Applied to the wet output below. Bypassed when inactive.
        if (bloomActive_ && ! frozen_)
        {
            const float a = std::abs (input);
            if (a < 1.0e-4f)
            {
                if (bloomQuietSamples_ < (1 << 30)) ++bloomQuietSamples_;
            }
            else
            {
                // Onset after enough silence → (re)trigger the swell ramp.
                if (bloomQuietSamples_ >= bloomRearmSamples_) bloomRampPos_ = 0.0f;
                bloomQuietSamples_ = 0;
            }
            if (bloomRampPos_ < 1.0f)
                bloomRampPos_ = std::min (1.0f, bloomRampPos_ + bloomRampInc_);
            bloomEnv_ = (bloomRampPos_ >= 1.0f) ? 1.0f
                                                : std::pow (bloomRampPos_, bloomExp_);
        }

        // ------------------------------------------------------------------
        // Input diffusion: smear the impulse into a dense burst BEFORE it
        // enters the tank, so the first ~60 ms of the response is already a
        // dense diffuse field (low kurtosis) instead of a sparse spray of
        // discrete tap arrivals. Feed-forward static allpass chain → no RT60
        // change. Bypassed when inactive → bit-identical legacy.
        if (inputDiffusionActive_ && ! frozen_)
        {
            for (int k = 0; k < kNumInputDiffusers; ++k)
                input = inputDiffuser_[k].process (
                    input, std::clamp (kInputDiffuserCoeff[k] * inputDiffCoeffScale_, 0.0f, 0.85f));
        }

        // --- Sustain-band limiter: advance the SHARED (both-tank) detectors once per
        // sample from the PREVIOUS sample's summed |band| (one-sample latency is
        // negligible at the detector taus). Input-keyed release protects the T60 fit.
        // Inactive → untouched → bit-null.
        if ((susLimMidCfg_.active || susLimLowCfg_.active) && ! frozen_)
        {
            const bool present = susLimKey_.advance (std::fabs (input));
            if (susLimMidCfg_.active)
            {
                susLimMidRed_ = susLimMidDet_.advance (0.5f * susLimMidAcc_, susLimMidCfg_, present);
                susLimMidAcc_ = 0.0f;
            }
            if (susLimLowCfg_.active)
            {
                susLimLowRed_ = susLimLowDet_.advance (0.5f * susLimLowAcc_, susLimLowCfg_, present);
                susLimLowAcc_ = 0.0f;
            }
        }

        // ------------------------------------------------------------------
        // Process both tanks. Each receives the other's cross-feed state.
        // Order: left first, then right. The one-sample delay in cross-feed
        // is intentional (Dattorro's figure-8 topology).

        auto processTank = [&] (Tank& tank, float otherCrossFeed, float sideSign)
        {
            // Tank input: new audio + cross-fed signal from the other tank
            float tankIn = input + otherCrossFeed;
            if (stereoInputActive_)
                tankIn += sideSign * stereoInputAmount_ * side;

            // --- Modulated allpass (decay diffusion 1) ---
            // Random-walk LFO replaces the previous std::sin(phase) + drift
            // chain. The smoothed-noise modulator never settles into a
            // perceptible periodic warble — gives the "expensive shimmer" of
            // high-end random-hall hardware. When frozen, hold the last
            // modulation offset so the read head doesn't snap (avoids click).
            float currentMod = frozen_ ? tank.savedAP1Mod : tank.lfo.next();
            if (! frozen_)
                tank.savedAP1Mod = currentMod;
            float ap1ReadDelay = tank.ap1DelaySamples + currentMod * modReduction_;
            ap1ReadDelay = std::max (ap1ReadDelay, 1.0f);  // Never read ahead of write

            float ap1Delayed = tank.ap1Buffer.readInterpolated (ap1ReadDelay);
            float coeff1 = frozen_ ? 0.0f : decayDiff1_;
            float ap1In = tankIn + coeff1 * ap1Delayed;
            tank.ap1Buffer.write (ap1In);
            float ap1Out = ap1Delayed - coeff1 * ap1In;

            // --- Delay 1 (with smooth random-walk modulation) ---
            // Skip when frozen to keep tail static (no LFO advance). The
            // LFO output is bounded to ±delayModDepthSamples_ and band-
            // limited by smoothstep interpolation, so it wanders the read
            // tap enough to break modal resonances without producing audio-
            // rate FM sidebands (the "tape hiss" that per-sample white-noise
            // jitter generated).
            float jitter1 = frozen_ ? 0.0f : tank.delay1Lfo.next();
            // Deep+slow mode-smear on top of the shallow mode-breaking jitter. Only
            // advanced when active AND not frozen → depth 0 leaves the read byte-exact.
            float smear1 = (smearActive_ && ! frozen_) ? tank.smearLfo1.next() : 0.0f;
            float del1Read = tank.delay1Samples + jitter1 * modReduction_ + smear1;
            del1Read = std::max (del1Read, 1.0f);
            float del1Out = tank.delay1.readInterpolated (del1Read);
            tank.delay1.write (ap1Out);

            // --- Density cascade: numActiveDensityAPs_ allpasses (3 legacy / 6 dense) ---
            float dense = del1Out;
            if (! frozen_)
            {
                const float dcoeff = std::clamp (densityDiffCoeff_ * densityCoeffBoost_, 0.0f, 0.85f);
                for (int d = 0; d < numActiveDensityAPs_; ++d)
                    dense = tank.densityAP[d].process (dense, dcoeff);
            }

            // --- Damping: per-octave GEQ (9 T60 plateaus) or legacy 3-band ---
            float damped = frozen_ ? dense
                         : (octaveActive_ ? tank.octaveDamping.process (dense, tank.octaveCoeffs)
                                          : tank.damping.process (dense));

            // --- Structural HF damping ---
            if (structHFCoeff_ > 0.0f && ! frozen_)
            {
                float& hfState = (&tank == &leftTank_) ? structHFStateL_ : structHFStateR_;
                hfState = (1.0f - structHFCoeff_) * damped + structHFCoeff_ * hfState;
                damped = hfState;
            }

            // --- In-loop mode notch (boing killer): narrow peaking CUT that
            // compounds per pass -> shortens ONE mode's decay. Off = skipped. ---
            if (notchActive_ && ! frozen_)
            {
                const bool isL = (&tank == &leftTank_);
                float& z1 = isL ? mnZ1L_ : mnZ1R_;
                float& z2 = isL ? mnZ2L_ : mnZ2R_;
                const float y = mnB0_ * damped + z1;
                z1 = mnB1_ * damped - mnA1_ * y + z2;
                z2 = mnB2_ * damped - mnA2_ * y;
                damped = y;
            }

            // --- Sustain-band limiter (in-loop; SustainBandLimiter.h): applies the
            // shared reduction (advanced at sample top) to this tank's split band;
            // accumulates |band| for the next advance. Inactive → bit-null. ---
            if (susLimMidCfg_.active && ! frozen_)
            {
                auto& sp = (&tank == &leftTank_) ? susLimMidSplitL_ : susLimMidSplitR_;
                const float b = sp.band (damped, susLimMidCfg_);
                susLimMidAcc_ += std::fabs (b);
                damped -= susLimMidRed_ * b;
            }
            if (susLimLowCfg_.active && ! frozen_)
            {
                auto& sp = (&tank == &leftTank_) ? susLimLowSplitL_ : susLimLowSplitR_;
                const float b = sp.band (damped, susLimLowCfg_);
                susLimLowAcc_ += std::fabs (b);
                damped -= susLimLowRed_ * b;
            }

            // --- Per-pass HF-sustain shelf (setHFSustain): cancels the compounding
            // interpolation/AA HF loss that cliffs the top octave. 0 dB = skipped/bit-null.
            if (hfSusGain_ > 0.0f && ! frozen_)
            {
                float& lp = (&tank == &leftTank_) ? hfSusLpL_ : hfSusLpR_;
                lp += hfSusCoeff_ * (damped - lp);
                damped += hfSusGain_ * (damped - lp);
            }

            // --- Static allpass (decay diffusion 2) ---
            float coeff2 = frozen_ ? 0.0f : decayDiff2_;
            float ap2Out = tank.ap2.process (damped, coeff2);

            // --- Delay 2 (with smooth random-walk modulation) ---
            float jitter2 = frozen_ ? 0.0f : tank.delay2Lfo.next();
            float smear2 = (smearActive_ && ! frozen_) ? tank.smearLfo2.next() : 0.0f;
            float del2Read = tank.delay2Samples + jitter2 * modReduction_ + smear2;
            del2Read = std::max (del2Read, 1.0f);
            float del2Out = tank.delay2.readInterpolated (del2Read);
            // Denormal prevention: tiny alternating bias
            float bias = frozen_ ? 0.0f
                                 : (((tank.delay2.writePos ^ 1) & 1)
                                        ? +DspUtils::kDenormalPrevention
                                        : -DspUtils::kDenormalPrevention);
            tank.delay2.write (ap2Out + bias);

            // Cross-feed output: end of this tank feeds the other tank's input.
            // softClip on the cross-feed write delivers analog-style soft
            // saturation that engages only when transients drive the loop above
            // ±1.0 (quiet tail samples pass through linearly, so RT60 is
            // preserved). Hard kSafetyClip stays as the stability backstop.
            tank.crossFeedState = std::clamp (DspUtils::softClip (del2Out, satThreshold, satCeiling),
                                              -kSafetyClip, kSafetyClip);
        };

        // Save right tank's cross-feed state before left tank overwrites it
        float rightCrossFeed = rightTank_.crossFeedState;
        float leftCrossFeed = leftTank_.crossFeedState;

        processTank (leftTank_, rightCrossFeed, +1.0f);
        processTank (rightTank_, leftCrossFeed, -1.0f);

        // ------------------------------------------------------------------
        // Output: sum 7 signed taps from both tanks per channel.
        const OutputTap* lTaps = useCustomTaps_ ? customLeftTaps_ : kLeftOutputTaps;
        const OutputTap* rTaps = useCustomTaps_ ? customRightTaps_ : kRightOutputTaps;

        float outL = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outL += readOutputTap (lTaps[t]) * lTaps[t].sign * lTaps[t].gain;

        float outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outR += readOutputTap (rTaps[t]) * rTaps[t].sign * rTaps[t].gain;

        // Normalize 7-tap sum. The tank has much higher internal energy than
        // the FDN (2 loops vs 16 channels with Hadamard ÷4 normalization),
        // so we use a lower output scale to match FDN output levels.
        constexpr float kOutputScale = 0.14285714f;  // 1/7 — average of 7 taps
        const float outputGain = kOutputScale * lateGainScale_;

        float scaledL = outL * outputGain;
        float scaledR = outR * outputGain;

        // Static tonal-correction GEQ on the wet output (steady-state spectral
        // shaping decoupled from decay). Identity when inactive → bit-null.
        if (tonalCorrActive_)
        {
            scaledL = tonalCorrL_.process (scaledL, tonalCorrCoeffs_);
            scaledR = tonalCorrR_.process (scaledR, tonalCorrCoeffs_);
        }

        // Slow-attack bloom swell (input-onset-driven). Pinned 1.0 when inactive.
        if (bloomActive_)
        {
            scaledL *= bloomEnv_;
            scaledR *= bloomEnv_;
        }

        // Soft output onset ramp: smooths the initial transient spike from early taps.
        // Ramps from 0→1 linearly over softOnsetMs_ after reset/preset change.
        if (softOnsetEnvL_ < 1.0f)
        {
            scaledL *= softOnsetEnvL_;
            scaledR *= softOnsetEnvL_;
            softOnsetEnvL_ = std::min (softOnsetEnvL_ + softOnsetCoeff_, 1.0f);
        }

        // Peak limiter: fast-attack / slow-release envelope follower with gain reduction.
        // Reduces transient peaks while preserving RMS level (lowers crest factor).
        // Attack: instant (0 samples). Release: ~50ms one-pole decay.
        // When peak exceeds limiterThreshold_, gain is reduced to keep output at threshold.
        if (limiterThreshold_ > 0.0f)
        {
            float peakLR = std::max (std::abs (scaledL), std::abs (scaledR));

            // Envelope: instant attack, slow release
            if (peakLR > limiterEnv_)
                limiterEnv_ = peakLR;  // Instant attack
            else
                limiterEnv_ = limiterReleaseCoeff_ * limiterEnv_
                            + (1.0f - limiterReleaseCoeff_) * peakLR;  // Slow release

            // Gain reduction: when envelope > threshold, reduce gain
            if (limiterEnv_ > limiterThreshold_)
            {
                float gain = limiterThreshold_ / limiterEnv_;
                scaledL *= gain;
                scaledR *= gain;
            }
        }

        outputL[i] = std::clamp (scaledL, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (scaledR, -kSafetyClip, kSafetyClip);
    }
}

// -----------------------------------------------------------------------
// Output tap reading

float DattorroTank::readOutputTap (const OutputTap& tap) const
{
    // Map buffer index to the actual delay buffer:
    // 0=leftDelay1, 1=leftDelay2, 2=leftAP2,
    // 3=rightDelay1, 4=rightDelay2, 5=rightAP2
    const DelayLine* delayBuf = nullptr;
    float totalDelay = 0.0f;

    switch (tap.bufferIndex)
    {
        case 0: delayBuf = &leftTank_.delay1;  totalDelay = leftTank_.delay1Samples;  break;
        case 1: delayBuf = &leftTank_.delay2;  totalDelay = leftTank_.delay2Samples;  break;
        case 2:
        {
            // Read from AP2's internal buffer at a fractional position
            const auto& ap = leftTank_.ap2;
            int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
            tapOffset = std::max (tapOffset, 1);
            return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
        }
        case 3: delayBuf = &rightTank_.delay1; totalDelay = rightTank_.delay1Samples; break;
        case 4: delayBuf = &rightTank_.delay2; totalDelay = rightTank_.delay2Samples; break;
        case 5:
        {
            const auto& ap = rightTank_.ap2;
            int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
            tapOffset = std::max (tapOffset, 1);
            return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
        }
        default: return 0.0f;
    }

    // Read from delay line at fractional position
    float tapDelay = tap.positionFrac * totalDelay;
    tapDelay = std::max (tapDelay, 1.0f);
    return delayBuf->readInterpolated (tapDelay);
}

// -----------------------------------------------------------------------
// Parameter setters

void DattorroTank::setDecayTime (float seconds)
{
    decayTime_ = std::max (seconds, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setMidMultiply (float mult)
{
    midMultiply_ = std::clamp (mult, 0.1f, 4.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
    // Wired into the per-sample softClip in process() — no recomputation needed.
}

void DattorroTank::setStereoInput (float amount)
{
    stereoInputAmount_ = std::clamp (amount, 0.0f, 4.0f);
    stereoInputActive_ = stereoInputAmount_ > 1.0e-6f;
}

void DattorroTank::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::min (std::max (hz, 1.0f), highCrossoverFreq_ - 10.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::max (hz, crossoverFreq_ + 10.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setModDepth (float depth)
{
    // Cache the original requested value so prepare() can replay it at a new
    // sample rate without losing precision from clamping.
    lastModDepthRaw_ = depth;

    // Clamp depth so modDepthSamples_ cannot exceed the ±32-sample modulation
    // headroom reserved in prepare()'s buffer allocation.
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float maxDepth = 32.0f / (16.0f * std::max (rateRatio, 1.0f));
    float clampedDepth = std::clamp (depth, 0.0f, maxDepth);
    // Map 0-1 knob range to 0-16 samples peak excursion (Dattorro: 8 samples typical)
    modDepthSamples_ = clampedDepth * 16.0f * rateRatio;
    leftTank_.lfo.setDepth  (modDepthSamples_);
    rightTank_.lfo.setDepth (modDepthSamples_);

    // Delay-tap modulation depth. SQUARED mapping (depth² × 8) so the per-
    // delay-line wander is essentially silent at low Mod Depth knob values
    // (modD=0.1 → 0.08 samples peak; effectively zero) while preserving
    // the full ±8-sample wander at modD=1.0 to disrupt modal resonances on
    // long decays. Linear mapping (commit 140017f) was inadvertently
    // stacking 3 LFOs at audible depths even when Mod Depth was dialed
    // low — short/medium plates picked up a chorus character that wasn't
    // there before. Squared curve restores the pre-140017f tail clarity
    // at conservative depths without losing the anti-modal benefit at
    // max depth.
    delayModDepthSamples_ = clampedDepth * clampedDepth * 8.0f * rateRatio;
    leftTank_.delay1Lfo .setDepth (delayModDepthSamples_);
    leftTank_.delay2Lfo .setDepth (delayModDepthSamples_);
    rightTank_.delay1Lfo.setDepth (delayModDepthSamples_);
    rightTank_.delay2Lfo.setDepth (delayModDepthSamples_);
}

void DattorroTank::setModRate (float hz)
{
    modRateHz_ = hz;
    if (prepared_)
        updateLFORates();
}

void DattorroTank::setModeSmear (float depthSamples, float rateHz)
{
    // Clamp depth to the same 512-sample ceiling FDNReverb uses: the read offset
    // (delaySamples + jitter + smear) must stay inside the per-line buffer slack
    // so a deep excursion never reads stale/wrapped history. Buffers reserve
    // maxDelay×(size 4x) + 2·maxModExcursion, so 512 is safely inside for the
    // baked depths (≤300) at any runtime size.
    smearDepthSamples_ = std::clamp (depthSamples, 0.0f, 512.0f);
    smearRateHz_       = std::clamp (rateHz, 0.001f, 5.0f);
    smearActive_       = smearDepthSamples_ > 1.0e-3f;
    if (! prepared_)
        return;
    // Slightly detune the two taps (like the mode-breaking LFOs) so the L/R + tap
    // paths never trace the same wander. Depth is a peak excursion in samples.
    leftTank_ .smearLfo1.setDepth (smearDepthSamples_); leftTank_ .smearLfo1.setRate (smearRateHz_);
    leftTank_ .smearLfo2.setDepth (smearDepthSamples_); leftTank_ .smearLfo2.setRate (smearRateHz_ * 1.11f);
    rightTank_.smearLfo1.setDepth (smearDepthSamples_); rightTank_.smearLfo1.setRate (smearRateHz_ * 0.93f);
    rightTank_.smearLfo2.setDepth (smearDepthSamples_); rightTank_.smearLfo2.setRate (smearRateHz_ * 1.04f);
}

void DattorroTank::setLimiter (float thresholdDb, float releaseMs)
{
    if (thresholdDb <= -60.0f || thresholdDb >= 0.0f)
    {
        limiterThreshold_ = 0.0f;  // Disabled
        return;
    }
    limiterThreshold_ = std::pow (10.0f, thresholdDb / 20.0f);
    limiterReleaseMs_ = releaseMs;
    if (prepared_)
    {
        float releaseSamples = releaseMs * 0.001f * static_cast<float> (sampleRate_);
        limiterReleaseCoeff_ = std::exp (-1.0f / std::max (releaseSamples, 1.0f));
    }
}

void DattorroTank::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DattorroTank::setSoftOnsetMs (float ms)
{
    float newMs = std::max (ms, 0.0f);
    bool changed = (newMs > 0.0f) != (softOnsetMs_ > 0.0f)
                || std::abs (newMs - softOnsetMs_) > 0.01f;
    softOnsetMs_ = newMs;

    if (prepared_ && softOnsetMs_ > 0.0f)
    {
        // Per-sample increment for linear ramp from 0→1 over softOnsetMs
        float samples = softOnsetMs_ * 0.001f * static_cast<float> (sampleRate_);
        softOnsetCoeff_ = 1.0f / std::max (samples, 1.0f);
        // Only reset ramp when value actually changes (not on every processBlock call)
        if (changed)
            softOnsetEnvL_ = 0.0f;
    }
    else
    {
        softOnsetCoeff_ = 0.0f;  // Disabled
        softOnsetEnvL_ = 1.0f;   // Full gain immediately
    }
}

void DattorroTank::setDelayScale (float scale)
{
    delayScale_ = std::clamp (scale, 0.25f, 4.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

// setNoiseModDepth() removed in v0.5.3: the per-sample white-noise jitter
// it controlled has been replaced by smoothstep-interpolated random-walk
// LFOs on delay1 and delay2 (see Tank::delay1Lfo / delay2Lfo). White noise
// on a delay-line read tap is audio-rate phase modulation, which generates
// broadband FM sidebands audible as tape-style hiss; the LFO output is
// band-limited and produces no such sidebands while still wandering the
// read position enough to break modal resonances.

void DattorroTank::setOutputTaps (const OutputTap* left, const OutputTap* right)
{
    if (left && right)
    {
        for (int i = 0; i < kNumOutputTaps; ++i)
        {
            customLeftTaps_[i] = left[i];
            customRightTaps_[i] = right[i];
        }
        useCustomTaps_ = true;
    }
    else
    {
        useCustomTaps_ = false;
    }
}

void DattorroTank::applyTapGains (const float* leftGains, const float* rightGains)
{
    if (leftGains && rightGains)
    {
        // If custom taps aren't already active, initialize from defaults
        // so buffer indices and positions are valid before applying gains.
        if (! useCustomTaps_)
        {
            for (int i = 0; i < kNumOutputTaps; ++i)
            {
                customLeftTaps_[i] = kLeftOutputTaps[i];
                customRightTaps_[i] = kRightOutputTaps[i];
            }
        }

        for (int i = 0; i < kNumOutputTaps; ++i)
        {
            customLeftTaps_[i].gain = leftGains[i];
            customRightTaps_[i].gain = rightGains[i];
        }
        useCustomTaps_ = true;
    }
}

void DattorroTank::setTankDiffusion (float amount)
{
    // Scale around baseline: knob 0 → 0.5×, knob 0.5 → 1.0× (= 0.55 baseline),
    // knob 0.85 (typical preset) → 1.095×, knob 1.0 → 1.2×. Hard cap at 0.85
    // keeps the AP comfortably inside its stable range.
    float a = std::clamp (amount, 0.0f, 1.0f);
    float scale = 0.5f + a * 0.7f;
    densityDiffCoeff_ = std::clamp (kDensityDiffBaseline_ * scale, 0.0f, 0.85f);
}

void DattorroTank::setFreeze (bool frozen)
{
    bool wasTransition = (frozen != frozen_);
    frozen_ = frozen;
    if (wasTransition)
    {
        structHFStateL_ = 0.0f;
        structHFStateR_ = 0.0f;
    }
}

void DattorroTank::setLateGainScale (float scale)
{
    lateGainScale_ = std::max (scale, 0.0f);
}

void DattorroTank::setSizeRange (float min, float max)
{
    float newMin = std::max (min, 0.0f);
    float newMax = std::max (max, newMin);
    if (prepared_)
    {
        newMin = std::min (newMin, sizeRangeAllocatedMax_);
        newMax = std::min (newMax, sizeRangeAllocatedMax_);
    }
    sizeRangeMin_ = newMin;
    sizeRangeMax_ = std::max (newMax, sizeRangeMin_);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DattorroTank::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setDensityJitter (float fraction)
{
    // Clamp to the 2 % the density-AP buffers allocate read headroom for.
    densityJitterFraction_ = std::clamp (fraction, 0.0f, kMaxDensityJitterFraction);
    const float sr = static_cast<float> (sampleRate_);
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        leftTank_ .densityAP[i].jitterDepthFraction = densityJitterFraction_;
        rightTank_.densityAP[i].jitterDepthFraction = densityJitterFraction_;
        leftTank_ .densityAP[i].updateJitterDepth (sr);
        rightTank_.densityAP[i].updateJitterDepth (sr);
    }
}

void DattorroTank::setDensityDepth (float depth01)
{
    const float d = std::clamp (depth01, 0.0f, 1.0f);
    // 0 -> legacy 3 APs, no boost (byte-identical). >0 -> all 6 density APs +
    // a small coefficient lift (denser, smoother tail without modulation).
    numActiveDensityAPs_ = (d > 0.001f) ? kNumDensityAPs : 3;
    densityCoeffBoost_   = 1.0f + d * 0.25f;   // clamped <0.85 at the process site
    // numActiveDensityAPs_ feeds the loop-length sum in updateDecayCoefficients()
    // (only active density APs recirculate). Re-derive the decay gains so they
    // stay matched to the new density-AP count instead of the prior loop length.
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setOctaveT60 (int band, float seconds)
{
    if (band < 0 || band >= 9) return;
    octaveT60_[band] = std::max (0.0f, seconds);
    // Active when ANY octave carries a target; all-zero → legacy 3-band (bit-null).
    octaveActive_ = false;
    for (int k = 0; k < 9; ++k)
        if (octaveT60_[k] > 0.001f) { octaveActive_ = true; break; }
    if (prepared_) updateDecayCoefficients();   // re-derive octave coeffs
}

void DattorroTank::setOctaveDecayRef (float seconds)
{
    octaveDecayRef_ = std::max (0.0f, seconds);
    if (prepared_) updateDecayCoefficients();
}

void DattorroTank::setTonalCorrDb (int band, float dB)
{
    if (band < 0 || band >= 9) return;
    // Cut-only (≤0 dB): designCoeffs' stability guard caps |H|<1, so boosts get
    // clamped away — shape the spectrum with cuts + a Gain-Trim makeup instead.
    tonalCorrGain_[band] = std::pow (10.0f, std::clamp (dB, -24.0f, 0.0f) / 20.0f);
    if (prepared_) updateTonalCorr();
}

void DattorroTank::updateTonalCorr()
{
    tonalCorrActive_ = false;
    for (int k = 0; k < 9; ++k)
        if (std::abs (tonalCorrGain_[k] - 1.0f) > 1.0e-4f) { tonalCorrActive_ = true; break; }
    if (tonalCorrActive_)
        tonalCorrCoeffs_ = OctaveBandDamping::designCoeffs (
            tonalCorrGain_, kOctaveXoverHz, static_cast<float> (sampleRate_));
}

void DattorroTank::setBloomAttackMs (float ms)
{
    bloomAttackMs_ = std::max (0.0f, ms);
    bloomActive_   = bloomAttackMs_ > 0.01f;
    const float sr = static_cast<float> (sampleRate_);
    // Ramp reaches rampPos=1 (env=1) in bloomAttackMs_; the power curve shifts
    // the IR's energy peak toward that time.
    bloomRampInc_     = (bloomActive_ && sr > 0.0f)
        ? 1.0f / std::max (1.0f, bloomAttackMs_ * 0.001f * sr) : 1.0f;
    bloomRearmSamples_ = (sr > 0.0f) ? static_cast<int> (0.15f * sr) : 6615; // 150 ms silence re-arms
    if (! bloomActive_) { bloomEnv_ = 1.0f; bloomRampPos_ = 1.0f; }           // pinned open = bit-null
}

void DattorroTank::setBloomExp (float e)
{
    bloomExp_ = std::clamp (e, 1.0f, 8.0f);
}

void DattorroTank::setInputDiffusionScale (float scale01)
{
    // Input diffusion is DECOUPLED from the in-loop density APs: it targets the
    // first-60ms kurtosis (the in-loop APs densify the late tail). scale 0 =
    // bypassed = bit-identical legacy; >0 engages the cascade at that coeff
    // scale. Cap so the strongest coeff (0.65) stays well under the 0.85 ceiling.
    inputDiffCoeffScale_  = std::clamp (scale01, 0.0f, 1.13f);
    inputDiffusionActive_ = (scale01 > 0.001f);
}

void DattorroTank::setModReduction (float reduction01)
{
    // 1.0 = legacy modulation; <1.0 pulls AP1 + delay1/2 modulation toward still.
    modReduction_ = std::clamp (reduction01, 0.0f, 1.0f);
}

void DattorroTank::setModeNotch (float hz, float cutDb, float q)
{
    mnHz_ = hz; mnCutDb_ = cutDb; mnQ_ = std::clamp (q, 0.5f, 40.0f);
    if (hz < 20.0f || cutDb >= -0.01f)
    {
        notchActive_ = false;
        mnB0_ = 1.0f; mnB1_ = mnB2_ = mnA1_ = mnA2_ = 0.0f;
        mnZ1L_ = mnZ2L_ = mnZ1R_ = mnZ2R_ = 0.0f;
        return;
    }
    // RBJ peaking EQ, negative gain = narrow cut. Inside the loop the cut
    // compounds per pass, shortening the mode's T60 without a broadband dip.
    const float sr = static_cast<float> (sampleRate_);
    const float A  = std::pow (10.0f, std::clamp (cutDb, -12.0f, 0.0f) / 40.0f);
    const float w0 = 6.283185307179586f * std::clamp (hz, 20.0f, 0.45f * sr) / sr;
    const float alpha = std::sin (w0) / (2.0f * mnQ_);
    const float cosw = std::cos (w0);
    const float a0 = 1.0f + alpha / A;
    mnB0_ = (1.0f + alpha * A) / a0;
    mnB1_ = (-2.0f * cosw) / a0;
    mnB2_ = (1.0f - alpha * A) / a0;
    mnA1_ = (-2.0f * cosw) / a0;
    mnA2_ = (1.0f - alpha / A) / a0;
    mnZ1L_ = mnZ2L_ = mnZ1R_ = mnZ2R_ = 0.0f;
    notchActive_ = true;
}

void DattorroTank::setSustainLimiterMid (float loHz, float hiHz, float threshDb, float maxCutDb,
                                         float atkMs, float relFastMs, float relSlowMs)
{
    susLimMidCfg_.set (loHz, hiHz, threshDb, maxCutDb, atkMs, relFastMs, relSlowMs, sampleRate_);
    susLimMidDet_.clear();
    susLimMidRed_ = susLimMidAcc_ = 0.0f;
}

void DattorroTank::setSustainLimiterLow (float loHz, float hiHz, float threshDb, float maxCutDb,
                                         float atkMs, float relFastMs, float relSlowMs)
{
    susLimLowCfg_.set (loHz, hiHz, threshDb, maxCutDb, atkMs, relFastMs, relSlowMs, sampleRate_);
    susLimLowDet_.clear();
    susLimLowRed_ = susLimLowAcc_ = 0.0f;
}

void DattorroTank::setHFSustain (float db, float cornerHz)
{
    hfSusDb_    = std::clamp (db, 0.0f, 6.0f);
    hfSusHz_    = std::clamp (cornerHz, 2000.0f, 18000.0f);
    hfSusGain_  = std::pow (10.0f, hfSusDb_ / 20.0f) - 1.0f;   // 0 dB -> 0 -> bit-null
    hfSusCoeff_ = 1.0f - std::exp (-kTwoPi * hfSusHz_ / static_cast<float> (sampleRate_));
    hfSusLpL_ = hfSusLpR_ = 0.0f;
}

void DattorroTank::setStructuralHFDamping (float hz)
{
    lastStructHFHz_ = hz;
    if (hz <= 0.0f)
    {
        structHFCoeff_ = 0.0f;
        structHFStateL_ = 0.0f;
        structHFStateR_ = 0.0f;
        return;
    }
    structHFCoeff_ = std::exp (-kTwoPi * hz / static_cast<float> (sampleRate_));
}

void DattorroTank::clearBuffers()
{
    auto clearTank = [this] (Tank& tank, uint32_t seed)
    {
        tank.ap1Buffer.clear();
        tank.delay1.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].clear();
        tank.ap2.clear();
        tank.delay2.clear();
        tank.damping.reset();
        tank.octaveDamping.reset();
        tank.crossFeedState = 0.0f;
        tank.savedAP1Mod = 0.0f;  // keep from the code-review fix
        tank.lfo.prepare (static_cast<float> (sampleRate_), seed);
        tank.lfo.setRate (modRateHz_);
        tank.lfo.setDepth (modDepthSamples_);

        // Reset the delay-tap LFOs too. Re-prepare with the same XOR-derived
        // seeds used in prepare() so each engine instance produces the same
        // wander pattern from a clean state — important during the
        // dual-engine preset crossfade so the swapped-in engine starts
        // deterministically rather than carrying stale LFO phase from a
        // previous use.
        tank.delay1Lfo.prepare (static_cast<float> (sampleRate_), seed ^ 0xA5A5A5A5u);
        tank.delay1Lfo.setRate (modRateHz_ * 0.95f);
        tank.delay1Lfo.setDepth (delayModDepthSamples_);
        tank.delay2Lfo.prepare (static_cast<float> (sampleRate_), seed ^ 0x5A5A5A5Au);
        tank.delay2Lfo.setRate (modRateHz_ * 1.05f);
        tank.delay2Lfo.setDepth (delayModDepthSamples_);
    };

    clearTank (leftTank_, 0x12345678u);
    clearTank (rightTank_, 0x87654321u);
    tonalCorrL_.reset();
    tonalCorrR_.reset();
    // Input diffusion cascade — zero the static-allpass buffers (delaySamples is
    // set in prepare() and survives a clear, so no re-derivation needed here).
    for (int k = 0; k < kNumInputDiffusers; ++k)
        inputDiffuser_[k].clear();
    // Reseed the density-AP jitter LFOs with the SAME per-channel seeds used in
    // prepare() (the lambda's tank seed differs), so density modulation also
    // restarts deterministically across resets/engine swaps instead of leaking
    // phase. clear() above only zeros the AP buffer, not the LFO phase.
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        const float sr = static_cast<float> (sampleRate_);
        leftTank_ .densityAP[i].jitterLFO.prepare (sr, 0xBADBEEFu + static_cast<std::uint32_t> (i * 31337));
        rightTank_.densityAP[i].jitterLFO.prepare (sr, 0xC0FFEEu  + static_cast<std::uint32_t> (i * 27449));
        // prepare() only sets phase; restore rate/depth too (clearBuffers does
        // NOT call updateDelayLengths/updateJitterDepth afterwards) so density
        // jitter isn't left disabled until the next size change.
        leftTank_ .densityAP[i].updateJitterDepth (sr);
        rightTank_.densityAP[i].updateJitterDepth (sr);
    }
    // Reset soft onset ramp (starts from 0 if enabled, 1 if disabled)
    softOnsetEnvL_ = (softOnsetMs_ > 0.0f) ? 0.0f : 1.0f;
    limiterEnv_ = 0.0f;
    structHFStateL_ = 0.0f;
    structHFStateR_ = 0.0f;
    // In-loop mode-notch biquad state (mirrors the reset in setModeNotch) — else
    // stale notch samples survive an algorithm/preset swap that calls clearBuffers.
    mnZ1L_ = mnZ2L_ = mnZ1R_ = mnZ2R_ = 0.0f;
    // HF-sustain shelf one-pole state.
    hfSusLpL_ = hfSusLpR_ = 0.0f;
    // Sustain-band limiter: detectors, per-channel splitters, presence key,
    // slewed reduction + accumulators — all in-loop state that must not carry
    // across an algorithm/preset swap.
    susLimMidDet_.clear(); susLimLowDet_.clear();
    susLimMidSplitL_.clear(); susLimMidSplitR_.clear();
    susLimLowSplitL_.clear(); susLimLowSplitR_.clear();
    susLimKey_.clear();
    susLimMidRed_ = susLimLowRed_ = 0.0f;
    susLimMidAcc_ = susLimLowAcc_ = 0.0f;
    // Mode-smear LFOs — reseed to prepare()'s deterministic phase (the clearTank
    // lambda above already does this for the mode-breaking LFOs), then re-apply
    // depth/rate (prepare(seed) leaves depth at 0).
    {
        const float sr = static_cast<float> (sampleRate_);
        leftTank_ .smearLfo1.prepare (sr, 0x12345678u ^ 0xDEADBEEFu);
        leftTank_ .smearLfo2.prepare (sr, 0x12345678u ^ 0xBEEFDEADu);
        rightTank_.smearLfo1.prepare (sr, 0x87654321u ^ 0xDEADBEEFu);
        rightTank_.smearLfo2.prepare (sr, 0x87654321u ^ 0xBEEFDEADu);
        setModeSmear (smearDepthSamples_, smearRateHz_);
    }
    bloomEnv_     = bloomActive_ ? 0.0f : 1.0f;
    bloomRampPos_ = bloomActive_ ? 0.0f : 1.0f;
    bloomQuietSamples_ = bloomRearmSamples_;  // armed: first onset triggers the swell
}

// -----------------------------------------------------------------------
// Internal update methods

void DattorroTank::updateDelayLengths()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;
    float totalScale = sizeScale * delayScale_;  // Combined size + per-algorithm delay scaling

    auto updateTank = [&] (Tank& tank, float dDel1, float dDel2)
    {
        // #87: dDel1/dDel2 = per-line incommensurate detune (identity 1.0 = bit-null).
        tank.ap1DelaySamples = static_cast<float> (tank.ap1BaseDelay) * rateRatio * totalScale;
        tank.delay1Samples   = static_cast<float> (tank.delay1BaseDelay) * rateRatio * totalScale * dDel1;
        tank.delay2Samples   = static_cast<float> (tank.delay2BaseDelay) * rateRatio * totalScale * dDel2;

        // AP2 delay (integer, used by Allpass::process)
        tank.ap2.delaySamples = std::max (1, static_cast<int> (
            static_cast<float> (tank.ap2BaseDelay) * rateRatio * totalScale));

        // Density cascade allpass delays (integer, scaled by rate + size + delayScale)
        // After updating delaySamples we MUST refresh the jitter LFO depth+rate
        // so the spin-and-wander modulation tracks the new delay length.
        const float sr = static_cast<float> (sampleRate_);
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            tank.densityAP[i].delaySamples = std::max (1, static_cast<int> (
                static_cast<float> (tank.densityAPBase[i]) * rateRatio * totalScale));
            tank.densityAP[i].updateJitterDepth (sr);
        }
    };

    updateTank (leftTank_,  mainDetune_[0], mainDetune_[1]);
    updateTank (rightTank_, mainDetune_[2], mainDetune_[3]);
}

void DattorroTank::updateDecayCoefficients()
{
    float sr = static_cast<float> (sampleRate_);
    float lowXoverCoeff = std::exp (-kTwoPi * crossoverFreq_ / sr);
    float highXoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / sr);

    // NOTE: SixAPTank uses a size-dependent AP energy-storage factor for
    // its 6-AP cascade. We tried it here too but it OVER-compensated short-
    // decay plate presets (Bright Drum Plate went from -2 % to -20 % off).
    // Dattorro has only 3 density APs and a different decay-diff structure,
    // so its empirical storage factor is much closer to 1.0. Use the simple
    // sum (storage factor = 1.0) — measured to be the best fit across the
    // 4 plate presets that depend on this engine.
    auto updateTankDamping = [&] (Tank& tank)
    {
        float loopLength = tank.ap1DelaySamples
                         + tank.delay1Samples
                         + static_cast<float> (tank.ap2.delaySamples)
                         + tank.delay2Samples;
        // Only the ACTIVE density APs are in the recirculating path — counting the
        // (allocated-but-unprocessed) extra stages would mis-derive the decay gain
        // and break bit-null at the legacy 3-AP default.
        for (int i = 0; i < numActiveDensityAPs_; ++i)
            loopLength += static_cast<float> (tank.densityAP[i].delaySamples);

        float gBase = std::pow (10.0f, -3.0f * loopLength / (decayTime_ * sr));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);
        // Per-band gains using true 3-band multipliers.
        float gLow = std::clamp (std::pow (gBase, 1.0f / bassMultiply_), 0.001f, 0.9999f);
        float gMid = std::clamp (std::pow (gBase, 1.0f / midMultiply_), 0.001f, 0.9999f);
        float gHi  = std::clamp (std::pow (gBase, 1.0f / trebleMultiply_), 0.001f, 0.9999f);

        tank.damping.setCoefficients (gLow, gMid, gHi, lowXoverCoeff, highXoverCoeff);

        // Per-octave GEQ path (active only when a preset sets octave T60s).
        // gOct[k] = 10^(-3·loopLength / (T60_k · scale · sr)) — same Sabine
        // round-trip-gain math as the 3-band gBase, per ISO octave. The Decay
        // knob scales the whole curve via octaveDecayRef_ so it stays live.
        // designCoeffs() is the message-thread composite-exact solver (this
        // function only runs off the audio thread, from the param setters).
        if (octaveActive_)
        {
            const float scale = (octaveDecayRef_ > 0.05f)
                              ? std::clamp (decayTime_ / octaveDecayRef_, 0.1f, 8.0f) : 1.0f;
            float gOct[9];
            for (int k = 0; k < 9; ++k)
            {
                // Unset (zero) octaves inherit the broadband gBase instead of
                // the clamped 0.01 s minimum, which would otherwise force a
                // dead band. Mirrors the AccurateHall path (FDNReverb.cpp).
                const float Tk = octaveT60_[k] * scale;
                if (Tk > 0.0f)
                {
                    float gk = std::pow (10.0f, -3.0f * loopLength / (Tk * sr));
                    gOct[k] = std::clamp (std::pow (gk, decayBoost_), 0.001f, 0.9999f);
                }
                else
                {
                    gOct[k] = gBase;   // flat octave → broadband decay
                }
            }
            tank.octaveCoeffs = OctaveBandDamping::designCoeffs (gOct, kOctaveXoverHz, sr);
        }
    };

    updateTankDamping (leftTank_);
    updateTankDamping (rightTank_);
}

void DattorroTank::updateLFORates()
{
    // Both tanks at the SAME rate. Originally we used asymmetric L/R rates
    // (×√5/2 offset) to "prevent correlated modulation paths," but this
    // turned out to be the dominant source of LR-correlation jitter — over
    // time the two LFOs drift in and out of phase, causing the late-field
    // L/R correlation to wander. Measured against Arturia LX-24 BladeRunner
    // (LR stddev 0.028), our wandering of 0.066 is audibly less stable
    // than a true random late field.
    //
    // The LFOs already use distinct PRNG seeds (0x12345678u vs 0x87654321u
    // in prepare()), so the per-sample modulation paths are decorrelated
    // even at the same rate. Setting equal rates keeps the average
    // modulation magnitude in phase between tanks while letting the
    // per-sample noise produce natural micro-variance.
    leftTank_.lfo.setRate  (modRateHz_);
    rightTank_.lfo.setRate (modRateHz_);

    // Tightly clustered delay-tap LFO spread (0.95×/1.05×). Previous
    // 0.83×/1.27× spread (44 %) produced audible beating between the AP1
    // and delay-tap modulators on long tails — the same chorus-in-tail
    // pathology as FDN's 70 % spread. Tight cluster keeps incommensurate
    // paths to avoid mechanical phase-lock without producing chorus.
    constexpr float kDelay1RateScale = 0.95f;
    constexpr float kDelay2RateScale = 1.05f;
    leftTank_.delay1Lfo .setRate (modRateHz_ * kDelay1RateScale);
    leftTank_.delay2Lfo .setRate (modRateHz_ * kDelay2RateScale);
    rightTank_.delay1Lfo.setRate (modRateHz_ * kDelay1RateScale);
    rightTank_.delay2Lfo.setRate (modRateHz_ * kDelay2RateScale);
}

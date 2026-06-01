#include "QuadTank.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// -----------------------------------------------------------------------
// DelayLine helpers (same as DattorroTank)

void QuadTank::DelayLine::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void QuadTank::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float QuadTank::DelayLine::readInterpolated (float delaySamples) const
{
    float readPos = static_cast<float> (writePos) - delaySamples;
    int intIdx = static_cast<int> (std::floor (readPos));
    float frac = readPos - static_cast<float> (intIdx);
    return DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
}

// -----------------------------------------------------------------------
void QuadTank::Allpass::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void QuadTank::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// -----------------------------------------------------------------------
QuadTank::QuadTank()
{
    for (int t = 0; t < kNumTanks; ++t)
    {
        tanks_[t].ap1BaseDelay     = kTankConfigs[t].ap1Base;
        tanks_[t].delay1BaseDelay  = kTankConfigs[t].del1Base;
        tanks_[t].ap2BaseDelay     = kTankConfigs[t].ap2Base;
        tanks_[t].delay2BaseDelay  = kTankConfigs[t].del2Base;
        for (int i = 0; i < kNumDensityAPs; ++i)
            tanks_[t].densityAPBase[i] = kTankConfigs[t].densityAPBase[i];
    }
}

// -----------------------------------------------------------------------
void QuadTank::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    sizeRangeAllocatedMax_ = std::max (sizeRangeAllocatedMax_, std::max (sizeRangeMax_, 1.5f));
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
    const int maxModExcursion = static_cast<int> (std::ceil (32.0 * sampleRate / kBaseSampleRate));

    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];

        int ap1Max = static_cast<int> (std::ceil (tank.ap1BaseDelay * rateRatio * sizeRangeAllocatedMax_)) + maxModExcursion;
        int del1Max = static_cast<int> (std::ceil (tank.delay1BaseDelay * rateRatio * sizeRangeAllocatedMax_)) + maxModExcursion;
        int ap2Max = static_cast<int> (std::ceil (tank.ap2BaseDelay * rateRatio * sizeRangeAllocatedMax_)) + maxModExcursion;
        int del2Max = static_cast<int> (std::ceil (tank.delay2BaseDelay * rateRatio * sizeRangeAllocatedMax_)) + maxModExcursion;

        tank.ap1Buffer.allocate (ap1Max);
        tank.delay1.allocate (del1Max + maxModExcursion);
        tank.ap2.allocate (ap2Max);
        tank.delay2.allocate (del2Max + maxModExcursion);

        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            int dapMax = static_cast<int> (std::ceil (tank.densityAPBase[i] * rateRatio * sizeRangeAllocatedMax_)) + 4;
            tank.densityAP[i].allocate (dapMax);
            // Sub-audio (1.5 Hz) density-AP jitter — mirrors the DattorroTank
            // fix. The audio-band variant generated #87 vibrato; slow
            // random-walk wander breaks comb-tooth phase-lock on Hall/Room/
            // Chamber presets without sidebands. 5 % depth needed for short
            // QuadTank density-AP delays (~100-200 samples on small-size
            // presets) where 3 % wasn't enough to spread comb teeth.
            tank.densityAP[i].jitterDepthFraction = 0.05f;
        }

        tank.damping.prepare (static_cast<float> (sampleRate));
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    }

    // Random-walk LFOs — distinct seeds so each tank's modulation traces an
    // independent path. Per-tank rate detune (in updateLFORates) further
    // ensures the streams never settle into a beating pattern. Three LFOs
    // per tank: AP1 modulation + delay1 + delay2 read taps. The delay-tap
    // seeds are XOR-derived from the AP1 seed so all three are deterministic
    // and decorrelated. Mirrors DattorroTank v0.5.3.
    static constexpr uint32_t kLFOSeeds[kNumTanks] = { 0x12345678u, 0x87654321u, 0xABCDEF01u, 0x13579BDFu };

    for (int t = 0; t < kNumTanks; ++t)
    {
        const float sr = static_cast<float> (sampleRate);
        tanks_[t].lfo       .prepare (sr, kLFOSeeds[t]);
        tanks_[t].delay1Lfo .prepare (sr, kLFOSeeds[t] ^ 0xA5A5A5A5u);
        tanks_[t].delay2Lfo .prepare (sr, kLFOSeeds[t] ^ 0x5A5A5A5Au);
        tanks_[t].savedAP1Mod    = 0.0f;
        tanks_[t].savedDelay1Mod = 0.0f;
        tanks_[t].savedDelay2Mod = 0.0f;

        // Per-density-AP jitter LFOs with distinct seeds per tank+stage.
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            const std::uint32_t s = 0xBADBEEFu
                                    + static_cast<std::uint32_t> (t * 0x9E3779B9u)
                                    + static_cast<std::uint32_t> (i * 31337);
            tanks_[t].densityAP[i].jitterLFO.prepare (sr, s);
        }
    }

    prepared_ = true;

    // Phase 2 master coherent LFO. Distinct seed so phase doesn't track any
    // tank's random walk; updateLFORates() will push the actual rate.
    coherentLfo_.prepare (static_cast<float> (sampleRate), 0xC0FFEE2Cu);

    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();
    setModDepth (lastModDepthRaw_);

    // Replay sample-rate-dependent setters so their scaled state is correct
    // after a host re-prepare at a different sample rate. setModDepth()
    // (called above) updates delay1Lfo/delay2Lfo depths — there's no
    // separate noise-jitter setter any more (the white-noise modulation
    // has been replaced by the smoothstep-interpolated delay LFOs).
    if (lastStructHFRawHz_ > 0.0f)
        setStructuralHFDamping (lastStructHFRawHz_);

    // Clear structural HF damping state. Without this, a host re-prepare
    // would start with empty delay buffers but retain previous tracker
    // state, leaking session state across reconfigure.
    for (int t = 0; t < kNumTanks; ++t)
        structHFState_[t] = 0.0f;
}

// -----------------------------------------------------------------------
void QuadTank::process (const float* inputL, const float* inputR,
                        float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    // Drive-style saturation (see DattorroTank for rationale).
    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;

    // Phase 2: per-tank phase offsets for CoherentLoop topology. Quadrature
    // spread — tanks 0/2 oppose at 180°, tanks 1/3 oppose at 180° but offset
    // 90° from the (0,2) pair. Result: cyclic rotation across the 4 tanks,
    // creating coherent envelope pumping rather than independent smearing.
    constexpr float kPi = 3.14159265358979f;
    static const float kTankPhase[kNumTanks] = {
        0.0f,
        kPi * 0.5f,
        kPi,
        kPi * 1.5f,
    };
    const bool useCoherent =
        (modulationTopology_ == DspUtils::ModulationTopology::CoherentLoop)
        && ! frozen_;

    for (int i = 0; i < numSamples; ++i)
    {
        float input = (inputL[i] + inputR[i]) * 0.5f;
        if (frozen_)
            input = 0.0f;

        // Phase 2: advance master sine ONCE per sample (not per tank).
        if (useCoherent)
            coherentLfo_.advance();

        // Save all cross-feed states before processing
        float cf[kNumTanks];
        for (int t = 0; t < kNumTanks; ++t)
            cf[t] = tanks_[t].crossFeedState;

        // Process each tank with bidirectional cross-coupling. Previous
        // single-direction ring 0→1→2→3→0 concentrated energy into a
        // deterministic 4-tank rotation (periodic modal pattern). Mixing the
        // previous AND next tank's output (forward-weighted) breaks that ring.
        //
        // CALIBRATION FIX 2026-05-31: these weights are NORMALIZED so the
        // coupling matrix's dominant (DC) eigenvalue = kFwd + kBack = 1.0
        // (lossless), preserving the old 0.55:0.20 forward:back RATIO
        // (0.73333:0.26667). The old un-normalized 0.55+0.20 = 0.75 made the
        // coupling lose 25%/cycle, capping RT60 at ~4.3 s regardless of the
        // Decay knob — updateDecayCoefficients then divided gBase by 0.75 to
        // compensate, which saturated gBase at its 1.0 clamp (the knob lied:
        // 9.32 s → 4.3 s). With ρ=1 the per-line gBase alone sets the decay,
        // so the Decay knob reads honest RT60 across the full range. Stability
        // now rests on gBase<1 (enforced in updateDecayCoefficients) + the
        // crossFeedState softClip, exactly as a standard lossless-matrix FDN.
        constexpr float kCrossFeedFwd  = 0.73333f;
        constexpr float kCrossFeedBack = 0.26667f;
        for (int t = 0; t < kNumTanks; ++t)
        {
            auto& tank = tanks_[t];
            const int prev = (t + kNumTanks - 1) % kNumTanks;
            const int next = (t + 1) % kNumTanks;
            const float otherCrossFeed = kCrossFeedFwd  * cf[prev]
                                       + kCrossFeedBack * cf[next];
            float tankIn = input + otherCrossFeed;

            // --- Modulated allpass (decay diffusion 1) ---
            // Phase 2: in CoherentLoop mode, read from master sine at this
            // tank's quadrature phase offset and scale to the AP1 mod depth.
            // delay1Lfo / delay2Lfo below stay random-walk because they're
            // micro-jitter decorrelators, not envelope modulators.
            float mod;
            if (frozen_)
            {
                mod = tank.savedAP1Mod;
            }
            else if (useCoherent)
            {
                // tank.lfo's RandomWalk depth was already set by setModDepth
                // via updateLFORates; reuse that scale for amplitude parity
                // between topologies. ModDepthSamples_ field tracks it.
                mod = coherentLfo_.read (kTankPhase[t]) * modDepthSamples_;
                tank.savedAP1Mod = mod;
            }
            else
            {
                mod = tank.lfo.next();
                tank.savedAP1Mod = mod;
            }
            float ap1ReadDelay = tank.ap1DelaySamples + mod;
            ap1ReadDelay = std::max (ap1ReadDelay, 1.0f);

            float ap1Delayed = tank.ap1Buffer.readInterpolated (ap1ReadDelay);
            float coeff1 = frozen_ ? 0.0f : decayDiff1_;
            float ap1In = tankIn + coeff1 * ap1Delayed;
            tank.ap1Buffer.write (ap1In);
            float ap1Out = ap1Delayed - coeff1 * ap1In;

            // --- Delay 1 with band-limited random-walk modulation ---
            // Replaces per-sample white-noise jitter (issue #87). LFO output
            // is bounded to ±delayModDepthSamples_ and band-limited by
            // smoothstep interpolation, so it wanders the read tap enough
            // to break modal resonances without producing audio-rate FM
            // sidebands.
            float jitter1 = frozen_ ? tank.savedDelay1Mod : tank.delay1Lfo.next();
            if (! frozen_)
                tank.savedDelay1Mod = jitter1;
            float del1Read = tank.delay1Samples + jitter1;
            del1Read = std::max (del1Read, 1.0f);
            float del1Out = tank.delay1.readInterpolated (del1Read);
            tank.delay1.write (ap1Out);

            // --- Density cascade: 3 allpasses ---
            float dense = del1Out;
            if (! frozen_)
            {
                for (int d = 0; d < kNumDensityAPs; ++d)
                    dense = tank.densityAP[d].process (dense, densityDiffCoeff_);
            }

            // --- Two-band damping ---
            float damped = frozen_ ? dense : tank.damping.process (dense);

            // --- Structural HF damping ---
            if (structHFCoeff_ > 0.0f && ! frozen_)
            {
                structHFState_[t] = (1.0f - structHFCoeff_) * damped + structHFCoeff_ * structHFState_[t];
                damped = structHFState_[t];
            }

            // --- Static allpass (decay diffusion 2) ---
            float coeff2 = frozen_ ? 0.0f : decayDiff2_;
            float ap2Out = tank.ap2.process (damped, coeff2);

            // --- Delay 2 with band-limited random-walk modulation ---
            float jitter2 = frozen_ ? tank.savedDelay2Mod : tank.delay2Lfo.next();
            if (! frozen_)
                tank.savedDelay2Mod = jitter2;
            float del2Read = tank.delay2Samples + jitter2;
            del2Read = std::max (del2Read, 1.0f);
            float del2Out = tank.delay2.readInterpolated (del2Read);
            // Denormals handled at processBlock entry via ScopedNoDenormals.
            tank.delay2.write (ap2Out);

            // Cross-feed: feeds next tank. Soft-clip on the way out — analog
            // tape/transformer-style warmth that engages only when transients
            // drive the loop above ±1.0.
            tank.crossFeedState = std::clamp (DspUtils::softClip (del2Out, satThreshold, satCeiling),
                                              -kSafetyClip, kSafetyClip);
        }

        // ------------------------------------------------------------------
        // Output: sum 14 signed taps from all 4 tanks per channel
        float outL = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outL += readOutputTap (kLeftOutputTaps[t]) * kLeftOutputTaps[t].sign;

        float outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outR += readOutputTap (kRightOutputTaps[t]) * kRightOutputTaps[t].sign;

        // Normalize output tap sum. With alternating signs on highly correlated
        // taps (same tank, adjacent delay positions), effective independence is
        // ~8 taps, not 48 — 1/sqrt(48) severely over-attenuates.
        // 0.35 ≈ 1/sqrt(8) compensates for sign cancellation and the slow
        // energy buildup of long-loop tanks vs shorter-loop FDN/DattorroTank.
        constexpr float kOutputScale = 0.35f;  // ~1/sqrt(8)
        const float outputGain = kOutputScale * lateGainScale_;

        outputL[i] = std::clamp (outL * outputGain, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (outR * outputGain, -kSafetyClip, kSafetyClip);
    }
}

// -----------------------------------------------------------------------
float QuadTank::readOutputTap (const OutputTap& tap) const
{
    // Buffer index mapping:
    //   0-3:  Delay1 from tanks 0-3
    //   4-7:  Delay2 from tanks 0-3
    //   8-11: AP2 from tanks 0-3
    int tankIdx = tap.bufferIndex % kNumTanks;
    int bufType = tap.bufferIndex / kNumTanks;  // 0=del1, 1=del2, 2=ap2

    const auto& tank = tanks_[tankIdx];

    if (bufType == 2)
    {
        // Read from AP2 internal buffer at fractional position
        const auto& ap = tank.ap2;
        int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
        tapOffset = std::max (tapOffset, 1);
        return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
    }

    const DelayLine* delayBuf = (bufType == 0) ? &tank.delay1 : &tank.delay2;
    float totalDelay = (bufType == 0) ? tank.delay1Samples : tank.delay2Samples;

    float tapDelay = tap.positionFrac * totalDelay;
    tapDelay = std::max (tapDelay, 1.0f);
    return delayBuf->readInterpolated (tapDelay);
}

// -----------------------------------------------------------------------
void QuadTank::setDecayTime (float seconds)
{
    // Knob-honesty calibration (2026-05-31). After the cross-feed normalization
    // (ρ=1) removed the 4.3 s saturation ceiling, the raw knob→RT60 law is still
    // sub-unity and nonlinear: measured mid-band RT60 ≈ 0.9373·T^0.7247 at
    // nominal size (level-dependent crossFeedState softClip + non-exponential
    // tail shape). Invert that law so the DISPLAYED Decay knob reads true RT60
    // seconds across the usable range. internal = (R / C)^(1/P) is the decay-
    // time target fed to the RT60 formula; clamped to the engine's stable range.
    static constexpr float kDecayCalC = 0.9373f;
    static constexpr float kDecayCalP = 0.7247f;
    const float honest   = std::max (seconds, 0.1f);
    const float internal = std::pow (honest / kDecayCalC, 1.0f / kDecayCalP);
    decayTime_ = std::clamp (internal, 0.1f, 60.0f);
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (mult, 0.1f);
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setMidMultiply (float mult)
{
    midMultiply_ = std::clamp (mult, 0.1f, 4.0f);
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
}

void QuadTank::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (mult, 0.1f);
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setCrossoverFreq (float hz)
{
    crossoverFreq_ = hz;
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setModDepth (float depth)
{
    // Cache the original requested value so prepare() can replay it at a new
    // sample rate without losing precision from clamping.
    lastModDepthRaw_ = depth;

    // Clamp depth so modDepthSamples_ cannot exceed the ±32-sample modulation
    // headroom reserved in prepare()'s buffer allocation (maxModExcursion).
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float maxDepth = 32.0f / (16.0f * std::max (rateRatio, 1.0f));
    float clampedDepth = std::clamp (depth, 0.0f, maxDepth);
    modDepthSamples_ = clampedDepth * 16.0f * rateRatio;

    // Delay-tap modulation depth. Half of the AP1 LFO depth so the long
    // delay reads don't pitch-warp on sustained content (mirrors
    // DattorroTank: a 100 ms delay wandering ±8 samples at <1 Hz is well
    // below detection threshold), while still moving the read tap enough
    // to disrupt modal resonances on long decays.
    delayModDepthSamples_ = clampedDepth * 8.0f * rateRatio;

    for (int t = 0; t < kNumTanks; ++t)
    {
        tanks_[t].lfo       .setDepth (modDepthSamples_);
        tanks_[t].delay1Lfo .setDepth (delayModDepthSamples_);
        tanks_[t].delay2Lfo .setDepth (delayModDepthSamples_);
    }
}

// setNoiseModDepth() removed in fix for issue #87: per-sample white-noise
// jitter on delay reads has been replaced by smoothstep-interpolated
// random-walk LFOs (delay1Lfo / delay2Lfo). Same diagnosis as DattorroTank
// v0.5.3 — white noise on a delay-line read is audio-rate phase modulation,
// which generates broadband FM sidebands audible as vibrato/bell-like
// artifacts.

void QuadTank::setModRate (float hz)
{
    modRateHz_ = hz;
    if (prepared_) updateLFORates();
}

void QuadTank::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void QuadTank::setTankDiffusion (float amount)
{
    float a = std::clamp (amount, 0.0f, 1.0f);
    float scale = 0.5f + a * 0.7f;
    densityDiffCoeff_ = std::clamp (kDensityDiffBaseline_ * scale, 0.0f, 0.85f);
}

void QuadTank::setFreeze (bool frozen)
{
    bool wasTransition = (frozen != frozen_);
    frozen_ = frozen;
    if (wasTransition)
    {
        for (int t = 0; t < kNumTanks; ++t)
            structHFState_[t] = 0.0f;
    }
}
void QuadTank::setLateGainScale (float scale) { lateGainScale_ = std::max (scale, 0.0f); }

void QuadTank::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::max (hz, 100.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void QuadTank::setSizeRange (float min, float max)
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

void QuadTank::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void QuadTank::setStructuralHFDamping (float hz)
{
    lastStructHFRawHz_ = hz;
    if (hz <= 0.0f)
    {
        structHFCoeff_ = 0.0f;
        for (int t = 0; t < kNumTanks; ++t)
            structHFState_[t] = 0.0f;
        return;
    }
    structHFCoeff_ = std::exp (-kTwoPi * hz / static_cast<float> (sampleRate_));
}

void QuadTank::clearBuffers()
{
    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];
        tank.ap1Buffer.clear();
        tank.delay1.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].clear();
        tank.ap2.clear();
        tank.delay2.clear();
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    }
    // Re-seed the random-walk LFOs so each clear gives the same predictable
    // starting state — important for A/B compare and bypass toggling in
    // DAWs. Density-AP jitter LFOs are reseeded too so per-stage wander is
    // also deterministic across resets (the buffer .clear() above zeros
    // sample state but leaves the LFO phase/seed where it left off).
    static constexpr uint32_t kLFOSeeds[kNumTanks] = { 0x12345678u, 0x87654321u, 0xABCDEF01u, 0x13579BDFu };
    const float sr = static_cast<float> (sampleRate_);
    for (int t = 0; t < kNumTanks; ++t)
    {
        structHFState_[t] = 0.0f;
        tanks_[t].lfo.prepare (sr, kLFOSeeds[t]);
        tanks_[t].lfo.setRate  (modRateHz_);
        tanks_[t].lfo.setDepth (modDepthSamples_);
        tanks_[t].savedAP1Mod = 0.0f;

        // Reset the delay-tap LFOs too. Re-prepare with the same XOR-derived
        // seeds used in prepare() so each engine instance produces the same
        // wander pattern from a clean state — important during the dual-
        // engine preset crossfade so the swapped-in engine starts
        // deterministically rather than carrying stale LFO phase.
        tanks_[t].delay1Lfo.prepare (sr, kLFOSeeds[t] ^ 0xA5A5A5A5u);
        tanks_[t].delay1Lfo.setDepth (delayModDepthSamples_);
        tanks_[t].delay2Lfo.prepare (sr, kLFOSeeds[t] ^ 0x5A5A5A5Au);
        tanks_[t].delay2Lfo.setDepth (delayModDepthSamples_);
        tanks_[t].savedDelay1Mod = 0.0f;
        tanks_[t].savedDelay2Mod = 0.0f;

        // Mirror prepare()'s per-density-AP seeding scheme so each stage's
        // jitterLFO restarts from the same deterministic state.
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            const std::uint32_t s = 0xBADBEEFu
                                    + static_cast<std::uint32_t> (t * 0x9E3779B9u)
                                    + static_cast<std::uint32_t> (i * 31337);
            tanks_[t].densityAP[i].jitterLFO.prepare (sr, s);
            tanks_[t].densityAP[i].updateJitterDepth (sr);
        }
    }
    // Master coherent LFO (CoherentLoop mode) — reseed its phase too, same seed
    // as prepare(), so its modulation also restarts deterministically across
    // resets/preset swaps (was leaking phase; rate is restored by updateLFORates).
    coherentLfo_.prepare (sr, 0xC0FFEE2Cu);
    // updateLFORates() needs to run after re-prepare to set per-tap rates.
    updateLFORates();
}

// -----------------------------------------------------------------------
void QuadTank::updateDelayLengths()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;

    const float sr = static_cast<float> (sampleRate_);
    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];
        tank.ap1DelaySamples = static_cast<float> (tank.ap1BaseDelay) * rateRatio * sizeScale;
        tank.delay1Samples   = static_cast<float> (tank.delay1BaseDelay) * rateRatio * sizeScale;
        tank.delay2Samples   = static_cast<float> (tank.delay2BaseDelay) * rateRatio * sizeScale;

        tank.ap2.delaySamples = std::max (1, static_cast<int> (
            static_cast<float> (tank.ap2BaseDelay) * rateRatio * sizeScale));

        // Refresh jitter LFO depth + rate when delay changes (matches the
        // SixAPTank + Dattorro pattern). Without this the jitter doesn't
        // track the size knob and density APs ring at large sizes.
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            tank.densityAP[i].delaySamples = std::max (1, static_cast<int> (
                static_cast<float> (tank.densityAPBase[i]) * rateRatio * sizeScale));
            tank.densityAP[i].updateJitterDepth (sr);
        }
    }
}

void QuadTank::updateDecayCoefficients()
{
    float sr = static_cast<float> (sampleRate_);
    float lowCrossoverCoeff = std::exp (-kTwoPi * crossoverFreq_ / sr);
    float highCrossoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / sr);

    // Size-dependent AP energy-storage factor (mirrors SixAPTank + Dattorro).
    // Linear interp: 2.65 at sizeParam=0.5, 1.55 at 1.0. Compensates for the
    // recursive-feedback storage in each density AP shrinking proportionally
    // as the direct loop grows.
    const float storageFactor = 2.65f - 1.10f * sizeParam_;

    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];

        float loopLength = tank.ap1DelaySamples
                         + tank.delay1Samples
                         + static_cast<float> (tank.ap2.delaySamples)
                         + tank.delay2Samples;
        float densityLen = 0.0f;
        for (int i = 0; i < kNumDensityAPs; ++i)
            densityLen += static_cast<float> (tank.densityAP[i].delaySamples);
        loopLength += densityLen * storageFactor;

        // Cross-feed matrix dominant eigenvalue (process() kCrossFeedFwd+Back).
        // NORMALIZED to 1.0 (2026-05-31 calibration fix), so the coupling is
        // lossless and the per-line damping gain gBase alone sets RT60:
        //   gEffTarget = g_damping^cycles → solve g_damping = gEffTarget.
        // The old value 0.75 forced gBaseRaw = gEffTarget/0.75, which exceeded
        // the 1.0 clamp for RT60 above ~4.3 s and saturated (dishonest knob).
        // Keep in sync with kCrossFeedFwd + kCrossFeedBack in process().
        constexpr float kCrossFeedTotal = 0.73333f + 0.26667f;  // = 1.0
        const float gEffTarget = std::pow (10.0f, -3.0f * loopLength / (decayTime_ * sr));
        const float gBaseRaw   = gEffTarget / kCrossFeedTotal;  // = gEffTarget
        float gBase = std::clamp (std::pow (gBaseRaw, decayBoost_), 0.001f, 0.9999f);
        float gLow  = std::clamp (std::pow (gBase, 1.0f / bassMultiply_), 0.001f, 0.9999f);
        // True 3-band: mid band uses midMultiply_ (default 1.0 = natural rate).
        float gMid  = std::clamp (std::pow (gBase, 1.0f / midMultiply_), 0.001f, 0.9999f);
        // Inlined airDampingScale = 0.70 (former tunable, never set externally).
        // Pre-computed product trebleMultiply_ * 0.70f.
        float gHigh = std::clamp (std::pow (gBase, 1.0f / (trebleMultiply_ * 0.70f)), 0.001f, 0.9999f);

        tank.damping.setCoefficients (gLow, gMid, gHigh, lowCrossoverCoeff, highCrossoverCoeff);
    }
}

void QuadTank::updateLFORates()
{
    // Tightly clustered per-tank rate spread (0.97×–1.03×). Previous
    // 0.89×–1.24× spread (38 %) produced sideband beating between the
    // four tanks audible as chorusing on long room tails. With unique
    // PRNG seeds per tank, slight rate variance is enough to avoid
    // mechanical phase-lock without producing audible chorus character.
    static constexpr float kRateMultipliers[kNumTanks] = {
        0.970f, 0.990f, 1.010f, 1.030f
    };
    // Delay-tap LFOs tightened similarly (was 0.83×/1.27× = 44 % span).
    // Keeps the three modulators per tank incommensurable but tight.
    constexpr float kDelay1RateScale = 0.95f;
    constexpr float kDelay2RateScale = 1.05f;
    for (int t = 0; t < kNumTanks; ++t)
    {
        const float ap1Rate = modRateHz_ * kRateMultipliers[t];
        tanks_[t].lfo       .setRate (ap1Rate);
        tanks_[t].delay1Lfo .setRate (ap1Rate * kDelay1RateScale);
        tanks_[t].delay2Lfo .setRate (ap1Rate * kDelay2RateScale);
    }
    // Phase 2 master sine — base rate, no per-tank spread (per-tank phase
    // offset comes from kTankPhase[] in the process loop).
    coherentLfo_.setRate (modRateHz_);
}

void QuadTank::setModulationTopology (DspUtils::ModulationTopology t)
{
    if (t == modulationTopology_)
        return;
    modulationTopology_ = t;
    if (modulationTopology_ == DspUtils::ModulationTopology::CoherentLoop)
        coherentLfo_.setRate (modRateHz_);
}

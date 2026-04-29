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
            // Lexicon spin-and-wander on each density AP — same fix as
            // SixAPTank + Dattorro got. Breaks AP modal phase-locking.
            tank.densityAP[i].jitterDepthFraction = 0.015f;  // 1.5 % wander
        }

        tank.damping.prepare (static_cast<float> (sampleRate));
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    }

    // Random-walk LFOs — distinct seeds so each tank's modulation traces an
    // independent path. Per-tank rate detune (in updateLFORates) further
    // ensures the four streams never settle into a beating pattern.
    static constexpr uint32_t kLFOSeeds[kNumTanks]   = { 0x12345678u, 0x87654321u, 0xABCDEF01u, 0x13579BDFu };
    static constexpr uint32_t kNoiseSeeds[kNumTanks]  = { 0xDEADBEEFu, 0xCAFEBABEu, 0xFEEDFACEu, 0xBAADF00Du };

    for (int t = 0; t < kNumTanks; ++t)
    {
        tanks_[t].lfo.prepare (static_cast<float> (sampleRate), kLFOSeeds[t]);
        tanks_[t].noiseState = kNoiseSeeds[t];

        // Per-density-AP jitter LFOs with distinct seeds per tank+stage.
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            const std::uint32_t s = 0xBADBEEFu
                                    + static_cast<std::uint32_t> (t * 0x9E3779B9u)
                                    + static_cast<std::uint32_t> (i * 31337);
            tanks_[t].densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), s);
        }
    }

    prepared_ = true;

    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();
    setModDepth (lastModDepthRaw_);

    // Replay sample-rate-dependent setters so their scaled state is correct
    // after a host re-prepare at a different sample rate.
    if (lastNoiseModRawSamples_ >= 0.0f)
        setNoiseModDepth (lastNoiseModRawSamples_);
    if (lastStructHFRawHz_ > 0.0f)
        setStructuralHFDamping (lastStructHFRawHz_);

    // Terminal decay: sample-rate-invariant smoothing coefficients.
    // At 44.1kHz these produce the same behavior as the original constants.
    constexpr float kRmsTauMs  = 45.0f;   // RMS window ~45ms (was 0.9995/0.0005)
    constexpr float kPeakTauMs = 2270.0f;  // Peak decay ~2.27s (was 0.99999)
    float sr = static_cast<float> (sampleRate);
    rmsAlpha_       = std::exp (-1000.0f / (kRmsTauMs * sr));
    peakDecayAlpha_ = std::exp (-1000.0f / (kPeakTauMs * sr));

    // Clear all per-tank stateful fields (RMS history, terminal-decay flags,
    // structural HF damping state). Without this, a host re-prepare would
    // start with empty delay buffers but retain the previous run's tracker
    // state, leaking session state across reconfigure.
    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];
        tank.currentRMS = 0.0f;
        tank.peakRMS = 0.0f;
        tank.terminalDecayActive = false;
        structHFState_[t] = 0.0f;
    }
}

// -----------------------------------------------------------------------
void QuadTank::process (const float* inputL, const float* inputR,
                        float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    const float effectiveNoiseMod = (independentNoiseModDepth_ >= 0.0f)
                                  ? independentNoiseModDepth_ : noiseModDepth_;

    // Drive-style saturation (see DattorroTank for rationale).
    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float input = (inputL[i] + inputR[i]) * 0.5f;
        if (frozen_)
            input = 0.0f;

        // Save all cross-feed states before processing
        float cf[kNumTanks];
        for (int t = 0; t < kNumTanks; ++t)
            cf[t] = tanks_[t].crossFeedState;

        // Process each tank with circular cross-coupling (0←3, 1←0, 2←1, 3←2)
        for (int t = 0; t < kNumTanks; ++t)
        {
            auto& tank = tanks_[t];
            float otherCrossFeed = cf[(t + kNumTanks - 1) % kNumTanks];
            float tankIn = input + otherCrossFeed;

            // --- Modulated allpass (decay diffusion 1) ---
            // Random-walk LFO read. When frozen, hold the last value so the
            // read head doesn't snap to centre on freeze entry.
            float mod = frozen_ ? tank.savedAP1Mod : tank.lfo.next();
            if (! frozen_)
                tank.savedAP1Mod = mod;
            float ap1ReadDelay = tank.ap1DelaySamples + mod;
            ap1ReadDelay = std::max (ap1ReadDelay, 1.0f);

            float ap1Delayed = tank.ap1Buffer.readInterpolated (ap1ReadDelay);
            float coeff1 = frozen_ ? 0.0f : decayDiff1_;
            float ap1In = tankIn + coeff1 * ap1Delayed;
            tank.ap1Buffer.write (ap1In);
            float ap1Out = ap1Delayed - coeff1 * ap1In;

            // --- Delay 1 with noise jitter ---
            float jitter1 = frozen_ ? 0.0f : nextDrift (tank.noiseState) * effectiveNoiseMod;
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

            // --- Delay 2 with noise jitter ---
            float jitter2 = frozen_ ? 0.0f : nextDrift (tank.noiseState) * effectiveNoiseMod;
            float del2Read = tank.delay2Samples + jitter2;
            del2Read = std::max (del2Read, 1.0f);
            float del2Out = tank.delay2.readInterpolated (del2Read);
            float bias = frozen_ ? 0.0f
                                 : (((tank.delay2.writePos ^ 1) & 1)
                                        ? +DspUtils::kDenormalPrevention
                                        : -DspUtils::kDenormalPrevention);
            tank.delay2.write (ap2Out + bias);

            // Terminal decay HARD-DISABLED — see VocalPlatePreset.cpp for rationale.
            if (false /* terminalDecayFactor_ < 1.0f */ && ! frozen_)
            {
                float sampleEnergy = del2Out * del2Out;
                tank.currentRMS = rmsAlpha_ * tank.currentRMS + (1.0f - rmsAlpha_) * sampleEnergy;
                if (tank.currentRMS > tank.peakRMS) tank.peakRMS = tank.currentRMS;
                else tank.peakRMS *= peakDecayAlpha_;
                float ratio = tank.peakRMS / std::max (tank.currentRMS, 1e-20f);
                tank.terminalDecayActive = (ratio > terminalLinearThreshold_) && (tank.peakRMS > 1e-12f);
                if (tank.terminalDecayActive)
                    del2Out *= terminalDecayFactor_;
            }

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
    decayTime_ = std::max (seconds, 0.1f);
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
    lastModDepthRaw_ = depth;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = depth * 16.0f * rateRatio;
    noiseModDepth_ = depth * 12.0f * rateRatio;  // Match DattorroTank (12 samples peak)
    for (int t = 0; t < kNumTanks; ++t)
        tanks_[t].lfo.setDepth (modDepthSamples_);
}

void QuadTank::setNoiseModDepth (float samples)
{
    // Independent noise jitter, decoupled from LFO modDepth. When set (>= 0),
    // this overrides the modDepth-coupled noise jitter. Mirrors DattorroTank.
    lastNoiseModRawSamples_ = samples;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    // Clamp to the modulation headroom reserved in prepare() (32 samples at base rate)
    float maxAllowed = 32.0f * rateRatio;
    independentNoiseModDepth_ = std::clamp (samples * rateRatio, -1.0f, maxAllowed);
}

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
        {
            structHFState_[t] = 0.0f;
            tanks_[t].currentRMS = 0.0f;
            tanks_[t].peakRMS = 0.0f;
            tanks_[t].terminalDecayActive = false;
        }
    }
}
void QuadTank::setLateGainScale (float scale) { lateGainScale_ = std::max (scale, 0.0f); }

void QuadTank::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::max (hz, 100.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void QuadTank::setAirDampingScale (float scale)
{
    airDampingScale_ = std::max (scale, 0.1f);
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

void QuadTank::setTerminalDecay (float thresholdDB, float factor)
{
    terminalDecayThresholdDB_ = -std::abs (thresholdDB);
    // Accept the full [0.0, 1.0] range to match DattorroTank and the
    // wrapper's pass-through behavior. PluginProcessor already gates the
    // override path on factor>0.001, so an inadvertent factor=0 from the
    // automation layer cannot reach here in normal use; but if the
    // calibrator or a future API caller wants a value below 0.8, it
    // should be honored exactly rather than silently rounded up.
    terminalDecayFactor_ = std::clamp (factor, 0.0f, 1.0f);
    terminalLinearThreshold_ = std::pow (10.0f, -terminalDecayThresholdDB_ * 0.1f);
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
        tank.currentRMS = 0.0f;
        tank.peakRMS = 0.0f;
        tank.terminalDecayActive = false;
    }
    // Re-seed the random-walk LFOs (and noise PRNGs) so each clear gives the
    // same predictable starting state — important for A/B compare and bypass
    // toggling in DAWs. Density-AP jitter LFOs are reseeded too so per-stage
    // wander is also deterministic across resets (the buffer .clear() above
    // zeros sample state but leaves the LFO phase/seed where it left off).
    static constexpr uint32_t kLFOSeeds[kNumTanks]   = { 0x12345678u, 0x87654321u, 0xABCDEF01u, 0x13579BDFu };
    static constexpr uint32_t kNoiseSeeds[kNumTanks]  = { 0xDEADBEEFu, 0xCAFEBABEu, 0xFEEDFACEu, 0xBAADF00Du };
    const float sr = static_cast<float> (sampleRate_);
    for (int t = 0; t < kNumTanks; ++t)
    {
        structHFState_[t] = 0.0f;
        tanks_[t].lfo.prepare (sr, kLFOSeeds[t]);
        tanks_[t].lfo.setRate  (modRateHz_);
        tanks_[t].lfo.setDepth (modDepthSamples_);
        tanks_[t].savedAP1Mod = 0.0f;
        tanks_[t].noiseState = kNoiseSeeds[t];

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

        float gBase = std::pow (10.0f, -3.0f * loopLength / (decayTime_ * sr));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);
        float gLow  = std::clamp (std::pow (gBase, 1.0f / bassMultiply_), 0.001f, 0.9999f);
        // True 3-band: mid band uses midMultiply_ (default 1.0 = natural rate).
        float gMid  = std::clamp (std::pow (gBase, 1.0f / midMultiply_), 0.001f, 0.9999f);
        float gHigh = std::clamp (std::pow (gBase, 1.0f / (trebleMultiply_ * airDampingScale_)), 0.001f, 0.9999f);

        tank.damping.setCoefficients (gLow, gMid, gHigh, lowCrossoverCoeff, highCrossoverCoeff);
    }
}

void QuadTank::updateLFORates()
{
    // 4 asymmetric rates using irrational multipliers — keeps the four
    // random-walk streams from drifting into a synchronised pattern even
    // if their seeds happened to align briefly.
    static constexpr float kRateMultipliers[kNumTanks] = {
        1.0f, 1.1180339887f, 0.8944271910f, 1.2360679775f  // 1, √5/2, 2/√5, (1+√5)/2/φ
    };
    for (int t = 0; t < kNumTanks; ++t)
        tanks_[t].lfo.setRate (modRateHz_ * kRateMultipliers[t]);
}

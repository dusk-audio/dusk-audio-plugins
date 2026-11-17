// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// MultiQDSP.cpp — Digital-character DSP for the Multi-Q core. Transcribed from
// MultiQ::processBlock's Digital branch (per-band routing + saturation + enable
// crossfades + delta-solo). Dynamics, British, and Tube paths are implemented
// below alongside Match EQ and the master-bus utilities.

#include "MultiQDSP.hpp"
#include <cmath>

namespace duskaudio
{

void MultiQDSP::prepare(double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    britishEQ.prepare(currentSampleRate, maxBlockSize > 0 ? maxBlockSize : 512);
    tubeEQ.prepare(currentSampleRate, maxBlockSize > 0 ? maxBlockSize : 512, 2);
    // Per-sample coefficient ramp for the static bands (~1 ms), matching the
    // JUCE build's svfSmoothCoeff = 1 - exp(-1/(0.001*SR)).
    biquadSmoothCoeff = (float)(1.0 - std::exp(-1.0 / (0.001 * currentSampleRate)));
    for (auto& f : svfFilters) { f.setSmoothCoeff(biquadSmoothCoeff); f.reset(); }
    for (auto& f : svfDynGainFilters) { f.setSmoothCoeff(biquadSmoothCoeff); f.reset(); }
    dynamicEQ.prepare(currentSampleRate, 2);
    dynUpdateInterval = std::max(1, (int)std::lround(kDynGainUpdateSeconds * currentSampleRate));
    dynUpdateCountdown = 0;
    matchProc.prepare(currentSampleRate, maxBlockSize > 0 ? maxBlockSize : 512);
    limiter.prepare(currentSampleRate, maxBlockSize > 0 ? maxBlockSize : 512);

    // Auto-gain: 2 s RMS window + 2 s smoothing ramp (matches JUCE
    // autoGainCompensation.reset(sr, 2.0) and rmsWindowSamples = 2*sr).
    rmsWindowSamples = (int)(2.0 * currentSampleRate);
    if (rmsWindowSamples < 1) rmsWindowSamples = 1;
    autoGainComp.reset(currentSampleRate, 2.0);
    autoGainComp.setCurrentAndTargetValue(1.0f);
    inputRmsSum = outputRmsSum = 0.0;
    inputPeakMax = outputPeakMax = 0.0f;
    rmsSampleCount = 0;

    // Per-band saturation oversamplers, and their solo timing references, start
    // transparent (1x).
    for (auto* set : {&satOsL, &satOsR, &satRefL, &satRefR, &deltaDelayL, &deltaDelayR})
        for (auto& os : *set) { os.setFactor(1); os.reset(); }
    prevSatOsFactor = 1;
    refRunning.fill(false);
    refBlend.fill(0.0f);
    refWarm.fill(0);
    bandRan.fill(false);
    bandHold.fill(0);
    bandHistoryFill.fill(0);
    refBlendStep = 1.0f / (float)std::max(1, (int)std::floor(0.003 * currentSampleRate));
    deltaDelayLayout = -1;
    for (int mode = 0; mode < 3; ++mode)
    {
        satOsDelay[(size_t)mode] = measureOversamplerDelay<OversamplerWide>(mode == 2 ? 4 : mode == 1 ? 2 : 1);
        // FourKEQDSP runs its whole core inside one Oversampler round trip.
        britishOsDelay[(size_t)mode] =
            measureOversamplerDelay<Oversampler>(FourKEQDSP::chooseFactor(currentSampleRate, mode));
    }

    hpfFilter.reset(); lpfFilter.reset();
    // Band enable and solo crossfade over 3 ms, as in the original Multi-Q
    // (MultiQ.cpp prepareToPlay). That build resets its smoother at the host
    // rate but steps it per processed sample, so its fade is 3 ms only with
    // Oversampling Off (1.5 ms at 2x, 0.75 ms at 4x); Multi-Q 2 processes at
    // the host rate and fades over 3 ms at every Oversampling setting.
    for (auto& s : bandEnableSmoothed) { s.reset(currentSampleRate, 0.003); s.setCurrentAndTargetValue(1.0f); }
    prevBandPhaseInvertGain.fill(1.0f);
    prevBandPanVal.fill(0.0f);
    prevHpfStages = prevLpfStages = -1;
    firstBlock = true;

    // Read-only observer taps: peak-hold release (~300 ms) + fresh analyzer ring.
    meterDecay = std::exp(-1.0f / (0.3f * (float)currentSampleRate));
    inPeakL.store(0.f, std::memory_order_relaxed); inPeakR.store(0.f, std::memory_order_relaxed);
    outPeakL.store(0.f, std::memory_order_relaxed); outPeakR.store(0.f, std::memory_order_relaxed);
    analyzerRing.reset();
    inputAnalyzerRing.reset();
}

void MultiQDSP::reset()
{
    inPeakL.store(0.f, std::memory_order_relaxed); inPeakR.store(0.f, std::memory_order_relaxed);
    outPeakL.store(0.f, std::memory_order_relaxed); outPeakR.store(0.f, std::memory_order_relaxed);
    analyzerRing.reset();
    inputAnalyzerRing.reset();
    for (auto& f : svfFilters) f.reset();
    for (auto& f : svfDynGainFilters) f.reset();
    dynamicEQ.reset();
    dynUpdateCountdown = 0;
    britishEQ.reset();
    matchProc.reset();
    tubeEQ.reset();
    limiter.requestReset();
    for (auto* set : {&satOsL, &satOsR, &satRefL, &satRefR, &deltaDelayL, &deltaDelayR})
        for (auto& os : *set) os.reset();
    refRunning.fill(false);
    refBlend.fill(0.0f);
    refWarm.fill(0);
    bandRan.fill(false);
    bandHold.fill(0);
    bandHistoryFill.fill(0);
    deltaDelayLayout = -1;
    autoGainComp.setCurrentAndTargetValue(1.0f);
    inputRmsSum = outputRmsSum = 0.0;
    inputPeakMax = outputPeakMax = 0.0f;
    rmsSampleCount = 0;
    hpfFilter.reset(); lpfFilter.reset();
    prevBandPhaseInvertGain.fill(1.0f);
    prevBandPanVal.fill(0.0f);
    firstBlock = true;
}

// Set the dyn-gain SVF target for a band from the current dynamic gain (dB).
// Ported from MultiQ::updateDynGainFilter — the SVF shape follows the band's
// static shape (peaking/shelf/tilt); notch/BP/HP shapes carry no gain → identity.
void MultiQDSP::updateDynGainFilter(int band, float dynGainDb, const Params& p)
{
    if (band < 1 || band > 6) return;
    SVFCoeffs svfC;
    if (std::abs(dynGainDb) < 0.01f)
    {
        svfC.setIdentity();
        svfDynGainFilters[(size_t)(band - 1)].setTarget(svfC);
        return;
    }
    const double sr = currentSampleRate;
    float freq = p.bandFreq[(size_t)band];
    float baseQ = p.bandQ[(size_t)band];
    float staticGain = p.bandGain[(size_t)band];
    float q = getQCoupledValue(baseQ, staticGain, (QCoupleMode)p.qCoupleMode);

    if (band == 1)
    {
        int shape = p.bandShape[1];
        if (shape == 1)      svfdes::computePeaking(svfC, sr, freq, dynGainDb, q);
        else if (shape == 2) svfC.setIdentity();                       // HP: no dynamic gain
        else                 svfdes::computeLowShelf(svfC, sr, freq, dynGainDb, q);
    }
    else if (band == 6)
    {
        int shape = p.bandShape[6];
        if (shape == 1)      svfdes::computePeaking(svfC, sr, freq, dynGainDb, q);
        else if (shape == 2) svfC.setIdentity();                       // LP: no dynamic gain
        else                 svfdes::computeHighShelf(svfC, sr, freq, dynGainDb, q);
    }
    else
    {
        int shape = p.bandShape[(size_t)band];
        if (shape == 1 || shape == 2) svfC.setIdentity();              // notch/BP: no gain
        else if (shape == 3)          svfdes::computeTiltShelf(svfC, sr, freq, dynGainDb);
        else                          svfdes::computePeaking(svfC, sr, freq, dynGainDb, q);
    }
    svfDynGainFilters[(size_t)(band - 1)].setTarget(svfC);
}

std::array<bool, MultiQDSP::NUM_BANDS> MultiQDSP::enabledBands(const Params& p)
{
    std::array<bool, NUM_BANDS> enabled{};
    if ((EQType)p.eqType == EQType::Match)
        return enabled;
    for (int i = 0; i < NUM_BANDS; ++i)
        enabled[(size_t)i] = p.bandEnabled[(size_t)i];
    return enabled;
}

std::array<bool, MultiQDSP::NUM_BANDS> MultiQDSP::runningBands(const Params& p)
{
    std::array<bool, NUM_BANDS> running = enabledBands(p);
    const int solo = p.soloBand;
    if (solo >= 0 && solo < NUM_BANDS && !p.deltaSolo)
    {
        const bool soloIsParametric = (solo >= 1 && solo <= 6);
        for (int i = 0; i < NUM_BANDS; ++i)
            if (i != solo && !(soloIsParametric && (i == 0 || i == 7)))
                running[(size_t)i] = false;
    }
    return running;
}

int MultiQDSP::getLatencySamples(const Params& p) const noexcept
{
    const size_t os = (size_t)(p.oversampling == 2 ? 2 : p.oversampling == 1 ? 1 : 0);
    const int limiterLatency = p.limiterEnabled ? limiter.getMaxLookaheadSamples() : 0;
    const auto eqType = (EQType)p.eqType;
    if (eqType == EQType::British)
        return limiterLatency + (int)std::lround(britishOsDelay[os]);
    if (eqType == EQType::Tube)
        return limiterLatency + (int)std::lround(tubeEQ.oversamplingDelay(p.oversampling));
    if (eqType == EQType::Match)
        return limiterLatency + matchProc.getReportedLatencySamples();
    if (eqType != EQType::Digital)
        return limiterLatency;   // process() passes such a block through undelayed

    // The saturating bands' oversamplers sit in series: sum their delays before
    // rounding (two bands at 2x render 125 samples, not 2 x 63). Each delays
    // both channels whatever its routing. Solo is left out: process() keeps a
    // silenced band's delay (see satRefL).
    const auto enabled = enabledBands(p);
    int saturating = 0;
    for (int band = 1; band < 7; ++band)
        if (enabled[(size_t)band] && p.bandSatType[(size_t)band] > 0)
            ++saturating;
    return limiterLatency + (int)std::lround(saturating * satOsDelay[os]);
}

void MultiQDSP::delayAsSaturation(int routing, OversamplerWide& osL, OversamplerWide& osR,
                                  float& l, float& r) noexcept
{
    // A saturating band delays both channels whatever it saturates, so this
    // does too: left and right through osL and osR, or for a band routed to Mid
    // or Side, Mid through osL and Side through osR, as the band itself does.
    const auto identity = [](float s) noexcept { return s; };
    if (routing == 3 || routing == 4)
    {
        float mid = (l + r) * 0.5f, side = (l - r) * 0.5f;
        mid = osL.processSample(mid, identity);
        side = osR.processSample(side, identity);
        l = mid + side;
        r = mid - side;
    }
    else
    {
        l = osL.processSample(l, identity);
        r = osR.processSample(r, identity);
    }
}

void MultiQDSP::startReference(int band, int routing) noexcept
{
    auto& osL = satRefL[(size_t)(band - 1)];
    auto& osR = satRefR[(size_t)(band - 1)];
    osL.reset();
    osR.reset();
    // A round trip remembers kRoundTripMemory inputs, so replaying that many
    // (or all the band has kept, if fewer) leaves it where an uninterrupted
    // one would be. Bounded work, no allocation.
    const auto& historyL = bandHistoryL[(size_t)(band - 1)];
    const auto& historyR = bandHistoryR[(size_t)(band - 1)];
    const int replay = std::min(bandHistoryFill[(size_t)(band - 1)], kRoundTripMemory);
    for (int back = replay; back > 0; --back)
    {
        const size_t at = (size_t)((bandHistoryPos - back) & (kBandHistory - 1));
        float l = historyL[at], r = historyR[at];
        delayAsSaturation(routing, osL, osR, l, r);
    }
    refWarm[(size_t)band] = replay;
}

void MultiQDSP::computeTiltShelf(MqBiquadCoeffs& c, double sr, double freq, float gainDB)
{
    // 1st-order tilt shelf, bilinear transform of H(s)=(s+w0*sqrt(A))/(s+w0/sqrt(A)).
    //
    // Nyquist guard, matching every amb:: designer and updateHPF/updateLPF. Without
    // it the prewarp tan(w0*T/2) goes negative once freq passes sr/2, which puts the
    // pole a1/a0 outside the unit circle and the filter diverges.
    //
    // It is unstable at gainDB 0, not merely at high gain, and the coefficients are
    // written unconditionally for every enabled band. Measured with a band left on
    // Tilt Shelf at its 1000 Hz default, gain 0: pole 0.998 at a 2001 Hz host rate,
    // 1.000 at 2000, -5.242 at 1234.567, 3.732 at 1500.
    //
    // The divergence does NOT surface as a NaN. StereoBiquad::processSampleL/R
    // checks the output and, on a non-finite sample, zeroes its state and returns
    // 0.0. So this failed as repeated state resets, heard as dropouts or crackle at
    // degenerate rates, and passed every non-finite output sweep unnoticed.
    freq = amb::clampFreq(freq, sr);
    double w0 = 2.0 * kMultiQPi * freq;
    double T = 1.0 / sr;
    double wc = (2.0 / T) * std::tan(w0 * T / 2.0);
    double A = std::pow(10.0, gainDB / 40.0);
    double sqrtA = std::sqrt(A);
    double twoOverT = 2.0 / T;
    double wcSqrtA = wc * sqrtA;
    double wcOverSqrtA = wc / sqrtA;
    double b0 = twoOverT + wcSqrtA;
    double b1 = wcSqrtA - twoOverT;
    double a0 = twoOverT + wcOverSqrtA;
    double a1 = wcOverSqrtA - twoOverT;
    c.coeffs[0] = float(b0 / a0);
    c.coeffs[1] = float(b1 / a0);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float(a1 / a0);
    c.coeffs[5] = 0.0f;
}

void MultiQDSP::computeBandCoeffs(int band, const Params& p, MqBiquadCoeffs& c) const
{
    const double sr = currentSampleRate;
    float gain = p.bandGain[(size_t)band];
    if (p.bandInvert[(size_t)band]) gain = -gain;
    float freq = p.bandFreq[(size_t)band];
    float baseQ = p.bandQ[(size_t)band];
    float q = getQCoupledValue(baseQ, gain, (QCoupleMode)p.qCoupleMode);

    // Gain-controlled shapes at exactly 0 dB are true bypasses. Designing a
    // pole/zero-cancelled biquad still leaves a float recurrence running and
    // can create the same deep-LF numerical floor found in 4K EQ 2. Emit exact
    // identity coefficients instead; notch/band-pass/HP/LP shapes remain
    // active because their response is not controlled by the gain parameter.
    const int shape = p.bandShape[(size_t)band];
    const bool gainControlledShape =
        (band == 1 || band == 6) ? shape != 2 : (shape == 0 || shape == 3);
    if (gainControlledShape && std::abs(gain) <= 1.0e-6f)
    {
        c.setIdentity();
        return;
    }

    if (band == 1)
    {
        if (shape == 1)      amb::computePeaking(c, freq, sr, gain, q);
        else if (shape == 2) amb::computeHighPass(c, freq, sr, q);
        else                 amb::computeLowShelf(c, freq, sr, gain, q);
    }
    else if (band == 6)
    {
        if (shape == 1)      amb::computePeaking(c, freq, sr, gain, q);
        else if (shape == 2) amb::computeLowPass(c, freq, sr, q);
        else                 amb::computeHighShelf(c, freq, sr, gain, q);
    }
    else
    {
        if (shape == 1)      amb::computeNotch(c, freq, sr, q);
        else if (shape == 2) amb::computeBandPass(c, freq, sr, q);
        else if (shape == 3) computeTiltShelf(c, sr, freq, gain);
        else                 amb::computePeaking(c, freq, sr, gain, q);
    }
}

void MultiQDSP::updateHPF(const Params& p)
{
    const double sampleRate = currentSampleRate;
    float freq = p.bandFreq[0];
    float userQ = p.bandQ[0];
    auto slope = (FilterSlope)p.bandSlope[0];
    double clampedFreq = std::clamp((double)freq, 20.0, sampleRate * 0.45);

    int stages = 1; bool firstStageFirstOrder = false; int secondOrderStages = 0;
    switch (slope)
    {
        case FilterSlope::Slope6dB:  stages = 1; firstStageFirstOrder = true;  secondOrderStages = 0; break;
        case FilterSlope::Slope12dB: stages = 1; secondOrderStages = 1; break;
        case FilterSlope::Slope18dB: stages = 2; firstStageFirstOrder = true;  secondOrderStages = 1; break;
        case FilterSlope::Slope24dB: stages = 2; secondOrderStages = 2; break;
        case FilterSlope::Slope36dB: stages = 3; secondOrderStages = 3; break;
        case FilterSlope::Slope48dB: stages = 4; secondOrderStages = 4; break;
        case FilterSlope::Slope72dB: stages = 6; secondOrderStages = 6; break;
        case FilterSlope::Slope96dB: stages = 8; secondOrderStages = 8; break;
    }
    if (stages != prevHpfStages) { hpfFilter.reset(); prevHpfStages = stages; }

    int soStageIdx = 0;
    for (int stage = 0; stage < stages; ++stage)
    {
        MqBiquadCoeffs c;
        if (firstStageFirstOrder && stage == 0)
            amb::computeFirstOrderHighPass(c, clampedFreq, sampleRate);
        else
        {
            float stageQ = ButterworthQ::getStageQ(secondOrderStages, soStageIdx, userQ);
            amb::computeHighPass(c, clampedFreq, sampleRate, stageQ);
            ++soStageIdx;
        }
        hpfFilter.co[(size_t)stage] = c;
    }
    hpfFilter.activeStages = stages;
}

void MultiQDSP::updateLPF(const Params& p)
{
    const double sampleRate = currentSampleRate;
    float freq = p.bandFreq[7];
    float userQ = p.bandQ[7];
    auto slope = (FilterSlope)p.bandSlope[7];
    double clampedFreq = std::clamp((double)freq, 20.0, sampleRate * 0.45);

    int stages = 1; bool firstStageFirstOrder = false; int secondOrderStages = 0;
    switch (slope)
    {
        case FilterSlope::Slope6dB:  stages = 1; firstStageFirstOrder = true;  secondOrderStages = 0; break;
        case FilterSlope::Slope12dB: stages = 1; secondOrderStages = 1; break;
        case FilterSlope::Slope18dB: stages = 2; firstStageFirstOrder = true;  secondOrderStages = 1; break;
        case FilterSlope::Slope24dB: stages = 2; secondOrderStages = 2; break;
        case FilterSlope::Slope36dB: stages = 3; secondOrderStages = 3; break;
        case FilterSlope::Slope48dB: stages = 4; secondOrderStages = 4; break;
        case FilterSlope::Slope72dB: stages = 6; secondOrderStages = 6; break;
        case FilterSlope::Slope96dB: stages = 8; secondOrderStages = 8; break;
    }
    if (stages != prevLpfStages) { lpfFilter.reset(); prevLpfStages = stages; }

    int soStageIdx = 0;
    for (int stage = 0; stage < stages; ++stage)
    {
        MqBiquadCoeffs c;
        if (firstStageFirstOrder && stage == 0)
            amb::computeFirstOrderLowPass(c, clampedFreq, sampleRate);
        else
        {
            float stageQ = ButterworthQ::getStageQ(secondOrderStages, soStageIdx, userQ);
            amb::computeLowPass(c, clampedFreq, sampleRate, stageQ);
            ++soStageIdx;
        }
        lpfFilter.co[(size_t)stage] = c;
    }
    lpfFilter.activeStages = stages;
}

// Read-only observer taps -----------------------------------------------------
// Peak-hold with per-block decay, mirroring FourKEQDSP's metering store. These
// only read the audio and store into relaxed atomics / push into the ring — they
// never modify the processed samples, so Digital audio stays bit-identical.
void MultiQDSP::captureInputPeak(const float* const* inputs, int numChannels, int numSamples) noexcept
{
    const float dk = std::pow(meterDecay, (float)numSamples);
    float pkL = 0.f, pkR = 0.f;
    for (int i = 0; i < numSamples; ++i) { float a = std::fabs(inputs[0][i]); if (a > pkL) pkL = a; }
    if (numChannels > 1)
        for (int i = 0; i < numSamples; ++i) { float a = std::fabs(inputs[1][i]); if (a > pkR) pkR = a; }
    else
        pkR = pkL;
    const float dL = inPeakL.load(std::memory_order_relaxed) * dk;
    const float dR = inPeakR.load(std::memory_order_relaxed) * dk;
    inPeakL.store(pkL > dL ? pkL : dL, std::memory_order_relaxed);
    inPeakR.store(pkR > dR ? pkR : dR, std::memory_order_relaxed);

    // PRE-EQ analyzer ring: push the RAW input (mono downmix) for the UI's pre/post
    // analyzer. Read-only/additive — mirrors publishOutputTaps' ring push but from
    // the input; it never writes the processed samples, so audio stays bit-identical.
    if (numChannels > 1)
        for (int i = 0; i < numSamples; ++i) inputAnalyzerRing.push(0.5f * (inputs[0][i] + inputs[1][i]));
    else
        for (int i = 0; i < numSamples; ++i) inputAnalyzerRing.push(inputs[0][i]);
}

void MultiQDSP::publishOutputTaps(const float* L, const float* R, bool isStereo, int numSamples) noexcept
{
    const float dk = std::pow(meterDecay, (float)numSamples);
    float pkL = 0.f, pkR = 0.f;
    for (int i = 0; i < numSamples; ++i) { float a = std::fabs(L[i]); if (a > pkL) pkL = a; }
    if (isStereo && R != nullptr)
        for (int i = 0; i < numSamples; ++i) { float a = std::fabs(R[i]); if (a > pkR) pkR = a; }
    else
        pkR = pkL;
    const float dL = outPeakL.load(std::memory_order_relaxed) * dk;
    const float dR = outPeakR.load(std::memory_order_relaxed) * dk;
    outPeakL.store(pkL > dL ? pkL : dL, std::memory_order_relaxed);
    outPeakR.store(pkR > dR ? pkR : dR, std::memory_order_relaxed);

    // Mono downmix of the final output into the lock-free analyzer ring.
    if (isStereo && R != nullptr)
        for (int i = 0; i < numSamples; ++i) analyzerRing.push(0.5f * (L[i] + R[i]));
    else
        for (int i = 0; i < numSamples; ++i) analyzerRing.push(L[i]);
}

// Auto-gain input window (raw input, mono downmix). Matches MultiQ.cpp:856-873.
void MultiQDSP::accumulateInputRms(const float* const* inputs, int numChannels, int numSamples) noexcept
{
    const float* L = inputs[0];
    const float* R = numChannels > 1 ? inputs[1] : inputs[0];
    for (int i = 0; i < numSamples; ++i)
    {
        float s = 0.5f * (L[i] + R[i]);
        inputRmsSum += (double)s * (double)s;
        float a = std::fabs(s);
        if (a > inputPeakMax) inputPeakMax = a;
    }
}

// Auto-gain output window + windowed make-up target + apply (MultiQ.cpp:1526-1599).
// Called on the post-master output so it loudness-matches the raw input, exactly
// like the JUCE build (auto-gain sits after master gain, before the limiter).
void MultiQDSP::applyAutoGain(float* L, float* R, bool isStereo, int numSamples) noexcept
{
    const float* Rr = (isStereo && R != nullptr) ? R : L;
    for (int i = 0; i < numSamples; ++i)
    {
        float s = 0.5f * (L[i] + Rr[i]);
        outputRmsSum += (double)s * (double)s;
        float a = std::fabs(s);
        if (a > outputPeakMax) outputPeakMax = a;
    }
    rmsSampleCount += numSamples;

    if (rmsSampleCount >= rmsWindowSamples)
    {
        float inputRms  = std::sqrt((float)(inputRmsSum  / (double)rmsSampleCount));
        float outputRms = std::sqrt((float)(outputRmsSum / (double)rmsSampleCount));
        if (outputRms > 1e-6f && inputRms > 1e-6f)
        {
            float targetGain = inputRms / outputRms;
            // Peak-safe cap: never push output peaks above the input peak level.
            if (outputPeakMax > 1e-6f && inputPeakMax > 1e-6f)
                targetGain = std::min(targetGain, inputPeakMax / outputPeakMax);
            targetGain = std::clamp(targetGain, 0.5f, 2.0f); // +/-6 dB max
            // The dead zone suppresses tiny corrections, but must also release
            // a preceding correction when the EQ returns to unity.
            if (std::abs(targetGain - 1.0f) <= 0.01f) targetGain = 1.0f;
            autoGainComp.setTargetValue(targetGain);
        }
        inputRmsSum = outputRmsSum = 0.0;
        inputPeakMax = outputPeakMax = 0.0f;
        rmsSampleCount = 0;
    }

    if (autoGainComp.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float g = autoGainComp.getNextValue();
            L[i] *= g;
            if (isStereo && R != nullptr) R[i] *= g;
        }
    }
    else
    {
        float g = autoGainComp.getCurrentValue();
        if (std::abs(g - 1.0f) > 0.001f)
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] *= g;
                if (isStereo && R != nullptr) R[i] *= g;
            }
    }
}

void MultiQDSP::process(const float* const* inputs, float* const* outputs,
                        int numChannels, int numSamples, const Params& p)
{
    if (numSamples <= 0 || numChannels <= 0) return;

    // Input peak tap: read BEFORE processing (outputs may alias inputs in place).
    captureInputPeak(inputs, numChannels, numSamples);

    // Match EQ: apply any pending UI learn/clear requests, then feed the learning
    // spectrum from the RAW input (mono downmix) before any processing overwrites
    // it — mirrors MultiQ.cpp:511-548. Runs regardless of the active character so
    // the UI can capture a "current"/"reference" from whatever is on the input.
    matchProc.audioThreadSync();
    if (matchProc.isLearning())
        matchProc.feedLearningStereo(inputs[0], numChannels > 1 ? inputs[1] : nullptr, numSamples);

    // Auto-gain input loudness window (raw input). When disabled, hold make-up at
    // unity and clear the accumulators (matches MultiQ.cpp:1601-1610).
    if (p.autoGainEnabled)
        accumulateInputRms(inputs, numChannels, numSamples);
    else
    {
        autoGainComp.setCurrentAndTargetValue(1.0f);
        inputRmsSum = outputRmsSum = 0.0;
        inputPeakMax = outputPeakMax = 0.0f;
        rmsSampleCount = 0;
    }

    const bool isStereo = numChannels > 1;
    float* procL = outputs[0];
    float* procR = isStereo ? outputs[1] : nullptr;

    const auto eqType = (EQType)p.eqType;

    // The British and Tube characters do not run the Digital bands, so their
    // solo references, input history and delta-solo delay stages fall out of
    // date: they start afresh when the bands run again.
    if (eqType == EQType::British || eqType == EQType::Tube)
    {
        refRunning.fill(false);
        refBlend.fill(0.0f);
        refWarm.fill(0);
        bandRan.fill(false);
        bandHold.fill(0);
        bandHistoryFill.fill(0);
        deltaDelayLayout = -1;
    }

    // British character: route through the upgraded FourKEQDSP core (its own
    // parallel-summing EQ + console saturation + oversampling + M/S). It reads
    // inputs and writes outputs directly (in-place safe), so drive it before the
    // Digital working-buffer copy below and return via master gain.
    if (eqType == EQType::British)
    {
        const auto& b = p.british;
        britishEQ.setHpfFreq(b.hpfFreq);   britishEQ.setHpfEnabled(b.hpfEnabled);
        britishEQ.setLpfFreq(b.lpfFreq);   britishEQ.setLpfEnabled(b.lpfEnabled);
        britishEQ.setLfGain(b.lfGain);     britishEQ.setLfFreq(b.lfFreq);   britishEQ.setLfBell(b.lfBell);
        britishEQ.setLmGain(b.lmGain);     britishEQ.setLmFreq(b.lmFreq);   britishEQ.setLmQ(b.lmQ);
        britishEQ.setHmGain(b.hmGain);     britishEQ.setHmFreq(b.hmFreq);   britishEQ.setHmQ(b.hmQ);
        britishEQ.setHfGain(b.hfGain);     britishEQ.setHfFreq(b.hfFreq);   britishEQ.setHfBell(b.hfBell);
        britishEQ.setEqType(b.blackMode ? 1 : 0);
        britishEQ.setSaturation(b.saturation);
        britishEQ.setInputGainDb(b.inputGain);
        britishEQ.setOutputGainDb(b.outputGain);
        britishEQ.setOversampling(p.oversampling);
        // British/Tube characters route through stereo-coupled cores that expose only
        // Mid/Side processing (no per-channel Left/Right split like the Digital path's
        // effectiveRouting). Processing-mode Left(1)/Right(2) therefore fall back to
        // full-stereo here, matching the JUCE build's British/Tube behaviour.
        britishEQ.setMsMode(p.processingMode == 3 || p.processingMode == 4);
        britishEQ.setBypass(false);
        britishEQ.setAutoGain(false);
        britishEQ.processBlock(inputs, outputs, numChannels, numSamples);
    }
    // Tube character: route through the framework-free MultiQTube port. It writes
    // the buffers in place, so copy input→output first, optionally M/S-encode, run
    // the tube (which self-oversamples its nonlinear stages), then M/S-decode.
    else if (eqType == EQType::Tube)
    {
        for (int i = 0; i < numSamples; ++i) procL[i] = inputs[0][i];
        if (isStereo) for (int i = 0; i < numSamples; ++i) procR[i] = inputs[1][i];

        // Global M/S encode (Mid=3 / Side=4). Mirrors MultiQ.cpp:942-946 — the tube
        // then processes the mid and side components, and we decode afterwards. Like
        // British (above), Left/Right processing modes fall back to full-stereo — the
        // tube core is stereo-coupled and exposes no per-channel L/R routing.
        const bool tubeMS = isStereo && (p.processingMode == 3 || p.processingMode == 4);
        if (tubeMS)
            for (int i = 0; i < numSamples; ++i)
            {
                float l = procL[i], r = procR[i];
                procL[i] = (l + r) * 0.5f;  // mid
                procR[i] = (l - r) * 0.5f;  // side
            }

        const auto& t = p.tube;
        MultiQTube::Parameters tp;
        tp.lfBoostGain = t.lfBoostGain;   tp.lfBoostFreq = t.lfBoostFreq;
        tp.lfAttenGain = t.lfAttenGain;
        tp.hfBoostGain = t.hfBoostGain;   tp.hfBoostFreq = t.hfBoostFreq;
        tp.hfBoostBandwidth = t.hfBoostBandwidth;
        tp.hfAttenGain = t.hfAttenGain;   tp.hfAttenFreq = t.hfAttenFreq;
        tp.midEnabled = t.midEnabled;
        tp.midLowFreq = t.midLowFreq;     tp.midLowPeak = t.midLowPeak;
        tp.midDipFreq = t.midDipFreq;     tp.midDip = t.midDip;
        tp.midHighFreq = t.midHighFreq;   tp.midHighPeak = t.midHighPeak;
        tp.inputGain = t.inputGain;       tp.outputGain = t.outputGain;
        tp.tubeDrive = t.tubeDrive;       tp.bypass = false;
        tubeEQ.setOversampling(p.oversampling); // 2x/4x on the nonlinear stages
        tubeEQ.setParameters(tp);
        tubeEQ.process(outputs, numChannels, numSamples);

        if (tubeMS)
            for (int i = 0; i < numSamples; ++i)
            {
                float m = procL[i], s = procR[i];
                procL[i] = m + s;   // left
                procR[i] = m - s;   // right
            }
    }
    else
    {
    // in-place-safe copy of input into the output working buffers
    for (int i = 0; i < numSamples; ++i) procL[i] = inputs[0][i];
    if (isStereo) for (int i = 0; i < numSamples; ++i) procR[i] = inputs[1][i];

    const bool digitalPath = (eqType == EQType::Digital || eqType == EQType::Match);

    // effective per-band routing (0=Stereo,1=Left,2=Right,3=Mid,4=Side)
    std::array<int, NUM_BANDS> effectiveRouting{};
    int globalRouting = 0;
    switch ((ProcessingMode)p.processingMode)
    {
        case ProcessingMode::Stereo: globalRouting = 0; break;
        case ProcessingMode::Left:   globalRouting = 1; break;
        case ProcessingMode::Right:  globalRouting = 2; break;
        case ProcessingMode::Mid:    globalRouting = 3; break;
        case ProcessingMode::Side:   globalRouting = 4; break;
    }
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        int perBand = p.bandRouting[(size_t)i];
        effectiveRouting[(size_t)i] = (perBand == 0) ? globalRouting : (perBand - 1);
    }

    if (digitalPath)
    {
        const std::array<bool, NUM_BANDS> bandEnabled = runningBands(p);
        const int currentSolo = p.soloBand;
        const bool deltaSoloActive = (currentSolo >= 0 && currentSolo < NUM_BANDS && p.deltaSolo);
        const bool streamStart = firstBlock;   // first block since prepare() or reset()

        for (int i = 0; i < NUM_BANDS; ++i)
            bandEnableSmoothed[(size_t)i].setTargetValue(bandEnabled[(size_t)i] ? 1.0f : 0.0f);

        // Match mode disables dynamics too.
        std::array<bool, NUM_BANDS> bandDynEnabled{};
        for (int i = 0; i < NUM_BANDS; ++i)
            bandDynEnabled[(size_t)i] = (eqType == EQType::Match) ? false : p.bandDynEnabled[(size_t)i];

        // Push dynamics parameters + detection-filter coeffs for all bands.
        for (int band = 0; band < NUM_BANDS; ++band)
        {
            MultiQDynamics::BandParameters dp;
            dp.enabled   = bandDynEnabled[(size_t)band];
            dp.threshold = p.bandDynThreshold[(size_t)band];
            dp.attack    = p.bandDynAttack[(size_t)band];
            dp.release   = p.bandDynRelease[(size_t)band];
            dp.range     = p.bandDynRange[(size_t)band];
            dp.ratio     = p.bandDynRatio[(size_t)band];
            dynamicEQ.setBandParameters(band, dp);
            dynamicEQ.updateDetectionFilter(band, p.bandFreq[(size_t)band], p.bandQ[(size_t)band]);
        }
        dynamicEQ.setRmsDetection(p.dynDetectionRms);
        // The dyn-gain filter coefficients follow the envelopes inside the sample
        // loop, on the kDynGainUpdateSeconds grid (see dynUpdateCountdown).

        // static band coefficients (block rate; per-sample smoothing in the loop)
        for (int band = 1; band < 7; ++band)
        {
            MqBiquadCoeffs c;
            computeBandCoeffs(band, p, c);
            svfFilters[(size_t)(band - 1)].setCoeffs(c);
        }
        updateHPF(p);
        updateLPF(p);

        // On the first block after prepare, snap coefficient ramps + enable
        // smoothers to their targets so there is no startup transient (a settled
        // JUCE instance is the A/B reference).
        if (firstBlock)
        {
            // prepare()/reset() zeroed the envelopes but not the dynamic-gain
            // filters' targets; aim every band's at the current (0 dB) gain
            // before the snap, dynamics on or off, or a re-activated band would
            // start from its old reduction, at once or when its dynamics are
            // switched on again.
            for (int band = 1; band < 7; ++band)
                updateDynGainFilter(band, dynamicEQ.getCurrentDynamicGain(band), p);
            for (auto& f : svfFilters) f.snapToTarget();
            for (auto& f : svfDynGainFilters) f.snapToTarget();
            for (int i = 0; i < NUM_BANDS; ++i)
                bandEnableSmoothed[(size_t)i].setCurrentAndTargetValue(bandEnabled[(size_t)i] ? 1.0f : 0.0f);
            firstBlock = false;
        }

        // per-band phase-invert + pan ramp targets/increments
        std::array<float, NUM_BANDS> targetPhaseInvertGain{}, targetPanVal{};
        std::array<float, NUM_BANDS> phaseInvertGainInc{}, panValInc{};
        float invNumSamples = (numSamples > 0) ? 1.0f / (float)numSamples : 1.0f;
        for (int b = 0; b < NUM_BANDS; ++b)
        {
            targetPhaseInvertGain[(size_t)b] = p.bandPhaseInvert[(size_t)b] ? -1.0f : 1.0f;
            targetPanVal[(size_t)b] = p.bandPan[(size_t)b];
            phaseInvertGainInc[(size_t)b] = (targetPhaseInvertGain[(size_t)b] - prevBandPhaseInvertGain[(size_t)b]) * invNumSamples;
            panValInc[(size_t)b] = (targetPanVal[(size_t)b] - prevBandPanVal[(size_t)b]) * invNumSamples;
        }

        // per-band saturation params + curve LUT
        using CT = AnalogEmulation::WaveshaperCurves::CurveType;
        auto& waveshaperCurves = AnalogEmulation::getWaveshaperCurves();
        std::array<int, NUM_BANDS> bandSatType{};
        std::array<float, NUM_BANDS> bandSatDrive{};
        std::array<CT, NUM_BANDS> bandSatCurve{};
        for (int band = 1; band < 7; ++band)
        {
            bandSatType[(size_t)band] = p.bandSatType[(size_t)band];
            bandSatDrive[(size_t)band] = p.bandSatDrive[(size_t)band];
            switch (bandSatType[(size_t)band])
            {
                case 1:  bandSatCurve[(size_t)band] = CT::Tape; break;
                case 2:  bandSatCurve[(size_t)band] = CT::Triode; break;
                case 3:  bandSatCurve[(size_t)band] = CT::Console_Bus; break;
                case 4:  bandSatCurve[(size_t)band] = CT::FET; break;
                default: bandSatCurve[(size_t)band] = CT::Linear; break;
            }
        }

        std::array<bool, NUM_BANDS> bandActive{};
        for (int b = 0; b < NUM_BANDS; ++b)
            bandActive[(size_t)b] = bandEnabled[(size_t)b] || bandEnableSmoothed[(size_t)b].isSmoothing();

        bool allStereoRouting = true;
        for (int b = 0; b < NUM_BANDS; ++b)
            if (effectiveRouting[(size_t)b] != 0) { allStereoRouting = false; break; }

        // Per-band saturation oversampling factor (hq_enabled: 0/1/2 → 1x/2x/4x).
        // Reset the OS state when the factor changes so no stale elevated-rate
        // samples leak. At 1x the wrapped waveshaper is a passthrough (bit-exact).
        const int satOsFactor = (p.oversampling == 2) ? 4 : (p.oversampling == 1) ? 2 : 1;
        if (satOsFactor != prevSatOsFactor)
        {
            for (auto* set : {&satOsL, &satOsR, &satRefL, &satRefR, &deltaDelayL, &deltaDelayR})
                for (auto& os : *set) { os.setFactor(satOsFactor); os.reset(); }
            prevSatOsFactor = satOsFactor;
            deltaDelayLayout = -1;
        }

        // Bands whose oversampled saturation is part of the reported latency
        // (enabled without regard to solo): they keep that delay even while a
        // solo silences them, so soloing never moves the output in time.
        //
        // A band's reference runs only while something reads it: a solo
        // silences the band, the band fades, is panned, phase-inverted or
        // delta-soloed, or its delay blends in or out. A saturating band that
        // simply plays, or is switched off, leaves it idle and costs nothing
        // extra. A reference that starts replays the band's input history, so
        // it is exact from its first sample; a band keeps that history only
        // while it saturates.
        auto bandRouting = [&](int band) { return allStereoRouting ? 0 : effectiveRouting[(size_t)band]; };
        const std::array<bool, NUM_BANDS> latencyBands = enabledBands(p);
        std::array<bool, NUM_BANDS> timedBand{}, bandHistoryKept{};
        for (int band = 1; band < 7; ++band)
        {
            const size_t bi = (size_t)band;
            timedBand[bi] = satOsFactor > 1 && latencyBands[bi] && bandSatType[bi] > 0;
            if (streamStart) refBlend[bi] = timedBand[bi] ? 1.0f : 0.0f;   // nothing to blend from yet
            const bool saturating = satOsFactor > 1 && eqType == EQType::Digital && bandSatType[bi] > 0;
            // History while the band's delay counts, and through the fade of
            // a saturating band switched off, whose reference it starts.
            bandHistoryKept[bi] = timedBand[bi] || (saturating && bandActive[bi]);
            if (!bandHistoryKept[bi]) bandHistoryFill[bi - 1] = 0;
            const bool silenced = timedBand[bi] && !bandEnabled[bi];     // on, but a solo silences it
            const bool fading = bandEnableSmoothed[bi].isSmoothing();
            const bool neutral = targetPhaseInvertGain[bi] == 1.0f && prevBandPhaseInvertGain[bi] == 1.0f
                                 && std::abs(targetPanVal[bi]) <= 0.001f && std::abs(prevBandPanVal[bi]) <= 0.001f;
            const bool readsReference = bandEnabled[bi] && (!neutral || (deltaSoloActive && currentSolo == band));
            const bool blending = refBlend[bi] != (timedBand[bi] ? 1.0f : 0.0f);
            const bool needed = (saturating && (silenced || fading || readsReference || blending))
                                || (refRunning[bi] && blending);
            if (needed && !refRunning[bi])
                startReference(band, bandRouting(band));
            refRunning[bi] = needed;
        }
        if (streamStart) bandRan.fill(true);   // nothing to restart from yet: no band holds

        // A band with oversampled saturation that runs again (a solo released,
        // switched on) first runs silently for one round-trip memory, so its
        // oversampler holds none of what it last played. A band can only start
        // to run again at a block boundary, so this is decided here; the
        // countdown and whether each band ran stay in locals while the samples
        // run, and are stored back after them.
        // A band that is off, with no reference running and no history to
        // keep, has nothing to do this block: the samples pass it untouched.
        std::array<int, NUM_BANDS> holdLeft{};
        std::array<bool, NUM_BANDS> ran = bandRan, idle{};
        for (int band = 1; band < 7; ++band)
        {
            const size_t bi = (size_t)band;
            if (!bandActive[bi])
                bandHold[bi] = 0;
            else if (!bandRan[bi] && satOsFactor > 1 && bandSatType[bi] > 0)
                bandHold[bi] = kRoundTripMemory;
            holdLeft[bi] = bandHold[bi];
            idle[bi] = !bandActive[bi] && !refRunning[bi] && !bandHistoryKept[bi];
            if (idle[bi]) ran[bi] = false;
        }

        // Delta solo outputs the soloed band's difference, but the bands after
        // it still delay the full output: one reference round trip per such
        // band, with its routing. The chain restarts when its layout changes.
        // The layout is written in base 16 with no zero digit: the soloed band
        // plus one (1..8), then each stage's routing plus one (1..5), so two
        // different layouts never share a value.
        std::array<int, 6> deltaStageRouting{};
        int deltaStages = 0, layout = -1;
        if (deltaSoloActive)
        {
            layout = currentSolo + 1;
            for (int band = currentSolo + 1; band < 7; ++band)
                if (band >= 1 && timedBand[(size_t)band])
                {
                    deltaStageRouting[(size_t)deltaStages++] = bandRouting(band);
                    layout = layout * 16 + 1 + bandRouting(band);
                }
        }
        if (layout != deltaDelayLayout)
        {
            for (auto* set : {&deltaDelayL, &deltaDelayR})
                for (auto& os : *set) os.reset();
            deltaDelayLayout = layout;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float sampleL = procL[i];
            float sampleR = isStereo ? procR[i] : sampleL;

            // Each dynamic band's gain filter takes the envelope's current gain
            // every kDynGainUpdateSeconds. The grid runs across process() calls,
            // so the gain does not depend on how the host splits the audio.
            if (--dynUpdateCountdown <= 0)
            {
                dynUpdateCountdown = dynUpdateInterval;
                for (int band = 1; band < 7; ++band)
                    if (bandDynEnabled[(size_t)band])
                        updateDynGainFilter(band, dynamicEQ.getCurrentDynamicGain(band), p);
            }

            float deltaBeforeL = 0.0f, deltaBeforeR = 0.0f;
            float deltaAfterL = 0.0f, deltaAfterR = 0.0f;

            auto applyFilterWithRouting = [&](auto& filter, int routing) {
                filter.stepSmoothing();
                switch (routing)
                {
                    case 0: sampleL = filter.processSampleL(sampleL); sampleR = filter.processSampleR(sampleR); break;
                    case 1: sampleL = filter.processSampleL(sampleL); break;
                    case 2: sampleR = filter.processSampleR(sampleR); break;
                    case 3: { float mid=(sampleL+sampleR)*0.5f, side=(sampleL-sampleR)*0.5f; mid=filter.processSampleL(mid); sampleL=mid+side; sampleR=mid-side; break; }
                    case 4: { float mid=(sampleL+sampleR)*0.5f, side=(sampleL-sampleR)*0.5f; side=filter.processSampleR(side); sampleL=mid+side; sampleR=mid-side; break; }
                }
            };
            auto applyFilterStereo = [&](auto& filter) {
                filter.stepSmoothing();
                sampleL = filter.processSampleL(sampleL);
                sampleR = filter.processSampleR(sampleR);
            };

            // enableGain: the band's enable/solo crossfade, this sample.
            // prevL/prevR: the band's reference (below), so the band's effect,
            // its phase/pan and its crossfade all line up in time. A faded-out
            // band leaves exactly that signal.
            auto applyBandWithSmoothing = [&](int bandIdx, float enableGain, float prevL, float prevR,
                                              const auto& applyFn) {
                if (enableGain < 0.001f) { sampleL = prevL; sampleR = prevR; return; }

                applyFn();

                {
                    float phaseGain = prevBandPhaseInvertGain[(size_t)bandIdx] + phaseInvertGainInc[(size_t)bandIdx] * (float)i;
                    float deltaL = sampleL - prevL;
                    float deltaR = sampleR - prevR;
                    sampleL = prevL + phaseGain * deltaL;
                    sampleR = prevR + phaseGain * deltaR;
                }
                {
                    float pan = prevBandPanVal[(size_t)bandIdx] + panValInc[(size_t)bandIdx] * (float)i;
                    if (std::abs(pan) > 0.001f)
                    {
                        float deltaL = sampleL - prevL;
                        float deltaR = sampleR - prevR;
                        float leftGain = std::min(1.0f, 1.0f - pan);
                        float rightGain = std::min(1.0f, 1.0f + pan);
                        sampleL = prevL + deltaL * leftGain;
                        sampleR = prevR + deltaR * rightGain;
                    }
                }
                if (enableGain < 0.999f)
                {
                    sampleL = prevL + enableGain * (sampleL - prevL);
                    sampleR = prevR + enableGain * (sampleR - prevR);
                }
            };

            // Band 1: HPF
            if (bandActive[0])
            {
                if (deltaSoloActive && currentSolo == 0) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }
                applyBandWithSmoothing(0, bandEnableSmoothed[0].getNextValue(), sampleL, sampleR, [&]() {
                    if (allStereoRouting) applyFilterStereo(hpfFilter);
                    else                  applyFilterWithRouting(hpfFilter, effectiveRouting[0]);
                });
                if (deltaSoloActive && currentSolo == 0) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
            }

            // Bands 2-7
            for (int band = 1; band < 7; ++band)
            {
                const size_t bi = (size_t)band;
                if (idle[bi]) continue;
                if (bandHistoryKept[bi])   // for startReference
                {
                    bandHistoryL[bi - 1][(size_t)bandHistoryPos] = sampleL;
                    bandHistoryR[bi - 1][(size_t)bandHistoryPos] = sampleR;
                    if (bandHistoryFill[bi - 1] < kBandHistory) ++bandHistoryFill[bi - 1];
                }

                const bool holding = holdLeft[bi] > 0;
                const float enableGain = (bandActive[bi] && !holding) ? bandEnableSmoothed[bi].getNextValue() : 0.0f;
                const bool runs = holding || enableGain >= 0.001f;

                // The band's reference: its input, delayed as its saturation
                // delays it when that delay counts toward the latency. While the
                // band is silent (solo, faded out, holding) it takes the band's
                // place. An idle reference is the plain input: the band then
                // plays at full level, unpanned and not inverted, and does not
                // read it.
                float refL = sampleL, refR = sampleR;
                if (refRunning[bi])
                {
                    float delayedL = sampleL, delayedR = sampleR;
                    delayAsSaturation(bandRouting(band), satRefL[bi - 1], satRefR[bi - 1], delayedL, delayedR);
                    float& blend = refBlend[bi];
                    if (refWarm[bi] < kRoundTripMemory)
                        ++refWarm[bi];
                    else
                        blend = timedBand[bi] ? std::min(1.0f, blend + refBlendStep) : std::max(0.0f, blend - refBlendStep);
                    if (blend >= 1.0f)     { refL = delayedL; refR = delayedR; }
                    else if (blend > 0.0f) { refL += blend * (delayedL - refL); refR += blend * (delayedR - refR); }
                }
                ran[bi] = runs;
                if (!runs) { sampleL = refL; sampleR = refR; continue; }
                if (!holding && deltaSoloActive && currentSolo == band) { deltaBeforeL = refL; deltaBeforeR = refR; }

                // While the band holds it runs only to fill its oversampler,
                // and its reference stays in its place (below).
                applyBandWithSmoothing(band, holding ? 1.0f : enableGain, refL, refR, [&]() {
                    auto& filter = svfFilters[(size_t)(band - 1)];
                    if (allStereoRouting)
                    {
                        if (bandDynEnabled[(size_t)band])
                        {
                            // Linked: the louder channel's envelope sets one gain for both.
                            dynamicEQ.processBandLinked(band, sampleL, sampleR);
                            applyFilterStereo(filter);
                            applyFilterStereo(svfDynGainFilters[(size_t)(band - 1)]);
                        }
                        else
                        {
                            applyFilterStereo(filter);
                        }
                        int satType = bandSatType[(size_t)band];
                        if (satType > 0)
                        {
                            float drive = bandSatDrive[(size_t)band];
                            CT curve = bandSatCurve[(size_t)band];
                            // Oversampled memoryless waveshaper (hq_enabled). At 1x
                            // this is a straight passthrough → bit-exact vs base.
                            auto ws = [&](float s) noexcept { return waveshaperCurves.processWithDrive(s, curve, drive); };
                            sampleL = satOsL[(size_t)(band - 1)].processSample(sampleL, ws);
                            sampleR = satOsR[(size_t)(band - 1)].processSample(sampleR, ws);
                        }
                    }
                    else
                    {
                        int routing = effectiveRouting[(size_t)band];
                        if (bandDynEnabled[(size_t)band])
                        {
                            // Detect what the band processes: one channel, Mid,
                            // Side, or both channels linked (the louder one).
                            float detL = sampleL, detR = sampleR;
                            if (routing == 3 || routing == 4)
                            {
                                float mid = (sampleL + sampleR) * 0.5f;
                                float side = (sampleL - sampleR) * 0.5f;
                                if (routing == 3) { detL = mid; detR = mid; }
                                else              { detL = side; detR = side; }
                            }
                            else if (routing == 1) { detR = 0.0f; }
                            else if (routing == 2) { detL = 0.0f; }
                            dynamicEQ.processBandLinked(band, detL, detR);
                            applyFilterWithRouting(filter, routing);
                            applyFilterWithRouting(svfDynGainFilters[(size_t)(band - 1)], routing);
                        }
                        else
                        {
                            applyFilterWithRouting(filter, routing);
                        }
                        int satType = bandSatType[(size_t)band];
                        if (satType > 0)
                        {
                            CT curve = bandSatCurve[(size_t)band];
                            float drive = bandSatDrive[(size_t)band];
                            auto& osL = satOsL[(size_t)(band - 1)];
                            auto& osR = satOsR[(size_t)(band - 1)];
                            auto ws = [&](float s) noexcept { return waveshaperCurves.processWithDrive(s, curve, drive); };
                            // What the band does not saturate still takes the same
                            // round trip (identity), so both channels carry the
                            // band's delay, as the latency reports, and stay together.
                            const auto identity = [](float s) noexcept { return s; };
                            switch (routing)
                            {
                                case 0: sampleL = osL.processSample(sampleL, ws);
                                        sampleR = osR.processSample(sampleR, ws); break;
                                case 1: sampleL = osL.processSample(sampleL, ws);
                                        sampleR = osR.processSample(sampleR, identity); break;
                                case 2: sampleL = osL.processSample(sampleL, identity);
                                        sampleR = osR.processSample(sampleR, ws); break;
                                case 3: { float mid=(sampleL+sampleR)*0.5f, side=(sampleL-sampleR)*0.5f; mid=osL.processSample(mid, ws); side=osR.processSample(side, identity); sampleL=mid+side; sampleR=mid-side; break; }
                                case 4: { float mid=(sampleL+sampleR)*0.5f, side=(sampleL-sampleR)*0.5f; mid=osL.processSample(mid, identity); side=osR.processSample(side, ws); sampleL=mid+side; sampleR=mid-side; break; }
                            }
                        }
                    }
                });
                if (holding)
                {
                    --holdLeft[bi];
                    sampleL = refL;
                    sampleR = refR;
                    continue;
                }
                if (deltaSoloActive && currentSolo == band) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
            }
            bandHistoryPos = (bandHistoryPos + 1) & (kBandHistory - 1);

            // Band 8: LPF
            if (bandActive[7])
            {
                if (deltaSoloActive && currentSolo == 7) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }
                applyBandWithSmoothing(7, bandEnableSmoothed[7].getNextValue(), sampleL, sampleR, [&]() {
                    if (allStereoRouting) applyFilterStereo(lpfFilter);
                    else                  applyFilterWithRouting(lpfFilter, effectiveRouting[7]);
                });
                if (deltaSoloActive && currentSolo == 7) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
            }

            if (deltaSoloActive)
            {
                sampleL = deltaAfterL - deltaBeforeL;
                sampleR = deltaAfterR - deltaBeforeR;
                for (int s = 0; s < deltaStages; ++s)
                    delayAsSaturation(deltaStageRouting[(size_t)s], deltaDelayL[(size_t)s], deltaDelayR[(size_t)s],
                                      sampleL, sampleR);
            }

            if (!safeIsFinite(sampleL)) sampleL = 0.0f;
            if (!safeIsFinite(sampleR)) sampleR = 0.0f;

            procL[i] = sampleL;
            if (isStereo) procR[i] = sampleR;
        }
        bandHold = holdLeft;
        bandRan = ran;

        for (int b = 0; b < NUM_BANDS; ++b)
        {
            prevBandPhaseInvertGain[(size_t)b] = targetPhaseInvertGain[(size_t)b];
            prevBandPanVal[(size_t)b] = targetPanVal[(size_t)b];
        }
    }

    // Match EQ correction: convolve the (all-bands-bypassed) signal with the
    // learned correction FIR, after the band stage and before master gain —
    // mirrors MultiQ.cpp:1462-1508. No-op passthrough until a correction is
    // computed; match_apply is baked into the FIR (0% = unit-delta passthrough).
    if (eqType == EQType::Match)
        matchProc.process(procL, procR, isStereo, numSamples);
    } // end Digital/Match branch

    // ---- Common master-bus tail (all characters), ordered as MultiQ.cpp ---------
    // master gain (1511) → NaN sanitize (1514) → auto-gain (1525) → limiter (1612).

    // Master gain (applied to all modes, base rate) — mirrors the JUCE tail.
    float mg = std::pow(10.0f, p.masterGain / 20.0f);
    if (std::abs(p.masterGain) > 0.01f)
    {
        for (int i = 0; i < numSamples; ++i) procL[i] *= mg;
        if (isStereo) for (int i = 0; i < numSamples; ++i) procR[i] *= mg;
    }

    // NaN/Inf sanitization before auto-gain + limiter + metering.
    for (int i = 0; i < numSamples; ++i)
        if (!safeIsFinite(procL[i])) procL[i] = 0.0f;
    if (isStereo)
        for (int i = 0; i < numSamples; ++i)
            if (!safeIsFinite(procR[i])) procR[i] = 0.0f;

    // Auto-gain (post-master, pre-limiter) — loudness-matches the raw input.
    if (p.autoGainEnabled)
        applyAutoGain(procL, procR, isStereo, numSamples);

    // Output limiter (mastering safety brickwall) — all characters.
    limiter.setEnabled(p.limiterEnabled);
    if (p.limiterEnabled)
    {
        limiter.setCeiling(p.limiterCeiling);
        float* limL = procL;
        float* limR = isStereo ? procR : procL;
        limiter.process(limL, limR, numSamples);
    }

    publishOutputTaps(procL, procR, isStereo, numSamples);
}

} // namespace duskaudio

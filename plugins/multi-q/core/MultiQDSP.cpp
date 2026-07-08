// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQDSP.cpp — Digital-character DSP for the Multi-Q core. Transcribed from
// MultiQ::processBlock's Digital branch (per-band routing + saturation + enable
// crossfades + delta-solo). Dynamics/British/Tube deferred to later sub-phases.

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

    // Per-band saturation oversamplers start transparent (1x).
    for (auto& os : satOsL) { os.setFactor(1); os.reset(); }
    for (auto& os : satOsR) { os.setFactor(1); os.reset(); }
    prevSatOsFactor = 1;
    lastDigitalSatLatency = 0;

    hpfFilter.reset(); lpfFilter.reset();
    for (auto& s : bandEnableSmoothed) { s.reset(currentSampleRate, 0.01); s.setCurrentAndTargetValue(1.0f); }
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
    britishEQ.reset();
    matchProc.reset();
    tubeEQ.reset();
    limiter.requestReset();
    for (auto& os : satOsL) os.reset();
    for (auto& os : satOsR) os.reset();
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

void MultiQDSP::computeTiltShelf(MqBiquadCoeffs& c, double sr, double freq, float gainDB)
{
    // 1st-order tilt shelf, bilinear transform of H(s)=(s+w0*sqrt(A))/(s+w0/sqrt(A)).
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

    if (band == 1)
    {
        int shape = p.bandShape[1];
        if (shape == 1)      amb::computePeaking(c, freq, sr, gain, q);
        else if (shape == 2) amb::computeHighPass(c, freq, sr, q);
        else                 amb::computeLowShelf(c, freq, sr, gain, q);
    }
    else if (band == 6)
    {
        int shape = p.bandShape[6];
        if (shape == 1)      amb::computePeaking(c, freq, sr, gain, q);
        else if (shape == 2) amb::computeLowPass(c, freq, sr, q);
        else                 amb::computeHighShelf(c, freq, sr, gain, q);
    }
    else
    {
        int shape = p.bandShape[(size_t)band];
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
            if (std::abs(targetGain - 1.0f) > 0.01f)         // dead zone (~0.09 dB)
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
    lastEqType = p.eqType; // cache for getLatencySamples()
    lastDigitalSatLatency = 0; // recomputed by the Digital branch if it saturates

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
        // then processes the mid and side components, and we decode afterwards.
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
        std::array<bool, NUM_BANDS> bandEnabled{};
        for (int i = 0; i < NUM_BANDS; ++i)
            bandEnabled[(size_t)i] = p.bandEnabled[(size_t)i];

        if (eqType == EQType::Match)
            bandEnabled.fill(false);

        // solo / delta-solo
        int currentSolo = p.soloBand;
        bool deltaSoloActive = (currentSolo >= 0 && currentSolo < NUM_BANDS && p.deltaSolo);
        if (currentSolo >= 0 && currentSolo < NUM_BANDS && !deltaSoloActive)
        {
            bool soloIsParametric = (currentSolo >= 1 && currentSolo <= 6);
            for (int i = 0; i < NUM_BANDS; ++i)
            {
                if (i == currentSolo) continue;
                if (soloIsParametric && (i == 0 || i == 7)) continue;
                bandEnabled[(size_t)i] = false;
            }
        }

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
        // Dyn-gain filter coeffs use the PREVIOUS block's envelope (1-block latency).
        for (int band = 1; band < 7; ++band)
            if (bandDynEnabled[(size_t)band])
                updateDynGainFilter(band, dynamicEQ.getCurrentDynamicGain(band), p);

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
            for (auto& os : satOsL) { os.setFactor(satOsFactor); os.reset(); }
            for (auto& os : satOsR) { os.setFactor(satOsFactor); os.reset(); }
            prevSatOsFactor = satOsFactor;
        }
        // Latency = summed group delay of the actively-saturating bands (serial in
        // the chain). Zero at 1x or when no band saturates, so the linear Digital
        // path stays zero-latency and the validated A/B is untouched.
        if (satOsFactor > 1)
        {
            const int perBandLat = (int)std::lround(satOsL[0].latency());
            for (int band = 1; band < 7; ++band)
                if (bandActive[(size_t)band] && bandSatType[(size_t)band] > 0)
                    lastDigitalSatLatency += perBandLat;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float sampleL = procL[i];
            float sampleR = isStereo ? procR[i] : sampleL;

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

            auto applyBandWithSmoothing = [&](int bandIdx, const auto& applyFn) {
                auto& smooth = bandEnableSmoothed[(size_t)bandIdx];
                float enableGain = smooth.getNextValue();
                if (enableGain < 0.001f) return;

                float prevL = sampleL, prevR = sampleR;
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
                applyBandWithSmoothing(0, [&]() {
                    if (allStereoRouting) applyFilterStereo(hpfFilter);
                    else                  applyFilterWithRouting(hpfFilter, effectiveRouting[0]);
                });
                if (deltaSoloActive && currentSolo == 0) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
            }

            // Bands 2-7
            for (int band = 1; band < 7; ++band)
            {
                if (!bandActive[(size_t)band]) continue;
                if (deltaSoloActive && currentSolo == band) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }

                applyBandWithSmoothing(band, [&]() {
                    auto& filter = svfFilters[(size_t)(band - 1)];
                    if (allStereoRouting)
                    {
                        if (bandDynEnabled[(size_t)band])
                        {
                            float detectionL = dynamicEQ.processDetection(band, sampleL, 0);
                            float detectionR = dynamicEQ.processDetection(band, sampleR, 1);
                            dynamicEQ.processBand(band, detectionL, 0);
                            dynamicEQ.processBand(band, detectionR, 1);
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
                            float detectionL = dynamicEQ.processDetection(band, detL, 0);
                            float detectionR = dynamicEQ.processDetection(band, detR, 1);
                            dynamicEQ.processBand(band, detectionL, 0);
                            dynamicEQ.processBand(band, detectionR, 1);
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
                            switch (routing)
                            {
                                case 0: sampleL = osL.processSample(sampleL, ws);
                                        sampleR = osR.processSample(sampleR, ws); break;
                                case 1: sampleL = osL.processSample(sampleL, ws); break;
                                case 2: sampleR = osR.processSample(sampleR, ws); break;
                                case 3: { float mid=(sampleL+sampleR)*0.5f, side=(sampleL-sampleR)*0.5f; mid=osL.processSample(mid, ws); sampleL=mid+side; sampleR=mid-side; break; }
                                case 4: { float mid=(sampleL+sampleR)*0.5f, side=(sampleL-sampleR)*0.5f; side=osR.processSample(side, ws); sampleL=mid+side; sampleR=mid-side; break; }
                            }
                        }
                    }
                });
                if (deltaSoloActive && currentSolo == band) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
            }

            // Band 8: LPF
            if (bandActive[7])
            {
                if (deltaSoloActive && currentSolo == 7) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }
                applyBandWithSmoothing(7, [&]() {
                    if (allStereoRouting) applyFilterStereo(lpfFilter);
                    else                  applyFilterWithRouting(lpfFilter, effectiveRouting[7]);
                });
                if (deltaSoloActive && currentSolo == 7) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
            }

            if (deltaSoloActive)
            {
                sampleL = deltaAfterL - deltaBeforeL;
                sampleR = deltaAfterR - deltaBeforeR;
            }

            if (!safeIsFinite(sampleL)) sampleL = 0.0f;
            if (!safeIsFinite(sampleR)) sampleR = 0.0f;

            procL[i] = sampleL;
            if (isStereo) procR[i] = sampleR;
        }

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

// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// TapeMachineDSP.cpp — orchestration layer (port of PluginProcessor.cpp's
// prepareToPlay / processBlock / updateFilters). See TapeMachineDSP.hpp and
// core/PORT_NOTES.md.

#include "TapeMachineDSP.hpp"

namespace duskaudio
{

//==============================================================================
// Tunable smoothing time-constants (exponential one-pole SmoothedValue replaces
// juce::SmoothedValue<Linear>). Chosen to settle in a similar time to the JUCE
// linear ramps (~3*tau ≈ ramp length). See PORT_NOTES for the shape change.
//==============================================================================
static constexpr float kGainTau  = 0.0067f; // ~20 ms settle (juce::dsp::Gain 20 ms ramp)
static constexpr float kSatTau   = 0.05f;   // ~150 ms settle (smoothedSaturation 150 ms)
static constexpr float kParamTau = 0.0067f; // ~20 ms settle (wow / flutter / noise 20 ms)

//==============================================================================
void TapeMachineDSP::prepare (double sampleRate, int maxBlockSize)
{
    if (sampleRate <= 0.0) sampleRate = 44100.0;
    if (maxBlockSize <= 0) maxBlockSize = 512;

    baseSampleRate = sampleRate;
    maxBlock       = maxBlockSize;
    maxOsRate      = baseSampleRate * 4.0;

    currentFactor = factorFromChoice (pOversampling.load (std::memory_order_relaxed));
    currentOsRate = baseSampleRate * static_cast<double> (currentFactor);
    lastFactor    = currentFactor;

    // Deterministic RNG streams: distinct constant seeds per channel (PORT_NOTES).
    coreL.setSeeds (1000u);
    coreR.setSeeds (2000u);
    sharedWowFlutter.setSeed (1u);

    osL.setFactor (currentFactor);
    osR.setFactor (currentFactor);
    osL.reset();
    osR.reset();

    coreL.prepare (currentOsRate, currentFactor, maxOsRate);
    coreR.prepare (currentOsRate, currentFactor, maxOsRate);
    sharedWowFlutter.prepare (currentOsRate, currentFactor, maxOsRate);

    const float hpFreq = pHighpassHz.load (std::memory_order_relaxed);
    const float lpFreq = pLowpassHz.load (std::memory_order_relaxed);
    hpL.prepare (currentOsRate, hpFreq, 0.707f); hpL.setType (DuskSVF::Type::highpass); hpL.reset();
    hpR.prepare (currentOsRate, hpFreq, 0.707f); hpR.setType (DuskSVF::Type::highpass); hpR.reset();
    lpL.prepare (currentOsRate, lpFreq, 0.707f); lpL.setType (DuskSVF::Type::lowpass);  lpL.reset();
    lpR.prepare (currentOsRate, lpFreq, 0.707f); lpR.setType (DuskSVF::Type::lowpass);  lpR.reset();
    bypassLowpass = (lpFreq >= 19000.0f);
    lastHpFreq = hpFreq;
    lastLpFreq = lpFreq;

    // Smoothers: param smoothers configured at BASE rate but advanced at the
    // oversampled rate (matches the JUCE structure — see PORT_NOTES). Output gain
    // is advanced at the oversampled rate so it is configured there.
    inGain.prepare  (baseSampleRate, kGainTau);
    outGain.prepare (currentOsRate,  kGainTau);
    smSat.prepare     (baseSampleRate, kSatTau);
    smWow.prepare     (baseSampleRate, kParamTau);
    smFlutter.prepare (baseSampleRate, kParamTau);
    smNoise.prepare   (baseSampleRate, kParamTau);

    // Initial (snap) values matching PluginProcessor::prepareToPlay.
    const float inGainDb = pInputGainDb.load (std::memory_order_relaxed);
    inGain.snap  (dbToGain (inGainDb));
    outGain.snap (pAutoComp.load (std::memory_order_relaxed)
                      ? dbToGain (-inGainDb + 3.08f)             // comp at inGainDb==0
                      : dbToGain (pOutputGainDb.load (std::memory_order_relaxed)));
    const float initSat = std::clamp (((inGainDb + 12.0f) / 24.0f) * 100.0f, 0.0f, 100.0f);
    smSat.snap     (initSat);
    smWow.snap     (pWow.load (std::memory_order_relaxed));
    smFlutter.snap (pFlutter.load (std::memory_order_relaxed));
    smNoise.snap   (pNoiseAmount.load (std::memory_order_relaxed) * 0.01f);

    // Scratch buffers (allocated here; never on the audio thread).
    inGainArr.assign  (static_cast<size_t> (maxBlock), 0.0f);
    const size_t osCap = static_cast<size_t> (maxBlock) * 4u;
    satArr.assign       (osCap, 0.0f);
    wowFlutArr.assign   (osCap, 0.0f);
    noiseArr.assign     (osCap, 0.0f);
    sharedModArr.assign (osCap, 0.0f);
    outGainArr.assign   (osCap, 0.0f);

    // VU: linear peak with ~300 ms release (per the DPF contract; JUCE used a
    // 300 ms RMS on separate in/out meters — see PORT_NOTES).
    vuReleaseCoeff = std::exp (-1.0f / (0.3f * static_cast<float> (baseSampleRate)));
    vuStateL = vuStateR = 0.0f;
    vuL.store (0.0f, std::memory_order_relaxed);
    vuR.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
void TapeMachineDSP::reset()
{
    osL.reset(); osR.reset();
    coreL.reset(); coreR.reset();
    // sharedWowFlutter has no dedicated reset(); re-zero its consumed state.
    if (! sharedWowFlutter.delayBuffer.empty())
        std::fill (sharedWowFlutter.delayBuffer.begin(), sharedWowFlutter.delayBuffer.end(), 0.0f);
    sharedWowFlutter.writeIndex = 0;
    sharedWowFlutter.allpassState = 0.0f;
    sharedWowFlutter.wowPhase = 0.0;
    sharedWowFlutter.flutterPhase = 0.0;
    sharedWowFlutter.randomCurrent = 0.0f;
    sharedWowFlutter.randomTarget = 0.0f;
    sharedWowFlutter.randomUpdateCounter = 0;

    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
    vuStateL = vuStateR = 0.0f;
    vuL.store (0.0f, std::memory_order_relaxed);
    vuR.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
int TapeMachineDSP::latencySamples() const noexcept
{
    // Oversampler::latency() is in base-rate samples for the active factor.
    return static_cast<int> (std::lround (osL.latency()));
}

//==============================================================================
void TapeMachineDSP::applyFactor (int newFactor)
{
    currentFactor = newFactor;
    currentOsRate = baseSampleRate * static_cast<double> (newFactor);

    osL.setFactor (newFactor); osR.setFactor (newFactor);
    osL.reset();               osR.reset();

    // No reallocation: wow/flutter buffers are pre-sized for the max factor.
    coreL.prepare (currentOsRate, newFactor, maxOsRate);
    coreR.prepare (currentOsRate, newFactor, maxOsRate);
    sharedWowFlutter.prepare (currentOsRate, newFactor, maxOsRate);

    const float hpFreq = pHighpassHz.load (std::memory_order_relaxed);
    const float lpFreq = pLowpassHz.load (std::memory_order_relaxed);
    hpL.prepare (currentOsRate, hpFreq, 0.707f); hpL.setType (DuskSVF::Type::highpass);
    hpR.prepare (currentOsRate, hpFreq, 0.707f); hpR.setType (DuskSVF::Type::highpass);
    lpL.prepare (currentOsRate, lpFreq, 0.707f); lpL.setType (DuskSVF::Type::lowpass);
    lpR.prepare (currentOsRate, lpFreq, 0.707f); lpR.setType (DuskSVF::Type::lowpass);

    outGain.prepare (currentOsRate, kGainTau);   // re-config OS-rate ramp coeff

    lastHpFreq = -1.0f;   // force SVF cutoff refresh below
    lastLpFreq = -1.0f;
    lastFactor = newFactor;
}

//==============================================================================
void TapeMachineDSP::processBlock (const float* const* inputs, float* const* outputs,
                                   int nCh, int nSamples) noexcept
{
    ScopedFlushDenormals denormalGuard;

    if (nSamples <= 0) return;
    nCh = clampI (nCh, 1, 2);

    // --- oversampling factor change (RT-safe reconfigure, no allocation) ------
    const int reqFactor = factorFromChoice (pOversampling.load (std::memory_order_relaxed));
    if (reqFactor != currentFactor)
        applyFactor (reqFactor);

    // --- bypass: pure passthrough (matches JUCE early return, no meter update)-
    if (pBypass.load (std::memory_order_relaxed))
    {
        for (int ch = 0; ch < nCh; ++ch)
            if (inputs[ch] != outputs[ch])
                for (int n = 0; n < nSamples; ++n) outputs[ch][n] = inputs[ch][n];
        return;
    }

    const auto signalPath = static_cast<TapeCore::SignalPath> (clampI (pSignalPath.load (std::memory_order_relaxed), 0, 3));

    // --- Thru: passthrough + VU (input == output) ----------------------------
    if (signalPath == TapeCore::Thru)
    {
        for (int ch = 0; ch < nCh; ++ch)
            if (inputs[ch] != outputs[ch])
                for (int n = 0; n < nSamples; ++n) outputs[ch][n] = inputs[ch][n];

        float sL = vuStateL, sR = vuStateR;
        for (int n = 0; n < nSamples; ++n)
        {
            const float aL = std::abs (inputs[0][n]);
            sL = aL > sL ? aL : sL * vuReleaseCoeff;
            if (nCh >= 2) { const float aR = std::abs (inputs[1][n]); sR = aR > sR ? aR : sR * vuReleaseCoeff; }
        }
        vuStateL = sL; vuStateR = (nCh >= 2) ? sR : sL;
        vuL.store (vuStateL, std::memory_order_relaxed);
        vuR.store (vuStateR, std::memory_order_relaxed);
        return;
    }

    // --- tone SVF coefficient refresh (dirty) --------------------------------
    const float hpFreq = pHighpassHz.load (std::memory_order_relaxed);
    const float lpFreq = pLowpassHz.load (std::memory_order_relaxed);
    bypassLowpass = (lpFreq >= 19000.0f);
    if (std::abs (hpFreq - lastHpFreq) > 0.01f || std::abs (lpFreq - lastLpFreq) > 0.01f)
    {
        hpL.setType (DuskSVF::Type::highpass); hpL.setCutoff (hpFreq); hpL.setResonance (0.707f);
        hpR.setType (DuskSVF::Type::highpass); hpR.setCutoff (hpFreq); hpR.setResonance (0.707f);
        lpL.setType (DuskSVF::Type::lowpass);  lpL.setCutoff (lpFreq); lpL.setResonance (0.707f);
        lpR.setType (DuskSVF::Type::lowpass);  lpR.setCutoff (lpFreq); lpR.setResonance (0.707f);
        lastHpFreq = hpFreq;
        lastLpFreq = lpFreq;
    }

    // --- block-constant parameter reads --------------------------------------
    const auto machine = static_cast<TapeCore::TapeMachine> (clampI (pMachine.load (std::memory_order_relaxed), 0, 1));
    const auto speed   = static_cast<TapeCore::TapeSpeed>   (clampI (pSpeed.load (std::memory_order_relaxed), 0, 2));
    const auto type    = static_cast<TapeCore::TapeType>    (clampI (pType.load (std::memory_order_relaxed), 0, 3));
    const auto eq      = static_cast<TapeCore::EQStandard>  (clampI (pEqStandard.load (std::memory_order_relaxed), 0, 2));

    const float inputGainDb = pInputGainDb.load (std::memory_order_relaxed);
    const float targetInputGain = dbToGain (inputGainDb);

    float targetOutputGain;
    if (pAutoComp.load (std::memory_order_relaxed))
    {
        float compressionCompensation;
        if (inputGainDb <= 0.0f)
            compressionCompensation = 0.0066667f * inputGainDb * inputGainDb
                                    + 0.346667f  * inputGainDb + 3.08f;
        else
            compressionCompensation = 0.0498611f * inputGainDb * inputGainDb
                                    + 0.0925f    * inputGainDb + 3.08f;
        targetOutputGain = dbToGain (-inputGainDb + compressionCompensation);
    }
    else
    {
        targetOutputGain = dbToGain (pOutputGainDb.load (std::memory_order_relaxed));
    }

    inGain.setTarget (targetInputGain);
    outGain.setTarget (targetOutputGain);

    const float saturationAmount = std::clamp (((inputGainDb + 12.0f) / 24.0f) * 100.0f, 0.0f, 100.0f);
    smSat.setTarget     (saturationAmount);
    smWow.setTarget     (pWow.load (std::memory_order_relaxed));
    smFlutter.setTarget (pFlutter.load (std::memory_order_relaxed));
    smNoise.setTarget   (pNoiseAmount.load (std::memory_order_relaxed) * 0.01f);

    // JUCE derives the noise gate from the amount knob (>0.05 %); the boolean
    // noiseEnabled param is dead in the source, so we mirror that (PORT_NOTES).
    const bool noiseEnabled = pNoiseAmount.load (std::memory_order_relaxed) > 0.05f;

    const float calibrationDb = static_cast<float> (clampI (pCalibration.load (std::memory_order_relaxed), 0, 3)) * 3.0f;

    // Bias: auto-cal (from type/speed) or manual — block-constant, matches JUCE.
    float biasAmount;
    if (pAutoCal.load (std::memory_order_relaxed))
    {
        float optimalBias = 0.5f;
        switch (type)
        {
            case TapeCore::Type456: optimalBias = 0.50f; break;
            case TapeCore::TypeGP9: optimalBias = 0.55f; break;
            case TapeCore::Type911: optimalBias = 0.52f; break;
            case TapeCore::Type250: optimalBias = 0.45f; break;
        }
        switch (speed)
        {
            case TapeCore::Speed_7_5_IPS: optimalBias *= 1.05f; break;
            case TapeCore::Speed_15_IPS: break;
            case TapeCore::Speed_30_IPS: optimalBias *= 0.95f; break;
        }
        biasAmount = std::clamp (optimalBias, 0.0f, 1.0f);
    }
    else
    {
        biasAmount = pBias.load (std::memory_order_relaxed) * 0.01f;
    }

    // Shared wow/flutter rates (block-constant, from speed).
    float wowRate = 0.5f, flutterRate = 5.0f;
    switch (speed)
    {
        case TapeCore::Speed_7_5_IPS: wowRate = 0.33f; flutterRate = 3.5f; break;
        case TapeCore::Speed_15_IPS:  wowRate = 0.5f;  flutterRate = 5.0f; break;
        case TapeCore::Speed_30_IPS:  wowRate = 0.8f;  flutterRate = 7.0f; break;
    }

    // --- precompute per-sample shared values ---------------------------------
    const int factor = currentFactor;
    const int osN = nSamples * factor;

    for (int n = 0; n < nSamples; ++n)
        inGainArr[static_cast<size_t> (n)] = inGain.next();

    for (int i = 0; i < osN; ++i)
    {
        const float sat = smSat.next();
        const float wv  = smWow.next();
        const float fv  = smFlutter.next();
        const float nv  = smNoise.next();
        const float combined = wv + fv;
        float sm = 0.0f;
        if (combined > 0.0f)
            sm = sharedWowFlutter.calculateModulation (wv * 0.01f, fv * 0.01f, wowRate, flutterRate, currentOsRate);

        const size_t si = static_cast<size_t> (i);
        satArr[si]       = sat * 0.01f;
        wowFlutArr[si]   = combined * 0.01f;
        noiseArr[si]     = nv * 100.0f;
        sharedModArr[si] = sm;
        outGainArr[si]   = outGain.next();
    }

    // --- per-channel oversampled processing ----------------------------------
    // Functor chain (at the oversampled rate): HP SVF -> tape core -> LP SVF ->
    // output gain. Input gain is applied at base rate before upsampling.
    {
        int osIdx = 0;
        for (int n = 0; n < nSamples; ++n)
        {
            const float x = inputs[0][n] * inGainArr[static_cast<size_t> (n)];
            outputs[0][n] = osL.processSample (x, [&] (float s) noexcept
            {
                const size_t si = static_cast<size_t> (osIdx);
                s = hpL.process (s);
                s = coreL.processSample (s, machine, speed, type, biasAmount,
                                         satArr[si], wowFlutArr[si], noiseEnabled, noiseArr[si],
                                         &sharedModArr[si], calibrationDb, eq, signalPath);
                if (! bypassLowpass) s = lpL.process (s);
                s *= outGainArr[si];
                ++osIdx;
                return s;
            });
        }
    }

    if (nCh >= 2)
    {
        int osIdx = 0;
        for (int n = 0; n < nSamples; ++n)
        {
            const float x = inputs[1][n] * inGainArr[static_cast<size_t> (n)];
            outputs[1][n] = osR.processSample (x, [&] (float s) noexcept
            {
                const size_t si = static_cast<size_t> (osIdx);
                s = hpR.process (s);
                s = coreR.processSample (s, machine, speed, type, biasAmount,
                                         satArr[si], wowFlutArr[si], noiseEnabled, noiseArr[si],
                                         &sharedModArr[si], calibrationDb, eq, signalPath);
                if (! bypassLowpass) s = lpR.process (s);
                s *= outGainArr[si];
                ++osIdx;
                return s;
            });
        }
    }

    // --- crosstalk (base rate — deviation from JUCE's OS-rate placement) ------
    if (nCh >= 2)
    {
        const float crosstalkAmount = (machine == TapeCore::Swiss800) ? 0.005f : 0.015f;
        for (int n = 0; n < nSamples; ++n)
        {
            const float tempL = outputs[0][n];
            const float tempR = outputs[1][n];
            outputs[0][n] += tempR * crosstalkAmount;
            outputs[1][n] += tempL * crosstalkAmount;
        }
    }

    // --- VU meter (output; linear peak, ~300 ms release) ---------------------
    float sL = vuStateL, sR = vuStateR;
    for (int n = 0; n < nSamples; ++n)
    {
        const float aL = std::abs (outputs[0][n]);
        sL = aL > sL ? aL : sL * vuReleaseCoeff;
        if (nCh >= 2) { const float aR = std::abs (outputs[1][n]); sR = aR > sR ? aR : sR * vuReleaseCoeff; }
    }
    vuStateL = sL;
    vuStateR = (nCh >= 2) ? sR : sL;
    vuL.store (vuStateL, std::memory_order_relaxed);
    vuR.store (vuStateR, std::memory_order_relaxed);
}

} // namespace duskaudio

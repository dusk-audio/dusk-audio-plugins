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

void MultiQDSP::prepare(double sampleRate, int /*maxBlockSize*/)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    // Per-sample coefficient ramp for the static bands (~1 ms), matching the
    // JUCE build's svfSmoothCoeff = 1 - exp(-1/(0.001*SR)).
    biquadSmoothCoeff = (float)(1.0 - std::exp(-1.0 / (0.001 * currentSampleRate)));
    for (auto& f : svfFilters) { f.setSmoothCoeff(biquadSmoothCoeff); f.reset(); }
    for (auto& f : svfDynGainFilters) { f.setSmoothCoeff(biquadSmoothCoeff); f.reset(); }
    dynamicEQ.prepare(currentSampleRate, 2);
    hpfFilter.reset(); lpfFilter.reset();
    for (auto& s : bandEnableSmoothed) { s.reset(currentSampleRate, 0.01); s.setCurrentAndTargetValue(1.0f); }
    prevBandPhaseInvertGain.fill(1.0f);
    prevBandPanVal.fill(0.0f);
    prevHpfStages = prevLpfStages = -1;
    firstBlock = true;
}

void MultiQDSP::reset()
{
    for (auto& f : svfFilters) f.reset();
    for (auto& f : svfDynGainFilters) f.reset();
    dynamicEQ.reset();
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

void MultiQDSP::computeTiltShelf(BiquadCoeffs& c, double sr, double freq, float gainDB)
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

void MultiQDSP::computeBandCoeffs(int band, const Params& p, BiquadCoeffs& c) const
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
        BiquadCoeffs c;
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
        BiquadCoeffs c;
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

void MultiQDSP::process(const float* const* inputs, float* const* outputs,
                        int numChannels, int numSamples, const Params& p)
{
    if (numSamples <= 0 || numChannels <= 0) return;

    const bool isStereo = numChannels > 1;
    float* procL = outputs[0];
    float* procR = isStereo ? outputs[1] : nullptr;
    // in-place-safe copy of input into the output working buffers
    for (int i = 0; i < numSamples; ++i) procL[i] = inputs[0][i];
    if (isStereo) for (int i = 0; i < numSamples; ++i) procR[i] = inputs[1][i];

    const auto eqType = (EQType)p.eqType;

    // British/Tube not ported yet — pass audio through untouched (still apply
    // master gain below so levels stay consistent for those modes' A/B).
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
            BiquadCoeffs c;
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
                            sampleL = waveshaperCurves.processWithDrive(sampleL, curve, drive);
                            sampleR = waveshaperCurves.processWithDrive(sampleR, curve, drive);
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
                            switch (routing)
                            {
                                case 0: sampleL = waveshaperCurves.processWithDrive(sampleL, curve, drive);
                                        sampleR = waveshaperCurves.processWithDrive(sampleR, curve, drive); break;
                                case 1: sampleL = waveshaperCurves.processWithDrive(sampleL, curve, drive); break;
                                case 2: sampleR = waveshaperCurves.processWithDrive(sampleR, curve, drive); break;
                                case 3: { float mid=(sampleL+sampleR)*0.5f, side=(sampleL-sampleR)*0.5f; mid=waveshaperCurves.processWithDrive(mid,curve,drive); sampleL=mid+side; sampleR=mid-side; break; }
                                case 4: { float mid=(sampleL+sampleR)*0.5f, side=(sampleL-sampleR)*0.5f; side=waveshaperCurves.processWithDrive(side,curve,drive); sampleL=mid+side; sampleR=mid-side; break; }
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

    // Master gain (applied to all modes, base rate) — mirrors the JUCE tail.
    float mg = std::pow(10.0f, p.masterGain / 20.0f);
    if (std::abs(p.masterGain) > 0.01f)
    {
        for (int i = 0; i < numSamples; ++i) procL[i] *= mg;
        if (isStereo) for (int i = 0; i < numSamples; ++i) procR[i] *= mg;
    }
}

} // namespace duskaudio

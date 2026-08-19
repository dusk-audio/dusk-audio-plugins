// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "MultiCompDSP.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace duskaudio
{

void MultiCompDSP::prepare(double sr, int blockSize)
{
    sampleRate = std::isfinite(sr) && sr > 0.0 ? sr : 48000.0;
    maxBlock = std::max(1, blockSize);
    preparedOversampling = params.oversampling.load(std::memory_order_relaxed) == 2 ? 4
                         : params.oversampling.load(std::memory_order_relaxed) == 1 ? 2 : 1;
    modes.prepare(sampleRate, maxBlock, preparedOversampling);
    truePeakDetector.prepare();
    truePeakDetector.setOversamplingFactor(4);
    for (auto& os : oversamplers) { os.setFactor(preparedOversampling); os.prepare(maxBlock); os.reset(); }
    for (auto& f : sidechainFilters) f.prepare(sampleRate);
    for (auto& f : sidechainFiltersExternal) f.prepare(sampleRate);
    for (auto& f : sidechainEQ) f.prepare(sampleRate);
    for (auto& band : bands) for (auto& v : band) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& band : sidechainBands) for (auto& v : band) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : processedSidechain) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : modeInput) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    dry.assign(static_cast<size_t>(maxBlock * kMaxChannels), 0.0f);
    mixCurve.assign(static_cast<size_t>(maxBlock), 1.0f);
    bypassCurve.assign(static_cast<size_t>(maxBlock), 0.0f);
    autoGainCurve.assign(static_cast<size_t>(maxBlock), 1.0f);
    for (auto& line : delayedInput) line.assign(static_cast<size_t>(maxBlock), 0.0f);
    const size_t lookaheadSize = static_cast<size_t>(std::ceil(sampleRate * 0.01)) + 1u;
    for (auto& line : globalLookahead) line.assign(lookaheadSize, 0.0f);
    globalLookaheadWrite = {{0, 0}};
    globalMixSmoother.prepare(sampleRate, 0.020f); globalMixSmoother.snap(params.mix.load(std::memory_order_relaxed) * 0.01f);
    autoGainMatcher.prepare(sampleRate);
    autoGainSmoother.prepare(sampleRate, 0.050f); autoGainSmoother.snap(1.0f);
    busMixRamp.prepare(sampleRate, 0.020f); busMixRamp.snap(params.busMix.load(std::memory_order_relaxed) * 0.01f);
    digitalMixRamp.prepare(sampleRate, 0.020f); digitalMixRamp.snap(params.digitalMix.load(std::memory_order_relaxed) * 0.01f);
    bypassRamp.prepare(sampleRate, static_cast<float>(kBypassRampMs) * 0.001f); bypassRamp.snap(0.0f);
    bypassSettled = false; lastBypass = false; firstBlock = true;
    noiseState = 0x6d2b79f5u;
    multibandEnvelopes.fill(1.0f);
    updateCrossovers();
    for (auto& m : bandGR) m.store(0.0f, std::memory_order_relaxed);
    masterGR.store(0.0f, std::memory_order_relaxed);
    inputLevel.store(-60.0f, std::memory_order_relaxed);
    outputLevel.store(-60.0f, std::memory_order_relaxed);
    currentLatency = getLatencySamples();
}

void MultiCompDSP::reset()
{
    modes.reset();
    truePeakDetector.prepare();
    for (auto& os : oversamplers) os.reset();
    for (auto& f : sidechainFilters) f.reset();
    for (auto& f : sidechainFiltersExternal) f.reset();
    for (auto& f : sidechainEQ) f.reset();
    for (auto& c : crossover1) c.reset();
    for (auto& c : crossover2) c.reset();
    for (auto& c : crossover3) c.reset();
    for (auto& c : scCrossover1) c.reset();
    for (auto& c : scCrossover2) c.reset();
    for (auto& c : scCrossover3) c.reset();
    for (auto& band : bands) for (auto& v : band) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& band : sidechainBands) for (auto& v : band) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : processedSidechain) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : modeInput) std::fill(v.begin(), v.end(), 0.0f);
    std::fill(dry.begin(), dry.end(), 0.0f);
    for (auto& line : delayedInput) std::fill(line.begin(), line.end(), 0.0f);
    for (auto& line : globalLookahead) std::fill(line.begin(), line.end(), 0.0f);
    globalLookaheadWrite = {{0, 0}};
    multibandEnvelopes.fill(1.0f);
    globalMixSmoother.snap(params.mix.load(std::memory_order_relaxed) * 0.01f);
    autoGainMatcher.reset();
    autoGainSmoother.snap(1.0f);
    busMixRamp.snap(params.busMix.load(std::memory_order_relaxed) * 0.01f);
    digitalMixRamp.snap(params.digitalMix.load(std::memory_order_relaxed) * 0.01f);
    bypassRamp.snap(params.bypass.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
    bypassSettled = params.bypass.load(std::memory_order_relaxed);
    lastBypass = bypassSettled;
    firstBlock = true;
    noiseState = 0x6d2b79f5u;
    for (auto& m : bandGR) m.store(0.0f, std::memory_order_relaxed);
    masterGR.store(0.0f, std::memory_order_relaxed);
}

void MultiCompDSP::setParameter(Parameter parameter, float value) noexcept
{
    const bool b = value >= 0.5f;
    switch (parameter)
    {
        case Parameter::Mode: params.mode.store(std::clamp(static_cast<int>(value), 0, 7), std::memory_order_relaxed); break;
        case Parameter::Bypass: params.bypass.store(b, std::memory_order_relaxed); break;
        case Parameter::StereoLink: params.stereoLink.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::Mix: params.mix.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::SidechainHP: params.sidechainHP.store(std::clamp(value, 0.0f, 500.0f), std::memory_order_relaxed); break;
        case Parameter::TruePeakEnable: params.truePeakEnable.store(b, std::memory_order_relaxed); break;
        case Parameter::TruePeakQuality: params.truePeakQuality.store(std::clamp(static_cast<int>(value), 0, 1), std::memory_order_relaxed); break;
        case Parameter::ExternalSidechain: params.externalSidechain.store(b, std::memory_order_relaxed); break;
        case Parameter::AutoMakeup: params.autoMakeup.store(b, std::memory_order_relaxed); break;
        case Parameter::Distortion: params.distortion.store(std::clamp(static_cast<int>(value), 0, 3), std::memory_order_relaxed); break;
        case Parameter::DistortionAmount: params.distortionAmount.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::Oversampling: params.oversampling.store(std::clamp(static_cast<int>(value), 0, 2), std::memory_order_relaxed); break;
        case Parameter::GlobalLookahead: params.globalLookahead.store(std::clamp(value, 0.0f, 10.0f), std::memory_order_relaxed); break;
        case Parameter::OptoPeakReduction: params.optoPeakReduction.store(value, std::memory_order_relaxed); break;
        case Parameter::OptoGain: params.optoGain.store(value, std::memory_order_relaxed); break;
        case Parameter::OptoLimit: params.optoLimit.store(b, std::memory_order_relaxed); break;
        case Parameter::FetInput: params.fetInput.store(value, std::memory_order_relaxed); break;
        case Parameter::FetOutput: params.fetOutput.store(value, std::memory_order_relaxed); break;
        case Parameter::FetAttack: params.fetAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::FetRelease: params.fetRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::FetRatio: params.fetRatio.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::FetCurve: params.fetCurve.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::FetTransient: params.fetTransient.store(value, std::memory_order_relaxed); break;
        case Parameter::FetThreshold: params.fetThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaThreshold: params.vcaThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaRatio: params.vcaRatio.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaAttack: params.vcaAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaRelease: params.vcaRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaOutput: params.vcaOutput.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaOverEasy: params.vcaOverEasy.store(b, std::memory_order_relaxed); break;
        case Parameter::VcaClassicDetector: params.vcaClassicDetector.store(b, std::memory_order_relaxed); break;
        case Parameter::BusThreshold: params.busThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::BusRatio: params.busRatio.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::BusAttack: params.busAttack.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::BusRelease: params.busRelease.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::BusMakeup: params.busMakeup.store(value, std::memory_order_relaxed); break;
        case Parameter::BusMix: params.busMix.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaThreshold: params.studioVcaThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaRatio: params.studioVcaRatio.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaAttack: params.studioVcaAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaRelease: params.studioVcaRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaOutput: params.studioVcaOutput.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalThreshold: params.digitalThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalRatio: params.digitalRatio.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalKnee: params.digitalKnee.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalAttack: params.digitalAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalRelease: params.digitalRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalLookahead: params.digitalLookahead.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalMix: params.digitalMix.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::DigitalOutput: params.digitalOutput.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalAdaptive: params.digitalAdaptive.store(b, std::memory_order_relaxed); break;
        case Parameter::Crossover1: params.crossover1.store(value, std::memory_order_relaxed); break;
        case Parameter::Crossover2: params.crossover2.store(value, std::memory_order_relaxed); break;
        case Parameter::Crossover3: params.crossover3.store(value, std::memory_order_relaxed); break;
        case Parameter::EnvelopeCurve: params.envelopeCurve.store(std::clamp(static_cast<int>(value), 0, 1), std::memory_order_relaxed); break;
        case Parameter::GlobalSidechainListen: params.globalSidechainListen.store(b, std::memory_order_relaxed); break;
        case Parameter::MbMix: params.mbMix.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::MbOutput: params.mbOutput.store(std::clamp(value, -24.0f, 24.0f), std::memory_order_relaxed); break;
        case Parameter::NoiseEnable: params.noiseEnable.store(b, std::memory_order_relaxed); break;
        case Parameter::SaturationMode: params.saturationMode.store(std::clamp(static_cast<int>(value), 0, 2), std::memory_order_relaxed); break;
        case Parameter::ScLowFreq: params.scLowFreq.store(std::clamp(value, 60.0f, 500.0f), std::memory_order_relaxed); break;
        case Parameter::ScLowGain: params.scLowGain.store(std::clamp(value, -12.0f, 12.0f), std::memory_order_relaxed); break;
        case Parameter::ScHighFreq: params.scHighFreq.store(std::clamp(value, 2000.0f, 16000.0f), std::memory_order_relaxed); break;
        case Parameter::ScHighGain: params.scHighGain.store(std::clamp(value, -12.0f, 12.0f), std::memory_order_relaxed); break;
        case Parameter::StereoLinkMode: params.stereoLinkMode.store(std::clamp(static_cast<int>(value), 0, 2), std::memory_order_relaxed); break;
    }
}

void MultiCompDSP::setMultibandParameter(int band, MultibandParameter parameter, float value) noexcept
{
    if (band < 0 || band >= kMultiCompBands) return;
    const size_t b = static_cast<size_t>(band);
    switch (parameter)
    {
        case MultibandParameter::Threshold: params.mbThreshold[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Ratio: params.mbRatio[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Attack: params.mbAttack[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Release: params.mbRelease[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Makeup: params.mbMakeup[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Bypass: params.mbBypass[b].store(value >= 0.5f, std::memory_order_relaxed); break;
        case MultibandParameter::Solo: params.mbSolo[b].store(value >= 0.5f, std::memory_order_relaxed); break;
        case MultibandParameter::Enabled: params.mbEnabled[b].store(value >= 0.5f, std::memory_order_relaxed); break;
    }
}

void MultiCompDSP::processBlock(const float* const* in, float* const* out, int nCh, int nSamples)
{
    processBlockExternal(in, nullptr, out, nCh, nSamples);
}

void MultiCompDSP::processBlockExternal(const float* const* in, const float* const* sidechain,
                                        float* const* out, int nCh, int nSamples)
{
    if (in == nullptr || out == nullptr || nSamples <= 0 || nCh <= 0 || dry.empty()) return;
    nCh = std::min(nCh, kMaxChannels);
    if (nSamples > maxBlock) return;
    ScopedFlushDenormals guard;
    const bool requestedBypass = params.bypass.load(std::memory_order_relaxed);
    if (requestedBypass != lastBypass)
    {
        lastBypass = requestedBypass;
        bypassSettled = false;
        bypassRamp.setTarget(requestedBypass ? 1.0f : 0.0f);
    }
    if (requestedBypass && bypassSettled)
    {
        for (int ch = 0; ch < nCh; ++ch)
            if (in[ch] != out[ch]) std::memcpy(out[ch], in[ch], static_cast<size_t>(nSamples) * sizeof(float));
        updateMeters(in, out, nCh, nSamples);
        return;
    }
    const float* processingIn[kMaxChannels] = {in[0], nCh > 1 ? in[1] : in[0]};
    const bool useExternalSidechain = params.externalSidechain.load(std::memory_order_relaxed) && sidechain != nullptr;
    const float* filteredSidechain[kMaxChannels] = {processedSidechain[0].data(), processedSidechain[1].data()};
    for (int ch = 0; ch < nCh; ++ch)
    {
        const float* source = useExternalSidechain ? sidechain[ch] : in[ch];
        sidechainFilters[ch].setFrequency(params.sidechainHP.load(std::memory_order_relaxed));
        sidechainEQ[ch].setLowShelf(params.scLowFreq.load(std::memory_order_relaxed), params.scLowGain.load(std::memory_order_relaxed));
        sidechainEQ[ch].setHighShelf(params.scHighFreq.load(std::memory_order_relaxed), params.scHighGain.load(std::memory_order_relaxed));
        for (int i = 0; i < nSamples; ++i)
        {
            float sample = source[i];
            if (params.sidechainHP.load(std::memory_order_relaxed) >= 1.0f)
                sample = sidechainFilters[ch].process(sample);
            processedSidechain[ch][static_cast<size_t>(i)] = sidechainEQ[ch].process(sample);
        }
    }
    if (nCh == 1) filteredSidechain[1] = filteredSidechain[0];
    prepareLookahead(in, processingIn, nCh, nSamples);
    for (int ch = 0; ch < nCh; ++ch)
        std::memcpy(dry.data() + static_cast<size_t>(ch * maxBlock), processingIn[ch], static_cast<size_t>(nSamples) * sizeof(float));
    if (nCh > 1 && params.stereoLinkMode.load(std::memory_order_relaxed) == 1)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            modeInput[0][static_cast<size_t>(i)] = (processingIn[0][i] + processingIn[1][i]) * 0.5f;
            modeInput[1][static_cast<size_t>(i)] = (processingIn[0][i] - processingIn[1][i]) * 0.5f;
        }
        processingIn[0] = modeInput[0].data();
        processingIn[1] = modeInput[1].data();
    }
    processRange(processingIn, filteredSidechain, out, nCh, 0, nSamples);
    const MultiCompMode mode = static_cast<MultiCompMode>(std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7));
    const bool isMulti = mode == MultiCompMode::Multiband;
    if (params.globalSidechainListen.load(std::memory_order_relaxed))
    {
        updateMeters(in, out, nCh, nSamples);
        firstBlock = false;
        return;
    }
    if (nCh > 1 && params.stereoLinkMode.load(std::memory_order_relaxed) == 1)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            const float mid = out[0][i], side = out[1][i];
            out[0][i] = mid + side;
            out[1][i] = mid - side;
        }
    }
    const float targetMix = std::clamp((isMulti ? params.mbMix.load(std::memory_order_relaxed) : params.mix.load(std::memory_order_relaxed)) * 0.01f, 0.0f, 1.0f);
    globalMixSmoother.setTarget(targetMix);
    for (int i = 0; i < nSamples; ++i)
    {
        mixCurve[static_cast<size_t>(i)] = globalMixSmoother.next();
        bypassCurve[static_cast<size_t>(i)] = bypassRamp.next();
    }
    // `dry` is one shared scratch line; copy the source per channel again before
    // mixing. This is safe for in-place processing and avoids process-time allocs.
    for (int ch = 0; ch < nCh; ++ch)
    {
        float* dryChannel = dry.data() + static_cast<size_t>(ch * maxBlock);
        for (int i = 0; i < nSamples; ++i)
        {
            const float wet = mixCurve[static_cast<size_t>(i)];
            const float bypass = bypassCurve[static_cast<size_t>(i)];
            if (!isMulti || wet < 1.0f) out[ch][i] = out[ch][i] * wet + dryChannel[i] * (1.0f - wet);
            if (!bypassSettled) out[ch][i] = out[ch][i] * (1.0f - bypass) + dryChannel[i] * bypass;
        }
    }
    if (requestedBypass && bypassRamp.value() >= 1.0f) bypassSettled = true;

    const bool autoMakeup = params.autoMakeup.load(std::memory_order_relaxed);
    if (autoMakeup)
    {
        double inSum = 0.0, outSum = 0.0;
        for (int ch = 0; ch < nCh; ++ch)
            for (int i = 0; i < nSamples; ++i)
            {
                const float drySample = dry[static_cast<size_t>(ch * maxBlock + i)];
                inSum += static_cast<double>(drySample) * drySample;
                outSum += static_cast<double>(out[ch][i]) * out[ch][i];
            }
        const float divisor = static_cast<float>(std::max(1, nCh * nSamples));
        const float target = autoGainMatcher.update(std::sqrt(static_cast<float>(inSum) / divisor),
                                                    std::sqrt(static_cast<float>(outSum) / divisor), nSamples);
        autoGainSmoother.setTarget(target);
    }
    else
    {
        autoGainMatcher.reset();
        autoGainSmoother.setTarget(1.0f);
    }
    for (int i = 0; i < nSamples; ++i) autoGainCurve[static_cast<size_t>(i)] = autoGainSmoother.next();
    for (int ch = 0; ch < nCh; ++ch)
        for (int i = 0; i < nSamples; ++i) out[ch][i] *= autoGainCurve[static_cast<size_t>(i)];
    if (params.noiseEnable.load(std::memory_order_relaxed) && mode != MultiCompMode::Digital && mode != MultiCompMode::Multiband)
    {
        for (int ch = 0; ch < nCh; ++ch)
            for (int i = 0; i < nSamples; ++i)
            {
                noiseState = noiseState * 1664525u + 1013904223u;
                const float unit = static_cast<float>((noiseState >> 8) & 0x00ffffffu) / 16777215.0f;
                out[ch][i] += (unit * 2.0f - 1.0f) * 0.0001f;
            }
    }
    updateMeters(in, out, nCh, nSamples);
    firstBlock = false;
}

void MultiCompDSP::prepareLookahead(const float* const* in,
                                    const float* (&processingIn)[kMaxChannels],
                                    int nCh, int nSamples)
{
    const int delay = static_cast<int>(std::round(std::clamp(
        params.globalLookahead.load(std::memory_order_relaxed), 0.0f, 10.0f)
        * 0.001f * static_cast<float>(sampleRate)));
    if (delay <= 0 || globalLookahead[0].empty()) return;

    const int size = static_cast<int>(globalLookahead[0].size());
    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& line = globalLookahead[static_cast<size_t>(ch)];
        int& write = globalLookaheadWrite[static_cast<size_t>(ch)];
        for (int i = 0; i < nSamples; ++i)
        {
            const int read = (write - std::min(delay, size - 1) + size) % size;
            line[static_cast<size_t>(write)] = in[ch][i];
            delayedInput[static_cast<size_t>(ch)][static_cast<size_t>(i)] = line[static_cast<size_t>(read)];
            write = (write + 1) % size;
        }
        processingIn[ch] = delayedInput[static_cast<size_t>(ch)].data();
    }
}

void MultiCompDSP::processRange(const float* const* in, const float* const* sidechain,
                                float* const* out, int nCh, int /*first*/, int nSamples)
{
    const MultiCompMode mode = static_cast<MultiCompMode>(std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7));
    const bool external = params.externalSidechain.load(std::memory_order_relaxed) && sidechain != nullptr;
    truePeakDetector.setOversamplingFactor(params.truePeakQuality.load(std::memory_order_relaxed) == 1 ? 1 : 0);
    if (params.globalSidechainListen.load(std::memory_order_relaxed))
    {
        for (int ch = 0; ch < nCh; ++ch)
            std::memcpy(out[ch], sidechain[ch], static_cast<size_t>(nSamples) * sizeof(float));
        masterGR.store(0.0f, std::memory_order_relaxed);
        return;
    }
    if (mode == MultiCompMode::Multiband)
    {
        processMultiband(in, external ? sidechain : nullptr, out, nCh, nSamples, params.mbMix.load(std::memory_order_relaxed) * 0.01f);
        return;
    }
    const int actualOs = params.oversampling.load(std::memory_order_relaxed) == 2 ? 4 : (params.oversampling.load(std::memory_order_relaxed) == 1 ? 2 : 1);
    for (auto& os : oversamplers) os.setFactor(actualOs);
    busMixRamp.setTarget(std::clamp(params.busMix.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
    digitalMixRamp.setTarget(std::clamp(params.digitalMix.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
    for (int i = 0; i < nSamples; ++i)
    {
        const float localBusMix = mode == MultiCompMode::Bus ? busMixRamp.next() : 1.0f;
        const float localDigitalMix = mode == MultiCompMode::Digital ? digitalMixRamp.next() : 1.0f;
        const float rawSc0 = sidechain != nullptr ? sidechain[0][i] : in[0][i];
        const float rawSc1 = sidechain != nullptr ? sidechain[std::min(1, nCh - 1)][i] : in[std::min(1, nCh - 1)][i];
        // processBlock has already applied the HP and shelf EQ once. Keeping
        // this path as a pure read avoids doubling the detector filters.
        const float sc0 = rawSc0;
        const float sc1 = nCh > 1 ? rawSc1 : sc0;
        const float scLevel = std::max(std::abs(sc0), std::abs(sc1));
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float input = in[ch][i];
            const int linkMode = std::clamp(params.stereoLinkMode.load(std::memory_order_relaxed), 0, 2);
            const bool link = linkMode == 0 && params.stereoLink.load(std::memory_order_relaxed) > 0.01f && nCh > 1;
            const float ownSc = ch == 0 ? sc0 : sc1;
            const float linkAmount = std::clamp(params.stereoLink.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f);
            const float midSideSc = ch == 0 ? (sc0 + sc1) * 0.5f : (sc0 - sc1) * 0.5f;
            const float scInput = link ? std::abs(ownSc) * (1.0f - linkAmount) + scLevel * linkAmount
                                       : linkMode == 1 ? midSideSc : ownSc;
            const bool useTruePeak = params.truePeakEnable.load(std::memory_order_relaxed);
            const float peakSc = useTruePeak ? std::copysign(truePeakDetector.processSample(scInput, ch), scInput) : scInput;
            const float sc = peakSc;
            const float localMix = mode == MultiCompMode::Digital ? localDigitalMix : localBusMix;
            if (actualOs == 1)
                out[ch][i] = applyCoreDistortion(modes.process(mode, input, ch, sc, params, localMix),
                                                 static_cast<DistortionType>(std::clamp(params.distortion.load(std::memory_order_relaxed), 0, 3)),
                                                 std::clamp(params.distortionAmount.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
            else
            {
                out[ch][i] = oversamplers[ch].processSample(input, [&](float sample) noexcept {
                    return applyCoreDistortion(modes.process(mode, sample, ch, scInput, params, localMix),
                                               static_cast<DistortionType>(std::clamp(params.distortion.load(std::memory_order_relaxed), 0, 3)),
                                               std::clamp(params.distortionAmount.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
                });
            }
        }
    }
    const float gr = std::min(modes.gainReduction(mode, 0), nCh > 1 ? modes.gainReduction(mode, 1) : modes.gainReduction(mode, 0));
    masterGR.store(gr, std::memory_order_relaxed);
}

void MultiCompDSP::processMultiband(const float* const* input, const float* const* sidechain,
                                    float* const* output, int nCh, int nSamples, float mix)
{
    updateCrossovers();
    std::array<float, kMultiCompBands> maxGr{{0, 0, 0, 0}};
    for (int ch = 0; ch < nCh; ++ch)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            const float x = input[ch][i];
            float l0, h0, l1, h1, l2, h2;
            crossover1[ch].process(x, l0, h0); crossover2[ch].process(h0, l1, h1); crossover3[ch].process(h1, l2, h2);
            bands[0][static_cast<size_t>(ch)][static_cast<size_t>(i)] = l0;
            bands[1][static_cast<size_t>(ch)][static_cast<size_t>(i)] = l1;
            bands[2][static_cast<size_t>(ch)][static_cast<size_t>(i)] = l2;
            bands[3][static_cast<size_t>(ch)][static_cast<size_t>(i)] = h2;
            if (sidechain != nullptr)
            {
                float sl0, sh0, sl1, sh1, sl2, sh2;
                scCrossover1[ch].process(sidechain[std::min(ch, nCh - 1)][i], sl0, sh0); scCrossover2[ch].process(sh0, sl1, sh1); scCrossover3[ch].process(sh1, sl2, sh2);
                sidechainBands[0][static_cast<size_t>(ch)][static_cast<size_t>(i)] = sl0;
                sidechainBands[1][static_cast<size_t>(ch)][static_cast<size_t>(i)] = sl1;
                sidechainBands[2][static_cast<size_t>(ch)][static_cast<size_t>(i)] = sl2;
                sidechainBands[3][static_cast<size_t>(ch)][static_cast<size_t>(i)] = sh2;
            }
        }
        for (int band = 0; band < kMultiCompBands; ++band)
        {
            const float threshold = params.mbThreshold[static_cast<size_t>(band)].load(std::memory_order_relaxed);
            const float ratio = std::max(1.0f, params.mbRatio[static_cast<size_t>(band)].load(std::memory_order_relaxed));
            const float attack = std::max(0.0001f, params.mbAttack[static_cast<size_t>(band)].load(std::memory_order_relaxed) * 0.001f);
            const float release = std::max(0.001f, params.mbRelease[static_cast<size_t>(band)].load(std::memory_order_relaxed) * 0.001f);
            float& envelope = multibandEnvelopes[static_cast<size_t>(band * kMaxChannels + ch)];
            for (int i = 0; i < nSamples; ++i)
            {
                const float own = sidechain != nullptr ? std::abs(sidechainBands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)]) : std::abs(bands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)]);
                const float other = nCh > 1 ? (sidechain != nullptr ? std::abs(sidechainBands[static_cast<size_t>(band)][static_cast<size_t>(1 - ch)][static_cast<size_t>(i)]) : std::abs(bands[static_cast<size_t>(band)][static_cast<size_t>(1 - ch)][static_cast<size_t>(i)])) : own;
                const float link = std::clamp(params.stereoLink.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f);
                const float detector = nCh > 1 && link > 0.01f ? own * (1.0f - link) + std::max(own, other) * link : own;
                const float db = gainToDecibels(std::max(detector, 1.0e-5f));
                const float over = std::max(0.0f, db - threshold);
                const float reduction = over * (1.0f - 1.0f / ratio);
                const float target = decibelsToGain(-reduction);
                const float c = std::exp(-1.0f / ((target < envelope ? attack : release) * sampleRate));
                envelope = c * envelope + (1.0f - c) * target;
                if (!params.mbEnabled[static_cast<size_t>(band)].load(std::memory_order_relaxed) || params.mbBypass[static_cast<size_t>(band)].load(std::memory_order_relaxed)) envelope = 1.0f;
                bands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)] *= envelope * decibelsToGain(params.mbMakeup[static_cast<size_t>(band)].load(std::memory_order_relaxed));
                maxGr[static_cast<size_t>(band)] = std::min(maxGr[static_cast<size_t>(band)], gainToDecibels(envelope));
            }
        }
        for (int i = 0; i < nSamples; ++i)
        {
            float sum = 0.0f;
            for (int band = 0; band < kMultiCompBands; ++band) sum += bands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)];
            sum *= decibelsToGain(params.mbOutput.load(std::memory_order_relaxed));
            output[ch][i] = applyCoreDistortion(sum,
                                                static_cast<DistortionType>(std::clamp(params.distortion.load(std::memory_order_relaxed), 0, 3)),
                                                std::clamp(params.distortionAmount.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
        }
    }
    for (int band = 0; band < kMultiCompBands; ++band) bandGR[static_cast<size_t>(band)].store(maxGr[static_cast<size_t>(band)], std::memory_order_relaxed);
    masterGR.store(*std::min_element(maxGr.begin(), maxGr.end()), std::memory_order_relaxed);
    (void)mix; // The latency-aligned mix is applied by processBlock's shared 20 ms ramp.
}

void MultiCompDSP::updateCrossovers()
{
    const float f1 = std::clamp(params.crossover1.load(std::memory_order_relaxed), 20.0f, 500.0f);
    const float f2 = std::clamp(params.crossover2.load(std::memory_order_relaxed), f1 * 1.5f, 5000.0f);
    const float f3 = std::clamp(params.crossover3.load(std::memory_order_relaxed), f2 * 1.5f, 16000.0f);
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        if (firstBlock)
        {
            crossover1[ch].prepare(sampleRate, f1); crossover2[ch].prepare(sampleRate, f2); crossover3[ch].prepare(sampleRate, f3);
            scCrossover1[ch].prepare(sampleRate, f1); scCrossover2[ch].prepare(sampleRate, f2); scCrossover3[ch].prepare(sampleRate, f3);
        }
        else
        {
            crossover1[ch].setFrequency(f1); crossover2[ch].setFrequency(f2); crossover3[ch].setFrequency(f3);
            scCrossover1[ch].setFrequency(f1); scCrossover2[ch].setFrequency(f2); scCrossover3[ch].setFrequency(f3);
        }
    }
}

void MultiCompDSP::updateMeters(const float* const* in, float* const* out, int nCh, int nSamples)
{
    float inPeak = 0.0f, outPeak = 0.0f;
    for (int ch = 0; ch < nCh; ++ch) for (int i = 0; i < nSamples; ++i) { inPeak = std::max(inPeak, std::abs(in[ch][i])); outPeak = std::max(outPeak, std::abs(out[ch][i])); }
    inputLevel.store(inPeak > 1.0e-5f ? gainToDecibels(inPeak) : -60.0f, std::memory_order_relaxed);
    outputLevel.store(outPeak > 1.0e-5f ? gainToDecibels(outPeak) : -60.0f, std::memory_order_relaxed);
}

int MultiCompDSP::getLatencySamples() const noexcept
{
    const auto mode = static_cast<MultiCompMode>(std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7));
    if (mode == MultiCompMode::Multiband) return 0;
    const int lookahead = static_cast<int>(std::round(std::clamp(params.globalLookahead.load(std::memory_order_relaxed), 0.0f, 10.0f) * 0.001f * static_cast<float>(sampleRate)));
    const int digital = mode == MultiCompMode::Digital ? static_cast<int>(std::round(std::clamp(params.digitalLookahead.load(std::memory_order_relaxed), 0.0f, 10.0f) * 0.001f * static_cast<float>(sampleRate))) : 0;
    // DPF exposes the real base-rate group delay. The JUCE processor reported
    // a constant 59-sample dry-path value even when its nonlinear path was
    // bypassed or oversampling was disabled; the shell deliberately reports
    // the actual shared-dpf halfband delay per selected factor instead.
    const int oversampling = params.oversampling.load(std::memory_order_relaxed);
    const int antiAlias = oversampling == 2 ? 27 : oversampling == 1 ? 23 : 0;
    return antiAlias + lookahead + digital;
}

} // namespace duskaudio

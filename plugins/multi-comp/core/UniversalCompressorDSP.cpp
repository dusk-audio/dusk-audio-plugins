// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// UniversalCompressorDSP.cpp — top-level flow port. Transcribes the active
// subset of UniversalCompressor::prepareToPlay / processBlock (multicomp.cpp)
// for the Opto/FET/VCA/Bus modes at the parameter subset Dusk Studio drives,
// with everything else at its JUCE default. Judgment log: core/PORT_NOTES.md.

#include "UniversalCompressorDSP.hpp"

#include <cmath>
#include <cstring>

#include "DuskDenormals.hpp" // duskaudio::ScopedFlushDenormals

namespace duskaudio
{

// Invariant: modes are prepared at 2x the processing rate. The reference
// processor ships with its "oversampling" APVTS param at the default "2x" and
// internal oversampling disabled, so JUCE prepareToPlay primes every mode at
// rate*2 while processBlock runs them at the native rate (updateSampleRate only
// fires on a param change, which never happens). Envelope/filter coefficients
// depend on this.
static constexpr int kModePrepareOversampleFactor = 2;

// The reference's AntiAliasing::getLatency() in this config: the 4x
// FIR-equiripple oversampler latency, lround(59.5) = 60 base-rate samples,
// rate-independent (JUCE half-band equiripple max-quality stage orders
// 118/78 and 50/34; (118+78)/2 / 2 + (50+34)/2 / 4 = 59.5). The runtime audio
// path never runs the oversampler, but this value still sizes the reference's
// GR-meter delay ring (one slot per block).
static constexpr int kReportedAaLatencySamples = 60;

namespace
{
    inline float getPeakLevel (const float* data, int numSamples) noexcept
    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = jmax (peak, std::abs (data[i]));
        return peak;
    }
}

UniversalCompressorDSP::UniversalCompressorDSP() = default;

void UniversalCompressorDSP::prepare (double sr, int block)
{
    if (sr <= 0.0 || block <= 0)
        return;

    sr = jlimit (8000.0, 384000.0, sr);
    sampleRate = sr;
    maxBlock = block;
    numChannelsPrepared = 2; // Dusk drives the strip stereo (mono buffers reuse ch 0)
    wasBypassedLastBlock = false;

    const double modeRate  = sr * (double) kModePrepareOversampleFactor;
    const int    modeBlock = block * kModePrepareOversampleFactor;

    // Mode coefficients are computed for modeRate (see kModePrepareOversampleFactor).
    opto.prepare (modeRate, numChannelsPrepared);
    fet.prepare  (modeRate, numChannelsPrepared);
    vca.prepare  (modeRate, numChannelsPrepared);
    bus.prepare  (modeRate, numChannelsPrepared, modeBlock);

    // Native-rate services.
    sidechainFilter.prepare (sr, numChannelsPrepared);
    transientShaper.prepare (sr, numChannelsPrepared);
    lookupTables.initialize();

    for (int ch = 0; ch < 2; ++ch)
    {
        filteredSidechain[ch].assign ((size_t) block, 0.0f);
        linkedSidechain[ch].assign   ((size_t) block, 0.0f);
        normalDry[ch].assign         ((size_t) block, 0.0f);
    }
    smoothedGainBuffer.assign ((size_t) block, 1.0f);

    smoothedAutoMakeupGain.reset (sr, 0.05);
    smoothedAutoMakeupGain.setCurrentAndTargetValue (1.0f);

    // GR-based auto-gain smoothing coefficient (~200 ms window at the block rate).
    const float grTimeConstantSec = 0.043f;
    const int   grBlockSize = jmax (1, block);
    const double safeSampleRate = jlimit (8000.0, 384000.0, sr);
    const float blocksPerSecond = (float) safeSampleRate / (float) grBlockSize;
    grSmoothCoeff = 1.0f - std::exp (-1.0f / (blocksPerSecond * grTimeConstantSec));
    grSmoothCoeff = jlimit (0.001f, 0.999f, grSmoothCoeff);
    smoothedGrDb = 0.0f;
    primeGrAccumulator = true;
    lastMode = -1;

    // GR-meter delay: the reference delays the displayed GR by the blocks
    // covering its (constant) reported AA latency, even though the audio path
    // has none in this config.
    grDelayBuffer.fill (0.0f);
    grDelayWritePos = 0;
    grDelayBlocks = jmin ((kReportedAaLatencySamples + block - 1) / block, kMaxGrDelayBlocks - 1);

    // Constant-latency report: the reference reports the AA latency from
    // prepareToPlay onward regardless of the runtime path (updateLatencyReport;
    // AA latency + lookahead 0 + non-Digital 0). The wet path has no actual
    // delay; the bypass / 0%-mix / partial-mix dry paths are delayed by this
    // amount instead.
    latencySamples = kReportedAaLatencySamples;

    // Input-history ring for the latency-aligned dry output on bypass / 0% mix.
    // Sized like the reference: max reportable latency (AA 4x + 10 ms global
    // lookahead + 10 ms Digital lookahead) + one block + 1.
    {
        const int laMax  = jlimit (0, 4096, (int) std::ceil (sr * 0.01));
        const int digMax = jlimit (0, 4096, (int) std::ceil (sr * 0.01));
        bypassAlignSize = kReportedAaLatencySamples + laMax + digMax + block + 1;
        for (int ch = 0; ch < 2; ++ch)
            bypassAlignBuf[ch].assign ((size_t) bypassAlignSize, 0.0f);
        bypassAlignWritePos[0] = bypassAlignWritePos[1] = 0;
    }

    // Bypass -> active crossfade: base fade 5 ms; buffer sized for the 15 ms
    // maximum (Digital fade extension, unreachable here but kept for the
    // reference's min() clamps).
    bypassFadeLengthSamples = jlimit (64, 2048, (int) (sr * 0.005));
    {
        const int maxFadeSamples = jlimit (64, 8192, (int) (sr * 0.015));
        for (int ch = 0; ch < 2; ++ch)
            bypassFadeBuffer[ch].assign ((size_t) maxFadeSamples, 0.0f);
    }
    bypassFadeActualLength = bypassFadeLengthSamples;
    bypassFadeRemaining = 0;

    for (auto& chBuf : mixDelayBuffer)
        chBuf.fill (0.0f);
    mixDelayWritePos = 0;
}

void UniversalCompressorDSP::reset()
{
    opto.reset();
    fet.reset();
    vca.reset();
    bus.reset();
    sidechainFilter.reset();
    transientShaper.reset();

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (filteredSidechain[ch].begin(), filteredSidechain[ch].end(), 0.0f);
        std::fill (linkedSidechain[ch].begin(),   linkedSidechain[ch].end(),   0.0f);
        std::fill (normalDry[ch].begin(),         normalDry[ch].end(),         0.0f);
    }

    smoothedAutoMakeupGain.setCurrentAndTargetValue (1.0f);
    smoothedGrDb = 0.0f;
    primeGrAccumulator = true;
    lastMode = -1;

    grDelayBuffer.fill (0.0f);
    grDelayWritePos = 0;

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (bypassAlignBuf[ch].begin(), bypassAlignBuf[ch].end(), 0.0f);
        std::fill (bypassFadeBuffer[ch].begin(), bypassFadeBuffer[ch].end(), 0.0f);
    }
    bypassAlignWritePos[0] = bypassAlignWritePos[1] = 0;
    bypassFadeRemaining = 0;
    wasBypassedLastBlock = false;

    for (auto& chBuf : mixDelayBuffer)
        chBuf.fill (0.0f);
    mixDelayWritePos = 0;
}

void UniversalCompressorDSP::processBlock (const float* const* in, float* const* out, int numChannels, int numSamples)
{
    ScopedFlushDenormals noDenormals;

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Mirror JUCE's in-place buffer: copy input into the output, then process out.
    for (int ch = 0; ch < numChannels; ++ch)
        if (in[ch] != out[ch])
            std::memcpy (out[ch], in[ch], (size_t) numSamples * sizeof (float));

    // Oversized-block guard (RT-safe degradation): scratch is sized to maxBlock.
    if (numSamples > maxBlock)
        return; // out already holds the dry passthrough

    const int nc = jmin (numChannels, 2);

    // Feed the input-history ring every block so the bypass / 0%-mix passthrough
    // can emit dry audio delayed by the CONSTANT reported latency. preWp = the
    // per-channel write position BEFORE this block's write.
    const int alignPreWp0 = bypassAlignWritePos[0];
    const int alignPreWp1 = bypassAlignWritePos[1];
    if (bypassAlignSize > 1)
    {
        for (int ch = 0; ch < nc; ++ch)
        {
            int wp = bypassAlignWritePos[ch];
            const float* inData = out[ch];
            float* ring = bypassAlignBuf[ch].data();
            for (int i = 0; i < numSamples; ++i)
            {
                ring[wp] = inData[i];
                wp = (wp + 1) % bypassAlignSize;
            }
            bypassAlignWritePos[ch] = wp;
        }
    }

    // Overwrite out with the input delayed by the constant reported latency.
    // D + numSamples >= ring size (out-of-spec block) degrades to the undelayed
    // passthrough already in out — same fallback as the reference.
    auto emitLatencyAlignedDry = [&]()
    {
        const int D = jlimit (0, jmax (0, bypassAlignSize - 1), latencySamples);
        if (bypassAlignSize <= 1 || D + numSamples >= bypassAlignSize)
            return;
        for (int ch = 0; ch < nc; ++ch)
        {
            const int preWp = (ch == 0) ? alignPreWp0 : alignPreWp1;
            const float* ring = bypassAlignBuf[ch].data();
            float* o = out[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                const int rp = ((preWp + i - D) % bypassAlignSize + bypassAlignSize) % bypassAlignSize;
                o[i] = ring[rp];
            }
        }
    };

    // Bypass: dry output delayed by the constant reported latency (the
    // reference never zeroes its latency report on bypass).
    if (pBypass.load (std::memory_order_relaxed))
    {
        bypassFadeRemaining = 0;   // cancel any in-progress fade if re-bypassed
        wasBypassedLastBlock = true;
        emitLatencyAlignedDry();
        return;
    }

    // Bypass -> active transition: restart the crossfade and reset the
    // auto-makeup accumulators (the Digital fade extension is unreachable).
    if (wasBypassedLastBlock)
    {
        wasBypassedLastBlock = false;
        bypassFadeActualLength = bypassFadeLengthSamples;
        bypassFadeRemaining = bypassFadeActualLength;

        smoothedAutoMakeupGain.setCurrentAndTargetValue (1.0f);
        smoothedGrDb = 0.0f;
        primeGrAccumulator = true;
    }

    const float stereoLinkAmount = pStereoLink.load (std::memory_order_relaxed) * 0.01f;
    const float mixAmount        = pMix.load (std::memory_order_relaxed) * 0.01f;
    const bool  needsDryBuffer   = (mixAmount > 0.001f && mixAmount < 0.999f);

    // Save the dry signal for the bypass crossfade. No global lookahead in
    // scope, so the raw input is already time-aligned with the wet path
    // (the reference's delaySamples == 0 memcpy branch).
    if (bypassFadeRemaining > 0)
    {
        const int fadeSamples = jmin (bypassFadeRemaining,
                                      jmin (numSamples, (int) bypassFadeBuffer[0].size()));
        for (int ch = 0; ch < nc; ++ch)
            std::memcpy (bypassFadeBuffer[ch].data(), out[ch], (size_t) fadeSamples * sizeof (float));
    }

    // 100% dry: skip all processing; zero the GR meters; emit the dry input
    // delayed by the constant reported latency.
    if (mixAmount <= 0.001f)
    {
        linkedGainReduction[0].store (0.0f, std::memory_order_relaxed);
        linkedGainReduction[1].store (0.0f, std::memory_order_relaxed);
        grMeter.store (0.0f, std::memory_order_relaxed);
        emitLatencyAlignedDry();
        return;
    }

    const int modeInt = pMode.load (std::memory_order_relaxed);
    if (modeInt < 0 || modeInt > (int) CompMode::Bus)
        return; // unported mode -> passthrough (out already == input)
    const CompMode mode = (CompMode) modeInt;

    // sidechain_enable defaults false and is not driven by Dusk -> no external SC.
    const bool autoMakeup = pAutoMakeup.load (std::memory_order_relaxed);
    const bool hasExternalSidechain = false;

    // Reset auto-gain state on mode change (JUCE: lastCompressorMode).
    if (modeInt != lastMode)
    {
        lastMode = modeInt;
        primeGrAccumulator = true;
        smoothedAutoMakeupGain.setCurrentAndTargetValue (1.0f);
    }

    // --- per-mode parameter caching (mirrors the JUCE switch) -----------------
    float cp[8] = { 0.0f };
    switch (mode)
    {
        case CompMode::Opto:
            cp[0] = jlimit (0.0f, 100.0f, pOptoPeakReduction.load (std::memory_order_relaxed));
            if (autoMakeup) cp[1] = 0.0f;
            else            cp[1] = jlimit (-40.0f, 40.0f, (jlimit (0.0f, 100.0f, pOptoGain.load (std::memory_order_relaxed)) - 50.0f) * 0.8f);
            cp[2] = pOptoLimit.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
            break;

        case CompMode::FET:
            cp[0] = pFetInput.load (std::memory_order_relaxed);
            cp[1] = autoMakeup ? 0.0f : pFetOutput.load (std::memory_order_relaxed);
            cp[2] = pFetAttack.load (std::memory_order_relaxed);
            cp[3] = pFetRelease.load (std::memory_order_relaxed);
            cp[4] = (float) pFetRatio.load (std::memory_order_relaxed);
            cp[5] = (float) pFetCurveMode.load (std::memory_order_relaxed);
            cp[6] = pFetTransient.load (std::memory_order_relaxed);
            cp[7] = pFetThreshold.load (std::memory_order_relaxed);
            break;

        case CompMode::VCA:
            cp[0] = pVcaThreshold.load (std::memory_order_relaxed);
            cp[1] = pVcaRatio.load (std::memory_order_relaxed);
            cp[2] = pVcaAttack.load (std::memory_order_relaxed);
            cp[3] = pVcaRelease.load (std::memory_order_relaxed);
            cp[4] = autoMakeup ? 0.0f : pVcaOutput.load (std::memory_order_relaxed);
            cp[5] = pVcaOverEasy.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
            vca.setDetectorClassic (pVcaDetectorMode.load (std::memory_order_relaxed) >= 1); // JUCE: index > 0.5
            break;

        case CompMode::Bus:
        {
            cp[0] = pBusThreshold.load (std::memory_order_relaxed);
            const int ratioChoice = pBusRatio.load (std::memory_order_relaxed);
            cp[1] = (ratioChoice == 1) ? 4.0f : (ratioChoice == 2) ? 10.0f : 2.0f;
            cp[2] = (float) pBusAttack.load (std::memory_order_relaxed);
            cp[3] = (float) pBusRelease.load (std::memory_order_relaxed);
            cp[4] = autoMakeup ? 0.0f : pBusMakeup.load (std::memory_order_relaxed);
            cp[5] = pBusMix.load (std::memory_order_relaxed) * 0.01f;
            break;
        }
    }

    // --- input metering -------------------------------------------------------
    {
        float inLevel = 0.0f, inL = 0.0f, inR = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float peak = getPeakLevel (out[ch], numSamples);
            inLevel = jmax (inLevel, peak);
            if (ch == 0) inL = peak; else if (ch == 1) inR = peak;
        }
        inputMeter.store  (inLevel > 1e-5f ? gainToDb (inLevel) : -60.0f, std::memory_order_relaxed);
        inputMeterL.store (inL > 1e-5f ? gainToDb (inL) : -60.0f, std::memory_order_relaxed);
        inputMeterR.store (numChannels > 1 ? (inR > 1e-5f ? gainToDb (inR) : -60.0f)
                                           : (inL > 1e-5f ? gainToDb (inL) : -60.0f), std::memory_order_relaxed);
    }

    // --- sidechain HP filter (internal sidechain = the input) -----------------
    const float scHpFreq = pScHp.load (std::memory_order_relaxed);
    const bool  scHpEnabled = scHpFreq >= 1.0f;
    if (scHpEnabled)
        sidechainFilter.setFrequency (scHpFreq);

    for (int ch = 0; ch < nc; ++ch)
    {
        const float* inData = out[ch];
        float* sc = filteredSidechain[ch].data();
        if (scHpEnabled) sidechainFilter.processBlock (inData, sc, numSamples, ch);
        else             std::memcpy (sc, inData, (size_t) numSamples * sizeof (float));
    }
    // NOTE: sidechain shelf EQ + true-peak detection run in the JUCE path but are
    // no-ops at Dusk defaults (sc gains == 0 dB is exact unity; true_peak off).
    // See PORT_NOTES §Default-off services.

    // --- sidechain meter ------------------------------------------------------
    {
        float scLevel = 0.0f;
        for (int ch = 0; ch < nc; ++ch)
            scLevel = jmax (scLevel, getPeakLevel (filteredSidechain[ch].data(), numSamples));
        sidechainMeter.store (scLevel > 1e-5f ? gainToDb (scLevel) : -60.0f, std::memory_order_relaxed);
    }

    // --- stereo linking (Stereo mode only; M/S + Dual-Mono unported) ----------
    const int stereoLinkMode = pStereoLinkMode.load (std::memory_order_relaxed);
    const bool useStereoLink = (stereoLinkMode == 0 && stereoLinkAmount > 0.01f) && (numChannels >= 2);
    if (useStereoLink)
    {
        const float* scL = filteredSidechain[0].data();
        const float* scR = filteredSidechain[1].data();
        float* leftSC  = linkedSidechain[0].data();
        float* rightSC = linkedSidechain[1].data();
        for (int i = 0; i < numSamples; ++i)
        {
            const float lL = std::abs (scL[i]);
            const float lR = std::abs (scR[i]);
            const float linked = jmax (lL, lR);
            leftSC[i]  = lL * (1.0f - stereoLinkAmount) + linked * stereoLinkAmount;
            rightSC[i] = lR * (1.0f - stereoLinkAmount) + linked * stereoLinkAmount;
        }
    }

    // --- mode processing (native rate; internal oversampling disabled) --------
    const float compensationGain = 1.0f;

    bool canMixNormal = false;
    if (needsDryBuffer)
    {
        for (int ch = 0; ch < nc; ++ch)
            std::memcpy (normalDry[ch].data(), out[ch], (size_t) numSamples * sizeof (float));
        canMixNormal = true;
    }

    for (int ch = 0; ch < nc; ++ch)
    {
        float* data = out[ch];
        switch (mode)
        {
            case CompMode::Opto:
                for (int i = 0; i < numSamples; ++i)
                {
                    const float scSignal = useStereoLink ? linkedSidechain[ch][(size_t) i] : filteredSidechain[ch][(size_t) i];
                    data[i] = opto.process (data[i], ch, cp[0], cp[1], cp[2] > 0.5f, false, scSignal, hasExternalSidechain) * compensationGain;
                }
                break;

            case CompMode::FET:
                for (int i = 0; i < numSamples; ++i)
                {
                    const float scSignal = useStereoLink ? linkedSidechain[ch][(size_t) i] : filteredSidechain[ch][(size_t) i];
                    data[i] = fet.process (data[i], ch, cp[0], cp[1], cp[2], cp[3], (int) cp[4], false,
                                           &lookupTables, &transientShaper,
                                           cp[5] > 0.5f, cp[6], scSignal, hasExternalSidechain, cp[7]) * compensationGain;
                }
                break;

            case CompMode::VCA:
                for (int i = 0; i < numSamples; ++i)
                {
                    const float scSignal = useStereoLink ? linkedSidechain[ch][(size_t) i] : filteredSidechain[ch][(size_t) i];
                    data[i] = vca.process (data[i], ch, cp[0], cp[1], cp[2], cp[3], cp[4], cp[5] > 0.5f, false, scSignal, hasExternalSidechain) * compensationGain;
                }
                break;

            case CompMode::Bus:
                if (useStereoLink && numChannels >= 2 && ch == 0)
                {
                    bus.processStereoLinked (out[0], out[1], numSamples,
                                             cp[0], cp[1], (int) cp[2], (int) cp[3],
                                             cp[4], compensationGain, stereoLinkAmount,
                                             linkedSidechain[0].data(), linkedSidechain[1].data(),
                                             hasExternalSidechain);
                }
                else if (useStereoLink && numChannels >= 2 && ch == 1)
                {
                    // Already processed by the ch==0 lockstep call.
                }
                else
                {
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float scSignal = useStereoLink ? linkedSidechain[ch][(size_t) i] : filteredSidechain[ch][(size_t) i];
                        data[i] = bus.process (data[i], ch, cp[0], cp[1], (int) cp[2], (int) cp[3], cp[4], cp[5], false, scSignal, hasExternalSidechain) * compensationGain;
                    }
                }
                break;
        }
        // Output distortion is default-Off (not driven by Dusk) -> skipped.
    }

    // --- dry/wet mix (normal-rate tier). The dry signal is ring-delayed by the
    // constant reported latency before the crossfade, exactly like the
    // reference's mixAtNormalRate (totalDelay = AA latency + 0 + 0). ------------
    if (canMixNormal)
    {
        const float wetAmount = mixAmount;
        const float dryAmount = 1.0f - mixAmount;
        const int totalDelay = kReportedAaLatencySamples;
        const int delayToApply = jmin (totalDelay, kMixDelaySamples - 1);

        for (int i = 0; i < numSamples; ++i)
        {
            const int readPos = (mixDelayWritePos - delayToApply + kMixDelaySamples) & kMixDelayMask;
            for (int ch = 0; ch < nc; ++ch)
            {
                float* wet = out[ch];
                const float* dry = normalDry[ch].data();
                const float delayedDry = mixDelayBuffer[(size_t) ch][(size_t) readPos];
                mixDelayBuffer[(size_t) ch][(size_t) mixDelayWritePos] = dry[i];
                wet[i] = wet[i] * wetAmount + delayedDry * dryAmount;
            }
            mixDelayWritePos = (mixDelayWritePos + 1) & kMixDelayMask;
        }
    }

    // --- gain reduction read-back ---------------------------------------------
    float grLeft = 0.0f, grRight = 0.0f;
    switch (mode)
    {
        case CompMode::Opto: grLeft = opto.getGainReduction (0); grRight = (numChannels > 1) ? opto.getGainReduction (1) : grLeft; break;
        case CompMode::FET:  grLeft = fet.getGainReduction (0);  grRight = (numChannels > 1) ? fet.getGainReduction (1)  : grLeft; break;
        case CompMode::VCA:  grLeft = vca.getGainReduction (0);  grRight = (numChannels > 1) ? vca.getGainReduction (1)  : grLeft; break;
        case CompMode::Bus:  grLeft = bus.getGainReduction (0);  grRight = (numChannels > 1) ? bus.getGainReduction (1)  : grLeft; break;
    }
    linkedGainReduction[0].store (grLeft, std::memory_order_relaxed);
    linkedGainReduction[1].store (grRight, std::memory_order_relaxed);
    const float gainReduction = jmin (grLeft, grRight);

    // --- GR-based auto-makeup gain --------------------------------------------
    {
        float targetAutoGain = 1.0f;
        if (autoMakeup)
        {
            const float avgGrDb = (grLeft + grRight) * 0.5f;

            if (primeGrAccumulator)
            {
                smoothedGrDb = avgGrDb;
                primeGrAccumulator = false;

                float autoGainDb = -avgGrDb;
                if (mode == CompMode::FET)  autoGainDb -= cp[0];
                if (mode == CompMode::Opto) autoGainDb -= jmin (1.5f, std::abs (avgGrDb) * 0.25f);
                autoGainDb = jlimit (-40.0f, 40.0f, autoGainDb);
                float primedGain = dbToGain (autoGainDb);
                primedGain = 1.0f + (primedGain - 1.0f) * mixAmount;
                smoothedAutoMakeupGain.setCurrentAndTargetValue (primedGain);
            }
            else
            {
                smoothedGrDb += grSmoothCoeff * (avgGrDb - smoothedGrDb);
            }

            float autoGainDb = -smoothedGrDb;
            if (mode == CompMode::FET)  autoGainDb -= cp[0];
            if (mode == CompMode::Opto) autoGainDb -= jmin (1.5f, std::abs (smoothedGrDb) * 0.25f);
            autoGainDb = jlimit (-40.0f, 40.0f, autoGainDb);
            targetAutoGain = dbToGain (autoGainDb);
            targetAutoGain = 1.0f + (targetAutoGain - 1.0f) * mixAmount;
        }

        smoothedAutoMakeupGain.setTargetValue (targetAutoGain);

        if (smoothedAutoMakeupGain.isSmoothing())
        {
            const int samplesToProcess = jmin (numSamples, (int) smoothedGainBuffer.size());
            for (int i = 0; i < samplesToProcess; ++i)
                smoothedGainBuffer[(size_t) i] = smoothedAutoMakeupGain.getNextValue();
            for (int ch = 0; ch < nc; ++ch)
            {
                float* data = out[ch];
                for (int i = 0; i < samplesToProcess; ++i)
                    data[i] *= smoothedGainBuffer[(size_t) i];
            }
        }
        else
        {
            const float currentGain = smoothedAutoMakeupGain.getCurrentValue();
            if (std::abs (currentGain - 1.0f) > 0.001f)
                for (int ch = 0; ch < nc; ++ch)
                {
                    float* data = out[ch];
                    for (int i = 0; i < numSamples; ++i)
                        data[i] *= currentGain;
                }
        }
    }

    // Bypass -> active crossfade: blend the wet output against the dry captured
    // at the top of the block.
    if (bypassFadeRemaining > 0)
    {
        const int fadeSamples = jmin (bypassFadeRemaining, numSamples);
        const float fadeLen = (float) bypassFadeActualLength;
        for (int ch = 0; ch < nc; ++ch)
        {
            float* o = out[ch];
            const float* dry = bypassFadeBuffer[ch].data();
            for (int i = 0; i < fadeSamples; ++i)
            {
                const float wet = 1.0f - (float) (bypassFadeRemaining - i) / fadeLen;
                o[i] = o[i] * wet + dry[i] * (1.0f - wet);
            }
        }
        bypassFadeRemaining -= fadeSamples;
    }

    // --- output metering ------------------------------------------------------
    {
        float outLevel = 0.0f, outL = 0.0f, outR = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float peak = getPeakLevel (out[ch], numSamples);
            outLevel = jmax (outLevel, peak);
            if (ch == 0) outL = peak; else if (ch == 1) outR = peak;
        }
        outputMeter.store  (outLevel > 1e-5f ? gainToDb (outLevel) : -60.0f, std::memory_order_relaxed);
        outputMeterL.store (outL > 1e-5f ? gainToDb (outL) : -60.0f, std::memory_order_relaxed);
        outputMeterR.store (numChannels > 1 ? (outR > 1e-5f ? gainToDb (outR) : -60.0f)
                                            : (outL > 1e-5f ? gainToDb (outL) : -60.0f), std::memory_order_relaxed);
    }

    // Delay the displayed GR to match the reference's meter behaviour (one ring
    // slot per processBlock call, delay computed in prepare from the reference's
    // constant reported AA latency).
    float delayedGR = gainReduction;
    if (grDelayBlocks > 0)
    {
        grDelayBuffer[(size_t) grDelayWritePos] = gainReduction;
        const int readPos = (grDelayWritePos - grDelayBlocks + kMaxGrDelayBlocks) % kMaxGrDelayBlocks;
        delayedGR = grDelayBuffer[(size_t) readPos];
        grDelayWritePos = (grDelayWritePos + 1) % kMaxGrDelayBlocks;
    }
    grMeter.store (delayedGR, std::memory_order_relaxed);

    // GR history (UI 30 Hz) + analog noise are not mirrored: noise is forced off
    // by Dusk (noise_enable = 0); history is a pure UI convenience. See PORT_NOTES.
}

} // namespace duskaudio

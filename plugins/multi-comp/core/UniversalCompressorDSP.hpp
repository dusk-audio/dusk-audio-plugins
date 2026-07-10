// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// UniversalCompressorDSP.hpp — framework-free (no-JUCE) port of the Multi-Comp
// (UniversalCompressor) DSP, scoped to the four modes Dusk Studio's strips use:
// Opto, FET, VCA, Bus. Mirrors the driving logic in multicomp.cpp
// (prepareToPlay / processBlock) for those modes at the parameter subset Dusk
// Studio drives, with everything else at its JUCE default.
//
// RT contract:
//   - prepare() / reset()  : main thread. Allocate + size here.
//   - processBlock()       : real-time. No alloc / lock / IO. Denormals flushed.
//   - set*() setters        : atomic, callable from any thread.
//
// Scope, default-trace and substitution decisions: core/PORT_NOTES.md.

#pragma once

#include <atomic>
#include <vector>

#include "UniversalCompressorServices.hpp"
#include "OptoCompressor.hpp"
#include "FETCompressor.hpp"
#include "VCACompressor.hpp"
#include "BusCompressor.hpp"

namespace duskaudio
{

// Mode indices — match CompressorMode in UniversalCompressor.h (Opto/FET/VCA/Bus
// are the only in-scope values; 4..7 are unported and treated as passthrough).
enum class CompMode : int { Opto = 0, FET = 1, VCA = 2, Bus = 3 };

class UniversalCompressorDSP
{
public:
    UniversalCompressorDSP();

    // --- lifecycle (main thread) ---------------------------------------------
    void prepare (double sampleRate, int maxBlock);
    void reset();

    // --- audio (real-time) ---------------------------------------------------
    // Stereo or mono. When in != out, in is copied to out first, then processed
    // in place (mirrors the JUCE in-place buffer semantics). numChannels is 1 or 2.
    void processBlock (const float* const* in, float* const* out, int numChannels, int numSamples);

    // Reported processing latency in samples. Mirrors the reference's constant
    // report: 60 (the 4x AA FIR latency) even though the runtime wet path has
    // no actual delay — the bypass / 0%-mix / partial-mix dry paths ARE delayed
    // by this amount to match. See PORT_NOTES §Oversampling & latency.
    int getLatencySamples() const noexcept { return latencySamples; }

    //==========================================================================
    // Parameter setters. Ranges/units match the JUCE APVTS layout EXACTLY
    // (multicomp.cpp createParameterLayout). Values are stored raw (SI/index),
    // as Dusk Studio writes them to the donor's raw parameter atoms; the audio
    // thread applies the same in-process clamps the JUCE processBlock does.
    //==========================================================================

    // mode: 0=Opto, 1=FET, 2=VCA, 3=Bus (choice index).
    void setMode (int modeIndex) noexcept                { pMode.store (modeIndex, std::memory_order_relaxed); }
    // bypass: on/off.
    void setBypass (bool on) noexcept                    { pBypass.store (on, std::memory_order_relaxed); }
    // mix: 0..100 %  (dry/wet; 100 = fully wet).
    void setMix (float percent) noexcept                 { pMix.store (percent, std::memory_order_relaxed); }
    // auto_makeup: off/on (GR-based auto makeup gain).
    void setAutoMakeup (bool on) noexcept                { pAutoMakeup.store (on, std::memory_order_relaxed); }
    // sidechain_hp: 0..500 Hz (0 = filter bypassed).
    void setSidechainHp (float hz) noexcept              { pScHp.store (hz, std::memory_order_relaxed); }
    // stereo_link: 0..100 %  (max-level link amount).
    void setStereoLink (float percent) noexcept          { pStereoLink.store (percent, std::memory_order_relaxed); }
    // stereo_link_mode: 0=Stereo, 1=Mid-Side, 2=Dual-Mono (default 0; only Stereo ported).
    void setStereoLinkMode (int index) noexcept          { pStereoLinkMode.store (index, std::memory_order_relaxed); }

    // Opto
    void setOptoPeakReduction (float v) noexcept         { pOptoPeakReduction.store (v, std::memory_order_relaxed); } // 0..100
    void setOptoGain (float v) noexcept                  { pOptoGain.store (v, std::memory_order_relaxed); }          // 0..100 (50 = unity)
    void setOptoLimit (bool on) noexcept                 { pOptoLimit.store (on, std::memory_order_relaxed); }

    // FET
    void setFetInput (float db) noexcept                 { pFetInput.store (db, std::memory_order_relaxed); }         // -20..40 dB
    void setFetOutput (float db) noexcept                { pFetOutput.store (db, std::memory_order_relaxed); }        // -20..20 dB
    void setFetAttack (float ms) noexcept                { pFetAttack.store (ms, std::memory_order_relaxed); }        // 0.02..80 ms
    void setFetRelease (float ms) noexcept               { pFetRelease.store (ms, std::memory_order_relaxed); }       // 50..1100 ms
    void setFetRatio (int index) noexcept                { pFetRatio.store (index, std::memory_order_relaxed); }      // 0..4 (4:1..All)
    void setFetThreshold (float dbfs) noexcept           { pFetThreshold.store (dbfs, std::memory_order_relaxed); }   // -60..0 dBFS
    void setFetCurveMode (int index) noexcept            { pFetCurveMode.store (index, std::memory_order_relaxed); }  // 0=Modern (default), 1=Measured
    void setFetTransient (float percent) noexcept        { pFetTransient.store (percent, std::memory_order_relaxed); }// 0..100 % (default)

    // VCA
    void setVcaThreshold (float db) noexcept             { pVcaThreshold.store (db, std::memory_order_relaxed); }     // -38..12 dB
    void setVcaRatio (float ratio) noexcept              { pVcaRatio.store (ratio, std::memory_order_relaxed); }      // 1..120 :1
    void setVcaAttack (float ms) noexcept                { pVcaAttack.store (ms, std::memory_order_relaxed); }        // 0.1..50 ms
    void setVcaRelease (float ms) noexcept               { pVcaRelease.store (ms, std::memory_order_relaxed); }       // 10..5000 ms
    void setVcaOutput (float db) noexcept                { pVcaOutput.store (db, std::memory_order_relaxed); }        // -20..20 dB
    void setVcaOverEasy (bool on) noexcept               { pVcaOverEasy.store (on, std::memory_order_relaxed); }
    void setVcaDetectorMode (int index) noexcept         { pVcaDetectorMode.store (index, std::memory_order_relaxed); }// 0=Adaptive,1=Classic

    // Bus
    void setBusThreshold (float db) noexcept             { pBusThreshold.store (db, std::memory_order_relaxed); }     // -30..15 dB
    void setBusRatio (int index) noexcept                { pBusRatio.store (index, std::memory_order_relaxed); }      // 0..2 (2:1/4:1/10:1)
    void setBusAttack (int index) noexcept               { pBusAttack.store (index, std::memory_order_relaxed); }     // 0..5 choice
    void setBusRelease (int index) noexcept              { pBusRelease.store (index, std::memory_order_relaxed); }    // 0..4 choice
    void setBusMakeup (float db) noexcept                { pBusMakeup.store (db, std::memory_order_relaxed); }        // 0..20 dB
    void setBusMix (float percent) noexcept              { pBusMix.store (percent, std::memory_order_relaxed); }      // 0..100 %

    //==========================================================================
    // Metering (relaxed atomics, written on the audio thread, polled by the UI).
    // Mirrors UniversalCompressor's public accessors Dusk Studio reads.
    //==========================================================================
    float getGainReduction() const noexcept   { return grMeter.load (std::memory_order_relaxed); }       // dB (<=0)
    float getInputLevel() const noexcept       { return inputMeter.load (std::memory_order_relaxed); }    // dB
    float getOutputLevel() const noexcept      { return outputMeter.load (std::memory_order_relaxed); }   // dB
    float getInputLevelL() const noexcept      { return inputMeterL.load (std::memory_order_relaxed); }
    float getInputLevelR() const noexcept      { return inputMeterR.load (std::memory_order_relaxed); }
    float getOutputLevelL() const noexcept     { return outputMeterL.load (std::memory_order_relaxed); }
    float getOutputLevelR() const noexcept     { return outputMeterR.load (std::memory_order_relaxed); }
    float getSidechainLevel() const noexcept   { return sidechainMeter.load (std::memory_order_relaxed); }
    float getLinkedGainReduction (int ch) const noexcept
    {
        return (ch >= 0 && ch < 2) ? linkedGainReduction[ch].load (std::memory_order_relaxed) : 0.0f;
    }

private:
    // --- DSP units -----------------------------------------------------------
    OptoCompressor opto;
    FETCompressor  fet;
    VCACompressor  vca;
    BusCompressor  bus;

    SidechainFilter sidechainFilter;
    TransientShaper transientShaper;
    LookupTables    lookupTables;

    // --- config --------------------------------------------------------------
    double sampleRate = 0.0;
    int    maxBlock   = 0;
    int    numChannelsPrepared = 2;
    int    latencySamples = 0;

    // --- per-block scratch (sized in prepare, never resized on the audio thread)
    std::vector<float> filteredSidechain[2];
    std::vector<float> linkedSidechain[2];
    std::vector<float> normalDry[2];
    std::vector<float> smoothedGainBuffer;

    // --- auto-makeup state (native rate) -------------------------------------
    MultiplicativeSmoothedValue smoothedAutoMakeupGain { 1.0f };
    float smoothedGrDb = 0.0f;
    float grSmoothCoeff = 0.0f;
    bool  primeGrAccumulator = true;
    int   lastMode = -1;

    // --- GR-meter block-delay ring (mirrors the reference's grDelayBuffer:
    // one GR value per block, delayed by the blocks covering the reference's
    // constant reported AA latency) --------------------------------------------
    static constexpr int kMaxGrDelayBlocks = 256;
    std::array<float, kMaxGrDelayBlocks> grDelayBuffer {};
    int grDelayWritePos = 0;
    int grDelayBlocks = 0;

    // --- bypass / 0%-mix latency-aligned dry ring (mirrors bypassAlignBuf:
    // fed with raw input every block; bypass and 0%-mix read it back delayed by
    // the constant reported latency) -------------------------------------------
    std::vector<float> bypassAlignBuf[2];
    int bypassAlignWritePos[2] { 0, 0 };
    int bypassAlignSize = 0;

    // --- bypass -> active crossfade (mirrors the bypassFadeBuffer machinery;
    // the Digital-mode fade extension is unreachable in scope) -----------------
    std::vector<float> bypassFadeBuffer[2];
    int  bypassFadeLengthSamples = 256;
    int  bypassFadeActualLength = 256;
    int  bypassFadeRemaining = 0;
    bool wasBypassedLastBlock = false;

    // --- partial-mix dry-delay ring (mirrors DryWetMixer's tier-2 delayBuffer;
    // the dry signal is delayed by the constant reported latency before the
    // crossfade) ----------------------------------------------------------------
    static constexpr int kMixDelaySamples = 256;               // power of 2
    static constexpr int kMixDelayMask = kMixDelaySamples - 1;
    std::array<std::array<float, kMixDelaySamples>, 2> mixDelayBuffer {};
    int mixDelayWritePos = 0;

    // --- parameter atoms (defaults == JUCE APVTS defaults) -------------------
    std::atomic<int>   pMode { 0 };
    std::atomic<bool>  pBypass { false };
    std::atomic<float> pMix { 100.0f };
    std::atomic<bool>  pAutoMakeup { false };
    std::atomic<float> pScHp { 0.0f };
    std::atomic<float> pStereoLink { 100.0f };
    std::atomic<int>   pStereoLinkMode { 0 };

    std::atomic<float> pOptoPeakReduction { 0.0f };
    std::atomic<float> pOptoGain { 50.0f };
    std::atomic<bool>  pOptoLimit { false };

    std::atomic<float> pFetInput { 0.0f };
    std::atomic<float> pFetOutput { 0.0f };
    std::atomic<float> pFetAttack { 0.2f };
    std::atomic<float> pFetRelease { 400.0f };
    std::atomic<int>   pFetRatio { 0 };
    std::atomic<float> pFetThreshold { -10.0f };
    std::atomic<int>   pFetCurveMode { 0 };
    std::atomic<float> pFetTransient { 0.0f };

    std::atomic<float> pVcaThreshold { 0.0f };
    std::atomic<float> pVcaRatio { 4.0f };
    std::atomic<float> pVcaAttack { 1.0f };
    std::atomic<float> pVcaRelease { 100.0f };
    std::atomic<float> pVcaOutput { 0.0f };
    std::atomic<bool>  pVcaOverEasy { false };
    std::atomic<int>   pVcaDetectorMode { 0 };

    std::atomic<float> pBusThreshold { 0.0f };
    std::atomic<int>   pBusRatio { 0 };
    std::atomic<int>   pBusAttack { 2 };
    std::atomic<int>   pBusRelease { 1 };
    std::atomic<float> pBusMakeup { 0.0f };
    std::atomic<float> pBusMix { 100.0f };

    // --- meters --------------------------------------------------------------
    mutable std::atomic<float> grMeter { 0.0f };
    mutable std::atomic<float> inputMeter { -60.0f };
    mutable std::atomic<float> outputMeter { -60.0f };
    mutable std::atomic<float> inputMeterL { -60.0f };
    mutable std::atomic<float> inputMeterR { -60.0f };
    mutable std::atomic<float> outputMeterL { -60.0f };
    mutable std::atomic<float> outputMeterR { -60.0f };
    mutable std::atomic<float> sidechainMeter { -60.0f };
    mutable std::atomic<float> linkedGainReduction[2] { {0.0f}, {0.0f} };
};

} // namespace duskaudio

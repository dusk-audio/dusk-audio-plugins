// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbDSP — framework-free replacement for the JUCE DuskVerbProcessor glue.
//
// It owns exactly what the JUCE processor owned outside the engines themselves:
// the two DuskVerbEngine instances and the equal-power preset crossfade between
// them, the dry scratch and the dry/wet mix smoother, the ReverbDucker, the
// SixAPTank brightness state, the edge-detected parameter pushes, and the
// metering atomics. The engines in ../src/dsp are used verbatim and untouched.
//
// The processBlock() body is a line-for-line transcription of
// DuskVerbProcessor::processBlock(), in the same order and with the same
// arithmetic, because a per-preset null test against the JUCE build is the
// acceptance gate for this port. The only deliberate additions are:
//   * bypass is a ~30 ms crossfade to a bit-exact passthrough instead of an
//     instant switch (DAF playbook landmine 7). Settled-unbypassed takes the
//     identical code path with no extra arithmetic, so the null test is unaffected.
//   * the host tempo arrives via setHostBpm() instead of a JUCE playhead query.
//   * a small ring of per-block output-peak dB for the UI's decay-envelope trace.
//
// Threading: setParameter/getParameter and the metering getters are lock-free
// (relaxed). applyFactoryPresetConfig() is message-thread only; it publishes
// with release ordering and the audio thread picks the swap up with acquire, as
// the JUCE processor did.

#pragma once

#include "../src/dsp/DuskVerbEngine.h"
#include "../src/dsp/ReverbDucker.h"
#include "DuskVerbParamTable.hpp"

#include <atomic>
#include <vector>

struct FactoryPreset;

namespace duskverb
{

class DuskVerbDSP
{
public:
    DuskVerbDSP();

    // May be called repeatedly (rate / block-size changes). Safe to call twice
    // with identical arguments.
    void prepare (double sampleRate, int maxBlockSize);

    // The host has stopped processing. Drops the prepared configuration so the
    // next prepare() genuinely re-prepares, and NOTHING ELSE.
    //
    // This is deliberately not a full clear, because it is the exact behaviour
    // of the JUCE build's AudioProcessor::releaseResources() and the null test
    // against that build is this port's acceptance gate. DuskVerbEngine::prepare()
    // does NOT reset everything clearAllBuffers() does — the pre-delay ring, the
    // input diffuser, the early-reflection taps, the reflection-tap ring and the
    // transient-duck envelopes all survive a re-prepare — so a DAF build that
    // cleared them on deactivate came out of a host deactivate/reactivate
    // cleaner than the JUCE build and diverged from it. Measured: clearing here
    // cost Medium Drum Room -75 dB, Bright Hall -102 dB and Small Drum Room
    // -108 dB in the per-preset null test; not clearing makes all three
    // bit-identical on every stem.
    void releaseResources() noexcept;

    // Hard flush: clears both engines and every piece of shell state, then
    // releases the prepared configuration. Not on the host's deactivate path
    // (see above) — this is the "stop making noise now" entry point, used by the
    // core tests and available to any future panic/reset control.
    // Does not touch the parameter values.
    void reset();

    // In-place safe (out[ch] may alias in[ch]). numSamples <= 0 returns early.
    // numChannels 1 or 2; the reverb always runs in stereo internally and a
    // mono host layout gets the left channel (the JUCE build refuses mono
    // output entirely, so there is no parity obligation here).
    void processBlock (const float* const* in, float* const* out,
                       int numChannels, int numSamples) noexcept;
    void processBlock (const float* const* in, float* const* out,
                       int numInputs, int numOutputs, int numSamples) noexcept;

    bool canRecall() const noexcept { return presetFadeRemaining_ == 0; }

    // ── Parameters ──────────────────────────────────────────────────────────
    // `plainValue` is the value the JUCE APVTS would have exposed through
    // getRawParameterValue(), i.e. after DuskVerbParamTable's round trips.
    void setParameter (int index, float plainValue) noexcept
    {
        if (index < 0 || index >= kNumParams) return;
        params_[static_cast<size_t> (index)].store (plainValue, std::memory_order_relaxed);
    }
    float getParameter (int index) const noexcept
    {
        if (index < 0 || index >= kNumParams) return 0.0f;
        return params_[static_cast<size_t> (index)].load (std::memory_order_relaxed);
    }

    // ── Factory presets ─────────────────────────────────────────────────────
    // Message thread. The caller writes the preset's parameter values FIRST
    // (FactoryPreset::collectParameters), then calls this, exactly as
    // DuskVerbProcessor::applyFactoryPreset() did.
    void applyFactoryPresetConfig (const FactoryPreset& preset);

    // Message thread, STATE-RESTORE path. Arms a swap that reinstalls `preset`'s
    // name-keyed engine config (PostTankEQ bands, modulation topology, FDN base
    // delays) without touching the parameter values or the SixAP brightness
    // state — the restored session's own values must survive. Pass nullptr when
    // the session carries no (or an unknown) preset identity: the swap then
    // resets that engine config to defaults, so a preset previously applied to
    // this reused instance cannot leak into the restored session.
    //
    // This is the JUCE setStateInformation() behaviour exactly, including the
    // consequence that a matched preset's applyEngineConfig() re-asserts the
    // PRESET's SixAP brightness over the state's at swap time.
    void restorePresetIdentity (const FactoryPreset* preset);

    const FactoryPreset* lastAppliedPreset() const noexcept
    { return lastAppliedPreset_.load (std::memory_order_acquire); }

    // Per-preset SixAPTank brightness/density. Not parameters (not automation
    // targets) but they travel with the session, so the shell persists them.
    struct SixAPBrightnessState
    {
        float densityBaseline = 0.62f;
        float bloomCeiling    = 0.85f;
        float bloomStagger[6] = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f };
        float earlyMix        = 0.5f;
        float outputTrim      = 1.3f;
    };
    // Audio thread, or while processing is stopped. The complete control
    // snapshot is installed together, before any engine sees the new values.
    void stageSound(const std::array<float, kNumParams>& plain,
                    const FactoryPreset* preset, const SixAPBrightnessState& sixAP) noexcept;
    SixAPBrightnessState getSixAPBrightness() const { return sixAPBrightness_; }
    // Message thread, before audio starts or between blocks: pushes to both
    // engines, matching setStateInformation()'s behaviour.
    void setSixAPBrightness (const SixAPBrightnessState& s);

    // Host tempo for the tempo-synced pre-delay. Call once per block before
    // processBlock(); `valid` false keeps the free-running pre-delay value.
    void setHostBpm (double bpm, bool valid) noexcept
    { hostBpm_ = bpm; hostBpmValid_ = valid; }

    // The JUCE build reports zero latency; so does this one.
    int getLatencySamples() const noexcept { return 0; }

    // ── Metering (audio thread writes, UI thread reads) ─────────────────────
    float getInputLevelL()  const noexcept { return inputLevelL_.load (std::memory_order_relaxed); }
    float getInputLevelR()  const noexcept { return inputLevelR_.load (std::memory_order_relaxed); }
    float getOutputLevelL() const noexcept { return outputLevelL_.load (std::memory_order_relaxed); }
    float getOutputLevelR() const noexcept { return outputLevelR_.load (std::memory_order_relaxed); }

    // Rolling history of output peak (dB) at 15 Hz, oldest first, for the
    // output-history display. Returns the number of frames written. The JUCE
    // editor sampled getOutputLevel* on a 15 Hz timer into its own 200-frame
    // ring; publishing the ring here makes the trace independent of the UI
    // frame rate and survives a UI that is closed and reopened.
    static constexpr int kTailHistorySize = 256;
    int getTailHistory (float* dest, int maxCount) const noexcept;

private:
    void forcePushAllParametersTo (DuskVerbEngine* target);
    void syncParameterCacheToCurrent();
    void performPresetSwap();
    void pushSixAPBrightnessTo (DuskVerbEngine& target);
    void pushTailFrame (float db) noexcept;
    void sampleTail(const float* left, const float* right, int count) noexcept;
    void clearTail() noexcept;
    static void writeOut (float* const* out, int numChannels,
                          const float* left, const float* right, int numSamples) noexcept;

    float p (int index) const noexcept
    { return params_[static_cast<size_t> (index)].load (std::memory_order_relaxed); }

    std::array<std::atomic<float>, kNumParams> params_ {};

    DuskVerbEngine engineA_, engineB_;
    DuskVerbEngine* activeEngine_   = &engineA_;
    DuskVerbEngine* previousEngine_ = nullptr;

    std::atomic<bool> pendingPresetSwap_ { false };
    int presetFadeRemaining_ = 0;
    int presetFadeTotal_     = 0;
    std::atomic<const FactoryPreset*> lastAppliedPreset_ { nullptr };

    std::vector<float> fadeBufL_, fadeBufR_;
    std::vector<float> dryBufL_, dryBufR_;
    std::vector<float> workL_, workR_;
    OnePoleSmoother mixSmoother_;
    ReverbDucker ducker_;

    SixAPBrightnessState sixAPBrightness_;

    double hostBpm_ = 0.0;
    bool   hostBpmValid_ = false;

    // Bypass crossfade. 0 = fully processing, 1 = fully passthrough.
    float bypassFade_     = 0.0f;
    float bypassFadeStep_ = 1.0f;

    // Cached PostTankEQ centres, filled at preset-swap time.
    float pteqBandFreq_[4] {  80.0f,  500.0f, 3000.0f, 10000.0f };
    float pteqBandQ_   [4] {   1.5f,    1.0f,    1.5f,     0.8f };
    float lastPteqBand0Gain_ = 0.0f, lastPteqBand1Gain_ = 0.0f;
    float lastPteqBand2Gain_ = 0.0f, lastPteqBand3Gain_ = 0.0f;

    // Edge-detected last-pushed values. Sentinels match the JUCE processor's.
    int   cachedAlgorithm_ = -1;
    float lastDecaySec_ = -1.0f, lastSize_ = -1.0f, lastDamping_ = -1.0f, lastBassMult_ = -1.0f;
    float lastDpvHfShelfDb_ = 9999.0f, lastDpvHfShelfHz_ = 9999.0f, lastDpvStructHfDamp_ = 9999.0f;
    float lastDpvBoxCutDb_ = 9999.0f, lastDpvBoxCutHz_ = 9999.0f;
    float lastDpvBassShelfDb_ = 9999.0f, lastDpvBassShelfHz_ = 9999.0f;
    float lastMidMult_ = -1.0f, lastSubMult_ = -1.0f, lastHiMidMult_ = -1.0f;
    float lastCrossoverSub_ = -1.0f, lastCrossoverAir_ = -1.0f;
    float lastShaperDepth_ = -1.0f, lastShaperTime_ = -1.0f;
    float lastShaperXover_ = -1.0f, lastShaperSens_ = -1.0f;
    float lastInputSubGain_ = -999.0f, lastInputMidGain_ = -999.0f, lastInputHighGain_ = -999.0f;
    float lastCrossover_ = -1.0f, lastHighCrossover_ = -1.0f, lastBassChoke_ = -1.0f;
    float lastSaturation_ = -1.0f, lastDiffusion_ = -1.0f, lastTonalCorr_ = -1.0f;
    float lastModDepth_ = -1.0f, lastModRate_ = -1.0f;
    float lastTailSpinDepth_ = -1.0f, lastTailSpinRate_ = -1.0f;
    float lastERSize_ = -1.0f, lastERLevel_ = -2.0f, lastERBoost_ = -1.0f;
    float lastQtHiMidMult_ = -99.0f, lastQtAirMult_ = -99.0f;
    float lastERRise_ = -1.0f, lastERBusLow_ = -99.0f, lastERBusHigh_ = -99.0f;
    float lastTankLevel_ = -1.0f, lastTankSplitHz_ = -1.0f;
    float lastERStereoNeutral_ = -1.0f, lastERDecorr_ = -1.0f, lastXTalk_ = -1.0f;
    bool  lastMbEnable_ = false;
    float lastMbLow_ = -1.0f, lastMbMid_ = -1.0f, lastMbHigh_ = -1.0f;
    float lastPreDelayMs_ = -1.0f, lastMix_ = -1.0f;
    float lastLoCut_ = -1.0f, lastHiCut_ = -1.0f, lastHiCutShelfDb_ = 999.0f;
    float lastWidth_ = -1.0f, lastGainTrim_ = -999.0f;
    float lastMonoBelow_ = -1.0f, lastMonoBelowDepth_ = -1.0f;
    bool  lastFreeze_ = false, haveLastFreeze_ = false;
    bool  lastGateEnabled_ = true, haveLastGateEnabled_ = false;

    float lastPostBandSub_ = 0.0f, lastPostBandLowMid_ = 0.0f;
    float lastPostBandMidHi_ = 0.0f, lastPostBandAir_ = 0.0f;
    float lastEDTSubAtk_ = 0.0f,    lastEDTSubTau_ = 100.0f;
    float lastEDTLowMidAtk_ = 0.0f, lastEDTLowMidTau_ = 100.0f;
    float lastEDTMidHiAtk_ = 0.0f,  lastEDTMidHiTau_ = 100.0f;
    float lastEDTAirAtk_ = 0.0f,    lastEDTAirTau_ = 100.0f;
    float lastInLoopPeakHz_ = 1000.0f, lastInLoopPeakQ_ = 2.0f, lastInLoopPeakDb_ = 0.0f;
    float lastBassShelfFastFc_ = 400.0f, lastBassShelfSlowFc_ = 200.0f;
    float lastBassShelfFastDb_ = 0.0f,   lastBassShelfSlowDb_ = 0.0f;
    float lastBassShelfTransition_ = 100.0f;

    double preparedSampleRate_ = 0.0;
    int    preparedBlockSize_  = 0;

    std::atomic<float> inputLevelL_  { -100.0f };
    std::atomic<float> inputLevelR_  { -100.0f };
    std::atomic<float> outputLevelL_ { -100.0f };
    std::atomic<float> outputLevelR_ { -100.0f };

    std::atomic<float> tailHistory_[kTailHistorySize] {};
    std::atomic<unsigned> tailWriteIndex_ { 0 };
    int tailSamplesPerFrame_ = 3200; // 15 Hz at 48 kHz; set in prepare()
    int tailSamples_ = 0;
    float tailPeak_ = 0.0f;
};

} // namespace duskverb

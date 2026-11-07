// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbDSP — framework-free replacement for the JUCE DuskVerbProcessor glue.
//
// It owns exactly what the JUCE processor owned outside the engines themselves:
// the two DuskVerbEngine instances and the equal-power preset crossfade between
// them, the dry scratch and the dry/wet mix smoother, the ReverbDucker, the
// SixAPTank brightness state, the edge-detected parameter pushes, and the
// metering atomics. The engines in ../src/dsp are used verbatim; the only things
// added there for DuskVerb 2 are inline read-only views and forwarders the legacy
// build never calls.
//
// The processBlock() body is a line-for-line transcription of
// DuskVerbProcessor::processBlock(), in the same order and with the same
// arithmetic, because a per-preset null test against the JUCE build is the
// acceptance gate for this port. The only deliberate additions are:
//   * bypass is a ~30 ms crossfade to a bit-exact passthrough instead of an
//     instant switch (DAF playbook landmine 7). Settled-unbypassed takes the
//     identical code path with no extra arithmetic, so the null test is unaffected.
//   * the host tempo arrives via setHostBpm() instead of a JUCE playhead query.
//   * Decay reads the measured tail on the presets DuskVerbPresets.hpp calibrates
//     (kDecayShownByName): at that reading the engines are given the row's own decay, and
//     anything else in proportion (engineDecay()), so the null test is unaffected.
//   * the damping card stays live on the engines whose presets replace the three-band
//     damping (DuskVerbDampingScaler.hpp). With the knobs at the preset's own values it
//     pushes nothing, so the null test is unaffected.
//   * an early field around each shared engine (DuskVerbEarlyField.hpp), configured per
//     preset by DuskVerbDv2Voicing.hpp. Its post path meets the engine's output controls in
//     a Dv2OutputShell and is crossfaded on its own, after the engines. A preset without one
//     runs its engine alone, through the same code as before, so the null test is unaffected.
//   * a static octave trim on the summed wet (DuskVerbOctaveTrim.hpp), per preset from the same
//     voicing, after the early fields and before the ducker, crossfaded over the preset fade. A
//     preset without one has a flat trim, which never runs, so the null test is unaffected.
//   * a band limit right after the trim (DuskVerbBandLimit.hpp): a low-pass with a lift under it,
//     per preset from the same voicing, crossfaded over the preset fade. A preset without one
//     runs no section, so the null test is unaffected.
//   * an engine patch per preset from the same voicing: shared-engine features the legacy build
//     never turns on (today the ParallelMultibandTank's decorrelated side), set after the shared
//     config at every swap. A preset without one gets them all off, as the legacy build runs.
//
// Threading: setParameter/getParameter and the metering getters are lock-free
// (relaxed). applyFactoryPresetConfig() is message-thread only; it publishes
// with release ordering and the audio thread picks the swap up with acquire, as
// the JUCE processor did.

#pragma once

#include "../src/dsp/DuskVerbEngine.h"
#include "../src/dsp/ReverbDucker.h"
#include "DuskVerbAlgorithms.hpp"
#include "DuskVerbBandLimit.hpp"
#include "DuskVerbDampingScaler.hpp"
#include "DuskVerbEarlyField.hpp"
#include "DuskVerbOctaveTrim.hpp"
#include "DuskVerbParamTable.hpp"
#include "Dv2OutputShell.hpp"
#include "Dv2StereoBands.hpp"
#include "dsp/NonLinBurstEngine.h"

#include <atomic>
#include <vector>

struct FactoryPreset;

namespace duskverb
{

class DuskVerbDSP
{
public:
    // `earlyFieldOverride`, when given, replaces the voicing table's early field for every
    // preset that loads (a preset-less session still gets none), as the DUSKVERB2_EARLYFIELD
    // tuning hook does in a DUSKVERB2_TUNING_HOOKS build; it wins over that hook. Copied here.
    // `octaveTrimOverride` does the same for the octave trim (DUSKVERB2_OCTAVETRIM); its gains
    // keep OctaveTrim::valid(). `bandLimitOverride` does it for the band limit
    // (DUSKVERB2_BANDLIMIT); it keeps BandLimit::valid().
    explicit DuskVerbDSP (const EarlyFieldConfig* earlyFieldOverride = nullptr,
                          const OctaveTrimGains* octaveTrimOverride = nullptr,
                          const BandLimitSetting* bandLimitOverride = nullptr);

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

    // Hard flush: clearState(), then releases the prepared configuration. Not on
    // the host's deactivate path (see above); used by the core tests.
    // Does not touch the parameter values.
    void reset();

    // The realtime half of reset(), for a host reset (CLAP reset, AU Reset) on the
    // audio thread: clears both engines, the active own voice and every piece of
    // shell state, without allocating. Keeps the prepared configuration, so
    // free-running modulators keep their phase (only a re-prepare resets them, and
    // that allocates). Does not touch the parameter values.
    //
    // Measured 2026-10-24 after a full-scale impulse, first block after the reset:
    // every program under -100 dBFS. Most reach the 1e-14 floor; Large Chamber
    // keeps a residual near -112 dBFS and the Dense Hall presets and Tiled Room
    // near -120 dBFS, from engine state not yet located (the shared engine is
    // frozen with the legacy build; its output match EQ, which left up to
    // -53 dBFS, is now cleared there).
    void clearState() noexcept;

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

    // The decay time the engines are given. Decay itself, except while a preset whose Decay reads
    // its measured tail (kDecayShownByName) plays on its own engine: then the row's decay at that
    // reading, and in proportion to Decay elsewhere. Audio thread (tests: between blocks).
    float engineDecay() const noexcept;

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

private:
    // DuskVerb 2's own engines (algorithm >= kNumSharedAlgorithms) run outside the shared
    // DuskVerbEngine. One sits beside each shared engine of the pair, so a preset swap or a
    // change of engine family crossfades exactly as a swap between two shared engines does:
    // while `active` is set, that member of the pair renders through its own voice instead,
    // and its shared engine sits idle. A voice only becomes active through armOwnVoice(),
    // which clears it, so an idle voice never needs clearing (clearState() skips it).
    struct OwnVoice
    {
        NonLinBurstEngine nonLin;
        Dv2OutputShell shell;
        bool active = false;
        bool needsSnap = false;      // armed since the last push: start at the targets, no glide
    };
    // The early field each engine of the pair plays, and the output stage its post path runs
    // through: added after the engine, the field would otherwise miss the engine's Lo Cut, Hi
    // Cut, Mono Below, Width and Gain Trim. Each engine has its own, so the one fading out keeps
    // its field, config and controls, as its engine does.
    struct FieldVoice
    {
        EarlyField field;
        Dv2OutputShell shell;
        bool needsSnap = false;      // started over since the last push: land on the controls, no glide
    };
    OwnVoice& ownFor (const DuskVerbEngine* engine) noexcept { return engine == &engineB_ ? ownB_ : ownA_; }
    DampingScaler& scalerFor (const DuskVerbEngine* engine) noexcept { return engine == &engineB_ ? scalerB_ : scalerA_; }
    FieldVoice& fieldFor (const DuskVerbEngine* engine) noexcept { return engine == &engineB_ ? fieldB_ : fieldA_; }
    // Whether `engine`'s field reaches the output: it has a post path, and the engine is a
    // shared one (an own voice never plays a field).
    bool fieldPostPlays (const DuskVerbEngine* engine) noexcept
    { return fieldFor (engine).field.postEnabled() && ! ownFor (engine).active; }
    // The early field `preset` plays (nullptr: none), and installing it on `engine`'s voice.
    const EarlyFieldConfig& earlyFieldConfigFor (const FactoryPreset* preset) const noexcept;
    // The octave trim `preset` plays (nullptr: flat).
    const OctaveTrimGains& octaveTrimGainsFor (const FactoryPreset* preset) const noexcept;
    // The band limit `preset` plays (nullptr: none).
    const BandLimitSetting& bandLimitSettingFor (const FactoryPreset* preset) const noexcept;
    // The band width stage's gains for `preset`: the hook's, the patch's, or off.
    const StereoBands::Gains& stereoBandsFor (const FactoryPreset* preset) const noexcept;
    void installEarlyField (const DuskVerbEngine* engine, const FactoryPreset* preset,
                            const DuskVerbEngine* outgoing) noexcept;
    // Takes `preset`'s Decay calibration for engineDecay() (nullptr, or a preset without one: none).
    void installDecayCalibration (const FactoryPreset* preset) noexcept;
    // DuskVerb 2's own settings of the shared engine for `preset` (DuskVerbDv2Voicing.hpp
    // kEnginePatchByName), after its shared config; nullptr or no row: every feature off.
    void applyEnginePatch (DuskVerbEngine& engine, const FactoryPreset* preset) noexcept;
    // The damping card as the engines receive it: Treble with the Tone macro folded in.
    DampingKnobs dampingKnobs (float toneTreble) const noexcept
    { return { p (BassMult), p (MidMult), p (Damping) * toneTreble, p (Crossover), p (HighCrossover) }; }
    void armOwnVoice (const DuskVerbEngine* engine, bool own) noexcept;
    void pushShellParameters (Dv2OutputShell& shell, float toneHiCut) noexcept;
    void pushOwnParameters (OwnVoice& voice, float preDelayMs, float toneTreble, float toneHiCut) noexcept;
    void pushFieldParameters (FieldVoice& voice, float toneHiCut) noexcept;
    void renderWet (DuskVerbEngine* engine, float* left, float* right, int numSamples) noexcept;
    void addEarlyFields (float* left, float* right, int numSamples,
                         const DuskVerbEngine* fadingOut, int fadeIndex) noexcept;

    void forcePushAllParametersTo (DuskVerbEngine* target);
    void syncParameterCacheToCurrent();
    void performPresetSwap();
    void pushSixAPBrightnessTo (DuskVerbEngine& target);
    static void writeOut (float* const* out, int numChannels,
                          const float* left, const float* right, int numSamples) noexcept;

    // The engine the Algorithm parameter selects: a Reserved slot resolves to its stand-in.
    int algorithm() const noexcept { return resolveAlgorithm (static_cast<int> (p (Algorithm))); }

    float p (int index) const noexcept
    { return params_[static_cast<size_t> (index)].load (std::memory_order_relaxed); }

    std::array<std::atomic<float>, kNumParams> params_ {};

    DuskVerbEngine engineA_, engineB_;
    OwnVoice ownA_, ownB_;
    DampingScaler scalerA_, scalerB_;    // one per engine: each tracks what its engine holds
    // Both fields read one input, written every block whichever field plays, so a field installed
    // at a swap starts with the whole history instead of its taps coming in one by one.
    EarlyFieldInput earlyInput_;
    FieldVoice fieldA_, fieldB_;
    EarlyFieldConfig earlyFieldOverride_;
    bool hasEarlyFieldOverride_ = false;
    // One trim on the summed wet, whichever engine plays: it holds both cascades of a swap.
    OctaveTrim octaveTrim_;
    StereoBands stereoBands_;            // the engine patch's band width stage, on the summed wet
    OctaveTrimGains octaveTrimOverride_ {};
    bool hasOctaveTrimOverride_ = false;
    // One band limit after the trim, holding both cascades of a swap like it.
    BandLimit bandLimit_;
    BandLimitSetting bandLimitOverride_ {};
    bool hasBandLimitOverride_ = false;
    // The installed preset's Decay calibration, both values as the DSP holds them after a recall:
    // Decay at decayShown_ gives the engines decayEngine_, while engine decayAlgorithm_ plays
    // (-1: no calibration).
    float decayShown_ = 0.0f, decayEngine_ = 0.0f;
    int decayAlgorithm_ = -1;
    int pmbSideOverride_ = -1;           // DUSKVERB2_PMBSIDE (hook builds): -1 the table, else 0 or 1
    float sparseERLpOverride_ = -1.0f;   // DUSKVERB2_SPARSEERLP (hook builds): below 0 the table, else the corner (0: off)
    std::array<float, 2> dualSlopeOverride_ {};   // DUSKVERB2_DUALSLOPE (hook builds)
    bool hasDualSlopeOverride_ = false;
    std::array<float, 9> octaveDecayOverride_ {};   // DUSKVERB2_OCTDECAY (hook builds)
    bool hasOctaveDecayOverride_ = false;
    bool sharedModeSmearHook_ = false;   // DUSKVERB_MODESMEAR set (hook builds): the patch leaves the smear to it
    bool sharedDensityJitterHook_ = false;   // DUSKVERB_DENSJIT set (hook builds): the patch leaves the jitter to it
    bool sharedFdnDelaysHook_ = false;   // DUSKVERB_FDN_DELAYS set (hook builds): the patch leaves the delays to it
    std::array<float, 2> fbRotationOverride_ {};   // DUSKVERB2_FBROT (hook builds)
    bool hasFbRotationOverride_ = false;
    std::array<float, 6> pmbSmearShareOverride_ {};   // DUSKVERB2_PMBSMEARSHARE (hook builds)
    bool hasPmbSmearShareOverride_ = false;
    std::array<float, 6> pmbDensifierOverride_ {};    // DUSKVERB2_PMBAPK (hook builds)
    bool hasPmbDensifierOverride_ = false;
    std::array<float, 6> pmbLevelOverride_ {};        // DUSKVERB2_PMBLEVEL (hook builds)
    bool hasPmbLevelOverride_ = false;
    StereoBands::Gains stereoBandsOverride_ = StereoBands::kOff;   // DUSKVERB2_STEREOBANDS (hook builds)
    bool hasStereoBandsOverride_ = false;
    DuskVerbEngine* activeEngine_   = &engineA_;
    DuskVerbEngine* previousEngine_ = nullptr;

    std::atomic<bool> pendingPresetSwap_ { false };
    int presetFadeRemaining_ = 0;
    int presetFadeTotal_     = 0;
    std::atomic<const FactoryPreset*> lastAppliedPreset_ { nullptr };

    std::vector<float> fadeBufL_, fadeBufR_;
    std::vector<float> dryBufL_, dryBufR_;
    std::vector<float> workL_, workR_;
    std::vector<float> earlyInL_, earlyInR_, earlyOutL_, earlyOutR_;   // the fields' post paths, fading in and out
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
};

} // namespace duskverb

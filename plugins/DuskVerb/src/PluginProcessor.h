#pragma once

#include "dsp/DuskVerbEngine.h"
#include "dsp/ReverbDucker.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

struct FactoryPreset;

class DuskVerbProcessor : public juce::AudioProcessor
{
public:
    DuskVerbProcessor();
    ~DuskVerbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    // VST3 program API wired to FactoryPresets list. Lets hosts + the
    // render harness route a preset apply through applyFactoryPreset() so
    // the full engine config (sixAP brightness, DPV shelves, PostTankEQ
    // bands) gets installed alongside the APVTS parameter values.
    int getNumPrograms() override;
    int getCurrentProgram() override                           { return currentProgram_; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam_; }

    juce::AudioProcessorValueTreeState parameters;

    // Combined preset apply: routes APVTS values + engine-specific tunables
    // (SixAPTank brightness/density) through one entry point so the editor
    // doesn't need to know about engine internals. Caches the per-preset
    // engine config so it survives state save/load.
    void applyFactoryPreset (const FactoryPreset& preset);

    // Level meters (audio thread writes, UI thread reads).
    float getInputLevelL()  const { return inputLevelL_.load (std::memory_order_relaxed); }
    float getInputLevelR()  const { return inputLevelR_.load (std::memory_order_relaxed); }
    float getOutputLevelL() const { return outputLevelL_.load (std::memory_order_relaxed); }
    float getOutputLevelR() const { return outputLevelR_.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Dual-engine crossfade architecture. Both engines stay prepared for the
    // session. Normally only activeEngine_ runs; when applyFactoryPreset()
    // fires, the audio thread copies all current parameters into the idle
    // engine, swaps the pointers, and runs both engines in parallel for a
    // short equal-power crossfade window so the old preset's reverb tail
    // decays naturally while the new preset's tail builds up. Eliminates
    // the click that was caused by simultaneous (a) buffer-clear on the
    // algorithm swap inside DuskVerbEngine::setAlgorithm() and (b) instant
    // parameter snaps on every tank-coefficient setter.
    DuskVerbEngine engineA_, engineB_;
    DuskVerbEngine* activeEngine_   = &engineA_;
    DuskVerbEngine* previousEngine_ = nullptr;  // valid only during fade-out

    // Set by applyFactoryPreset() on the message thread, consumed at the top
    // of the next processBlock() on the audio thread. Release/acquire ordering
    // ensures the SixAPBrightnessState writes that precede the flag-set are
    // visible to the audio thread when it picks up the swap.
    std::atomic<bool> pendingPresetSwap_ { false };

    // Crossfade state (audio thread only).
    int presetFadeRemaining_ = 0;
    int presetFadeTotal_     = 0;

    // VST3 program API state (message thread only — host calls these on
    // the message thread). Indexed into getFactoryPresets() vector.
    int currentProgram_ = 0;

    // Last factory preset applied via applyFactoryPreset(). Read from the
    // audio thread inside performPresetSwap() to run preset.applyEngineConfig()
    // on the newly active engine — that's where modulation topology and the
    // PostTankEQ band overrides actually install (forcePushAllParametersTo
    // only handles APVTS-routed parameters). Nullptr until first apply.
    std::atomic<const FactoryPreset*> lastAppliedPreset_ { nullptr };

    // Scratch buffers for the previous engine's output during a fade. Sized
    // in prepareToPlay() to safeBlockSize so they're never reallocated on
    // the audio thread.
    std::vector<float> fadeBufL_, fadeBufR_;

    // Dry-signal scratch. The engines output WET-ONLY; the dry/wet mix is
    // applied here after any preset crossfade so the dry stays correlated
    // (no +3 dB swell from equal-power blending the same dry through two
    // engines) and the mix smoother is independent of which engine is
    // active.
    std::vector<float> dryBufL_, dryBufR_;
    OnePoleSmoother mixSmoother_;

    // ChromaVerb-style wet ducking, sidechained off the dry input. Applied to
    // the wet output after the engine/crossfade, before the dry/wet mix. Depth
    // 0 (default) → pass-through, so legacy presets are bit-identical.
    ReverbDucker ducker_;

    // Cached SixAPTank brightness/density state. Per-preset values that
    // travel with the project (persisted in get/setStateInformation as
    // properties on the state ValueTree, not as APVTS parameters since
    // they're not user-facing automation targets). Defaults match the
    // engine's historical hardcoded constants — old save files without
    // these properties round-trip to identical sound.
    struct SixAPBrightnessState
    {
        float densityBaseline = 0.62f;
        float bloomCeiling    = 0.85f;
        float bloomStagger[6] = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f };
        float earlyMix        = 0.5f;
        float outputTrim      = 1.3f;
    };
    SixAPBrightnessState sixAPBrightness_;

    // Pushes the cached brightness state to a target engine. Called for both
    // engines on state load and for the new active engine during a preset
    // swap.
    void pushSixAPBrightnessTo (DuskVerbEngine& target);
    // DPV brightness + corrective EQ migrated to APVTS-only on 2026-05-25.
    // Single source of truth = the 7 dpv* APVTS atomics; no struct cache.
    // setStateInformation forwards legacy custom-property values into APVTS
    // via migrateLegacyDpvProp(). Next processBlock's edge-detect pushes to
    // active engine; forcePushAllParametersTo pushes to idle engine at swap.

    // Force-pushes every current APVTS parameter and the cached SixAPTank
    // brightness state to a target engine. Called from the audio thread at
    // preset-swap time so the freshly-cleared idle engine starts the fade
    // already configured for the new preset.
    void forcePushAllParametersTo (DuskVerbEngine* target);

    // Resets the parameter-edge-detection cache to the current APVTS values
    // so the next processBlock doesn't re-push parameters we just force-
    // pushed during a swap.
    void syncParameterCacheToCurrent();

    // Audio-thread side of preset apply: clear new active engine's buffers,
    // force-push current parameters, snap shell smoothers, swap pointers,
    // arm the fade counter.
    void performPresetSwap();

    // Cached APVTS pointers — read once at construction, hot in processBlock.
    std::atomic<float>* algorithmParam_     = nullptr;
    std::atomic<float>* mixParam_           = nullptr;
    std::atomic<float>* busModeParam_       = nullptr;
    std::atomic<float>* preDelayParam_      = nullptr;
    std::atomic<float>* preDelaySyncParam_  = nullptr;
    std::atomic<float>* decayParam_         = nullptr;
    std::atomic<float>* sizeParam_          = nullptr;
    std::atomic<float>* modDepthParam_      = nullptr;
    std::atomic<float>* modRateParam_       = nullptr;
    std::atomic<float>* tailSpinDepthParam_ = nullptr;
    std::atomic<float>* tailSpinRateParam_  = nullptr;
    std::atomic<float>* dampingParam_       = nullptr;
    std::atomic<float>* bassMultParam_      = nullptr;
    std::atomic<float>* midMultParam_       = nullptr;
    std::atomic<float>* subMultParam_       = nullptr;
    std::atomic<float>* hiMidMultParam_     = nullptr;
    std::atomic<float>* crossoverSubParam_  = nullptr;
    std::atomic<float>* crossoverAirParam_  = nullptr;
    std::atomic<float>* shaperDepthParam_   = nullptr;
    std::atomic<float>* shaperTimeParam_    = nullptr;
    std::atomic<float>* shaperXoverParam_   = nullptr;
    std::atomic<float>* shaperSensParam_    = nullptr;
    std::atomic<float>* inputSubGainParam_  = nullptr;
    std::atomic<float>* inputMidGainParam_  = nullptr;
    std::atomic<float>* inputHighGainParam_ = nullptr;
    std::atomic<float>* crossoverParam_     = nullptr;
    std::atomic<float>* highCrossoverParam_ = nullptr;
    std::atomic<float>* bassChokeParam_     = nullptr;
    std::atomic<float>* saturationParam_    = nullptr;
    std::atomic<float>* diffusionParam_     = nullptr;
    std::atomic<float>* erLevelParam_       = nullptr;
    std::atomic<float>* erSizeParam_        = nullptr;
    std::atomic<float>* erBoostParam_       = nullptr;
    std::atomic<float>* qtHiMidMultParam_   = nullptr;
    std::atomic<float>* qtAirMultParam_     = nullptr;
    std::atomic<float>* erRiseParam_        = nullptr;
    std::atomic<float>* erBusLowGainParam_  = nullptr;
    std::atomic<float>* erBusHighGainParam_ = nullptr;
    std::atomic<float>* tankLevelParam_     = nullptr;
    std::atomic<float>* tankSplitHzParam_   = nullptr;
    std::atomic<float>* erStereoNeutralParam_ = nullptr;
    std::atomic<float>* erDecorrParam_      = nullptr;
    std::atomic<float>* xtalkParam_         = nullptr;
    std::atomic<float>* mbEnableParam_      = nullptr;
    std::atomic<float>* mbLowDecayParam_    = nullptr;
    std::atomic<float>* mbMidDecayParam_    = nullptr;
    std::atomic<float>* mbHighDecayParam_   = nullptr;
    std::atomic<float>* loCutParam_         = nullptr;
    std::atomic<float>* hiCutParam_         = nullptr;
    // Phase 1 post-tank shelf depth — sweepable per preset.
    std::atomic<float>* hiCutShelfDbParam_  = nullptr;
    std::atomic<float>* widthParam_         = nullptr;
    std::atomic<float>* freezeParam_        = nullptr;
    std::atomic<float>* gateEnabledParam_   = nullptr;
    std::atomic<float>* gainTrimParam_      = nullptr;
    std::atomic<float>* monoBelowParam_     = nullptr;
    std::atomic<float>* monoBelowDepthParam_= nullptr;
    std::atomic<float>* duckParam_          = nullptr;   // wet ducking depth (0 = off)

    // Phase α: PostTankEQ 4-band GAIN APVTS-driven. Freq + Q live in the
    // per-preset kPostTankEQByName map (engine-config only). Optimizer
    // touches these from --param sweeps in Stage 3.
    std::atomic<float>* pteqBand0GainParam_ = nullptr;
    std::atomic<float>* pteqBand1GainParam_ = nullptr;
    std::atomic<float>* pteqBand2GainParam_ = nullptr;
    std::atomic<float>* pteqBand3GainParam_ = nullptr;
    // Per-band freq + Q cached from kPostTankEQByName at preset-swap time
    // so edge-detect can re-issue engine.setPostTankEQBand on gain changes.
    float pteqBandFreq_[4] {  80.0f,  500.0f, 3000.0f, 10000.0f };
    float pteqBandQ_   [4] {   1.5f,    1.0f,    1.5f,     0.8f };
    float lastPteqBand0Gain_ = 0.0f;
    float lastPteqBand1Gain_ = 0.0f;
    float lastPteqBand2Gain_ = 0.0f;
    float lastPteqBand3Gain_ = 0.0f;

    // Phase γ (2026-05-29): PostTankBandTrim 4-region gain trims. Independent
    // linear gain stage that sits AFTER PostTankEQ, BEFORE the dry/wet mix
    // matrix. Decoupled from FDN loop damping — corrective scalpel for EDT
    // band shape and late bass boom. All defaults 0 dB → bit-identical
    // bypass on legacy presets that don't opt in.
    std::atomic<float>* postBandSubParam_    = nullptr;
    std::atomic<float>* postBandLowMidParam_ = nullptr;
    std::atomic<float>* postBandMidHiParam_  = nullptr;
    std::atomic<float>* postBandAirParam_    = nullptr;
    float lastPostBandSub_    = 0.0f;
    float lastPostBandLowMid_ = 0.0f;
    float lastPostBandMidHi_  = 0.0f;
    float lastPostBandAir_    = 0.0f;

    // Phase δ (2026-05-29): per-band attack-ramp envelope shape. 4 regions ×
    // {attack_db, tau_ms}. All attack_db default 0 dB → AttackRamp gains
    // stay at unity → bit-identical pass-through on legacy presets.
    std::atomic<float>* edtSubAtkParam_     = nullptr;
    std::atomic<float>* edtSubTauParam_     = nullptr;
    std::atomic<float>* edtLowMidAtkParam_  = nullptr;
    std::atomic<float>* edtLowMidTauParam_  = nullptr;
    std::atomic<float>* edtMidHiAtkParam_   = nullptr;
    std::atomic<float>* edtMidHiTauParam_   = nullptr;
    std::atomic<float>* edtAirAtkParam_     = nullptr;
    std::atomic<float>* edtAirTauParam_     = nullptr;
    float lastEDTSubAtk_    = 0.0f;
    float lastEDTSubTau_    = 100.0f;
    float lastEDTLowMidAtk_ = 0.0f;
    float lastEDTLowMidTau_ = 100.0f;
    float lastEDTMidHiAtk_  = 0.0f;
    float lastEDTMidHiTau_  = 100.0f;
    float lastEDTAirAtk_    = 0.0f;
    float lastEDTAirTau_    = 100.0f;

    // Phase ε (2026-05-29): in-loop narrow-Q peaking band on FDN feedback
    // path. gainDb default 0 → bypass guard in FDNReverb skips the per-
    // line biquad → zero audio-thread cost when not in use.
    std::atomic<float>* inLoopPeakHzParam_ = nullptr;
    std::atomic<float>* inLoopPeakQParam_  = nullptr;
    std::atomic<float>* inLoopPeakDbParam_ = nullptr;
    float lastInLoopPeakHz_ = 1000.0f;
    float lastInLoopPeakQ_  = 2.0f;
    float lastInLoopPeakDb_ = 0.0f;

    // Phase η (2026-05-29): per-line dual-time-constant bass shelf. Both
    // gains 0 dB → bypass guard inside FDNReverb (dualBassShelfActive_)
    // → no audio-thread cost on legacy presets.
    std::atomic<float>* bassShelfFastFcParam_     = nullptr;
    std::atomic<float>* bassShelfSlowFcParam_     = nullptr;
    std::atomic<float>* bassShelfFastDbParam_     = nullptr;
    std::atomic<float>* bassShelfSlowDbParam_     = nullptr;
    std::atomic<float>* bassShelfTransitionParam_ = nullptr;
    float lastBassShelfFastFc_     = 400.0f;
    float lastBassShelfSlowFc_     = 200.0f;
    float lastBassShelfFastDb_     = 0.0f;
    float lastBassShelfSlowDb_     = 0.0f;
    float lastBassShelfTransition_ = 100.0f;

    // DattorroPlateVintage corrective EQ + brightness — APVTS-driven so
    // Optuna can sweep them via --param. Only the active engine sees
    // the values; non-DPV engines forward to no-op setters.
    std::atomic<float>* dpvHfShelfDbParam_       = nullptr;
    std::atomic<float>* dpvHfShelfHzParam_       = nullptr;
    std::atomic<float>* dpvStructHfDampHzParam_  = nullptr;
    std::atomic<float>* dpvBoxCutDbParam_        = nullptr;
    std::atomic<float>* dpvBoxCutHzParam_        = nullptr;
    std::atomic<float>* dpvBassShelfDbParam_     = nullptr;
    std::atomic<float>* dpvBassShelfHzParam_     = nullptr;

    juce::AudioParameterBool* bypassParam_ = nullptr;

    // Edge-detected last-pushed values so the audio thread only forwards
    // changes (not every sample).
    int   cachedAlgorithm_ = -1;
    float lastDecaySec_    = -1.0f;
    float lastSize_        = -1.0f;
    float lastDamping_     = -1.0f;
    float lastBassMult_    = -1.0f;
    // DPV edge-detect last-pushed values. Sentinel +9999 = never pushed.
    float lastDpvHfShelfDb_     = 9999.0f;
    float lastDpvHfShelfHz_     = 9999.0f;
    float lastDpvStructHfDamp_  = 9999.0f;
    float lastDpvBoxCutDb_      = 9999.0f;
    float lastDpvBoxCutHz_      = 9999.0f;
    float lastDpvBassShelfDb_   = 9999.0f;
    float lastDpvBassShelfHz_   = 9999.0f;
    float lastMidMult_     = -1.0f;
    float lastSubMult_     = -1.0f;
    float lastHiMidMult_   = -1.0f;
    float lastCrossoverSub_ = -1.0f;
    float lastCrossoverAir_ = -1.0f;
    float lastShaperDepth_ = -1.0f;
    float lastShaperTime_  = -1.0f;
    float lastShaperXover_ = -1.0f;
    float lastShaperSens_  = -1.0f;
    float lastInputSubGain_ = -999.0f;
    float lastInputMidGain_ = -999.0f;
    float lastInputHighGain_ = -999.0f;
    float lastCrossover_   = -1.0f;
    float lastHighCrossover_ = -1.0f;
    float lastBassChoke_     = -1.0f;
    float lastSaturation_  = -1.0f;
    float lastDiffusion_   = -1.0f;
    float lastModDepth_    = -1.0f;
    float lastModRate_     = -1.0f;
    float lastTailSpinDepth_ = -1.0f;
    float lastTailSpinRate_  = -1.0f;
    float lastERSize_      = -1.0f;
    float lastERLevel_     = -2.0f;
    float lastERBoost_     = -1.0f;
    float lastQtHiMidMult_ = -99.0f;
    float lastQtAirMult_   = -99.0f;
    float lastERRise_      = -1.0f;
    float lastERBusLow_    = -99.0f;
    float lastERBusHigh_   = -99.0f;
    float lastTankLevel_   = -1.0f;
    float lastTankSplitHz_ = -1.0f;
    float lastERStereoNeutral_ = -1.0f;
    float lastERDecorr_    = -1.0f;
    float lastXTalk_       = -1.0f;
    bool  lastMbEnable_    = false;
    float lastMbLow_       = -1.0f;
    float lastMbMid_       = -1.0f;
    float lastMbHigh_      = -1.0f;
    float lastPreDelayMs_  = -1.0f;
    float lastMix_         = -1.0f;
    float lastLoCut_       = -1.0f;
    float lastHiCut_       = -1.0f;
    float lastHiCutShelfDb_= 999.0f;   // out-of-range sentinel
    float lastWidth_       = -1.0f;
    float lastGainTrim_    = -999.0f;
    float lastMonoBelow_   = -1.0f;
    float lastMonoBelowDepth_ = -1.0f;
    float lastDuck_        = -1.0f;
    bool  lastFreeze_      = false;
    bool  haveLastFreeze_  = false;
    bool  lastGateEnabled_     = true;
    bool  haveLastGateEnabled_ = false;

    double preparedSampleRate_ = 0.0;
    int    preparedBlockSize_  = 0;

    // Metering atomics
    std::atomic<float> inputLevelL_  { -100.0f };
    std::atomic<float> inputLevelR_  { -100.0f };
    std::atomic<float> outputLevelL_ { -100.0f };
    std::atomic<float> outputLevelR_ { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskVerbProcessor)
};

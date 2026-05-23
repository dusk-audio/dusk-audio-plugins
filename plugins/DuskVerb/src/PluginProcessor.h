#pragma once

#include "dsp/DuskVerbEngine.h"

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

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
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
        float earlyHighpassHz = 20.0f;     // 20 Hz = effective bypass
    };
    SixAPBrightnessState sixAPBrightness_;

    // Pushes the cached brightness state to a target engine. Called for both
    // engines on state load and for the new active engine during a preset
    // swap.
    void pushSixAPBrightnessTo (DuskVerbEngine& target);

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
    std::atomic<float>* dampingParam_       = nullptr;
    std::atomic<float>* bassMultParam_      = nullptr;
    std::atomic<float>* midMultParam_       = nullptr;
    std::atomic<float>* crossoverParam_     = nullptr;
    std::atomic<float>* highCrossoverParam_ = nullptr;
    std::atomic<float>* bassChokeParam_     = nullptr;
    std::atomic<float>* saturationParam_    = nullptr;
    std::atomic<float>* diffusionParam_     = nullptr;
    std::atomic<float>* erLevelParam_       = nullptr;
    std::atomic<float>* erSizeParam_        = nullptr;
    std::atomic<float>* loCutParam_         = nullptr;
    std::atomic<float>* hiCutParam_         = nullptr;
    std::atomic<float>* widthParam_         = nullptr;
    std::atomic<float>* freezeParam_        = nullptr;
    std::atomic<float>* gateEnabledParam_   = nullptr;
    std::atomic<float>* gainTrimParam_      = nullptr;
    std::atomic<float>* monoBelowParam_     = nullptr;
    std::atomic<float>* firstReflLDlyParam_  = nullptr;
    std::atomic<float>* firstReflRDlyParam_  = nullptr;
    std::atomic<float>* firstReflLGainParam_ = nullptr;
    std::atomic<float>* firstReflRGainParam_ = nullptr;
    std::atomic<float>* firstReflHFCutParam_ = nullptr;

    // HallReverb advanced (algo 10 only) — forwarded straight to hall_
    // through DuskVerbEngine::setHall* methods. Other engines ignore.
    std::atomic<float>* hallBassDampingParam_     = nullptr;
    std::atomic<float>* hallMidDampingParam_      = nullptr;
    std::atomic<float>* hallTrebleDampingParam_   = nullptr;
    std::atomic<float>* hallBassGainParam_        = nullptr;
    std::atomic<float>* hallMidGainParam_         = nullptr;
    std::atomic<float>* hallTrebleGainParam_      = nullptr;
    std::atomic<float>* hallInlineDiffusionParam_ = nullptr;
    std::atomic<float>* hallStereoWidthParam_     = nullptr;
    // Multi-tap input injection (6 taps × {ms, weight})
    std::atomic<float>* hallTap0MsParam_     = nullptr;
    std::atomic<float>* hallTap1MsParam_     = nullptr;
    std::atomic<float>* hallTap2MsParam_     = nullptr;
    std::atomic<float>* hallTap3MsParam_     = nullptr;
    std::atomic<float>* hallTap4MsParam_     = nullptr;
    std::atomic<float>* hallTap5MsParam_     = nullptr;
    std::atomic<float>* hallTap0WeightParam_ = nullptr;
    std::atomic<float>* hallTap1WeightParam_ = nullptr;
    std::atomic<float>* hallTap2WeightParam_ = nullptr;
    std::atomic<float>* hallTap3WeightParam_ = nullptr;
    std::atomic<float>* hallTap4WeightParam_ = nullptr;
    std::atomic<float>* hallTap5WeightParam_ = nullptr;
    // P8b specular taps (4 × {ms, weight} + shared HF cut)
    std::atomic<float>* hallSpec0MsParam_     = nullptr;
    std::atomic<float>* hallSpec1MsParam_     = nullptr;
    std::atomic<float>* hallSpec2MsParam_     = nullptr;
    std::atomic<float>* hallSpec3MsParam_     = nullptr;
    std::atomic<float>* hallSpec0WeightParam_ = nullptr;
    std::atomic<float>* hallSpec1WeightParam_ = nullptr;
    std::atomic<float>* hallSpec2WeightParam_ = nullptr;
    std::atomic<float>* hallSpec3WeightParam_ = nullptr;
    std::atomic<float>* hallSpecHFCutParam_   = nullptr;
    // P10 per-band peaking EQ
    std::atomic<float>* hallBassEQGainParam_   = nullptr;
    std::atomic<float>* hallBassEQQParam_      = nullptr;
    std::atomic<float>* hallMidEQGainParam_    = nullptr;
    std::atomic<float>* hallMidEQQParam_       = nullptr;
    std::atomic<float>* hallTrebleEQGainParam_ = nullptr;
    std::atomic<float>* hallTrebleEQQParam_    = nullptr;
    std::atomic<float>* hallBassEQFcParam_     = nullptr;
    std::atomic<float>* hallMidEQFcParam_      = nullptr;
    std::atomic<float>* hallTrebleEQFcParam_   = nullptr;
    std::atomic<float>* hallBassDampingFcParam_   = nullptr;
    std::atomic<float>* hallMidDampingFcParam_    = nullptr;
    std::atomic<float>* hallTrebleDampingFcParam_ = nullptr;
    std::atomic<float>* hallBassModDepthParam_    = nullptr;
    std::atomic<float>* hallBassModRateParam_     = nullptr;
    std::atomic<float>* hallMidModDepthParam_     = nullptr;
    std::atomic<float>* hallMidModRateParam_      = nullptr;
    std::atomic<float>* hallTrebleModDepthParam_  = nullptr;
    std::atomic<float>* hallTrebleModRateParam_   = nullptr;
    std::atomic<float>* hallBassModShapeParam_    = nullptr;
    std::atomic<float>* hallMidModShapeParam_     = nullptr;
    std::atomic<float>* hallTrebleModShapeParam_  = nullptr;
    std::atomic<float>* hallBassChanGainSpreadParam_   = nullptr;
    std::atomic<float>* hallMidChanGainSpreadParam_    = nullptr;
    std::atomic<float>* hallTrebleChanGainSpreadParam_ = nullptr;
    std::atomic<float>* hallBassShelfGainParam_   = nullptr;
    std::atomic<float>* hallBassShelfFcParam_     = nullptr;
    std::atomic<float>* hallMidShelfGainParam_    = nullptr;
    std::atomic<float>* hallMidShelfFcParam_      = nullptr;
    std::atomic<float>* hallTrebleShelfGainParam_ = nullptr;
    std::atomic<float>* hallTrebleShelfFcParam_   = nullptr;
    std::atomic<float>* hallInputDiffusionParam_  = nullptr;
    // RingReverb (algo 11) APVTS — 7 ring-specific axes (Decay Time +
    // Size already broadcast by the shared setDecayTime/setSize setters).
    std::atomic<float>* ringDampingParam_     = nullptr;
    std::atomic<float>* ringDampingFcParam_   = nullptr;
    std::atomic<float>* ringSpreadParam_      = nullptr;
    std::atomic<float>* ringShapeParam_       = nullptr;
    std::atomic<float>* ringSpinParam_        = nullptr;
    std::atomic<float>* ringWanderParam_      = nullptr;
    std::atomic<float>* ringStereoWidthParam_ = nullptr;
    // HybridHallReverb (algo 12) — 14 axes.
    std::atomic<float>* hybridRingLevelParam_    = nullptr;
    std::atomic<float>* hybridERLevelParam_      = nullptr;
    std::atomic<float>* hybridERW1Param_         = nullptr;
    std::atomic<float>* hybridERW2Param_         = nullptr;
    std::atomic<float>* hybridERW3Param_         = nullptr;
    std::atomic<float>* hybridLowShelfGainParam_ = nullptr;
    std::atomic<float>* hybridLowShelfFcParam_   = nullptr;
    std::atomic<float>* hybridHighShelfGainParam_= nullptr;
    std::atomic<float>* hybridHighShelfFcParam_  = nullptr;
    std::atomic<float>* hybridRingDampingParam_  = nullptr;
    std::atomic<float>* hybridRingDampingFcParam_= nullptr;
    std::atomic<float>* hybridRingSpinParam_     = nullptr;
    std::atomic<float>* hybridRingWanderParam_   = nullptr;
    std::atomic<float>* hybridRingStereoParam_   = nullptr;
    // HallTrueLexReverb (algo 13) — 7 axes (everything else borrowed
    // from the engine-shared / Hall (Lex) APVTS surface).
    std::atomic<float>* truelexERLevelParam_     = nullptr;
    std::atomic<float>* truelexTankLevelParam_   = nullptr;
    std::atomic<float>* truelexERW0Param_        = nullptr;
    std::atomic<float>* truelexERW1Param_        = nullptr;
    std::atomic<float>* truelexERW2Param_        = nullptr;
    std::atomic<float>* truelexERW3Param_        = nullptr;
    std::atomic<float>* truelexAPCoeffParam_     = nullptr;
    // HallTrueLex16Reverb (algo 14) — 7 axes (16-ch FDN variant).
    std::atomic<float>* truelex16ERLevelParam_   = nullptr;
    std::atomic<float>* truelex16TankLevelParam_ = nullptr;
    std::atomic<float>* truelex16ERW0Param_      = nullptr;
    std::atomic<float>* truelex16ERW1Param_      = nullptr;
    std::atomic<float>* truelex16ERW2Param_      = nullptr;
    std::atomic<float>* truelex16ERW3Param_      = nullptr;
    std::atomic<float>* truelex16ERW4Param_      = nullptr;
    std::atomic<float>* truelex16APCoeffParam_   = nullptr;
    // LexFigure8Reverb (algo 15) — 1 axis (in-loop structural HF damping)
    // + Phase A: 9 ER TDL axes (4 dly, 4 gain, 1 stereo offset).
    std::atomic<float>* lexFig8StructHFParam_    = nullptr;
    std::atomic<float>* lexFig8ERTap0DlyParam_   = nullptr;
    std::atomic<float>* lexFig8ERTap1DlyParam_   = nullptr;
    std::atomic<float>* lexFig8ERTap2DlyParam_   = nullptr;
    std::atomic<float>* lexFig8ERTap3DlyParam_   = nullptr;
    std::atomic<float>* lexFig8ERTap0GainParam_  = nullptr;
    std::atomic<float>* lexFig8ERTap1GainParam_  = nullptr;
    std::atomic<float>* lexFig8ERTap2GainParam_  = nullptr;
    std::atomic<float>* lexFig8ERTap3GainParam_  = nullptr;
    std::atomic<float>* lexFig8ERStereoOffsetParam_ = nullptr;
    std::atomic<float>* lexFig8TankAttenParam_   = nullptr;
    std::atomic<float>* lexFig8TankInParam_      = nullptr;
    std::atomic<float>* lexFig8TankPDlyParam_    = nullptr;
    std::atomic<float>* lexFig8DensityJitterParam_ = nullptr;
    std::atomic<float>* lexFig8DensityRateParam_   = nullptr;
    std::atomic<float>* lexFig8SubBassMultParam_   = nullptr;
    std::atomic<float>* lexFig8SubBassXoverParam_  = nullptr;
    std::atomic<float>* lexFig8TiltParam_           = nullptr;
    std::atomic<float>* lexFig8AirMultParam_        = nullptr;
    std::atomic<float>* lexFig8AirXoverParam_       = nullptr;
    std::atomic<float>* lexFig8BandMultParam_[8]    = { nullptr };
    std::atomic<float>* lexFig8DapDelayParam_[4]    = { nullptr };
    std::atomic<float>* lexFig8OTapLParam_[4]       = { nullptr };
    std::atomic<float>* lexFig8OTapRParam_[4]       = { nullptr };
    std::atomic<float>* lexFig8Del1LParam_          = nullptr;
    std::atomic<float>* lexFig8Del1RParam_          = nullptr;
    std::atomic<float>* lexFig8Del2LParam_          = nullptr;
    std::atomic<float>* lexFig8Del2RParam_          = nullptr;
    std::atomic<float>* lexFig8AP1LParam_           = nullptr;
    std::atomic<float>* lexFig8AP1RParam_           = nullptr;
    std::atomic<float>* lexFig8AP2LParam_           = nullptr;
    std::atomic<float>* lexFig8AP2RParam_           = nullptr;
    std::atomic<float>* lexFig8XFdLParam_           = nullptr;
    std::atomic<float>* lexFig8XFdRParam_           = nullptr;
    std::atomic<float>* lexFig8BypassDiffParam_     = nullptr;
    std::atomic<float>* lexFig8DuckThreshParam_     = nullptr;
    std::atomic<float>* lexFig8DuckAtkParam_        = nullptr;
    std::atomic<float>* lexFig8DuckRelParam_        = nullptr;
    std::atomic<float>* lexFig8DuckDepthParam_      = nullptr;

    // LexiconMTDL (algo 16) tuning axes. v35: per-line damping (8 axes).
    // v36: per-line feedback (8), per-tap ER gain dB (4), per-line LFO
    // depth ms (8) — full Lexicon-style spin-and-wander surface.
    std::atomic<float>* mtdlFeedbackParam_         = nullptr;
    std::atomic<float>* mtdlFeedbackAtParam_[8]    = { nullptr };
    std::atomic<float>* mtdlDampingHzParam_[8]     = { nullptr };
    std::atomic<float>* mtdlERLevelParam_          = nullptr;
    std::atomic<float>* mtdlERTapGainDbParam_[4]   = { nullptr };
    std::atomic<float>* mtdlLateLevelParam_        = nullptr;
    std::atomic<float>* mtdlLineModMsParam_[8]     = { nullptr };
    // v37
    std::atomic<float>* mtdlSchroederCoeffParam_   = nullptr;
    std::atomic<float>* mtdlTiltDbParam_           = nullptr;
    // v38 — Engine 17 hybrid mix axes
    std::atomic<float>* hybridWashLevelParam_      = nullptr;
    std::atomic<float>* hybridChatterLevelParam_   = nullptr;

    juce::AudioParameterBool* bypassParam_ = nullptr;

    // Edge-detected last-pushed values so the audio thread only forwards
    // changes (not every sample).
    int   cachedAlgorithm_ = -1;
    float lastDecaySec_    = -1.0f;
    float lastSize_        = -1.0f;
    float lastDamping_     = -1.0f;
    float lastBassMult_    = -1.0f;
    float lastMidMult_     = -1.0f;
    float lastCrossover_   = -1.0f;
    float lastHighCrossover_ = -1.0f;
    float lastBassChoke_     = -1.0f;
    float lastSaturation_  = -1.0f;
    float lastDiffusion_   = -1.0f;
    float lastModDepth_    = -1.0f;
    float lastModRate_     = -1.0f;
    float lastERSize_      = -1.0f;
    float lastERLevel_     = -2.0f;
    float lastPreDelayMs_  = -1.0f;
    float lastMix_         = -1.0f;
    float lastLoCut_       = -1.0f;
    float lastHiCut_       = -1.0f;
    float lastWidth_       = -1.0f;
    float lastGainTrim_    = -999.0f;
    float lastMonoBelow_   = -1.0f;
    float lastFirstReflLDly_  = -1.0f;
    float lastFirstReflRDly_  = -1.0f;
    float lastFirstReflLGain_ = -999.0f;
    float lastFirstReflRGain_ = -999.0f;
    float lastFirstReflHFCut_ = -1.0f;
    // Hall (Lex) advanced — edge-detect against -999 so the first push
    // from snapEnginesToCurrentValues always fires regardless of default.
    float lastHallBassDamping_     = -999.0f;
    float lastHallMidDamping_      = -999.0f;
    float lastHallTrebleDamping_   = -999.0f;
    float lastHallBassGain_        = -999.0f;
    float lastHallMidGain_         = -999.0f;
    float lastHallTrebleGain_      = -999.0f;
    float lastHallInlineDiffusion_ = -999.0f;
    float lastHallStereoWidth_     = -999.0f;
    float lastHallTap0Ms_     = -999.0f;
    float lastHallTap1Ms_     = -999.0f;
    float lastHallTap2Ms_     = -999.0f;
    float lastHallTap3Ms_     = -999.0f;
    float lastHallTap4Ms_     = -999.0f;
    float lastHallTap5Ms_     = -999.0f;
    float lastHallTap0Weight_ = -999.0f;
    float lastHallTap1Weight_ = -999.0f;
    float lastHallTap2Weight_ = -999.0f;
    float lastHallTap3Weight_ = -999.0f;
    float lastHallTap4Weight_ = -999.0f;
    float lastHallTap5Weight_ = -999.0f;
    float lastHallSpec0Ms_     = -999.0f;
    float lastHallSpec1Ms_     = -999.0f;
    float lastHallSpec2Ms_     = -999.0f;
    float lastHallSpec3Ms_     = -999.0f;
    float lastHallSpec0Weight_ = -999.0f;
    float lastHallSpec1Weight_ = -999.0f;
    float lastHallSpec2Weight_ = -999.0f;
    float lastHallSpec3Weight_ = -999.0f;
    float lastHallSpecHFCut_   = -999.0f;
    float lastHallBassEQGain_   = -999.0f;
    float lastHallBassEQQ_      = -999.0f;
    float lastHallMidEQGain_    = -999.0f;
    float lastHallMidEQQ_       = -999.0f;
    float lastHallTrebleEQGain_ = -999.0f;
    float lastHallTrebleEQQ_    = -999.0f;
    float lastHallBassEQFc_     = -999.0f;
    float lastHallMidEQFc_      = -999.0f;
    float lastHallTrebleEQFc_   = -999.0f;
    float lastHallBassDampingFc_   = -999.0f;
    float lastHallMidDampingFc_    = -999.0f;
    float lastHallTrebleDampingFc_ = -999.0f;
    float lastHallBassModDepth_    = -999.0f;
    float lastHallBassModRate_     = -999.0f;
    float lastHallMidModDepth_     = -999.0f;
    float lastHallMidModRate_      = -999.0f;
    float lastHallTrebleModDepth_  = -999.0f;
    float lastHallTrebleModRate_   = -999.0f;
    float lastHallBassModShape_    = -999.0f;
    float lastHallMidModShape_     = -999.0f;
    float lastHallTrebleModShape_  = -999.0f;
    float lastHallBassChanGainSpread_   = -999.0f;
    float lastHallMidChanGainSpread_    = -999.0f;
    float lastHallTrebleChanGainSpread_ = -999.0f;
    float lastHallBassShelfGain_   = -999.0f;
    float lastHallBassShelfFc_     = -999.0f;
    float lastHallMidShelfGain_    = -999.0f;
    float lastHallMidShelfFc_      = -999.0f;
    float lastHallTrebleShelfGain_ = -999.0f;
    float lastHallTrebleShelfFc_   = -999.0f;
    float lastHallInputDiffusion_  = -999.0f;
    float lastRingDamping_     = -999.0f;
    float lastRingDampingFc_   = -999.0f;
    float lastRingSpread_      = -999.0f;
    float lastRingShape_       = -999.0f;
    float lastRingSpin_        = -999.0f;
    float lastRingWander_      = -999.0f;
    float lastRingStereoWidth_ = -999.0f;
    float lastHybridRingLevel_    = -999.0f;
    float lastHybridERLevel_      = -999.0f;
    float lastHybridERW1_         = -999.0f;
    float lastHybridERW2_         = -999.0f;
    float lastHybridERW3_         = -999.0f;
    float lastHybridLowShelfGain_ = -999.0f;
    float lastHybridLowShelfFc_   = -999.0f;
    float lastHybridHighShelfGain_= -999.0f;
    float lastHybridHighShelfFc_  = -999.0f;
    float lastHybridRingDamping_  = -999.0f;
    float lastHybridRingDampingFc_= -999.0f;
    float lastHybridRingSpin_     = -999.0f;
    float lastHybridRingWander_   = -999.0f;
    float lastHybridRingStereo_   = -999.0f;
    float lastTrueLexERLevel_     = -999.0f;
    float lastTrueLexTankLevel_   = -999.0f;
    float lastTrueLexERW0_        = -999.0f;
    float lastTrueLexERW1_        = -999.0f;
    float lastTrueLexERW2_        = -999.0f;
    float lastTrueLexERW3_        = -999.0f;
    float lastTrueLexAPCoeff_     = -999.0f;
    float lastTrueLex16ERLevel_   = -999.0f;
    float lastTrueLex16TankLevel_ = -999.0f;
    float lastTrueLex16ERW0_      = -999.0f;
    float lastTrueLex16ERW1_      = -999.0f;
    float lastTrueLex16ERW2_      = -999.0f;
    float lastTrueLex16ERW3_      = -999.0f;
    float lastTrueLex16ERW4_      = -999.0f;
    float lastTrueLex16APCoeff_   = -999.0f;
    float lastLexFig8StructHF_    = -999.0f;
    float lastLexFig8ERTap0Dly_   = -999.0f;
    float lastLexFig8ERTap1Dly_   = -999.0f;
    float lastLexFig8ERTap2Dly_   = -999.0f;
    float lastLexFig8ERTap3Dly_   = -999.0f;
    float lastLexFig8ERTap0Gain_  = -999.0f;
    float lastLexFig8ERTap1Gain_  = -999.0f;
    float lastLexFig8ERTap2Gain_  = -999.0f;
    float lastLexFig8ERTap3Gain_  = -999.0f;
    float lastLexFig8ERStereoOffset_ = -999.0f;
    float lastLexFig8TankAtten_   = -999.0f;
    float lastLexFig8TankIn_      = -999.0f;
    float lastLexFig8TankPDly_    = -999.0f;
    float lastLexFig8DensityJitter_ = -999.0f;
    float lastLexFig8DensityRate_   = -999.0f;
    float lastLexFig8SubBassMult_   = -999.0f;
    float lastLexFig8SubBassXover_  = -999.0f;
    float lastLexFig8Tilt_           = -999.0f;
    float lastLexFig8AirMult_        = -999.0f;
    float lastLexFig8AirXover_       = -999.0f;
    float lastLexFig8BandMult_[8]    = { -999.0f, -999.0f, -999.0f, -999.0f,
                                         -999.0f, -999.0f, -999.0f, -999.0f };
    float lastLexFig8DapDelay_[4]    = { -999.0f, -999.0f, -999.0f, -999.0f };
    float lastLexFig8OTapL_[4]       = { -999.0f, -999.0f, -999.0f, -999.0f };
    float lastLexFig8OTapR_[4]       = { -999.0f, -999.0f, -999.0f, -999.0f };
    float lastLexFig8Del1L_          = -999.0f;
    float lastLexFig8Del1R_          = -999.0f;
    float lastLexFig8Del2L_          = -999.0f;
    float lastLexFig8Del2R_          = -999.0f;
    float lastLexFig8AP1L_           = -999.0f;
    float lastLexFig8AP1R_           = -999.0f;
    float lastLexFig8AP2L_           = -999.0f;
    float lastLexFig8AP2R_           = -999.0f;
    float lastLexFig8XFdL_           = -999.0f;
    float lastLexFig8XFdR_           = -999.0f;
    float lastLexFig8BypassDiff_     = -999.0f;
    float lastLexFig8DuckThresh_     = -999.0f;
    float lastLexFig8DuckAtk_        = -999.0f;
    float lastLexFig8DuckRel_        = -999.0f;
    float lastLexFig8DuckDepth_      = -999.0f;

    // LexiconMTDL (algo 16). v35 per-line damping. v36 per-line feedback,
    // per-tap ER gain dB, per-line LFO depth ms. ER tap delays still
    // LOCKED at Lex anchor 0/4/7.52/9.79 ms (not exposed).
    float lastMTDLFeedback_         = -999.0f;
    float lastMTDLFeedbackAt_[8]    = { -999.0f, -999.0f, -999.0f, -999.0f,
                                        -999.0f, -999.0f, -999.0f, -999.0f };
    float lastMTDLDampingHz_[8]     = { -999.0f, -999.0f, -999.0f, -999.0f,
                                        -999.0f, -999.0f, -999.0f, -999.0f };
    float lastMTDLERLevel_          = -999.0f;
    float lastMTDLERTapGainDb_[4]   = { -999.0f, -999.0f, -999.0f, -999.0f };
    float lastMTDLLateLevel_        = -999.0f;
    float lastMTDLLineModMs_[8]     = { -999.0f, -999.0f, -999.0f, -999.0f,
                                        -999.0f, -999.0f, -999.0f, -999.0f };
    // v37
    float lastMTDLSchroederCoeff_   = -999.0f;
    float lastMTDLTiltDb_           = -999.0f;
    // v38 — hybrid mix
    float lastHybridWashLevel_      = -999.0f;
    float lastHybridChatterLevel_   = -999.0f;
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

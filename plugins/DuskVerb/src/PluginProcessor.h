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

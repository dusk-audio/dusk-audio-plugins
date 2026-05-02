#pragma once

#include "dsp/DuskAmpEngine.h"
#include "dsp/PitchDetector.h"

#include <juce_audio_processors/juce_audio_processors.h>

class DuskAmpProcessor : public juce::AudioProcessor,
                         public juce::AudioProcessorValueTreeState::Listener
{
public:
    DuskAmpProcessor();
    ~DuskAmpProcessor() override;

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
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam_; }

    // Direct XML access for preset management
    std::unique_ptr<juce::XmlElement> getStateXML();
    void setStateXML (const juce::XmlElement& xml);

    // Engine access for editor (IR loading, etc.)
    DuskAmpEngine& getEngine() { return engine_; }

    // Load a cabinet IR file (called from editor's Load IR... button).
    // Marks the IR as user-chosen so amp-model changes won't overwrite it.
    void loadCabinetIR (const juce::File& file)
    {
        engine_.getCabinetIR().loadIR (file);
        cabIRPath_ = file.getFullPathName();
        userLoadedIR_ = true;
    }

    // APVTS::Listener — fires on TONE_TYPE change to swap in the default IR
    // for the new amp model (unless the user has explicitly loaded one).
    void parameterChanged (const juce::String& paramID, float newValue) override;

    // Clears the "user picked an IR" override so factory presets and
    // amp-model swaps will load the matching bundled default. Called from
    // the preset dropdown when a factory preset is selected.
    void clearUserIROverride() noexcept { userLoadedIR_ = false; }

    // Load a NAM model file (called from editor)
    void loadNAMModel (const juce::File& file);
    juce::String getNAMModelPath() const { return namModelPath_; }
    juce::String getLastNAMStatus() const { return lastNAMStatus_; }

    juce::AudioProcessorValueTreeState parameters;

    // Level metering (audio thread writes, UI thread reads)
    float getInputLevelL() const  { return inputLevelL_.load (std::memory_order_relaxed); }
    float getInputLevelR() const  { return inputLevelR_.load (std::memory_order_relaxed); }
    float getOutputLevelL() const { return outputLevelL_.load (std::memory_order_relaxed); }
    float getOutputLevelR() const { return outputLevelR_.load (std::memory_order_relaxed); }

    // Tuner state. Audio thread reads tunerActive_ to decide whether to mute;
    // it writes detectedHz_ / detectedLevel_ from the pitch detector. UI
    // thread sets tunerActive_ from the TUNER button + reads the detected
    // values to drive the overlay.
    void  setTunerActive (bool on) noexcept { tunerActive_.store (on, std::memory_order_relaxed); }
    bool  isTunerActive  () const  noexcept { return tunerActive_.load (std::memory_order_relaxed); }
    float getDetectedHz   () const noexcept { return detectedHz_.load (std::memory_order_relaxed); }
    float getDetectedLevel() const noexcept { return detectedLevel_.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    DuskAmpEngine engine_;

    // Cab IR path (persisted in state)
    juce::String cabIRPath_;
    // True once the user explicitly picks an IR via the editor — prevents
    // amp-model changes from clobbering their choice.
    bool userLoadedIR_ = false;

    // Loads the default IR for the current TONE_TYPE if the user hasn't
    // already picked one. Safe to call from any thread (file I/O is queued
    // by juce::dsp::Convolution internally).
    void loadDefaultIRForCurrentToneType();

    // NAM model path (persisted in state)
    juce::String namModelPath_;
    juce::String lastNAMStatus_ { "idle" };

    // Discrete / choice parameter pointers
    std::atomic<float>* ampModeParam_       = nullptr;
    std::atomic<float>* preampChannelParam_ = nullptr;
    std::atomic<float>* toneTypeParam_      = nullptr;  // DSP tonestack type
    std::atomic<float>* toneTypeNamParam_   = nullptr;  // NAM tonestack type
    std::atomic<float>* oversamplingParam_  = nullptr;
    std::atomic<float>* cabEnabledParam_    = nullptr;
    std::atomic<float>* cabNormalizeParam_  = nullptr;
    std::atomic<float>* brightParam_        = nullptr;
    std::atomic<float>* delayEnabledParam_  = nullptr;
    std::atomic<float>* reverbEnabledParam_ = nullptr;

    // Continuous float parameter pointers
    std::atomic<float>* inputGainParam_     = nullptr;
    std::atomic<float>* gateThresholdParam_ = nullptr;
    std::atomic<float>* gateReleaseParam_   = nullptr;
    std::atomic<float>* inputGainNamParam_     = nullptr;
    std::atomic<float>* gateThresholdNamParam_ = nullptr;
    std::atomic<float>* gateReleaseNamParam_   = nullptr;
    std::atomic<float>* preampGainParam_    = nullptr;
    std::atomic<float>* bassParam_          = nullptr;
    std::atomic<float>* midParam_           = nullptr;
    std::atomic<float>* trebleParam_        = nullptr;
    std::atomic<float>* bassNamParam_       = nullptr;
    std::atomic<float>* midNamParam_        = nullptr;
    std::atomic<float>* trebleNamParam_     = nullptr;
    std::atomic<float>* powerDriveParam_    = nullptr;
    std::atomic<float>* presenceParam_      = nullptr;
    std::atomic<float>* resonanceParam_     = nullptr;
    std::atomic<float>* sagParam_           = nullptr;
    std::atomic<float>* cabMixParam_        = nullptr;
    std::atomic<float>* cabHiCutParam_      = nullptr;
    std::atomic<float>* cabLoCutParam_      = nullptr;
    std::atomic<float>* delayTimeParam_     = nullptr;
    std::atomic<float>* delayFeedbackParam_ = nullptr;
    std::atomic<float>* delayMixParam_      = nullptr;
    std::atomic<float>* reverbMixParam_     = nullptr;
    std::atomic<float>* reverbDecayParam_   = nullptr;
    std::atomic<float>* outputLevelParam_    = nullptr;
    std::atomic<float>* outputLevelNamParam_ = nullptr;

    juce::AudioParameterBool* bypassParam_  = nullptr;

    // Cached discrete values
    int cachedAmpMode_       = 0;
    int cachedPreampChannel_ = 1;
    int cachedToneType_      = 1;
    int cachedToneTypeNam_   = 1;
    int cachedOversampling_  = 0;
    bool cachedCabEnabled_   = true;
    bool cachedCabNormalize_ = true;
    bool cachedBright_       = false;
    bool cachedDelayEnabled_ = false;
    bool cachedReverbEnabled_= false;

    // Smoothed values for continuous parameters
    juce::SmoothedValue<float> inputGainSmooth_;
    juce::SmoothedValue<float> gateThresholdSmooth_;
    juce::SmoothedValue<float> gateReleaseSmooth_;
    juce::SmoothedValue<float> inputGainNamSmooth_;
    juce::SmoothedValue<float> gateThresholdNamSmooth_;
    juce::SmoothedValue<float> gateReleaseNamSmooth_;
    juce::SmoothedValue<float> preampGainSmooth_;
    juce::SmoothedValue<float> bassSmooth_;
    juce::SmoothedValue<float> midSmooth_;
    juce::SmoothedValue<float> trebleSmooth_;
    juce::SmoothedValue<float> bassNamSmooth_;
    juce::SmoothedValue<float> midNamSmooth_;
    juce::SmoothedValue<float> trebleNamSmooth_;
    juce::SmoothedValue<float> powerDriveSmooth_;
    juce::SmoothedValue<float> presenceSmooth_;
    juce::SmoothedValue<float> resonanceSmooth_;
    juce::SmoothedValue<float> sagSmooth_;
    juce::SmoothedValue<float> cabMixSmooth_;
    juce::SmoothedValue<float> cabHiCutSmooth_;
    juce::SmoothedValue<float> cabLoCutSmooth_;
    juce::SmoothedValue<float> delayTimeSmooth_;
    juce::SmoothedValue<float> delayFeedbackSmooth_;
    juce::SmoothedValue<float> delayMixSmooth_;
    juce::SmoothedValue<float> reverbMixSmooth_;
    juce::SmoothedValue<float> reverbDecaySmooth_;
    juce::SmoothedValue<float> outputLevelSmooth_;
    juce::SmoothedValue<float> outputLevelNamSmooth_;

    static constexpr int kSmoothingBlockSize = 32;

    // Metering atomics
    std::atomic<float> inputLevelL_  { -100.0f };
    std::atomic<float> inputLevelR_  { -100.0f };
    std::atomic<float> outputLevelL_ { -100.0f };
    std::atomic<float> outputLevelR_ { -100.0f };

    // Tuner state — see public accessors above for the contract.
    PitchDetector pitchDetector_;
    std::atomic<bool>  tunerActive_   { false };
    std::atomic<float> detectedHz_    { 0.0f };
    std::atomic<float> detectedLevel_ { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskAmpProcessor)
};

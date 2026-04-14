#pragma once

#include "dsp/DuskAmpEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

class DuskAmpProcessor : public juce::AudioProcessor
{
public:
    DuskAmpProcessor();
    ~DuskAmpProcessor() override = default;

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

    // Load a cabinet IR file (called from editor)
    void loadCabinetIR (const juce::File& file)
    {
        engine_.getCabinetIR().loadIR (file);
        cabIRPath_ = file.getFullPathName();
    }

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    DuskAmpEngine engine_;

    // Cab IR path (persisted in state)
    juce::String cabIRPath_;

    // NAM model path (persisted in state)
    juce::String namModelPath_;
    juce::String lastNAMStatus_ { "idle" };

    // Discrete / choice parameter pointers
    std::atomic<float>* ampModeParam_       = nullptr;
    std::atomic<float>* ampTypeParam_       = nullptr;
    std::atomic<float>* oversamplingParam_  = nullptr;
    std::atomic<float>* cabEnabledParam_    = nullptr;
    std::atomic<float>* brightParam_        = nullptr;
    std::atomic<float>* boostEnabledParam_  = nullptr;
    std::atomic<float>* delayEnabledParam_  = nullptr;
    std::atomic<float>* delayTypeParam_     = nullptr;
    std::atomic<float>* reverbEnabledParam_ = nullptr;

    // Continuous float parameter pointers
    std::atomic<float>* inputGainParam_      = nullptr;
    std::atomic<float>* gateThresholdParam_  = nullptr;
    std::atomic<float>* gateReleaseParam_    = nullptr;
    std::atomic<float>* preampGainParam_     = nullptr;
    std::atomic<float>* bassParam_           = nullptr;
    std::atomic<float>* midParam_            = nullptr;
    std::atomic<float>* trebleParam_         = nullptr;
    std::atomic<float>* powerDriveParam_     = nullptr;
    std::atomic<float>* presenceParam_       = nullptr;
    std::atomic<float>* resonanceParam_      = nullptr;
    std::atomic<float>* sagParam_            = nullptr;
    std::atomic<float>* cabMixParam_         = nullptr;
    std::atomic<float>* cabHiCutParam_       = nullptr;
    std::atomic<float>* cabLoCutParam_       = nullptr;
    std::atomic<float>* boostGainParam_      = nullptr;
    std::atomic<float>* boostToneParam_      = nullptr;
    std::atomic<float>* boostLevelParam_     = nullptr;
    std::atomic<float>* delayTimeParam_      = nullptr;
    std::atomic<float>* delayFeedbackParam_  = nullptr;
    std::atomic<float>* delayMixParam_       = nullptr;
    std::atomic<float>* reverbMixParam_      = nullptr;
    std::atomic<float>* reverbDecayParam_    = nullptr;
    std::atomic<float>* reverbPreDelayParam_ = nullptr;
    std::atomic<float>* reverbDampingParam_  = nullptr;
    std::atomic<float>* reverbSizeParam_     = nullptr;
    std::atomic<float>* outputLevelParam_    = nullptr;

    juce::AudioParameterBool* bypassParam_  = nullptr;

    // Cached discrete values
    int cachedAmpMode_        = 0;
    int cachedAmpType_        = 1;
    int cachedOversampling_   = 0;
    int cachedDelayType_      = 0;
    bool cachedCabEnabled_    = true;
    bool cachedBright_        = false;
    bool cachedBoostEnabled_  = false;
    bool cachedDelayEnabled_  = false;
    bool cachedReverbEnabled_ = false;

    // Smoothed values for continuous parameters
    juce::SmoothedValue<float> inputGainSmooth_;
    juce::SmoothedValue<float> gateThresholdSmooth_;
    juce::SmoothedValue<float> gateReleaseSmooth_;
    juce::SmoothedValue<float> preampGainSmooth_;
    juce::SmoothedValue<float> bassSmooth_;
    juce::SmoothedValue<float> midSmooth_;
    juce::SmoothedValue<float> trebleSmooth_;
    juce::SmoothedValue<float> powerDriveSmooth_;
    juce::SmoothedValue<float> presenceSmooth_;
    juce::SmoothedValue<float> resonanceSmooth_;
    juce::SmoothedValue<float> sagSmooth_;
    juce::SmoothedValue<float> cabMixSmooth_;
    juce::SmoothedValue<float> cabHiCutSmooth_;
    juce::SmoothedValue<float> cabLoCutSmooth_;
    juce::SmoothedValue<float> boostGainSmooth_;
    juce::SmoothedValue<float> boostToneSmooth_;
    juce::SmoothedValue<float> boostLevelSmooth_;
    juce::SmoothedValue<float> delayTimeSmooth_;
    juce::SmoothedValue<float> delayFeedbackSmooth_;
    juce::SmoothedValue<float> delayMixSmooth_;
    juce::SmoothedValue<float> reverbMixSmooth_;
    juce::SmoothedValue<float> reverbDecaySmooth_;
    juce::SmoothedValue<float> reverbPreDelaySmooth_;
    juce::SmoothedValue<float> reverbDampingSmooth_;
    juce::SmoothedValue<float> reverbSizeSmooth_;
    juce::SmoothedValue<float> outputLevelSmooth_;

    static constexpr int kSmoothingBlockSize = 32;

    // Metering atomics
    std::atomic<float> inputLevelL_  { -100.0f };
    std::atomic<float> inputLevelR_  { -100.0f };
    std::atomic<float> outputLevelL_ { -100.0f };
    std::atomic<float> outputLevelR_ { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskAmpProcessor)
};

#pragma once

#include "dsp/DuskVerbEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

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

    // Direct XML access for preset management
    std::unique_ptr<juce::XmlElement> getStateXML();
    void setStateXML (const juce::XmlElement& xml);

    // Per-preset level correction (called from editor on factory preset load)
    void setGainTrim (float dB) { engine_.setGainTrim (dB); }

    // Per-preset custom early reflection taps (called from editor on factory preset load)
    void setCustomERTaps (const CustomERTap* taps, int numTaps);

    juce::AudioProcessorValueTreeState parameters;

    // Level metering (audio thread writes, UI thread reads)
    float getInputLevelL() const  { return inputLevelL_.load (std::memory_order_relaxed); }
    float getInputLevelR() const  { return inputLevelR_.load (std::memory_order_relaxed); }
    float getOutputLevelL() const { return outputLevelL_.load (std::memory_order_relaxed); }
    float getOutputLevelR() const { return outputLevelR_.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    DuskVerbEngine engine_;

    std::atomic<float>* algorithmParam_ = nullptr;
    int cachedAlgorithm_ = 1; // Hall default

    // Track sample rate / block size to make prepareToPlay idempotent
    // (pedalboard calls it before every process() which would otherwise
    // tear down and rebuild the engine on every call).
    double preparedSampleRate_ = 0.0;
    int preparedBlockSize_ = 0;

    std::atomic<float>* decayParam_     = nullptr;
    std::atomic<float>* preDelayParam_  = nullptr;
    std::atomic<float>* sizeParam_      = nullptr;
    std::atomic<float>* dampingParam_   = nullptr;
    std::atomic<float>* bassMultParam_  = nullptr;
    std::atomic<float>* crossoverParam_ = nullptr;
    std::atomic<float>* diffusionParam_ = nullptr;
    std::atomic<float>* modDepthParam_  = nullptr;
    std::atomic<float>* modRateParam_   = nullptr;
    std::atomic<float>* erLevelParam_   = nullptr;
    std::atomic<float>* erSizeParam_    = nullptr;
    std::atomic<float>* mixParam_       = nullptr;
    std::atomic<float>* loCutParam_     = nullptr;
    std::atomic<float>* hiCutParam_     = nullptr;
    std::atomic<float>* widthParam_     = nullptr;
    std::atomic<float>* freezeParam_    = nullptr;
    std::atomic<float>* predelaySyncParam_ = nullptr;
    std::atomic<float>* busModeParam_ = nullptr;
    std::atomic<float>* gateHoldParam_ = nullptr;
    std::atomic<float>* gateReleaseParam_ = nullptr;
    std::atomic<float>* gainTrimParam_ = nullptr;
    std::atomic<float>* inputOnsetParam_ = nullptr;
    std::atomic<float>* delayScaleParam_ = nullptr;
    std::atomic<float>* softOnsetParam_ = nullptr;
    std::atomic<float>* lateFeedFwdParam_ = nullptr;
    std::atomic<float>* limiterThreshParam_ = nullptr;
    std::atomic<float>* tapPosParams_[14] = {};
    std::atomic<float>* tapGainParams_[14] = {};
    std::atomic<float>* presetIdParam_ = nullptr;
    int lastPresetId_ = 0;

    std::atomic<float>* airDampingParam_ = nullptr;
    std::atomic<float>* highCrossoverParam_ = nullptr;
    std::atomic<float>* noiseModParam_ = nullptr;
    std::atomic<float>* inlineDiffParam_ = nullptr;
    std::atomic<float>* stereoCouplingParam_ = nullptr;
    std::atomic<float>* chorusDepthParam_ = nullptr;
    std::atomic<float>* chorusRateParam_ = nullptr;
    std::atomic<float>* outputGainParam_ = nullptr;
    std::atomic<float>* erCrossfeedParam_ = nullptr;
    std::atomic<float>* decayTimeScaleParam_ = nullptr;
    std::atomic<float>* decayBoostParam_ = nullptr;
    std::atomic<float>* structHFDampParam_ = nullptr;
    std::atomic<float>* outputLowShelfDBParam_ = nullptr;
    std::atomic<float>* outputHighShelfDBParam_ = nullptr;
    std::atomic<float>* outputHighShelfHzParam_ = nullptr;
    std::atomic<float>* outputMidEQDBParam_ = nullptr;
    std::atomic<float>* outputMidEQHzParam_ = nullptr;
    std::atomic<float>* terminalThresholdParam_ = nullptr;
    std::atomic<float>* terminalFactorParam_ = nullptr;
    std::atomic<float>* erAirCeilingParam_ = nullptr;
    std::atomic<float>* erAirFloorParam_ = nullptr;

    juce::AudioParameterBool* bypassParam_ = nullptr;

    juce::SmoothedValue<float> decaySmooth_;
    juce::SmoothedValue<float> preDelaySmooth_;
    juce::SmoothedValue<float> sizeSmooth_;
    juce::SmoothedValue<float> dampingSmooth_;
    juce::SmoothedValue<float> bassMultSmooth_;
    juce::SmoothedValue<float> crossoverSmooth_;
    juce::SmoothedValue<float> diffusionSmooth_;
    juce::SmoothedValue<float> modDepthSmooth_;
    juce::SmoothedValue<float> modRateSmooth_;
    juce::SmoothedValue<float> erLevelSmooth_;
    juce::SmoothedValue<float> erSizeSmooth_;
    juce::SmoothedValue<float> mixSmooth_;
    juce::SmoothedValue<float> loCutSmooth_;
    juce::SmoothedValue<float> hiCutSmooth_;
    juce::SmoothedValue<float> widthSmooth_;

    static constexpr int kSmoothingBlockSize = 32;

    // Metering atomics
    std::atomic<float> inputLevelL_  { -100.0f };
    std::atomic<float> inputLevelR_  { -100.0f };
    std::atomic<float> outputLevelL_ { -100.0f };
    std::atomic<float> outputLevelR_ { -100.0f };

    int originalLatencySamples_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskVerbProcessor)
};

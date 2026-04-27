#pragma once

#include "dsp/DuskVerbEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

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

    // Level meters (audio thread writes, UI thread reads).
    float getInputLevelL()  const { return inputLevelL_.load (std::memory_order_relaxed); }
    float getInputLevelR()  const { return inputLevelR_.load (std::memory_order_relaxed); }
    float getOutputLevelL() const { return outputLevelL_.load (std::memory_order_relaxed); }
    float getOutputLevelR() const { return outputLevelR_.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    DuskVerbEngine engine_;

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
    std::atomic<float>* saturationParam_    = nullptr;
    std::atomic<float>* diffusionParam_     = nullptr;
    std::atomic<float>* erLevelParam_       = nullptr;
    std::atomic<float>* erSizeParam_        = nullptr;
    std::atomic<float>* loCutParam_         = nullptr;
    std::atomic<float>* hiCutParam_         = nullptr;
    std::atomic<float>* widthParam_         = nullptr;
    std::atomic<float>* freezeParam_        = nullptr;
    std::atomic<float>* gainTrimParam_      = nullptr;
    std::atomic<float>* monoBelowParam_     = nullptr;

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
    bool  lastFreeze_      = false;
    bool  haveLastFreeze_  = false;

    double preparedSampleRate_ = 0.0;
    int    preparedBlockSize_  = 0;

    // Metering atomics
    std::atomic<float> inputLevelL_  { -100.0f };
    std::atomic<float> inputLevelR_  { -100.0f };
    std::atomic<float> outputLevelL_ { -100.0f };
    std::atomic<float> outputLevelR_ { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuskVerbProcessor)
};

#pragma once

#include <JuceHeader.h>
#include "DSP/DragonflyReverb.h"
#include "PresetManager.h"

//==============================================================================
class StudioReverbAudioProcessor  : public juce::AudioProcessor,
                                    public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    StudioReverbAudioProcessor();
    ~StudioReverbAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Parameters using AudioProcessorValueTreeState for better thread safety
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameter pointers for quick access
    juce::AudioParameterChoice* reverbType;

    // Mix controls (Dragonfly-style)
    juce::AudioParameterFloat* dryLevel;
    juce::AudioParameterFloat* earlyLevel;
    juce::AudioParameterFloat* earlySend;
    juce::AudioParameterFloat* lateLevel;

    // Basic reverb parameters
    juce::AudioParameterFloat* size;
    juce::AudioParameterFloat* width;
    juce::AudioParameterFloat* preDelay;
    juce::AudioParameterFloat* decay;
    juce::AudioParameterFloat* diffuse;

    // Modulation controls
    juce::AudioParameterFloat* spin;
    juce::AudioParameterFloat* wander;

    // Filter controls
    juce::AudioParameterFloat* highCut;
    juce::AudioParameterFloat* lowCut;

    // Hall-specific crossover controls
    juce::AudioParameterFloat* lowCross;
    juce::AudioParameterFloat* highCross;
    juce::AudioParameterFloat* lowMult;
    juce::AudioParameterFloat* highMult;

    // Parameter change detection
    std::atomic<bool> parametersChanged { true };

    // Preset management
    PresetManager presetManager;
    void loadPreset(const juce::String& presetName);
    void loadPresetForAlgorithm(const juce::String& presetName, int algorithmIndex);

    // Static parameter ID list to avoid duplication
    static const juce::StringArray& getParameterIDs()
    {
        static const juce::StringArray ids = {
            "reverbType", "dryLevel", "earlyLevel", "earlySend", "lateLevel",
            "size", "width", "preDelay", "decay", "diffuse",
            "spin", "wander", "highCut", "lowCut",
            "lowCross", "highCross", "lowMult", "highMult"
        };
        return ids;
    }

private:
    std::unique_ptr<DragonflyReverb> reverb;
    void updateReverbParameters();

    // Parameter listeners
    void parameterChanged(const juce::String& parameterID, float newValue);

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioReverbAudioProcessor)
};
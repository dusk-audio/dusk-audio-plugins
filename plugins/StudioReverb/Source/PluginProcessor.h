#pragma once

#include <JuceHeader.h>
#include "DSP/DragonflyReverb.h"

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
    juce::AudioParameterFloat* roomSize;
    juce::AudioParameterFloat* damping;
    juce::AudioParameterFloat* preDelay;
    juce::AudioParameterFloat* decayTime;
    juce::AudioParameterFloat* diffusion;
    juce::AudioParameterFloat* wetLevel;
    juce::AudioParameterFloat* dryLevel;
    juce::AudioParameterFloat* width;

    // Parameter change detection
    std::atomic<bool> parametersChanged { true };

private:
    std::unique_ptr<DragonflyReverb> reverb;
    void updateReverbParameters();

    // Parameter listeners
    void parameterChanged(const juce::String& parameterID, float newValue);

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioReverbAudioProcessor)
};
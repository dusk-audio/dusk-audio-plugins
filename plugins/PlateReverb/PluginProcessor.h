/*
  ==============================================================================

    Plate Reverb - Plugin Processor
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "DattorroPlate.h"

//==============================================================================
class PlateReverbAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    PlateReverbAudioProcessor();
    ~PlateReverbAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;

    // DSP - Dattorro is inherently mono-in, stereo-out, so we only need one instance
    DattorroPlate reverb;

    // Parameter pointers
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlateReverbAudioProcessor)
};

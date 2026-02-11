/*
  ==============================================================================

    PluginProcessor.h
    Suede 200 — Vintage Digital Reverberator

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Suede200Reverb.h"
#include "Suede200Presets.h"

class Suede200Processor : public juce::AudioProcessor
{
public:
    Suede200Processor();
    ~Suede200Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Metering
    float getOutputLevelL() const { return outputLevelL.load(); }
    float getOutputLevelR() const { return outputLevelR.load(); }

    // RT60 readout for UI display
    float getRT60Display() const { return reverbEngine.getTargetRT60(); }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    Suede200::Suede200Reverb reverbEngine;

    // Parameter pointers
    std::atomic<float>* programParam = nullptr;
    std::atomic<float>* predelayParam = nullptr;
    std::atomic<float>* reverbTimeParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* preEchoesParam = nullptr;
    std::atomic<float>* diffusionParam = nullptr;
    std::atomic<float>* rtLowParam = nullptr;
    std::atomic<float>* rtHighParam = nullptr;
    std::atomic<float>* rolloffParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    // Smoothed parameters
    juce::SmoothedValue<float> smoothedPreDelay;
    juce::SmoothedValue<float> smoothedReverbTime;
    juce::SmoothedValue<float> smoothedSize;
    juce::SmoothedValue<float> smoothedMix;

    // Tracking for discrete parameter changes
    int lastProgram = -1;
    int lastDiffusion = -1;
    int lastRTLow = -1;
    int lastRTHigh = -1;
    int lastRolloff = -1;
    int lastPreEchoes = -1;

    // Factory preset index
    int currentPresetIndex = 0;

    // Metering
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Suede200Processor)
};

/*
  ==============================================================================

    PluginProcessor.h
    SilkVerb - Algorithmic Reverb with Plate, Room, Hall modes

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "FDNReverb.h"

class SilkVerbProcessor : public juce::AudioProcessor
{
public:
    SilkVerbProcessor();
    ~SilkVerbProcessor() override;

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

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SilkVerb::FDNReverb reverbEngine;

    // Parameter pointers - Main controls
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* colorParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* preDelayParam = nullptr;

    // Parameter pointers - Modulation
    std::atomic<float>* modRateParam = nullptr;
    std::atomic<float>* modDepthParam = nullptr;

    // Parameter pointers - Bass decay
    std::atomic<float>* bassMultParam = nullptr;
    std::atomic<float>* bassFreqParam = nullptr;

    // Parameter pointers - Diffusion
    std::atomic<float>* earlyDiffParam = nullptr;
    std::atomic<float>* lateDiffParam = nullptr;

    // Parameter pointers - Output EQ
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;

    // Parameter pointers - Freeze
    std::atomic<float>* freezeParam = nullptr;

    // Smoothed parameters
    juce::SmoothedValue<float> smoothedSize;
    juce::SmoothedValue<float> smoothedDamping;
    juce::SmoothedValue<float> smoothedWidth;
    juce::SmoothedValue<float> smoothedMix;
    juce::SmoothedValue<float> smoothedPreDelay;
    juce::SmoothedValue<float> smoothedModRate;
    juce::SmoothedValue<float> smoothedModDepth;
    juce::SmoothedValue<float> smoothedBassMult;
    juce::SmoothedValue<float> smoothedBassFreq;
    juce::SmoothedValue<float> smoothedEarlyDiff;
    juce::SmoothedValue<float> smoothedLateDiff;
    juce::SmoothedValue<float> smoothedHighCut;
    juce::SmoothedValue<float> smoothedLowCut;

    // Current mode/color tracking
    int lastMode = -1;
    int lastColor = -1;

    // Metering
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilkVerbProcessor)
};

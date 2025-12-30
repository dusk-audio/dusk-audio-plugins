/*
  ==============================================================================

    PluginProcessor.h
    Tape Echo - RE-201 Style Tape Delay Plugin

    Signal Flow:
    Input → Input Gain → [Echo Section] → Echo Volume →
                       → [Reverb Section] → Reverb Volume →
                       → Dry/Wet Mix → Output

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/TapeEcho.h"
#include "DSP/SpringReverb.h"
#include <array>

class TapeEchoProcessor : public juce::AudioProcessor
{
public:
    TapeEchoProcessor();
    ~TapeEchoProcessor() override;

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
    float getInputLevelL() const { return inputLevelL.load(); }
    float getInputLevelR() const { return inputLevelR.load(); }
    float getOutputLevelL() const { return outputLevelL.load(); }
    float getOutputLevelR() const { return outputLevelR.load(); }

    // For tape visualization (thread-safe via atomics)
    float getCurrentTapeSpeed() const { return currentTapeSpeed.load(); }
    int getCurrentMode() const { return currentMode.load(); }
    bool isHeadActive(int head) const
    {
        if (head >= 0 && head < 3)
            return headActiveState[static_cast<size_t>(head)].load();
        return false;
    }
    float getFeedbackLevel() const { return feedbackLevel.load(); }

    // Transport state for reel animation
    bool isProcessing() const { return isProcessingAudio.load(std::memory_order_relaxed); }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // DSP engines
    TapeEchoDSP::TapeEchoEngine echoEngine;
    TapeEchoDSP::SpringReverb springReverb;

    // Oversampling (2x for alias-free saturation)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    // Tone controls
    juce::dsp::IIR::Filter<float> bassFilterL;
    juce::dsp::IIR::Filter<float> bassFilterR;
    juce::dsp::IIR::Filter<float> trebleFilterL;
    juce::dsp::IIR::Filter<float> trebleFilterR;

    // Parameter pointers
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* repeatRateParam = nullptr;
    std::atomic<float>* intensityParam = nullptr;
    std::atomic<float>* echoVolumeParam = nullptr;
    std::atomic<float>* reverbVolumeParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* bassParam = nullptr;
    std::atomic<float>* trebleParam = nullptr;
    std::atomic<float>* wowFlutterParam = nullptr;
    std::atomic<float>* dryWetParam = nullptr;
    std::atomic<float>* tempoSyncParam = nullptr;
    std::atomic<float>* noteDivisionParam = nullptr;

    // Smoothed parameters
    juce::SmoothedValue<float> smoothedInputGain;
    juce::SmoothedValue<float> smoothedEchoVolume;
    juce::SmoothedValue<float> smoothedReverbVolume;
    juce::SmoothedValue<float> smoothedDryWet;
    juce::SmoothedValue<float> smoothedBass;
    juce::SmoothedValue<float> smoothedTreble;

    // Metering
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };

    // Visualization state (thread-safe)
    std::atomic<float> currentTapeSpeed { 1.0f };
    std::atomic<int> currentMode { 1 };
    std::atomic<float> feedbackLevel { 0.0f };
    std::array<std::atomic<bool>, 3> headActiveState { { true, false, false } };
    std::atomic<bool> isProcessingAudio { false };

    // Sample rate
    double currentSampleRate = 44100.0;

    // Processing state flag - true only when fully ready to process
    std::atomic<bool> readyToProcess { false };

    // SpinLock to prevent concurrent access during prepare/process
    juce::SpinLock processLock;

    // Last mode for change detection
    int lastMode = -1;

    // Tempo sync
    double lastBpm = 120.0;

    void updateToneFilters();

    // Convert note division index to delay time in ms at given BPM
    // Note divisions: 0=1/1, 1=1/2, 2=1/2T, 3=1/4, 4=1/4T, 5=1/8, 6=1/8T,
    //                 7=1/16, 8=1/16T, 9=1/32, 10=1/32T, 11=1/1D, 12=1/2D, 13=1/4D, 14=1/8D
    static float getDelayTimeForNoteDivision(int division, double bpm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoProcessor)
};

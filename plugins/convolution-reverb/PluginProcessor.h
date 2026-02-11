/*
  ==============================================================================

    Convolution Reverb - Plugin Processor
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ConvolutionEngine.h"
#include "EnvelopeProcessor.h"
#include "WetSignalEQ.h"

class ConvolutionReverbProcessor : public juce::AudioProcessor
{
public:
    ConvolutionReverbProcessor();
    ~ConvolutionReverbProcessor() override;

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

    // Parameter access
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // IR loading interface
    void loadImpulseResponse(const juce::File& irFile);
    void clearImpulseResponse();

    // IR information
    juce::String getCurrentIRName() const;
    juce::String getCurrentIRPath() const;
    juce::AudioBuffer<float> getCurrentIRWaveform() const;
    double getCurrentIRSampleRate() const;
    float getCurrentIRLengthSeconds() const;
    bool isIRLoaded() const { return irLoaded.load(); }

    // Metering (mono)
    float getInputLevel() const { return inputMeter.load(); }
    float getOutputLevel() const { return outputMeter.load(); }

    // Metering (stereo)
    float getInputLevelL() const { return inputMeterL.load(); }
    float getInputLevelR() const { return inputMeterR.load(); }
    float getOutputLevelL() const { return outputMeterL.load(); }
    float getOutputLevelR() const { return outputMeterR.load(); }

    // Apply pending IR changes (call from message thread, e.g., timer callback)
    void applyPendingIRChanges() { convolutionEngine.applyPendingChanges(); }

    // IR directory settings
    void setCustomIRDirectory(const juce::File& directory);
    juce::File getCustomIRDirectory() const;
    juce::File getDefaultIRDirectory() const;

private:
    juce::AudioProcessorValueTreeState parameters;

    // Convolution engine
    ConvolutionEngine convolutionEngine;

    // Pre-delay
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayL;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayR;

    // Wet signal processing
    WetSignalEQ wetEQ;
    juce::dsp::StateVariableTPTFilter<float> wetHighpass;
    juce::dsp::StateVariableTPTFilter<float> wetLowpass;

    // Envelope processor
    EnvelopeProcessor envelopeProcessor;

    // Processing buffers
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;

    // Current IR data (for display)
    juce::AudioBuffer<float> currentIRWaveform;
    double currentIRSampleRate = 44100.0;
    juce::String currentIRName;
    juce::String currentIRPath;
    std::atomic<bool> irLoaded{false};

    // Thread safety
    mutable juce::CriticalSection irLock;

    // Metering (mono - max of stereo channels)
    std::atomic<float> inputMeter{-60.0f};
    std::atomic<float> outputMeter{-60.0f};

    // Metering (stereo L/R)
    std::atomic<float> inputMeterL{-60.0f};
    std::atomic<float> inputMeterR{-60.0f};
    std::atomic<float> outputMeterL{-60.0f};
    std::atomic<float> outputMeterR{-60.0f};

    // Settings
    juce::File customIRDirectory;

    // Current sample rate
    double currentSampleRate = 44100.0;

    // Parameter pointers (for efficient access)
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* preDelayParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* reverseParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* hpfFreqParam = nullptr;
    std::atomic<float>* lpfFreqParam = nullptr;
    std::atomic<float>* eqLowFreqParam = nullptr;
    std::atomic<float>* eqLowGainParam = nullptr;
    std::atomic<float>* eqLowMidFreqParam = nullptr;
    std::atomic<float>* eqLowMidGainParam = nullptr;
    std::atomic<float>* eqHighMidFreqParam = nullptr;
    std::atomic<float>* eqHighMidGainParam = nullptr;
    std::atomic<float>* eqHighFreqParam = nullptr;
    std::atomic<float>* eqHighGainParam = nullptr;
    std::atomic<float>* zeroLatencyParam = nullptr;

    // New parameters
    std::atomic<float>* irOffsetParam = nullptr;        // IR start offset (0-1)
    std::atomic<float>* qualityParam = nullptr;         // Quality/sample rate (0=Lo-Fi, 1=Low, 2=Medium, 3=High)
    std::atomic<float>* volumeCompParam = nullptr;      // Volume compensation on/off
    std::atomic<float>* filterEnvEnabledParam = nullptr; // Filter envelope on/off
    std::atomic<float>* filterEnvInitFreqParam = nullptr; // Filter envelope initial frequency
    std::atomic<float>* filterEnvEndFreqParam = nullptr;  // Filter envelope end frequency
    std::atomic<float>* filterEnvAttackParam = nullptr;   // Filter envelope attack time
    std::atomic<float>* stereoModeParam = nullptr;        // Stereo mode (0=True Stereo, 1=Mono-to-Stereo)

    // Internal methods
    void updateFilters();
    void updatePreDelay();
    void applyWidth(juce::AudioBuffer<float>& buffer, float width);
    float calculateRMS(const juce::AudioBuffer<float>& buffer);
    float calculateChannelRMS(const juce::AudioBuffer<float>& buffer, int channel);

    // Create parameter layout
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionReverbProcessor)
    JUCE_DECLARE_WEAK_REFERENCEABLE(ConvolutionReverbProcessor)
};

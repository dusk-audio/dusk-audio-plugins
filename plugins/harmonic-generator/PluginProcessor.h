#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "HardwarePresets.h"

class HarmonicGeneratorAudioProcessor : public juce::AudioProcessor
{
public:
    HarmonicGeneratorAudioProcessor();
    ~HarmonicGeneratorAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Harmonic Generator"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Parameter access via APVTS for thread safety
    juce::AudioProcessorValueTreeState apvts;

    // Level metering
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };

    // Hardware saturation control (public for UI access)
    void setHardwareMode(HardwareSaturation::Mode mode);
    HardwareSaturation::Mode getHardwareMode() const;

private:
    // Parameter Layout
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameter pointers (managed by APVTS, no manual deletion needed)
    std::atomic<float>* hardwareMode = nullptr;  // Hardware preset selector
    std::atomic<float>* secondHarmonic = nullptr;
    std::atomic<float>* thirdHarmonic = nullptr;
    std::atomic<float>* fourthHarmonic = nullptr;
    std::atomic<float>* fifthHarmonic = nullptr;
    std::atomic<float>* evenHarmonics = nullptr;
    std::atomic<float>* oddHarmonics = nullptr;
    std::atomic<float>* warmth = nullptr;
    std::atomic<float>* brightness = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* wetDryMix = nullptr;
    std::atomic<float>* tone = nullptr;

    void processHarmonics(juce::dsp::AudioBlock<float>& block);
    float generateHarmonics(float input, float second, float third, float fourth, float fifth);

    // Hardware saturation engine
    HardwareSaturation hardwareSaturation;

    juce::dsp::Oversampling<float> oversampling;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> highPassFilterL, highPassFilterR;
    juce::AudioBuffer<float> dryBuffer;
    double lastSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicGeneratorAudioProcessor)
};
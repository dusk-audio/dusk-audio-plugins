#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

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

    // Parameters
    juce::AudioParameterBool* oversamplingSwitch;
    juce::AudioParameterFloat* secondHarmonic;
    juce::AudioParameterFloat* thirdHarmonic;
    juce::AudioParameterFloat* fourthHarmonic;
    juce::AudioParameterFloat* fifthHarmonic;
    juce::AudioParameterFloat* evenHarmonics;
    juce::AudioParameterFloat* oddHarmonics;
    juce::AudioParameterFloat* warmth;
    juce::AudioParameterFloat* brightness;
    juce::AudioParameterFloat* drive;
    juce::AudioParameterFloat* outputGain;
    juce::AudioParameterFloat* wetDryMix;

    // Level metering
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };

private:
    void processHarmonics(juce::dsp::AudioBlock<float>& block);
    float generateHarmonics(float input, float second, float third, float fourth, float fifth);

    juce::dsp::Oversampling<float> oversampling;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> highPassFilterL, highPassFilterR;
    juce::AudioBuffer<float> dryBuffer;
    double lastSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicGeneratorAudioProcessor)
};
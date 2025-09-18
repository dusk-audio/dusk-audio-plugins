#pragma once

#include <JuceHeader.h>
#include "FilterCoefficients.h"

class SSL4000EQAudioProcessor : public juce::AudioProcessor
{
public:
    SSL4000EQAudioProcessor();
    ~SSL4000EQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Oversampling for anti-aliasing
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    static constexpr int oversamplingFactor = 2;

    // Filter chains for each channel
    struct ChannelFilters
    {
        // High-pass filter (3rd order = 18dB/oct)
        juce::dsp::IIR::Filter<float> hpf1, hpf2, hpf3;

        // Low-frequency band
        juce::dsp::IIR::Filter<float> lfBand;

        // Low-mid band
        juce::dsp::IIR::Filter<float> lmBand;

        // High-mid band
        juce::dsp::IIR::Filter<float> hmBand;

        // High-frequency band
        juce::dsp::IIR::Filter<float> hfBand;

        // Low-pass filter (2nd order = 12dB/oct)
        juce::dsp::IIR::Filter<float> lpf1, lpf2;
    };

    ChannelFilters leftChannel, rightChannel;

    // Saturation amount
    float saturationAmount = 0.05f;

    // Update filter coefficients
    void updateFilters();
    void updateHPF(float freq);
    void updateLPF(float freq);
    void updateLFBand(float freq, float gain, bool isBell);
    void updateLMBand(float freq, float gain, float q);
    void updateHMBand(float freq, float gain, float q);
    void updateHFBand(float freq, float gain, bool isBell);

    // Process saturation
    float processSaturation(float sample);

    // Current sample rate
    double currentSampleRate = 44100.0;

    // Parameter IDs
    static constexpr const char* hpfFreqID = "hpf_freq";
    static constexpr const char* lpfFreqID = "lpf_freq";
    static constexpr const char* lfFreqID = "lf_freq";
    static constexpr const char* lfGainID = "lf_gain";
    static constexpr const char* lmFreqID = "lm_freq";
    static constexpr const char* lmGainID = "lm_gain";
    static constexpr const char* lmQID = "lm_q";
    static constexpr const char* hmFreqID = "hm_freq";
    static constexpr const char* hmGainID = "hm_gain";
    static constexpr const char* hmQID = "hm_q";
    static constexpr const char* hfFreqID = "hf_freq";
    static constexpr const char* hfGainID = "hf_gain";
    static constexpr const char* variantID = "variant";
    static constexpr const char* bypassID = "bypass";
    static constexpr const char* outputGainID = "output_gain";
    static constexpr const char* lfBellID = "lf_bell";
    static constexpr const char* hfBellID = "hf_bell";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SSL4000EQAudioProcessor)
};
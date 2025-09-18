#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>

// Forward declaration for LV2 inline display

//==============================================================================
/**
    SSL 4000 Series Console EQ Emulation

    Features:
    - 4-band parametric EQ (LF, LM, HM, HF)
    - High-pass and low-pass filters
    - Brown/Black knob variants
    - 2x/4x oversampling for anti-aliasing
    - Analog-modeled nonlinearities
*/
class FourKEQ : public juce::AudioProcessor
{
public:
    //==============================================================================
    FourKEQ();
    ~FourKEQ() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Public parameter access for GUI and inline display
    juce::AudioProcessorValueTreeState parameters;

    #ifdef JucePlugin_Build_LV2
    #endif

private:
    //==============================================================================
    // Filter chain for stereo processing
    struct FilterBand
    {
        juce::dsp::IIR::Filter<float> filter;
        juce::dsp::IIR::Filter<float> filterR;  // Right channel

        void reset()
        {
            filter.reset();
            filterR.reset();
        }

        void prepare(const juce::dsp::ProcessSpec& spec)
        {
            filter.prepare(spec);
            filterR.prepare(spec);
        }
    };

    // HPF: 3rd order (18 dB/oct) implemented as cascaded filters
    struct HighPassFilter
    {
        FilterBand stage1;  // First biquad
        FilterBand stage2;  // Second biquad (combined gives ~18dB/oct)

        void reset()
        {
            stage1.reset();
            stage2.reset();
        }

        void prepare(const juce::dsp::ProcessSpec& spec)
        {
            stage1.prepare(spec);
            stage2.prepare(spec);
        }
    };

    // LPF: 2nd order (12 dB/oct)
    FilterBand lpfFilter;

    // HPF structure
    HighPassFilter hpfFilter;

    // 4-band EQ filters
    FilterBand lfFilter;   // Low frequency
    FilterBand lmFilter;   // Low-mid frequency
    FilterBand hmFilter;   // High-mid frequency
    FilterBand hfFilter;   // High frequency

    // Oversampling
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    int oversamplingFactor = 2;

    // Parameter pointers
    std::atomic<float>* hpfFreqParam = nullptr;
    std::atomic<float>* lpfFreqParam = nullptr;

    std::atomic<float>* lfGainParam = nullptr;
    std::atomic<float>* lfFreqParam = nullptr;
    std::atomic<float>* lfBellParam = nullptr;

    std::atomic<float>* lmGainParam = nullptr;
    std::atomic<float>* lmFreqParam = nullptr;
    std::atomic<float>* lmQParam = nullptr;

    std::atomic<float>* hmGainParam = nullptr;
    std::atomic<float>* hmFreqParam = nullptr;
    std::atomic<float>* hmQParam = nullptr;

    std::atomic<float>* hfGainParam = nullptr;
    std::atomic<float>* hfFreqParam = nullptr;
    std::atomic<float>* hfBellParam = nullptr;

    std::atomic<float>* eqTypeParam = nullptr;     // 0 = Brown, 1 = Black
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* saturationParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr; // 0 = 2x, 1 = 4x

    // Processing state
    double currentSampleRate = 44100.0;

    // Filter update methods
    void updateFilters();
    void updateHPF(double sampleRate);
    void updateLPF(double sampleRate);
    void updateLFBand(double sampleRate);
    void updateLMBand(double sampleRate);
    void updateHMBand(double sampleRate);
    void updateHFBand(double sampleRate);

    // Helper methods
    float calculateDynamicQ(float gain, float baseQ) const;
    float applySaturation(float sample, float amount) const;

    // Parameter creation
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQ)
};
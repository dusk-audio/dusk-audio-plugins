#pragma once

#include <JuceHeader.h>
#include "DSP/TapeDelay.h"
#include "DSP/SpringReverb.h"
#include "DSP/PreampSaturation.h"

class TapeEchoProcessor : public juce::AudioProcessor
{
public:
    TapeEchoProcessor();
    ~TapeEchoProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

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

    // Parameter IDs
    static const juce::String PARAM_MODE;
    static const juce::String PARAM_REPEAT_RATE;
    static const juce::String PARAM_INTENSITY;
    static const juce::String PARAM_ECHO_VOLUME;
    static const juce::String PARAM_REVERB_VOLUME;
    static const juce::String PARAM_BASS;
    static const juce::String PARAM_TREBLE;
    static const juce::String PARAM_INPUT_VOLUME;
    static const juce::String PARAM_WOW_FLUTTER;
    static const juce::String PARAM_TAPE_AGE;
    static const juce::String PARAM_MOTOR_TORQUE;
    static const juce::String PARAM_STEREO_MODE;
    static const juce::String PARAM_LFO_SHAPE;
    static const juce::String PARAM_LFO_RATE;

    // Get current peak level for VU meter
    float getCurrentPeakLevel() const { return currentPeakLevel.load(); }

    // Parameters accessible to GUI
    juce::AudioProcessorValueTreeState apvts;

    // Mode definitions
    enum Mode
    {
        Mode1_ShortEcho = 0,
        Mode2_MediumEcho,
        Mode3_LongEcho,
        Mode4_ShortMedium,
        Mode5_ShortLong,
        Mode6_MediumLong,
        Mode7_AllHeads,
        Mode8_ShortMediumReverb,
        Mode9_ShortLongReverb,
        Mode10_MediumLongReverb,
        Mode11_AllHeadsReverb,
        Mode12_ReverbOnly,
        NumModes
    };

    // Factory presets
    struct Preset
    {
        juce::String name;
        float repeatRate;
        float intensity;
        float echoVolume;
        float reverbVolume;
        float bass;
        float treble;
        float inputVolume;
        float wowFlutter;
        float tapeAge;
        int mode;
    };

    static const std::vector<Preset>& getFactoryPresets();
    void loadPreset(const Preset& preset);

private:
    // DSP components
    TapeDelay tapeDelay;
    SpringReverb springReverb;
    PreampSaturation preamp;

    // EQ filters
    juce::IIRFilter bassFilterL, bassFilterR;
    juce::IIRFilter trebleFilterL, trebleFilterR;

    // Feedback storage (for routing delay output through EQ before feedback)
    float lastDelayOutputL = 0.0f;
    float lastDelayOutputR = 0.0f;

    // Level monitoring
    std::atomic<float> currentPeakLevel{0.0f};
    float peakDecay = 0.99f;

    // Mode configuration
    struct ModeConfig
    {
        bool head1, head2, head3;
        bool reverb;
        float delayTimes[3];
    };

    ModeConfig modeConfigs[NumModes];

    void initializeModeConfigs();
    void updateDelayConfiguration();
    void updateEQFilters();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoProcessor)
};
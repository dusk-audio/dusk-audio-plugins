/*
  ==============================================================================

    VintageVerb - Classic Digital Reverb Emulation
    Inspired by legendary hardware units from the 1970s and 1980s

    Copyright (c) 2024 - Vintage Audio Labs

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../DSP/SimpleReverbEngine.h"
#include "../DSP/ReverbEngine.h"
#include "../DSP/VintageColoration.h"
#include "../DSP/DualEngineRouter.h"
#include "../Presets/PresetManager.h"

//==============================================================================
class VintageVerbAudioProcessor : public juce::AudioProcessor,
                                  public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    VintageVerbAudioProcessor();
    ~VintageVerbAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

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
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter Tree
    juce::AudioProcessorValueTreeState& getAPVTS() { return parameters; }

    // Reverb Modes (22 algorithms inspired by Valhalla VintageVerb)
    enum ReverbMode
    {
        ConcertHall = 0,
        BrightHall,
        Plate,
        Room,
        Chamber,
        RandomSpace,
        ChorusSpace,
        Ambience,
        Sanctuary,
        DirtyHall,
        DirtyPlate,
        SmoothPlate,
        SmoothRoom,
        SmoothRandom,
        Nonlin,
        ChaoticHall,
        ChaoticChamber,
        ChaoticNeutral,
        Cathedral,
        Palace,
        Chamber1979,
        Hall1984,
        NumModes
    };

    // Color Modes (Era-specific processing)
    enum ColorMode
    {
        Color1970s = 0,  // Dark, noisy, lo-fi
        Color1980s,      // Bright, funky, digital artifacts
        ColorNow,        // Clean, transparent, modern
        NumColorModes
    };

    // Dual Engine Routing (inspired by Relab LX480)
    enum RoutingMode
    {
        Series = 0,      // Engine A -> Engine B
        Parallel,        // Engine A + Engine B
        AtoB,           // A processed by B
        BtoA,           // B processed by A
        NumRoutingModes
    };

    // Get current processing info for meters
    float getInputLevel(int channel) const;
    float getOutputLevel(int channel) const;
    float getCurrentDecayTime() const;

    // Preset management
    PresetManager* getPresetManager() { return &presetManager; }

    // Parameter change callback
    void parameterChanged (const juce::String& parameterID, float newValue) override;

private:
    //==============================================================================
    // DSP Components
    std::unique_ptr<SimpleReverbEngine> simpleReverb;  // Simple working reverb
    std::unique_ptr<ReverbEngine> engineA;
    std::unique_ptr<ReverbEngine> engineB;
    std::unique_ptr<VintageColoration> vintageProcessor;
    std::unique_ptr<DualEngineRouter> router;

    // Filters and EQ
    juce::dsp::StateVariableTPTFilter<float> highpassFilter;
    juce::dsp::StateVariableTPTFilter<float> lowpassFilter;
    juce::dsp::IIR::Filter<float> tiltEQ[2];  // Stereo tilt EQ

    // Delay lines for pre-delay
    juce::dsp::DelayLine<float> predelayLeft{192000};
    juce::dsp::DelayLine<float> predelayRight{192000};

    // Modulation LFOs
    juce::dsp::Oscillator<float> modLFO1;
    juce::dsp::Oscillator<float> modLFO2;

    // Meters and analysis
    std::atomic<float> inputLevelL{0.0f};
    std::atomic<float> inputLevelR{0.0f};
    std::atomic<float> outputLevelL{0.0f};
    std::atomic<float> outputLevelR{0.0f};

    // Parameter management
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Cached parameter values
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* predelayParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* modulationParam = nullptr;
    std::atomic<float>* bassFreqParam = nullptr;
    std::atomic<float>* bassMulParam = nullptr;
    std::atomic<float>* highFreqParam = nullptr;
    std::atomic<float>* highMulParam = nullptr;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* diffusionParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;

    std::atomic<float>* reverbModeParam = nullptr;
    std::atomic<float>* colorModeParam = nullptr;
    std::atomic<float>* routingModeParam = nullptr;
    std::atomic<float>* engineMixParam = nullptr;
    std::atomic<float>* crossFeedParam = nullptr;
    std::atomic<float>* seriesBlendParam = nullptr;
    std::atomic<float>* vintageIntensityParam = nullptr;

    std::atomic<float>* hpfFreqParam = nullptr;
    std::atomic<float>* lpfFreqParam = nullptr;
    std::atomic<float>* tiltGainParam = nullptr;

    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;

    // Processing state
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    ReverbMode currentMode = ConcertHall;
    ColorMode currentColor = Color1980s;
    RoutingMode currentRouting = Parallel;

    // Preset management
    PresetManager presetManager;

    // Helper functions
    void updateReverbParameters();
    void updateFilterParameters();
    void processVintageArtifacts(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VintageVerbAudioProcessor)
};
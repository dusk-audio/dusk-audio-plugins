#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "MultiSynthVoice.h"
#include "Arpeggiator.h"
#include "EffectsEngine.h"
#include "../shared/AnalogEmulation/AnalogEmulation.h"

class MultiSynthProcessor : public juce::AudioProcessor
{
public:
    MultiSynthProcessor();
    ~MultiSynthProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Metering
    std::atomic<float> outputLevelL { -60.0f };
    std::atomic<float> outputLevelR { -60.0f };

    // Current arp step for UI
    std::atomic<int> arpCurrentStep { 0 };
    std::atomic<int> arpTotalSteps { 0 };

    // Current mode for UI
    MultiSynthDSP::SynthMode getCurrentMode() const;

    // Oscilloscope ring buffer (written in processBlock, read by editor)
    static constexpr int kScopeSize = 512;
    float scopeBuffer[kScopeSize] = {};
    std::atomic<int> scopeWritePos { 0 };

    // Factory preset names
    static const juce::StringArray& getFactoryPresetNames();

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateVoiceParameters();
    void applyFactoryPreset(int index);

    juce::AudioProcessorValueTreeState apvts;

    // DSP
    MultiSynthDSP::VoiceAllocator voiceAllocator;
    MultiSynthDSP::VoiceParameters voiceParams;
    MultiSynthDSP::ModMatrix modMatrix;
    MultiSynthDSP::UnisonEngine unisonEngine;
    MultiSynthDSP::Arpeggiator arpeggiator;
    MultiSynthDSP::EffectsChain effects;

    // Juno-style chorus for Cosmos mode (I / II / Both)
    MultiSynthDSP::JunoChorusEffect junoChorus;

    // Oversampling
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    int currentOversamplingFactor = 1;

    // Performance state
    float modWheelValue = 0.0f;
    float aftertouchValue = 0.0f;
    float pitchBendValue = 0.0f;
    int pitchBendRange = 2; // semitones

    double currentBPM = 120.0;
    bool transportPlaying = false;
    int currentProgram = 0;

    // Vintage filter state
    float prevVintageL = 0.0f;
    float prevVintageR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiSynthProcessor)
};

// Parameter ID constants
namespace ParamIDs
{
    // Mode
    static const juce::String MODE = "mode";

    // Oscillator 1
    static const juce::String OSC1_WAVE = "osc1Wave";
    static const juce::String OSC1_DETUNE = "osc1Detune";
    static const juce::String OSC1_PW = "osc1PW";
    static const juce::String OSC1_LEVEL = "osc1Level";

    // Oscillator 2
    static const juce::String OSC2_WAVE = "osc2Wave";
    static const juce::String OSC2_DETUNE = "osc2Detune";
    static const juce::String OSC2_PW = "osc2PW";
    static const juce::String OSC2_LEVEL = "osc2Level";
    static const juce::String OSC2_SEMI = "osc2Semi";

    // Oscillator 3 (Modular)
    static const juce::String OSC3_WAVE = "osc3Wave";
    static const juce::String OSC3_LEVEL = "osc3Level";

    // Sub oscillator (Mono)
    static const juce::String SUB_LEVEL = "subLevel";
    static const juce::String SUB_WAVE = "subWave";

    // Noise
    static const juce::String NOISE_LEVEL = "noiseLevel";

    // Filter
    static const juce::String FILTER_CUTOFF = "filterCutoff";
    static const juce::String FILTER_RESONANCE = "filterRes";
    static const juce::String FILTER_HP_CUTOFF = "filterHP";
    static const juce::String FILTER_ENV_AMT = "filterEnvAmt";

    // Amp Envelope
    static const juce::String AMP_ATTACK = "ampA";
    static const juce::String AMP_DECAY = "ampD";
    static const juce::String AMP_SUSTAIN = "ampS";
    static const juce::String AMP_RELEASE = "ampR";
    static const juce::String AMP_CURVE = "ampCurve";

    // Filter Envelope
    static const juce::String FILT_ATTACK = "filtA";
    static const juce::String FILT_DECAY = "filtD";
    static const juce::String FILT_SUSTAIN = "filtS";
    static const juce::String FILT_RELEASE = "filtR";
    static const juce::String FILT_CURVE = "filtCurve";

    // Mode-specific
    static const juce::String CROSS_MOD = "crossMod";
    static const juce::String RING_MOD = "ringMod";
    static const juce::String HARD_SYNC = "hardSync";
    static const juce::String FM_AMOUNT = "fmAmount";

    // LFO 1
    static const juce::String LFO1_RATE = "lfo1Rate";
    static const juce::String LFO1_SHAPE = "lfo1Shape";
    static const juce::String LFO1_FADE = "lfo1Fade";
    static const juce::String LFO1_SYNC = "lfo1Sync";

    // LFO 2
    static const juce::String LFO2_RATE = "lfo2Rate";
    static const juce::String LFO2_SHAPE = "lfo2Shape";
    static const juce::String LFO2_FADE = "lfo2Fade";
    static const juce::String LFO2_SYNC = "lfo2Sync";

    // Unison
    static const juce::String UNISON_VOICES = "unisonVoices";
    static const juce::String UNISON_DETUNE = "unisonDetune";
    static const juce::String UNISON_SPREAD = "unisonSpread";

    // Portamento
    static const juce::String PORTA_TIME = "portaTime";
    static const juce::String LEGATO = "legato";

    // Analog character
    static const juce::String ANALOG_AMT = "analogAmt";
    static const juce::String VINTAGE = "vintage";

    // Velocity
    static const juce::String VEL_SENS = "velSens";
    static const juce::String PITCH_BEND_RANGE = "pbRange";

    // Arpeggiator
    static const juce::String ARP_ON = "arpOn";
    static const juce::String ARP_MODE = "arpMode";
    static const juce::String ARP_OCTAVE = "arpOctave";
    static const juce::String ARP_RATE = "arpRate";
    static const juce::String ARP_GATE = "arpGate";
    static const juce::String ARP_SWING = "arpSwing";
    static const juce::String ARP_LATCH = "arpLatch";
    static const juce::String ARP_VEL_MODE = "arpVelMode";
    static const juce::String ARP_FIXED_VEL = "arpFixedVel";

    // Effects - Drive
    static const juce::String DRIVE_ON = "driveOn";
    static const juce::String DRIVE_TYPE = "driveType";
    static const juce::String DRIVE_AMT = "driveAmt";
    static const juce::String DRIVE_MIX = "driveMix";

    // Effects - Chorus
    static const juce::String CHORUS_ON = "chorusOn";
    static const juce::String CHORUS_RATE = "chorusRate";
    static const juce::String CHORUS_DEPTH = "chorusDepth";
    static const juce::String CHORUS_MIX = "chorusMix";

    // Effects - Delay
    static const juce::String DELAY_ON = "delayOn";
    static const juce::String DELAY_SYNC = "delaySync";
    static const juce::String DELAY_TIME = "delayTime";
    static const juce::String DELAY_RATE_DIV = "delayDiv";
    static const juce::String DELAY_FEEDBACK = "delayFB";
    static const juce::String DELAY_MIX = "delayMix";
    static const juce::String DELAY_PINGPONG = "delayPP";
    static const juce::String DELAY_TAPE = "delayTape";

    // Effects - Reverb
    static const juce::String REVERB_ON = "reverbOn";
    static const juce::String REVERB_SIZE = "reverbSize";
    static const juce::String REVERB_DECAY = "reverbDecay";
    static const juce::String REVERB_DAMP = "reverbDamp";
    static const juce::String REVERB_MIX = "reverbMix";
    static const juce::String REVERB_PREDELAY = "reverbPD";

    // Mod Matrix (8 slots x 3 params each)
    static inline juce::String modSlotSource(int i) { return "modSrc" + juce::String(i); }
    static inline juce::String modSlotDest(int i) { return "modDst" + juce::String(i); }
    static inline juce::String modSlotAmount(int i) { return "modAmt" + juce::String(i); }

    // Master
    // Cosmos-specific
    static const juce::String COSMOS_CHORUS_MODE = "cosmosChorus";

    // Master
    static const juce::String MASTER_VOL = "masterVol";
    static const juce::String MASTER_PAN = "masterPan";
    static const juce::String OVERSAMPLING = "oversampling";
}

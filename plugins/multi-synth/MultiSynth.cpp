#include "MultiSynth.h"
#include "MultiSynthEditor.h"

//==============================================================================
static const juce::StringArray kFactoryPresetNames = {
    // Cosmos (0-4)
    "Neon Nights", "Glass Highway", "Velvet Fog", "Sunset Strip", "Crystal Rain",
    // Oracle (5-9)
    "Brass Section", "Wooden Keys", "Poly Mod Bells", "Dark Prophet", "Stab Machine",
    // Mono (10-14)
    "Pulsing Darkness", "Acid Squelch", "Screaming Lead", "Sub Thunder", "Sync Sweep",
    // Modular (15-19)
    "Upside Down", "Sci-Fi Computer", "Horror Drone", "Voltage Ghost", "Retro Sequence",
    // Init (20-23)
    "Init Cosmos", "Init Oracle", "Init Mono", "Init Modular"
};

//==============================================================================
MultiSynthProcessor::MultiSynthProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    AnalogEmulation::initializeLibrary();
}

MultiSynthProcessor::~MultiSynthProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MultiSynthProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Mode selector
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::MODE, 1), "Synth Mode",
        juce::StringArray("Cosmos", "Oracle", "Mono", "Modular"), 0));

    // Oscillator 1
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::OSC1_WAVE, 1), "Osc 1 Wave",
        juce::StringArray("Saw", "Square", "Triangle", "Sine", "Pulse"), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::OSC1_DETUNE, 1), "Osc 1 Detune",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::OSC1_PW, 1), "Osc 1 PW",
        juce::NormalisableRange<float>(0.05f, 0.95f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::OSC1_LEVEL, 1), "Osc 1 Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    // Oscillator 2
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::OSC2_WAVE, 1), "Osc 2 Wave",
        juce::StringArray("Saw", "Square", "Triangle", "Sine", "Pulse"), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::OSC2_DETUNE, 1), "Osc 2 Detune",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 7.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::OSC2_PW, 1), "Osc 2 PW",
        juce::NormalisableRange<float>(0.05f, 0.95f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::OSC2_LEVEL, 1), "Osc 2 Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamIDs::OSC2_SEMI, 1), "Osc 2 Semi",
        -24, 24, 0));

    // Oscillator 3 (Modular)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::OSC3_WAVE, 1), "Osc 3 Wave",
        juce::StringArray("Saw", "Square", "Triangle", "Sine"), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::OSC3_LEVEL, 1), "Osc 3 Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    // Sub oscillator (Mono)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::SUB_LEVEL, 1), "Sub Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::SUB_WAVE, 1), "Sub Wave",
        juce::StringArray("Square", "Sine"), 0));

    // Noise
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::NOISE_LEVEL, 1), "Noise Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    // Filter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FILTER_CUTOFF, 1), "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.3f), 8000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FILTER_RESONANCE, 1), "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FILTER_HP_CUTOFF, 1), "Filter HP",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 0.1f, 0.3f), 20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FILTER_ENV_AMT, 1), "Filter Env Amt",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.5f));

    // Amp Envelope
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::AMP_ATTACK, 1), "Amp Attack",
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.3f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::AMP_DECAY, 1), "Amp Decay",
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.3f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::AMP_SUSTAIN, 1), "Amp Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::AMP_RELEASE, 1), "Amp Release",
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.3f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::AMP_CURVE, 1), "Amp Curve",
        juce::StringArray("Linear", "Exponential", "Logarithmic", "Analog RC"), 3));

    // Filter Envelope
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FILT_ATTACK, 1), "Filter Attack",
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.3f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FILT_DECAY, 1), "Filter Decay",
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.3f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FILT_SUSTAIN, 1), "Filter Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FILT_RELEASE, 1), "Filter Release",
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.3f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::FILT_CURVE, 1), "Filter Curve",
        juce::StringArray("Linear", "Exponential", "Logarithmic", "Analog RC"), 3));

    // Mode-specific
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::CROSS_MOD, 1), "Cross Mod",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::RING_MOD, 1), "Ring Mod",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::HARD_SYNC, 1), "Hard Sync", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::FM_AMOUNT, 1), "FM Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    // LFO 1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::LFO1_RATE, 1), "LFO 1 Rate",
        juce::NormalisableRange<float>(0.01f, 50.0f, 0.01f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::LFO1_SHAPE, 1), "LFO 1 Shape",
        juce::StringArray("Sine", "Triangle", "Square", "S&H", "Random"), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::LFO1_FADE, 1), "LFO 1 Fade In",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::LFO1_SYNC, 1), "LFO 1 Sync", false));

    // LFO 2
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::LFO2_RATE, 1), "LFO 2 Rate",
        juce::NormalisableRange<float>(0.01f, 50.0f, 0.01f, 0.3f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::LFO2_SHAPE, 1), "LFO 2 Shape",
        juce::StringArray("Sine", "Triangle", "Square", "S&H", "Random"), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::LFO2_FADE, 1), "LFO 2 Fade In",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::LFO2_SYNC, 1), "LFO 2 Sync", false));

    // Unison
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamIDs::UNISON_VOICES, 1), "Unison Voices", 1, 8, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::UNISON_DETUNE, 1), "Unison Detune",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.1f), 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::UNISON_SPREAD, 1), "Unison Spread",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    // Portamento
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::PORTA_TIME, 1), "Portamento",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::LEGATO, 1), "Legato", false));

    // Analog & vintage
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::ANALOG_AMT, 1), "Analog",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::VINTAGE, 1), "Vintage",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    // Velocity / Pitch bend
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::VEL_SENS, 1), "Velocity Sens",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamIDs::PITCH_BEND_RANGE, 1), "PB Range", 1, 24, 2));

    // Arpeggiator
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::ARP_ON, 1), "Arp On", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::ARP_MODE, 1), "Arp Mode",
        juce::StringArray("Up", "Down", "Up/Down", "Down/Up", "Random", "Order", "Chord"), 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamIDs::ARP_OCTAVE, 1), "Arp Octave", 1, 4, 1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::ARP_RATE, 1), "Arp Rate",
        juce::StringArray("1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                          "1/2D", "1/4D", "1/8D", "1/16D",
                          "1/2T", "1/4T", "1/8T", "1/16T"), 3));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::ARP_GATE, 1), "Arp Gate",
        juce::NormalisableRange<float>(0.01f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::ARP_SWING, 1), "Arp Swing",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::ARP_LATCH, 1), "Arp Latch", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::ARP_VEL_MODE, 1), "Arp Vel Mode",
        juce::StringArray("As Played", "Fixed", "Accent"), 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamIDs::ARP_FIXED_VEL, 1), "Arp Fixed Vel", 1, 127, 100));

    // Cosmos-specific: Juno chorus mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::COSMOS_CHORUS_MODE, 1), "Cosmos Chorus",
        juce::StringArray("Off", "I", "II", "I+II"), 3)); // Default: Both

    // Effects - Drive
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::DRIVE_ON, 1), "Drive On", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::DRIVE_TYPE, 1), "Drive Type",
        juce::StringArray("Soft", "Hard", "Tube"), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::DRIVE_AMT, 1), "Drive Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::DRIVE_MIX, 1), "Drive Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    // Effects - Chorus
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::CHORUS_ON, 1), "Chorus On", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::CHORUS_RATE, 1), "Chorus Rate",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::CHORUS_DEPTH, 1), "Chorus Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::CHORUS_MIX, 1), "Chorus Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    // Effects - Delay
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::DELAY_ON, 1), "Delay On", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::DELAY_SYNC, 1), "Delay Sync", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::DELAY_TIME, 1), "Delay Time",
        juce::NormalisableRange<float>(1.0f, 2000.0f, 0.1f, 0.3f), 500.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::DELAY_RATE_DIV, 1), "Delay Division",
        juce::StringArray("1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                          "1/2D", "1/4D", "1/8D", "1/16D",
                          "1/2T", "1/4T", "1/8T", "1/16T"), 3));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::DELAY_FEEDBACK, 1), "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::DELAY_MIX, 1), "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::DELAY_PINGPONG, 1), "Delay Ping-Pong", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::DELAY_TAPE, 1), "Delay Tape", false));

    // Effects - Reverb
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::REVERB_ON, 1), "Reverb On", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::REVERB_SIZE, 1), "Reverb Size",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::REVERB_DECAY, 1), "Reverb Decay",
        juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f, 0.3f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::REVERB_DAMP, 1), "Reverb Damping",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::REVERB_MIX, 1), "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::REVERB_PREDELAY, 1), "Reverb Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 20.0f));

    // Mod Matrix (8 slots)
    for (int i = 0; i < MultiSynthDSP::kNumModSlots; ++i)
    {
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(ParamIDs::modSlotSource(i), 1),
            "Mod " + juce::String(i + 1) + " Source",
            juce::StringArray("None", "LFO 1", "LFO 2", "Env 2", "Mod Wheel",
                              "Aftertouch", "Velocity", "Key Track", "Random", "Pitch Bend"), 0));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(ParamIDs::modSlotDest(i), 1),
            "Mod " + juce::String(i + 1) + " Dest",
            juce::StringArray("None", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 1 PWM", "Osc 2 PWM",
                              "Filter Cutoff", "Filter Res", "Amplitude", "Pan",
                              "LFO 1 Rate", "LFO 2 Rate", "FX Mix", "Unison Det"), 0));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::modSlotAmount(i), 1),
            "Mod " + juce::String(i + 1) + " Amount",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    }

    // Master
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::MASTER_VOL, 1), "Master Volume",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::MASTER_PAN, 1), "Master Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::OVERSAMPLING, 1), "Oversampling",
        juce::StringArray("1x", "2x", "4x"), 0));

    return { params.begin(), params.end() };
}

//==============================================================================
void MultiSynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    voiceAllocator.prepare(sampleRate);
    arpeggiator.prepare(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    junoChorus.prepare(sampleRate, samplesPerBlock);

    // Setup oversampling (2x default)
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
    oversampling->initProcessing(static_cast<size_t>(samplesPerBlock));
}

void MultiSynthProcessor::releaseResources()
{
    oversampling.reset();
}

//==============================================================================
void MultiSynthProcessor::updateVoiceParameters()
{
    // Mode
    int modeIdx = static_cast<int>(*apvts.getRawParameterValue(ParamIDs::MODE));
    voiceParams.mode = static_cast<MultiSynthDSP::SynthMode>(modeIdx);

    // Set voice count per mode
    switch (voiceParams.mode)
    {
        case MultiSynthDSP::SynthMode::Cosmos:  voiceAllocator.setMaxVoices(6); break; // Juno-60 = 6 voices
        case MultiSynthDSP::SynthMode::Oracle:  voiceAllocator.setMaxVoices(5); break; // Prophet-5 = 5 voices
        case MultiSynthDSP::SynthMode::Mono:    voiceAllocator.setMaxVoices(1); break;
        case MultiSynthDSP::SynthMode::Modular: voiceAllocator.setMaxVoices(2); break; // ARP 2600 = duophonic
    }

    // Oscillators
    voiceParams.osc1Wave = static_cast<MultiSynthDSP::Waveform>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::OSC1_WAVE)));
    voiceParams.osc1Detune = *apvts.getRawParameterValue(ParamIDs::OSC1_DETUNE);
    voiceParams.osc1PulseWidth = *apvts.getRawParameterValue(ParamIDs::OSC1_PW);
    voiceParams.osc1Level = *apvts.getRawParameterValue(ParamIDs::OSC1_LEVEL);

    voiceParams.osc2Wave = static_cast<MultiSynthDSP::Waveform>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::OSC2_WAVE)));
    voiceParams.osc2Detune = *apvts.getRawParameterValue(ParamIDs::OSC2_DETUNE);
    voiceParams.osc2PulseWidth = *apvts.getRawParameterValue(ParamIDs::OSC2_PW);
    voiceParams.osc2Level = *apvts.getRawParameterValue(ParamIDs::OSC2_LEVEL);
    voiceParams.osc2SemiOffset = static_cast<int>(*apvts.getRawParameterValue(ParamIDs::OSC2_SEMI));

    voiceParams.osc3Wave = static_cast<MultiSynthDSP::Waveform>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::OSC3_WAVE)));
    voiceParams.osc3Level = *apvts.getRawParameterValue(ParamIDs::OSC3_LEVEL);

    voiceParams.subLevel = *apvts.getRawParameterValue(ParamIDs::SUB_LEVEL);
    int subWaveIdx = static_cast<int>(*apvts.getRawParameterValue(ParamIDs::SUB_WAVE));
    voiceParams.subWave = subWaveIdx == 0 ? MultiSynthDSP::Waveform::Square : MultiSynthDSP::Waveform::Sine;

    voiceParams.noiseLevel = *apvts.getRawParameterValue(ParamIDs::NOISE_LEVEL);

    // Filter
    voiceParams.filterCutoff = *apvts.getRawParameterValue(ParamIDs::FILTER_CUTOFF);
    voiceParams.filterResonance = *apvts.getRawParameterValue(ParamIDs::FILTER_RESONANCE);
    voiceParams.filterHPCutoff = *apvts.getRawParameterValue(ParamIDs::FILTER_HP_CUTOFF);
    voiceParams.filterEnvAmount = *apvts.getRawParameterValue(ParamIDs::FILTER_ENV_AMT);

    // Envelopes
    voiceParams.ampAttack = *apvts.getRawParameterValue(ParamIDs::AMP_ATTACK);
    voiceParams.ampDecay = *apvts.getRawParameterValue(ParamIDs::AMP_DECAY);
    voiceParams.ampSustain = *apvts.getRawParameterValue(ParamIDs::AMP_SUSTAIN);
    voiceParams.ampRelease = *apvts.getRawParameterValue(ParamIDs::AMP_RELEASE);
    voiceParams.ampCurve = static_cast<MultiSynthDSP::EnvelopeCurve>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::AMP_CURVE)));

    voiceParams.filtAttack = *apvts.getRawParameterValue(ParamIDs::FILT_ATTACK);
    voiceParams.filtDecay = *apvts.getRawParameterValue(ParamIDs::FILT_DECAY);
    voiceParams.filtSustain = *apvts.getRawParameterValue(ParamIDs::FILT_SUSTAIN);
    voiceParams.filtRelease = *apvts.getRawParameterValue(ParamIDs::FILT_RELEASE);
    voiceParams.filtCurve = static_cast<MultiSynthDSP::EnvelopeCurve>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::FILT_CURVE)));

    // Mode-specific
    voiceParams.crossMod = *apvts.getRawParameterValue(ParamIDs::CROSS_MOD);
    voiceParams.ringMod = *apvts.getRawParameterValue(ParamIDs::RING_MOD);
    voiceParams.hardSync = *apvts.getRawParameterValue(ParamIDs::HARD_SYNC) > 0.5f;
    voiceParams.fmAmount = *apvts.getRawParameterValue(ParamIDs::FM_AMOUNT);

    // Portamento
    voiceParams.portamentoTime = *apvts.getRawParameterValue(ParamIDs::PORTA_TIME);
    voiceParams.legatoMode = *apvts.getRawParameterValue(ParamIDs::LEGATO) > 0.5f;

    // Analog
    voiceParams.analogAmount = *apvts.getRawParameterValue(ParamIDs::ANALOG_AMT);
    voiceParams.velocitySensitivity = *apvts.getRawParameterValue(ParamIDs::VEL_SENS);

    // Unison
    unisonEngine.setVoiceCount(static_cast<int>(*apvts.getRawParameterValue(ParamIDs::UNISON_VOICES)));
    unisonEngine.setDetune(*apvts.getRawParameterValue(ParamIDs::UNISON_DETUNE));
    unisonEngine.setStereoSpread(*apvts.getRawParameterValue(ParamIDs::UNISON_SPREAD));

    // Arpeggiator
    arpeggiator.setEnabled(*apvts.getRawParameterValue(ParamIDs::ARP_ON) > 0.5f);
    arpeggiator.setMode(static_cast<MultiSynthDSP::ArpMode>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::ARP_MODE))));
    arpeggiator.setOctaveRange(static_cast<int>(*apvts.getRawParameterValue(ParamIDs::ARP_OCTAVE)));
    arpeggiator.setRate(static_cast<MultiSynthDSP::ArpRateDivision>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::ARP_RATE))));
    arpeggiator.setGate(*apvts.getRawParameterValue(ParamIDs::ARP_GATE));
    arpeggiator.setSwing(*apvts.getRawParameterValue(ParamIDs::ARP_SWING));
    arpeggiator.setLatch(*apvts.getRawParameterValue(ParamIDs::ARP_LATCH) > 0.5f);
    arpeggiator.setVelocityMode(static_cast<MultiSynthDSP::ArpVelocityMode>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::ARP_VEL_MODE))));
    arpeggiator.setFixedVelocity(static_cast<int>(*apvts.getRawParameterValue(ParamIDs::ARP_FIXED_VEL)));

    // Effects
    effects.drive.setEnabled(*apvts.getRawParameterValue(ParamIDs::DRIVE_ON) > 0.5f);
    effects.drive.setType(static_cast<MultiSynthDSP::DriveType>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::DRIVE_TYPE))));
    effects.drive.setDrive(*apvts.getRawParameterValue(ParamIDs::DRIVE_AMT));
    effects.drive.setMix(*apvts.getRawParameterValue(ParamIDs::DRIVE_MIX));

    effects.chorus.setEnabled(*apvts.getRawParameterValue(ParamIDs::CHORUS_ON) > 0.5f);
    effects.chorus.setRate(*apvts.getRawParameterValue(ParamIDs::CHORUS_RATE));
    effects.chorus.setDepth(*apvts.getRawParameterValue(ParamIDs::CHORUS_DEPTH));
    effects.chorus.setMix(*apvts.getRawParameterValue(ParamIDs::CHORUS_MIX));

    effects.delay.setEnabled(*apvts.getRawParameterValue(ParamIDs::DELAY_ON) > 0.5f);
    effects.delay.setTempoSync(*apvts.getRawParameterValue(ParamIDs::DELAY_SYNC) > 0.5f);
    effects.delay.setTimeMs(*apvts.getRawParameterValue(ParamIDs::DELAY_TIME));
    effects.delay.setSyncDivision(static_cast<MultiSynthDSP::ArpRateDivision>(
        static_cast<int>(*apvts.getRawParameterValue(ParamIDs::DELAY_RATE_DIV))));
    effects.delay.setFeedback(*apvts.getRawParameterValue(ParamIDs::DELAY_FEEDBACK));
    effects.delay.setMix(*apvts.getRawParameterValue(ParamIDs::DELAY_MIX));
    effects.delay.setPingPong(*apvts.getRawParameterValue(ParamIDs::DELAY_PINGPONG) > 0.5f);
    effects.delay.setTapeCharacter(*apvts.getRawParameterValue(ParamIDs::DELAY_TAPE) > 0.5f);

    effects.reverb.setEnabled(*apvts.getRawParameterValue(ParamIDs::REVERB_ON) > 0.5f);
    effects.reverb.setSize(*apvts.getRawParameterValue(ParamIDs::REVERB_SIZE));
    effects.reverb.setDecay(*apvts.getRawParameterValue(ParamIDs::REVERB_DECAY));
    effects.reverb.setDamping(*apvts.getRawParameterValue(ParamIDs::REVERB_DAMP));
    effects.reverb.setMix(*apvts.getRawParameterValue(ParamIDs::REVERB_MIX));
    effects.reverb.setPreDelay(*apvts.getRawParameterValue(ParamIDs::REVERB_PREDELAY));

    // Juno chorus for Cosmos mode (I / II / Both)
    bool isCosmos = voiceParams.mode == MultiSynthDSP::SynthMode::Cosmos;
    if (isCosmos)
    {
        int chorusIdx = static_cast<int>(*apvts.getRawParameterValue(ParamIDs::COSMOS_CHORUS_MODE));
        junoChorus.setMode(static_cast<MultiSynthDSP::JunoChorusMode>(chorusIdx));
    }
    else
    {
        junoChorus.setMode(MultiSynthDSP::JunoChorusMode::Off);
    }

    // Modular spring reverb — auto-enable in Modular mode with low mix
    bool isModular = voiceParams.mode == MultiSynthDSP::SynthMode::Modular;
    effects.springReverb.setEnabled(isModular);
    if (isModular)
        effects.springReverb.setMix(0.15f);

    // Mod matrix
    for (int i = 0; i < MultiSynthDSP::kNumModSlots; ++i)
    {
        auto& slot = modMatrix.getSlot(i);
        slot.source = static_cast<MultiSynthDSP::ModSource>(
            static_cast<int>(*apvts.getRawParameterValue(ParamIDs::modSlotSource(i))));
        slot.destination = static_cast<MultiSynthDSP::ModDest>(
            static_cast<int>(*apvts.getRawParameterValue(ParamIDs::modSlotDest(i))));
        slot.amount = *apvts.getRawParameterValue(ParamIDs::modSlotAmount(i));
    }

    pitchBendRange = static_cast<int>(*apvts.getRawParameterValue(ParamIDs::PITCH_BEND_RANGE));
}

//==============================================================================
void MultiSynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Update parameters from APVTS
    updateVoiceParameters();

    // Get transport info
    if (auto* playHead = getPlayHead())
    {
        auto pos = playHead->getPosition();
        if (pos.hasValue())
        {
            if (auto bpm = pos->getBpm())
                currentBPM = *bpm;
            transportPlaying = pos->getIsPlaying();
        }
    }

    // Master gain
    float masterGainDb = *apvts.getRawParameterValue(ParamIDs::MASTER_VOL);
    float masterGain = juce::Decibels::decibelsToGain(masterGainDb);
    float masterPan = *apvts.getRawParameterValue(ParamIDs::MASTER_PAN);

    // Process MIDI
    juce::MidiBuffer processedMidi;
    bool arpActive = arpeggiator.isEnabled();

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            if (arpActive)
                arpeggiator.noteOn(msg.getNoteNumber(), msg.getVelocity());
            else
                voiceAllocator.noteOn(msg.getNoteNumber(), msg.getFloatVelocity(), voiceParams);
        }
        else if (msg.isNoteOff())
        {
            if (arpActive)
                arpeggiator.noteOff(msg.getNoteNumber());
            else
                voiceAllocator.noteOff(msg.getNoteNumber());
        }
        else if (msg.isControllerOfType(1)) // Mod wheel
        {
            modWheelValue = msg.getControllerValue() / 127.0f;
        }
        else if (msg.isChannelPressure())
        {
            aftertouchValue = msg.getChannelPressureValue() / 127.0f;
        }
        else if (msg.isPitchWheel())
        {
            pitchBendValue = (msg.getPitchWheelValue() - 8192) / 8192.0f
                             * static_cast<float>(pitchBendRange);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            voiceAllocator.allNotesOff();
            arpeggiator.reset();
        }
    }

    // Process arpeggiator
    if (arpActive)
    {
        auto arpEvents = arpeggiator.processBlock(buffer.getNumSamples(), currentBPM, transportPlaying);
        for (auto& evt : arpEvents)
        {
            if (evt.isNoteOn)
                voiceAllocator.noteOn(evt.noteNumber, evt.velocity / 127.0f, voiceParams);
            else
                voiceAllocator.noteOff(evt.noteNumber);
        }
        arpCurrentStep.store(arpeggiator.getCurrentStep(), std::memory_order_relaxed);
        arpTotalSteps.store(arpeggiator.getTotalSteps(), std::memory_order_relaxed);
    }

    // Render audio
    auto* outputL = buffer.getWritePointer(0);
    auto* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    float peakL = 0.0f, peakR = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sampleL = 0.0f, sampleR = 0.0f;

        voiceAllocator.renderSample(voiceParams, modMatrix, unisonEngine,
                                    modWheelValue, aftertouchValue, pitchBendValue,
                                    sampleL, sampleR);

        // Voice output attenuation — keep voices at healthy level so
        // effects chain + limiter have headroom
        static constexpr float kVoiceGain = 0.18f;
        sampleL *= kVoiceGain;
        sampleR *= kVoiceGain;

        // Juno chorus (Cosmos mode only, I/II/Both)
        junoChorus.process(sampleL, sampleR);

        // Effects chain
        effects.process(sampleL, sampleR, currentBPM);

        // Vintage character: subtle HF rolloff and noise
        float vintage = *apvts.getRawParameterValue(ParamIDs::VINTAGE);
        if (vintage > 0.01f)
        {
            // Simple one-pole LPF for HF rolloff
            float coeff = 1.0f - vintage * 0.3f;
            sampleL = sampleL * coeff + prevVintageL * (1.0f - coeff);
            sampleR = sampleR * coeff + prevVintageR * (1.0f - coeff);
            prevVintageL = sampleL;
            prevVintageR = sampleR;

            // Subtle noise floor
            float noise = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f)
                          * vintage * 0.001f;
            sampleL += noise;
            sampleR += noise;
        }

        // Master gain & pan
        float panAngle = (masterPan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        sampleL *= masterGain * std::cos(panAngle);
        sampleR *= masterGain * std::sin(panAngle);

        // Safety limiter: soft-knee that's transparent below 0.7 (-3dB),
        // gently compresses 0.7-1.0, hard limits at 1.0
        auto softLimit = [](float x) -> float {
            if (std::isnan(x) || std::isinf(x)) return 0.0f;
            float ax = std::abs(x);
            if (ax <= 0.7f) return x;
            // Soft knee: map 0.7..inf -> 0.7..1.0 using tanh
            float over = (ax - 0.7f) * 3.33f; // normalize overshoot
            float limited = 0.7f + 0.3f * std::tanh(over);
            return (x >= 0.0f) ? limited : -limited;
        };
        sampleL = softLimit(sampleL);
        sampleR = softLimit(sampleR);

        // Output
        outputL[i] = sampleL;
        if (outputR) outputR[i] = sampleR;

        peakL = juce::jmax(peakL, std::abs(sampleL));

        // Write to oscilloscope ring buffer
        int wp = scopeWritePos.load(std::memory_order_relaxed);
        scopeBuffer[wp] = (sampleL + (outputR ? sampleR : sampleL)) * 0.5f;
        scopeWritePos.store((wp + 1) % kScopeSize, std::memory_order_relaxed);
        peakR = juce::jmax(peakR, std::abs(sampleR));
    }

    // Update metering
    float dbL = peakL > 0.0f ? juce::Decibels::gainToDecibels(peakL) : -60.0f;
    float dbR = peakR > 0.0f ? juce::Decibels::gainToDecibels(peakR) : -60.0f;
    outputLevelL.store(dbL, std::memory_order_relaxed);
    outputLevelR.store(dbR, std::memory_order_relaxed);
}

//==============================================================================
MultiSynthDSP::SynthMode MultiSynthProcessor::getCurrentMode() const
{
    int modeIdx = static_cast<int>(*apvts.getRawParameterValue(ParamIDs::MODE));
    return static_cast<MultiSynthDSP::SynthMode>(modeIdx);
}

//==============================================================================
int MultiSynthProcessor::getNumPrograms() { return kFactoryPresetNames.size(); }
int MultiSynthProcessor::getCurrentProgram() { return currentProgram; }
void MultiSynthProcessor::setCurrentProgram(int index)
{
    if (index >= 0 && index < kFactoryPresetNames.size())
    {
        currentProgram = index;
        applyFactoryPreset(index);
    }
}
const juce::String MultiSynthProcessor::getProgramName(int index)
{
    if (index >= 0 && index < kFactoryPresetNames.size())
        return kFactoryPresetNames[index];
    return {};
}

const juce::StringArray& MultiSynthProcessor::getFactoryPresetNames()
{
    return kFactoryPresetNames;
}

//==============================================================================
void MultiSynthProcessor::applyFactoryPreset(int index)
{
    // Helper to set parameters
    auto setParam = [this](const juce::String& id, float value) {
        if (auto* p = apvts.getRawParameterValue(id))
        {
            if (auto* param = apvts.getParameter(id))
                param->setValueNotifyingHost(param->convertTo0to1(value));
        }
    };

    // Reset to defaults first
    setParam(ParamIDs::OSC1_WAVE, 0); // Saw
    setParam(ParamIDs::OSC2_WAVE, 0);
    setParam(ParamIDs::OSC1_DETUNE, 0);
    setParam(ParamIDs::OSC2_DETUNE, 7);
    setParam(ParamIDs::OSC1_LEVEL, 1.0f);
    setParam(ParamIDs::OSC2_LEVEL, 0.8f);
    setParam(ParamIDs::NOISE_LEVEL, 0);
    setParam(ParamIDs::FILTER_CUTOFF, 8000);
    setParam(ParamIDs::FILTER_RESONANCE, 0.3f);
    setParam(ParamIDs::FILTER_ENV_AMT, 0.5f);
    setParam(ParamIDs::AMP_ATTACK, 0.01f);
    setParam(ParamIDs::AMP_DECAY, 0.2f);
    setParam(ParamIDs::AMP_SUSTAIN, 0.8f);
    setParam(ParamIDs::AMP_RELEASE, 0.3f);
    setParam(ParamIDs::CROSS_MOD, 0);
    setParam(ParamIDs::RING_MOD, 0);
    setParam(ParamIDs::ARP_ON, 0);
    setParam(ParamIDs::DRIVE_ON, 0);
    setParam(ParamIDs::CHORUS_ON, 0);
    setParam(ParamIDs::DELAY_ON, 0);
    setParam(ParamIDs::REVERB_ON, 0);
    setParam(ParamIDs::UNISON_VOICES, 1);
    setParam(ParamIDs::PORTA_TIME, 0);
    setParam(ParamIDs::ANALOG_AMT, 0.2f);
    setParam(ParamIDs::VINTAGE, 0);

    switch (index)
    {
        case 0: // Neon Nights - lush poly pad with slow chorus
            setParam(ParamIDs::MODE, 0); // Cosmos
            setParam(ParamIDs::OSC1_WAVE, 0); // Saw
            setParam(ParamIDs::OSC2_WAVE, 4); // Pulse
            setParam(ParamIDs::OSC2_DETUNE, 12);
            setParam(ParamIDs::FILTER_CUTOFF, 3000);
            setParam(ParamIDs::FILTER_RESONANCE, 0.2f);
            setParam(ParamIDs::AMP_ATTACK, 0.3f);
            setParam(ParamIDs::AMP_DECAY, 0.5f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.9f);
            setParam(ParamIDs::AMP_RELEASE, 1.5f);
            setParam(ParamIDs::UNISON_VOICES, 4);
            setParam(ParamIDs::UNISON_DETUNE, 15);
            setParam(ParamIDs::ANALOG_AMT, 0.4f);
            setParam(ParamIDs::REVERB_ON, 1);
            setParam(ParamIDs::REVERB_SIZE, 0.7f);
            setParam(ParamIDs::REVERB_MIX, 0.3f);
            break;

        case 1: // Glass Highway
            setParam(ParamIDs::MODE, 0);
            setParam(ParamIDs::OSC1_WAVE, 0);
            setParam(ParamIDs::OSC2_WAVE, 0);
            setParam(ParamIDs::FILTER_CUTOFF, 6000);
            setParam(ParamIDs::FILTER_RESONANCE, 0.4f);
            setParam(ParamIDs::AMP_ATTACK, 0.01f);
            setParam(ParamIDs::AMP_DECAY, 0.3f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.6f);
            setParam(ParamIDs::AMP_RELEASE, 0.8f);
            setParam(ParamIDs::ARP_ON, 1);
            setParam(ParamIDs::ARP_RATE, 3); // 1/8
            setParam(ParamIDs::ARP_GATE, 0.6f);
            setParam(ParamIDs::DELAY_ON, 1);
            setParam(ParamIDs::DELAY_MIX, 0.25f);
            break;

        case 2: // Velvet Fog
            setParam(ParamIDs::MODE, 0);
            setParam(ParamIDs::FILTER_CUTOFF, 1500);
            setParam(ParamIDs::FILTER_RESONANCE, 0.3f);
            setParam(ParamIDs::FILTER_ENV_AMT, 0.2f);
            setParam(ParamIDs::AMP_ATTACK, 0.8f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.9f);
            setParam(ParamIDs::AMP_RELEASE, 2.0f);
            setParam(ParamIDs::UNISON_VOICES, 3);
            setParam(ParamIDs::VINTAGE, 0.4f);
            break;

        case 3: // Sunset Strip
            setParam(ParamIDs::MODE, 0);
            setParam(ParamIDs::OSC2_DETUNE, 20);
            setParam(ParamIDs::FILTER_CUTOFF, 4000);
            setParam(ParamIDs::AMP_ATTACK, 0.2f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.85f);
            setParam(ParamIDs::AMP_RELEASE, 1.2f);
            setParam(ParamIDs::UNISON_VOICES, 6);
            setParam(ParamIDs::UNISON_DETUNE, 25);
            setParam(ParamIDs::CHORUS_ON, 1);
            break;

        case 4: // Crystal Rain
            setParam(ParamIDs::MODE, 0);
            setParam(ParamIDs::FILTER_CUTOFF, 8000);
            setParam(ParamIDs::AMP_ATTACK, 0.005f);
            setParam(ParamIDs::AMP_DECAY, 0.15f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.3f);
            setParam(ParamIDs::AMP_RELEASE, 0.4f);
            setParam(ParamIDs::ARP_ON, 1);
            setParam(ParamIDs::ARP_RATE, 4); // 1/16
            setParam(ParamIDs::ARP_GATE, 0.3f);
            setParam(ParamIDs::DELAY_ON, 1);
            setParam(ParamIDs::DELAY_MIX, 0.3f);
            setParam(ParamIDs::REVERB_ON, 1);
            setParam(ParamIDs::REVERB_MIX, 0.25f);
            break;

        case 5: // Brass Section
            setParam(ParamIDs::MODE, 1); // Oracle
            setParam(ParamIDs::FILTER_CUTOFF, 2000);
            setParam(ParamIDs::FILTER_RESONANCE, 0.4f);
            setParam(ParamIDs::FILTER_ENV_AMT, 0.7f);
            setParam(ParamIDs::FILT_ATTACK, 0.05f);
            setParam(ParamIDs::FILT_DECAY, 0.3f);
            setParam(ParamIDs::FILT_SUSTAIN, 0.3f);
            setParam(ParamIDs::AMP_ATTACK, 0.01f);
            setParam(ParamIDs::AMP_DECAY, 0.3f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.7f);
            break;

        case 6: // Wooden Keys
            setParam(ParamIDs::MODE, 1);
            setParam(ParamIDs::FILTER_CUTOFF, 3000);
            setParam(ParamIDs::FILTER_RESONANCE, 0.2f);
            setParam(ParamIDs::AMP_ATTACK, 0.005f);
            setParam(ParamIDs::AMP_DECAY, 0.4f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.5f);
            setParam(ParamIDs::AMP_RELEASE, 0.3f);
            break;

        case 7: // Poly Mod Bells
            setParam(ParamIDs::MODE, 1);
            setParam(ParamIDs::CROSS_MOD, 0.6f);
            setParam(ParamIDs::FILTER_CUTOFF, 5000);
            setParam(ParamIDs::AMP_ATTACK, 0.005f);
            setParam(ParamIDs::AMP_DECAY, 1.0f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.0f);
            setParam(ParamIDs::AMP_RELEASE, 1.5f);
            setParam(ParamIDs::REVERB_ON, 1);
            setParam(ParamIDs::REVERB_MIX, 0.35f);
            setParam(ParamIDs::REVERB_DECAY, 3.0f);
            break;

        case 8: // Dark Prophet
            setParam(ParamIDs::MODE, 1);
            setParam(ParamIDs::FILTER_CUTOFF, 1200);
            setParam(ParamIDs::FILTER_RESONANCE, 0.5f);
            setParam(ParamIDs::FILTER_ENV_AMT, 0.3f);
            setParam(ParamIDs::AMP_ATTACK, 0.5f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.85f);
            setParam(ParamIDs::AMP_RELEASE, 1.5f);
            setParam(ParamIDs::VINTAGE, 0.3f);
            break;

        case 9: // Stab Machine
            setParam(ParamIDs::MODE, 1);
            setParam(ParamIDs::FILTER_CUTOFF, 4000);
            setParam(ParamIDs::FILTER_ENV_AMT, 0.6f);
            setParam(ParamIDs::AMP_ATTACK, 0.001f);
            setParam(ParamIDs::AMP_DECAY, 0.15f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.0f);
            setParam(ParamIDs::AMP_RELEASE, 0.1f);
            break;

        case 10: // Pulsing Darkness
            setParam(ParamIDs::MODE, 2); // Mono
            setParam(ParamIDs::OSC1_WAVE, 4); // Pulse
            setParam(ParamIDs::FILTER_CUTOFF, 800);
            setParam(ParamIDs::FILTER_RESONANCE, 0.5f);
            setParam(ParamIDs::FILTER_ENV_AMT, 0.6f);
            setParam(ParamIDs::SUB_LEVEL, 0.8f);
            setParam(ParamIDs::ARP_ON, 1);
            setParam(ParamIDs::ARP_RATE, 3); // 1/8
            setParam(ParamIDs::ARP_GATE, 0.7f);
            setParam(ParamIDs::VINTAGE, 0.2f);
            break;

        case 11: // Acid Squelch
            setParam(ParamIDs::MODE, 2);
            setParam(ParamIDs::FILTER_CUTOFF, 500);
            setParam(ParamIDs::FILTER_RESONANCE, 0.8f);
            setParam(ParamIDs::FILTER_ENV_AMT, 0.9f);
            setParam(ParamIDs::FILT_ATTACK, 0.001f);
            setParam(ParamIDs::FILT_DECAY, 0.2f);
            setParam(ParamIDs::FILT_SUSTAIN, 0.1f);
            setParam(ParamIDs::SUB_LEVEL, 0.6f);
            setParam(ParamIDs::PORTA_TIME, 0.05f);
            break;

        case 12: // Screaming Lead
            setParam(ParamIDs::MODE, 2);
            setParam(ParamIDs::FILTER_CUTOFF, 6000);
            setParam(ParamIDs::FILTER_RESONANCE, 0.3f);
            setParam(ParamIDs::AMP_ATTACK, 0.005f);
            setParam(ParamIDs::PORTA_TIME, 0.1f);
            setParam(ParamIDs::DRIVE_ON, 1);
            setParam(ParamIDs::DRIVE_AMT, 0.4f);
            setParam(ParamIDs::DELAY_ON, 1);
            setParam(ParamIDs::DELAY_MIX, 0.2f);
            break;

        case 13: // Sub Thunder
            setParam(ParamIDs::MODE, 2);
            setParam(ParamIDs::OSC1_WAVE, 3); // Sine
            setParam(ParamIDs::OSC2_WAVE, 3);
            setParam(ParamIDs::FILTER_CUTOFF, 400);
            setParam(ParamIDs::SUB_LEVEL, 1.0f);
            setParam(ParamIDs::NOISE_LEVEL, 0.02f);
            break;

        case 14: // Sync Sweep
            setParam(ParamIDs::MODE, 2);
            setParam(ParamIDs::OSC2_SEMI, 7);
            setParam(ParamIDs::FILTER_CUTOFF, 5000);
            setParam(ParamIDs::FILT_ATTACK, 0.01f);
            setParam(ParamIDs::FILT_DECAY, 0.5f);
            setParam(ParamIDs::FILTER_ENV_AMT, 0.8f);
            setParam(ParamIDs::DELAY_ON, 1);
            break;

        case 15: // Upside Down
            setParam(ParamIDs::MODE, 3); // Modular
            setParam(ParamIDs::FM_AMOUNT, 0.3f);
            setParam(ParamIDs::FILTER_CUTOFF, 2000);
            setParam(ParamIDs::FILTER_RESONANCE, 0.6f);
            setParam(ParamIDs::AMP_ATTACK, 1.0f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.8f);
            setParam(ParamIDs::AMP_RELEASE, 3.0f);
            setParam(ParamIDs::NOISE_LEVEL, 0.05f);
            setParam(ParamIDs::REVERB_ON, 1);
            setParam(ParamIDs::REVERB_DECAY, 5.0f);
            setParam(ParamIDs::REVERB_MIX, 0.4f);
            setParam(ParamIDs::VINTAGE, 0.5f);
            break;

        case 16: // Sci-Fi Computer
            setParam(ParamIDs::MODE, 3);
            setParam(ParamIDs::FM_AMOUNT, 0.5f);
            setParam(ParamIDs::FILTER_CUTOFF, 4000);
            setParam(ParamIDs::AMP_ATTACK, 0.001f);
            setParam(ParamIDs::AMP_DECAY, 0.1f);
            setParam(ParamIDs::AMP_SUSTAIN, 0.0f);
            setParam(ParamIDs::ARP_ON, 1);
            setParam(ParamIDs::ARP_RATE, 4); // 1/16
            setParam(ParamIDs::ARP_MODE, 4); // Random
            break;

        case 17: // Horror Drone
            setParam(ParamIDs::MODE, 3);
            setParam(ParamIDs::RING_MOD, 0.5f);
            setParam(ParamIDs::OSC2_SEMI, -5); // Dissonant
            setParam(ParamIDs::FILTER_CUTOFF, 1000);
            setParam(ParamIDs::FILTER_RESONANCE, 0.7f);
            setParam(ParamIDs::AMP_ATTACK, 2.0f);
            setParam(ParamIDs::AMP_SUSTAIN, 1.0f);
            setParam(ParamIDs::AMP_RELEASE, 4.0f);
            setParam(ParamIDs::REVERB_ON, 1);
            setParam(ParamIDs::REVERB_DECAY, 8.0f);
            setParam(ParamIDs::REVERB_MIX, 0.5f);
            setParam(ParamIDs::VINTAGE, 0.6f);
            break;

        case 18: // Voltage Ghost
            setParam(ParamIDs::MODE, 3);
            setParam(ParamIDs::FM_AMOUNT, 0.7f);
            setParam(ParamIDs::HARD_SYNC, 1);
            setParam(ParamIDs::FILTER_CUTOFF, 3000);
            setParam(ParamIDs::AMP_ATTACK, 0.5f);
            setParam(ParamIDs::AMP_RELEASE, 2.0f);
            setParam(ParamIDs::VINTAGE, 0.4f);
            break;

        case 19: // Retro Sequence
            setParam(ParamIDs::MODE, 3);
            setParam(ParamIDs::FILTER_CUTOFF, 2500);
            setParam(ParamIDs::FILTER_ENV_AMT, 0.7f);
            setParam(ParamIDs::ARP_ON, 1);
            setParam(ParamIDs::ARP_RATE, 3);
            setParam(ParamIDs::ARP_GATE, 0.5f);
            setParam(ParamIDs::DELAY_ON, 1);
            setParam(ParamIDs::DELAY_TAPE, 1);
            break;

        // Init presets (20-23)
        case 20: setParam(ParamIDs::MODE, 0); break; // Init Cosmos
        case 21: setParam(ParamIDs::MODE, 1); break; // Init Oracle
        case 22: setParam(ParamIDs::MODE, 2); break; // Init Mono
        case 23: setParam(ParamIDs::MODE, 3); break; // Init Modular
    }
}

//==============================================================================
void MultiSynthProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MultiSynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessorEditor* MultiSynthProcessor::createEditor()
{
    return new MultiSynthEditor(*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultiSynthProcessor();
}

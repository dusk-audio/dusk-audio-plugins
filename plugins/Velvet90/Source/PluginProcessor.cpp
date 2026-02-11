/*
  ==============================================================================

    PluginProcessor.cpp
    Velvet 90 - Algorithmic Reverb with Plate, Room, Hall modes

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

Velvet90Processor::Velvet90Processor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Main controls
    modeParam = apvts.getRawParameterValue("mode");
    colorParam = apvts.getRawParameterValue("color");
    sizeParam = apvts.getRawParameterValue("size");
    dampingParam = apvts.getRawParameterValue("damping");
    widthParam = apvts.getRawParameterValue("width");
    mixParam = apvts.getRawParameterValue("mix");
    preDelayParam = apvts.getRawParameterValue("predelay");

    // Modulation
    modRateParam = apvts.getRawParameterValue("modrate");
    modDepthParam = apvts.getRawParameterValue("moddepth");

    // Bass decay
    bassMultParam = apvts.getRawParameterValue("bassmult");
    bassFreqParam = apvts.getRawParameterValue("bassfreq");

    // Diffusion & Balance
    earlyDiffParam = apvts.getRawParameterValue("earlydiff");
    lateDiffParam = apvts.getRawParameterValue("latediff");
    earlyLateBalParam = apvts.getRawParameterValue("erlatebal");

    // Room Size & HF Decay
    roomSizeParam = apvts.getRawParameterValue("roomsize");
    highDecayParam = apvts.getRawParameterValue("highdecay");

    // 4-band decay & ER controls
    midDecayParam = apvts.getRawParameterValue("middecay");
    highFreqParam = apvts.getRawParameterValue("highfreq");
    erShapeParam = apvts.getRawParameterValue("ershape");
    erSpreadParam = apvts.getRawParameterValue("erspread");
    erBassCutParam = apvts.getRawParameterValue("erbasscut");

    // Output EQ
    highCutParam = apvts.getRawParameterValue("highcut");
    lowCutParam = apvts.getRawParameterValue("lowcut");

    // Freeze
    freezeParam = apvts.getRawParameterValue("freeze");

    // Treble & Stereo (optimizer-controllable)
    trebleRatioParam = apvts.getRawParameterValue("trebleratio");
    stereoCouplingParam = apvts.getRawParameterValue("stereocoupling");

    // Low-Mid Decay (optimizer-controllable)
    lowMidFreqParam = apvts.getRawParameterValue("lowmidfreq");
    lowMidDecayParam = apvts.getRawParameterValue("lowmiddecay");

    // Envelope Shaper (optimizer-controllable)
    envModeParam = apvts.getRawParameterValue("envmode");
    envHoldParam = apvts.getRawParameterValue("envhold");
    envReleaseParam = apvts.getRawParameterValue("envrelease");
    envDepthParam = apvts.getRawParameterValue("envdepth");
    echoDelayParam = apvts.getRawParameterValue("echodelay");
    echoFeedbackParam = apvts.getRawParameterValue("echofeedback");

    // Parametric Output EQ (optimizer-controllable)
    outEQ1FreqParam = apvts.getRawParameterValue("outeq1freq");
    outEQ1GainParam = apvts.getRawParameterValue("outeq1gain");
    outEQ1QParam = apvts.getRawParameterValue("outeq1q");
    outEQ2FreqParam = apvts.getRawParameterValue("outeq2freq");
    outEQ2GainParam = apvts.getRawParameterValue("outeq2gain");
    outEQ2QParam = apvts.getRawParameterValue("outeq2q");

    // Stereo Invert & Resonance
    stereoInvertParam = apvts.getRawParameterValue("stereoinvert");
    resonanceParam = apvts.getRawParameterValue("resonance");

    // Echo Ping-Pong & Dynamics
    echoPingPongParam = apvts.getRawParameterValue("echopingpong");
    dynAmountParam = apvts.getRawParameterValue("dynamount");
    dynSpeedParam = apvts.getRawParameterValue("dynspeed");

    // Pre-delay tempo sync
    preDelaySyncParam = apvts.getRawParameterValue("predelaysync");
    preDelayNoteParam = apvts.getRawParameterValue("predelaynote");
}

Velvet90Processor::~Velvet90Processor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout Velvet90Processor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Mode: 10 reverb algorithms
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("mode", 3),
        "Mode",
        juce::StringArray{ "Plate", "Room", "Hall", "Chamber", "Cathedral", "Ambience",
                           "Bright Hall", "Chorus Space", "Random Space", "Dirty Hall" },
        0));

    // Color: 0=1970s, 1=1980s, 2=Now
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("color", 2),
        "Color",
        juce::StringArray{ "1970s", "1980s", "Now" },
        2));

    // Size (decay time): 0.3s to 10s with exponential curve
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("size", 1),
        "Size",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.4f,
        juce::AudioParameterFloatAttributes()
            .withLabel("")
            .withStringFromValueFunction([](float value, int) {
                // Match DSP: 0.1s + value^1.5 * 9.9s (before mode multiplier)
                float seconds = 0.1f + std::pow(value, 1.5f) * 9.9f;
                return juce::String(seconds, 1) + "s";
            })));

    // Room Size: scales delay line lengths independently from decay time
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("roomsize", 1),
        "Room Size",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Pre-delay: 0-250ms
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("predelay", 1),
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 250.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 1) + " ms";
            })));

    // Damping: bright to dark
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("damping", 1),
        "Damping",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Width: mono to stereo
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("width", 1),
        "Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Mix: dry/wet
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1),
        "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.35f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Mod Rate: 0.1 - 5.0 Hz
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("modrate", 1),
        "Mod Rate",
        juce::NormalisableRange<float>(0.1f, 5.0f, 0.01f, 0.5f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 2) + " Hz";
            })));

    // Mod Depth: 0-100%
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("moddepth", 1),
        "Mod Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Bass Mult: 0.1x - 3.0x
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("bassmult", 1),
        "Bass Mult",
        juce::NormalisableRange<float>(0.1f, 3.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("x")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 2) + "x";
            })));

    // Bass Freq: 100-1000 Hz
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("bassfreq", 1),
        "Bass Freq",
        juce::NormalisableRange<float>(100.0f, 1000.0f, 1.0f, 0.5f),
        500.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value)) + " Hz";
            })));

    // Early Diffusion: 0-100%
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("earlydiff", 1),
        "Early Diff",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.7f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Late Diffusion: 0-100%
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("latediff", 1),
        "Late Diff",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // High Cut: 1000-20000 Hz
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("highcut", 1),
        "High Cut",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f, 0.3f),
        12000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int) {
                if (value >= 1000.0f)
                    return juce::String(value / 1000.0f, 1) + " kHz";
                return juce::String(static_cast<int>(value)) + " Hz";
            })));

    // Low Cut: 20-500 Hz
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lowcut", 1),
        "Low Cut",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f),
        20.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value)) + " Hz";
            })));

    // Early/Late Balance: controls ER vs late tail mix
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("erlatebal", 1),
        "ER/Late",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.7f,
        juce::AudioParameterFloatAttributes()
            .withLabel("")
            .withStringFromValueFunction([](float value, int) {
                if (value < 0.05f) return juce::String("Early");
                if (value > 0.95f) return juce::String("Late");
                return juce::String("E") + juce::String(static_cast<int>((1.0f - value) * 100))
                    + "/L" + juce::String(static_cast<int>(value * 100));
            })));

    // HF Decay Multiplier: user control over high-frequency decay rate
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("highdecay", 1),
        "HF Decay",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("x")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 2) + "x";
            })));

    // Mid Decay Multiplier: mid-frequency decay control (4-band decay system)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("middecay", 1),
        "Mid Decay",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("x")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 2) + "x";
            })));

    // High Frequency: upper crossover for 4-band decay filter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("highfreq", 1),
        "High Freq",
        juce::NormalisableRange<float>(1000.0f, 12000.0f, 1.0f, 0.3f),
        4000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int) {
                if (value >= 1000.0f)
                    return juce::String(value / 1000.0f, 1) + " kHz";
                return juce::String(static_cast<int>(value)) + " Hz";
            })));

    // ER Shape: early reflection envelope shape
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("ershape", 1),
        "ER Shape",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // ER Spread: early reflection timing spread
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("erspread", 1),
        "ER Spread",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // ER Bass Cut: high-pass filter on early reflections (reduces bass buildup in short reverbs)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("erbasscut", 1),
        "ER Bass Cut",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f),
        20.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value)) + " Hz";
            })));

    // Pre-delay tempo sync toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("predelaysync", 1),
        "Pre-Delay Sync",
        false));

    // Pre-delay note value (for tempo sync)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("predelaynote", 1),
        "Pre-Delay Note",
        juce::StringArray{ "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D" },
        4));  // default: 1/8

    // Treble Ratio: multiplier on damping-derived treble decay (optimizer-controllable)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("trebleratio", 1),
        "Treble Ratio",
        juce::NormalisableRange<float>(0.3f, 2.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 2) + "x";
            })));

    // Stereo Coupling: cross-channel feedback amount (optimizer-controllable)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("stereocoupling", 1),
        "Stereo Coupling",
        juce::NormalisableRange<float>(0.0f, 0.5f, 0.01f),
        0.15f,
        juce::AudioParameterFloatAttributes()
            .withLabel("")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Low-Mid Decay (5-band crossover split, optimizer-controllable)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lowmidfreq", 1),
        "Low-Mid Freq",
        juce::NormalisableRange<float>(100.0f, 8000.0f, 1.0f, 0.5f),
        700.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value)) + " Hz";
            })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lowmiddecay", 1),
        "Low-Mid Decay",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("x")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 2) + "x";
            })));

    // Envelope Shaper (for non-linear presets, optimizer-controllable)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("envmode", 1),
        "Env Mode",
        juce::StringArray{ "Off", "Gate", "Reverse", "Swell", "Ducked" },
        0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("envhold", 1),
        "Env Hold",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f),
        500.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("envrelease", 1),
        "Env Release",
        juce::NormalisableRange<float>(10.0f, 3000.0f, 1.0f),
        500.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("envdepth", 1),
        "Env Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("echodelay", 1),
        "Echo Delay",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("echofeedback", 1),
        "Echo Feedback",
        juce::NormalisableRange<float>(0.0f, 90.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Parametric Output EQ (2-band peaking, optimizer-controllable)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outeq1freq", 1),
        "Out EQ1 Freq",
        juce::NormalisableRange<float>(100.0f, 8000.0f, 1.0f, 0.5f),
        1000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outeq1gain", 1),
        "Out EQ1 Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outeq1q", 1),
        "Out EQ1 Q",
        juce::NormalisableRange<float>(0.3f, 5.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outeq2freq", 1),
        "Out EQ2 Freq",
        juce::NormalisableRange<float>(100.0f, 8000.0f, 1.0f, 0.5f),
        4000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outeq2gain", 1),
        "Out EQ2 Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outeq2q", 1),
        "Out EQ2 Q",
        juce::NormalisableRange<float>(0.3f, 5.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()));

    // Stereo Invert: anti-correlation for wide vintage-style stereo imaging
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("stereoinvert", 1),
        "Stereo Invert",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Resonance: metallic/resonant coloration (reduces diffusion, shifts delay ratios)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("resonance", 1),
        "Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Echo Ping-Pong: cross-channel echo feedback blend (optimizer-controllable)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("echopingpong", 1),
        "Echo Ping-Pong",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Dynamics Amount: sidechain dynamics (-1=duck, 0=off, +1=expand) (optimizer-controllable)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("dynamount", 1),
        "Dyn Amount",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("")
            .withStringFromValueFunction([](float value, int) {
                if (value < -0.005f) return juce::String("Duck ") + juce::String(juce::roundToInt(std::abs(value) * 100.0f)) + "%";
                if (value > 0.005f) return juce::String("Expand ") + juce::String(juce::roundToInt(value * 100.0f)) + "%";
                return juce::String("Off");
            })));

    // Dynamics Speed: envelope follower speed (optimizer-controllable)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("dynspeed", 1),
        "Dyn Speed",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value * 100)) + "%";
            })));

    // Freeze mode toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("freeze", 1),
        "Freeze",
        false));

    return { params.begin(), params.end() };
}

const juce::String Velvet90Processor::getName() const
{
    return JucePlugin_Name;
}

bool Velvet90Processor::acceptsMidi() const { return false; }
bool Velvet90Processor::producesMidi() const { return false; }
bool Velvet90Processor::isMidiEffect() const { return false; }
double Velvet90Processor::getTailLengthSeconds() const { return 5.0; }

int Velvet90Processor::getNumPrograms()
{
    return static_cast<int>(Velvet90Presets::getFactoryPresets().size()) + 1;
}

int Velvet90Processor::getCurrentProgram() { return currentPresetIndex; }

void Velvet90Processor::setCurrentProgram(int index)
{
    auto presets = Velvet90Presets::getFactoryPresets();
    if (index > 0 && index <= static_cast<int>(presets.size()))
    {
        Velvet90Presets::applyPreset(apvts, presets[static_cast<size_t>(index - 1)]);
        currentPresetIndex = index;
    }
    else
    {
        currentPresetIndex = 0;
    }
}

const juce::String Velvet90Processor::getProgramName(int index)
{
    if (index == 0)
        return "Init";

    auto presets = Velvet90Presets::getFactoryPresets();
    if (index > 0 && index <= static_cast<int>(presets.size()))
        return presets[static_cast<size_t>(index - 1)].name;

    return {};
}

void Velvet90Processor::changeProgramName(int, const juce::String&) {}

void Velvet90Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    reverbEngine.prepare(sampleRate, samplesPerBlock);

    // Initialize smoothed values
    smoothedSize.reset(sampleRate, 0.05);
    smoothedDamping.reset(sampleRate, 0.05);
    smoothedWidth.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    smoothedPreDelay.reset(sampleRate, 0.05);
    smoothedModRate.reset(sampleRate, 0.1);
    smoothedModDepth.reset(sampleRate, 0.05);
    smoothedBassMult.reset(sampleRate, 0.05);
    smoothedBassFreq.reset(sampleRate, 0.05);
    smoothedEarlyDiff.reset(sampleRate, 0.05);
    smoothedLateDiff.reset(sampleRate, 0.05);
    smoothedRoomSize.reset(sampleRate, 0.1);
    smoothedEarlyLateBal.reset(sampleRate, 0.02);
    smoothedHighDecay.reset(sampleRate, 0.05);
    smoothedMidDecay.reset(sampleRate, 0.05);
    smoothedHighFreq.reset(sampleRate, 0.05);
    smoothedERShape.reset(sampleRate, 0.05);
    smoothedERSpread.reset(sampleRate, 0.05);
    smoothedERBassCut.reset(sampleRate, 0.05);
    smoothedHighCut.reset(sampleRate, 0.05);
    smoothedLowCut.reset(sampleRate, 0.05);
    smoothedTrebleRatio.reset(sampleRate, 0.05);
    smoothedStereoCoupling.reset(sampleRate, 0.05);
    smoothedLowMidFreq.reset(sampleRate, 0.05);
    smoothedLowMidDecay.reset(sampleRate, 0.05);
    smoothedEnvHold.reset(sampleRate, 0.05);
    smoothedEnvRelease.reset(sampleRate, 0.05);
    smoothedEnvDepth.reset(sampleRate, 0.05);
    smoothedEchoDelay.reset(sampleRate, 0.05);
    smoothedEchoFeedback.reset(sampleRate, 0.05);
    smoothedOutEQ1Freq.reset(sampleRate, 0.05);
    smoothedOutEQ1Gain.reset(sampleRate, 0.05);
    smoothedOutEQ1Q.reset(sampleRate, 0.05);
    smoothedOutEQ2Freq.reset(sampleRate, 0.05);
    smoothedOutEQ2Gain.reset(sampleRate, 0.05);
    smoothedOutEQ2Q.reset(sampleRate, 0.05);
    smoothedStereoInvert.reset(sampleRate, 0.05);
    smoothedResonance.reset(sampleRate, 0.1);
    smoothedEchoPingPong.reset(sampleRate, 0.05);
    smoothedDynAmount.reset(sampleRate, 0.05);
    smoothedDynSpeed.reset(sampleRate, 0.05);

    // Set initial values
    smoothedSize.setCurrentAndTargetValue(sizeParam->load());
    smoothedDamping.setCurrentAndTargetValue(dampingParam->load());
    smoothedWidth.setCurrentAndTargetValue(widthParam->load());
    smoothedMix.setCurrentAndTargetValue(mixParam->load());
    smoothedPreDelay.setCurrentAndTargetValue(preDelayParam->load());
    smoothedModRate.setCurrentAndTargetValue(modRateParam->load());
    smoothedModDepth.setCurrentAndTargetValue(modDepthParam->load());
    smoothedBassMult.setCurrentAndTargetValue(bassMultParam->load());
    smoothedBassFreq.setCurrentAndTargetValue(bassFreqParam->load());
    smoothedEarlyDiff.setCurrentAndTargetValue(earlyDiffParam->load());
    smoothedLateDiff.setCurrentAndTargetValue(lateDiffParam->load());
    smoothedRoomSize.setCurrentAndTargetValue(roomSizeParam->load());
    smoothedEarlyLateBal.setCurrentAndTargetValue(earlyLateBalParam->load());
    smoothedHighDecay.setCurrentAndTargetValue(highDecayParam->load());
    smoothedMidDecay.setCurrentAndTargetValue(midDecayParam->load());
    smoothedHighFreq.setCurrentAndTargetValue(highFreqParam->load());
    smoothedERShape.setCurrentAndTargetValue(erShapeParam->load());
    smoothedERSpread.setCurrentAndTargetValue(erSpreadParam->load());
    smoothedERBassCut.setCurrentAndTargetValue(erBassCutParam->load());
    smoothedHighCut.setCurrentAndTargetValue(highCutParam->load());
    smoothedLowCut.setCurrentAndTargetValue(lowCutParam->load());
    smoothedTrebleRatio.setCurrentAndTargetValue(trebleRatioParam->load());
    smoothedStereoCoupling.setCurrentAndTargetValue(stereoCouplingParam->load());
    smoothedLowMidFreq.setCurrentAndTargetValue(lowMidFreqParam->load());
    smoothedLowMidDecay.setCurrentAndTargetValue(lowMidDecayParam->load());
    smoothedEnvHold.setCurrentAndTargetValue(envHoldParam->load());
    smoothedEnvRelease.setCurrentAndTargetValue(envReleaseParam->load());
    smoothedEnvDepth.setCurrentAndTargetValue(envDepthParam->load());
    smoothedEchoDelay.setCurrentAndTargetValue(echoDelayParam->load());
    smoothedEchoFeedback.setCurrentAndTargetValue(echoFeedbackParam->load());
    smoothedOutEQ1Freq.setCurrentAndTargetValue(outEQ1FreqParam->load());
    smoothedOutEQ1Gain.setCurrentAndTargetValue(outEQ1GainParam->load());
    smoothedOutEQ1Q.setCurrentAndTargetValue(outEQ1QParam->load());
    smoothedOutEQ2Freq.setCurrentAndTargetValue(outEQ2FreqParam->load());
    smoothedOutEQ2Gain.setCurrentAndTargetValue(outEQ2GainParam->load());
    smoothedOutEQ2Q.setCurrentAndTargetValue(outEQ2QParam->load());
    smoothedStereoInvert.setCurrentAndTargetValue(stereoInvertParam->load());
    smoothedResonance.setCurrentAndTargetValue(resonanceParam->load());
    smoothedEchoPingPong.setCurrentAndTargetValue(echoPingPongParam->load());
    smoothedDynAmount.setCurrentAndTargetValue(dynAmountParam->load());
    smoothedDynSpeed.setCurrentAndTargetValue(dynSpeedParam->load());

    // Set initial envelope shaper state
    reverbEngine.setEnvMode(static_cast<int>(envModeParam->load()));
    reverbEngine.setEnvHold(envHoldParam->load());
    reverbEngine.setEnvRelease(envReleaseParam->load());
    reverbEngine.setEnvDepth(envDepthParam->load() / 100.0f);
    reverbEngine.setEchoDelay(echoDelayParam->load());
    reverbEngine.setEchoFeedback(echoFeedbackParam->load() / 100.0f);

    // Set initial mode and color
    int mode = static_cast<int>(modeParam->load());
    reverbEngine.setMode(static_cast<Velvet90::ReverbMode>(mode));
    lastMode = mode;

    // Always use clean/modern mode (color modes removed — vintage character is in mode tuning)
    reverbEngine.setColor(Velvet90::ColorMode::Now);

    // Apply initial parameter values to engine
    reverbEngine.setSize(sizeParam->load());
    reverbEngine.setDamping(dampingParam->load());
    reverbEngine.setWidth(widthParam->load());
    reverbEngine.setMix(mixParam->load());
    reverbEngine.setPreDelay(preDelayParam->load());
    reverbEngine.setModRate(modRateParam->load());
    reverbEngine.setModDepth(modDepthParam->load());
    reverbEngine.setBassMult(bassMultParam->load());
    reverbEngine.setBassFreq(bassFreqParam->load());
    reverbEngine.setEarlyDiffusion(earlyDiffParam->load());
    reverbEngine.setLateDiffusion(lateDiffParam->load());
    reverbEngine.setRoomSize(roomSizeParam->load());
    reverbEngine.setEarlyLateBalance(earlyLateBalParam->load());
    reverbEngine.setHighDecayMult(highDecayParam->load());
    reverbEngine.setMidDecayMult(midDecayParam->load());
    reverbEngine.setHighFreq(highFreqParam->load());
    reverbEngine.setERShape(erShapeParam->load());
    reverbEngine.setERSpread(erSpreadParam->load());
    reverbEngine.setERBassCut(erBassCutParam->load());
    reverbEngine.setHighCut(highCutParam->load());
    reverbEngine.setLowCut(lowCutParam->load());
    reverbEngine.setTrebleRatio(trebleRatioParam->load());
    reverbEngine.setStereoCoupling(stereoCouplingParam->load());
    reverbEngine.setLowMidFreq(lowMidFreqParam->load());
    reverbEngine.setLowMidDecayMult(lowMidDecayParam->load());
    reverbEngine.setOutEQ1(outEQ1FreqParam->load(), outEQ1GainParam->load(), outEQ1QParam->load());
    reverbEngine.setOutEQ2(outEQ2FreqParam->load(), outEQ2GainParam->load(), outEQ2QParam->load());
    reverbEngine.setStereoInvert(stereoInvertParam->load());
    reverbEngine.setResonance(resonanceParam->load());
    reverbEngine.setEchoPingPong(echoPingPongParam->load());
    reverbEngine.setDynAmount(dynAmountParam->load());
    reverbEngine.setDynSpeed(dynSpeedParam->load());
}

void Velvet90Processor::releaseResources()
{
    reverbEngine.reset();
}

bool Velvet90Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Only support stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Input can be mono or stereo
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void Velvet90Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Check for mode change
    int currentMode = static_cast<int>(modeParam->load());
    if (currentMode != lastMode)
    {
        reverbEngine.setMode(static_cast<Velvet90::ReverbMode>(currentMode));
        lastMode = currentMode;
    }

    // Update freeze state
    reverbEngine.setFreeze(freezeParam->load() > 0.5f);

    // Update smoothed parameters
    smoothedSize.setTargetValue(sizeParam->load());
    smoothedDamping.setTargetValue(dampingParam->load());
    smoothedWidth.setTargetValue(widthParam->load());
    smoothedMix.setTargetValue(mixParam->load());
    smoothedPreDelay.setTargetValue(preDelayParam->load());
    smoothedModRate.setTargetValue(modRateParam->load());
    smoothedModDepth.setTargetValue(modDepthParam->load());
    smoothedBassMult.setTargetValue(bassMultParam->load());
    smoothedBassFreq.setTargetValue(bassFreqParam->load());
    smoothedEarlyDiff.setTargetValue(earlyDiffParam->load());
    smoothedLateDiff.setTargetValue(lateDiffParam->load());
    smoothedRoomSize.setTargetValue(roomSizeParam->load());
    smoothedEarlyLateBal.setTargetValue(earlyLateBalParam->load());
    smoothedHighDecay.setTargetValue(highDecayParam->load());
    smoothedMidDecay.setTargetValue(midDecayParam->load());
    smoothedHighFreq.setTargetValue(highFreqParam->load());
    smoothedERShape.setTargetValue(erShapeParam->load());
    smoothedERSpread.setTargetValue(erSpreadParam->load());
    smoothedERBassCut.setTargetValue(erBassCutParam->load());
    smoothedHighCut.setTargetValue(highCutParam->load());
    smoothedLowCut.setTargetValue(lowCutParam->load());
    smoothedTrebleRatio.setTargetValue(trebleRatioParam->load());
    smoothedStereoCoupling.setTargetValue(stereoCouplingParam->load());
    smoothedLowMidFreq.setTargetValue(lowMidFreqParam->load());
    smoothedLowMidDecay.setTargetValue(lowMidDecayParam->load());
    smoothedEnvHold.setTargetValue(envHoldParam->load());
    smoothedEnvRelease.setTargetValue(envReleaseParam->load());
    smoothedEnvDepth.setTargetValue(envDepthParam->load());
    smoothedEchoDelay.setTargetValue(echoDelayParam->load());
    smoothedEchoFeedback.setTargetValue(echoFeedbackParam->load());
    smoothedOutEQ1Freq.setTargetValue(outEQ1FreqParam->load());
    smoothedOutEQ1Gain.setTargetValue(outEQ1GainParam->load());
    smoothedOutEQ1Q.setTargetValue(outEQ1QParam->load());
    smoothedOutEQ2Freq.setTargetValue(outEQ2FreqParam->load());
    smoothedOutEQ2Gain.setTargetValue(outEQ2GainParam->load());
    smoothedOutEQ2Q.setTargetValue(outEQ2QParam->load());
    smoothedStereoInvert.setTargetValue(stereoInvertParam->load());
    smoothedResonance.setTargetValue(resonanceParam->load());
    smoothedEchoPingPong.setTargetValue(echoPingPongParam->load());
    smoothedDynAmount.setTargetValue(dynAmountParam->load());
    smoothedDynSpeed.setTargetValue(dynSpeedParam->load());

    // Envelope mode is discrete — apply directly
    reverbEngine.setEnvMode(static_cast<int>(envModeParam->load()));

    // Pre-delay tempo sync: calculate ms from BPM if sync is active
    bool preDelaySync = preDelaySyncParam->load() > 0.5f;
    if (preDelaySync)
    {
        float syncMs = 0.0f;
        if (auto* playHead = getPlayHead())
        {
            if (auto pos = playHead->getPosition())
            {
                if (auto bpm = pos->getBpm())
                {
                    float beatsPerMs = static_cast<float>(*bpm) / 60000.0f;
                    int noteIdx = static_cast<int>(preDelayNoteParam->load());
                    // Note values in beats: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D
                    constexpr float noteValues[] = {
                        0.125f,      // 1/32
                        0.1667f,     // 1/16T (1/6 of a beat)
                        0.25f,       // 1/16
                        0.3333f,     // 1/8T
                        0.5f,        // 1/8
                        0.75f,       // 1/8D (dotted)
                        1.0f,        // 1/4
                        1.5f         // 1/4D (dotted)
                    };
                    noteIdx = std::clamp(noteIdx, 0, 7);
                    syncMs = noteValues[noteIdx] / beatsPerMs;
                    syncMs = std::min(syncMs, 250.0f);  // Clamp to pre-delay max
                }
            }
        }
        smoothedPreDelay.setTargetValue(syncMs);
    }

    // Get channel pointers - always write stereo output
    jassert(totalNumOutputChannels >= 2);
    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);    auto* inputLeftChannel = buffer.getReadPointer(0);
    auto* inputRightChannel = buffer.getReadPointer(totalNumInputChannels > 1 ? 1 : 0);
    float peakL = 0.0f, peakR = 0.0f;

    // Process sample by sample for smooth parameter changes
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        // Update parameters when smoothing
        if (smoothedSize.isSmoothing())
            reverbEngine.setSize(smoothedSize.getNextValue());
        else
            smoothedSize.skip(1);

        if (smoothedDamping.isSmoothing())
            reverbEngine.setDamping(smoothedDamping.getNextValue());
        else
            smoothedDamping.skip(1);

        if (smoothedWidth.isSmoothing())
            reverbEngine.setWidth(smoothedWidth.getNextValue());
        else
            smoothedWidth.skip(1);

        if (smoothedMix.isSmoothing())
            reverbEngine.setMix(smoothedMix.getNextValue());
        else
            smoothedMix.skip(1);

        if (smoothedPreDelay.isSmoothing())
            reverbEngine.setPreDelay(smoothedPreDelay.getNextValue());
        else
            smoothedPreDelay.skip(1);

        if (smoothedModRate.isSmoothing())
            reverbEngine.setModRate(smoothedModRate.getNextValue());
        else
            smoothedModRate.skip(1);

        if (smoothedModDepth.isSmoothing())
            reverbEngine.setModDepth(smoothedModDepth.getNextValue());
        else
            smoothedModDepth.skip(1);

        if (smoothedBassMult.isSmoothing())
            reverbEngine.setBassMult(smoothedBassMult.getNextValue());
        else
            smoothedBassMult.skip(1);

        if (smoothedBassFreq.isSmoothing())
            reverbEngine.setBassFreq(smoothedBassFreq.getNextValue());
        else
            smoothedBassFreq.skip(1);

        if (smoothedEarlyDiff.isSmoothing())
            reverbEngine.setEarlyDiffusion(smoothedEarlyDiff.getNextValue());
        else
            smoothedEarlyDiff.skip(1);

        if (smoothedLateDiff.isSmoothing())
            reverbEngine.setLateDiffusion(smoothedLateDiff.getNextValue());
        else
            smoothedLateDiff.skip(1);

        if (smoothedRoomSize.isSmoothing())
            reverbEngine.setRoomSize(smoothedRoomSize.getNextValue());
        else
            smoothedRoomSize.skip(1);

        if (smoothedEarlyLateBal.isSmoothing())
            reverbEngine.setEarlyLateBalance(smoothedEarlyLateBal.getNextValue());
        else
            smoothedEarlyLateBal.skip(1);

        if (smoothedHighDecay.isSmoothing())
            reverbEngine.setHighDecayMult(smoothedHighDecay.getNextValue());
        else
            smoothedHighDecay.skip(1);

        if (smoothedMidDecay.isSmoothing())
            reverbEngine.setMidDecayMult(smoothedMidDecay.getNextValue());
        else
            smoothedMidDecay.skip(1);

        if (smoothedHighFreq.isSmoothing())
            reverbEngine.setHighFreq(smoothedHighFreq.getNextValue());
        else
            smoothedHighFreq.skip(1);

        if (smoothedERShape.isSmoothing())
            reverbEngine.setERShape(smoothedERShape.getNextValue());
        else
            smoothedERShape.skip(1);

        if (smoothedERSpread.isSmoothing())
            reverbEngine.setERSpread(smoothedERSpread.getNextValue());
        else
            smoothedERSpread.skip(1);

        if (smoothedERBassCut.isSmoothing())
            reverbEngine.setERBassCut(smoothedERBassCut.getNextValue());
        else
            smoothedERBassCut.skip(1);

        if (smoothedHighCut.isSmoothing())
            reverbEngine.setHighCut(smoothedHighCut.getNextValue());
        else
            smoothedHighCut.skip(1);

        if (smoothedLowCut.isSmoothing())
            reverbEngine.setLowCut(smoothedLowCut.getNextValue());
        else
            smoothedLowCut.skip(1);

        if (smoothedTrebleRatio.isSmoothing())
            reverbEngine.setTrebleRatio(smoothedTrebleRatio.getNextValue());
        else
            smoothedTrebleRatio.skip(1);

        if (smoothedStereoCoupling.isSmoothing())
            reverbEngine.setStereoCoupling(smoothedStereoCoupling.getNextValue());
        else
            smoothedStereoCoupling.skip(1);

        if (smoothedStereoInvert.isSmoothing())
            reverbEngine.setStereoInvert(smoothedStereoInvert.getNextValue());
        else
            smoothedStereoInvert.skip(1);

        if (smoothedResonance.isSmoothing())
            reverbEngine.setResonance(smoothedResonance.getNextValue());
        else
            smoothedResonance.skip(1);

        if (smoothedLowMidFreq.isSmoothing())
            reverbEngine.setLowMidFreq(smoothedLowMidFreq.getNextValue());
        else
            smoothedLowMidFreq.skip(1);

        if (smoothedLowMidDecay.isSmoothing())
            reverbEngine.setLowMidDecayMult(smoothedLowMidDecay.getNextValue());
        else
            smoothedLowMidDecay.skip(1);

        if (smoothedEnvHold.isSmoothing())
            reverbEngine.setEnvHold(smoothedEnvHold.getNextValue());
        else
            smoothedEnvHold.skip(1);

        if (smoothedEnvRelease.isSmoothing())
            reverbEngine.setEnvRelease(smoothedEnvRelease.getNextValue());
        else
            smoothedEnvRelease.skip(1);

        if (smoothedEnvDepth.isSmoothing())
            reverbEngine.setEnvDepth(smoothedEnvDepth.getNextValue() / 100.0f);
        else
            smoothedEnvDepth.skip(1);

        if (smoothedEchoDelay.isSmoothing())
            reverbEngine.setEchoDelay(smoothedEchoDelay.getNextValue());
        else
            smoothedEchoDelay.skip(1);

        if (smoothedEchoFeedback.isSmoothing())
            reverbEngine.setEchoFeedback(smoothedEchoFeedback.getNextValue() / 100.0f);
        else
            smoothedEchoFeedback.skip(1);

        if (smoothedEchoPingPong.isSmoothing())
            reverbEngine.setEchoPingPong(smoothedEchoPingPong.getNextValue());
        else
            smoothedEchoPingPong.skip(1);

        if (smoothedDynAmount.isSmoothing())
            reverbEngine.setDynAmount(smoothedDynAmount.getNextValue());
        else
            smoothedDynAmount.skip(1);

        if (smoothedDynSpeed.isSmoothing())
            reverbEngine.setDynSpeed(smoothedDynSpeed.getNextValue());
        else
            smoothedDynSpeed.skip(1);

        // Parametric output EQ — update when any band is smoothing
        if (smoothedOutEQ1Freq.isSmoothing() || smoothedOutEQ1Gain.isSmoothing() || smoothedOutEQ1Q.isSmoothing())
            reverbEngine.setOutEQ1(smoothedOutEQ1Freq.getNextValue(), smoothedOutEQ1Gain.getNextValue(), smoothedOutEQ1Q.getNextValue());
        else
        {
            smoothedOutEQ1Freq.skip(1);
            smoothedOutEQ1Gain.skip(1);
            smoothedOutEQ1Q.skip(1);
        }

        if (smoothedOutEQ2Freq.isSmoothing() || smoothedOutEQ2Gain.isSmoothing() || smoothedOutEQ2Q.isSmoothing())
            reverbEngine.setOutEQ2(smoothedOutEQ2Freq.getNextValue(), smoothedOutEQ2Gain.getNextValue(), smoothedOutEQ2Q.getNextValue());
        else
        {
            smoothedOutEQ2Freq.skip(1);
            smoothedOutEQ2Gain.skip(1);
            smoothedOutEQ2Q.skip(1);
        }

        float inputL = inputLeftChannel[sample];
        float inputR = inputRightChannel[sample];

        float outputL, outputR;
        reverbEngine.process(inputL, inputR, outputL, outputR);

        leftChannel[sample] = outputL;
        rightChannel[sample] = outputR;

        peakL = std::max(peakL, std::abs(outputL));
        peakR = std::max(peakR, std::abs(outputR));
    }

    outputLevelL.store(peakL);
    outputLevelR.store(peakR);
}

bool Velvet90Processor::hasEditor() const { return true; }

juce::AudioProcessorEditor* Velvet90Processor::createEditor()
{
    return new Velvet90Editor(*this);
}

void Velvet90Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void Velvet90Processor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));

        // Snap bool parameters after state restore — replaceState can leave
        // bool values unsnapped when the ValueTree stores intermediate floats
        for (const auto* paramId : { "freeze", "predelaysync" })
        {
            if (auto* p = apvts.getParameter(paramId))
            {
                float v = p->getValue();
                p->setValueNotifyingHost(v >= 0.5f ? 1.0f : 0.0f);
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Velvet90Processor();
}

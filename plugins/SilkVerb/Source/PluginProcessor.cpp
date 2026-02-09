/*
  ==============================================================================

    PluginProcessor.cpp
    SilkVerb - Algorithmic Reverb with Plate, Room, Hall modes

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

SilkVerbProcessor::SilkVerbProcessor()
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

    // Pre-delay tempo sync
    preDelaySyncParam = apvts.getRawParameterValue("predelaysync");
    preDelayNoteParam = apvts.getRawParameterValue("predelaynote");
}

SilkVerbProcessor::~SilkVerbProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout SilkVerbProcessor::createParameterLayout()
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

    // Freeze mode toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("freeze", 1),
        "Freeze",
        false));

    return { params.begin(), params.end() };
}

const juce::String SilkVerbProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SilkVerbProcessor::acceptsMidi() const { return false; }
bool SilkVerbProcessor::producesMidi() const { return false; }
bool SilkVerbProcessor::isMidiEffect() const { return false; }
double SilkVerbProcessor::getTailLengthSeconds() const { return 5.0; }

int SilkVerbProcessor::getNumPrograms()
{
    return static_cast<int>(SilkVerbPresets::getFactoryPresets().size()) + 1;
}

int SilkVerbProcessor::getCurrentProgram() { return currentPresetIndex; }

void SilkVerbProcessor::setCurrentProgram(int index)
{
    auto presets = SilkVerbPresets::getFactoryPresets();
    if (index > 0 && index <= static_cast<int>(presets.size()))
    {
        SilkVerbPresets::applyPreset(apvts, presets[static_cast<size_t>(index - 1)]);
        currentPresetIndex = index;
    }
    else
    {
        currentPresetIndex = 0;
    }
}

const juce::String SilkVerbProcessor::getProgramName(int index)
{
    if (index == 0)
        return "Init";

    auto presets = SilkVerbPresets::getFactoryPresets();
    if (index > 0 && index <= static_cast<int>(presets.size()))
        return presets[static_cast<size_t>(index - 1)].name;

    return {};
}

void SilkVerbProcessor::changeProgramName(int, const juce::String&) {}

void SilkVerbProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
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

    // Set initial mode and color
    int mode = static_cast<int>(modeParam->load());
    reverbEngine.setMode(static_cast<SilkVerb::ReverbMode>(mode));
    lastMode = mode;

    // Always use clean/modern mode (color modes removed — Lexicon character is in mode tuning)
    reverbEngine.setColor(SilkVerb::ColorMode::Now);

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
}

void SilkVerbProcessor::releaseResources()
{
    reverbEngine.reset();
}

bool SilkVerbProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

void SilkVerbProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
        reverbEngine.setMode(static_cast<SilkVerb::ReverbMode>(currentMode));
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

bool SilkVerbProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SilkVerbProcessor::createEditor()
{
    return new SilkVerbEditor(*this);
}

void SilkVerbProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SilkVerbProcessor::setStateInformation(const void* data, int sizeInBytes)
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
    return new SilkVerbProcessor();
}

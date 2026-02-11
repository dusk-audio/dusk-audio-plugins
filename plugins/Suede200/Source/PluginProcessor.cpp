/*
  ==============================================================================

    PluginProcessor.cpp
    Suede 200 — Vintage Digital Reverberator

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

Suede200Processor::Suede200Processor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    programParam    = apvts.getRawParameterValue("program");
    predelayParam   = apvts.getRawParameterValue("predelay");
    reverbTimeParam = apvts.getRawParameterValue("reverbtime");
    sizeParam       = apvts.getRawParameterValue("size");
    preEchoesParam  = apvts.getRawParameterValue("preechoes");
    diffusionParam  = apvts.getRawParameterValue("diffusion");
    rtLowParam      = apvts.getRawParameterValue("rtlow");
    rtHighParam     = apvts.getRawParameterValue("rthigh");
    rolloffParam    = apvts.getRawParameterValue("rolloff");
    mixParam        = apvts.getRawParameterValue("mix");
}

Suede200Processor::~Suede200Processor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout Suede200Processor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Program: 6 reverb algorithms
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("program", 1),
        "Program",
        juce::StringArray{ "Concert Hall", "Plate", "Chamber",
                           "Rich Plate", "Rich Splits", "Inverse Rooms" },
        0));

    // Pre-Delay: 0-999 ms
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("predelay", 1),
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 999.0f, 0.1f, 0.4f),
        39.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(value, 1) + " ms";
            })));

    // Reverb Time: 0.6-70.0 s (RT60 at 1kHz)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("reverbtime", 1),
        "Reverb Time",
        juce::NormalisableRange<float>(0.6f, 70.0f, 0.1f, 0.3f),
        2.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("s")
            .withStringFromValueFunction([](float value, int) {
                if (value < 10.0f)
                    return juce::String(value, 1) + " s";
                return juce::String(static_cast<int>(value)) + " s";
            })));

    // Size: 8-90 meters (room size)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("size", 1),
        "Size",
        juce::NormalisableRange<float>(8.0f, 90.0f, 0.5f, 0.5f),
        26.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("m")
            .withStringFromValueFunction([](float value, int) {
                return juce::String(static_cast<int>(value)) + " m";
            })));

    // Pre-Echoes: On/Off
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("preechoes", 1),
        "Pre-Echoes",
        false));

    // Diffusion: Lo/Med/Hi
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("diffusion", 1),
        "Diffusion",
        juce::StringArray{ "Lo", "Med", "Hi" },
        1));

    // RT Contour Low (100Hz): X0.5 / X1.0 / X1.5
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("rtlow", 1),
        "RT Low",
        juce::StringArray{ "X0.5", "X1.0", "X1.5" },
        1));

    // RT Contour High (10kHz): X0.25 / X0.5 / X1.0
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("rthigh", 1),
        "RT High",
        juce::StringArray{ "X0.25", "X0.5", "X1.0" },
        1));

    // Rolloff: 3kHz / 7kHz / 10kHz
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("rolloff", 1),
        "Rolloff",
        juce::StringArray{ "3 kHz", "7 kHz", "10 kHz" },
        2));

    // Mix: 0-100%
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

    return { params.begin(), params.end() };
}

const juce::String Suede200Processor::getName() const
{
    return JucePlugin_Name;
}

bool Suede200Processor::acceptsMidi() const { return false; }
bool Suede200Processor::producesMidi() const { return false; }
bool Suede200Processor::isMidiEffect() const { return false; }
double Suede200Processor::getTailLengthSeconds() const { return 10.0; }

int Suede200Processor::getNumPrograms()
{
    return static_cast<int>(Suede200Presets::getFactoryPresets().size()) + 1;
}

int Suede200Processor::getCurrentProgram() { return currentPresetIndex; }

void Suede200Processor::setCurrentProgram(int index)
{
    auto presets = Suede200Presets::getFactoryPresets();
    if (index > 0 && index <= static_cast<int>(presets.size()))
    {
        auto& preset = presets[static_cast<size_t>(index - 1)];
        Suede200Presets::applyPreset(apvts, preset);

        // Load IR-optimized coefficients if available
        if (preset.hasOptimizedCoeffs)
            reverbEngine.setOptimizedCoefficients(preset.coefficients.data(), 16, preset.coeffRolloffHz);
        else
            reverbEngine.clearOptimizedCoefficients();

        currentPresetIndex = index;
    }
    else
    {
        reverbEngine.clearOptimizedCoefficients();
        currentPresetIndex = 0;
    }
}

const juce::String Suede200Processor::getProgramName(int index)
{
    if (index == 0)
        return "Init";

    auto presets = Suede200Presets::getFactoryPresets();
    if (index > 0 && index <= static_cast<int>(presets.size()))
        return presets[static_cast<size_t>(index - 1)].name;

    return {};
}

void Suede200Processor::changeProgramName(int, const juce::String&) {}

void Suede200Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    reverbEngine.prepare(sampleRate, samplesPerBlock);

    // Initialize smoothed values
    smoothedPreDelay.reset(sampleRate, 0.05);
    smoothedReverbTime.reset(sampleRate, 0.05);
    smoothedSize.reset(sampleRate, 0.1);
    smoothedMix.reset(sampleRate, 0.02);

    smoothedPreDelay.setCurrentAndTargetValue(predelayParam->load());
    smoothedReverbTime.setCurrentAndTargetValue(reverbTimeParam->load());
    smoothedSize.setCurrentAndTargetValue(sizeParam->load());
    smoothedMix.setCurrentAndTargetValue(mixParam->load());

    // Apply initial parameter values
    reverbEngine.setProgram(static_cast<int>(programParam->load()));
    reverbEngine.setPreDelay(predelayParam->load());
    reverbEngine.setReverbTime(reverbTimeParam->load());
    reverbEngine.setSize(sizeParam->load());
    reverbEngine.setPreEchoes(preEchoesParam->load() > 0.5f);
    reverbEngine.setDiffusion(static_cast<int>(diffusionParam->load()));
    reverbEngine.setRTContourLow(static_cast<int>(rtLowParam->load()));
    reverbEngine.setRTContourHigh(static_cast<int>(rtHighParam->load()));
    reverbEngine.setRolloff(static_cast<int>(rolloffParam->load()));
    reverbEngine.setMix(mixParam->load());

    lastProgram = static_cast<int>(programParam->load());
    lastDiffusion = static_cast<int>(diffusionParam->load());
    lastRTLow = static_cast<int>(rtLowParam->load());
    lastRTHigh = static_cast<int>(rtHighParam->load());
    lastRolloff = static_cast<int>(rolloffParam->load());
    lastPreEchoes = static_cast<int>(preEchoesParam->load() > 0.5f ? 1 : 0);
}

void Suede200Processor::releaseResources()
{
    reverbEngine.reset();
}

bool Suede200Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void Suede200Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Check discrete parameter changes
    int currentProgram = static_cast<int>(programParam->load());
    if (currentProgram != lastProgram)
    {
        reverbEngine.clearOptimizedCoefficients();
        reverbEngine.setProgram(currentProgram);
        lastProgram = currentProgram;
    }

    int currentDiffusion = static_cast<int>(diffusionParam->load());
    if (currentDiffusion != lastDiffusion)
    {
        reverbEngine.setDiffusion(currentDiffusion);
        lastDiffusion = currentDiffusion;
    }

    int currentRTLow = static_cast<int>(rtLowParam->load());
    if (currentRTLow != lastRTLow)
    {
        reverbEngine.setRTContourLow(currentRTLow);
        lastRTLow = currentRTLow;
    }

    int currentRTHigh = static_cast<int>(rtHighParam->load());
    if (currentRTHigh != lastRTHigh)
    {
        reverbEngine.setRTContourHigh(currentRTHigh);
        lastRTHigh = currentRTHigh;
    }

    int currentRolloff = static_cast<int>(rolloffParam->load());
    if (currentRolloff != lastRolloff)
    {
        reverbEngine.setRolloff(currentRolloff);
        lastRolloff = currentRolloff;
    }

    int currentPreEchoes = preEchoesParam->load() > 0.5f ? 1 : 0;
    if (currentPreEchoes != lastPreEchoes)
    {
        reverbEngine.setPreEchoes(currentPreEchoes > 0);
        lastPreEchoes = currentPreEchoes;
    }

    // Update smoothed parameters
    smoothedPreDelay.setTargetValue(predelayParam->load());
    smoothedReverbTime.setTargetValue(reverbTimeParam->load());
    smoothedSize.setTargetValue(sizeParam->load());
    smoothedMix.setTargetValue(mixParam->load());

    // Get channel pointers
    jassert(totalNumOutputChannels >= 2);
    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);
    auto* inputLeftChannel = buffer.getReadPointer(0);
    auto* inputRightChannel = buffer.getReadPointer(totalNumInputChannels > 1 ? 1 : 0);
    float peakL = 0.0f, peakR = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        // Update smoothed parameters
        if (smoothedPreDelay.isSmoothing())
            reverbEngine.setPreDelay(smoothedPreDelay.getNextValue());
        else
            smoothedPreDelay.skip(1);

        if (smoothedReverbTime.isSmoothing())
            reverbEngine.setReverbTime(smoothedReverbTime.getNextValue());
        else
            smoothedReverbTime.skip(1);

        if (smoothedSize.isSmoothing())
            reverbEngine.setSize(smoothedSize.getNextValue());
        else
            smoothedSize.skip(1);

        if (smoothedMix.isSmoothing())
            reverbEngine.setMix(smoothedMix.getNextValue());
        else
            smoothedMix.skip(1);

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

bool Suede200Processor::hasEditor() const { return true; }

juce::AudioProcessorEditor* Suede200Processor::createEditor()
{
    return new Suede200Editor(*this);
}

void Suede200Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void Suede200Processor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));

        // Snap bool parameters after state restore
        if (auto* p = apvts.getParameter("preechoes"))
        {
            float v = p->getValue();
            p->setValueNotifyingHost(v >= 0.5f ? 1.0f : 0.0f);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Suede200Processor();
}

#include "PluginProcessor.h"
#include "PluginEditor_Simple.h"  // Using simple editor temporarily
#include "DSP/PlateReverb.h"
#include "DSP/RoomReverb.h"
#include "DSP/HallReverb.h"
#include "DSP/EarlyReflections.h"

StudioReverbAudioProcessor::StudioReverbAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor(BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput("Input", juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
       apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Initialize reverb processors (only 4 types now)
    reverbProcessors[static_cast<int>(ReverbType::Room)] = std::make_unique<RoomReverbProcessor>();
    reverbProcessors[static_cast<int>(ReverbType::Hall)] = std::make_unique<HallReverbProcessor>();
    reverbProcessors[static_cast<int>(ReverbType::Plate)] = std::make_unique<PlateReverbProcessor>();
    reverbProcessors[static_cast<int>(ReverbType::EarlyReflections)] = std::make_unique<EarlyReflectionsProcessor>();

    // Get parameter pointers
    wetDryParam = apvts.getRawParameterValue(PARAM_WET_DRY);
    decayParam = apvts.getRawParameterValue(PARAM_DECAY);
    predelayParam = apvts.getRawParameterValue(PARAM_PREDELAY);
    dampingParam = apvts.getRawParameterValue(PARAM_DAMPING);
    roomSizeParam = apvts.getRawParameterValue(PARAM_ROOM_SIZE);
    diffusionParam = apvts.getRawParameterValue(PARAM_DIFFUSION);
    lowCutParam = apvts.getRawParameterValue(PARAM_LOW_CUT);
    highCutParam = apvts.getRawParameterValue(PARAM_HIGH_CUT);
    earlyMixParam = apvts.getRawParameterValue(PARAM_EARLY_MIX);
    lateMixParam = apvts.getRawParameterValue(PARAM_LATE_MIX);
    modulationParam = apvts.getRawParameterValue(PARAM_MODULATION);
    outputGainParam = apvts.getRawParameterValue(PARAM_OUTPUT_GAIN);

    // Add listener for reverb type changes
    apvts.addParameterListener(PARAM_REVERB_TYPE, this);
}

StudioReverbAudioProcessor::~StudioReverbAudioProcessor()
{
    apvts.removeParameterListener(PARAM_REVERB_TYPE, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
StudioReverbAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Reverb Type selector (only 4 types matching Dragonfly)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        PARAM_REVERB_TYPE, "Reverb Type",
        juce::StringArray{"Room", "Hall", "Plate", "Early Reflections"},
        0)); // Default to Room

    // Global parameters (always visible)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_WET_DRY, "Wet/Dry Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_OUTPUT_GAIN, "Output Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " dB"; }));

    // Type-specific parameters
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_DECAY, "Decay Time",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.5f), 2.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " s"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_PREDELAY, "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 10.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ms"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_DAMPING, "Damping",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_ROOM_SIZE, "Room Size",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_DIFFUSION, "Diffusion",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_LOW_CUT, "Low Cut",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f), 20.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 0) + " Hz"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_HIGH_CUT, "High Cut",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 10.0f, 0.5f), 16000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            if (value >= 1000.0f)
                return juce::String(value / 1000.0f, 1) + " kHz";
            else
                return juce::String(value, 0) + " Hz";
        }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_EARLY_MIX, "Early Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_LATE_MIX, "Late Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PARAM_MODULATION, "Modulation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 20.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    return layout;
}

void StudioReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Validate input parameters
    if (sampleRate <= 0.0 || samplesPerBlock <= 0)
        return;

    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // Prepare all reverb processors
    for (auto& processor : reverbProcessors)
    {
        if (processor)
            processor->prepare(sampleRate, samplesPerBlock);
    }

    // Prepare wet buffer
    wetBuffer.setSize(2, samplesPerBlock);
    wetBuffer.clear();

    // Initialize smoothers
    wetDrySmoothed.reset(sampleRate, 0.05); // 50ms smoothing
    outputGainSmoothed.reset(sampleRate, 0.05);

    // Set initial values for smoothers
    wetDrySmoothed.setCurrentAndTargetValue(50.0f);
    outputGainSmoothed.setCurrentAndTargetValue(1.0f);

    updateReverbParameters();
}

void StudioReverbAudioProcessor::releaseResources()
{
    for (auto& processor : reverbProcessors)
    {
        if (processor)
            processor->reset();
    }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool StudioReverbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    #endif

    return true;
}
#endif

void StudioReverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                        juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Validate buffer
    if (buffer.getNumSamples() <= 0)
        return;

    // Clear unused channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2)
        return;

    // Check if we're properly initialized
    if (currentSampleRate <= 0.0 || currentBlockSize <= 0)
        return;

    // Update parameters
    updateReverbParameters();

    // Get current reverb processor
    auto& processor = reverbProcessors[static_cast<int>(currentReverbType)];
    if (!processor)
        return;

    const int numSamples = buffer.getNumSamples();

    // Copy dry signal to wet buffer
    wetBuffer.setSize(buffer.getNumChannels(), numSamples, false, false, true);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        wetBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // Process reverb
    processor->process(wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1), numSamples);

    // Apply wet/dry mix with smoothing (check for null pointers)
    if (wetDryParam)
        wetDrySmoothed.setTargetValue(wetDryParam->load());
    else
        wetDrySmoothed.setTargetValue(50.0f); // Default 50% mix

    if (outputGainParam)
        outputGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(outputGainParam->load()));
    else
        outputGainSmoothed.setTargetValue(1.0f); // Default 0dB gain

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float wetAmount = wetDrySmoothed.getNextValue() * 0.01f;
        float dryAmount = 1.0f - wetAmount;
        float outputGain = outputGainSmoothed.getNextValue();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float drySample = buffer.getSample(ch, sample);
            float wetSample = wetBuffer.getSample(ch, sample);
            float mixedSample = (drySample * dryAmount + wetSample * wetAmount) * outputGain;

            buffer.setSample(ch, sample, mixedSample);
        }
    }
}

double StudioReverbAudioProcessor::getTailLengthSeconds() const
{
    // Return the maximum tail length from all reverb types
    double maxTail = 0.0;
    for (const auto& processor : reverbProcessors)
    {
        if (processor)
            maxTail = std::max(maxTail, processor->getTailLength());
    }
    return maxTail;
}

void StudioReverbAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void StudioReverbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

ReverbType StudioReverbAudioProcessor::getCurrentReverbType() const
{
    return currentReverbType;
}

void StudioReverbAudioProcessor::setReverbType(ReverbType type)
{
    if (type != currentReverbType && type < ReverbType::NumTypes)
    {
        switchReverbType(type);
    }
}

void StudioReverbAudioProcessor::switchReverbType(ReverbType newType)
{
    // Reset old processor
    if (reverbProcessors[static_cast<int>(currentReverbType)])
        reverbProcessors[static_cast<int>(currentReverbType)]->reset();

    currentReverbType = newType;

    // Initialize new processor if needed
    int typeIndex = static_cast<int>(newType);
    // All processors are now created in the constructor, so this should never happen
    if (typeIndex < reverbProcessors.size() && !reverbProcessors[typeIndex])
    {
        // This shouldn't happen as all 4 types are initialized in constructor
        return;
    }

    updateReverbParameters();
}

void StudioReverbAudioProcessor::updateReverbParameters()
{
    auto& processor = reverbProcessors[static_cast<int>(currentReverbType)];
    if (!processor)
        return;

    // Update common parameters (check for null pointers)
    if (decayParam) processor->setDecay(decayParam->load());
    if (predelayParam) processor->setPreDelay(predelayParam->load());
    if (dampingParam) processor->setDamping(dampingParam->load() / 100.0f);
    if (diffusionParam) processor->setDiffusion(diffusionParam->load() / 100.0f);

    // Update type-specific parameters (check for null pointers)
    switch (currentReverbType)
    {
        case ReverbType::Room:
            if (roomSizeParam) processor->setRoomSize(roomSizeParam->load() / 100.0f);
            if (earlyMixParam) processor->setEarlyMix(earlyMixParam->load() / 100.0f);
            if (lateMixParam) processor->setLateMix(lateMixParam->load() / 100.0f);
            break;

        case ReverbType::Hall:
            if (roomSizeParam) processor->setRoomSize(roomSizeParam->load() / 100.0f);
            if (modulationParam) processor->setModulation(modulationParam->load() / 100.0f);
            break;

        case ReverbType::Plate:
            if (modulationParam) processor->setModulation(modulationParam->load() / 100.0f);
            break;

        default:
            break;
    }

    // Update filters
    processor->setLowCut(*lowCutParam);
    processor->setHighCut(*highCutParam);
}

// Parameter listener callback
void StudioReverbAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == PARAM_REVERB_TYPE)
    {
        setReverbType(static_cast<ReverbType>(static_cast<int>(newValue)));
    }
}

juce::AudioProcessorEditor* StudioReverbAudioProcessor::createEditor()
{
    return new StudioReverbAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StudioReverbAudioProcessor();
}
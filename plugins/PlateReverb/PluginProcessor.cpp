/*
  ==============================================================================

    Plate Reverb - Plugin Processor
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PlateReverbAudioProcessor::PlateReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("PlateReverb"),
                 {
                     std::make_unique<juce::AudioParameterFloat>(
                         "size",
                         "Size",
                         juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
                         0.5f),
                     std::make_unique<juce::AudioParameterFloat>(
                         "decay",
                         "Decay",
                         juce::NormalisableRange<float>(0.0f, 0.99f, 0.01f),
                         0.5f),
                     std::make_unique<juce::AudioParameterFloat>(
                         "damping",
                         "Damping",
                         juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
                         0.5f),
                     std::make_unique<juce::AudioParameterFloat>(
                         "predelay",
                         "Predelay",
                         juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f),
                         0.0f),
                     std::make_unique<juce::AudioParameterFloat>(
                         "width",
                         "Width",
                         juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
                         1.0f),
                     std::make_unique<juce::AudioParameterFloat>(
                         "mix",
                         "Mix",
                         juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
                         0.5f),
                 })
{
    // Get parameter pointers
    sizeParam = parameters.getRawParameterValue("size");
    decayParam = parameters.getRawParameterValue("decay");
    dampingParam = parameters.getRawParameterValue("damping");
    predelayParam = parameters.getRawParameterValue("predelay");
    widthParam = parameters.getRawParameterValue("width");
    mixParam = parameters.getRawParameterValue("mix");
}

PlateReverbAudioProcessor::~PlateReverbAudioProcessor()
{
}

//==============================================================================
const juce::String PlateReverbAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PlateReverbAudioProcessor::acceptsMidi() const
{
    return false;
}

bool PlateReverbAudioProcessor::producesMidi() const
{
    return false;
}

bool PlateReverbAudioProcessor::isMidiEffect() const
{
    return false;
}

double PlateReverbAudioProcessor::getTailLengthSeconds() const
{
    return 10.0;
}

int PlateReverbAudioProcessor::getNumPrograms()
{
    return 1;
}

int PlateReverbAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PlateReverbAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String PlateReverbAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void PlateReverbAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void PlateReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    reverbLeft.prepare(sampleRate, samplesPerBlock);
    reverbRight.prepare(sampleRate, samplesPerBlock);
}

void PlateReverbAudioProcessor::releaseResources()
{
}

bool PlateReverbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PlateReverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Get parameters
    float size = sizeParam->load();
    float decay = decayParam->load();
    float damping = dampingParam->load();
    float predelay = predelayParam->load();
    float width = widthParam->load();
    float mix = mixParam->load();

    // Process samples
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float inL = totalNumInputChannels > 0 ? buffer.getSample(0, sample) : 0.0f;
        float inR = totalNumInputChannels > 1 ? buffer.getSample(1, sample) : inL;

        float wetL, wetR;

        // Process reverb
        reverbLeft.process(inL, inR, wetL, wetR, size, decay, damping, predelay, width);

        // Mix wet and dry
        float outL = inL * (1.0f - mix) + wetL * mix;
        float outR = inR * (1.0f - mix) + wetR * mix;

        // Write outputs
        if (totalNumOutputChannels > 0)
            buffer.setSample(0, sample, outL);
        if (totalNumOutputChannels > 1)
            buffer.setSample(1, sample, outR);
    }
}

//==============================================================================
bool PlateReverbAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PlateReverbAudioProcessor::createEditor()
{
    return new PlateReverbAudioProcessorEditor(*this);
}

//==============================================================================
void PlateReverbAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PlateReverbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PlateReverbAudioProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
StudioReverbAudioProcessor::StudioReverbAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    // Initialize parameters with safe defaults
    roomSize = new juce::AudioParameterFloat(
        "roomSize", "Room Size",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; });

    damping = new juce::AudioParameterFloat(
        "damping", "Damping",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; });

    wetLevel = new juce::AudioParameterFloat(
        "wetLevel", "Wet Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; });

    dryLevel = new juce::AudioParameterFloat(
        "dryLevel", "Dry Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; });

    width = new juce::AudioParameterFloat(
        "width", "Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; });

    addParameter(roomSize);
    addParameter(damping);
    addParameter(wetLevel);
    addParameter(dryLevel);
    addParameter(width);

    reverb = std::make_unique<SimpleFreeverb>();
}

StudioReverbAudioProcessor::~StudioReverbAudioProcessor()
{
}

//==============================================================================
const juce::String StudioReverbAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool StudioReverbAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool StudioReverbAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool StudioReverbAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double StudioReverbAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int StudioReverbAudioProcessor::getNumPrograms()
{
    return 1;
}

int StudioReverbAudioProcessor::getCurrentProgram()
{
    return 0;
}

void StudioReverbAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String StudioReverbAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void StudioReverbAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void StudioReverbAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    reverb->prepare(sampleRate, samplesPerBlock);
    updateReverbParameters();
}

void StudioReverbAudioProcessor::releaseResources()
{
    reverb->reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool StudioReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void StudioReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    updateReverbParameters();
    reverb->processBlock(buffer);
}

void StudioReverbAudioProcessor::updateReverbParameters()
{
    reverb->setRoomSize(roomSize->get() / 100.0f);
    reverb->setDamping(damping->get() / 100.0f);
    reverb->setWetLevel(wetLevel->get() / 100.0f);
    reverb->setDryLevel(dryLevel->get() / 100.0f);
    reverb->setWidth(width->get() / 100.0f);
}

//==============================================================================
bool StudioReverbAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* StudioReverbAudioProcessor::createEditor()
{
    return new StudioReverbAudioProcessorEditor (*this);
}

//==============================================================================
void StudioReverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = getParameters();
    std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("StudioReverbState"));

    for (auto* param : state)
    {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
        {
            xml->setAttribute(p->paramID, p->getValue());
        }
    }

    copyXmlToBinary(*xml, destData);
}

void StudioReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName("StudioReverbState"))
        {
            auto state = getParameters();
            for (auto* param : state)
            {
                if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
                {
                    float value = (float) xmlState->getDoubleAttribute(p->paramID, p->getValue());
                    p->setValueNotifyingHost(value);
                }
            }
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StudioReverbAudioProcessor();
}
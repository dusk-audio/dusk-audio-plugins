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
    reverbType = new juce::AudioParameterChoice(
        "reverbType", "Reverb Type",
        juce::StringArray{"Room", "Hall", "Plate", "Early Reflections"},
        1); // Default to Hall

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

    preDelay = new juce::AudioParameterFloat(
        "preDelay", "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ms"; });

    decayTime = new juce::AudioParameterFloat(
        "decayTime", "Decay Time",
        juce::NormalisableRange<float>(0.1f, 30.0f, 0.01f), 2.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " s"; });  // 2 decimal places

    diffusion = new juce::AudioParameterFloat(
        "diffusion", "Diffusion",
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

    addParameter(reverbType);
    addParameter(roomSize);
    addParameter(damping);
    addParameter(preDelay);
    addParameter(decayTime);
    addParameter(diffusion);
    addParameter(wetLevel);
    addParameter(dryLevel);
    addParameter(width);

    reverb = std::make_unique<Freeverb3Reverb>();
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
    // Set reverb type
    reverb->setReverbType(static_cast<Freeverb3Reverb::ReverbType>(reverbType->getIndex()));

    // Set parameters
    reverb->setSize(roomSize->get() / 100.0f * 50.0f + 10.0f);  // Map 0-100% to 10-60 meters
    reverb->setPreDelay(preDelay->get());
    reverb->setDecay(decayTime->get());
    reverb->setDiffuse(diffusion->get() / 100.0f);
    // Set levels matching Dragonfly's mixing model
    float wet = wetLevel->get() / 100.0f;
    float dry = dryLevel->get() / 100.0f;
    reverb->setDryLevel(dry);
    reverb->setEarlyLevel(wet * 0.4f);  // Early reflections level
    reverb->setLateLevel(wet * 0.6f);   // Late reverb level
    reverb->setWidth(width->get() / 100.0f);

    // Set tone controls matching Dragonfly defaults
    reverb->setLowCut(50.0f);
    reverb->setHighCut(15000.0f);
    reverb->setLowCrossover(200.0f);
    reverb->setHighCrossover(2000.0f);
    reverb->setLowMult(1.0f);
    reverb->setHighMult(1.0f - damping->get() / 100.0f * 0.5f);  // Map damping to high freq multiplier

    // Set modulation
    reverb->setModAmount(0.1f);
    reverb->setModSpeed(0.5f);
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
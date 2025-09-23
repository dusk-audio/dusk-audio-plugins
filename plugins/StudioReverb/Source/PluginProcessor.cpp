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
       , apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Get parameter pointers from APVTS
    reverbType = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("reverbType"));
    roomSize = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("roomSize"));
    damping = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("damping"));
    preDelay = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("preDelay"));
    decayTime = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("decayTime"));
    diffusion = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("diffusion"));
    wetLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("wetLevel"));
    dryLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("dryLevel"));
    width = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("width"));

    // Add parameter listeners for change detection
    apvts.addParameterListener("reverbType", this);
    apvts.addParameterListener("roomSize", this);
    apvts.addParameterListener("damping", this);
    apvts.addParameterListener("preDelay", this);
    apvts.addParameterListener("decayTime", this);
    apvts.addParameterListener("diffusion", this);
    apvts.addParameterListener("wetLevel", this);
    apvts.addParameterListener("dryLevel", this);
    apvts.addParameterListener("width", this);

    reverb = std::make_unique<DragonflyReverb>();
}

StudioReverbAudioProcessor::~StudioReverbAudioProcessor()
{
    // Remove parameter listeners
    apvts.removeParameterListener("reverbType", this);
    apvts.removeParameterListener("roomSize", this);
    apvts.removeParameterListener("damping", this);
    apvts.removeParameterListener("preDelay", this);
    apvts.removeParameterListener("decayTime", this);
    apvts.removeParameterListener("diffusion", this);
    apvts.removeParameterListener("wetLevel", this);
    apvts.removeParameterListener("dryLevel", this);
    apvts.removeParameterListener("width", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout StudioReverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "reverbType", "Reverb Type",
        juce::StringArray{"Room", "Hall", "Plate", "Early Reflections"},
        1)); // Default to Hall

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "roomSize", "Room Size",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "damping", "Damping",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preDelay", "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ms"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decayTime", "Decay Time",
        juce::NormalisableRange<float>(0.1f, 30.0f, 0.01f), 2.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " s"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "diffusion", "Diffusion",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wetLevel", "Wet Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dryLevel", "Dry Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "width", "Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    return { params.begin(), params.end() };
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
    // Return the maximum possible reverb tail (decay time + predelay)
    return decayTime->get() + (preDelay->get() / 1000.0);
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

    // Only update parameters if they've changed
    if (parametersChanged.exchange(false))
        updateReverbParameters();

    reverb->processBlock(buffer);
}

void StudioReverbAudioProcessor::updateReverbParameters()
{
    // Set reverb algorithm
    reverb->setAlgorithm(static_cast<DragonflyReverb::Algorithm>(reverbType->getIndex()));

    // Set core parameters (matching Dragonfly's exact parameter scaling)
    reverb->setSize(roomSize->get() / 100.0f * 50.0f + 10.0f);  // Map 0-100% to 10-60 meters
    reverb->setPreDelay(preDelay->get());
    reverb->setDecay(decayTime->get());
    reverb->setDiffuse(diffusion->get());  // Already in 0-100% range
    reverb->setWidth(width->get());        // Already in 0-100% range

    // Set mix levels
    float wet = wetLevel->get() / 100.0f;
    float dry = dryLevel->get() / 100.0f;
    reverb->setDryLevel(dry);
    reverb->setEarlyLevel(wet * 0.4f);     // 40% of wet goes to early reflections
    reverb->setLateLevel(wet * 0.6f);      // 60% of wet goes to late reverb
    reverb->setEarlySend(0.2f);            // Default early->late send

    // Set tone controls
    reverb->setLowCut(50.0f);              // Default high-pass
    reverb->setHighCut(15000.0f);          // Default low-pass
    reverb->setLowCrossover(200.0f);       // Low frequency crossover
    reverb->setHighCrossover(2000.0f);     // High frequency crossover
    reverb->setLowMult(1.0f);              // Full low frequency decay
    reverb->setHighMult(1.0f - damping->get() / 200.0f);  // Map damping to HF decay

    // Set modulation (for Hall algorithm)
    reverb->setSpin(0.5f);                 // Default modulation speed
    reverb->setWander(0.1f);               // Default modulation depth
}

void StudioReverbAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(parameterID, newValue);
    parametersChanged = true;
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
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void StudioReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StudioReverbAudioProcessor();
}
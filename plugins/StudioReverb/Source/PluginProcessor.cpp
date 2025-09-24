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

    // Mix controls - separate dry and wet
    dryLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("dryLevel"));
    wetLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("wetLevel"));
    earlyLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("earlyLevel"));
    earlySend = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("earlySend"));
    lateLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lateLevel"));

    // Basic parameters
    size = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("size"));
    width = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("width"));
    preDelay = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("preDelay"));
    decay = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("decay"));
    diffuse = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("diffuse"));

    // Modulation
    spin = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("spin"));
    wander = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("wander"));
    modulation = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("modulation"));

    // Filters
    highCut = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("highCut"));
    lowCut = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lowCut"));
    dampen = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("dampen"));
    earlyDamp = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("earlyDamp"));
    lateDamp = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lateDamp"));

    // Room-specific boost
    lowBoost = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lowBoost"));
    boostFreq = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("boostFreq"));

    // Hall-specific
    lowCross = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lowCross"));
    highCross = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("highCross"));
    lowMult = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lowMult"));
    highMult = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("highMult"));

    // Add parameter listeners for all parameters
    for (const auto& id : getParameterIDs())
    {
        apvts.addParameterListener(id, this);
    }

    reverb = std::make_unique<DragonflyReverb>();
}

StudioReverbAudioProcessor::~StudioReverbAudioProcessor()
{
    // Remove all parameter listeners
    for (const auto& id : getParameterIDs())
    {
        apvts.removeParameterListener(id, this);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout StudioReverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Algorithm selection
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "reverbType", "Reverb Type",
        juce::StringArray{"Room", "Hall", "Plate", "Early Reflections"},
        1)); // Default to Hall

    // === Mix Controls - Separate Dry and Wet for better control ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dryLevel", "Dry Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wetLevel", "Wet Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    // === Internal Mix Controls ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "earlyLevel", "Early Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 20.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "earlySend", "Early Send",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 20.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lateLevel", "Late Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    // === Basic Reverb Parameters ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "size", "Size",
        juce::NormalisableRange<float>(10.0f, 60.0f, 0.1f), 30.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " m"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "width", "Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preDelay", "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ms"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decay", "Decay",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 2.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " s"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "diffuse", "Diffuse",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    // === Modulation Controls ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "spin", "Spin",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 0.5f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wander", "Wander",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.1f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " ms"; }));

    // Hall-specific modulation
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modulation", "Modulation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    // === Filter Controls ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highCut", "High Cut",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f), 16000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowCut", "Low Cut",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    // Plate-specific damping control
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dampen", "Dampen",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f), 10000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    // Room-specific damping controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "earlyDamp", "Early Damp",
        juce::NormalisableRange<float>(1000.0f, 16000.0f, 1.0f), 10000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lateDamp", "Late Damp",
        juce::NormalisableRange<float>(1000.0f, 16000.0f, 1.0f), 9000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    // Room-specific boost controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowBoost", "Low Boost",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 0) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "boostFreq", "Boost Freq",
        juce::NormalisableRange<float>(50.0f, 4000.0f, 1.0f), 600.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    // === Hall-specific Crossover Controls ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowCross", "Low Cross",
        juce::NormalisableRange<float>(50.0f, 1000.0f, 1.0f), 200.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highCross", "High Cross",
        juce::NormalisableRange<float>(1000.0f, 10000.0f, 1.0f), 3000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowMult", "Low Mult",
        juce::NormalisableRange<float>(0.1f, 2.0f, 0.01f), 1.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + "x"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highMult", "High Mult",
        juce::NormalisableRange<float>(0.1f, 2.0f, 0.01f), 0.8f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + "x"; }));

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
    // Add null checks to prevent crashes if parameters aren't initialized
    double decayValue = (decay != nullptr) ? decay->get() : 0.0;
    double preDelayValue = (preDelay != nullptr) ? preDelay->get() : 0.0;
    return decayValue + (preDelayValue / 1000.0);
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
    if (reverb)
    {
        reverb->prepare(sampleRate, samplesPerBlock);
        reverb->reset();  // Ensure clean state after prepare
        updateReverbParameters();
    }
}

void StudioReverbAudioProcessor::releaseResources()
{
    if (reverb)
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

    if (reverb)
        reverb->processBlock(buffer);
}

void StudioReverbAudioProcessor::updateReverbParameters()
{
    if (!reverb)
        return;

    // Set reverb algorithm
    int algIndex = reverbType ? reverbType->getIndex() : 0;
    DBG("updateReverbParameters - Setting algorithm to: " << algIndex
        << " (" << reverbType->getCurrentChoiceName() << ")");
    reverb->setAlgorithm(static_cast<DragonflyReverb::Algorithm>(algIndex));

    // Set core parameters with null checks
    if (size) reverb->setSize(size->get());
    if (preDelay) reverb->setPreDelay(preDelay->get());
    if (decay) reverb->setDecay(decay->get());
    if (diffuse) reverb->setDiffuse(diffuse->get());
    if (width) reverb->setWidth(width->get());

    // Get separate dry and wet levels
    float dryPercent = dryLevel ? dryLevel->get() : 100.0f;  // 0-100%
    float wetPercent = wetLevel ? wetLevel->get() : 30.0f;   // 0-100%

    // algIndex already declared above
    // 0=Room, 1=Hall, 2=Plate, 3=Early Reflections

    // Set mix levels - these are percentages expected by DragonflyReverb
    reverb->setDryLevel(dryPercent);             // Dry signal percentage

    // Room and Hall algorithms expose early level and early send in UI
    if (algIndex == 0 || algIndex == 1)  // Room or Hall
    {
        // Use the actual early parameters from UI
        float earlyPercent = earlyLevel ? earlyLevel->get() : 10.0f;
        float sendPercent = earlySend ? earlySend->get() : 20.0f;
        reverb->setEarlyLevel(earlyPercent);     // User-controlled early reflections
        reverb->setEarlySend(sendPercent);       // User-controlled early send
        reverb->setLateLevel(wetPercent);        // Late reverb controlled by wet/dry
    }
    else if (algIndex == 2)  // Plate
    {
        // Dragonfly Plate has NO early reflections - it's a pure plate algorithm
        reverb->setEarlyLevel(0.0f);             // No early reflections
        reverb->setEarlySend(0.0f);              // No early send
        reverb->setLateLevel(wetPercent);        // Pure plate reverb only
    }
    else if (algIndex == 3)  // Early Reflections
    {
        // Early Reflections only - no late reverb
        reverb->setEarlyLevel(wetPercent);       // Wet signal controls early reflections
        reverb->setEarlySend(0.0f);              // No send to late (no late reverb)
        reverb->setLateLevel(0.0f);              // No late reverb
    }

    // Set filter controls with null checks
    if (lowCut) reverb->setLowCut(lowCut->get());
    if (highCut) reverb->setHighCut(highCut->get());

    // Mode-specific parameter handling
    if (algIndex == 0)  // Room
    {
        // Room-specific modulation
        if (spin) reverb->setSpin(spin->get());
        if (wander) reverb->setWander(wander->get());

        // Room-specific damping - only set if parameters exist and are valid
        if (earlyDamp && earlyDamp->get() > 0.0f)
            reverb->setEarlyDamp(earlyDamp->get());
        if (lateDamp && lateDamp->get() > 0.0f)
            reverb->setLateDamp(lateDamp->get());

        // Room-specific boost controls - only set if valid
        if (lowBoost && lowBoost->get() >= 0.0f)
            reverb->setLowBoost(lowBoost->get());
        if (boostFreq && boostFreq->get() > 0.0f)
            reverb->setBoostFreq(boostFreq->get());
    }
    else if (algIndex == 1)  // Hall
    {
        // Hall-specific modulation
        if (spin) reverb->setSpin(spin->get());
        if (wander) reverb->setWander(wander->get());
        if (modulation && modulation->get() >= 0.0f)
            reverb->setModulation(modulation->get());

        // Hall-specific crossover controls
        if (lowCross) reverb->setLowCrossover(lowCross->get());
        if (highCross) reverb->setHighCrossover(highCross->get());
        if (lowMult) reverb->setLowMult(lowMult->get());
        if (highMult) reverb->setHighMult(highMult->get());
    }
    else if (algIndex == 2)  // Plate
    {
        // Plate-specific damping - only set if valid
        if (dampen && dampen->get() > 0.0f)
            reverb->setDamping(dampen->get());
    }
    // Early Reflections (algIndex == 3) doesn't have extra parameters
}

void StudioReverbAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);

    DBG("Parameter changed: " << parameterID);
    if (parameterID == "reverbType")
    {
        DBG("  Reverb type changed to index: " << reverbType->getIndex()
            << " (" << reverbType->getCurrentChoiceName() << ")");
    }

    parametersChanged = true;
}

void StudioReverbAudioProcessor::loadPreset(const juce::String& presetName)
{
    // Use the parameter's current algorithm index
    int algorithmIndex = reverbType ? reverbType->getIndex() : 0;
    loadPresetForAlgorithm(presetName, algorithmIndex);
}

void StudioReverbAudioProcessor::loadPresetForAlgorithm(const juce::String& presetName, int algorithmIndex)
{
    DBG("StudioReverbAudioProcessor::loadPresetForAlgorithm called with: " << presetName
        << " for algorithm " << algorithmIndex);

    if (presetName == "-- Select Preset --" || presetName.isEmpty())
    {
        DBG("  Ignoring preset selection header");
        return;
    }

    DBG("  Loading preset for algorithm index: " << algorithmIndex);

    auto preset = presetManager.getPreset(algorithmIndex, presetName);

    if (preset.name.isEmpty())
    {
        DBG("  ERROR: Preset not found!");
        return;
    }

    DBG("  Found preset: " << preset.name << " with " << preset.parameters.size() << " parameters");

    // Load preset parameters
    for (const auto& param : preset.parameters)
    {
        // Use the AudioParameterFloat directly for proper value conversion
        if (param.first == "dryLevel" && dryLevel)
            dryLevel->setValueNotifyingHost(dryLevel->convertTo0to1(param.second));
        else if (param.first == "earlyLevel" && earlyLevel)
            earlyLevel->setValueNotifyingHost(earlyLevel->convertTo0to1(param.second));
        else if (param.first == "earlySend" && earlySend)
            earlySend->setValueNotifyingHost(earlySend->convertTo0to1(param.second));
        else if (param.first == "lateLevel" && lateLevel)
            lateLevel->setValueNotifyingHost(lateLevel->convertTo0to1(param.second));
        else if (param.first == "size" && size) {
            DBG("  Setting size to " << param.second << " (normalized: " << size->convertTo0to1(param.second) << ")");
            size->setValueNotifyingHost(size->convertTo0to1(param.second));
            DBG("  After setting, size value is: " << size->get());
        }
        else if (param.first == "width" && width)
            width->setValueNotifyingHost(width->convertTo0to1(param.second));
        else if (param.first == "preDelay" && preDelay)
            preDelay->setValueNotifyingHost(preDelay->convertTo0to1(param.second));
        else if (param.first == "decay" && decay)
            decay->setValueNotifyingHost(decay->convertTo0to1(param.second));
        else if (param.first == "diffuse" && diffuse)
            diffuse->setValueNotifyingHost(diffuse->convertTo0to1(param.second));
        else if (param.first == "spin" && spin)
            spin->setValueNotifyingHost(spin->convertTo0to1(param.second));
        else if (param.first == "wander" && wander)
            wander->setValueNotifyingHost(wander->convertTo0to1(param.second));
        else if (param.first == "highCut" && highCut)
            highCut->setValueNotifyingHost(highCut->convertTo0to1(param.second));
        else if (param.first == "lowCut" && lowCut)
            lowCut->setValueNotifyingHost(lowCut->convertTo0to1(param.second));
        else if (param.first == "lowCross" && lowCross)
            lowCross->setValueNotifyingHost(lowCross->convertTo0to1(param.second));
        else if (param.first == "highCross" && highCross)
            highCross->setValueNotifyingHost(highCross->convertTo0to1(param.second));
        else if (param.first == "lowMult" && lowMult)
            lowMult->setValueNotifyingHost(lowMult->convertTo0to1(param.second));
        else if (param.first == "highMult" && highMult)
            highMult->setValueNotifyingHost(highMult->convertTo0to1(param.second));
    }

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
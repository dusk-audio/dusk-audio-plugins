/*
  ==============================================================================

    VintageVerb - PluginProcessor Implementation

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
VintageVerbAudioProcessor::VintageVerbAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       parameters (*this, nullptr, juce::Identifier ("VintageVerb"), createParameterLayout())
{
    // Create DSP components
    simpleReverb = std::make_unique<SimpleReverbEngine>();
    engineA = std::make_unique<ReverbEngine>();
    engineB = std::make_unique<ReverbEngine>();
    vintageProcessor = std::make_unique<VintageColoration>();
    router = std::make_unique<DualEngineRouter>();

    // Connect parameter pointers
    mixParam = parameters.getRawParameterValue("mix");
    sizeParam = parameters.getRawParameterValue("size");
    attackParam = parameters.getRawParameterValue("attack");
    dampingParam = parameters.getRawParameterValue("damping");
    predelayParam = parameters.getRawParameterValue("predelay");
    widthParam = parameters.getRawParameterValue("width");
    modulationParam = parameters.getRawParameterValue("modulation");
    bassFreqParam = parameters.getRawParameterValue("bassFreq");
    bassMulParam = parameters.getRawParameterValue("bassMul");
    highFreqParam = parameters.getRawParameterValue("highFreq");
    highMulParam = parameters.getRawParameterValue("highMul");
    densityParam = parameters.getRawParameterValue("density");
    diffusionParam = parameters.getRawParameterValue("diffusion");
    shapeParam = parameters.getRawParameterValue("shape");
    spreadParam = parameters.getRawParameterValue("spread");
    reverbModeParam = parameters.getRawParameterValue("reverbMode");
    colorModeParam = parameters.getRawParameterValue("colorMode");
    routingModeParam = parameters.getRawParameterValue("routingMode");
    engineMixParam = parameters.getRawParameterValue("engineMix");
    crossFeedParam = parameters.getRawParameterValue("crossFeed");
    seriesBlendParam = parameters.getRawParameterValue("seriesBlend");
    vintageIntensityParam = parameters.getRawParameterValue("vintageIntensity");
    hpfFreqParam = parameters.getRawParameterValue("hpfFreq");
    lpfFreqParam = parameters.getRawParameterValue("lpfFreq");
    tiltGainParam = parameters.getRawParameterValue("tiltGain");
    inputGainParam = parameters.getRawParameterValue("inputGain");
    outputGainParam = parameters.getRawParameterValue("outputGain");

    // Add parameter listeners
    parameters.addParameterListener("reverbMode", this);
    parameters.addParameterListener("colorMode", this);
    parameters.addParameterListener("routingMode", this);

    // Initialize presets
    presetManager.initializeFactoryPresets();
}

VintageVerbAudioProcessor::~VintageVerbAudioProcessor()
{
    parameters.removeParameterListener("reverbMode", this);
    parameters.removeParameterListener("colorMode", this);
    parameters.removeParameterListener("routingMode", this);
}

//==============================================================================
const juce::String VintageVerbAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool VintageVerbAudioProcessor::acceptsMidi() const
{
    return false;
}

bool VintageVerbAudioProcessor::producesMidi() const
{
    return false;
}

bool VintageVerbAudioProcessor::isMidiEffect() const
{
    return false;
}

double VintageVerbAudioProcessor::getTailLengthSeconds() const
{
    return 10.0;  // Maximum reverb tail
}

int VintageVerbAudioProcessor::getNumPrograms()
{
    return presetManager.getNumPresets();
}

int VintageVerbAudioProcessor::getCurrentProgram()
{
    return 0;  // Not implemented
}

void VintageVerbAudioProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < presetManager.getNumPresets())
    {
        presetManager.applyPresetByIndex(index, parameters);
    }
}

const juce::String VintageVerbAudioProcessor::getProgramName (int index)
{
    if (auto* preset = presetManager.getPreset(index))
        return preset->name;
    return {};
}

void VintageVerbAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    // Not implemented - factory presets are read-only
}

//==============================================================================
void VintageVerbAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // Prepare DSP components
    simpleReverb->prepare(sampleRate, samplesPerBlock);
    engineA->prepare(sampleRate, samplesPerBlock);
    engineB->prepare(sampleRate, samplesPerBlock);
    vintageProcessor->prepare(sampleRate, samplesPerBlock);
    router->prepare(sampleRate, samplesPerBlock);

    // Set router engines
    router->setEngines(engineA.get(), engineB.get());

    // Prepare filters
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    highpassFilter.prepare(spec);
    lowpassFilter.prepare(spec);

    for (auto& eq : tiltEQ)
    {
        eq.prepare(spec);
    }

    // Prepare predelay
    predelayLeft.prepare(spec);
    predelayRight.prepare(spec);
    predelayLeft.setMaximumDelayInSamples(192000);
    predelayRight.setMaximumDelayInSamples(192000);

    // Prepare modulation LFOs
    modLFO1.prepare(spec);
    modLFO1.initialise([](float x) { return std::sin(x); });
    modLFO1.setFrequency(0.3f);

    modLFO2.prepare(spec);
    modLFO2.initialise([](float x) { return std::sin(x); });
    modLFO2.setFrequency(0.7f);

    // Apply initial parameter values
    updateReverbParameters();
    updateFilterParameters();
}

void VintageVerbAudioProcessor::releaseResources()
{
    engineA->reset();
    engineB->reset();
    highpassFilter.reset();
    lowpassFilter.reset();
    predelayLeft.reset();
    predelayRight.reset();
}

bool VintageVerbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Only supports stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void VintageVerbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (totalNumInputChannels < 2)
        return;

    const int numSamples = buffer.getNumSamples();

    // Update reverb parameters
    updateReverbParameters();
    updateFilterParameters();

    // Apply input gain
    float inputGain = juce::Decibels::decibelsToGain(inputGainParam->load());
    buffer.applyGain(inputGain);

    // Update input level meters
    inputLevelL = buffer.getRMSLevel(0, 0, numSamples);
    inputLevelR = buffer.getRMSLevel(1, 0, numSamples);

    // Apply pre-delay
    float predelayMs = predelayParam->load();
    int predelaySamples = static_cast<int>(predelayMs * currentSampleRate * 0.001f);
    predelayLeft.setDelay(static_cast<float>(predelaySamples));
    predelayRight.setDelay(static_cast<float>(predelaySamples));

    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);

    for (int i = 0; i < numSamples; ++i)
    {
        predelayLeft.pushSample(0, leftChannel[i]);
        predelayRight.pushSample(0, rightChannel[i]);
        leftChannel[i] = predelayLeft.popSample(0);
        rightChannel[i] = predelayRight.popSample(0);
    }

    // Apply high-pass filter
    highpassFilter.setCutoffFrequency(hpfFreqParam->load());
    highpassFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);

    highpassFilter.process(context);

    // Create dry buffer for mixing
    juce::AudioBuffer<float> dryBuffer(buffer.getNumChannels(), numSamples);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // Use simple reverb instead of the complex router for now
    // Update simple reverb parameters
    simpleReverb->setRoomSize(sizeParam->load());
    simpleReverb->setDamping(dampingParam->load());
    simpleReverb->setWidth(widthParam->load());
    simpleReverb->setMix(1.0f); // We'll do our own mix later

    // Process through simple reverb
    simpleReverb->process(buffer);

    // Skip the complex routing for now
    // router->process(buffer, numSamples);

    // Apply vintage coloration (optional)
    // vintageProcessor->process(buffer, numSamples);

    // Apply low-pass filter
    lowpassFilter.setCutoffFrequency(lpfFreqParam->load());
    lowpassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    lowpassFilter.process(context);

    // Apply tilt EQ
    float tiltGain = tiltGainParam->load();
    if (std::abs(tiltGain) > 0.1f)
    {
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
            currentSampleRate, 1000.0f, 0.707f,
            juce::Decibels::decibelsToGain(tiltGain));

        for (auto& eq : tiltEQ)
        {
            eq.coefficients = coeffs;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            leftChannel[i] = tiltEQ[0].processSample(leftChannel[i]);
            rightChannel[i] = tiltEQ[1].processSample(rightChannel[i]);
        }
    }

    // Mix dry and wet signals
    float mixAmount = mixParam->load();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float dry = dryBuffer.getSample(ch, i);
            float wet = buffer.getSample(ch, i);
            buffer.setSample(ch, i, dry * (1.0f - mixAmount) + wet * mixAmount);
        }
    }

    // Apply output gain
    float outputGain = juce::Decibels::decibelsToGain(outputGainParam->load());
    buffer.applyGain(outputGain);

    // Update output level meters
    outputLevelL = buffer.getRMSLevel(0, 0, numSamples);
    outputLevelR = buffer.getRMSLevel(1, 0, numSamples);
}

//==============================================================================
bool VintageVerbAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* VintageVerbAudioProcessor::createEditor()
{
    return new VintageVerbAudioProcessorEditor (*this);
}

//==============================================================================
void VintageVerbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void VintageVerbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
void VintageVerbAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == "reverbMode")
    {
        currentMode = static_cast<ReverbMode>(static_cast<int>(newValue));
        engineA->configureForMode(currentMode);
        engineB->configureForMode(currentMode);
    }
    else if (parameterID == "colorMode")
    {
        currentColor = static_cast<ColorMode>(static_cast<int>(newValue));
        vintageProcessor->setColorMode(static_cast<VintageColoration::ColorMode>(currentColor));
    }
    else if (parameterID == "routingMode")
    {
        currentRouting = static_cast<RoutingMode>(static_cast<int>(newValue));
        router->setRoutingMode(static_cast<DualEngineRouter::RoutingMode>(currentRouting));
    }
}

void VintageVerbAudioProcessor::updateReverbParameters()
{
    // Update engine A parameters
    engineA->setSize(sizeParam->load());
    engineA->setAttack(attackParam->load());
    engineA->setDamping(dampingParam->load());
    engineA->setModulation(modulationParam->load());
    engineA->setDensity(densityParam->load());
    engineA->setDiffusion(diffusionParam->load());
    engineA->setShape(shapeParam->load());
    engineA->setSpread(spreadParam->load());

    // Update engine B parameters (can be different for variety)
    engineB->setSize(sizeParam->load() * 0.9f);
    engineB->setAttack(attackParam->load() * 1.1f);
    engineB->setDamping(dampingParam->load() * 0.95f);
    engineB->setModulation(modulationParam->load() * 1.2f);
    engineB->setDensity(densityParam->load());
    engineB->setDiffusion(diffusionParam->load());
    engineB->setShape(shapeParam->load());
    engineB->setSpread(spreadParam->load());

    // Update vintage processor
    vintageProcessor->setIntensity(vintageIntensityParam->load());
    vintageProcessor->setNoiseAmount(vintageIntensityParam->load() * 0.2f);  // Scale noise with intensity
    vintageProcessor->setArtifactAmount(vintageIntensityParam->load() * 0.6f);  // Scale artifacts with intensity

    // Update router
    router->setEngineMix(engineMixParam->load());
    router->setWidth(widthParam->load());
    router->setCrossFeedAmount(crossFeedParam->load());
    router->setSeriesBlend(seriesBlendParam->load());
}

void VintageVerbAudioProcessor::updateFilterParameters()
{
    // Bass and treble multipliers affect EQ
    float bassFreq = bassFreqParam->load();
    float bassMul = bassMulParam->load();
    float highFreq = highFreqParam->load();
    float highMul = highMulParam->load();

    // Apply bass/treble multipliers to both engines
    engineA->setBassMultiplier(bassFreq, bassMul);
    engineA->setTrebleMultiplier(highFreq, highMul);
    engineB->setBassMultiplier(bassFreq, bassMul);
    engineB->setTrebleMultiplier(highFreq, highMul);
}

float VintageVerbAudioProcessor::getInputLevel(int channel) const
{
    return channel == 0 ? inputLevelL.load() : inputLevelR.load();
}

float VintageVerbAudioProcessor::getOutputLevel(int channel) const
{
    return channel == 0 ? outputLevelL.load() : outputLevelR.load();
}

float VintageVerbAudioProcessor::getCurrentDecayTime() const
{
    return engineA->getDecayTime();
}

juce::AudioProcessorValueTreeState::ParameterLayout VintageVerbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Main controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix", 0.0f, 1.0f, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "size", "Size", 0.0f, 1.0f, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack", 0.0f, 1.0f, 0.1f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "damping", "Damping", 0.0f, 1.0f, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "predelay", "PreDelay", 0.0f, 200.0f, 20.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "width", "Width", 0.0f, 2.0f, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modulation", "Modulation", 0.0f, 1.0f, 0.2f));

    // EQ controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bassFreq", "Bass Freq", 20.0f, 500.0f, 150.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bassMul", "Bass Mult", 0.1f, 4.0f, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highFreq", "High Freq", 1000.0f, 20000.0f, 6000.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highMul", "High Mult", 0.1f, 4.0f, 1.0f));

    // Advanced controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "density", "Density", 0.0f, 1.0f, 0.7f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "diffusion", "Diffusion", 0.0f, 1.0f, 0.8f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "shape", "Shape", 0.0f, 1.0f, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "spread", "Spread", 0.0f, 2.0f, 1.0f));

    // Mode selectors
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "reverbMode", "Reverb Mode",
        juce::StringArray{"Concert Hall", "Bright Hall", "Plate", "Room", "Chamber",
                         "Random Space", "Chorus Space", "Ambience", "Sanctuary",
                         "Dirty Hall", "Dirty Plate", "Smooth Plate", "Smooth Room",
                         "Smooth Random", "Nonlin", "Chaotic Hall", "Chaotic Chamber",
                         "Chaotic Neutral", "Cathedral", "Palace", "Chamber 1979", "Hall 1984"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "colorMode", "Color Mode",
        juce::StringArray{"1970s", "1980s", "Now"},
        2));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "routingMode", "Routing Mode",
        juce::StringArray{"Series", "Parallel", "A to B", "B to A"},
        1));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "engineMix", "Engine Mix", 0.0f, 1.0f, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crossFeed", "Cross Feed", 0.0f, 1.0f, 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "seriesBlend", "Series Blend", 0.0f, 1.0f, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "vintageIntensity", "Vintage", 0.0f, 1.0f, 0.5f));

    // Filter controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hpfFreq", "HPF Freq", 20.0f, 1000.0f, 20.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lpfFreq", "LPF Freq", 1000.0f, 20000.0f, 20000.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tiltGain", "Tilt", -12.0f, 12.0f, 0.0f));

    // Gain controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "inputGain", "Input Gain", -24.0f, 24.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain", -24.0f, 24.0f, 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VintageVerbAudioProcessor();
}
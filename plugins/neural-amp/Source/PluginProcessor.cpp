#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NAMProcessor.h"
#include "CabinetProcessor.h"

NeuralAmpAudioProcessor::NeuralAmpAudioProcessor()
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
    // Get parameter pointers
    inputGainParam = apvts.getRawParameterValue("input_gain");
    outputGainParam = apvts.getRawParameterValue("output_gain");
    gateThresholdParam = apvts.getRawParameterValue("gate_threshold");
    gateEnabledParam = apvts.getRawParameterValue("gate_enabled");
    bassParam = apvts.getRawParameterValue("bass");
    midParam = apvts.getRawParameterValue("mid");
    trebleParam = apvts.getRawParameterValue("treble");
    lowCutParam = apvts.getRawParameterValue("low_cut");
    highCutParam = apvts.getRawParameterValue("high_cut");
    cabEnabledParam = apvts.getRawParameterValue("cab_enabled");
    cabMixParam = apvts.getRawParameterValue("cab_mix");
    bypassParam = apvts.getRawParameterValue("bypass");

    // Create processors
    namProcessor = std::make_unique<NAMProcessor>();
    cabinetProcessor = std::make_unique<CabinetProcessor>();
}

NeuralAmpAudioProcessor::~NeuralAmpAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout NeuralAmpAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Input/Output gains
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "input_gain", "Input Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output_gain", "Output Level",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Noise gate
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gate_threshold", "Gate Threshold",
        juce::NormalisableRange<float>(-80.0f, 0.0f, 0.1f), -60.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "gate_enabled", "Gate Enable", false));

    // Tone stack
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bass", "Bass",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mid", "Mid",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "treble", "Treble",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Output filters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "low_cut", "Low Cut",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "high_cut", "High Cut",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 1.0f, 0.5f), 20000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Cabinet
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "cab_enabled", "Cabinet Enable", true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cab_mix", "Cabinet Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Bypass
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "bypass", "Bypass", false));

    return { params.begin(), params.end() };
}

const juce::String NeuralAmpAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NeuralAmpAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool NeuralAmpAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool NeuralAmpAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double NeuralAmpAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NeuralAmpAudioProcessor::getNumPrograms()
{
    return 1;
}

int NeuralAmpAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NeuralAmpAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String NeuralAmpAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void NeuralAmpAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void NeuralAmpAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Prepare DSP
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    inputGain.prepare(spec);
    outputGain.prepare(spec);
    noiseGate.prepare(spec);

    // Prepare NAM processor
    if (namProcessor)
        namProcessor->prepare(sampleRate, samplesPerBlock);

    // Prepare cabinet processor
    if (cabinetProcessor)
        cabinetProcessor->prepare(sampleRate, samplesPerBlock);

    // Prepare filters
    bassFilter.prepare(spec);
    midFilter.prepare(spec);
    trebleFilter.prepare(spec);
    lowCutFilter.prepare(spec);
    highCutFilter.prepare(spec);

    updateFilters();
}

void NeuralAmpAudioProcessor::releaseResources()
{
    if (namProcessor)
        namProcessor->reset();
    if (cabinetProcessor)
        cabinetProcessor->reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NeuralAmpAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
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

void NeuralAmpAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Check bypass
    bool bypassed = bypassParam->load() > 0.5f;
    if (bypassed)
        return;

    // Update filters if needed
    updateFilters();

    // Measure input level
    float inLevel = buffer.getMagnitude(0, buffer.getNumSamples());
    inputLevel.store(inLevel);

    // Create audio block for DSP
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);

    // Apply input gain
    float inGainDb = inputGainParam->load();
    inputGain.setGainDecibels(inGainDb);
    inputGain.process(context);

    // Apply noise gate if enabled
    bool gateEnabled = gateEnabledParam->load() > 0.5f;
    if (gateEnabled)
    {
        float threshold = gateThresholdParam->load();
        noiseGate.setThreshold(threshold);
        noiseGate.setRatio(100.0f);  // Hard gate
        noiseGate.setAttack(0.5f);
        noiseGate.setRelease(50.0f);
        noiseGate.process(context);
    }

    // Process through NAM model (mono processing, then copy to stereo)
    if (namProcessor && namProcessor->isModelLoaded())
    {
        // NAM is mono - process left channel, copy to right
        namProcessor->process(buffer);
    }

    // Apply tone stack
    float bassDb = bassParam->load();
    float midDb = midParam->load();
    float trebleDb = trebleParam->load();

    if (std::abs(bassDb) > 0.1f)
        bassFilter.process(context);
    if (std::abs(midDb) > 0.1f)
        midFilter.process(context);
    if (std::abs(trebleDb) > 0.1f)
        trebleFilter.process(context);

    // Apply cabinet IR if enabled
    bool cabEnabled = cabEnabledParam->load() > 0.5f;
    if (cabEnabled && cabinetProcessor && cabinetProcessor->isIRLoaded())
    {
        float mix = cabMixParam->load() / 100.0f;
        cabinetProcessor->setMix(mix);
        cabinetProcessor->process(buffer);
    }

    // Apply output filters
    float lowCut = lowCutParam->load();
    float highCut = highCutParam->load();

    if (lowCut > 25.0f)
        lowCutFilter.process(context);
    if (highCut < 19000.0f)
        highCutFilter.process(context);

    // Apply output gain
    float outGainDb = outputGainParam->load();
    outputGain.setGainDecibels(outGainDb);
    outputGain.process(context);

    // Measure output level
    float outLevel = buffer.getMagnitude(0, buffer.getNumSamples());
    outputLevel.store(outLevel);
}

void NeuralAmpAudioProcessor::updateFilters()
{
    float bassDb = bassParam->load();
    float midDb = midParam->load();
    float trebleDb = trebleParam->load();
    float lowCut = lowCutParam->load();
    float highCut = highCutParam->load();

    // Bass shelf at 100Hz
    *bassFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 100.0f, 0.707f, juce::Decibels::decibelsToGain(bassDb));

    // Mid peak at 800Hz
    *midFilter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate, 800.0f, 1.0f, juce::Decibels::decibelsToGain(midDb));

    // Treble shelf at 3000Hz
    *trebleFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 3000.0f, 0.707f, juce::Decibels::decibelsToGain(trebleDb));

    // Low cut (high pass)
    *lowCutFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(
        currentSampleRate, lowCut, 0.707f);

    // High cut (low pass)
    *highCutFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(
        currentSampleRate, highCut, 0.707f);
}

bool NeuralAmpAudioProcessor::loadNAMModel(const juce::File& modelFile)
{
    if (namProcessor && namProcessor->loadModel(modelFile))
    {
        currentModelPath = modelFile.getFullPathName();

        // Re-prepare with current settings
        if (currentSampleRate > 0)
            namProcessor->prepare(currentSampleRate, getBlockSize());

        return true;
    }
    return false;
}

bool NeuralAmpAudioProcessor::loadCabinetIR(const juce::File& irFile)
{
    if (cabinetProcessor && cabinetProcessor->loadIR(irFile))
    {
        currentIRPath = irFile.getFullPathName();
        return true;
    }
    return false;
}

juce::String NeuralAmpAudioProcessor::getModelName() const
{
    if (namProcessor)
        return namProcessor->getModelName();
    return "No Model";
}

juce::String NeuralAmpAudioProcessor::getModelInfo() const
{
    if (namProcessor)
        return namProcessor->getModelInfo();
    return "";
}

juce::String NeuralAmpAudioProcessor::getIRName() const
{
    if (cabinetProcessor)
        return cabinetProcessor->getIRName();
    return "No IR";
}

bool NeuralAmpAudioProcessor::isModelLoaded() const
{
    return namProcessor && namProcessor->isModelLoaded();
}

bool NeuralAmpAudioProcessor::isIRLoaded() const
{
    return cabinetProcessor && cabinetProcessor->isIRLoaded();
}

bool NeuralAmpAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* NeuralAmpAudioProcessor::createEditor()
{
    return new NeuralAmpAudioProcessorEditor(*this);
}

void NeuralAmpAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Add model and IR paths to state
    state.setProperty("modelPath", currentModelPath, nullptr);
    state.setProperty("irPath", currentIRPath, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NeuralAmpAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        auto newState = juce::ValueTree::fromXml(*xmlState);
        apvts.replaceState(newState);

        // Restore model and IR
        juce::String modelPath = newState.getProperty("modelPath", "");
        juce::String irPath = newState.getProperty("irPath", "");

        if (modelPath.isNotEmpty())
        {
            juce::File modelFile(modelPath);
            if (modelFile.existsAsFile())
                loadNAMModel(modelFile);
        }

        if (irPath.isNotEmpty())
        {
            juce::File irFile(irPath);
            if (irFile.existsAsFile())
                loadCabinetIR(irFile);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeuralAmpAudioProcessor();
}

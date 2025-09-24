#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ImprovedTapeEmulation.h"
#include <cmath>

TapeMachineAudioProcessor::TapeMachineAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
       apvts(*this, nullptr, "Parameters", createParameterLayout()),
       oversampling(2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false)
{
    tapeMachineParam = apvts.getRawParameterValue("tapeMachine");
    tapeSpeedParam = apvts.getRawParameterValue("tapeSpeed");
    tapeTypeParam = apvts.getRawParameterValue("tapeType");
    inputGainParam = apvts.getRawParameterValue("inputGain");
    saturationParam = apvts.getRawParameterValue("saturation");
    highpassFreqParam = apvts.getRawParameterValue("highpassFreq");
    lowpassFreqParam = apvts.getRawParameterValue("lowpassFreq");
    noiseAmountParam = apvts.getRawParameterValue("noiseAmount");
    noiseEnabledParam = apvts.getRawParameterValue("noiseEnabled");
    wowFlutterParam = apvts.getRawParameterValue("wowFlutter");
    outputGainParam = apvts.getRawParameterValue("outputGain");

    tapeEmulationLeft = std::make_unique<ImprovedTapeEmulation>();
    tapeEmulationRight = std::make_unique<ImprovedTapeEmulation>();

    // Initialize bias parameter
    biasParam = apvts.getRawParameterValue("bias");
}

TapeMachineAudioProcessor::~TapeMachineAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TapeMachineAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeMachine", "Tape Machine",
        juce::StringArray{"Studer A800", "Ampex ATR-102", "Blend"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeSpeed", "Tape Speed",
        juce::StringArray{"7.5 IPS", "15 IPS", "30 IPS"},
        1));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeType", "Tape Type",
        juce::StringArray{"Ampex 456", "GP9", "BASF 911"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "inputGain", "Input Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " dB"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "saturation", "Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bias", "Bias",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highpassFreq", "Highpass Frequency",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f), 30.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowpassFreq", "Lowpass Frequency",
        juce::NormalisableRange<float>(3000.0f, 20000.0f, 10.0f, 0.5f), 15000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noiseAmount", "Noise Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 5.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "noiseEnabled", "Noise Enabled", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wowFlutter", "Wow & Flutter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 10.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " dB"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    return { params.begin(), params.end() };
}

const juce::String TapeMachineAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TapeMachineAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool TapeMachineAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool TapeMachineAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double TapeMachineAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TapeMachineAudioProcessor::getNumPrograms()
{
    return 1;
}

int TapeMachineAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TapeMachineAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String TapeMachineAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void TapeMachineAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void TapeMachineAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    if (samplesPerBlock <= 0)
        samplesPerBlock = 512;

    currentSampleRate = static_cast<float>(sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 1;

    processorChainLeft.prepare(spec);
    processorChainRight.prepare(spec);

    oversampling.initProcessing(static_cast<size_t>(samplesPerBlock));

    wowFlutterDelayLeft.prepare(spec);
    wowFlutterDelayRight.prepare(spec);
    wowFlutterDelayLeft.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.05));
    wowFlutterDelayRight.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.05));

    if (tapeEmulationLeft)
        tapeEmulationLeft->prepare(sampleRate, samplesPerBlock);
    if (tapeEmulationRight)
        tapeEmulationRight->prepare(sampleRate, samplesPerBlock);

    updateFilters();
}

void TapeMachineAudioProcessor::releaseResources()
{
    processorChainLeft.reset();
    processorChainRight.reset();
    oversampling.reset();
    wowFlutterDelayLeft.reset();
    wowFlutterDelayRight.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TapeMachineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void TapeMachineAudioProcessor::updateFilters()
{
    if (!highpassFreqParam || !lowpassFreqParam)
        return;

    float hpFreq = highpassFreqParam->load();
    float lpFreq = lowpassFreqParam->load();

    if (currentSampleRate > 0.0f)
    {
        processorChainLeft.get<1>().setCutoffFrequency(hpFreq);
        processorChainLeft.get<1>().setType(juce::dsp::StateVariableTPTFilterType::highpass);
        processorChainLeft.get<1>().setResonance(0.707f);

        processorChainLeft.get<2>().setCutoffFrequency(lpFreq);
        processorChainLeft.get<2>().setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        processorChainLeft.get<2>().setResonance(0.707f);

        processorChainRight.get<1>().setCutoffFrequency(hpFreq);
        processorChainRight.get<1>().setType(juce::dsp::StateVariableTPTFilterType::highpass);
        processorChainRight.get<1>().setResonance(0.707f);

        processorChainRight.get<2>().setCutoffFrequency(lpFreq);
        processorChainRight.get<2>().setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        processorChainRight.get<2>().setResonance(0.707f);
    }
}

float TapeMachineAudioProcessor::processTapeSaturation(float input, float saturation,
                                                       TapeMachine machine, TapeType tape)
{
    if (std::abs(input) < 1e-8f)
        return 0.0f;

    float drive = 1.0f + (saturation * 0.01f) * 4.0f;
    float tapeCoeff = 1.0f;
    float harmonicMix = 0.5f;

    switch (tape)
    {
        case Ampex456:
            tapeCoeff = 1.2f;
            harmonicMix = 0.6f;
            break;
        case GP9:
            tapeCoeff = 0.9f;
            harmonicMix = 0.4f;
            break;
        case BASF911:
            tapeCoeff = 1.1f;
            harmonicMix = 0.5f;
            break;
    }

    float machineCharacter = 1.0f;
    float warmth = 0.0f;

    switch (machine)
    {
        case StuderA800:
            machineCharacter = 0.95f;
            warmth = 0.15f;
            break;
        case AmpexATR102:
            machineCharacter = 1.05f;
            warmth = 0.08f;
            break;
        case Blend:
            machineCharacter = 1.0f;
            warmth = 0.12f;
            break;
    }

    float driven = input * drive * tapeCoeff * machineCharacter;

    float tanh_sat = std::tanh(driven * 0.7f);
    float poly_sat = driven - (driven * driven * driven) / 3.0f;
    poly_sat = juce::jlimit(-1.0f, 1.0f, poly_sat);

    float saturated = tanh_sat * (1.0f - harmonicMix) + poly_sat * harmonicMix;

    float even_harmonic = driven * driven * 0.05f * warmth;
    even_harmonic = juce::jlimit(-0.1f, 0.1f, even_harmonic);
    saturated += even_harmonic;

    return saturated * 0.9f;
}

std::pair<float, float> TapeMachineAudioProcessor::processWowFlutter(float inputL, float inputR, float amount)
{
    if (currentSampleRate <= 0.0f || amount < 0.01f)
        return {inputL, inputR};

    const float wowRate = 0.3f;
    const float flutterRate = 7.0f;
    const float maxDelay = 0.002f;

    float wowIncrement = 2.0f * juce::MathConstants<float>::pi * wowRate / currentSampleRate;
    float flutterIncrement = 2.0f * juce::MathConstants<float>::pi * flutterRate / currentSampleRate;

    wowPhase += wowIncrement;
    if (wowPhase > 2.0f * juce::MathConstants<float>::pi)
        wowPhase -= 2.0f * juce::MathConstants<float>::pi;

    flutterPhase += flutterIncrement;
    if (flutterPhase > 2.0f * juce::MathConstants<float>::pi)
        flutterPhase -= 2.0f * juce::MathConstants<float>::pi;

    float wowMod = std::sin(wowPhase) * 0.7f;
    float flutterMod = std::sin(flutterPhase) * 0.3f;
    float totalMod = (wowMod + flutterMod) * amount * 0.01f * maxDelay;

    float delaySamples = juce::jmax(0.0f, currentSampleRate * totalMod);

    wowFlutterDelayLeft.setDelay(delaySamples);
    wowFlutterDelayRight.setDelay(delaySamples);

    wowFlutterDelayLeft.pushSample(0, inputL);
    wowFlutterDelayRight.pushSample(0, inputR);

    return {wowFlutterDelayLeft.popSample(0), wowFlutterDelayRight.popSample(0)};
}

void TapeMachineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    if (!tapeMachineParam || !tapeSpeedParam || !tapeTypeParam || !inputGainParam ||
        !saturationParam || !noiseAmountParam || !noiseEnabledParam ||
        !wowFlutterParam || !outputGainParam)
        return;

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0)
        return;

    // Set processing flag for reel animation
    isProcessingAudio.store(buffer.getMagnitude(0, buffer.getNumSamples()) > 0.001f);

    updateFilters();

    const auto machine = static_cast<TapeMachine>(static_cast<int>(tapeMachineParam->load()));
    const auto tapeType = static_cast<TapeType>(static_cast<int>(tapeTypeParam->load()));
    const auto tapeSpeed = static_cast<TapeSpeed>(static_cast<int>(tapeSpeedParam->load()));

    const float inputGainValue = juce::Decibels::decibelsToGain(inputGainParam->load());
    const float outputGainValue = juce::Decibels::decibelsToGain(outputGainParam->load());
    const float saturation = saturationParam->load();
    const float wowFlutter = wowFlutterParam->load();
    const float noiseAmount = noiseAmountParam->load() * 0.01f * 0.001f;
    const bool noiseEnabled = noiseEnabledParam->load() > 0.5f;

    processorChainLeft.get<0>().setGainLinear(inputGainValue);
    processorChainRight.get<0>().setGainLinear(inputGainValue);
    processorChainLeft.get<3>().setGainLinear(outputGainValue);
    processorChainRight.get<3>().setGainLinear(outputGainValue);

    // Calculate input levels AFTER input gain for tape saturation metering
    float inputPeakL = 0.0f;
    float inputPeakR = 0.0f;
    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);

    // Measure the signal level after input gain is applied
    // This shows how hard we're driving the tape saturation
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        inputPeakL = juce::jmax(inputPeakL, std::abs(leftChannel[i] * inputGainValue));
        inputPeakR = juce::jmax(inputPeakR, std::abs(rightChannel[i] * inputGainValue));
    }

    // Apply exponential smoothing to the levels
    const float attack = 0.3f;
    const float release = 0.7f;
    float currentInputL = inputLevelL.load();
    float currentInputR = inputLevelR.load();

    if (inputPeakL > currentInputL)
        inputLevelL.store(currentInputL + (inputPeakL - currentInputL) * attack);
    else
        inputLevelL.store(currentInputL + (inputPeakL - currentInputL) * (1.0f - release));

    if (inputPeakR > currentInputR)
        inputLevelR.store(currentInputR + (inputPeakR - currentInputR) * attack);
    else
        inputLevelR.store(currentInputR + (inputPeakR - currentInputR) * (1.0f - release));

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::AudioBlock<float> oversampledBlock = oversampling.processSamplesUp(block);

    auto leftBlock = oversampledBlock.getSingleChannelBlock(0);
    auto rightBlock = oversampledBlock.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    processorChainLeft.process(leftContext);
    processorChainRight.process(rightContext);

    float* leftData = leftBlock.getChannelPointer(0);
    float* rightData = rightBlock.getChannelPointer(0);
    const size_t numSamples = leftBlock.getNumSamples();

    for (size_t i = 0; i < numSamples; ++i)
    {
        if (leftData && rightData)
        {
            leftData[i] = processTapeSaturation(leftData[i], saturation, machine, tapeType);
            rightData[i] = processTapeSaturation(rightData[i], saturation, machine, tapeType);

            if (tapeEmulationLeft && tapeEmulationRight)
            {
                auto emulationMachine = static_cast<ImprovedTapeEmulation::TapeMachine>(static_cast<int>(machine));
                auto emulationSpeed = static_cast<ImprovedTapeEmulation::TapeSpeed>(static_cast<int>(tapeSpeed));
                auto emulationType = static_cast<ImprovedTapeEmulation::TapeType>(static_cast<int>(tapeType));

                // Get bias parameter value (default to 0.5 if not available)
                float biasAmount = biasParam ? biasParam->load() * 0.01f : 0.5f;

                // Process with improved tape emulation including bias
                leftData[i] = tapeEmulationLeft->processSample(leftData[i],
                                                              emulationMachine,
                                                              emulationSpeed,
                                                              emulationType,
                                                              biasAmount,
                                                              saturation * 0.01f,
                                                              wowFlutter * 0.01f);

                rightData[i] = tapeEmulationRight->processSample(rightData[i],
                                                                emulationMachine,
                                                                emulationSpeed,
                                                                emulationType,
                                                                biasAmount,
                                                                saturation * 0.01f,
                                                                wowFlutter * 0.01f);
            }

            auto [wowL, wowR] = processWowFlutter(leftData[i], rightData[i], wowFlutter);
            leftData[i] = wowL;
            rightData[i] = wowR;

            if (noiseEnabled && noiseAmount > 0.0f)
            {
                float noise = (noiseGenerator.nextFloat() * 2.0f - 1.0f) * noiseAmount;
                leftData[i] += noise;
                rightData[i] += noise;
            }
        }
    }

    oversampling.processSamplesDown(block);

    // Calculate output levels for metering after processing
    float outputPeakL = 0.0f;
    float outputPeakR = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        outputPeakL = juce::jmax(outputPeakL, std::abs(leftChannel[i]));
        outputPeakR = juce::jmax(outputPeakR, std::abs(rightChannel[i]));
    }

    // Apply exponential smoothing to the output levels
    float currentOutputL = outputLevelL.load();
    float currentOutputR = outputLevelR.load();

    if (outputPeakL > currentOutputL)
        outputLevelL.store(currentOutputL + (outputPeakL - currentOutputL) * attack);
    else
        outputLevelL.store(currentOutputL + (outputPeakL - currentOutputL) * (1.0f - release));

    if (outputPeakR > currentOutputR)
        outputLevelR.store(currentOutputR + (outputPeakR - currentOutputR) * attack);
    else
        outputLevelR.store(currentOutputR + (outputPeakR - currentOutputR) * (1.0f - release));
}

bool TapeMachineAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TapeMachineAudioProcessor::createEditor()
{
    return new TapeMachineAudioProcessorEditor (*this);
}

void TapeMachineAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml)
        copyXmlToBinary(*xml, destData);
}

void TapeMachineAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeMachineAudioProcessor();
}
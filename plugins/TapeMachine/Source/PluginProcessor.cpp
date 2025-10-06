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

    // Initialize shared wow/flutter for stereo coherence
    sharedWowFlutter = std::make_unique<WowFlutterProcessor>();

    // Initialize bias and calibration parameters
    biasParam = apvts.getRawParameterValue("bias");
    calibrationParam = apvts.getRawParameterValue("calibration");
}

TapeMachineAudioProcessor::~TapeMachineAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TapeMachineAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeMachine", "Tape Machine",
        juce::StringArray{"Swiss 800", "Classic 102", "Hybrid Blend"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeSpeed", "Tape Speed",
        juce::StringArray{"7.5 IPS", "15 IPS", "30 IPS"},
        1));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeType", "Tape Type",
        juce::StringArray{"Type 456", "Type GP9", "Type 911"},
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

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "calibration", "Calibration",
        juce::StringArray{"0dB", "+3dB", "+6dB", "+9dB"},
        0));  // Default to 0dB

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highpassFreq", "Highpass Frequency",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f), 20.0f,
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

    // Get the actual oversampling factor from the oversampling object
    // The object was initialized with 2 stages, giving 2^2 = 4x oversampling
    const auto oversamplingFactor = oversampling.getOversamplingFactor();
    const double oversampledRate = sampleRate * static_cast<double>(oversamplingFactor);
    const int oversampledBlockSize = samplesPerBlock * static_cast<int>(oversamplingFactor);

    // Store oversampled rate for filter updates
    currentOversampledRate = static_cast<float>(oversampledRate);

    // Prepare processor chains with OVERSAMPLED rate since they process oversampled audio
    juce::dsp::ProcessSpec oversampledSpec;
    oversampledSpec.sampleRate = oversampledRate;
    oversampledSpec.maximumBlockSize = static_cast<juce::uint32>(oversampledBlockSize);
    oversampledSpec.numChannels = 1;

    processorChainLeft.prepare(oversampledSpec);
    processorChainRight.prepare(oversampledSpec);

    // Set ramp duration for gain processors to handle smoothing automatically
    processorChainLeft.get<0>().setRampDurationSeconds(0.02);  // 20ms for input gain
    processorChainRight.get<0>().setRampDurationSeconds(0.02);
    processorChainLeft.get<3>().setRampDurationSeconds(0.02);  // 20ms for output gain
    processorChainRight.get<3>().setRampDurationSeconds(0.02);

    oversampling.initProcessing(static_cast<size_t>(samplesPerBlock));

    // Prepare tape emulation with oversampled rate so filter cutoffs are correct
    if (tapeEmulationLeft)
        tapeEmulationLeft->prepare(oversampledRate, oversampledBlockSize);
    if (tapeEmulationRight)
        tapeEmulationRight->prepare(oversampledRate, oversampledBlockSize);

    // Prepare shared wow/flutter with oversampled rate
    if (sharedWowFlutter)
        sharedWowFlutter->prepare(oversampledRate);

    updateFilters();

    // Initialize smoothed parameters with a ramp time of 20ms to prevent zipper noise
    const float rampTimeMs = 20.0f;

    // Note: Input/output gain smoothing is now handled by the gain processors themselves
    smoothedSaturation.reset(sampleRate, rampTimeMs * 0.001f);
    smoothedNoiseAmount.reset(sampleRate, rampTimeMs * 0.001f);
    smoothedWowFlutter.reset(sampleRate, rampTimeMs * 0.001f);
}

void TapeMachineAudioProcessor::releaseResources()
{
    processorChainLeft.reset();
    processorChainRight.reset();
    oversampling.reset();
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

    // Use oversampled rate since filters process oversampled audio
    if (currentOversampledRate > 0.0f)
    {
        // Bypass highpass filter only when at minimum frequency (20Hz)
        bypassHighpass = (hpFreq <= 20.0f);

        if (!bypassHighpass)
        {
            processorChainLeft.get<1>().setCutoffFrequency(hpFreq);
            processorChainLeft.get<1>().setType(juce::dsp::StateVariableTPTFilterType::highpass);
            processorChainLeft.get<1>().setResonance(0.707f);

            processorChainRight.get<1>().setCutoffFrequency(hpFreq);
            processorChainRight.get<1>().setType(juce::dsp::StateVariableTPTFilterType::highpass);
            processorChainRight.get<1>().setResonance(0.707f);
        }

        // Bypass lowpass filter only when at maximum frequency (19kHz or above)
        bypassLowpass = (lpFreq >= 19000.0f);

        if (!bypassLowpass)
        {
            processorChainLeft.get<2>().setCutoffFrequency(lpFreq);
            processorChainLeft.get<2>().setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            processorChainLeft.get<2>().setResonance(0.707f);

            processorChainRight.get<2>().setCutoffFrequency(lpFreq);
            processorChainRight.get<2>().setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            processorChainRight.get<2>().setResonance(0.707f);
        }
    }
}


void TapeMachineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    // Critical safety check - if parameters failed to initialize, don't process
    if (!tapeMachineParam || !tapeSpeedParam || !tapeTypeParam || !inputGainParam ||
        !saturationParam || !highpassFreqParam || !lowpassFreqParam ||
        !noiseAmountParam || !noiseEnabledParam || !wowFlutterParam || !outputGainParam)
    {
        // This should never happen in production, but if it does, pass audio through
        // rather than producing silence
        jassertfalse; // Alert during debug builds
        return;
    }

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0)
        return;

    // Set processing flag based on DAW transport state
    if (auto* playHead = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo posInfo;
        if (playHead->getCurrentPosition(posInfo))
        {
            // Reels spin when transport is playing OR recording
            isProcessingAudio.store(posInfo.isPlaying || posInfo.isRecording);
        }
    }

    // Only update filters if parameters changed (using instance variables)
    float currentHpFreq = highpassFreqParam->load();
    float currentLpFreq = lowpassFreqParam->load();

    if (std::abs(currentHpFreq - lastHpFreq) > 0.01f || std::abs(currentLpFreq - lastLpFreq) > 0.01f)
    {
        updateFilters();
        lastHpFreq = currentHpFreq;
        lastLpFreq = currentLpFreq;
    }

    const auto machine = static_cast<TapeMachine>(static_cast<int>(tapeMachineParam->load()));
    const auto tapeType = static_cast<TapeType>(static_cast<int>(tapeTypeParam->load()));
    const auto tapeSpeed = static_cast<TapeSpeed>(static_cast<int>(tapeSpeedParam->load()));

    // Update target values for smoothing
    const float targetInputGain = juce::Decibels::decibelsToGain(inputGainParam->load());
    const float targetOutputGain = juce::Decibels::decibelsToGain(outputGainParam->load());

    // Let the gain processors handle their own smoothing with the configured ramp time
    processorChainLeft.get<0>().setGainLinear(targetInputGain);
    processorChainRight.get<0>().setGainLinear(targetInputGain);
    processorChainLeft.get<3>().setGainLinear(targetOutputGain);
    processorChainRight.get<3>().setGainLinear(targetOutputGain);

    // Keep smoothing for non-gain parameters that we process per-sample
    smoothedSaturation.setTargetValue(saturationParam->load());
    smoothedWowFlutter.setTargetValue(wowFlutterParam->load());
    // Scale noise amount reasonably (0-100% becomes 0-0.01 for subtle tape hiss)
    smoothedNoiseAmount.setTargetValue(noiseAmountParam->load() * 0.01f * 0.01f);

    const bool noiseEnabled = noiseEnabledParam->load() > 0.5f;

    // Calculate RMS input levels BEFORE input gain for VU-accurate metering
    // VU meters use RMS with 300ms integration time (not peak)
    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);

    // Calculate RMS for this block
    float sumSquaresL = 0.0f;
    float sumSquaresR = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        sumSquaresL += leftChannel[i] * leftChannel[i];
        sumSquaresR += rightChannel[i] * rightChannel[i];
    }
    float rmsBlockL = std::sqrt(sumSquaresL / buffer.getNumSamples());
    float rmsBlockR = std::sqrt(sumSquaresR / buffer.getNumSamples());

    // VU meter ballistics: 300ms integration (exponential moving average)
    // Time constant: tau = 300ms, coefficient = exp(-dt/tau)
    const float dt = buffer.getNumSamples() / currentSampleRate;  // Block duration in seconds
    const float tau = 0.3f;  // 300ms VU standard
    const float alpha = std::exp(-dt / tau);  // Smoothing coefficient

    // Update RMS with exponential moving average
    rmsInputL = alpha * rmsInputL + (1.0f - alpha) * rmsBlockL;
    rmsInputR = alpha * rmsInputR + (1.0f - alpha) * rmsBlockR;

    // Store as dB for display (VU meters show dB scale)
    inputLevelL.store(rmsInputL);
    inputLevelR.store(rmsInputR);

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::AudioBlock<float> oversampledBlock = oversampling.processSamplesUp(block);

    auto leftBlock = oversampledBlock.getSingleChannelBlock(0);
    auto rightBlock = oversampledBlock.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    // Input chain: Gain → Highpass (before tape emulation)
    // Element 0: Input gain
    processorChainLeft.get<0>().process(leftContext);
    processorChainRight.get<0>().process(rightContext);

    // Element 1: Highpass filter (bypass if at minimum)
    if (!bypassHighpass)
    {
        processorChainLeft.get<1>().process(leftContext);
        processorChainRight.get<1>().process(rightContext);
    }

    float* leftData = leftBlock.getChannelPointer(0);
    float* rightData = rightBlock.getChannelPointer(0);
    const size_t numSamples = leftBlock.getNumSamples();

    // Use the stored oversampled rate (calculated in prepareToPlay)
    const double oversampledRate = static_cast<double>(currentOversampledRate);

    for (size_t i = 0; i < numSamples; ++i)
    {
        if (leftData && rightData)
        {
            // Get smoothed values per sample for zipper-free parameter changes
            const float currentSaturation = smoothedSaturation.getNextValue();
            const float currentWowFlutter = smoothedWowFlutter.getNextValue();
            const float currentNoiseAmount = smoothedNoiseAmount.getNextValue();

            // Calculate shared wow/flutter modulation once per sample for stereo coherence
            float sharedModulation = 0.0f;
            if (currentWowFlutter > 0.0f && sharedWowFlutter)
            {
                auto speedEnum = static_cast<ImprovedTapeEmulation::TapeSpeed>(static_cast<int>(tapeSpeed));
                float wowRate = 0.5f;
                float flutterRate = 5.0f;

                // Get speed characteristics (simplified - could be cached)
                switch (speedEnum)
                {
                    case ImprovedTapeEmulation::Speed_7_5_IPS:
                        wowRate = 0.33f;
                        flutterRate = 3.5f;
                        break;
                    case ImprovedTapeEmulation::Speed_15_IPS:
                        wowRate = 0.5f;
                        flutterRate = 5.0f;
                        break;
                    case ImprovedTapeEmulation::Speed_30_IPS:
                        wowRate = 0.8f;
                        flutterRate = 7.0f;
                        break;
                }

                sharedModulation = sharedWowFlutter->calculateModulation(
                    currentWowFlutter * 0.7f * 0.01f,   // Wow amount
                    currentWowFlutter * 0.3f * 0.01f,   // Flutter amount
                    wowRate,
                    flutterRate,
                    oversampledRate);
            }

            // Use improved tape emulation
            auto emulationMachine = static_cast<ImprovedTapeEmulation::TapeMachine>(static_cast<int>(machine));
            auto emulationSpeed = static_cast<ImprovedTapeEmulation::TapeSpeed>(static_cast<int>(tapeSpeed));
            auto emulationType = static_cast<ImprovedTapeEmulation::TapeType>(static_cast<int>(tapeType));

            float biasAmount = biasParam ? biasParam->load() * 0.01f : 0.5f;

            // Get calibration level (0/3/6/9 dB)
            int calIndex = calibrationParam ? static_cast<int>(calibrationParam->load()) : 0;
            float calibrationDb = calIndex * 3.0f;  // 0, 3, 6, or 9 dB

            // Process with improved tape emulation (includes saturation and wow/flutter)
            // Pass shared modulation for stereo coherence
            leftData[i] = tapeEmulationLeft->processSample(leftData[i],
                                                          emulationMachine,
                                                          emulationSpeed,
                                                          emulationType,
                                                          biasAmount,
                                                          currentSaturation * 0.01f,
                                                          currentWowFlutter * 0.01f,
                                                          noiseEnabled,
                                                          currentNoiseAmount * 100.0f,
                                                          &sharedModulation,
                                                          calibrationDb);

            rightData[i] = tapeEmulationRight->processSample(rightData[i],
                                                            emulationMachine,
                                                            emulationSpeed,
                                                            emulationType,
                                                            biasAmount,
                                                            currentSaturation * 0.01f,
                                                            currentWowFlutter * 0.01f,
                                                            noiseEnabled,
                                                            currentNoiseAmount * 100.0f,
                                                            &sharedModulation,
                                                            calibrationDb);
        }
    }

    // Apply crosstalk simulation (L/R channel bleed from tape head)
    // Real tape machines have subtle channel crosstalk, more pronounced in vintage machines
    if (leftData && rightData && machine != Blend)  // Skip for blend mode
    {
        // Get machine-specific crosstalk amount
        float crosstalkAmount = 0.01f;  // Default -40dB
        switch (machine)
        {
            case StuderA800:
                crosstalkAmount = 0.005f;  // -46dB (excellent separation)
                break;
            case AmpexATR102:
                crosstalkAmount = 0.015f;  // -36dB (vintage character)
                break;
            case Blend:
                crosstalkAmount = 0.01f;   // -40dB (balanced)
                break;
        }

        // Apply crosstalk by mixing small amount of opposite channel
        for (size_t i = 0; i < numSamples; ++i)
        {
            float tempL = leftData[i];
            float tempR = rightData[i];
            leftData[i] += tempR * crosstalkAmount;
            rightData[i] += tempL * crosstalkAmount;
        }
    }

    // Output chain: Lowpass → Output Gain (after tape emulation)
    // Element 2: Lowpass filter (bypass if at maximum)
    if (!bypassLowpass)
    {
        processorChainLeft.get<2>().process(leftContext);
        processorChainRight.get<2>().process(rightContext);
    }

    // Element 3: Output gain
    processorChainLeft.get<3>().process(leftContext);
    processorChainRight.get<3>().process(rightContext);

    oversampling.processSamplesDown(block);

    // Calculate RMS output levels after processing (VU-accurate)
    float sumSquaresOutL = 0.0f;
    float sumSquaresOutR = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        sumSquaresOutL += leftChannel[i] * leftChannel[i];
        sumSquaresOutR += rightChannel[i] * rightChannel[i];
    }
    float rmsBlockOutL = std::sqrt(sumSquaresOutL / buffer.getNumSamples());
    float rmsBlockOutR = std::sqrt(sumSquaresOutR / buffer.getNumSamples());

    // Apply same VU ballistics to output (reuse dt, tau, alpha from input calculation)
    rmsOutputL = alpha * rmsOutputL + (1.0f - alpha) * rmsBlockOutL;
    rmsOutputR = alpha * rmsOutputR + (1.0f - alpha) * rmsBlockOutR;

    outputLevelL.store(rmsOutputL);
    outputLevelR.store(rmsOutputR);
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
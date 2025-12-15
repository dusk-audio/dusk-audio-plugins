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
       apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    tapeMachineParam = apvts.getRawParameterValue("tapeMachine");
    tapeSpeedParam = apvts.getRawParameterValue("tapeSpeed");
    tapeTypeParam = apvts.getRawParameterValue("tapeType");
    inputGainParam = apvts.getRawParameterValue("inputGain");
    highpassFreqParam = apvts.getRawParameterValue("highpassFreq");
    lowpassFreqParam = apvts.getRawParameterValue("lowpassFreq");
    noiseAmountParam = apvts.getRawParameterValue("noiseAmount");
    noiseEnabledParam = apvts.getRawParameterValue("noiseEnabled");
    wowFlutterParam = apvts.getRawParameterValue("wowFlutter");
    outputGainParam = apvts.getRawParameterValue("outputGain");
    autoCompParam = apvts.getRawParameterValue("autoComp");

    tapeEmulationLeft = std::make_unique<ImprovedTapeEmulation>();
    tapeEmulationRight = std::make_unique<ImprovedTapeEmulation>();

    // Initialize shared wow/flutter for stereo coherence
    sharedWowFlutter = std::make_unique<WowFlutterProcessor>();

    // Initialize bias and calibration parameters
    biasParam = apvts.getRawParameterValue("bias");
    calibrationParam = apvts.getRawParameterValue("calibration");
    oversamplingParam = apvts.getRawParameterValue("oversampling");
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
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 4.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bias", "Bias",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 40.0f,
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

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "autoComp", "Auto Compensation", true));

    // Oversampling quality (2x or 4x) - higher reduces aliasing from saturation
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "oversampling", "Oversampling",
        juce::StringArray{"2x", "4x"},
        1));  // Default to 4x for best quality

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

    // Get user's oversampling choice (0 = 2x, 1 = 4x)
    int oversamplingChoice = oversamplingParam ? static_cast<int>(oversamplingParam->load()) : 1;
    currentOversamplingFactor = (oversamplingChoice == 0) ? 2 : 4;

    // Check if we need to recreate oversamplers
    bool needsRecreate = (std::abs(sampleRate - lastPreparedSampleRate) > 0.01) ||
                         (samplesPerBlock != lastPreparedBlockSize) ||
                         (oversamplingChoice != lastOversamplingChoice) ||
                         !oversampler2x || !oversampler4x;

    if (needsRecreate)
    {
        // Create both 2x and 4x oversamplers using high-quality FIR equiripple filters
        // FIR provides superior alias rejection compared to IIR, essential for tape saturation
        oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
            static_cast<size_t>(getTotalNumInputChannels()), 1,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

        oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
            static_cast<size_t>(getTotalNumInputChannels()), 2,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

        oversampler2x->initProcessing(static_cast<size_t>(samplesPerBlock));
        oversampler4x->initProcessing(static_cast<size_t>(samplesPerBlock));

        lastPreparedSampleRate = sampleRate;
        lastPreparedBlockSize = samplesPerBlock;
        lastOversamplingChoice = oversamplingChoice;
    }
    else
    {
        if (oversampler2x) oversampler2x->reset();
        if (oversampler4x) oversampler4x->reset();
    }

    // Calculate oversampled rate based on current factor
    const double oversampledRate = sampleRate * currentOversamplingFactor;
    const int oversampledBlockSize = samplesPerBlock * currentOversamplingFactor;

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
    const float saturationRampTimeMs = 150.0f;  // Slower smoothing for saturation to avoid jumps

    // Note: Input/output gain smoothing is now handled by the gain processors themselves
    smoothedSaturation.reset(sampleRate, saturationRampTimeMs * 0.001f);
    smoothedNoiseAmount.reset(sampleRate, rampTimeMs * 0.001f);
    smoothedWowFlutter.reset(sampleRate, rampTimeMs * 0.001f);

    // Report latency to host for PDC
    auto* activeOversampler = (currentOversamplingFactor == 4) ? oversampler4x.get() : oversampler2x.get();
    if (activeOversampler)
        setLatencySamples(static_cast<int>(activeOversampler->getLatencyInSamples()));
}

void TapeMachineAudioProcessor::releaseResources()
{
    processorChainLeft.reset();
    processorChainRight.reset();
    if (oversampler2x) oversampler2x->reset();
    if (oversampler4x) oversampler4x->reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TapeMachineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    auto in = layouts.getMainInputChannelSet();
    auto out = layouts.getMainOutputChannelSet();

    // Support mono and stereo
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Support: mono→mono, mono→stereo, stereo→stereo
    if (in == juce::AudioChannelSet::mono() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()))
        return true;

    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo())
        return true;

    return false;
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
        // Always apply highpass filter to remove subsonic rumble from tape nonlinearities
        bypassHighpass = false;

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

    // Debug: Log when processBlock is called
    static int processCallCount = 0;
    static bool loggedProcessing = false;
    if (!loggedProcessing && ++processCallCount > 10)
    {
        loggedProcessing = true;
        juce::File logFile("/tmp/tapemachine_processing.txt");
        juce::String logText;
        logText << "ProcessBlock called, channels=" << buffer.getNumChannels()
                << ", samples=" << buffer.getNumSamples() << juce::newLine;
        logFile.appendText(logText);
    }

    // Critical safety check - if parameters failed to initialize, don't process
    if (!tapeMachineParam || !tapeSpeedParam || !tapeTypeParam || !inputGainParam ||
        !highpassFreqParam || !lowpassFreqParam ||
        !noiseAmountParam || !noiseEnabledParam || !wowFlutterParam || !outputGainParam)
    {
        // This should never happen in production, but if it does, pass audio through
        // rather than producing silence
        jassertfalse; // Alert during debug builds

        static bool loggedParamError = false;
        if (!loggedParamError)
        {
            loggedParamError = true;
            juce::File logFile("/tmp/tapemachine_param_error.txt");
            logFile.appendText("PARAMETER INITIALIZATION FAILED - bypassing\n");
        }
        return;
    }

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0)
        return;

    // For mono processing: duplicate channel 0 to channel 1 temporarily
    const bool isMono = (buffer.getNumChannels() == 1);
    if (isMono)
    {
        // Add a second channel for processing
        buffer.setSize(2, buffer.getNumSamples(), true, false, false);
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    }

    if (buffer.getNumChannels() < 2)
        return;  // Safety check - should never happen now

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
    const float inputGainDB = inputGainParam->load();
    const float targetInputGain = juce::Decibels::decibelsToGain(inputGainDB);
    float targetOutputGain;

    // VTM-style auto-compensation: when enabled, output is LOCKED to inverse of input
    // This lets you drive the tape harder with input while maintaining consistent output level
    const bool autoCompEnabled = autoCompParam && autoCompParam->load() > 0.5f;

    if (autoCompEnabled)
    {
        // VTM-style: Output is exactly inverse of input for unity gain through the plugin
        // Input drives tape saturation, output compensates to maintain consistent level
        // At 0dB input: 0dB output (unity)
        // At +12dB input: -12dB output (unity through saturation)
        targetOutputGain = juce::Decibels::decibelsToGain(-inputGainDB);
    }
    else
    {
        // Manual mode: use the output gain knob directly
        targetOutputGain = juce::Decibels::decibelsToGain(outputGainParam->load());
    }

    // Let the gain processors handle their own smoothing with the configured ramp time
    processorChainLeft.get<0>().setGainLinear(targetInputGain);
    processorChainRight.get<0>().setGainLinear(targetInputGain);
    processorChainLeft.get<3>().setGainLinear(targetOutputGain);
    processorChainRight.get<3>().setGainLinear(targetOutputGain);

    // Keep smoothing for non-gain parameters that we process per-sample
    // Drive saturation from input gain: -12dB to +12dB maps to 0% to 100%
    // (inputGainDB already calculated above)
    const float saturationAmount = juce::jlimit(0.0f, 100.0f, ((inputGainDB + 12.0f) / 24.0f) * 100.0f);
    smoothedSaturation.setTargetValue(saturationAmount);
    smoothedWowFlutter.setTargetValue(wowFlutterParam->load());
    // Scale noise amount: 0-100% becomes 0-1.0 range for proper noise level control
    // The actual noise floor level is determined by tape characteristics (-62dB to -68dB)
    smoothedNoiseAmount.setTargetValue(noiseAmountParam->load() * 0.01f);

    const bool noiseEnabled = noiseEnabledParam->load() > 0.5f;

    // Apply input gain in non-oversampled domain for accurate VU metering
    juce::dsp::AudioBlock<float> block(buffer);
    auto blockLeftChan = block.getSingleChannelBlock(0);
    auto blockRightChan = block.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> blockLeftContext(blockLeftChan);
    juce::dsp::ProcessContextReplacing<float> blockRightContext(blockRightChan);

    // Apply input gain at original sample rate
    processorChainLeft.get<0>().process(blockLeftContext);
    processorChainRight.get<0>().process(blockRightContext);

    // Calculate RMS input levels AFTER input gain for tape drive metering
    // VU meter shows how hard you're driving the tape (measure at original sample rate)
    const float* leftChannel = buffer.getReadPointer(0);
    const float* rightChannel = buffer.getReadPointer(1);
    const int numSamplesOriginal = buffer.getNumSamples();

    // Calculate RMS for this block (input gain already applied above)
    float sumSquaresL = 0.0f;
    float sumSquaresR = 0.0f;
    for (int i = 0; i < numSamplesOriginal; ++i)
    {
        float sampleL = leftChannel[i];
        float sampleR = rightChannel[i];
        sumSquaresL += sampleL * sampleL;
        sumSquaresR += sampleR * sampleR;
    }
    float rmsBlockL = std::sqrt(sumSquaresL / numSamplesOriginal);
    float rmsBlockR = std::sqrt(sumSquaresR / numSamplesOriginal);

    // VU meter ballistics: 300ms integration (exponential moving average)
    const float dt = numSamplesOriginal / currentSampleRate;
    const float tau = 0.3f;  // 300ms VU standard
    const float alpha = std::exp(-dt / tau);

    rmsInputL = alpha * rmsInputL + (1.0f - alpha) * rmsBlockL;
    rmsInputR = alpha * rmsInputR + (1.0f - alpha) * rmsBlockR;

    inputLevelL.store(rmsInputL);
    inputLevelR.store(rmsInputR);

    // Debug: Print VU meter values periodically
    static int debugCounter = 0;
    if (++debugCounter > 100)
    {
        debugCounter = 0;
        DBG("RMS Level: " << juce::Decibels::gainToDecibels(rmsInputL) << " dBFS");
        DBG("VU Reading: " << (juce::Decibels::gainToDecibels(rmsInputL) + 12.0f) << " VU");
    }

    // Now oversample for high-quality processing using selected quality
    auto* activeOversampler = (currentOversamplingFactor == 4) ? oversampler4x.get() : oversampler2x.get();
    if (!activeOversampler)
        return;

    juce::dsp::AudioBlock<float> oversampledBlock = activeOversampler->processSamplesUp(block);

    auto leftBlock = oversampledBlock.getSingleChannelBlock(0);
    auto rightBlock = oversampledBlock.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

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

    activeOversampler->processSamplesDown(block);

    // Calculate RMS output levels after processing (VU-accurate)
    const float* outputLeftChannel = buffer.getReadPointer(0);
    const float* outputRightChannel = buffer.getReadPointer(1);

    float sumSquaresOutL = 0.0f;
    float sumSquaresOutR = 0.0f;
    float peakOutL = 0.0f;
    float peakOutR = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sampleL = outputLeftChannel[i];
        float sampleR = outputRightChannel[i];
        sumSquaresOutL += sampleL * sampleL;
        sumSquaresOutR += sampleR * sampleR;
        peakOutL = std::max(peakOutL, std::abs(sampleL));
        peakOutR = std::max(peakOutR, std::abs(sampleR));
    }
    float rmsBlockOutL = std::sqrt(sumSquaresOutL / buffer.getNumSamples());
    float rmsBlockOutR = std::sqrt(sumSquaresOutR / buffer.getNumSamples());

    // VU meter ballistics: 300ms integration (exponential moving average)
    const float dtOut = buffer.getNumSamples() / currentSampleRate;
    const float tauOut = 0.3f;  // 300ms VU standard
    const float alphaOut = std::exp(-dtOut / tauOut);

    rmsOutputL = alphaOut * rmsOutputL + (1.0f - alphaOut) * rmsBlockOutL;
    rmsOutputR = alphaOut * rmsOutputR + (1.0f - alphaOut) * rmsBlockOutR;

    outputLevelL.store(rmsOutputL);
    outputLevelR.store(rmsOutputR);

    // Debug: Write output levels to file
    static int debugCounterOut = 0;
    if (++debugCounterOut > 48)  // Write ~once per second
    {
        debugCounterOut = 0;
        float peakDB = juce::Decibels::gainToDecibels(peakOutL);
        float rmsDB = juce::Decibels::gainToDecibels(rmsOutputL);

        juce::File logFile("/tmp/tapemachine_debug.txt");
        juce::String logText;
        logText << "=== OUTPUT LEVELS ===" << juce::newLine;
        logText << "Peak: " << juce::String(peakDB, 2) << " dBFS" << juce::newLine;
        logText << "RMS:  " << juce::String(rmsDB, 2) << " dBFS (VU shows this)" << juce::newLine;
        logText << "RMS Linear: " << juce::String(rmsOutputL, 4) << juce::newLine;
        logText << "Difference: " << juce::String(peakDB - rmsDB, 2) << " dB" << juce::newLine;
        logText << juce::newLine;
        logFile.appendText(logText);
    }
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
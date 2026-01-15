#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ImprovedTapeEmulation.h"
#include "TapeMachinePresets.h"
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
    // Initialize all parameter pointers - CRITICAL: validate these exist
    tapeMachineParam = apvts.getRawParameterValue("tapeMachine");
    tapeSpeedParam = apvts.getRawParameterValue("tapeSpeed");
    tapeTypeParam = apvts.getRawParameterValue("tapeType");
    inputGainParam = apvts.getRawParameterValue("inputGain");
    highpassFreqParam = apvts.getRawParameterValue("highpassFreq");
    lowpassFreqParam = apvts.getRawParameterValue("lowpassFreq");
    noiseAmountParam = apvts.getRawParameterValue("noiseAmount");
    noiseEnabledParam = apvts.getRawParameterValue("noiseEnabled");
    wowAmountParam = apvts.getRawParameterValue("wowAmount");
    flutterAmountParam = apvts.getRawParameterValue("flutterAmount");
    outputGainParam = apvts.getRawParameterValue("outputGain");
    autoCompParam = apvts.getRawParameterValue("autoComp");
    biasParam = apvts.getRawParameterValue("bias");
    calibrationParam = apvts.getRawParameterValue("calibration");
    oversamplingParam = apvts.getRawParameterValue("oversampling");

    // Validate all critical parameters exist - fail early if not
    // This catches configuration errors during development rather than during audio processing
    jassert(tapeMachineParam != nullptr);
    jassert(tapeSpeedParam != nullptr);
    jassert(tapeTypeParam != nullptr);
    jassert(inputGainParam != nullptr);
    jassert(highpassFreqParam != nullptr);
    jassert(lowpassFreqParam != nullptr);
    jassert(noiseAmountParam != nullptr);
    jassert(noiseEnabledParam != nullptr);
    jassert(wowAmountParam != nullptr);
    jassert(flutterAmountParam != nullptr);
    jassert(outputGainParam != nullptr);
    jassert(biasParam != nullptr);
    jassert(calibrationParam != nullptr);
    jassert(oversamplingParam != nullptr);

    // Log error if any critical parameter is missing (helps debug in release builds)
    if (!tapeMachineParam || !tapeSpeedParam || !tapeTypeParam || !inputGainParam ||
        !highpassFreqParam || !lowpassFreqParam || !noiseAmountParam || !noiseEnabledParam ||
        !wowAmountParam || !flutterAmountParam || !outputGainParam || !biasParam ||
        !calibrationParam || !oversamplingParam)
    {
        DBG("TapeMachine: CRITICAL ERROR - One or more parameters failed to initialize!");
    }

    tapeEmulationLeft = std::make_unique<ImprovedTapeEmulation>();
    tapeEmulationRight = std::make_unique<ImprovedTapeEmulation>();

    // Initialize shared wow/flutter for stereo coherence
    sharedWowFlutter = std::make_unique<WowFlutterProcessor>();
}

TapeMachineAudioProcessor::~TapeMachineAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TapeMachineAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeMachine", "Tape Machine",
        juce::StringArray{"Swiss 800", "Classic 102"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeSpeed", "Tape Speed",
        juce::StringArray{"7.5 IPS", "15 IPS", "30 IPS"},
        1));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "tapeType", "Tape Type",
        juce::StringArray{"Type 456", "Type GP9", "Type 911", "Type 250"},
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
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,  // 50% = optimal bias calibration
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

    // Using AudioParameterChoice instead of AudioParameterBool for better state persistence
    // AudioParameterBool can have issues with state restoration in some hosts
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "noiseEnabled", "Noise Enabled",
        juce::StringArray{"Off", "On"}, 0));

    // Separate Wow and Flutter controls for more creative flexibility
    // Wow: Slow pitch drift (0.3-0.8 Hz) - creates vinyl-like wobble
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wowAmount", "Wow",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 7.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    // Flutter: Faster pitch modulation (3-7 Hz) - tape machine character
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flutterAmount", "Flutter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 3.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " dB"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    // Using AudioParameterChoice instead of AudioParameterBool for better state persistence
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "autoComp", "Auto Compensation",
        juce::StringArray{"Off", "On"}, 1));  // Default to On

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
    return static_cast<int>(TapeMachinePresets::getFactoryPresets().size()) + 1;  // +1 for "Default"
}

int TapeMachineAudioProcessor::getCurrentProgram()
{
    return currentPresetIndex;
}

void TapeMachineAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;

    currentPresetIndex = index;

    if (index == 0)
    {
        // Default preset - keeps current values
        // User can manually adjust parameters from here
        return;
    }

    // Apply factory preset (index - 1 because 0 is "Default")
    auto presets = TapeMachinePresets::getFactoryPresets();
    if (index - 1 < static_cast<int>(presets.size()))
    {
        TapeMachinePresets::applyPreset(presets[static_cast<size_t>(index - 1)], apvts);
    }
}
const juce::String TapeMachineAudioProcessor::getProgramName (int index)
{
    if (index == 0)
        return "Default";

    auto presets = TapeMachinePresets::getFactoryPresets();
    if (index - 1 < static_cast<int>(presets.size()))
        return presets[static_cast<size_t>(index - 1)].name;

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
        // Ensure we have at least 2 channels for the oversampler
        size_t numChannels = static_cast<size_t>(std::max(2, getTotalNumInputChannels()));

        // Create both 2x and 4x oversamplers using high-quality FIR equiripple filters
        // FIR provides superior alias rejection compared to IIR, essential for tape saturation
        oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
            numChannels, 1,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

        oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
            numChannels, 2,
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
    smoothedWow.reset(sampleRate, rampTimeMs * 0.001f);
    smoothedFlutter.reset(sampleRate, rampTimeMs * 0.001f);

    // IMPORTANT: Set initial values to prevent jumps on first buffer
    // Calculate initial saturation from input gain parameter
    if (inputGainParam)
    {
        float inputGainDB = inputGainParam->load();
        float initialSaturation = juce::jlimit(0.0f, 100.0f, ((inputGainDB + 12.0f) / 24.0f) * 100.0f);
        smoothedSaturation.setCurrentAndTargetValue(initialSaturation);
    }
    if (noiseAmountParam)
        smoothedNoiseAmount.setCurrentAndTargetValue(noiseAmountParam->load() * 0.01f);
    if (wowAmountParam)
        smoothedWow.setCurrentAndTargetValue(wowAmountParam->load());
    if (flutterAmountParam)
        smoothedFlutter.setCurrentAndTargetValue(flutterAmountParam->load());

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

    // Critical safety check - if parameters failed to initialize, output silence
    // This should never happen if constructor validation passed, but provides safety
    if (!tapeMachineParam || !tapeSpeedParam || !tapeTypeParam || !inputGainParam ||
        !highpassFreqParam || !lowpassFreqParam ||
        !noiseAmountParam || !noiseEnabledParam || !wowAmountParam || !flutterAmountParam ||
        !outputGainParam || !biasParam || !calibrationParam || !oversamplingParam)
    {
        jassertfalse; // Alert during debug builds - this indicates a configuration error
        buffer.clear();  // Output silence rather than unprocessed audio
        return;
    }

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0)
        return;

    // For mono processing: check the configured bus layout, not buffer channel count
    // DAWs often send stereo buffers even for mono tracks (duplicated across channels)
    const auto inputBus = getBusesLayout().getMainInputChannelSet();
    const bool isMono = (inputBus == juce::AudioChannelSet::mono());
    isMonoInput.store(isMono, std::memory_order_relaxed);  // Track mono state for VU meter display

    // If buffer has only 1 channel, duplicate it for stereo processing
    if (buffer.getNumChannels() == 1)
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
            isProcessingAudio.store(posInfo.isPlaying || posInfo.isRecording, std::memory_order_relaxed);
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

    // =========================================================================
    // SATURATION DEPTH CALCULATION - Driven by Input Gain
    // =========================================================================
    // Input gain (-12 to +12 dB) controls how hard we "drive" the virtual tape.
    // This maps directly to saturation depth (0-100%):
    //   -12 dB input → 0% saturation (clean, transparent)
    //     0 dB input → 50% saturation (moderate warmth, like +0 VU)
    //   +12 dB input → 100% saturation (heavy, like +6 VU on hot tape)
    //
    // This relationship mirrors real tape machines where hotter input levels
    // push the tape into saturation, adding harmonic content and compression.
    // The saturation amount controls:
    //   - Harmonic generation (H2/H3 levels in ImprovedTapeEmulation)
    //   - Soft compression characteristics
    //   - Overall "tape warmth"
    const float saturationAmount = juce::jlimit(0.0f, 100.0f, ((inputGainDB + 12.0f) / 24.0f) * 100.0f);
    smoothedSaturation.setTargetValue(saturationAmount);
    smoothedWow.setTargetValue(wowAmountParam->load());
    smoothedFlutter.setTargetValue(flutterAmountParam->load());
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

    // Use relaxed ordering - UI thread reads these for metering, no sync needed
    inputLevelL.store(rmsInputL, std::memory_order_relaxed);
    inputLevelR.store(rmsInputR, std::memory_order_relaxed);

    // Read oversampling parameter directly to handle real-time changes
    // Both oversamplers are pre-initialized in prepareToPlay, so we can switch instantly
    int oversamplingChoice = oversamplingParam ? static_cast<int>(oversamplingParam->load()) : 1;
    int requestedFactor = (oversamplingChoice == 0) ? 2 : 4;

    // Select the correct oversampler based on current setting
    auto* activeOversampler = (requestedFactor == 4) ? oversampler4x.get() : oversampler2x.get();

    // Safety check - if no oversampler available, clear output and return
    if (!activeOversampler)
    {
        buffer.clear();
        return;
    }

    // If oversampling factor changed, start a crossfade transition
    // This prevents clicks/pops when switching HQ modes during playback
    if (requestedFactor != currentOversamplingFactor)
    {
        // Start crossfade transition
        oversamplingTransitionActive = true;
        oversamplingTransitionSamples = OVERSAMPLING_CROSSFADE_SAMPLES;

        currentOversamplingFactor = requestedFactor;
        const double newOversampledRate = currentSampleRate * static_cast<double>(currentOversamplingFactor);
        currentOversampledRate = static_cast<float>(newOversampledRate);
        const int oversampledBlockSize = buffer.getNumSamples() * currentOversamplingFactor;

        // Re-prepare tape emulation with new oversampled rate
        // This updates filter coefficients (e.g., 18kHz cutoff at new sample rate)
        // Note: prepare() resets filter states - the crossfade handles the transition smoothly
        if (tapeEmulationLeft)
            tapeEmulationLeft->prepare(newOversampledRate, oversampledBlockSize);
        if (tapeEmulationRight)
            tapeEmulationRight->prepare(newOversampledRate, oversampledBlockSize);
        if (sharedWowFlutter)
            sharedWowFlutter->prepare(newOversampledRate);

        // Update latency for host PDC
        setLatencySamples(static_cast<int>(activeOversampler->getLatencyInSamples()));

        // Update processor chain filters with new oversampled rate
        updateFilters();
    }

    // Handle crossfade transition - apply fade-in envelope to prevent clicks
    // from filter state reset
    float crossfadeGain = 1.0f;
    if (oversamplingTransitionActive)
    {
        // Calculate crossfade progress (0 to 1)
        float progress = 1.0f - (static_cast<float>(oversamplingTransitionSamples) / OVERSAMPLING_CROSSFADE_SAMPLES);
        // Use smooth S-curve for fade: 3t² - 2t³
        crossfadeGain = progress * progress * (3.0f - 2.0f * progress);

        // Decrement remaining samples
        oversamplingTransitionSamples -= buffer.getNumSamples();
        if (oversamplingTransitionSamples <= 0)
        {
            oversamplingTransitionActive = false;
            oversamplingTransitionSamples = 0;
        }
    }

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

    // Use cached oversampled rate from prepareToPlay() for consistent DSP calculations
    // This ensures wow/flutter timing matches the filter configurations
    const double oversampledRate = static_cast<double>(currentOversampledRate);

    for (size_t i = 0; i < numSamples; ++i)
    {
        if (leftData && rightData)
        {
            // Get smoothed values per sample for zipper-free parameter changes
            const float currentSaturation = smoothedSaturation.getNextValue();
            const float currentWow = smoothedWow.getNextValue();
            const float currentFlutter = smoothedFlutter.getNextValue();
            const float currentNoiseAmount = smoothedNoiseAmount.getNextValue();

            // Calculate shared wow/flutter modulation once per sample for stereo coherence
            float sharedModulation = 0.0f;
            const float combinedWowFlutter = currentWow + currentFlutter;
            if (combinedWowFlutter > 0.0f && sharedWowFlutter)
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

                // Use separate wow and flutter amounts for independent control
                sharedModulation = sharedWowFlutter->calculateModulation(
                    currentWow * 0.01f,      // Wow amount (0-100% -> 0-1)
                    currentFlutter * 0.01f,  // Flutter amount (0-100% -> 0-1)
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
            // Combined wow+flutter used for the emulation's internal modulation scaling
            const float wowFlutterForEmulation = combinedWowFlutter * 0.01f;
            leftData[i] = tapeEmulationLeft->processSample(leftData[i],
                                                          emulationMachine,
                                                          emulationSpeed,
                                                          emulationType,
                                                          biasAmount,
                                                          currentSaturation * 0.01f,
                                                          wowFlutterForEmulation,
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
                                                            wowFlutterForEmulation,
                                                            noiseEnabled,
                                                            currentNoiseAmount * 100.0f,
                                                            &sharedModulation,
                                                            calibrationDb);
        }
    }

    // Apply crosstalk simulation (L/R channel bleed from tape head)
    // Real tape machines have subtle channel crosstalk, more pronounced in vintage machines
    if (leftData && rightData)
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

    // Apply crossfade gain if transitioning between oversampling modes
    // This smooths out the transient from filter state reset
    if (crossfadeGain < 1.0f)
    {
        buffer.applyGain(crossfadeGain);
    }

    // Calculate RMS output levels after processing (VU-accurate)
    const float* outputLeftChannel = buffer.getReadPointer(0);
    const float* outputRightChannel = buffer.getReadPointer(1);

    float sumSquaresOutL = 0.0f;
    float sumSquaresOutR = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sampleL = outputLeftChannel[i];
        float sampleR = outputRightChannel[i];
        sumSquaresOutL += sampleL * sampleL;
        sumSquaresOutR += sampleR * sampleR;
    }
    float rmsBlockOutL = std::sqrt(sumSquaresOutL / buffer.getNumSamples());
    float rmsBlockOutR = std::sqrt(sumSquaresOutR / buffer.getNumSamples());

    // VU meter ballistics: 300ms integration (exponential moving average)
    const float dtOut = buffer.getNumSamples() / currentSampleRate;
    const float tauOut = 0.3f;  // 300ms VU standard
    const float alphaOut = std::exp(-dtOut / tauOut);

    rmsOutputL = alphaOut * rmsOutputL + (1.0f - alphaOut) * rmsBlockOutL;
    rmsOutputR = alphaOut * rmsOutputR + (1.0f - alphaOut) * rmsBlockOutR;

    outputLevelL.store(rmsOutputL, std::memory_order_relaxed);
    outputLevelR.store(rmsOutputR, std::memory_order_relaxed);

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
    {
        juce::ValueTree restoredState = juce::ValueTree::fromXml(*xmlState);

        if (restoredState.isValid())
        {
            apvts.replaceState(restoredState);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeMachineAudioProcessor();
}
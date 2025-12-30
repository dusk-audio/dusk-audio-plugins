/*
  ==============================================================================

    PluginProcessor.cpp
    Tape Echo - RE-201 Style Tape Delay Plugin

    Copyright (c) 2025 Luna Co. Audio - All rights reserved.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

TapeEchoProcessor::TapeEchoProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Cache parameter pointers
    inputGainParam = apvts.getRawParameterValue("inputGain");
    repeatRateParam = apvts.getRawParameterValue("repeatRate");
    intensityParam = apvts.getRawParameterValue("intensity");
    echoVolumeParam = apvts.getRawParameterValue("echoVolume");
    reverbVolumeParam = apvts.getRawParameterValue("reverbVolume");
    modeParam = apvts.getRawParameterValue("mode");
    bassParam = apvts.getRawParameterValue("bass");
    trebleParam = apvts.getRawParameterValue("treble");
    wowFlutterParam = apvts.getRawParameterValue("wowFlutter");
    dryWetParam = apvts.getRawParameterValue("dryWet");
    tempoSyncParam = apvts.getRawParameterValue("tempoSync");
    noteDivisionParam = apvts.getRawParameterValue("noteDivision");
}

TapeEchoProcessor::~TapeEchoProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TapeEchoProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Input Gain (-12 to +12 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "inputGain", 1 },
        "Input Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Repeat Rate (0.5x to 2.0x tape speed)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "repeatRate", 1 },
        "Repeat Rate",
        juce::NormalisableRange<float>(0.5f, 2.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("x")));

    // Intensity (Feedback) (0% to 110%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "intensity", 1 },
        "Intensity",
        juce::NormalisableRange<float>(0.0f, 110.0f, 0.1f),
        30.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Echo Volume (-60 to 0 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "echoVolume", 1 },
        "Echo Volume",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f),
        -6.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Reverb Volume (-60 to 0 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "reverbVolume", 1 },
        "Reverb Volume",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f),
        -12.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Mode (1-12)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "mode", 1 },
        "Mode",
        1, 12, 1));

    // Bass (-6 to +6 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "bass", 1 },
        "Bass",
        juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Treble (-6 to +6 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "treble", 1 },
        "Treble",
        juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Wow/Flutter (0% to 100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "wowFlutter", 1 },
        "Wow/Flutter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Dry/Wet (0% to 100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "dryWet", 1 },
        "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Tempo Sync (on/off)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "tempoSync", 1 },
        "Tempo Sync",
        false));

    // Note Division (0-14: 1/1, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/1D, 1/2D, 1/4D, 1/8D)
    juce::StringArray noteDivisions = {
        "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T",
        "1/16", "1/16T", "1/32", "1/32T", "1/1D", "1/2D", "1/4D", "1/8D"
    };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "noteDivision", 1 },
        "Note Division",
        noteDivisions,
        5));  // Default to 1/8 note

    return { params.begin(), params.end() };
}

const juce::String TapeEchoProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TapeEchoProcessor::acceptsMidi() const { return false; }
bool TapeEchoProcessor::producesMidi() const { return false; }
bool TapeEchoProcessor::isMidiEffect() const { return false; }

double TapeEchoProcessor::getTailLengthSeconds() const
{
    return 3.0;  // Account for reverb and echo decay
}

int TapeEchoProcessor::getNumPrograms() { return 1; }
int TapeEchoProcessor::getCurrentProgram() { return 0; }
void TapeEchoProcessor::setCurrentProgram(int) {}
const juce::String TapeEchoProcessor::getProgramName(int) { return {}; }
void TapeEchoProcessor::changeProgramName(int, const juce::String&) {}

void TapeEchoProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Acquire lock to prevent processBlock from running
    const juce::SpinLock::ScopedLockType scopedLock(processLock);

    // Mark as not ready while preparing
    readyToProcess.store(false, std::memory_order_release);

    currentSampleRate = sampleRate;

    // Initialize oversampling (2x) - but don't use it for now to debug
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampling->initProcessing(static_cast<size_t>(samplesPerBlock));

    // Prepare DSP engines at native sample rate (no oversampling for now)
    echoEngine.prepare(sampleRate, samplesPerBlock);
    springReverb.prepare(sampleRate, samplesPerBlock);

    // Prepare tone filters at normal rate
    bassFilterL.reset();
    bassFilterR.reset();
    trebleFilterL.reset();
    trebleFilterR.reset();
    updateToneFilters();

    // Smoothed parameters
    smoothedInputGain.reset(sampleRate, 0.02);
    smoothedEchoVolume.reset(sampleRate, 0.02);
    smoothedReverbVolume.reset(sampleRate, 0.02);
    smoothedDryWet.reset(sampleRate, 0.02);
    smoothedBass.reset(sampleRate, 0.02);
    smoothedTreble.reset(sampleRate, 0.02);

    // Now ready to process
    readyToProcess.store(true, std::memory_order_release);
}

void TapeEchoProcessor::releaseResources()
{
    readyToProcess.store(false, std::memory_order_release);
    oversampling.reset();
}

bool TapeEchoProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void TapeEchoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
        return;

    // Try to acquire lock - if we can't, just clear and return
    const juce::SpinLock::ScopedTryLockType tryLock(processLock);
    if (!tryLock.isLocked())
    {
        buffer.clear();
        return;
    }

    // Safety check for initialization - most important check first
    if (!readyToProcess.load(std::memory_order_acquire))
    {
        buffer.clear();
        return;
    }

    // Additional safety check for components
    if (!oversampling || !echoEngine.isPrepared() || !springReverb.isPrepared())
    {
        buffer.clear();
        return;
    }

    // Get current parameter values
    const float inputGainDb = inputGainParam->load();
    const float repeatRate = repeatRateParam->load();
    const float intensity = intensityParam->load() / 100.0f;  // Convert to 0-1.1 range
    const float echoVolumeDb = echoVolumeParam->load();
    const float reverbVolumeDb = reverbVolumeParam->load();
    const int mode = static_cast<int>(modeParam->load());
    const float bassDb = bassParam->load();
    const float trebleDb = trebleParam->load();
    const float wowFlutter = wowFlutterParam->load() / 100.0f;  // Convert to 0-1 range
    const float dryWet = dryWetParam->load() / 100.0f;  // Convert to 0-1 range
    const bool tempoSync = tempoSyncParam->load() > 0.5f;
    const int noteDivision = static_cast<int>(noteDivisionParam->load());

    // Get tempo and transport state from DAW playhead
    double currentBpm = 120.0;  // Default BPM
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            // Set processing flag based on DAW transport state (for reel animation)
            bool isPlaying = position->getIsPlaying();
            bool isRecording = position->getIsRecording();
            isProcessingAudio.store(isPlaying || isRecording, std::memory_order_relaxed);

            if (auto bpm = position->getBpm())
            {
                currentBpm = *bpm;
                lastBpm = currentBpm;
            }
            else
            {
                currentBpm = lastBpm;  // Use last known BPM if not available
            }
        }
    }

    // Update tempo sync parameters
    echoEngine.setTempoSync(tempoSync);
    if (tempoSync)
    {
        float delayTimeMs = getDelayTimeForNoteDivision(noteDivision, currentBpm);
        echoEngine.setSyncDelayTimeMs(delayTimeMs);
    }

    // Update smoothed values
    smoothedInputGain.setTargetValue(juce::Decibels::decibelsToGain(inputGainDb));
    smoothedEchoVolume.setTargetValue(juce::Decibels::decibelsToGain(echoVolumeDb));
    smoothedReverbVolume.setTargetValue(juce::Decibels::decibelsToGain(reverbVolumeDb));
    smoothedDryWet.setTargetValue(dryWet);
    smoothedBass.setTargetValue(bassDb);
    smoothedTreble.setTargetValue(trebleDb);

    // Update mode if changed
    if (mode != lastMode)
    {
        echoEngine.setMode(mode);
        lastMode = mode;
        currentMode.store(mode);

        // Update head active state for thread-safe UI access
        for (size_t i = 0; i < 3; ++i)
            headActiveState[i].store(echoEngine.isHeadActive(static_cast<int>(i)));
    }

    // Update tape speed and other parameters
    echoEngine.setSpeed(repeatRate);
    echoEngine.setFeedback(intensity);
    echoEngine.setWowFlutterAmount(wowFlutter);
    echoEngine.setSaturationDrive(intensity * 0.5f);  // Drive based on intensity

    // Store visualization state
    currentTapeSpeed.store(repeatRate);
    feedbackLevel.store(intensity);

    // Input metering
    float inL = buffer.getMagnitude(0, 0, numSamples);
    float inR = numChannels > 1 ? buffer.getMagnitude(1, 0, numSamples) : inL;
    inputLevelL.store(inL);
    inputLevelR.store(inR);

    // Store dry signal
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    // Apply input gain
    for (int i = 0; i < numSamples; ++i)
    {
        const float gain = smoothedInputGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.setSample(ch, i, buffer.getSample(ch, i) * gain);
        }
    }

    // Create temp buffers for wet signals (no oversampling for now)
    juce::AudioBuffer<float> echoBuffer(numChannels, numSamples);
    juce::AudioBuffer<float> reverbBuffer(numChannels, numSamples);

    // Copy to echo buffer
    for (int ch = 0; ch < numChannels; ++ch)
    {
        juce::FloatVectorOperations::copy(
            echoBuffer.getWritePointer(ch),
            buffer.getReadPointer(ch),
            numSamples);
    }

    // Copy to reverb buffer
    for (int ch = 0; ch < numChannels; ++ch)
    {
        juce::FloatVectorOperations::copy(
            reverbBuffer.getWritePointer(ch),
            buffer.getReadPointer(ch),
            numSamples);
    }

    // Process echo
    echoEngine.process(echoBuffer);

    // Process spring reverb
    springReverb.process(reverbBuffer);

    // Mix echo and reverb into output
    const float echoGain = smoothedEchoVolume.getCurrentValue();
    const float reverbGain = smoothedReverbVolume.getCurrentValue();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* dest = buffer.getWritePointer(ch);
        const float* echoSrc = echoBuffer.getReadPointer(ch);
        const float* reverbSrc = reverbBuffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            dest[i] = echoSrc[i] * echoGain + reverbSrc[i] * reverbGain;
        }
    }

    // Apply tone controls
    bool bassChanged = std::abs(smoothedBass.getCurrentValue() - smoothedBass.getTargetValue()) > 0.01f;
    bool trebleChanged = std::abs(smoothedTreble.getCurrentValue() - smoothedTreble.getTargetValue()) > 0.01f;
    if (bassChanged || trebleChanged)
    {
        updateToneFilters();
    }

    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        leftChannel[i] = bassFilterL.processSample(leftChannel[i]);
        leftChannel[i] = trebleFilterL.processSample(leftChannel[i]);

        if (rightChannel)
        {
            rightChannel[i] = bassFilterR.processSample(rightChannel[i]);
            rightChannel[i] = trebleFilterR.processSample(rightChannel[i]);
        }
    }

    // Mix dry/wet
    const float wet = smoothedDryWet.getCurrentValue();
    const float dry = 1.0f - wet;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* drySrc = dryBuffer.getReadPointer(ch);
        float* dest = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            dest[i] = drySrc[i] * dry + dest[i] * wet;
        }
    }

    // Output metering
    float outL = buffer.getMagnitude(0, 0, numSamples);
    float outR = numChannels > 1 ? buffer.getMagnitude(1, 0, numSamples) : outL;
    outputLevelL.store(outL);
    outputLevelR.store(outR);
}

void TapeEchoProcessor::updateToneFilters()
{
    const float bass = smoothedBass.getCurrentValue();
    const float treble = smoothedTreble.getCurrentValue();

    // Bass: Low shelf at 200Hz
    float bassGain = juce::Decibels::decibelsToGain(bass);
    auto bassCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 200.0f, 0.707f, bassGain);
    *bassFilterL.coefficients = *bassCoeffs;
    *bassFilterR.coefficients = *bassCoeffs;

    // Treble: High shelf at 3kHz
    float trebleGain = juce::Decibels::decibelsToGain(treble);
    auto trebleCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 3000.0f, 0.707f, trebleGain);
    *trebleFilterL.coefficients = *trebleCoeffs;
    *trebleFilterR.coefficients = *trebleCoeffs;
}

bool TapeEchoProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TapeEchoProcessor::createEditor()
{
    return new TapeEchoEditor(*this);
}

void TapeEchoProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void TapeEchoProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeEchoProcessor();
}

float TapeEchoProcessor::getDelayTimeForNoteDivision(int division, double bpm)
{
    // Quarter note duration in ms
    const double quarterNoteMs = 60000.0 / bpm;

    // Note divisions: 0=1/1, 1=1/2, 2=1/2T, 3=1/4, 4=1/4T, 5=1/8, 6=1/8T,
    //                 7=1/16, 8=1/16T, 9=1/32, 10=1/32T, 11=1/1D, 12=1/2D, 13=1/4D, 14=1/8D
    switch (division)
    {
        case 0:  return static_cast<float>(quarterNoteMs * 4.0);      // 1/1 (whole)
        case 1:  return static_cast<float>(quarterNoteMs * 2.0);      // 1/2 (half)
        case 2:  return static_cast<float>(quarterNoteMs * 4.0 / 3.0); // 1/2T (half triplet)
        case 3:  return static_cast<float>(quarterNoteMs);            // 1/4 (quarter)
        case 4:  return static_cast<float>(quarterNoteMs * 2.0 / 3.0); // 1/4T (quarter triplet)
        case 5:  return static_cast<float>(quarterNoteMs / 2.0);      // 1/8 (eighth)
        case 6:  return static_cast<float>(quarterNoteMs / 3.0);      // 1/8T (eighth triplet)
        case 7:  return static_cast<float>(quarterNoteMs / 4.0);      // 1/16 (sixteenth)
        case 8:  return static_cast<float>(quarterNoteMs / 6.0);      // 1/16T (sixteenth triplet)
        case 9:  return static_cast<float>(quarterNoteMs / 8.0);      // 1/32 (thirty-second)
        case 10: return static_cast<float>(quarterNoteMs / 12.0);     // 1/32T (thirty-second triplet)
        case 11: return static_cast<float>(quarterNoteMs * 6.0);      // 1/1D (dotted whole)
        case 12: return static_cast<float>(quarterNoteMs * 3.0);      // 1/2D (dotted half)
        case 13: return static_cast<float>(quarterNoteMs * 1.5);      // 1/4D (dotted quarter)
        case 14: return static_cast<float>(quarterNoteMs * 0.75);     // 1/8D (dotted eighth)
        default: return static_cast<float>(quarterNoteMs / 2.0);      // Default to 1/8
    }
}

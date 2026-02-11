/*
  ==============================================================================

    Convolution Reverb - Plugin Processor
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AifcStreamWrapper.h"

//==============================================================================
ConvolutionReverbProcessor::ConvolutionReverbProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("ConvolutionReverb"), createParameterLayout())
{
    // Cache parameter pointers
    mixParam = parameters.getRawParameterValue("mix");
    preDelayParam = parameters.getRawParameterValue("predelay");
    attackParam = parameters.getRawParameterValue("attack");
    decayParam = parameters.getRawParameterValue("decay");
    lengthParam = parameters.getRawParameterValue("length");
    reverseParam = parameters.getRawParameterValue("reverse");
    widthParam = parameters.getRawParameterValue("width");
    hpfFreqParam = parameters.getRawParameterValue("hpf_freq");
    lpfFreqParam = parameters.getRawParameterValue("lpf_freq");
    eqLowFreqParam = parameters.getRawParameterValue("eq_low_freq");
    eqLowGainParam = parameters.getRawParameterValue("eq_low_gain");
    eqLowMidFreqParam = parameters.getRawParameterValue("eq_lmid_freq");
    eqLowMidGainParam = parameters.getRawParameterValue("eq_lmid_gain");
    eqHighMidFreqParam = parameters.getRawParameterValue("eq_hmid_freq");
    eqHighMidGainParam = parameters.getRawParameterValue("eq_hmid_gain");
    eqHighFreqParam = parameters.getRawParameterValue("eq_high_freq");
    eqHighGainParam = parameters.getRawParameterValue("eq_high_gain");
    zeroLatencyParam = parameters.getRawParameterValue("zero_latency");

    // New parameters
    irOffsetParam = parameters.getRawParameterValue("ir_offset");
    qualityParam = parameters.getRawParameterValue("quality");
    volumeCompParam = parameters.getRawParameterValue("volume_comp");
    filterEnvEnabledParam = parameters.getRawParameterValue("filter_env_enabled");
    filterEnvInitFreqParam = parameters.getRawParameterValue("filter_env_init_freq");
    filterEnvEndFreqParam = parameters.getRawParameterValue("filter_env_end_freq");
    filterEnvAttackParam = parameters.getRawParameterValue("filter_env_attack");
    stereoModeParam = parameters.getRawParameterValue("stereo_mode");

    // Set default IR directory
    customIRDirectory = getDefaultIRDirectory();
}

ConvolutionReverbProcessor::~ConvolutionReverbProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ConvolutionReverbProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Mix (dry/wet)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Pre-delay
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "predelay", "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Envelope controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decay", "Decay",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "length", "Length",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Reverse
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "reverse", "Reverse", false));

    // Stereo width
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "width", "Stereo Width",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f),
        1.0f));

    // Filters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hpf_freq", "HPF Frequency",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f),
        20.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lpf_freq", "LPF Frequency",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 1.0f, 0.3f),
        20000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // 4-band EQ
    // Low shelf
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eq_low_freq", "Low Freq",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eq_low_gain", "Low Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Low-mid peak
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eq_lmid_freq", "Lo-Mid Freq",
        juce::NormalisableRange<float>(200.0f, 2000.0f, 1.0f, 0.5f),
        600.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eq_lmid_gain", "Lo-Mid Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // High-mid peak
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eq_hmid_freq", "Hi-Mid Freq",
        juce::NormalisableRange<float>(1000.0f, 8000.0f, 1.0f, 0.5f),
        3000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eq_hmid_gain", "Hi-Mid Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // High shelf
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eq_high_freq", "High Freq",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 1.0f, 0.3f),
        8000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eq_high_gain", "High Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Latency mode
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "zero_latency", "Zero Latency", true));

    // IR Offset (0-100% of IR start position)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ir_offset", "IR Offset",
        juce::NormalisableRange<float>(0.0f, 0.5f, 0.01f),  // Max 50% offset
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Quality (sample rate divisor)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "quality", "Quality",
        juce::StringArray{"Lo-Fi", "Low", "Medium", "High"},
        2));  // Default: Medium

    // Volume Compensation
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "volume_comp", "Volume Compensation", true));

    // Filter Envelope
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "filter_env_enabled", "Filter Envelope", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filter_env_init_freq", "Filter Init Freq",
        juce::NormalisableRange<float>(200.0f, 20000.0f, 1.0f, 0.3f),
        20000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filter_env_end_freq", "Filter End Freq",
        juce::NormalisableRange<float>(200.0f, 20000.0f, 1.0f, 0.3f),
        2000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filter_env_attack", "Filter Attack",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.3f));  // 30% of IR length for filter attack

    // Stereo Mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "stereo_mode", "Stereo Mode",
        juce::StringArray{"True Stereo", "Mono-to-Stereo"},
        0));  // Default: True Stereo

    return {params.begin(), params.end()};
}

//==============================================================================
const juce::String ConvolutionReverbProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ConvolutionReverbProcessor::acceptsMidi() const { return false; }
bool ConvolutionReverbProcessor::producesMidi() const { return false; }
bool ConvolutionReverbProcessor::isMidiEffect() const { return false; }

double ConvolutionReverbProcessor::getTailLengthSeconds() const
{
    // Return the IR length plus pre-delay
    float lengthSec = getCurrentIRLengthSeconds();
    float preDelayMs = preDelayParam ? preDelayParam->load() : 0.0f;
    return static_cast<double>(lengthSec + preDelayMs / 1000.0f);
}

int ConvolutionReverbProcessor::getNumPrograms() { return 1; }
int ConvolutionReverbProcessor::getCurrentProgram() { return 0; }
void ConvolutionReverbProcessor::setCurrentProgram(int) {}
const juce::String ConvolutionReverbProcessor::getProgramName(int) { return {}; }
void ConvolutionReverbProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void ConvolutionReverbProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Prepare convolution engine
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    convolutionEngine.prepare(spec);

    // Prepare pre-delay (max 500ms)
    int maxDelaySamples = static_cast<int>(0.5 * sampleRate) + 1;
    preDelayL.setMaximumDelayInSamples(maxDelaySamples);
    preDelayR.setMaximumDelayInSamples(maxDelaySamples);
    preDelayL.prepare(spec);
    preDelayR.prepare(spec);

    // Prepare filters
    wetHighpass.prepare(spec);
    wetHighpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);

    wetLowpass.prepare(spec);
    wetLowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    // Prepare EQ
    wetEQ.prepare(spec);

    // Prepare buffers
    dryBuffer.setSize(2, samplesPerBlock);
    wetBuffer.setSize(2, samplesPerBlock);
}

void ConvolutionReverbProcessor::releaseResources()
{
    convolutionEngine.reset();
    preDelayL.reset();
    preDelayR.reset();
    wetHighpass.reset();
    wetLowpass.reset();
    wetEQ.reset();
}

bool ConvolutionReverbProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo() &&
        layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono())
        return false;

    return true;
}

void ConvolutionReverbProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    int numSamples = buffer.getNumSamples();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Get parameters
    float mix = mixParam->load();
    float preDelayMs = preDelayParam->load();
    float width = widthParam->load();
    float hpfFreq = hpfFreqParam->load();
    float lpfFreq = lpfFreqParam->load();

    // Update envelope processor
    envelopeProcessor.setAttack(attackParam->load());
    envelopeProcessor.setDecay(decayParam->load());
    envelopeProcessor.setLength(lengthParam->load());

    // Update convolution engine settings
    convolutionEngine.setReverse(reverseParam->load() > 0.5f);
    convolutionEngine.setZeroLatency(zeroLatencyParam->load() > 0.5f);

    // Update new parameters
    convolutionEngine.setIROffset(irOffsetParam->load());
    convolutionEngine.setQuality(static_cast<ConvolutionEngine::Quality>(static_cast<int>(qualityParam->load())));
    convolutionEngine.setVolumeCompensation(volumeCompParam->load() > 0.5f);

    // Update filter envelope
    convolutionEngine.setFilterEnvelopeEnabled(filterEnvEnabledParam->load() > 0.5f);
    convolutionEngine.setFilterEnvelopeParams(
        filterEnvInitFreqParam->load(),
        filterEnvEndFreqParam->load(),
        filterEnvAttackParam->load()
    );

    // Update stereo mode
    convolutionEngine.setStereoMode(static_cast<ConvolutionEngine::StereoMode>(
        static_cast<int>(stereoModeParam->load())));

    // Update filters
    wetHighpass.setCutoffFrequency(hpfFreq);
    wetLowpass.setCutoffFrequency(lpfFreq);

    // Update EQ
    wetEQ.setLowShelf(eqLowFreqParam->load(), eqLowGainParam->load());
    wetEQ.setLowMid(eqLowMidFreqParam->load(), eqLowMidGainParam->load());
    wetEQ.setHighMid(eqHighMidFreqParam->load(), eqHighMidGainParam->load());
    wetEQ.setHighShelf(eqHighFreqParam->load(), eqHighGainParam->load());

    // Calculate input level (mono max and stereo L/R)
    inputMeter.store(calculateRMS(buffer), std::memory_order_relaxed);
    inputMeterL.store(calculateChannelRMS(buffer, 0), std::memory_order_relaxed);
    inputMeterR.store(calculateChannelRMS(buffer, buffer.getNumChannels() > 1 ? 1 : 0), std::memory_order_relaxed);

    // Store dry signal
    dryBuffer.makeCopyOf(buffer, true);

    // Copy to wet buffer for processing
    wetBuffer.makeCopyOf(buffer, true);

    // Apply pre-delay
    if (preDelayMs > 0.0f)
    {
        float delaySamples = preDelayMs * 0.001f * static_cast<float>(currentSampleRate);
        preDelayL.setDelay(delaySamples);
        preDelayR.setDelay(delaySamples);

        auto* leftChannel = wetBuffer.getWritePointer(0);
        auto* rightChannel = wetBuffer.getNumChannels() > 1 ? wetBuffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            leftChannel[i] = preDelayL.popSample(0, delaySamples);
            preDelayL.pushSample(0, buffer.getSample(0, i));

            if (rightChannel != nullptr)
            {
                rightChannel[i] = preDelayR.popSample(0, delaySamples);
                preDelayR.pushSample(0, buffer.getSample(1, i));
            }
        }
    }

    // Process convolution (if IR is loaded)
    if (irLoaded.load())
    {
        // Pass dry buffer for transient detection (filter envelope reset)
        convolutionEngine.processBlock(wetBuffer, envelopeProcessor, &dryBuffer);
    }
    else
    {
        wetBuffer.clear();
    }

    // Apply filters to wet signal
    juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
    juce::dsp::ProcessContextReplacing<float> wetContext(wetBlock);
    wetHighpass.process(wetContext);
    wetLowpass.process(wetContext);

    // Apply EQ to wet signal
    wetEQ.processBlock(wetBuffer);

    // Apply stereo width
    applyWidth(wetBuffer, width);

    // Mix dry and wet
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* outputData = buffer.getWritePointer(channel);
        const auto* dryData = dryBuffer.getReadPointer(channel);
        const auto* wetData = wetBuffer.getReadPointer(channel);

        for (int i = 0; i < numSamples; ++i)
        {
            outputData[i] = dryData[i] * (1.0f - mix) + wetData[i] * mix;
        }
    }

    // Calculate output level (mono max and stereo L/R)
    outputMeter.store(calculateRMS(buffer), std::memory_order_relaxed);
    outputMeterL.store(calculateChannelRMS(buffer, 0), std::memory_order_relaxed);
    outputMeterR.store(calculateChannelRMS(buffer, buffer.getNumChannels() > 1 ? 1 : 0), std::memory_order_relaxed);
}

//==============================================================================
void ConvolutionReverbProcessor::applyWidth(juce::AudioBuffer<float>& buffer, float width)
{
    if (buffer.getNumChannels() < 2 || std::abs(width - 1.0f) < 0.001f)
        return;

    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);
    int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        float mid = (leftChannel[i] + rightChannel[i]) * 0.5f;
        float side = (leftChannel[i] - rightChannel[i]) * 0.5f;

        side *= width;

        leftChannel[i] = mid + side;
        rightChannel[i] = mid - side;
    }
}

float ConvolutionReverbProcessor::calculateRMS(const juce::AudioBuffer<float>& buffer)
{
    float sum = 0.0f;
    int totalSamples = 0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            sum += data[i] * data[i];
            ++totalSamples;
        }
    }

    if (totalSamples == 0)
        return -60.0f;

    float rms = std::sqrt(sum / static_cast<float>(totalSamples));
    float dB = 20.0f * std::log10(std::max(rms, 1e-6f));
    return juce::jlimit(-60.0f, 6.0f, dB);
}

float ConvolutionReverbProcessor::calculateChannelRMS(const juce::AudioBuffer<float>& buffer, int channel)
{
    if (channel >= buffer.getNumChannels())
        return -60.0f;

    const auto* data = buffer.getReadPointer(channel);
    int numSamples = buffer.getNumSamples();

    if (numSamples == 0)
        return -60.0f;

    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        sum += data[i] * data[i];
    }

    float rms = std::sqrt(sum / static_cast<float>(numSamples));
    float dB = 20.0f * std::log10(std::max(rms, 1e-6f));
    return juce::jlimit(-60.0f, 6.0f, dB);
}

//==============================================================================
void ConvolutionReverbProcessor::loadImpulseResponse(const juce::File& irFile)
{
    if (!irFile.existsAsFile())
        return;

    // Load the IR file for display
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    // Use createReaderForAudioFile which handles AIFC files with non-standard
    // compression types like 'in24' (used by Space Designer .SDIR files)
    std::unique_ptr<juce::AudioFormatReader> reader = createReaderForAudioFile(formatManager, irFile);

    if (reader != nullptr)
    {
        // Read IR data
        juce::AudioBuffer<float> tempBuffer(static_cast<int>(reader->numChannels),
                                             static_cast<int>(reader->lengthInSamples));
        reader->read(&tempBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

        // Store for display (thread-safe)
        {
            const juce::ScopedLock lock(irLock);
            currentIRWaveform = std::move(tempBuffer);
            currentIRSampleRate = reader->sampleRate;
            currentIRName = irFile.getFileNameWithoutExtension();
            currentIRPath = irFile.getFullPathName();
        }

        // Load into convolution engine
        convolutionEngine.loadImpulseResponse(irFile, currentSampleRate);

        irLoaded.store(true, std::memory_order_release);
    }
}

void ConvolutionReverbProcessor::clearImpulseResponse()
{
    const juce::ScopedLock lock(irLock);
    currentIRWaveform.setSize(0, 0);
    currentIRName.clear();
    currentIRPath.clear();
    irLoaded.store(false, std::memory_order_release);
    convolutionEngine.reset();
}

juce::String ConvolutionReverbProcessor::getCurrentIRName() const
{
    const juce::ScopedLock lock(irLock);
    return currentIRName;
}

juce::String ConvolutionReverbProcessor::getCurrentIRPath() const
{
    const juce::ScopedLock lock(irLock);
    return currentIRPath;
}

float ConvolutionReverbProcessor::getCurrentIRLengthSeconds() const
{
    const juce::ScopedLock lock(irLock);
    if (currentIRWaveform.getNumSamples() == 0 || currentIRSampleRate <= 0)
        return 0.0f;
    return static_cast<float>(currentIRWaveform.getNumSamples()) / static_cast<float>(currentIRSampleRate);
}

juce::AudioBuffer<float> ConvolutionReverbProcessor::getCurrentIRWaveform() const
{
    const juce::ScopedLock lock(irLock);
    return currentIRWaveform;
}

double ConvolutionReverbProcessor::getCurrentIRSampleRate() const
{
    const juce::ScopedLock lock(irLock);
    return currentIRSampleRate;
}

juce::File ConvolutionReverbProcessor::getDefaultIRDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".local/share/DuskAudio/IRs");
}

juce::File ConvolutionReverbProcessor::getCustomIRDirectory() const
{
    return customIRDirectory;
}

//==============================================================================
bool ConvolutionReverbProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ConvolutionReverbProcessor::createEditor()
{
    return new ConvolutionReverbEditor(*this);
}

//==============================================================================
void ConvolutionReverbProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    // Add custom properties
    state.setProperty("irPath", getCurrentIRPath(), nullptr);
    state.setProperty("customIRDirectory", customIRDirectory.getFullPathName(), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ConvolutionReverbProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        auto state = juce::ValueTree::fromXml(*xmlState);

        // Restore APVTS parameters
        parameters.replaceState(state);

        // Restore custom properties
        juce::String irPath = state.getProperty("irPath", "");
        juce::String customDir = state.getProperty("customIRDirectory", "");

        // Restore custom IR directory
        if (customDir.isNotEmpty())
        {
            juce::File dir(customDir);
            if (dir.exists() && dir.isDirectory())
                customIRDirectory = dir;
        }

        // Reload IR if path exists
        if (irPath.isNotEmpty())
        {
            juce::File irFile(irPath);
            if (irFile.existsAsFile())
            {
                // Use a message thread callback to load IR after initialization
                // Use WeakReference to avoid use-after-free if processor is destroyed
                juce::WeakReference<ConvolutionReverbProcessor> weakThis(this);
                juce::MessageManager::callAsync([weakThis, irFile]()
                {
                    if (weakThis != nullptr)
                        weakThis->loadImpulseResponse(irFile);
                });
            }
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ConvolutionReverbProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpectrumAnalyzerProcessor::SpectrumAnalyzerProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, juce::Identifier("SpectrumAnalyzerState"),
            createParameterLayout())
{
    // Register parameter listeners
    apvts.addParameterListener(PARAM_CHANNEL_MODE, this);
    apvts.addParameterListener(PARAM_FFT_RESOLUTION, this);
    apvts.addParameterListener(PARAM_SMOOTHING, this);
    apvts.addParameterListener(PARAM_SLOPE, this);
    apvts.addParameterListener(PARAM_DECAY_RATE, this);
    apvts.addParameterListener(PARAM_PEAK_HOLD, this);
    apvts.addParameterListener(PARAM_PEAK_HOLD_TIME, this);
    apvts.addParameterListener(PARAM_K_SYSTEM_TYPE, this);
}

SpectrumAnalyzerProcessor::~SpectrumAnalyzerProcessor()
{
    apvts.removeParameterListener(PARAM_CHANNEL_MODE, this);
    apvts.removeParameterListener(PARAM_FFT_RESOLUTION, this);
    apvts.removeParameterListener(PARAM_SMOOTHING, this);
    apvts.removeParameterListener(PARAM_SLOPE, this);
    apvts.removeParameterListener(PARAM_DECAY_RATE, this);
    apvts.removeParameterListener(PARAM_PEAK_HOLD, this);
    apvts.removeParameterListener(PARAM_PEAK_HOLD_TIME, this);
    apvts.removeParameterListener(PARAM_K_SYSTEM_TYPE, this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SpectrumAnalyzerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Channel mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(PARAM_CHANNEL_MODE, 1),
        "Channel Mode",
        juce::StringArray{"Stereo", "Mono", "Mid", "Side"},
        0));

    // FFT Resolution
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(PARAM_FFT_RESOLUTION, 1),
        "FFT Resolution",
        juce::StringArray{"2048", "4096", "8192"},
        1));  // Default 4096

    // Smoothing
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(PARAM_SMOOTHING, 1),
        "Smoothing",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f));

    // Slope (default 0.0 for flat response)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(PARAM_SLOPE, 1),
        "Slope",
        juce::NormalisableRange<float>(-4.5f, 4.5f, 0.5f),
        0.0f));

    // Decay rate
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(PARAM_DECAY_RATE, 1),
        "Decay Rate",
        juce::NormalisableRange<float>(3.0f, 60.0f, 1.0f),
        20.0f));

    // Peak hold
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(PARAM_PEAK_HOLD, 1),
        "Peak Hold",
        true));

    // Peak hold time
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(PARAM_PEAK_HOLD_TIME, 1),
        "Peak Hold Time",
        juce::NormalisableRange<float>(0.5f, 10.0f, 0.1f),
        2.0f));

    // Display range min
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(PARAM_DISPLAY_MIN, 1),
        "Display Min",
        juce::NormalisableRange<float>(-100.0f, -30.0f, 1.0f),
        -60.0f));

    // Display range max
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(PARAM_DISPLAY_MAX, 1),
        "Display Max",
        juce::NormalisableRange<float>(0.0f, 12.0f, 1.0f),
        6.0f));

    // K-System type
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(PARAM_K_SYSTEM_TYPE, 1),
        "K-System Type",
        juce::StringArray{"K-12", "K-14", "K-20"},
        1));  // Default K-14

    return {params.begin(), params.end()};
}

//==============================================================================
void SpectrumAnalyzerProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == PARAM_CHANNEL_MODE)
    {
        channelRouter.setMode(static_cast<ChannelRouter::Mode>(static_cast<int>(newValue)));
    }
    else if (parameterID == PARAM_FFT_RESOLUTION)
    {
        int resIndex = static_cast<int>(newValue);
        FFTProcessor::Resolution res;
        switch (resIndex)
        {
            case 0: res = FFTProcessor::Resolution::Low; break;
            case 1: res = FFTProcessor::Resolution::Medium; break;
            case 2: res = FFTProcessor::Resolution::High; break;
            default: res = FFTProcessor::Resolution::Medium;
        }
        fftProcessor.setResolution(res);
    }
    else if (parameterID == PARAM_SMOOTHING)
    {
        fftProcessor.setSmoothing(newValue);
    }
    else if (parameterID == PARAM_SLOPE)
    {
        fftProcessor.setSlope(newValue);
    }
    else if (parameterID == PARAM_DECAY_RATE)
    {
        fftProcessor.setDecayRate(newValue);
    }
    else if (parameterID == PARAM_PEAK_HOLD)
    {
        fftProcessor.setPeakHoldEnabled(newValue > 0.5f);
    }
    else if (parameterID == PARAM_PEAK_HOLD_TIME)
    {
        fftProcessor.setPeakHoldTime(newValue);
    }
    else if (parameterID == PARAM_K_SYSTEM_TYPE)
    {
        kSystemMeter.setType(static_cast<KSystemMeter::Type>(static_cast<int>(newValue)));
    }
}

//==============================================================================
void SpectrumAnalyzerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Prepare all DSP components
    fftProcessor.prepare(sampleRate, samplesPerBlock);
    lufsMeter.prepare(sampleRate, 2);
    kSystemMeter.prepare(sampleRate);
    truePeakDetector.prepare(sampleRate, 2);
    correlationMeter.prepare(sampleRate);

    // Resize routing buffers
    routedL.resize(static_cast<size_t>(samplesPerBlock));
    routedR.resize(static_cast<size_t>(samplesPerBlock));

    // RMS decay coefficient (for 300ms integration at sample rate)
    float integrationTimeSec = 0.3f;
    float samplesForIntegration = static_cast<float>(sampleRate) * integrationTimeSec;
    rmsDecay = 1.0f - (1.0f / samplesForIntegration);

    // Initialize from current parameter values
    parameterChanged(PARAM_CHANNEL_MODE, *apvts.getRawParameterValue(PARAM_CHANNEL_MODE));
    parameterChanged(PARAM_FFT_RESOLUTION, *apvts.getRawParameterValue(PARAM_FFT_RESOLUTION));
    parameterChanged(PARAM_SMOOTHING, *apvts.getRawParameterValue(PARAM_SMOOTHING));
    parameterChanged(PARAM_SLOPE, *apvts.getRawParameterValue(PARAM_SLOPE));
    parameterChanged(PARAM_DECAY_RATE, *apvts.getRawParameterValue(PARAM_DECAY_RATE));
    parameterChanged(PARAM_PEAK_HOLD, *apvts.getRawParameterValue(PARAM_PEAK_HOLD));
    parameterChanged(PARAM_PEAK_HOLD_TIME, *apvts.getRawParameterValue(PARAM_PEAK_HOLD_TIME));
    parameterChanged(PARAM_K_SYSTEM_TYPE, *apvts.getRawParameterValue(PARAM_K_SYSTEM_TYPE));
}

void SpectrumAnalyzerProcessor::releaseResources()
{
    fftProcessor.reset();
    lufsMeter.reset();
    kSystemMeter.reset();
    truePeakDetector.reset();
    correlationMeter.reset();
}

//==============================================================================
void SpectrumAnalyzerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    int numSamples = buffer.getNumSamples();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Get input pointers
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = totalNumInputChannels > 1 ? buffer.getReadPointer(1) : inputL;

    // Apply channel routing
    channelRouter.process(inputL, inputR, routedL.data(), routedR.data(), numSamples);

    // Push to FFT processor
    fftProcessor.pushSamples(routedL.data(), routedR.data(), numSamples);

    // Process LUFS metering (always on original stereo input)
    lufsMeter.process(inputL, inputR, numSamples);

    // Process K-System metering
    kSystemMeter.process(inputL, inputR, numSamples);

    // Process true peak detection
    const float* channels[2] = {inputL, inputR};
    truePeakDetector.process(channels, numSamples);

    // Process correlation metering
    correlationMeter.process(inputL, inputR, numSamples);

    // Calculate output levels and RMS
    float peakL = 0.0f, peakR = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float absL = std::abs(inputL[i]);
        float absR = std::abs(inputR[i]);

        peakL = std::max(peakL, absL);
        peakR = std::max(peakR, absR);

        // RMS accumulation
        rmsAccumL = rmsAccumL * rmsDecay + (inputL[i] * inputL[i]) * (1.0f - rmsDecay);
        rmsAccumR = rmsAccumR * rmsDecay + (inputR[i] * inputR[i]) * (1.0f - rmsDecay);
    }

    // Convert to dB and store
    outputLevelL.store(juce::Decibels::gainToDecibels(peakL, -100.0f));
    outputLevelR.store(juce::Decibels::gainToDecibels(peakR, -100.0f));

    float rmsLinear = std::sqrt((rmsAccumL + rmsAccumR) * 0.5f);
    rmsLevel.store(juce::Decibels::gainToDecibels(rmsLinear, -100.0f));

    // This is an analyzer plugin - pass audio through unchanged
    // (already done since we didn't modify the buffer)
}

//==============================================================================
const juce::String SpectrumAnalyzerProcessor::getName() const
{
    return JucePlugin_Name;
}

//==============================================================================
void SpectrumAnalyzerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpectrumAnalyzerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* SpectrumAnalyzerProcessor::createEditor()
{
    return new SpectrumAnalyzerEditor(*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectrumAnalyzerProcessor();
}

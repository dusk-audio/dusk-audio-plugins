#include "FourKEQ.h"
#include "PluginEditor.h"
#include <cmath>

// Helper function to prevent frequency cramping at high frequencies
static float preWarpFrequency(float freq, double sampleRate)
{
    // Pre-warp frequency for bilinear transform to prevent cramping
    const float nyquist = sampleRate * 0.5f;

    // Standard pre-warping formula
    const float k = std::tan((M_PI * freq) / sampleRate);
    float warpedFreq = (sampleRate / M_PI) * std::atan(k);

    // Additional compensation for very high frequencies (above 40% of Nyquist)
    if (freq > nyquist * 0.4f) {
        float ratio = freq / nyquist;
        float compensation = 1.0f + (ratio - 0.4f) * 0.25f;
        warpedFreq = freq * compensation;
    }

    return std::min(warpedFreq, static_cast<float>(nyquist * 0.98f));
}


#ifndef JucePlugin_Name
#define JucePlugin_Name "SSL4KEQ"
#endif

//==============================================================================
FourKEQ::FourKEQ()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "SSL4KEQ", createParameterLayout())
{
    // Link parameters to atomic values
    hpfFreqParam = parameters.getRawParameterValue("hpf_freq");
    lpfFreqParam = parameters.getRawParameterValue("lpf_freq");

    lfGainParam = parameters.getRawParameterValue("lf_gain");
    lfFreqParam = parameters.getRawParameterValue("lf_freq");
    lfBellParam = parameters.getRawParameterValue("lf_bell");

    lmGainParam = parameters.getRawParameterValue("lm_gain");
    lmFreqParam = parameters.getRawParameterValue("lm_freq");
    lmQParam = parameters.getRawParameterValue("lm_q");

    hmGainParam = parameters.getRawParameterValue("hm_gain");
    hmFreqParam = parameters.getRawParameterValue("hm_freq");
    hmQParam = parameters.getRawParameterValue("hm_q");

    hfGainParam = parameters.getRawParameterValue("hf_gain");
    hfFreqParam = parameters.getRawParameterValue("hf_freq");
    hfBellParam = parameters.getRawParameterValue("hf_bell");

    eqTypeParam = parameters.getRawParameterValue("eq_type");
    bypassParam = parameters.getRawParameterValue("bypass");
    outputGainParam = parameters.getRawParameterValue("output_gain");
    saturationParam = parameters.getRawParameterValue("saturation");
    oversamplingParam = parameters.getRawParameterValue("oversampling");

}

FourKEQ::~FourKEQ() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FourKEQ::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // High-pass filter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hpf_freq", "HPF Frequency",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.3f),
        20.0f, "Hz"));

    // Low-pass filter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lpf_freq", "LPF Frequency",
        juce::NormalisableRange<float>(3000.0f, 20000.0f, 1.0f, 0.3f),
        20000.0f, "Hz"));

    // Low frequency band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lf_gain", "LF Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lf_freq", "LF Frequency",
        juce::NormalisableRange<float>(20.0f, 600.0f, 1.0f, 0.3f),
        100.0f, "Hz"));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "lf_bell", "LF Bell Mode", false));

    // Low-mid band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lm_gain", "LM Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lm_freq", "LM Frequency",
        juce::NormalisableRange<float>(200.0f, 2500.0f, 1.0f, 0.3f),
        600.0f, "Hz"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lm_q", "LM Q",
        juce::NormalisableRange<float>(0.5f, 5.0f, 0.01f),
        0.7f));

    // High-mid band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hm_gain", "HM Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hm_freq", "HM Frequency",
        juce::NormalisableRange<float>(600.0f, 7000.0f, 1.0f, 0.3f),
        2000.0f, "Hz"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hm_q", "HM Q",
        juce::NormalisableRange<float>(0.5f, 5.0f, 0.01f),
        0.7f));

    // High frequency band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hf_gain", "HF Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hf_freq", "HF Frequency",
        juce::NormalisableRange<float>(1500.0f, 20000.0f, 1.0f, 0.3f),
        8000.0f, "Hz"));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "hf_bell", "HF Bell Mode", false));

    // Global parameters
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "eq_type", "EQ Type", juce::StringArray("Brown", "Black"), 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "bypass", "Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output_gain", "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "saturation", "Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        20.0f, "%"));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "oversampling", "Oversampling", juce::StringArray("2x", "4x"), 0));

    return { params.begin(), params.end() };
}

//==============================================================================
void FourKEQ::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Initialize oversampling
    oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
        getTotalNumInputChannels(), 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

    oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
        getTotalNumInputChannels(), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

    oversampler2x->initProcessing(samplesPerBlock);
    oversampler4x->initProcessing(samplesPerBlock);

    // Prepare filters
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate * oversamplingFactor;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;

    hpfFilter.prepare(spec);
    lpfFilter.prepare(spec);
    lfFilter.prepare(spec);
    lmFilter.prepare(spec);
    hmFilter.prepare(spec);
    hfFilter.prepare(spec);

    updateFilters();
}

void FourKEQ::releaseResources()
{
    hpfFilter.reset();
    lpfFilter.reset();
    lfFilter.reset();
    lmFilter.reset();
    hmFilter.reset();
    hfFilter.reset();

    if (oversampler2x) oversampler2x->reset();
    if (oversampler4x) oversampler4x->reset();
}

//==============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations
bool FourKEQ::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
#endif

//==============================================================================
void FourKEQ::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Check bypass
    if (bypassParam->load() > 0.5f)
        return;

    // Update filter coefficients if needed
    updateFilters();

    // Choose oversampling
    oversamplingFactor = (oversamplingParam->load() < 0.5f) ? 2 : 4;
    auto& oversampler = (oversamplingFactor == 2) ? *oversampler2x : *oversampler4x;

    // Create audio block and oversample
    juce::dsp::AudioBlock<float> block(buffer);
    auto oversampledBlock = oversampler.processSamplesUp(block);

    auto numChannels = oversampledBlock.getNumChannels();
    auto numSamples = oversampledBlock.getNumSamples();

    // Process each channel
    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = oversampledBlock.getChannelPointer(channel);

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            float processSample = channelData[sample];

            // Apply HPF (two stages for 18dB/oct)
            if (channel == 0)
            {
                processSample = hpfFilter.stage1.filter.processSample(processSample);
                processSample = hpfFilter.stage2.filter.processSample(processSample);
            }
            else
            {
                processSample = hpfFilter.stage1.filterR.processSample(processSample);
                processSample = hpfFilter.stage2.filterR.processSample(processSample);
            }

            // Apply 4-band EQ
            if (channel == 0)
            {
                processSample = lfFilter.filter.processSample(processSample);
                processSample = lmFilter.filter.processSample(processSample);
                processSample = hmFilter.filter.processSample(processSample);
                processSample = hfFilter.filter.processSample(processSample);
            }
            else
            {
                processSample = lfFilter.filterR.processSample(processSample);
                processSample = lmFilter.filterR.processSample(processSample);
                processSample = hmFilter.filterR.processSample(processSample);
                processSample = hfFilter.filterR.processSample(processSample);
            }

            // Apply LPF
            if (channel == 0)
                processSample = lpfFilter.filter.processSample(processSample);
            else
                processSample = lpfFilter.filterR.processSample(processSample);

            // Apply saturation in the oversampled domain
            float satAmount = saturationParam->load() * 0.01f;
            if (satAmount > 0.0f)
                processSample = applySaturation(processSample, satAmount);

            channelData[sample] = processSample;
        }
    }

    // Downsample back to original rate
    oversampler.processSamplesDown(block);

    // Apply output gain
    float outputGainValue = outputGainParam->load();
    float outputGain = juce::Decibels::decibelsToGain(outputGainValue);
    buffer.applyGain(outputGain);
}

//==============================================================================
void FourKEQ::updateFilters()
{
    double oversampledRate = currentSampleRate * oversamplingFactor;

    updateHPF(oversampledRate);
    updateLPF(oversampledRate);
    updateLFBand(oversampledRate);
    updateLMBand(oversampledRate);
    updateHMBand(oversampledRate);
    updateHFBand(oversampledRate);
}

void FourKEQ::updateHPF(double sampleRate)
{
    float freq = hpfFreqParam->load();

    // Create coefficients for a Butterworth HPF
    // Use two cascaded 2nd order filters for ~18dB/oct
    auto coeffs1 = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, freq, 0.54f);  // Q for Butterworth cascade
    auto coeffs2 = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, freq, 1.31f);  // Q for Butterworth cascade

    hpfFilter.stage1.filter.coefficients = coeffs1;
    hpfFilter.stage1.filterR.coefficients = coeffs1;
    hpfFilter.stage2.filter.coefficients = coeffs2;
    hpfFilter.stage2.filterR.coefficients = coeffs2;
}

void FourKEQ::updateLPF(double sampleRate)
{
    float freq = lpfFreqParam->load();

    // Pre-warp if close to Nyquist
    float processFreq = freq;
    if (freq > sampleRate * 0.3f) {
        processFreq = preWarpFrequency(freq, sampleRate);
    }

    // 12dB/oct Butterworth LPF with pre-warped frequency
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, processFreq, 0.707f);

    lpfFilter.filter.coefficients = coeffs;
    lpfFilter.filterR.coefficients = coeffs;
}

void FourKEQ::updateLFBand(double sampleRate)
{
    float gain = lfGainParam->load();
    float freq = lfFreqParam->load();
    bool isBlack = (eqTypeParam->load() > 0.5f);
    bool isBell = (lfBellParam->load() > 0.5f);

    if (isBlack && isBell)
    {
        // Bell mode in Black variant
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, freq, 0.7f, juce::Decibels::decibelsToGain(gain));
        lfFilter.filter.coefficients = coeffs;
        lfFilter.filterR.coefficients = coeffs;
    }
    else
    {
        // Shelf mode
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
            sampleRate, freq, 0.7f, juce::Decibels::decibelsToGain(gain));
        lfFilter.filter.coefficients = coeffs;
        lfFilter.filterR.coefficients = coeffs;
    }
}

void FourKEQ::updateLMBand(double sampleRate)
{
    float gain = lmGainParam->load();
    float freq = lmFreqParam->load();
    float q = lmQParam->load();
    bool isBlack = (eqTypeParam->load() > 0.5f);

    // Dynamic Q in Black mode
    if (isBlack)
        q = calculateDynamicQ(gain, q);

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, freq, q, juce::Decibels::decibelsToGain(gain));

    lmFilter.filter.coefficients = coeffs;
    lmFilter.filterR.coefficients = coeffs;
}

void FourKEQ::updateHMBand(double sampleRate)
{
    float gain = hmGainParam->load();
    float freq = hmFreqParam->load();
    float q = hmQParam->load();
    bool isBlack = (eqTypeParam->load() > 0.5f);

    // Dynamic Q in Black mode
    if (isBlack)
        q = calculateDynamicQ(gain, q);

    // Pre-warp frequency if above 3kHz to prevent cramping
    float processFreq = freq;
    if (freq > 3000.0f) {
        processFreq = preWarpFrequency(freq, sampleRate);
    }

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, processFreq, q, juce::Decibels::decibelsToGain(gain));

    hmFilter.filter.coefficients = coeffs;
    hmFilter.filterR.coefficients = coeffs;
}

void FourKEQ::updateHFBand(double sampleRate)
{
    float gain = hfGainParam->load();
    float freq = hfFreqParam->load();
    bool isBlack = (eqTypeParam->load() > 0.5f);
    bool isBell = (hfBellParam->load() > 0.5f);

    // Always pre-warp HF band frequencies to prevent cramping
    float warpedFreq = preWarpFrequency(freq, sampleRate);

    if (isBlack && isBell)
    {
        // Bell mode in Black variant with pre-warped frequency
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, warpedFreq, 0.7f, juce::Decibels::decibelsToGain(gain));
        hfFilter.filter.coefficients = coeffs;
        hfFilter.filterR.coefficients = coeffs;
    }
    else
    {
        // Shelf mode with pre-warped frequency
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
            sampleRate, warpedFreq, 0.7f, juce::Decibels::decibelsToGain(gain));
        hfFilter.filter.coefficients = coeffs;
        hfFilter.filterR.coefficients = coeffs;
    }
}

float FourKEQ::calculateDynamicQ(float gain, float baseQ) const
{
    // In Black mode, Q widens (becomes lower) at lower gains
    // This creates a more gentle curve at lower gain settings
    float absGain = std::abs(gain);
    float scale = 1.0f - (absGain / 20.0f) * 0.5f;  // Scale from 1.0 to 0.5
    float dynamicQ = baseQ * (0.5f + 0.5f * scale);

    return juce::jlimit(0.5f, 5.0f, dynamicQ);
}

float FourKEQ::applySaturation(float sample, float amount) const
{
    // Soft saturation using tanh
    // Scale input to control saturation amount
    float drive = 1.0f + amount * 2.0f;
    float saturated = std::tanh(sample * drive);

    // Mix dry and wet signals
    return sample * (1.0f - amount) + saturated * amount;
}

//==============================================================================
void FourKEQ::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FourKEQ::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessorEditor* FourKEQ::createEditor()
{
    return new FourKEQEditor(*this);
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FourKEQ();
}

//==============================================================================
// LV2 extension data export
#ifdef JucePlugin_Build_LV2

#ifndef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#define LV2_SYMBOL_EXPORT __attribute__((visibility("default")))
#endif
#endif

#endif
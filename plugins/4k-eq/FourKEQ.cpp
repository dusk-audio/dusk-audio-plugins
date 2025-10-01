#include "FourKEQ.h"
#include "PluginEditor.h"
#include <cmath>

// Helper function to prevent frequency cramping at high frequencies
static float preWarpFrequency(float freq, double sampleRate)
{
    // Pre-warp frequency for bilinear transform to prevent cramping
    const float nyquist = sampleRate * 0.5f;

    // Standard pre-warping formula
    const float k = std::tan((juce::MathConstants<float>::pi * freq) / sampleRate);
    float warpedFreq = (sampleRate / juce::MathConstants<float>::pi) * std::atan(k);

    // Additional compensation for very high frequencies (above 40% of Nyquist)
    if (freq > nyquist * 0.4f) {
        float ratio = freq / nyquist;
        float compensation = 1.0f + (ratio - 0.4f) * 0.3f;  // Increased from 0.25f to 0.3f
        warpedFreq = freq * compensation;
    }

    return std::min(warpedFreq, static_cast<float>(nyquist * 0.99f));  // Changed from 0.98f to 0.99f
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
    // Link parameters to atomic values with null checks
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

    // Assert all parameters are valid (keeps for debug builds)
    jassert(hpfFreqParam && lpfFreqParam && lfGainParam && lfFreqParam &&
            lfBellParam && lmGainParam && lmFreqParam && lmQParam &&
            hmGainParam && hmFreqParam && hmQParam && hfGainParam &&
            hfFreqParam && hfBellParam && eqTypeParam && bypassParam &&
            outputGainParam && saturationParam && oversamplingParam);

    // Runtime checks for production safety
    if (!hpfFreqParam || !lpfFreqParam || !lfGainParam || !lfFreqParam ||
        !lfBellParam || !lmGainParam || !lmFreqParam || !lmQParam ||
        !hmGainParam || !hmFreqParam || !hmQParam || !hfGainParam ||
        !hfFreqParam || !hfBellParam || !eqTypeParam || !bypassParam ||
        !outputGainParam || !saturationParam || !oversamplingParam)
    {
        DBG("FourKEQ: ERROR - One or more parameters failed to initialize!");
        // Log which specific parameters are null for debugging
        if (!hpfFreqParam) DBG("  - hpf_freq is null");
        if (!lpfFreqParam) DBG("  - lpf_freq is null");
        if (!lfGainParam) DBG("  - lf_gain is null");
        if (!lfFreqParam) DBG("  - lf_freq is null");
        if (!lfBellParam) DBG("  - lf_bell is null");
        if (!lmGainParam) DBG("  - lm_gain is null");
        if (!lmFreqParam) DBG("  - lm_freq is null");
        if (!lmQParam) DBG("  - lm_q is null");
        if (!hmGainParam) DBG("  - hm_gain is null");
        if (!hmFreqParam) DBG("  - hm_freq is null");
        if (!hmQParam) DBG("  - hm_q is null");
        if (!hfGainParam) DBG("  - hf_gain is null");
        if (!hfFreqParam) DBG("  - hf_freq is null");
        if (!hfBellParam) DBG("  - hf_bell is null");
        if (!eqTypeParam) DBG("  - eq_type is null");
        if (!bypassParam) DBG("  - bypass is null");
        if (!outputGainParam) DBG("  - output_gain is null");
        if (!saturationParam) DBG("  - saturation is null");
        if (!oversamplingParam) DBG("  - oversampling is null");
    }

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
    // Validate sample rate
    if (sampleRate <= 0.0 || samplesPerBlock <= 0)
    {
        jassertfalse;
        return;
    }

    currentSampleRate = sampleRate;

    // Determine oversampling factor from parameter
    oversamplingFactor = (!oversamplingParam || oversamplingParam->load() < 0.5f) ? 2 : 4;

    // Initialize oversampling
    oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
        getTotalNumInputChannels(), 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

    oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
        getTotalNumInputChannels(), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

    oversampler2x->initProcessing(samplesPerBlock);
    oversampler4x->initProcessing(samplesPerBlock);

    // Prepare filters with oversampled rate
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate * oversamplingFactor;
    spec.maximumBlockSize = samplesPerBlock * oversamplingFactor;
    spec.numChannels = 1;

    // Reset filters before preparing to ensure clean state
    hpfFilter.reset();
    lpfFilter.reset();
    lfFilter.reset();
    lmFilter.reset();
    hmFilter.reset();
    hfFilter.reset();

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

    // Check bypass with null check
    if (!bypassParam || bypassParam->load() > 0.5f)
        return;

    // Check if oversamplers are initialized
    if (!oversampler2x || !oversampler4x)
        return;

    // Update filter coefficients if needed
    updateFilters();

    // Choose oversampling with null check
    oversamplingFactor = (!oversamplingParam || oversamplingParam->load() < 0.5f) ? 2 : 4;
    auto& oversampler = (oversamplingFactor == 2) ? *oversampler2x : *oversampler4x;

    // Create audio block and oversample
    juce::dsp::AudioBlock<float> block(buffer);
    auto oversampledBlock = oversampler.processSamplesUp(block);

    auto numChannels = oversampledBlock.getNumChannels();
    auto numSamples = oversampledBlock.getNumSamples();

    // Determine if we're processing mono or stereo
    const bool isMono = (numChannels == 1);

    // Process each channel
    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = oversampledBlock.getChannelPointer(channel);

        // For mono, always use left channel filters; for stereo, use appropriate filter per channel
        const bool useLeftFilter = (channel == 0) || isMono;

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            float processSample = channelData[sample];

            // Apply HPF (two stages for 18dB/oct)
            if (useLeftFilter)
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
            if (useLeftFilter)
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
            if (useLeftFilter)
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
    if (outputGainParam)
    {
        float outputGainValue = outputGainParam->load();
        float outputGain = juce::Decibels::decibelsToGain(outputGainValue);
        buffer.applyGain(outputGain);
    }
}

//==============================================================================
void FourKEQ::updateFilters()
{
    double oversampledRate = currentSampleRate * oversamplingFactor;

    // Check for parameter changes and set dirty flags
    if (hpfFreqParam)
    {
        float currentHpfFreq = hpfFreqParam->load();
        if (currentHpfFreq != lastHpfFreq)
        {
            hpfNeedsUpdate = true;
            lastHpfFreq = currentHpfFreq;
        }
    }

    if (lpfFreqParam)
    {
        float currentLpfFreq = lpfFreqParam->load();
        if (currentLpfFreq != lastLpfFreq)
        {
            lpfNeedsUpdate = true;
            lastLpfFreq = currentLpfFreq;
        }
    }

    // LF Band
    if (lfGainParam && lfFreqParam && lfBellParam && eqTypeParam)
    {
        float gain = lfGainParam->load();
        float freq = lfFreqParam->load();
        float bell = lfBellParam->load();
        float eqType = eqTypeParam->load();
        if (gain != lastLfGain || freq != lastLfFreq || bell != lastLfBell || eqType != lastEqType)
        {
            lfNeedsUpdate = true;
            lastLfGain = gain;
            lastLfFreq = freq;
            lastLfBell = bell;
            lastEqType = eqType;
        }
    }

    // LM Band
    if (lmGainParam && lmFreqParam && lmQParam && eqTypeParam)
    {
        float gain = lmGainParam->load();
        float freq = lmFreqParam->load();
        float q = lmQParam->load();
        float eqType = eqTypeParam->load();
        if (gain != lastLmGain || freq != lastLmFreq || q != lastLmQ || eqType != lastEqType)
        {
            lmNeedsUpdate = true;
            lastLmGain = gain;
            lastLmFreq = freq;
            lastLmQ = q;
            lastEqType = eqType;
        }
    }

    // HM Band
    if (hmGainParam && hmFreqParam && hmQParam && eqTypeParam)
    {
        float gain = hmGainParam->load();
        float freq = hmFreqParam->load();
        float q = hmQParam->load();
        float eqType = eqTypeParam->load();
        if (gain != lastHmGain || freq != lastHmFreq || q != lastHmQ || eqType != lastEqType)
        {
            hmNeedsUpdate = true;
            lastHmGain = gain;
            lastHmFreq = freq;
            lastHmQ = q;
            lastEqType = eqType;
        }
    }

    // HF Band
    if (hfGainParam && hfFreqParam && hfBellParam && eqTypeParam)
    {
        float gain = hfGainParam->load();
        float freq = hfFreqParam->load();
        float bell = hfBellParam->load();
        float eqType = eqTypeParam->load();
        if (gain != lastHfGain || freq != lastHfFreq || bell != lastHfBell || eqType != lastEqType)
        {
            hfNeedsUpdate = true;
            lastHfGain = gain;
            lastHfFreq = freq;
            lastHfBell = bell;
            lastEqType = eqType;
        }
    }

    // Only update filters that need updating
    if (hpfNeedsUpdate.load())
    {
        updateHPF(oversampledRate);
        hpfNeedsUpdate = false;
    }

    if (lpfNeedsUpdate.load())
    {
        updateLPF(oversampledRate);
        lpfNeedsUpdate = false;
    }

    if (lfNeedsUpdate.load())
    {
        updateLFBand(oversampledRate);
        lfNeedsUpdate = false;
    }

    if (lmNeedsUpdate.load())
    {
        updateLMBand(oversampledRate);
        lmNeedsUpdate = false;
    }

    if (hmNeedsUpdate.load())
    {
        updateHMBand(oversampledRate);
        hmNeedsUpdate = false;
    }

    if (hfNeedsUpdate.load())
    {
        updateHFBand(oversampledRate);
        hfNeedsUpdate = false;
    }
}

void FourKEQ::updateHPF(double sampleRate)
{
    if (!hpfFreqParam || sampleRate <= 0.0)
        return;

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
    if (!lpfFreqParam || sampleRate <= 0.0)
        return;

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
    if (!lfGainParam || !lfFreqParam || !eqTypeParam || !lfBellParam || sampleRate <= 0.0)
        return;

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
    if (!lmGainParam || !lmFreqParam || !lmQParam || !eqTypeParam || sampleRate <= 0.0)
        return;

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
    if (!hmGainParam || !hmFreqParam || !hmQParam || !eqTypeParam || sampleRate <= 0.0)
        return;

    float gain = hmGainParam->load();
    float freq = hmFreqParam->load();
    float q = hmQParam->load();
    bool isBlack = (eqTypeParam->load() > 0.5f);

    // Dynamic Q in Black mode
    if (isBlack)
        q = calculateDynamicQ(gain, q);

    // Pre-warp frequency if above 3kHz to prevent cramping (updated to use improved version)
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
    if (!hfGainParam || !hfFreqParam || !eqTypeParam || !hfBellParam || sampleRate <= 0.0)
        return;

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
    // In Black mode, Q behavior is asymmetric: wider for cuts, tighter for boosts
    // This matches SSL console behavior where cuts are more musical and broad
    float absGain = std::abs(gain);

    // Different scaling for boosts vs cuts
    float scale;
    if (gain >= 0.0f)
    {
        // Boosts: moderate Q reduction (tighter curves)
        scale = 0.5f;  // Reduces Q by up to 50% at max boost
    }
    else
    {
        // Cuts: more Q reduction (wider, more gentle curves)
        scale = 0.6f;  // Reduces Q by up to 60% at max cut
    }

    // Apply dynamic Q based on gain amount
    // Note: Gain parameters are ±20 dB, so we divide by 20.0f for full-range modulation
    float dynamicQ = baseQ * (1.0f - (absGain / 20.0f) * scale);

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
#include "FourKEQ.h"
#include "PluginEditor.h"
#include <cmath>

// Helper function to prevent frequency cramping at high frequencies
// Based on SSL-style analog prototype matching for accurate HF response
static float preWarpFrequency(float freq, double sampleRate)
{
    const float nyquist = static_cast<float>(sampleRate * 0.5);

    // Standard bilinear pre-warping: f_analog = (fs/π) * tan(π*f_digital/fs)
    const float omega = juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
    float warpedFreq = static_cast<float>(sampleRate) / juce::MathConstants<float>::pi * std::tan(omega);

    // SSL-specific high-frequency compensation (tuned to match hardware measurements)
    // Applies progressive correction above 3kHz to maintain shelf shape
    if (freq > 3000.0f)
    {
        float ratio = freq / nyquist;

        // Piecewise compensation based on frequency region
        float compensation = 1.0f;
        if (ratio < 0.3f)
        {
            // 3-6kHz region: minimal compensation
            compensation = 1.0f + (ratio - 0.136f) * 0.15f;
        }
        else if (ratio < 0.5f)
        {
            // 6-10kHz region: moderate compensation
            compensation = 1.0f + (ratio - 0.3f) * 0.4f;
        }
        else
        {
            // 10kHz+ region: stronger compensation for extreme HF
            compensation = 1.0f + (ratio - 0.5f) * 0.6f;
        }

        warpedFreq = freq * compensation;
    }

    // Clamp to safe range (leave 1% headroom from Nyquist)
    return std::min(warpedFreq, nyquist * 0.99f);
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

    // Verify all critical parameters are initialized
    bool allParamsValid = hpfFreqParam && lpfFreqParam && lfGainParam && lfFreqParam &&
        lfBellParam && lmGainParam && lmFreqParam && lmQParam &&
        hmGainParam && hmFreqParam && hmQParam && hfGainParam &&
        hfFreqParam && hfBellParam && eqTypeParam && bypassParam &&
        outputGainParam && saturationParam && oversamplingParam;

    if (!allParamsValid)
    {
        DBG("FourKEQ: WARNING - Some parameters failed to initialize, plugin may not function correctly");
        // Note: We continue with safe accessors that provide defaults
    }

    // Add parameter change listener for performance optimization
    parameters.addParameterListener("hpf_freq", this);
    parameters.addParameterListener("lpf_freq", this);
    parameters.addParameterListener("lf_gain", this);
    parameters.addParameterListener("lf_freq", this);
    parameters.addParameterListener("lf_bell", this);
    parameters.addParameterListener("lm_gain", this);
    parameters.addParameterListener("lm_freq", this);
    parameters.addParameterListener("lm_q", this);
    parameters.addParameterListener("hm_gain", this);
    parameters.addParameterListener("hm_freq", this);
    parameters.addParameterListener("hm_q", this);
    parameters.addParameterListener("hf_gain", this);
    parameters.addParameterListener("hf_freq", this);
    parameters.addParameterListener("hf_bell", this);
    parameters.addParameterListener("eq_type", this);
    parameters.addParameterListener("oversampling", this);
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
    // Black mode extends to 13kHz (vs Brown's 7kHz) for more HF control
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
        juce::NormalisableRange<float>(0.4f, 5.0f, 0.01f),  // Wider range for both modes
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

    // Report latency introduced by oversampling to host
    if (oversamplingFactor == 4 && oversampler4x)
        setLatencySamples(static_cast<int>(oversampler4x->getLatencyInSamples()));
    else if (oversampler2x)
        setLatencySamples(static_cast<int>(oversampler2x->getLatencyInSamples()));
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

    // Check bypass - skip ALL processing when bypassed (including output gain and saturation)
    if (bypassParam && bypassParam->load() > 0.5f)
        return;

    // Check if oversamplers are initialized
    if (!oversampler2x || !oversampler4x)
        return;

    // Only update filters if parameters have changed (performance optimization)
    if (parametersChanged.load())
    {
        updateFilters();
        parametersChanged.store(false);
    }

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

            // Apply HPF (3rd-order: 1st-order + 2nd-order stages = 18dB/oct)
            if (useLeftFilter)
            {
                processSample = hpfFilter.stage1L.processSample(processSample);
                processSample = hpfFilter.stage2.filter.processSample(processSample);
            }
            else
            {
                processSample = hpfFilter.stage1R.processSample(processSample);
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

    // Apply output gain with auto-compensation
    if (outputGainParam)
    {
        float outputGainValue = outputGainParam->load();
        float autoCompensation = calculateAutoGainCompensation();
        float totalGain = juce::Decibels::decibelsToGain(outputGainValue) * autoCompensation;
        buffer.applyGain(totalGain);
    }
}

//==============================================================================
void FourKEQ::updateFilters()
{
    // Update all filters when parameters change
    // Individual change detection removed for simplicity - updateFilters only called when needed
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
    if (!hpfFreqParam || sampleRate <= 0.0)
        return;

    float freq = hpfFreqParam->load();

    // True 3rd-order Butterworth HPF (18dB/oct):
    // Stage 1: 1st-order highpass (6dB/oct)
    auto coeffs1st = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(sampleRate, freq);
    hpfFilter.stage1L.coefficients = coeffs1st;
    hpfFilter.stage1R.coefficients = coeffs1st;

    // Stage 2: 2nd-order Butterworth highpass (12dB/oct, Q=0.707)
    auto coeffs2nd = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, freq, 0.707f);  // Butterworth Q

    hpfFilter.stage2.filter.coefficients = coeffs2nd;
    hpfFilter.stage2.filterR.coefficients = coeffs2nd;
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

    // Brown vs Black mode differences
    if (isBlack)
    {
        // Black mode: Proportional Q (tighter with more gain)
        q = calculateDynamicQ(gain, q);
    }
    else
    {
        // Brown mode: Fixed Q with slight gain-dependent adjustment for musicality
        // More subtle than Black mode - maintains broad, musical character
        float absGain = std::abs(gain);
        q = q * (1.0f + (absGain / 20.0f) * 0.2f);  // Only 20% variation vs 100-150% in Black
        q = juce::jlimit(0.5f, 3.0f, q);
    }

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

    // Brown vs Black mode differences
    if (isBlack)
    {
        // Black mode: Proportional Q, extended frequency range (up to 13kHz)
        q = calculateDynamicQ(gain, q);
        // No frequency limiting in Black mode - full 600Hz-13kHz range
    }
    else
    {
        // Brown mode: Fixed Q with minimal variation, limited to 7kHz
        float absGain = std::abs(gain);
        q = q * (1.0f + (absGain / 20.0f) * 0.2f);
        q = juce::jlimit(0.5f, 3.0f, q);
        // Soft-limit frequency for Brown mode character
        if (freq > 7000.0f) {
            freq = 7000.0f;
        }
    }

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
    // SSL Black mode proportional Q behavior:
    // Q INCREASES with gain amount for sharper, more focused adjustments
    // Boosts get tighter (higher Q), cuts get broader (lower Q for musicality)
    float absGain = std::abs(gain);

    // Different scaling for boosts vs cuts to match SSL character
    float scale;
    if (gain >= 0.0f)
    {
        // Boosts: Q increases significantly for surgical precision
        scale = 1.5f;  // Q can increase by 150% at max boost (+20dB)
    }
    else
    {
        // Cuts: Q increases moderately for broad, musical cuts
        scale = 1.0f;  // Q can increase by 100% at max cut (-20dB)
    }

    // Apply proportional Q: higher gain = higher Q (inverted from previous logic)
    // Gain parameters are ±20 dB
    float dynamicQ = baseQ * (1.0f + (absGain / 20.0f) * scale);

    return juce::jlimit(0.5f, 8.0f, dynamicQ);  // Extended upper range for SSL behavior
}

float FourKEQ::applySaturation(float sample, float amount) const
{
    // Analog-style saturation using asymmetric clipping to model op-amp behavior
    // SSL consoles use NE5534 op-amps with characteristic asymmetric distortion
    float drive = 1.0f + amount * 3.0f;  // More aggressive drive range

    // Apply analog saturation model
    float saturated = applyAnalogSaturation(sample, drive, true);

    // Mix dry and wet signals based on amount
    return sample * (1.0f - amount) + saturated * amount;
}

float FourKEQ::applyAnalogSaturation(float sample, float drive, bool isAsymmetric) const
{
    // SSL-style op-amp saturation model
    // Based on asymmetric soft-clipping with different thresholds for positive/negative
    float driven = sample * drive;

    if (isAsymmetric)
    {
        // Asymmetric clipping: positive clips softer (NE5534 characteristic)
        if (driven > 0.0f)
        {
            // Positive: softer knee, lower threshold
            const float threshold = 0.7f;
            if (driven < threshold)
                return driven / drive;
            else
                return (threshold + std::tanh((driven - threshold) * 2.0f) * 0.3f) / drive;
        }
        else
        {
            // Negative: harder knee, higher threshold
            const float threshold = -0.85f;
            if (driven > threshold)
                return driven / drive;
            else
                return (threshold + std::tanh((driven - threshold) * 1.5f) * 0.15f) / drive;
        }
    }
    else
    {
        // Symmetric soft clipping (fallback)
        return std::tanh(driven) / drive;
    }
}

float FourKEQ::calculateAutoGainCompensation() const
{
    // Calculate approximate gain compensation based on all EQ bands
    // This maintains perceived loudness similar to SSL hardware behavior

    // Sum all band gains (negative = cut, positive = boost)
    float lfGain = safeGetParam(lfGainParam, 0.0f);
    float lmGain = safeGetParam(lmGainParam, 0.0f);
    float hmGain = safeGetParam(hmGainParam, 0.0f);
    float hfGain = safeGetParam(hfGainParam, 0.0f);

    // Weight the contributions based on how broad each band typically affects spectrum
    // Shelves (LF/HF) affect more frequencies, so weight them higher
    float weightedSum = (lfGain * 0.3f) + (lmGain * 0.2f) + (hmGain * 0.2f) + (hfGain * 0.3f);

    // Calculate compensation: reduce output when boosting, increase when cutting
    // Use gentler compensation (25%) to maintain SSL "bigger" sound character
    float compensationDB = -weightedSum * 0.25f;

    // Limit compensation range to ±6dB to avoid extreme changes
    compensationDB = juce::jlimit(-6.0f, 6.0f, compensationDB);

    return juce::Decibels::decibelsToGain(compensationDB);
}

//==============================================================================
void FourKEQ::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    // Add version information for backward/forward compatibility
    xml->setAttribute("pluginVersion", "1.0.0");
    xml->setAttribute("manufacturer", "Luna Co. Audio");

    copyXmlToBinary(*xml, destData);
}

void FourKEQ::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName(parameters.state.getType()))
        {
            // Check version for compatibility (future-proofing)
            auto version = xmlState->getStringAttribute("pluginVersion", "1.0.0");
            DBG("Loading 4K EQ state, version: " + version);

            // Load the state
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));

            // Force filter update after loading state
            parametersChanged.store(true);
        }
    }
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
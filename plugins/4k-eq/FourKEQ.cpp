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
    msModeParam = parameters.getRawParameterValue("ms_mode");
    spectrumPrePostParam = parameters.getRawParameterValue("spectrum_prepost");

    // Assert all parameters are valid (keeps for debug builds)
    jassert(hpfFreqParam && lpfFreqParam && lfGainParam && lfFreqParam &&
            lfBellParam && lmGainParam && lmFreqParam && lmQParam &&
            hmGainParam && hmFreqParam && hmQParam && hfGainParam &&
            hfFreqParam && hfBellParam && eqTypeParam && bypassParam &&
            outputGainParam && saturationParam && oversamplingParam);

    // Verify all critical parameters are initialized
    paramsValid = hpfFreqParam && lpfFreqParam && lfGainParam && lfFreqParam &&
        lfBellParam && lmGainParam && lmFreqParam && lmQParam &&
        hmGainParam && hmFreqParam && hmQParam && hfGainParam &&
        hfFreqParam && hfBellParam && eqTypeParam && bypassParam &&
        outputGainParam && saturationParam && oversamplingParam && msModeParam;

    if (!paramsValid)
    {
        DBG("FourKEQ: CRITICAL - Parameters failed to initialize! Plugin will skip processing.");
        // Processing will be bypassed in processBlock() when paramsValid is false
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
    // SSL specs: ±15dB (Brown E-series), ±18dB (Black G-series)
    // Using ±20dB range to accommodate both variants with headroom
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lf_gain", "LF Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lf_freq", "LF Frequency",
        // Brown: typically 30-450Hz, Black: slightly extended
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

    // M/S Processing
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "ms_mode", "M/S Mode", false));

    // Spectrum Pre/Post Toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "spectrum_prepost", "Spectrum Pre/Post", false));  // false = post-EQ (default)

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

    // Validate sample rate to prevent division-by-zero and invalid filter calculations
    if (sampleRate <= 0.0 || std::isnan(sampleRate) || std::isinf(sampleRate))
    {
        DBG("FourKEQ: Invalid sample rate received: " << sampleRate);
        return;  // Skip preparation - retain last valid state
    }

    // Clamp sample rate to reasonable range (8kHz to 192kHz)
    sampleRate = juce::jlimit(8000.0, 192000.0, sampleRate);

    currentSampleRate = sampleRate;

    // Determine oversampling factor from parameter
    // Optimization: Auto-limit to 2x at high sample rates (>96kHz) to reduce CPU load
    int requestedFactor = (!oversamplingParam || oversamplingParam->load() < 0.5f) ? 2 : 4;

    if (sampleRate > 96000.0 && requestedFactor == 4)
    {
        oversamplingFactor = 2;  // Force 2x at high sample rates
        DBG("FourKEQ: High sample rate detected (" << sampleRate << " Hz) - limiting to 2x oversampling");
    }
    else
    {
        oversamplingFactor = requestedFactor;
    }

    // Only recreate oversamplers if sample rate or factor changed (optimization)
    bool needsRecreate = (std::abs(sampleRate - lastPreparedSampleRate) > 0.01) ||
                         (oversamplingFactor != lastOversamplingFactor) ||
                         !oversampler2x || !oversampler4x;

    if (needsRecreate)
    {
        // Initialize oversampling
        oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
            getTotalNumInputChannels(), 1,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

        oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
            getTotalNumInputChannels(), 2,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

        oversampler2x->initProcessing(samplesPerBlock);
        oversampler4x->initProcessing(samplesPerBlock);

        lastPreparedSampleRate = sampleRate;
        lastOversamplingFactor = oversamplingFactor;
    }
    else
    {
        // Just reset existing oversamplers
        oversampler2x->reset();
        oversampler4x->reset();
    }

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

    // Critical safety check: skip processing if parameters failed to initialize
    if (!paramsValid)
    {
        DBG("FourKEQ: Skipping processing - parameters not valid");
        return;
    }

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

    // Capture pre-EQ buffer for spectrum analyzer (thread-safe)
    {
        const juce::ScopedLock sl(spectrumBufferLock);
        spectrumBufferPre.makeCopyOf(buffer, true);
    }

    // Choose oversampling with null check
    oversamplingFactor = (!oversamplingParam || oversamplingParam->load() < 0.5f) ? 2 : 4;
    auto& oversampler = (oversamplingFactor == 2) ? *oversampler2x : *oversampler4x;

    // Check M/S mode
    bool useMSProcessing = (msModeParam && msModeParam->load() > 0.5f);

    // Convert to M/S if enabled (before oversampling)
    if (useMSProcessing && buffer.getNumChannels() == 2)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float left = buffer.getSample(0, i);
            float right = buffer.getSample(1, i);

            // L+R = Mid, L-R = Side
            float mid = (left + right) * 0.5f;
            float side = (left - right) * 0.5f;

            buffer.setSample(0, i, mid);
            buffer.setSample(1, i, side);
        }
    }

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

            // Apply 4-band EQ with per-band saturation for SSL character
            // Stage-specific nonlinearities model op-amp behavior in each EQ section
            if (useLeftFilter)
            {
                processSample = lfFilter.filter.processSample(processSample);
                processSample = applyAnalogSaturation(processSample, lfSatDrive, true);

                processSample = lmFilter.filter.processSample(processSample);
                processSample = applyAnalogSaturation(processSample, lmSatDrive, true);

                processSample = hmFilter.filter.processSample(processSample);
                processSample = applyAnalogSaturation(processSample, hmSatDrive, true);

                processSample = hfFilter.filter.processSample(processSample);
                processSample = applyAnalogSaturation(processSample, hfSatDrive, true);
            }
            else
            {
                processSample = lfFilter.filterR.processSample(processSample);
                processSample = applyAnalogSaturation(processSample, lfSatDrive, true);

                processSample = lmFilter.filterR.processSample(processSample);
                processSample = applyAnalogSaturation(processSample, lmSatDrive, true);

                processSample = hmFilter.filterR.processSample(processSample);
                processSample = applyAnalogSaturation(processSample, hmSatDrive, true);

                processSample = hfFilter.filterR.processSample(processSample);
                processSample = applyAnalogSaturation(processSample, hfSatDrive, true);
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

    // Convert back from M/S to L/R if enabled
    if (useMSProcessing && buffer.getNumChannels() == 2)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float mid = buffer.getSample(0, i);
            float side = buffer.getSample(1, i);

            // M+S = Left, M-S = Right
            float left = mid + side;
            float right = mid - side;

            buffer.setSample(0, i, left);
            buffer.setSample(1, i, right);
        }
    }

    // Apply output gain with auto-compensation
    if (outputGainParam)
    {
        float outputGainValue = outputGainParam->load();
        float autoCompensation = calculateAutoGainCompensation();
        float totalGain = juce::Decibels::decibelsToGain(outputGainValue) * autoCompensation;
        buffer.applyGain(totalGain);
    }

    // Copy processed buffer for spectrum analyzer (thread-safe)
    {
        const juce::ScopedLock sl(spectrumBufferLock);
        spectrumBuffer.makeCopyOf(buffer, true);
    }
}

//==============================================================================
void FourKEQ::updateFilters()
{
    // Optimized: Only update filters that have changed (per-band dirty flags)
    double oversampledRate = currentSampleRate * oversamplingFactor;

    if (hpfDirty.load())
    {
        updateHPF(oversampledRate);
        hpfDirty.store(false);
    }

    if (lpfDirty.load())
    {
        updateLPF(oversampledRate);
        lpfDirty.store(false);
    }

    if (lfDirty.load())
    {
        updateLFBand(oversampledRate);
        lfDirty.store(false);
    }

    if (lmDirty.load())
    {
        updateLMBand(oversampledRate);
        lmDirty.store(false);
    }

    if (hmDirty.load())
    {
        updateHMBand(oversampledRate);
        hmDirty.store(false);
    }

    if (hfDirty.load())
    {
        updateHFBand(oversampledRate);
        hfDirty.store(false);
    }
}

void FourKEQ::updateHPF(double sampleRate)
{
    if (!hpfFreqParam || sampleRate <= 0.0)
        return;

    float freq = hpfFreqParam->load();

    // SSL HPF: Both Brown (E-series) and Black (G-series) use 18dB/oct
    // Note: Some conflicting sources suggest Brown = 12dB/oct, but most measurements
    // and official SSL documentation confirm 18dB/oct for both variants
    //
    // Implementation: 3rd-order Butterworth (1st-order + 2nd-order cascade)
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
    if (!lpfFreqParam || !eqTypeParam || sampleRate <= 0.0)
        return;

    float freq = lpfFreqParam->load();
    bool isBlack = (eqTypeParam->load() > 0.5f);

    // Pre-warp if close to Nyquist
    float processFreq = freq;
    if (freq > sampleRate * 0.3f) {
        processFreq = preWarpFrequency(freq, sampleRate);
    }

    // Brown (E-series): 12dB/oct (2nd-order Butterworth, Q=0.707) - gentler, musical rolloff
    // Black (G-series): 12dB/oct (2nd-order with lower Q=0.5) - steeper initial slope
    // Note: Both are 12dB/oct (2nd-order). Lower Q gives steeper initial rolloff but
    // same asymptotic slope. True 18dB/oct would require 3rd-order (cascaded 1st+2nd).
    float q = isBlack ? 0.5f : 0.707f;  // Lower Q = steeper initial rolloff

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, processFreq, q);

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
        // Bell mode in Black variant - use SSL peak coefficients
        auto coeffs = makeSSLPeak(sampleRate, freq, 0.7f, gain, isBlack);
        lfFilter.filter.coefficients = coeffs;
        lfFilter.filterR.coefficients = coeffs;
    }
    else
    {
        // Shelf mode - use SSL shelf coefficients
        auto coeffs = makeSSLShelf(sampleRate, freq, 0.7f, gain, false, isBlack);
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

    // Brown vs Black mode differences (per SSL E-series vs G-series specs)
    if (isBlack)
    {
        // Black (G-series): Proportional Q - increases with gain for surgical precision
        q = calculateDynamicQ(gain, q);
    }
    // else: Brown (E-series): Fixed Q - no proportionality, maintains constant bandwidth

    // Use SSL-specific peak coefficients
    auto coeffs = makeSSLPeak(sampleRate, freq, q, gain, isBlack);

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

    // Brown vs Black mode differences (per SSL E-series vs G-series specs)
    if (isBlack)
    {
        // Black (G-series): Proportional Q, extended frequency range (up to 13kHz)
        q = calculateDynamicQ(gain, q);
        // No frequency limiting in Black mode - full 600Hz-13kHz range
    }
    else
    {
        // Brown (E-series): Fixed Q, limited to 7kHz
        // No proportionality - maintains constant bandwidth per SSL E-series design
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

    // Use SSL-specific peak coefficients
    auto coeffs = makeSSLPeak(sampleRate, processFreq, q, gain, isBlack);

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
        // Bell mode in Black variant - use SSL peak coefficients
        auto coeffs = makeSSLPeak(sampleRate, warpedFreq, 0.7f, gain, isBlack);
        hfFilter.filter.coefficients = coeffs;
        hfFilter.filterR.coefficients = coeffs;
    }
    else
    {
        // Shelf mode - use SSL shelf coefficients
        auto coeffs = makeSSLShelf(sampleRate, warpedFreq, 0.7f, gain, true, isBlack);
        hfFilter.filter.coefficients = coeffs;
        hfFilter.filterR.coefficients = coeffs;
    }
}

float FourKEQ::calculateDynamicQ(float gain, float baseQ) const
{
    // SSL Black mode proportional Q behavior (from hardware measurements):
    // Q INCREASES with gain amount - higher gain = narrower bandwidth = more focused
    // This is opposite to many generic EQs and is key to SSL's surgical character
    // Reference: SSL G-Series manual, UAD/Waves emulation analysis

    float absGain = std::abs(gain);

    // Scale factors tuned to match SSL hardware measurements
    // Black mode: Aggressive proportional Q (1.5-2.0x at full gain)
    float scale;
    if (gain >= 0.0f)
    {
        // Boosts: Q increases dramatically for surgical precision
        // At +15dB boost, Q roughly doubles (2.0x multiplier)
        scale = 2.0f;
    }
    else
    {
        // Cuts: Q increases more moderately for broad, musical reductions
        // At -15dB cut, Q increases by ~50% (1.5x multiplier)
        scale = 1.5f;
    }

    // Apply proportional Q: dynamicQ = baseQ * (1 + normalized_gain * scale)
    // Using ±20dB range (slightly exceeds hardware ±15/18dB for headroom)
    float dynamicQ = baseQ * (1.0f + (absGain / 20.0f) * scale);

    // Limit to practical range: 0.5 (broad) to 8.0 (surgical)
    return juce::jlimit(0.5f, 8.0f, dynamicQ);
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
    // Note: getRawParameterValue returns normalized [0,1], need to get actual dB values

    // Get actual dB values from parameters (not normalized)
    auto* lfParam = parameters.getParameter("lf_gain");
    auto* lmParam = parameters.getParameter("lm_gain");
    auto* hmParam = parameters.getParameter("hm_gain");
    auto* hfParam = parameters.getParameter("hf_gain");

    if (!lfParam || !lmParam || !hmParam || !hfParam)
        return 1.0f;  // No compensation if params unavailable

    // Get denormalized values in dB
    float lfGainDB = lfParam->convertFrom0to1(lfParam->getValue());
    float lmGainDB = lmParam->convertFrom0to1(lmParam->getValue());
    float hmGainDB = hmParam->convertFrom0to1(hmParam->getValue());
    float hfGainDB = hfParam->convertFrom0to1(hfParam->getValue());

    // Weight the contributions based on how broad each band typically affects spectrum
    // Shelves (LF/HF) affect more frequencies, so weight them higher
    // Reduce weights for gentler, more musical compensation
    float weightedSum = (lfGainDB * 0.35f) + (lmGainDB * 0.15f) + (hmGainDB * 0.15f) + (hfGainDB * 0.35f);

    // Calculate compensation: reduce output when boosting, increase when cutting
    // Use 20% compensation factor to maintain SSL "bigger" sound while preventing clipping
    float compensationDB = -weightedSum * 0.20f;

    // Limit compensation range to ±4dB for subtle, transparent adjustment
    compensationDB = juce::jlimit(-4.0f, 4.0f, compensationDB);

    return juce::Decibels::decibelsToGain(compensationDB);
}

//==============================================================================
// SSL-Specific Filter Coefficient Generation
// Based on hardware measurements and analog prototype matching
//==============================================================================

juce::dsp::IIR::Coefficients<float>::Ptr FourKEQ::makeSSLShelf(
    double sampleRate, float freq, float q, float gainDB, bool isHighShelf, bool isBlackMode) const
{
    // SSL shelves have characteristic asymmetric response with slight overshoot on boost
    // Black mode (G-series): Steeper slopes with subtle resonance bump at shelf transition
    // Brown mode (E-series): Gentler, broader shelves matching Brainworx/hardware measurements

    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);

    // SSL-specific Q adjustment for shelf behavior
    float sslQ = q;
    if (isBlackMode)
    {
        // Black mode: Tighter, more focused shelves with characteristic "bump"
        sslQ *= 1.35f;  // Increased from 1.2x for more pronounced slope

        // Add resonance peak for boost - creates SSL G-series characteristic overshoot
        // This subtle bump at the shelf frequency is evident in hardware measurements
        if (gainDB > 0.0f)
        {
            // Scale resonance with gain amount: subtle at low boost, more prominent at high boost
            float resonanceScale = 1.0f + (gainDB / 15.0f) * 0.4f;  // Up to +40% at +15dB
            sslQ *= resonanceScale;
        }
    }
    else
    {
        // Brown mode: Gentler, broader shelves - characteristic E-series "musical" sound
        // Reduced from 0.85x to 0.70x for even broader, more gentle transition
        sslQ *= 0.70f;
    }

    float alpha = sinw0 / (2.0f * sslQ);

    float b0, b1, b2, a0, a1, a2;

    if (isHighShelf)
    {
        // High shelf with SSL character
        b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
        b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
        a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
        a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;
    }
    else
    {
        // Low shelf with SSL character
        b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
        a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
        a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;
    }

    // Normalize
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    return new juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2);
}

juce::dsp::IIR::Coefficients<float>::Ptr FourKEQ::makeSSLPeak(
    double sampleRate, float freq, float q, float gainDB, bool isBlackMode) const
{
    // SSL peak filters have asymmetric boost/cut behavior
    // Black mode (G-series): Sharper, more surgical peaks with proportional Q
    // Brown mode (E-series): Broader, more musical peaks matching hardware measurements

    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);

    // SSL-specific Q shaping based on gain direction and mode
    float sslQ = q;
    if (gainDB > 0.0f)
    {
        // Boosts: Tighter Q for surgical precision (SSL characteristic)
        if (isBlackMode)
            sslQ *= (1.0f + gainDB * 0.05f);  // More aggressive in Black mode (increased from 0.04)
        else
            // Brown mode boosts: Keep broader for "musical" character
            // Don't tighten as much - matches Brainworx/Gearspace measurements
            sslQ *= (1.0f + gainDB * 0.01f);  // Reduced from 0.02 for gentler peaks
    }
    else
    {
        // Cuts: Broader Q for musical character
        if (isBlackMode)
            sslQ *= (1.0f - std::abs(gainDB) * 0.025f);  // Slightly broader cuts
        else
            // Brown mode cuts: Very broad and gentle
            sslQ *= (1.0f - std::abs(gainDB) * 0.015f);  // Increased from 0.01 for broader cuts
    }

    sslQ = juce::jlimit(0.1f, 10.0f, sslQ);
    float alpha = sinw0 / (2.0f * sslQ);

    // Standard peaking EQ coefficients with SSL-modified Q
    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cosw0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha / A;

    // Normalize
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    return new juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2);
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
int FourKEQ::getNumPrograms()
{
    return 10;  // 9 factory presets + 1 user (default)
}

const juce::String FourKEQ::getProgramName(int index)
{
    static const char* presetNames[] = {
        "Default",
        "Vocal Presence",
        "Kick Punch",
        "Snare Crack",
        "Bass Warmth",
        "Bright Mix",
        "Telephone EQ",
        "Air & Silk",
        "Mix Bus Glue",
        "Master Sheen"
    };

    if (index >= 0 && index < getNumPrograms())
        return presetNames[index];

    return "Unknown";
}

void FourKEQ::setCurrentProgram(int index)
{
    if (index >= 0 && index < getNumPrograms())
    {
        currentPreset = index;
        loadFactoryPreset(index);
    }
}

void FourKEQ::loadFactoryPreset(int index)
{
    // Reset all parameters to default first for clean preset loading
    auto resetToFlat = [this]()
    {
        parameters.getParameter("lf_gain")->setValueNotifyingHost(0.5f);  // 0dB
        parameters.getParameter("lf_freq")->setValueNotifyingHost(0.14f);  // 100Hz
        parameters.getParameter("lf_bell")->setValueNotifyingHost(0.0f);   // Shelf
        parameters.getParameter("lm_gain")->setValueNotifyingHost(0.5f);
        parameters.getParameter("lm_freq")->setValueNotifyingHost(0.26f);  // 600Hz
        parameters.getParameter("lm_q")->setValueNotifyingHost(0.5f);      // Q=1.0
        parameters.getParameter("hm_gain")->setValueNotifyingHost(0.5f);
        parameters.getParameter("hm_freq")->setValueNotifyingHost(0.48f);  // 3kHz
        parameters.getParameter("hm_q")->setValueNotifyingHost(0.5f);
        parameters.getParameter("hf_gain")->setValueNotifyingHost(0.5f);
        parameters.getParameter("hf_freq")->setValueNotifyingHost(0.46f);  // 10kHz
        parameters.getParameter("hf_bell")->setValueNotifyingHost(0.0f);   // Shelf
        parameters.getParameter("hpf_freq")->setValueNotifyingHost(0.0f);  // Off
        parameters.getParameter("lpf_freq")->setValueNotifyingHost(1.0f);  // Off
        parameters.getParameter("saturation")->setValueNotifyingHost(0.0f);
        parameters.getParameter("output_gain")->setValueNotifyingHost(0.5f);  // 0dB
    };

    // Reset first, then apply preset-specific changes
    resetToFlat();

    // Load factory preset parameters (SSL-inspired, musical settings)
    switch (index)
    {
        case 0:  // Default - Flat/Reset (already set by resetToFlat)
            // All parameters at neutral
            break;

        case 1:  // Vocal Clarity - Subtle presence boost without harshness
            parameters.getParameter("lf_gain")->setValueNotifyingHost(0.60f);  // +3dB @ 100Hz (warmth)
            parameters.getParameter("lm_gain")->setValueNotifyingHost(0.40f);  // -3dB @ 300Hz (reduce mud)
            parameters.getParameter("lm_freq")->setValueNotifyingHost(0.08f);  // 300Hz
            parameters.getParameter("lm_q")->setValueNotifyingHost(0.65f);     // Q=1.3 (tighter)
            parameters.getParameter("hm_gain")->setValueNotifyingHost(0.63f);  // +4dB @ 3.5kHz (presence)
            parameters.getParameter("hm_freq")->setValueNotifyingHost(0.53f);  // 3.5kHz
            parameters.getParameter("hf_gain")->setValueNotifyingHost(0.57f);  // +2dB @ 10kHz (air)
            parameters.getParameter("hpf_freq")->setValueNotifyingHost(0.25f); // HPF @ 80Hz
            break;

        case 2:  // Kick Tighten - Punch without mud
            parameters.getParameter("lf_gain")->setValueNotifyingHost(0.70f);  // +6dB @ 50Hz (thump)
            parameters.getParameter("lf_freq")->setValueNotifyingHost(0.00f);  // 50Hz
            parameters.getParameter("lm_gain")->setValueNotifyingHost(0.37f);  // -4dB @ 200Hz (tighten)
            parameters.getParameter("lm_freq")->setValueNotifyingHost(0.05f);  // 200Hz
            parameters.getParameter("lm_q")->setValueNotifyingHost(0.40f);     // Q=0.8 (broad)
            parameters.getParameter("hm_gain")->setValueNotifyingHost(0.60f);  // +3dB @ 2kHz (attack)
            parameters.getParameter("hm_freq")->setValueNotifyingHost(0.37f);  // 2kHz
            parameters.getParameter("hm_q")->setValueNotifyingHost(0.75f);     // Q=1.5 (focused)
            parameters.getParameter("hpf_freq")->setValueNotifyingHost(0.10f); // HPF @ 30Hz
            break;

        case 3:  // Snare Bite - Body and crack
            parameters.getParameter("lm_gain")->setValueNotifyingHost(0.63f);  // +4dB @ 250Hz (body)
            parameters.getParameter("lm_freq")->setValueNotifyingHost(0.07f);  // 250Hz
            parameters.getParameter("hm_gain")->setValueNotifyingHost(0.67f);  // +5dB @ 5kHz (snap)
            parameters.getParameter("hm_freq")->setValueNotifyingHost(0.69f);  // 5kHz
            parameters.getParameter("hm_q")->setValueNotifyingHost(0.60f);     // Q=1.2
            parameters.getParameter("hf_gain")->setValueNotifyingHost(0.60f);  // +3dB @ 8kHz (sizzle)
            parameters.getParameter("hf_freq")->setValueNotifyingHost(0.35f);  // 8kHz
            parameters.getParameter("hf_bell")->setValueNotifyingHost(1.0f);   // Bell mode
            parameters.getParameter("hpf_freq")->setValueNotifyingHost(0.55f); // HPF @ 150Hz
            break;

        case 4:  // Bass Definition - Punch without boom
            parameters.getParameter("lf_gain")->setValueNotifyingHost(0.63f);  // +4dB @ 80Hz
            parameters.getParameter("lf_freq")->setValueNotifyingHost(0.10f);  // 80Hz
            parameters.getParameter("lm_gain")->setValueNotifyingHost(0.40f);  // -3dB @ 400Hz (reduce mud)
            parameters.getParameter("lm_freq")->setValueNotifyingHost(0.17f);  // 400Hz
            parameters.getParameter("hm_gain")->setValueNotifyingHost(0.57f);  // +2dB @ 1.5kHz (definition)
            parameters.getParameter("hm_freq")->setValueNotifyingHost(0.24f);  // 1.5kHz
            parameters.getParameter("hm_q")->setValueNotifyingHost(0.35f);     // Q=0.7 (musical)
            parameters.getParameter("lpf_freq")->setValueNotifyingHost(0.45f); // LPF @ 10kHz
            break;

        case 5:  // Mix Polish - Subtle master bus enhancement
            parameters.getParameter("lf_gain")->setValueNotifyingHost(0.57f);  // +2dB @ 60Hz
            parameters.getParameter("lf_freq")->setValueNotifyingHost(0.03f);  // 60Hz
            parameters.getParameter("hm_gain")->setValueNotifyingHost(0.47f);  // -2dB @ 2.5kHz (smooth)
            parameters.getParameter("hm_freq")->setValueNotifyingHost(0.43f);  // 2.5kHz
            parameters.getParameter("hm_q")->setValueNotifyingHost(0.40f);     // Q=0.8 (gentle)
            parameters.getParameter("hf_gain")->setValueNotifyingHost(0.60f);  // +3dB @ 12kHz (sheen)
            parameters.getParameter("hf_freq")->setValueNotifyingHost(0.57f);  // 12kHz
            parameters.getParameter("saturation")->setValueNotifyingHost(0.20f);  // 20% saturation (glue)
            break;

        case 6:  // Telephone Effect - Lo-fi narrow bandwidth
            parameters.getParameter("hpf_freq")->setValueNotifyingHost(0.85f); // HPF @ 300Hz
            parameters.getParameter("lpf_freq")->setValueNotifyingHost(0.15f); // LPF @ 3kHz
            parameters.getParameter("lm_gain")->setValueNotifyingHost(0.70f);  // +6dB @ 1kHz
            parameters.getParameter("lm_freq")->setValueNotifyingHost(0.35f);  // 1kHz
            parameters.getParameter("lm_q")->setValueNotifyingHost(0.75f);     // Q=1.5 (narrow)
            break;

        case 7:  // Air Lift - High-end sparkle
            parameters.getParameter("hm_gain")->setValueNotifyingHost(0.60f);  // +3dB @ 7kHz
            parameters.getParameter("hm_freq")->setValueNotifyingHost(0.90f);  // 7kHz
            parameters.getParameter("hm_q")->setValueNotifyingHost(0.35f);     // Q=0.7 (smooth)
            parameters.getParameter("hf_gain")->setValueNotifyingHost(0.63f);  // +4dB @ 15kHz (air)
            parameters.getParameter("hf_freq")->setValueNotifyingHost(0.73f);  // 15kHz
            break;

        case 8:  // Glue Bus - Very subtle cohesion
            parameters.getParameter("lf_gain")->setValueNotifyingHost(0.55f);  // +1.5dB @ 100Hz
            parameters.getParameter("hm_gain")->setValueNotifyingHost(0.45f);  // -1.5dB @ 3kHz
            parameters.getParameter("hf_gain")->setValueNotifyingHost(0.57f);  // +2dB @ 10kHz
            parameters.getParameter("saturation")->setValueNotifyingHost(0.30f);  // 30% saturation
            break;

        case 9:  // Master Sheen - Polished top-end sparkle for mastering
            parameters.getParameter("hm_gain")->setValueNotifyingHost(0.525f);  // +1dB @ 5kHz (presence)
            parameters.getParameter("hm_freq")->setValueNotifyingHost(0.69f);   // 5kHz
            parameters.getParameter("hm_q")->setValueNotifyingHost(0.35f);      // Q=0.7 (smooth/broad)
            parameters.getParameter("hf_gain")->setValueNotifyingHost(0.537f);  // +1.5dB @ 16kHz (air)
            parameters.getParameter("hf_freq")->setValueNotifyingHost(0.78f);   // 16kHz
            parameters.getParameter("saturation")->setValueNotifyingHost(0.10f);  // 10% saturation (subtle glue)
            break;
    }

    parametersChanged.store(true);
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
// LV2 inline display removed - JUCE doesn't natively support this extension
// and manual export conflicts with JUCE's internal LV2 wrapper.
// The full GUI works fine in all LV2 hosts.
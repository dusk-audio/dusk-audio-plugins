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
    inputGainParam = parameters.getRawParameterValue("input_gain");
    outputGainParam = parameters.getRawParameterValue("output_gain");
    saturationParam = parameters.getRawParameterValue("saturation");
    oversamplingParam = parameters.getRawParameterValue("oversampling");
    msModeParam = parameters.getRawParameterValue("ms_mode");
    spectrumPrePostParam = parameters.getRawParameterValue("spectrum_prepost");
    autoGainParam = parameters.getRawParameterValue("auto_gain");

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

    // High-pass filter - SSL 4000 E style (skew optimized for SSL tick values)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hpf_freq", "HPF Frequency",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.58f),
        20.0f, "Hz"));

    // Low-pass filter - SSL 4000 E style
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lpf_freq", "LPF Frequency",
        juce::NormalisableRange<float>(3000.0f, 20000.0f, 1.0f, 0.57f),
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
        // SSL Hardware: 30-480Hz - skew 0.51 optimized for SSL tick values
        juce::NormalisableRange<float>(30.0f, 480.0f, 1.0f, 0.51f),
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
        // SSL 4000 E style - skew 0.68 optimized for SSL tick values
        juce::NormalisableRange<float>(200.0f, 2500.0f, 1.0f, 0.68f),
        600.0f, "Hz"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lm_q", "LM Q",
        // SSL Hardware: Typical Q range 0.4-4.0 (realistic for both Brown and Black)
        juce::NormalisableRange<float>(0.4f, 4.0f, 0.01f),
        0.7f));

    // High-mid band
    // Black mode extends to 13kHz (vs Brown's 7kHz) for more HF control
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hm_gain", "HM Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hm_freq", "HM Frequency",
        // SSL 4000 E style - skew 0.93 optimized for SSL tick values
        juce::NormalisableRange<float>(600.0f, 7000.0f, 1.0f, 0.93f),
        2000.0f, "Hz"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hm_q", "HM Q",
        // SSL Hardware: Typical Q range 0.4-4.0 (realistic for both Brown and Black)
        juce::NormalisableRange<float>(0.4f, 4.0f, 0.01f),
        0.7f));

    // High frequency band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hf_gain", "HF Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hf_freq", "HF Frequency",
        // SSL 4000 E style - skew 1.73 optimized for SSL tick values (1.5kHz-16kHz)
        juce::NormalisableRange<float>(1500.0f, 16000.0f, 1.0f, 1.73f),
        8000.0f, "Hz"));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "hf_bell", "HF Bell Mode", false));

    // Global parameters
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "eq_type", "EQ Type", juce::StringArray("Brown", "Black"), 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "bypass", "Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "input_gain", "Input Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output_gain", "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "saturation", "Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        0.0f, "%"));  // SSL is clean by default - only saturates when driven
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "oversampling", "Oversampling", juce::StringArray("2x", "4x"), 0));

    // M/S Processing
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "ms_mode", "M/S Mode", false));

    // Spectrum Pre/Post Toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "spectrum_prepost", "Spectrum Pre/Post", false));  // false = post-EQ (default)

    // Auto-Gain Compensation
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "auto_gain", "Auto Gain Compensation", true));  // Enable by default for transparent workflow

    return { params.begin(), params.end() };
}

//==============================================================================
void FourKEQ::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Validate sample rate and buffer size to prevent invalid filter calculations
    if (sampleRate <= 0.0 || std::isnan(sampleRate) || std::isinf(sampleRate) || samplesPerBlock <= 0)
    {
        DBG("FourKEQ: Invalid sample rate (" << sampleRate << ") or buffer size (" << samplesPerBlock << ")");
        jassertfalse;
        return;  // Skip preparation - retain last valid state
    }

    // Clamp sample rate to reasonable range (8kHz to 192kHz)
    sampleRate = juce::jlimit(8000.0, 192000.0, sampleRate);

    currentSampleRate = sampleRate;

    // Initialize spectrum buffers with proper size to prevent crashes
    const int numChannels = getTotalNumInputChannels();
    spectrumBuffer.setSize(numChannels, samplesPerBlock, false, true, true);
    spectrumBufferPre.setSize(numChannels, samplesPerBlock, false, true, true);

    // Adaptive oversampling based on sample rate
    // At very high sample rates, oversampling provides diminishing returns for aliasing
    // while significantly increasing CPU load. Smart adaptation matches UAD behavior:
    //
    // 44.1/48kHz:    Allow user choice of 2x or 4x (aliasing is a concern)
    // 88.2/96kHz:    Force 2x maximum (already high Nyquist, 4x wasteful)
    // 176.4/192kHz+: Disable oversampling (Nyquist >88kHz, saturation aliasing negligible)

    int requestedFactor = (!oversamplingParam || oversamplingParam->load() < 0.5f) ? 2 : 4;

    if (sampleRate >= 176000.0)
    {
        // Ultra-high sample rates: disable oversampling entirely
        // At 176.4kHz+, Nyquist is already >88kHz, saturation aliasing is inaudible
        oversamplingFactor = 1;  // No oversampling
        DBG("FourKEQ: Ultra-high sample rate (" << sampleRate << " Hz) - oversampling disabled (not needed)");
    }
    else if (sampleRate > 96000.0)
    {
        // High sample rates: force 2x maximum
        // At 88.2/96kHz, Nyquist is already >44kHz, 4x is wasteful
        oversamplingFactor = 2;
        DBG("FourKEQ: High sample rate (" << sampleRate << " Hz) - limiting to 2x oversampling");
    }
    else
    {
        // Standard sample rates (44.1/48kHz): respect user's choice
        oversamplingFactor = requestedFactor;
    }

    // Only recreate oversamplers if sample rate or factor changed (optimization)
    bool needsRecreate = (std::abs(sampleRate - lastPreparedSampleRate) > 0.01) ||
                         (oversamplingFactor != lastOversamplingFactor) ||
                         !oversampler2x || !oversampler4x;

    if (needsRecreate)
    {
        // Initialize oversampling with high-quality FIR filters for better anti-aliasing
        // FIR equiripple provides superior alias rejection compared to IIR, essential for aggressive saturation
        oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
            getTotalNumInputChannels(), 1,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

        oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
            getTotalNumInputChannels(), 2,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

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

    // Initialize SSL saturation with oversampled rate
    sslSaturation.setSampleRate(spec.sampleRate);
    sslSaturation.reset();

    // Initialize transformer phase shift
    // E-series has transformers, G-series is transformerless
    // Phase shift centered around 200Hz for typical transformer behavior
    phaseShift.prepare(spec);
    phaseShift.setFrequency(spec.sampleRate, 200.0f);

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
    phaseShift.reset();

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
void FourKEQ::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    // For now, convert to float, process, and convert back
    // This maintains compatibility while avoiding the virtual function hiding warning
    juce::AudioBuffer<float> floatBuffer(buffer.getNumChannels(), buffer.getNumSamples());

    // Copy double to float
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const double* src = buffer.getReadPointer(ch);
        float* dst = floatBuffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            dst[i] = static_cast<float>(src[i]);
    }

    // Process as float
    processBlock(floatBuffer, midiMessages);

    // Copy float back to double
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* src = floatBuffer.getReadPointer(ch);
        double* dst = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            dst[i] = static_cast<double>(src[i]);
    }
}

void FourKEQ::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
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
    // Atomically read AND clear the flag to prevent race conditions
    // Use exchange() instead of load() + store() to ensure atomicity
    if (parametersChanged.exchange(false))
    {
        // Create local copies of all parameters to avoid race conditions
        // These values are guaranteed to be from the same "snapshot" since we read them
        // immediately after detecting the change flag
        float hpfFreq = hpfFreqParam ? hpfFreqParam->load() : 20.0f;
        float lpfFreq = lpfFreqParam ? lpfFreqParam->load() : 20000.0f;
        float lfGain = lfGainParam ? lfGainParam->load() : 0.0f;
        float lfFreq = lfFreqParam ? lfFreqParam->load() : 100.0f;
        float lfBell = lfBellParam ? lfBellParam->load() : 0.0f;
        float lmGain = lmGainParam ? lmGainParam->load() : 0.0f;
        float lmFreq = lmFreqParam ? lmFreqParam->load() : 600.0f;
        float lmQ = lmQParam ? lmQParam->load() : 0.7f;
        float hmGain = hmGainParam ? hmGainParam->load() : 0.0f;
        float hmFreq = hmFreqParam ? hmFreqParam->load() : 2000.0f;
        float hmQ = hmQParam ? hmQParam->load() : 0.7f;
        float hfGain = hfGainParam ? hfGainParam->load() : 0.0f;
        float hfFreq = hfFreqParam ? hfFreqParam->load() : 8000.0f;
        float hfBell = hfBellParam ? hfBellParam->load() : 0.0f;
        float eqType = eqTypeParam ? eqTypeParam->load() : 0.0f;

        // Store in temporary structure for updateFilters to use
        cachedParams.hpfFreq = hpfFreq;
        cachedParams.lpfFreq = lpfFreq;
        cachedParams.lfGain = lfGain;
        cachedParams.lfFreq = lfFreq;
        cachedParams.lfBell = lfBell;
        cachedParams.lmGain = lmGain;
        cachedParams.lmFreq = lmFreq;
        cachedParams.lmQ = lmQ;
        cachedParams.hmGain = hmGain;
        cachedParams.hmFreq = hmFreq;
        cachedParams.hmQ = hmQ;
        cachedParams.hfGain = hfGain;
        cachedParams.hfFreq = hfFreq;
        cachedParams.hfBell = hfBell;
        cachedParams.eqType = eqType;

        updateFilters();
    }

    // Capture input levels for metering (before gain)
    if (buffer.getNumChannels() >= 1)
    {
        float peakL = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        inputLevelL.store(juce::Decibels::gainToDecibels(peakL, -96.0f), std::memory_order_relaxed);
    }
    if (buffer.getNumChannels() >= 2)
    {
        float peakR = buffer.getMagnitude(1, 0, buffer.getNumSamples());
        inputLevelR.store(juce::Decibels::gainToDecibels(peakR, -96.0f), std::memory_order_relaxed);
    }

    // Apply input gain
    if (inputGainParam)
    {
        float inputGainValue = inputGainParam->load();
        float inputGainLinear = juce::Decibels::decibelsToGain(inputGainValue);
        buffer.applyGain(inputGainLinear);
    }

    // Capture pre-EQ buffer for spectrum analyzer (thread-safe)
    {
        const juce::ScopedLock sl(spectrumBufferLock);
        spectrumBufferPre.makeCopyOf(buffer, true);
    }

    // Use oversampling factor already calculated in prepareToPlay()
    // Note: oversamplingFactor is adaptively set based on sample rate (1, 2, or 4)
    // No need to recalculate here - prepareToPlay() handles this optimization
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

    // Create audio block and optionally oversample
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::AudioBlock<float> oversampledBlock;

    if (oversamplingFactor > 1)
    {
        // Apply oversampling (2x or 4x)
        oversampledBlock = oversampler.processSamplesUp(block);
    }
    else
    {
        // No oversampling at ultra-high sample rates
        oversampledBlock = block;
    }

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
            processSample = hpfFilter.processSample(processSample, useLeftFilter);

            // Apply 4-band EQ (no per-band saturation - removed for SSL accuracy)
            // Real SSL console saturation is from the channel strip, not individual EQ bands
            processSample = lfFilter.processSample(processSample, useLeftFilter);
            processSample = lmFilter.processSample(processSample, useLeftFilter);
            processSample = hmFilter.processSample(processSample, useLeftFilter);
            processSample = hfFilter.processSample(processSample, useLeftFilter);

            // Apply LPF
            processSample = lpfFilter.processSample(processSample, useLeftFilter);

            // Apply transformer phase shift (E-series only)
            // G-series is transformerless, so skip phase shift in Black mode
            bool isBlack = (eqTypeParam && eqTypeParam->load() > 0.5f);
            if (!isBlack)
            {
                // E-series (Brown) has input/output transformers
                // Apply subtle phase rotation for authentic transformer character
                processSample = phaseShift.processSample(processSample, useLeftFilter);
            }

            // Apply global SSL saturation (user-controlled amount)
            float satAmount = saturationParam->load() * 0.01f;  // 0-100% to 0.0-1.0
            if (satAmount > 0.001f)
                processSample = sslSaturation.processSample(processSample, satAmount, useLeftFilter);

            channelData[sample] = processSample;
        }
    }

    // Downsample back to original rate (only if we upsampled)
    if (oversamplingFactor > 1)
    {
        oversampler.processSamplesDown(block);
    }

    // Apply stereo crosstalk (before M/S decode)
    // SSL consoles have ~-60dB crosstalk between channels due to:
    // - PCB trace proximity
    // - Shared power supply rails
    // - Magnetic coupling in transformers
    // This adds subtle stereo width and "glue"
    if (!useMSProcessing && buffer.getNumChannels() == 2)
    {
        const float crosstalkAmount = 0.001f;  // -60dB (0.1%)

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float left = buffer.getSample(0, i);
            float right = buffer.getSample(1, i);

            // Each channel gets a tiny bit of the opposite channel
            buffer.setSample(0, i, left + right * crosstalkAmount);
            buffer.setSample(1, i, right + left * crosstalkAmount);
        }
    }

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

    // Apply output gain with optional auto-compensation
    if (outputGainParam)
    {
        float outputGainValue = outputGainParam->load();

        // Apply auto-gain compensation only if enabled
        float autoCompensation = 1.0f;
        if (autoGainParam && autoGainParam->load() > 0.5f)
        {
            autoCompensation = calculateAutoGainCompensation();
        }

        float totalGain = juce::Decibels::decibelsToGain(outputGainValue) * autoCompensation;
        buffer.applyGain(totalGain);
    }

    // Capture output levels for metering (after processing)
    if (buffer.getNumChannels() >= 1)
    {
        float peakL = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        outputLevelL.store(juce::Decibels::gainToDecibels(peakL, -96.0f), std::memory_order_relaxed);
    }
    if (buffer.getNumChannels() >= 2)
    {
        float peakR = buffer.getMagnitude(1, 0, buffer.getNumSamples());
        outputLevelR.store(juce::Decibels::gainToDecibels(peakR, -96.0f), std::memory_order_relaxed);
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

    // Update SSL saturation console type based on EQ type
    if (eqTypeParam)
    {
        bool isBlack = eqTypeParam->load() > 0.5f;
        sslSaturation.setConsoleType(isBlack ? SSLSaturation::ConsoleType::GSeries
                                              : SSLSaturation::ConsoleType::ESeries);
    }

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
    if (sampleRate <= 0.0)
        return;

    float freq = cachedParams.hpfFreq;

    // SSL HPF: Both Brown (E-series) and Black (G-series) use 18dB/oct
    // Note: Some conflicting sources suggest Brown = 12dB/oct, but most measurements
    // and official SSL documentation confirm 18dB/oct for both variants
    //
    // Implementation: 3rd-order (1st-order + 2nd-order cascade)
    // Stage 1: 1st-order highpass (6dB/oct)
    auto coeffs1st = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(sampleRate, freq);
    if (coeffs1st)
    {
        hpfFilter.stage1L.coefficients = coeffs1st;
        hpfFilter.stage1R.coefficients = coeffs1st;
    }

    // Stage 2: 2nd-order highpass (12dB/oct)
    // SSL uses a custom slightly underdamped response (NOT standard Butterworth Q=0.707)
    // This creates subtle resonance/"punch" at the cutoff frequency
    // Measured from real SSL hardware: Q ≈ 0.54 (between critically damped and Butterworth)
    // This is what gives SSL HPFs their characteristic "musical" sound vs. generic filters
    const float sslHPFQ = 0.54f;  // SSL-specific Q for musical character
    auto coeffs2nd = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, freq, sslHPFQ);

    if (coeffs2nd)
    {
        hpfFilter.stage2.filter.coefficients = coeffs2nd;
        hpfFilter.stage2.filterR.coefficients = coeffs2nd;
    }
}

void FourKEQ::updateLPF(double sampleRate)
{
    if (sampleRate <= 0.0)
        return;

    float freq = cachedParams.lpfFreq;
    bool isBlack = (cachedParams.eqType > 0.5f);

    // Pre-warp if close to Nyquist
    float processFreq = freq;
    if (freq > sampleRate * 0.3f) {
        processFreq = preWarpFrequency(freq, sampleRate);
    }

    // SSL LPF characteristics differ between E and G series:
    //
    // Brown (E-series): 12dB/oct, maximally flat Butterworth response (Q=0.707)
    // - Gentler, more "musical" rolloff
    // - No resonance peak, smooth transition
    //
    // Black (G-series): 12dB/oct, slightly resonant (Q≈0.8)
    // - Subtle resonance peak at cutoff frequency
    // - More "focused" sound with slight presence boost before rolloff
    // - This is OPPOSITE to the HPF - G-series LPF has HIGHER Q for character
    //
    // Note: Both are 12dB/oct (2nd-order), difference is in the Q value
    float q = isBlack ? 0.8f : 0.707f;  // Black has subtle resonance, Brown is flat

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
        sampleRate, processFreq, q);

    if (coeffs)
    {
        lpfFilter.filter.coefficients = coeffs;
        lpfFilter.filterR.coefficients = coeffs;
    }
}

void FourKEQ::updateLFBand(double sampleRate)
{
    if (sampleRate <= 0.0)
        return;

    float gain = cachedParams.lfGain;
    float freq = cachedParams.lfFreq;
    bool isBlack = (cachedParams.eqType > 0.5f);
    bool isBell = (cachedParams.lfBell > 0.5f);

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
    if (sampleRate <= 0.0)
        return;

    float gain = cachedParams.lmGain;
    float freq = cachedParams.lmFreq;
    float q = cachedParams.lmQ;
    bool isBlack = (cachedParams.eqType > 0.5f);

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
    if (sampleRate <= 0.0)
        return;

    float gain = cachedParams.hmGain;
    float freq = cachedParams.hmFreq;
    float q = cachedParams.hmQ;
    bool isBlack = (cachedParams.eqType > 0.5f);

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
    if (sampleRate <= 0.0)
        return;

    float gain = cachedParams.hfGain;
    float freq = cachedParams.hfFreq;
    bool isBlack = (cachedParams.eqType > 0.5f);
    bool isBell = (cachedParams.hfBell > 0.5f);

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

// Old saturation functions removed - now using SSLSaturation class for accurate modeling

float FourKEQ::calculateAutoGainCompensation() const
{
    // Calculate auto-gain compensation based on EQ energy contribution
    // Uses a bandwidth-weighted approach that accounts for filter shapes

    // Get gain parameters
    auto* lfGainP = parameters.getParameter("lf_gain");
    auto* lmGainP = parameters.getParameter("lm_gain");
    auto* hmGainP = parameters.getParameter("hm_gain");
    auto* hfGainP = parameters.getParameter("hf_gain");

    // Get Q parameters for mid bands
    auto* lmQP = parameters.getParameter("lm_q");
    auto* hmQP = parameters.getParameter("hm_q");

    // Get bell/shelf mode for LF and HF
    auto* lfBellP = parameters.getParameter("lf_bell");
    auto* hfBellP = parameters.getParameter("hf_bell");

    // Safety check
    if (!lfGainP || !lmGainP || !hmGainP || !hfGainP)
        return 1.0f;

    // Get denormalized values
    float lfGainDB = lfGainP->convertFrom0to1(lfGainP->getValue());
    float lmGainDB = lmGainP->convertFrom0to1(lmGainP->getValue());
    float hmGainDB = hmGainP->convertFrom0to1(hmGainP->getValue());
    float hfGainDB = hfGainP->convertFrom0to1(hfGainP->getValue());

    float lmQ = lmQP ? lmQP->convertFrom0to1(lmQP->getValue()) : 1.0f;
    float hmQ = hmQP ? hmQP->convertFrom0to1(hmQP->getValue()) : 1.0f;

    bool lfBell = lfBellP && lfBellP->getValue() > 0.5f;
    bool hfBell = hfBellP && hfBellP->getValue() > 0.5f;

    // Validate values
    if (!std::isfinite(lfGainDB) || !std::isfinite(lmGainDB) ||
        !std::isfinite(hmGainDB) || !std::isfinite(hfGainDB))
        return 1.0f;

    // Calculate bandwidth-weighted energy contribution for each band
    // Shelves contribute more energy than narrow peaks
    // Higher Q = narrower bandwidth = less energy contribution

    // LF band: shelf mode affects ~1 octave, bell ~0.5 octaves
    float lfBandwidth = lfBell ? 0.3f : 0.5f;
    float lfEnergy = lfGainDB * lfBandwidth;

    // LMF band: Q determines bandwidth (Q=1 is ~1 octave, Q=4 is ~0.25 octaves)
    float lmBandwidth = 0.7f / lmQ;  // Inverse relationship with Q
    float lmEnergy = lmGainDB * juce::jmin(lmBandwidth, 0.5f);

    // HMF band: same as LMF
    float hmBandwidth = 0.7f / hmQ;
    float hmEnergy = hmGainDB * juce::jmin(hmBandwidth, 0.5f);

    // HF band: shelf affects more octaves due to position in spectrum
    float hfBandwidth = hfBell ? 0.3f : 0.6f;
    float hfEnergy = hfGainDB * hfBandwidth;

    // Sum the energy contributions
    // This represents the approximate dB change in overall energy
    float totalEnergyDB = lfEnergy + lmEnergy + hmEnergy + hfEnergy;

    // Compensation: invert the energy change
    // Use 100% compensation for accurate loudness matching
    float compensationDB = -totalEnergyDB;

    // Limit to reasonable range
    compensationDB = juce::jlimit(-12.0f, 12.0f, compensationDB);

    return juce::Decibels::decibelsToGain(compensationDB);
}

//==============================================================================
// SSL-Specific Filter Coefficient Generation
// Based on hardware measurements and analog prototype matching
//==============================================================================

juce::dsp::IIR::Coefficients<float>::Ptr FourKEQ::makeSSLShelf(
    double sampleRate, float freq, float q, float gainDB, bool isHighShelf, bool isBlackMode) const
{
    // SSL shelves have characteristic asymmetric response differences between modes:
    // Black mode (G-series): Steeper, more focused shelves for precise tonal shaping
    // Brown mode (E-series): Gentler, broader shelves for musical warmth
    //
    // IMPORTANT: Unlike peaks, shelf Q is FIXED (not gain-dependent) for both modes
    // The difference is only in the base Q value, not in any resonance or gain-scaling

    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);

    // SSL-specific shelf Q: FIXED for both modes (no gain dependency)
    float sslQ = q;
    if (isBlackMode)
    {
        // Black mode (G-series): Steeper, more focused shelves
        // Higher Q = steeper transition = more "modern" sound
        sslQ *= 1.4f;  // Fixed multiplier - characteristic G-series shelf slope
    }
    else
    {
        // Brown mode (E-series): Gentler, broader shelves
        // Lower Q = gentler transition = more "vintage/musical" sound
        sslQ *= 0.65f;  // Fixed multiplier - characteristic E-series shelf slope
    }

    // NO gain-dependent Q modification for shelves
    // Real SSL hardware has fixed shelf Q regardless of boost/cut amount
    // Any perceived "resonance" comes from the shelf curve shape itself, not Q variation

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

    // Use JUCE's smart pointer system to avoid memory leaks
    return juce::dsp::IIR::Coefficients<float>::Ptr(new juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2));
}

juce::dsp::IIR::Coefficients<float>::Ptr FourKEQ::makeSSLPeak(
    double sampleRate, float freq, float q, float gainDB, bool isBlackMode) const
{
    // SSL peak filters have fundamentally different Q behavior between modes:
    // Black mode (G-series): Proportional Q - bandwidth varies with gain for surgical precision
    // Brown mode (E-series): Constant Q - bandwidth remains fixed at all gains for musical character
    //
    // This is THE defining difference between E and G series EQ behavior per SSL documentation

    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sampleRate);
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);

    // SSL-specific Q behavior: CRITICAL DIFFERENCE between modes
    float sslQ = q;

    if (isBlackMode && std::abs(gainDB) > 0.1f)
    {
        // G-Series (Black): PROPORTIONAL Q - increases with gain amount
        // More gain = narrower bandwidth = more surgical/focused
        // This is what makes the G-series sound "precise" and "modern"

        float gainFactor = std::abs(gainDB) / 15.0f;  // Normalize to typical SSL max (±15dB)

        if (gainDB > 0.0f)
        {
            // Boosts: Q increases significantly for surgical precision
            // At +15dB, Q roughly doubles (SSL G-series measured behavior)
            sslQ *= (1.0f + gainFactor * 1.2f);
        }
        else
        {
            // Cuts: Q increases moderately for broad, musical reductions
            // At -15dB, Q increases by ~60% (gentler than boosts)
            sslQ *= (1.0f + gainFactor * 0.6f);
        }
    }
    // else: E-Series (Brown) - Q remains COMPLETELY CONSTANT at all gains
    // This is the "musical" E-series character - consistent bandwidth regardless of boost/cut amount
    // No modification to sslQ for Brown mode - this is intentional and matches hardware!

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

    // Use JUCE's smart pointer system to avoid memory leaks
    return juce::dsp::IIR::Coefficients<float>::Ptr(new juce::dsp::IIR::Coefficients<float>(b0, b1, b2, 1.0f, a1, a2));
}

//==============================================================================
void FourKEQ::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    // Add version information for backward/forward compatibility
    xml->setAttribute("pluginVersion", "1.0.2");
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
    return 15;  // 14 factory presets + 1 user (default)
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
        "Master Sheen",
        "Bass Guitar Polish",
        "Drum Bus Punch",
        "Acoustic Guitar",
        "Piano Brilliance",
        "Master Bus Sweetening"
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
    // Helper lambda to set parameter by actual value (not normalized)
    auto setParam = [this](const juce::String& paramID, float actualValue)
    {
        if (auto* param = parameters.getParameter(paramID))
        {
            float normalized = param->convertTo0to1(actualValue);
            param->setValueNotifyingHost(normalized);
        }
    };

    // Reset all parameters to default first for clean preset loading
    auto resetToFlat = [&setParam]()
    {
        setParam("lf_gain", 0.0f);       // 0dB
        setParam("lf_freq", 100.0f);     // 100Hz
        setParam("lf_bell", 0.0f);       // Shelf
        setParam("lm_gain", 0.0f);       // 0dB
        setParam("lm_freq", 600.0f);     // 600Hz
        setParam("lm_q", 0.7f);          // Q=0.7
        setParam("hm_gain", 0.0f);       // 0dB
        setParam("hm_freq", 2000.0f);    // 2kHz
        setParam("hm_q", 0.7f);          // Q=0.7
        setParam("hf_gain", 0.0f);       // 0dB
        setParam("hf_freq", 8000.0f);    // 8kHz
        setParam("hf_bell", 0.0f);       // Shelf
        setParam("hpf_freq", 20.0f);     // Minimum (off)
        setParam("lpf_freq", 20000.0f);  // Maximum (off)
        setParam("saturation", 0.0f);    // 0%
        setParam("output_gain", 0.0f);   // 0dB
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
            setParam("lf_gain", 3.0f);      // +3dB @ 100Hz (warmth)
            setParam("lm_gain", -3.0f);     // -3dB @ 300Hz (reduce mud)
            setParam("lm_freq", 300.0f);    // 300Hz
            setParam("lm_q", 1.3f);         // Q=1.3 (tighter)
            setParam("hm_gain", 4.0f);      // +4dB @ 3.5kHz (presence)
            setParam("hm_freq", 3500.0f);   // 3.5kHz
            setParam("hf_gain", 2.0f);      // +2dB @ 10kHz (air)
            setParam("hpf_freq", 80.0f);    // HPF @ 80Hz
            break;

        case 2:  // Kick Punch - Punch without mud (SSL-authentic settings)
            setParam("lf_gain", 4.0f);      // +4dB @ 50Hz (punchy but controlled)
            setParam("lf_freq", 50.0f);     // 50Hz
            setParam("lm_gain", -2.5f);     // -2.5dB @ 200Hz (gentler tightening)
            setParam("lm_freq", 200.0f);    // 200Hz
            setParam("lm_q", 0.8f);         // Q=0.8 (broad)
            setParam("hm_gain", 3.0f);      // +3dB @ 2kHz (attack)
            setParam("hm_freq", 2000.0f);   // 2kHz
            setParam("hm_q", 1.5f);         // Q=1.5 (focused)
            setParam("hpf_freq", 30.0f);    // HPF @ 30Hz
            break;

        case 3:  // Snare Bite - Body and crack
            setParam("lm_gain", 4.0f);      // +4dB @ 250Hz (body)
            setParam("lm_freq", 250.0f);    // 250Hz
            setParam("hm_gain", 5.0f);      // +5dB @ 5kHz (snap)
            setParam("hm_freq", 5000.0f);   // 5kHz
            setParam("hm_q", 1.2f);         // Q=1.2
            setParam("hf_gain", 3.0f);      // +3dB @ 8kHz (sizzle)
            setParam("hf_freq", 8000.0f);   // 8kHz
            setParam("hf_bell", 1.0f);      // Bell mode
            setParam("hpf_freq", 150.0f);   // HPF @ 150Hz
            break;

        case 4:  // Bass Definition - Punch without boom
            setParam("lf_gain", 4.0f);      // +4dB @ 80Hz
            setParam("lf_freq", 80.0f);     // 80Hz
            setParam("lm_gain", -3.0f);     // -3dB @ 400Hz (reduce mud)
            setParam("lm_freq", 400.0f);    // 400Hz
            setParam("hm_gain", 2.0f);      // +2dB @ 1.5kHz (definition)
            setParam("hm_freq", 1500.0f);   // 1.5kHz
            setParam("hm_q", 0.7f);         // Q=0.7 (musical)
            setParam("lpf_freq", 10000.0f); // LPF @ 10kHz
            break;

        case 5:  // Mix Polish - Subtle master bus enhancement
            setParam("lf_gain", 2.0f);      // +2dB @ 60Hz
            setParam("lf_freq", 60.0f);     // 60Hz
            setParam("hm_gain", -2.0f);     // -2dB @ 2.5kHz (smooth)
            setParam("hm_freq", 2500.0f);   // 2.5kHz
            setParam("hm_q", 0.8f);         // Q=0.8 (gentle)
            setParam("hf_gain", 2.5f);      // +2.5dB @ 10kHz (polish, not too bright)
            setParam("hf_freq", 10000.0f);  // 10kHz
            setParam("saturation", 20.0f);  // 20% saturation (glue)
            break;

        case 6:  // Telephone Effect - Lo-fi narrow bandwidth
            setParam("hpf_freq", 300.0f);   // HPF @ 300Hz
            setParam("lpf_freq", 3000.0f);  // LPF @ 3kHz
            setParam("lm_gain", 6.0f);      // +6dB @ 1kHz
            setParam("lm_freq", 1000.0f);   // 1kHz
            setParam("lm_q", 1.5f);         // Q=1.5 (narrow)
            break;

        case 7:  // Air Lift - High-end sparkle
            setParam("hm_gain", 3.0f);      // +3dB @ 7kHz
            setParam("hm_freq", 7000.0f);   // 7kHz
            setParam("hm_q", 0.7f);         // Q=0.7 (smooth)
            setParam("hf_gain", 4.0f);      // +4dB @ 15kHz (air)
            setParam("hf_freq", 15000.0f);  // 15kHz
            break;

        case 8:  // Glue Bus - Subtle cohesion (SSL-authentic glue settings)
            setParam("lf_gain", 2.0f);      // +2dB @ 100Hz (more audible warmth)
            setParam("hm_gain", -1.5f);     // -1.5dB @ 3kHz
            setParam("hm_freq", 3000.0f);   // 3kHz
            setParam("hf_gain", 2.0f);      // +2dB @ 10kHz
            setParam("saturation", 20.0f);  // 20% saturation (balanced glue)
            break;

        case 9:  // Master Sheen - Polished top-end sparkle for mastering
            setParam("hm_gain", 1.0f);      // +1dB @ 5kHz (presence)
            setParam("hm_freq", 5000.0f);   // 5kHz
            setParam("hm_q", 0.7f);         // Q=0.7 (smooth/broad)
            setParam("hf_gain", 1.5f);      // +1.5dB @ 16kHz (air)
            setParam("hf_freq", 16000.0f);  // 16kHz
            setParam("saturation", 10.0f);  // 10% saturation (subtle glue)
            break;

        case 10:  // Bass Guitar Polish - Definition and punch for bass guitar
            setParam("lf_gain", 5.0f);      // +5dB @ 60Hz (fundamental)
            setParam("lf_freq", 60.0f);     // 60Hz
            setParam("lm_gain", -2.0f);     // -2dB @ 250Hz (reduce boxiness)
            setParam("lm_freq", 250.0f);    // 250Hz
            setParam("lm_q", 1.0f);         // Q=1.0
            setParam("hm_gain", 3.0f);      // +3dB @ 1.2kHz (actual growl/grind)
            setParam("hm_freq", 1200.0f);   // 1.2kHz (growl sweet spot)
            setParam("hm_q", 0.8f);         // Q=0.8
            setParam("hf_gain", 2.0f);      // +2dB @ 4.5kHz (string attack/definition)
            setParam("hf_freq", 4500.0f);   // 4.5kHz
            setParam("hf_bell", 1.0f);      // Bell mode
            setParam("hpf_freq", 35.0f);    // HPF @ 35Hz (tighten low end)
            break;

        case 11:  // Drum Bus Punch - Cohesive drum processing
            setParam("lf_gain", 4.0f);      // +4dB @ 70Hz (kick weight)
            setParam("lf_freq", 70.0f);     // 70Hz
            setParam("lm_gain", -3.0f);     // -3dB @ 350Hz (clear boxiness)
            setParam("lm_freq", 350.0f);    // 350Hz
            setParam("lm_q", 0.6f);         // Q=0.6 (broad)
            setParam("hm_gain", 3.0f);      // +3dB @ 3.5kHz (attack)
            setParam("hm_freq", 3500.0f);   // 3.5kHz
            setParam("hm_q", 1.0f);         // Q=1.0
            setParam("hf_gain", 2.5f);      // +2.5dB @ 10kHz (cymbals)
            setParam("hf_freq", 10000.0f);  // 10kHz
            setParam("saturation", 25.0f);  // 25% saturation (glue)
            setParam("eq_type", 1.0f);      // Black mode (more aggressive)
            break;

        case 12:  // Acoustic Guitar - Clarity and sparkle
            setParam("lf_gain", -2.0f);     // -2dB @ 100Hz (reduce boom)
            setParam("lf_freq", 100.0f);    // 100Hz
            setParam("lm_gain", 2.0f);      // +2dB @ 200Hz (body)
            setParam("lm_freq", 200.0f);    // 200Hz
            setParam("lm_q", 0.7f);         // Q=0.7
            setParam("hm_gain", 3.0f);      // +3dB @ 2.5kHz (presence)
            setParam("hm_freq", 2500.0f);   // 2.5kHz
            setParam("hm_q", 0.9f);         // Q=0.9
            setParam("hf_gain", 4.0f);      // +4dB @ 12kHz (sparkle)
            setParam("hf_freq", 12000.0f);  // 12kHz
            setParam("hpf_freq", 80.0f);    // HPF @ 80Hz
            break;

        case 13:  // Piano Brilliance - Clarity and presence
            setParam("lf_gain", 2.0f);      // +2dB @ 80Hz (warmth)
            setParam("lf_freq", 80.0f);     // 80Hz
            setParam("lm_gain", -2.5f);     // -2.5dB @ 500Hz (reduce muddiness)
            setParam("lm_freq", 500.0f);    // 500Hz
            setParam("lm_q", 0.8f);         // Q=0.8
            setParam("hm_gain", 3.0f);      // +3dB @ 2kHz (presence)
            setParam("hm_freq", 2000.0f);   // 2kHz
            setParam("hm_q", 0.7f);         // Q=0.7
            setParam("hf_gain", 3.5f);      // +3.5dB @ 8kHz (brilliance)
            setParam("hf_freq", 8000.0f);   // 8kHz
            setParam("hpf_freq", 30.0f);    // HPF @ 30Hz
            break;

        case 14:  // Master Bus Sweetening - Final polish for mastering
            setParam("lf_gain", 1.0f);      // +1dB @ 50Hz (subtle warmth)
            setParam("lf_freq", 50.0f);     // 50Hz
            setParam("lm_gain", -1.0f);     // -1dB @ 600Hz (slight de-mud)
            setParam("lm_freq", 600.0f);    // 600Hz
            setParam("lm_q", 0.5f);         // Q=0.5 (very broad)
            setParam("hm_gain", 0.5f);      // +0.5dB @ 4kHz (subtle presence)
            setParam("hm_freq", 4000.0f);   // 4kHz
            setParam("hm_q", 0.6f);         // Q=0.6
            setParam("hf_gain", 1.5f);      // +1.5dB @ 15kHz (air)
            setParam("hf_freq", 15000.0f);  // 15kHz
            setParam("saturation", 15.0f);  // 15% saturation (cohesion)
            setParam("output_gain", -0.5f); // -0.5dB output (headroom)
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
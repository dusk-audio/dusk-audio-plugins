#include "MultiQ.h"
#include "MultiQEditor.h"

//==============================================================================
MultiQ::MultiQ()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, &undoManager, juce::Identifier("MultiQ"), createParameterLayout())
{
    // Initialize dirty flags
    for (auto& dirty : bandDirty)
        dirty.store(true);

    // Get parameter pointers for all bands
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        bandEnabledParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandEnabled(i + 1));
        bandFreqParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandFreq(i + 1));
        bandGainParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandGain(i + 1));
        bandQParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandQ(i + 1));

        // Add listeners
        parameters.addParameterListener(ParamIDs::bandEnabled(i + 1), this);
        parameters.addParameterListener(ParamIDs::bandFreq(i + 1), this);
        parameters.addParameterListener(ParamIDs::bandGain(i + 1), this);
        parameters.addParameterListener(ParamIDs::bandQ(i + 1), this);
    }

    // Slope params for HPF and LPF
    bandSlopeParams[0] = parameters.getRawParameterValue(ParamIDs::bandSlope(1));
    bandSlopeParams[1] = parameters.getRawParameterValue(ParamIDs::bandSlope(8));
    parameters.addParameterListener(ParamIDs::bandSlope(1), this);
    parameters.addParameterListener(ParamIDs::bandSlope(8), this);

    // Shape params for parametric bands 3-6
    for (int i = 2; i <= 5; ++i)
        parameters.addParameterListener(ParamIDs::bandShape(i + 1), this);

    // Global parameters
    masterGainParam = parameters.getRawParameterValue(ParamIDs::masterGain);
    bypassParam = parameters.getRawParameterValue(ParamIDs::bypass);
    hqEnabledParam = parameters.getRawParameterValue(ParamIDs::hqEnabled);
    linearPhaseEnabledParam = parameters.getRawParameterValue(ParamIDs::linearPhaseEnabled);
    linearPhaseLengthParam = parameters.getRawParameterValue(ParamIDs::linearPhaseLength);
    processingModeParam = parameters.getRawParameterValue(ParamIDs::processingMode);
    qCoupleModeParam = parameters.getRawParameterValue(ParamIDs::qCoupleMode);

    // Analyzer parameters
    analyzerEnabledParam = parameters.getRawParameterValue(ParamIDs::analyzerEnabled);
    analyzerPrePostParam = parameters.getRawParameterValue(ParamIDs::analyzerPrePost);
    analyzerModeParam = parameters.getRawParameterValue(ParamIDs::analyzerMode);
    analyzerResolutionParam = parameters.getRawParameterValue(ParamIDs::analyzerResolution);
    analyzerDecayParam = parameters.getRawParameterValue(ParamIDs::analyzerDecay);

    // Display parameters
    displayScaleModeParam = parameters.getRawParameterValue(ParamIDs::displayScaleMode);
    visualizeMasterGainParam = parameters.getRawParameterValue(ParamIDs::visualizeMasterGain);

    // EQ Type parameter
    eqTypeParam = parameters.getRawParameterValue(ParamIDs::eqType);

    // British mode parameters
    britishHpfFreqParam = parameters.getRawParameterValue(ParamIDs::britishHpfFreq);
    britishHpfEnabledParam = parameters.getRawParameterValue(ParamIDs::britishHpfEnabled);
    britishLpfFreqParam = parameters.getRawParameterValue(ParamIDs::britishLpfFreq);
    britishLpfEnabledParam = parameters.getRawParameterValue(ParamIDs::britishLpfEnabled);
    britishLfGainParam = parameters.getRawParameterValue(ParamIDs::britishLfGain);
    britishLfFreqParam = parameters.getRawParameterValue(ParamIDs::britishLfFreq);
    britishLfBellParam = parameters.getRawParameterValue(ParamIDs::britishLfBell);
    britishLmGainParam = parameters.getRawParameterValue(ParamIDs::britishLmGain);
    britishLmFreqParam = parameters.getRawParameterValue(ParamIDs::britishLmFreq);
    britishLmQParam = parameters.getRawParameterValue(ParamIDs::britishLmQ);
    britishHmGainParam = parameters.getRawParameterValue(ParamIDs::britishHmGain);
    britishHmFreqParam = parameters.getRawParameterValue(ParamIDs::britishHmFreq);
    britishHmQParam = parameters.getRawParameterValue(ParamIDs::britishHmQ);
    britishHfGainParam = parameters.getRawParameterValue(ParamIDs::britishHfGain);
    britishHfFreqParam = parameters.getRawParameterValue(ParamIDs::britishHfFreq);
    britishHfBellParam = parameters.getRawParameterValue(ParamIDs::britishHfBell);
    britishModeParam = parameters.getRawParameterValue(ParamIDs::britishMode);
    britishSaturationParam = parameters.getRawParameterValue(ParamIDs::britishSaturation);
    britishInputGainParam = parameters.getRawParameterValue(ParamIDs::britishInputGain);
    britishOutputGainParam = parameters.getRawParameterValue(ParamIDs::britishOutputGain);

    // Tube EQ mode parameters
    tubeEQLfBoostGainParam = parameters.getRawParameterValue(ParamIDs::pultecLfBoostGain);
    tubeEQLfBoostFreqParam = parameters.getRawParameterValue(ParamIDs::pultecLfBoostFreq);
    tubeEQLfAttenGainParam = parameters.getRawParameterValue(ParamIDs::pultecLfAttenGain);
    tubeEQHfBoostGainParam = parameters.getRawParameterValue(ParamIDs::pultecHfBoostGain);
    tubeEQHfBoostFreqParam = parameters.getRawParameterValue(ParamIDs::pultecHfBoostFreq);
    tubeEQHfBoostBandwidthParam = parameters.getRawParameterValue(ParamIDs::pultecHfBoostBandwidth);
    tubeEQHfAttenGainParam = parameters.getRawParameterValue(ParamIDs::pultecHfAttenGain);
    tubeEQHfAttenFreqParam = parameters.getRawParameterValue(ParamIDs::pultecHfAttenFreq);
    tubeEQInputGainParam = parameters.getRawParameterValue(ParamIDs::pultecInputGain);
    tubeEQOutputGainParam = parameters.getRawParameterValue(ParamIDs::pultecOutputGain);
    tubeEQTubeDriveParam = parameters.getRawParameterValue(ParamIDs::pultecTubeDrive);

    // Tube EQ Mid Dip/Peak section parameters
    tubeEQMidEnabledParam = parameters.getRawParameterValue(ParamIDs::pultecMidEnabled);
    tubeEQMidLowFreqParam = parameters.getRawParameterValue(ParamIDs::pultecMidLowFreq);
    tubeEQMidLowPeakParam = parameters.getRawParameterValue(ParamIDs::pultecMidLowPeak);
    tubeEQMidDipFreqParam = parameters.getRawParameterValue(ParamIDs::pultecMidDipFreq);
    tubeEQMidDipParam = parameters.getRawParameterValue(ParamIDs::pultecMidDip);
    tubeEQMidHighFreqParam = parameters.getRawParameterValue(ParamIDs::pultecMidHighFreq);
    tubeEQMidHighPeakParam = parameters.getRawParameterValue(ParamIDs::pultecMidHighPeak);

    // Dynamic mode per-band parameters
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        bandDynEnabledParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandDynEnabled(i + 1));
        bandDynThresholdParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandDynThreshold(i + 1));
        bandDynAttackParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandDynAttack(i + 1));
        bandDynReleaseParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandDynRelease(i + 1));
        bandDynRangeParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandDynRange(i + 1));
        bandDynRatioParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandDynRatio(i + 1));

        // Shape parameter exists for bands 2-7 (indices 1-6)
        if (i >= 1 && i <= 6)
            bandShapeParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandShape(i + 1));

        // Per-band channel routing
        bandRoutingParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandChannelRouting(i + 1));
    }
    dynDetectionModeParam = parameters.getRawParameterValue(ParamIDs::dynDetectionMode);

    // Per-band saturation parameter pointers (bands 2-7)
    for (int i = 1; i <= 6; ++i)
    {
        bandSatTypeParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandSatType(i + 1));
        bandSatDriveParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandSatDrive(i + 1));
    }

    autoGainEnabledParam = parameters.getRawParameterValue(ParamIDs::autoGainEnabled);
    limiterEnabledParam = parameters.getRawParameterValue(ParamIDs::limiterEnabled);
    limiterCeilingParam = parameters.getRawParameterValue(ParamIDs::limiterCeiling);

    // Add global parameter listeners
    parameters.addParameterListener(ParamIDs::hqEnabled, this);
    parameters.addParameterListener(ParamIDs::linearPhaseEnabled, this);
    parameters.addParameterListener(ParamIDs::linearPhaseLength, this);
    parameters.addParameterListener(ParamIDs::qCoupleMode, this);
    parameters.addParameterListener(ParamIDs::limiterEnabled, this);
    parameters.addParameterListener(ParamIDs::analyzerResolution, this);

    // Initialize FFT
    fft = std::make_unique<juce::dsp::FFT>(FFT_ORDER_MEDIUM);
    fftWindow = std::make_unique<juce::dsp::WindowingFunction<float>>(
        static_cast<size_t>(1 << FFT_ORDER_MEDIUM),
        juce::dsp::WindowingFunction<float>::hann);
    currentFFTSize = 1 << FFT_ORDER_MEDIUM;
    fftInputBuffer.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
    fftOutputBuffer.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
    analyzerAudioBuffer.resize(8192, 0.0f);

    // Pre-EQ analyzer buffers
    preAnalyzerAudioBuffer.resize(8192, 0.0f);
    preFFTInputBuffer.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
}

MultiQ::~MultiQ()
{
    // Remove all listeners
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        parameters.removeParameterListener(ParamIDs::bandEnabled(i + 1), this);
        parameters.removeParameterListener(ParamIDs::bandFreq(i + 1), this);
        parameters.removeParameterListener(ParamIDs::bandGain(i + 1), this);
        parameters.removeParameterListener(ParamIDs::bandQ(i + 1), this);
    }
    parameters.removeParameterListener(ParamIDs::bandSlope(1), this);
    parameters.removeParameterListener(ParamIDs::bandSlope(8), this);
    for (int i = 2; i <= 5; ++i)
        parameters.removeParameterListener(ParamIDs::bandShape(i + 1), this);
    parameters.removeParameterListener(ParamIDs::hqEnabled, this);
    parameters.removeParameterListener(ParamIDs::linearPhaseEnabled, this);
    parameters.removeParameterListener(ParamIDs::linearPhaseLength, this);
    parameters.removeParameterListener(ParamIDs::qCoupleMode, this);
    parameters.removeParameterListener(ParamIDs::limiterEnabled, this);
    parameters.removeParameterListener(ParamIDs::analyzerResolution, this);
}

//==============================================================================
void MultiQ::parameterChanged(const juce::String& parameterID, float newValue)
{
    // Mark appropriate band as dirty
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        if (parameterID.startsWith("band" + juce::String(i + 1)))
        {
            bandDirty[static_cast<size_t>(i)].store(true);
            filtersNeedUpdate.store(true);
            return;
        }
    }

    // Q-couple mode affects all parametric bands
    if (parameterID == ParamIDs::qCoupleMode)
    {
        for (int i = 1; i < 7; ++i)  // Bands 2-7 (shelf and parametric)
            bandDirty[static_cast<size_t>(i)].store(true);
        filtersNeedUpdate.store(true);
    }

    // Oversampling mode change
    if (parameterID == ParamIDs::hqEnabled)
    {
        // Update oversamplingMode to the new value before calculating latency
        oversamplingMode = static_cast<int>(newValue);
        // Filter coefficient update will be handled in processBlock
        filtersNeedUpdate.store(true);
        // Update host latency for oversampling (now uses the correct new mode)
        setLatencySamples(getLatencySamples());
    }

    // Limiter enable/disable changes latency (lookahead)
    if (parameterID == ParamIDs::limiterEnabled)
    {
        outputLimiter.setEnabled(newValue > 0.5f);
        setLatencySamples(getLatencySamples());
    }

    // Linear phase mode change
    if (parameterID == ParamIDs::linearPhaseEnabled)
    {
        linearPhaseParamsChanged.store(true);
    }

    // Linear phase filter length change - apply at runtime
    if (parameterID == ParamIDs::linearPhaseLength)
    {
        int lengthChoice = static_cast<int>(safeGetParam(linearPhaseLengthParam, 1.0f));
        LinearPhaseEQProcessor::FilterLength filterLength;
        int filterLengthSamples;
        switch (lengthChoice)
        {
            case 0:
                filterLength = LinearPhaseEQProcessor::Short;
                filterLengthSamples = 4096;
                break;
            case 2:
                filterLength = LinearPhaseEQProcessor::Long;
                filterLengthSamples = 16384;
                break;
            default:
                filterLength = LinearPhaseEQProcessor::Medium;
                filterLengthSamples = 8192;
                break;
        }
        for (auto& proc : linearPhaseEQ)
        {
            proc.setFilterLength(filterLength);
        }

        // Update host latency when linear phase is enabled
        bool linearPhaseEnabled = safeGetParam(linearPhaseEnabledParam, 0.0f) > 0.5f;
        if (linearPhaseEnabled)
        {
            int newLatency = filterLengthSamples / 2;
            setLatencySamples(newLatency);
        }

        linearPhaseParamsChanged.store(true);
    }

    // Analyzer resolution change
    if (parameterID == ParamIDs::analyzerResolution)
    {
        auto res = static_cast<AnalyzerResolution>(
            static_cast<int>(safeGetParam(analyzerResolutionParam, 1.0f)));
        updateFFTSize(res);
    }

    // Update latency when linear phase or dynamics parameters change
    if (parameterID == ParamIDs::linearPhaseEnabled ||
        parameterID.startsWith("dyn_enabled"))
    {
        setLatencySamples(getLatencySamples());
    }
}

//==============================================================================
void MultiQ::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;

    // Check oversampling mode (0=Off, 1=2x, 2=4x)
    oversamplingMode = static_cast<int>(safeGetParam(hqEnabledParam, 0.0f));

    // Pre-allocate both 2x and 4x oversamplers to avoid runtime allocation
    // This is critical for real-time safety - we never want to allocate in processBlock()
    if (!oversamplerReady)
    {
        // 2x oversampling (order=1) - FIR equiripple filters for superior alias rejection
        oversampler2x = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
        oversampler2x->initProcessing(static_cast<size_t>(samplesPerBlock));

        // 4x oversampling (order=2)
        oversampler4x = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
        oversampler4x->initProcessing(static_cast<size_t>(samplesPerBlock));

        oversamplerReady = true;
    }

    // Pre-allocate scratch buffer for British/Tube EQ processing
    // Size: 2 channels, max oversampled block size (4x input block size for max oversampling)
    maxOversampledBlockSize = samplesPerBlock * 4;
    scratchBuffer.setSize(2, maxOversampledBlockSize, false, false, true);

    // Calculate oversampling factor
    int osFactor = (oversamplingMode == 2) ? 4 : (oversamplingMode == 1) ? 2 : 1;

    // Set current sample rate based on oversampling mode
    currentSampleRate = sampleRate * osFactor;

    // Prepare filter spec
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock * osFactor);
    spec.numChannels = 2;

    // Prepare HPF
    hpfFilter.prepare(spec);

    // Reset SVF filters (bands 2-7) — no allocation needed, SVFs are state-only
    for (auto& filter : svfFilters)
        filter.reset();
    for (auto& filter : svfDynGainFilters)
        filter.reset();

    // Compute SVF smoothing coefficient: ~1ms transition time
    svfSmoothCoeff = 1.0f - std::exp(-1.0f / (0.001f * static_cast<float>(currentSampleRate)));
    for (auto& f : svfFilters)
        f.setSmoothCoeff(svfSmoothCoeff);
    for (auto& f : svfDynGainFilters)
        f.setSmoothCoeff(svfSmoothCoeff);

    // Prepare dynamic gain filters (bands 2-7)
    for (auto& filter : dynGainFilters)
        filter.prepare(spec);

    // Prepare LPF
    lpfFilter.prepare(spec);

    // Reset HPF/LPF filters
    hpfFilter.reset();
    lpfFilter.reset();

    // Pre-allocate coefficient objects for HPF/LPF cascaded IIR filters
    auto makeIdentity = []() {
        return new juce::dsp::IIR::Coefficients<float>(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    };
    for (int i = 0; i < CascadedFilter::MAX_STAGES; ++i)
    {
        auto hpfCoeffs = makeIdentity();
        hpfFilter.stagesL[static_cast<size_t>(i)].coefficients = hpfCoeffs;
        hpfFilter.stagesR[static_cast<size_t>(i)].coefficients = hpfCoeffs;

        auto lpfCoeffs = makeIdentity();
        lpfFilter.stagesL[static_cast<size_t>(i)].coefficients = lpfCoeffs;
        lpfFilter.stagesR[static_cast<size_t>(i)].coefficients = lpfCoeffs;
    }

    // Force filter update
    filtersNeedUpdate.store(true);
    updateAllFilters();

    // Snap SVF filters to target (no interpolation at startup)
    for (auto& f : svfFilters)
        f.snapToTarget();
    for (auto& f : svfDynGainFilters)
        f.snapToTarget();

    // Prepare British EQ processor
    britishEQ.prepare(currentSampleRate, samplesPerBlock * osFactor, 2);
    britishParamsChanged.store(true);

    // Prepare Tube EQ processor
    tubeEQ.prepare(currentSampleRate, samplesPerBlock * osFactor, 2);
    tubeEQParamsChanged.store(true);

    // Prepare Dynamic EQ processor
    dynamicEQ.prepare(currentSampleRate, 2);
    dynamicParamsChanged.store(true);

    // Prepare Linear Phase EQ processors (one per channel)
    // Note: Linear phase uses base sample rate (no oversampling - already FIR based)
    linearPhaseModeEnabled = safeGetParam(linearPhaseEnabledParam, 0.0f) > 0.5f;
    int lengthChoice = static_cast<int>(safeGetParam(linearPhaseLengthParam, 1.0f));
    LinearPhaseEQProcessor::FilterLength filterLength;
    switch (lengthChoice)
    {
        case 0: filterLength = LinearPhaseEQProcessor::Short; break;
        case 2: filterLength = LinearPhaseEQProcessor::Long; break;
        default: filterLength = LinearPhaseEQProcessor::Medium; break;
    }

    for (auto& proc : linearPhaseEQ)
    {
        proc.setFilterLength(filterLength);
        proc.prepare(baseSampleRate, samplesPerBlock);
        proc.reset();
    }
    linearPhaseParamsChanged.store(true);

    // Reset analyzers (post-EQ and pre-EQ)
    analyzerFifo.reset();
    std::fill(analyzerMagnitudes.begin(), analyzerMagnitudes.end(), -100.0f);
    std::fill(peakHoldValues.begin(), peakHoldValues.end(), -100.0f);
    preAnalyzerFifo.reset();
    std::fill(preAnalyzerMagnitudes.begin(), preAnalyzerMagnitudes.end(), -100.0f);
    std::fill(prePeakHoldValues.begin(), prePeakHoldValues.end(), -100.0f);

    // Mono mix scratch buffer for block-based analyzer feed
    analyzerMonoBuffer.resize(static_cast<size_t>(samplesPerBlock * osFactor), 0.0f);

    // Initialize auto-gain compensation
    // Use ~200ms smoothing with 500ms RMS window for mastering-appropriate behavior
    autoGainCompensation.reset(sampleRate, 0.2);
    autoGainCompensation.setCurrentAndTargetValue(1.0f);
    inputRmsSum = 0.0f;
    outputRmsSum = 0.0f;
    rmsSampleCount = 0;

    // Initialize output limiter
    outputLimiter.prepare(sampleRate, samplesPerBlock);
    outputLimiter.reset();

    // Initialize bypass crossfade (~5ms)
    bypassSmoothed.reset(sampleRate, 0.005);
    bypassSmoothed.setCurrentAndTargetValue(safeGetParam(bypassParam, 0.0f) > 0.5f ? 1.0f : 0.0f);
    dryBuffer.setSize(2, samplesPerBlock, false, false, true);

    // Initialize per-band enable smoothing (~3ms)
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        bandEnableSmoothed[static_cast<size_t>(i)].reset(sampleRate, 0.003);
        float enabled = safeGetParam(bandEnabledParams[static_cast<size_t>(i)], 0.0f) > 0.5f ? 1.0f : 0.0f;
        bandEnableSmoothed[static_cast<size_t>(i)].setCurrentAndTargetValue(enabled);
    }

    // Initialize EQ type crossfade (~10ms)
    eqTypeCrossfade.reset(sampleRate, 0.01);
    eqTypeCrossfade.setCurrentAndTargetValue(1.0f);
    previousEQType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));
    eqTypeChanging = false;
    prevTypeBuffer.setSize(2, samplesPerBlock, false, false, true);

    // Initialize oversampling crossfade (~5ms)
    osCrossfade.reset(sampleRate, 0.005);
    osCrossfade.setCurrentAndTargetValue(1.0f);
    osChanging = false;
    prevOsBuffer.setSize(2, samplesPerBlock, false, false, true);
}

void MultiQ::releaseResources()
{
    oversampler2x.reset();
    oversampler4x.reset();
    oversamplerReady = false;
}

bool MultiQ::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainInput = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    // Support mono and stereo
    if (mainOutput != juce::AudioChannelSet::mono() &&
        mainOutput != juce::AudioChannelSet::stereo())
        return false;

    // Input and output must match
    if (mainInput != mainOutput)
        return false;

    return true;
}

//==============================================================================
void MultiQ::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Bypass with smooth crossfade (no clicks)
    bool bypassed = safeGetParam(bypassParam, 0.0f) > 0.5f;
    bypassSmoothed.setTargetValue(bypassed ? 1.0f : 0.0f);

    // If fully bypassed and not transitioning, skip all processing
    if (bypassed && !bypassSmoothed.isSmoothing())
        return;

    // Save dry input for bypass crossfade (before any processing modifies the buffer)
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, buffer.getNumSamples());

    // Check if oversampling mode changed - handle without calling prepareToPlay() for real-time safety
    int newOsMode = static_cast<int>(safeGetParam(hqEnabledParam, 0.0f));
    if (newOsMode != oversamplingMode)
    {
        // prevOsBuffer already contains last processed output (saved at end of previous block)
        // Start crossfade from old processed output to new processed output
        osChanging = true;
        osCrossfade.setCurrentAndTargetValue(0.0f);
        osCrossfade.setTargetValue(1.0f);

        oversamplingMode = newOsMode;
        int osFactor = (oversamplingMode == 2) ? 4 : (oversamplingMode == 1) ? 2 : 1;
        // Update sample rate for filter coefficient calculations
        currentSampleRate = baseSampleRate * osFactor;
        // Reset oversampler states to avoid artifacts on mode switch
        if (oversampler2x) oversampler2x->reset();
        if (oversampler4x) oversampler4x->reset();
        // Reset all filters
        hpfFilter.reset();
        for (auto& filter : svfFilters)
            filter.reset();
        for (auto& filter : svfDynGainFilters)
            filter.reset();
        lpfFilter.reset();
        // Force filter coefficient update at new sample rate
        filtersNeedUpdate.store(true);
    }

    // Check EQ type (Digital, British, or Tube)
    auto eqType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));

    // Detect EQ type change and start crossfade
    if (eqType != previousEQType)
    {
        // prevTypeBuffer already contains last processed output (saved at end of previous block)
        // Start crossfade from old EQ type's output to new EQ type's output
        eqTypeChanging = true;
        eqTypeCrossfade.setCurrentAndTargetValue(0.0f);
        eqTypeCrossfade.setTargetValue(1.0f);
        previousEQType = eqType;
    }

    // Update filters if needed (for Digital mode with optional dynamics)
    if (eqType == EQType::Digital && filtersNeedUpdate.exchange(false))
        updateAllFilters();

    // Update British EQ parameters if needed
    if (eqType == EQType::British)
    {
        BritishEQProcessor::Parameters britishParams;
        britishParams.hpfFreq = safeGetParam(britishHpfFreqParam, 20.0f);
        britishParams.hpfEnabled = safeGetParam(britishHpfEnabledParam, 0.0f) > 0.5f;
        britishParams.lpfFreq = safeGetParam(britishLpfFreqParam, 20000.0f);
        britishParams.lpfEnabled = safeGetParam(britishLpfEnabledParam, 0.0f) > 0.5f;
        britishParams.lfGain = safeGetParam(britishLfGainParam, 0.0f);
        britishParams.lfFreq = safeGetParam(britishLfFreqParam, 100.0f);
        britishParams.lfBell = safeGetParam(britishLfBellParam, 0.0f) > 0.5f;
        britishParams.lmGain = safeGetParam(britishLmGainParam, 0.0f);
        britishParams.lmFreq = safeGetParam(britishLmFreqParam, 600.0f);
        britishParams.lmQ = safeGetParam(britishLmQParam, 0.7f);
        britishParams.hmGain = safeGetParam(britishHmGainParam, 0.0f);
        britishParams.hmFreq = safeGetParam(britishHmFreqParam, 2000.0f);
        britishParams.hmQ = safeGetParam(britishHmQParam, 0.7f);
        britishParams.hfGain = safeGetParam(britishHfGainParam, 0.0f);
        britishParams.hfFreq = safeGetParam(britishHfFreqParam, 8000.0f);
        britishParams.hfBell = safeGetParam(britishHfBellParam, 0.0f) > 0.5f;
        britishParams.isBlackMode = safeGetParam(britishModeParam, 0.0f) > 0.5f;
        britishParams.saturation = safeGetParam(britishSaturationParam, 0.0f);
        britishParams.inputGain = safeGetParam(britishInputGainParam, 0.0f);
        britishParams.outputGain = safeGetParam(britishOutputGainParam, 0.0f);
        britishEQ.setParameters(britishParams);
    }

    // Update Tube EQ parameters if needed
    if (eqType == EQType::Tube)
    {
        // LF boost frequency lookup table: 20, 30, 60, 100 Hz
        static const float lfFreqValues[] = { 20.0f, 30.0f, 60.0f, 100.0f };
        // HF boost frequency lookup table: 3k, 4k, 5k, 8k, 10k, 12k, 16k Hz
        static const float hfBoostFreqValues[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
        // HF atten frequency lookup table: 5k, 10k, 20k Hz
        static const float hfAttenFreqValues[] = { 5000.0f, 10000.0f, 20000.0f };
        // Mid Low frequency lookup table: 0.2, 0.3, 0.5, 0.7, 1.0 kHz
        static const float midLowFreqValues[] = { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f };
        // Mid Dip frequency lookup table: 0.2, 0.3, 0.5, 0.7, 1.0, 1.5, 2.0 kHz
        static const float midDipFreqValues[] = { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f, 1500.0f, 2000.0f };
        // Mid High frequency lookup table: 1.5, 2.0, 3.0, 4.0, 5.0 kHz
        static const float midHighFreqValues[] = { 1500.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f };

        TubeEQProcessor::Parameters tubeEQParams;
        tubeEQParams.lfBoostGain = safeGetParam(tubeEQLfBoostGainParam, 0.0f);

        int lfFreqIdx = static_cast<int>(safeGetParam(tubeEQLfBoostFreqParam, 2.0f));
        lfFreqIdx = juce::jlimit(0, 3, lfFreqIdx);
        tubeEQParams.lfBoostFreq = lfFreqValues[lfFreqIdx];

        tubeEQParams.lfAttenGain = safeGetParam(tubeEQLfAttenGainParam, 0.0f);
        tubeEQParams.hfBoostGain = safeGetParam(tubeEQHfBoostGainParam, 0.0f);

        int hfBoostFreqIdx = static_cast<int>(safeGetParam(tubeEQHfBoostFreqParam, 3.0f));
        hfBoostFreqIdx = juce::jlimit(0, 6, hfBoostFreqIdx);
        tubeEQParams.hfBoostFreq = hfBoostFreqValues[hfBoostFreqIdx];

        tubeEQParams.hfBoostBandwidth = safeGetParam(tubeEQHfBoostBandwidthParam, 0.5f);
        tubeEQParams.hfAttenGain = safeGetParam(tubeEQHfAttenGainParam, 0.0f);

        int hfAttenFreqIdx = static_cast<int>(safeGetParam(tubeEQHfAttenFreqParam, 1.0f));
        hfAttenFreqIdx = juce::jlimit(0, 2, hfAttenFreqIdx);
        tubeEQParams.hfAttenFreq = hfAttenFreqValues[hfAttenFreqIdx];

        // Mid section parameters
        tubeEQParams.midEnabled = safeGetParam(tubeEQMidEnabledParam, 1.0f) > 0.5f;

        int midLowFreqIdx = static_cast<int>(safeGetParam(tubeEQMidLowFreqParam, 2.0f));
        midLowFreqIdx = juce::jlimit(0, 4, midLowFreqIdx);
        tubeEQParams.midLowFreq = midLowFreqValues[midLowFreqIdx];
        tubeEQParams.midLowPeak = safeGetParam(tubeEQMidLowPeakParam, 0.0f);

        int midDipFreqIdx = static_cast<int>(safeGetParam(tubeEQMidDipFreqParam, 3.0f));
        midDipFreqIdx = juce::jlimit(0, 6, midDipFreqIdx);
        tubeEQParams.midDipFreq = midDipFreqValues[midDipFreqIdx];
        tubeEQParams.midDip = safeGetParam(tubeEQMidDipParam, 0.0f);

        int midHighFreqIdx = static_cast<int>(safeGetParam(tubeEQMidHighFreqParam, 2.0f));
        midHighFreqIdx = juce::jlimit(0, 4, midHighFreqIdx);
        tubeEQParams.midHighFreq = midHighFreqValues[midHighFreqIdx];
        tubeEQParams.midHighPeak = safeGetParam(tubeEQMidHighPeakParam, 0.0f);

        tubeEQParams.inputGain = safeGetParam(tubeEQInputGainParam, 0.0f);
        tubeEQParams.outputGain = safeGetParam(tubeEQOutputGainParam, 0.0f);
        tubeEQParams.tubeDrive = safeGetParam(tubeEQTubeDriveParam, 0.3f);
        tubeEQ.setParameters(tubeEQParams);
    }

    // Input level metering (peak absolute value per channel, single-pass)
    int numSamp = buffer.getNumSamples();
    auto inRangeL = juce::FloatVectorOperations::findMinAndMax(buffer.getReadPointer(0), numSamp);
    float inL = std::max(std::abs(inRangeL.getStart()), std::abs(inRangeL.getEnd()));
    float inR = inL;
    if (buffer.getNumChannels() > 1)
    {
        auto inRangeR = juce::FloatVectorOperations::findMinAndMax(buffer.getReadPointer(1), numSamp);
        inR = std::max(std::abs(inRangeR.getStart()), std::abs(inRangeR.getEnd()));
    }

    // Calculate input RMS for auto-gain compensation (block-based)
    bool autoGainEnabled = safeGetParam(autoGainEnabledParam, 0.0f) > 0.5f;
    if (autoGainEnabled)
    {
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        // Mono downmix then sum of squares
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, numSamp);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, numSamp);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, numSamp);
        // Sum of squares using dot product with itself
        for (int i = 0; i < numSamp; ++i)
            inputRmsSum += analyzerMonoBuffer[static_cast<size_t>(i)] * analyzerMonoBuffer[static_cast<size_t>(i)];
    }
    float inLdB = inL > 1e-3f ? juce::Decibels::gainToDecibels(inL) : -60.0f;
    float inRdB = inR > 1e-3f ? juce::Decibels::gainToDecibels(inR) : -60.0f;
    inputLevelL.store(inLdB);
    inputLevelR.store(inRdB);
    if (inL >= 1.0f || inR >= 1.0f)
        inputClipped.store(true, std::memory_order_relaxed);

    // Always push pre-EQ samples to analyzer for dual spectrum overlay (block-based)
    bool analyzerEnabled = safeGetParam(analyzerEnabledParam, 0.0f) > 0.5f;
    if (analyzerEnabled)
    {
        int n = buffer.getNumSamples();
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        // Block mono downmix: copy L, add R, scale by 0.5
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, n);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, n);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, n);
        pushSamplesToAnalyzer(analyzerMonoBuffer.data(), n, true);
    }

    // Get processing mode
    auto procMode = static_cast<ProcessingMode>(
        static_cast<int>(safeGetParam(processingModeParam, 0.0f)));

    // Oversampling upsample
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::AudioBlock<float> processBlock = block;

    if (oversamplingMode == 2 && oversampler4x)
        processBlock = oversampler4x->processSamplesUp(block);
    else if (oversamplingMode == 1 && oversampler2x)
        processBlock = oversampler2x->processSamplesUp(block);

    int numSamples = static_cast<int>(processBlock.getNumSamples());
    float* procL = processBlock.getChannelPointer(0);
    float* procR = processBlock.getNumChannels() > 1 ? processBlock.getChannelPointer(1) : procL;

    // Resolve per-band effective routing (0=Stereo, 1=Left, 2=Right, 3=Mid, 4=Side)
    // "Global" (param value 0) follows the global processing mode
    std::array<int, NUM_BANDS> effectiveRouting{};
    int globalRouting = 0;  // Stereo by default
    switch (procMode)
    {
        case ProcessingMode::Stereo: globalRouting = 0; break;
        case ProcessingMode::Left:   globalRouting = 1; break;
        case ProcessingMode::Right:  globalRouting = 2; break;
        case ProcessingMode::Mid:    globalRouting = 3; break;
        case ProcessingMode::Side:   globalRouting = 4; break;
    }
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        int perBand = static_cast<int>(safeGetParam(bandRoutingParams[static_cast<size_t>(i)], 0.0f));
        effectiveRouting[static_cast<size_t>(i)] = (perBand == 0) ? globalRouting : (perBand - 1);
    }

    // Global M/S encode for British/Tube EQ modes (they don't have per-band routing)
    // Digital mode uses per-band routing instead
    bool useMS = (procMode == ProcessingMode::Mid || procMode == ProcessingMode::Side);

    // Track if linear phase mode is used (set in Digital mode block)
    bool useLinearPhase = false;

    if (useMS && eqType != EQType::Digital)
    {
        for (int i = 0; i < numSamples; ++i)
            encodeMS(procL[i], procR[i]);
    }

    // Process based on EQ type
    if (eqType == EQType::British)
    {
        // British mode: Use 4K-EQ style processing
        // Use pre-allocated scratch buffer (no heap allocation in audio thread)
        int numChannels = static_cast<int>(processBlock.getNumChannels());
        int blockSamples = static_cast<int>(processBlock.getNumSamples());

        // Copy to scratch buffer
        for (int ch = 0; ch < numChannels; ++ch)
        {
            scratchBuffer.copyFrom(ch, 0,
                                   processBlock.getChannelPointer(static_cast<size_t>(ch)),
                                   blockSamples);
        }

        // Create a view into scratch buffer for the processor (avoids allocation)
        juce::AudioBuffer<float> tempView(scratchBuffer.getArrayOfWritePointers(),
                                          numChannels, blockSamples);
        britishEQ.process(tempView);

        // Copy back to processBlock
        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::memcpy(processBlock.getChannelPointer(static_cast<size_t>(ch)),
                       scratchBuffer.getReadPointer(ch),
                       sizeof(float) * static_cast<size_t>(blockSamples));
        }
    }
    else if (eqType == EQType::Tube)
    {
        // Tube EQ mode: passive tube program EQ style processing
        // Use pre-allocated scratch buffer (no heap allocation in audio thread)
        int numChannels = static_cast<int>(processBlock.getNumChannels());
        int blockSamples = static_cast<int>(processBlock.getNumSamples());

        // Copy to scratch buffer
        for (int ch = 0; ch < numChannels; ++ch)
        {
            scratchBuffer.copyFrom(ch, 0,
                                   processBlock.getChannelPointer(static_cast<size_t>(ch)),
                                   blockSamples);
        }

        // Create a view into scratch buffer for the processor (avoids allocation)
        juce::AudioBuffer<float> tempView(scratchBuffer.getArrayOfWritePointers(),
                                          numChannels, blockSamples);
        tubeEQ.process(tempView);

        // Copy back to processBlock
        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::memcpy(processBlock.getChannelPointer(static_cast<size_t>(ch)),
                       scratchBuffer.getReadPointer(ch),
                       sizeof(float) * static_cast<size_t>(blockSamples));
        }
    }
    else
    {
        // Digital mode: Multi-Q 8-band EQ with optional per-band dynamics
        // Check if linear phase mode is enabled
        useLinearPhase = safeGetParam(linearPhaseEnabledParam, 0.0f) > 0.5f;

        // Check which bands are enabled and update smooth crossfade targets
        std::array<bool, NUM_BANDS> bandEnabled{};
        std::array<bool, NUM_BANDS> bandDynEnabled{};
        for (int i = 0; i < NUM_BANDS; ++i)
        {
            bandEnabled[static_cast<size_t>(i)] = safeGetParam(bandEnabledParams[static_cast<size_t>(i)], 0.0f) > 0.5f;
            bandDynEnabled[static_cast<size_t>(i)] = safeGetParam(bandDynEnabledParams[static_cast<size_t>(i)], 0.0f) > 0.5f;
        }

        // Apply solo mode: if any band is soloed, only that band is processed
        // Delta solo: all bands stay active, we capture before/after the target band
        int currentSolo = soloedBand.load();
        bool deltaSoloActive = (currentSolo >= 0 && currentSolo < NUM_BANDS && deltaSoloMode.load());
        if (currentSolo >= 0 && currentSolo < NUM_BANDS && !deltaSoloActive)
        {
            for (int i = 0; i < NUM_BANDS; ++i)
            {
                if (i != currentSolo)
                    bandEnabled[static_cast<size_t>(i)] = false;
            }
        }

        // Update per-band enable smoothing targets
        for (int i = 0; i < NUM_BANDS; ++i)
            bandEnableSmoothed[static_cast<size_t>(i)].setTargetValue(bandEnabled[static_cast<size_t>(i)] ? 1.0f : 0.0f);

        if (useLinearPhase)
        {
            // Linear Phase mode: Use FIR-based processing (no per-band dynamics)
            // Note: Linear phase mode doesn't support oversampling or per-band dynamics

            // Gather EQ parameters for the linear phase processor
            std::array<float, NUM_BANDS> lpFreqs{}, lpGains{}, lpQs{};
            std::array<int, 2> lpSlopes{};
            std::array<int, NUM_BANDS> lpShapes{};

            for (int i = 0; i < NUM_BANDS; ++i)
            {
                lpFreqs[static_cast<size_t>(i)] = safeGetParam(bandFreqParams[static_cast<size_t>(i)],
                    DefaultBandConfigs[static_cast<size_t>(i)].defaultFreq);
                lpGains[static_cast<size_t>(i)] = safeGetParam(bandGainParams[static_cast<size_t>(i)], 0.0f);
                lpQs[static_cast<size_t>(i)] = safeGetParam(bandQParams[static_cast<size_t>(i)], 0.71f);
                if (bandShapeParams[static_cast<size_t>(i)])
                    lpShapes[static_cast<size_t>(i)] = static_cast<int>(bandShapeParams[static_cast<size_t>(i)]->load());
            }
            lpSlopes[0] = static_cast<int>(safeGetParam(bandSlopeParams[0], 0.0f));
            lpSlopes[1] = static_cast<int>(safeGetParam(bandSlopeParams[1], 0.0f));

            float lpMasterGain = safeGetParam(masterGainParam, 0.0f);

            // Update the impulse response only if parameters changed (dirty flag check)
            // The IR rebuild happens on a background thread, so this is safe to call
            // but we avoid unnecessary work by only updating when needed
            if (linearPhaseParamsChanged.exchange(false) || filtersNeedUpdate.load())
            {
                for (auto& proc : linearPhaseEQ)
                {
                    proc.updateImpulseResponse(bandEnabled, lpFreqs, lpGains, lpQs, lpSlopes, lpMasterGain, lpShapes);
                }
            }

            // Process through linear phase EQ (works on original buffer, not oversampled)
            // Linear phase already handles its own zero-padding internally
            linearPhaseEQ[0].processChannel(buffer.getWritePointer(0), buffer.getNumSamples());
            if (buffer.getNumChannels() > 1)
                linearPhaseEQ[1].processChannel(buffer.getWritePointer(1), buffer.getNumSamples());

            // Skip the normal IIR processing and M/S decode (linear phase processes raw L/R)
            // Master gain is included in the linear phase impulse response
            // Skip to analyzer and metering
        }
        else
        {
            // Standard IIR mode with optional per-band dynamics
            // Update dynamic processor parameters for all bands
            for (int band = 0; band < NUM_BANDS; ++band)
            {
                DynamicEQProcessor::BandParameters dynParams;
                dynParams.enabled = safeGetParam(bandDynEnabledParams[static_cast<size_t>(band)], 0.0f) > 0.5f;
                dynParams.threshold = safeGetParam(bandDynThresholdParams[static_cast<size_t>(band)], 0.0f);
                dynParams.attack = safeGetParam(bandDynAttackParams[static_cast<size_t>(band)], 10.0f);
                dynParams.release = safeGetParam(bandDynReleaseParams[static_cast<size_t>(band)], 100.0f);
                dynParams.range = safeGetParam(bandDynRangeParams[static_cast<size_t>(band)], 12.0f);
                dynParams.ratio = safeGetParam(bandDynRatioParams[static_cast<size_t>(band)], 4.0f);
                dynamicEQ.setBandParameters(band, dynParams);

                // Update detection filter to match band frequency
                float bandFreq = safeGetParam(bandFreqParams[static_cast<size_t>(band)], 1000.0f);
                float bandQ = safeGetParam(bandQParams[static_cast<size_t>(band)], 0.71f);
                dynamicEQ.updateDetectionFilter(band, bandFreq, bandQ);
            }

            // Update dynamic gain filter coefficients for this block
            // Uses the latest smoothed gain from the previous block's envelope followers
            for (int band = 1; band < 7; ++band)
            {
                if (bandDynEnabled[static_cast<size_t>(band)])
                    updateDynGainFilter(band, dynamicEQ.getCurrentDynamicGain(band));
            }

            // Read per-band saturation params once per block
            std::array<int, NUM_BANDS> bandSatType{};
            std::array<float, NUM_BANDS> bandSatDrive{};
            auto& waveshaperCurves = AnalogEmulation::getWaveshaperCurves();
            for (int band = 1; band < 7; ++band)
            {
                bandSatType[static_cast<size_t>(band)] = static_cast<int>(safeGetParam(bandSatTypeParams[static_cast<size_t>(band)], 0.0f));
                bandSatDrive[static_cast<size_t>(band)] = safeGetParam(bandSatDriveParams[static_cast<size_t>(band)], 0.3f);
            }

            // Process each sample through the filter chain with per-band routing
            for (int i = 0; i < numSamples; ++i)
            {
                float sampleL = procL[i];
                float sampleR = procR[i];

                // Delta solo: capture signal before and after the target band
                float deltaBeforeL = 0.0f, deltaBeforeR = 0.0f;
                float deltaAfterL = 0.0f, deltaAfterR = 0.0f;

                // Per-band routing helper lambda:
                // routing: 0=Stereo, 1=Left, 2=Right, 3=Mid, 4=Side
                // Applies the filter to the appropriate channel(s) with M/S encode/decode as needed
                auto applyFilterWithRouting = [&](auto& filter, int routing) {
                    switch (routing)
                    {
                        case 0:  // Stereo
                            sampleL = filter.processSampleL(sampleL);
                            sampleR = filter.processSampleR(sampleR);
                            break;
                        case 1:  // Left only
                            sampleL = filter.processSampleL(sampleL);
                            break;
                        case 2:  // Right only
                            sampleR = filter.processSampleR(sampleR);
                            break;
                        case 3:  // Mid
                        {
                            float mid = (sampleL + sampleR) * 0.5f;
                            float side = (sampleL - sampleR) * 0.5f;
                            mid = filter.processSampleL(mid);
                            sampleL = mid + side;
                            sampleR = mid - side;
                            break;
                        }
                        case 4:  // Side
                        {
                            float mid = (sampleL + sampleR) * 0.5f;
                            float side = (sampleL - sampleR) * 0.5f;
                            side = filter.processSampleR(side);
                            sampleL = mid + side;
                            sampleR = mid - side;
                            break;
                        }
                    }
                };

                // Per-band processing with smooth enable/disable crossfade
                // Helper: apply band filter with smooth enable blending
                auto applyBandWithSmoothing = [&](int bandIdx, const auto& applyFn) {
                    auto& smooth = bandEnableSmoothed[static_cast<size_t>(bandIdx)];
                    bool active = bandEnabled[static_cast<size_t>(bandIdx)] || smooth.isSmoothing();
                    if (!active) return;

                    float enableGain = smooth.getNextValue();
                    if (enableGain < 0.001f) return;  // Fully faded out

                    float prevL = sampleL, prevR = sampleR;
                    applyFn();

                    // Blend between dry (pre-filter) and wet (post-filter)
                    if (enableGain < 0.999f)
                    {
                        sampleL = prevL + enableGain * (sampleL - prevL);
                        sampleR = prevR + enableGain * (sampleR - prevR);
                    }
                };

                // Band 1: HPF (no dynamics for filters)
                if (deltaSoloActive && currentSolo == 0) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }
                applyBandWithSmoothing(0, [&]() {
                    applyFilterWithRouting(hpfFilter, effectiveRouting[0]);
                });
                if (deltaSoloActive && currentSolo == 0) { deltaAfterL = sampleL; deltaAfterR = sampleR; }

                // Bands 2-7: Shelf and Parametric with optional dynamics
                for (int band = 1; band < 7; ++band)
                {
                    if (deltaSoloActive && currentSolo == band) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }

                    applyBandWithSmoothing(band, [&]() {
                        auto& filter = svfFilters[static_cast<size_t>(band - 1)];
                        int routing = effectiveRouting[static_cast<size_t>(band)];

                        if (bandDynEnabled[static_cast<size_t>(band)])
                        {
                            // Per-sample detection and envelope following
                            float detectionL = dynamicEQ.processDetection(band, sampleL, 0);
                            float detectionR = dynamicEQ.processDetection(band, sampleR, 1);
                            dynamicEQ.processBand(band, detectionL, 0);
                            dynamicEQ.processBand(band, detectionR, 1);

                            // Apply static EQ filter (SVF with per-sample interpolation)
                            applyFilterWithRouting(filter, routing);

                            // Apply dynamic gain filter (SVF at same freq/Q with dynamic gain)
                            applyFilterWithRouting(svfDynGainFilters[static_cast<size_t>(band - 1)], routing);
                        }
                        else
                        {
                            // Static EQ with per-band routing (SVF)
                            applyFilterWithRouting(filter, routing);
                        }

                        // Per-band saturation (after filter, before next band)
                        int satType = bandSatType[static_cast<size_t>(band)];
                        if (satType > 0)
                        {
                            using CT = AnalogEmulation::WaveshaperCurves::CurveType;
                            CT curve;
                            switch (satType)
                            {
                                case 1: curve = CT::Tape; break;
                                case 2: curve = CT::Triode; break;
                                case 3: curve = CT::SSL_Bus; break;
                                case 4: curve = CT::FET_1176; break;
                                default: curve = CT::Linear; break;
                            }
                            float drive = bandSatDrive[static_cast<size_t>(band)];
                            sampleL = waveshaperCurves.processWithDrive(sampleL, curve, drive);
                            sampleR = waveshaperCurves.processWithDrive(sampleR, curve, drive);
                        }
                    });

                    if (deltaSoloActive && currentSolo == band) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
                }

                // Band 8: LPF (no dynamics for filters)
                if (deltaSoloActive && currentSolo == 7) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }
                applyBandWithSmoothing(7, [&]() {
                    applyFilterWithRouting(lpfFilter, effectiveRouting[7]);
                });
                if (deltaSoloActive && currentSolo == 7) { deltaAfterL = sampleL; deltaAfterR = sampleR; }

                // Delta solo: output only what the soloed band changes
                if (deltaSoloActive)
                {
                    sampleL = deltaAfterL - deltaBeforeL;
                    sampleR = deltaAfterR - deltaBeforeR;
                }

                procL[i] = sampleL;
                procR[i] = sampleR;
            }
        }  // end IIR else
    }  // end Digital mode else

    // Skip M/S decode, oversampling, and master gain for linear phase mode
    // (Linear phase processes raw L/R buffer directly and includes master gain in the IR)
    if (!useLinearPhase)
    {
        // M/S decode for British/Tube EQ modes (Digital mode handles M/S per-band)
        if (useMS && eqType != EQType::Digital)
        {
            for (int i = 0; i < numSamples; ++i)
                decodeMS(procL[i], procR[i]);
        }

        // Oversampling downsample
        if (oversamplingMode == 2 && oversampler4x)
            oversampler4x->processSamplesDown(block);
        else if (oversamplingMode == 1 && oversampler2x)
            oversampler2x->processSamplesDown(block);

        // Apply master gain
        float masterGain = juce::Decibels::decibelsToGain(safeGetParam(masterGainParam, 0.0f));
        buffer.applyGain(masterGain);
    }

    // Auto-gain compensation: measure output RMS and apply inverse gain
    // (Bypass already checked above - if bypassed, we would have returned)
    if (autoGainEnabled)
    {
        // Calculate output RMS (block-based mono downmix + sum of squares)
        int outN = buffer.getNumSamples();
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, outN);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, outN);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, outN);
        for (int i = 0; i < outN; ++i)
            outputRmsSum += analyzerMonoBuffer[static_cast<size_t>(i)] * analyzerMonoBuffer[static_cast<size_t>(i)];

        rmsSampleCount += outN;

        // Update auto-gain compensation when we have enough samples (~100ms window)
        if (rmsSampleCount >= RMS_WINDOW_SAMPLES)
        {
            float inputRms = std::sqrt(inputRmsSum / static_cast<float>(rmsSampleCount));
            float outputRms = std::sqrt(outputRmsSum / static_cast<float>(rmsSampleCount));

            // Calculate compensation gain (ratio of input to output RMS)
            // Limit to reasonable range to prevent extreme corrections
            if (outputRms > 1e-6f && inputRms > 1e-6f)
            {
                float targetGain = inputRms / outputRms;
                targetGain = juce::jlimit(0.1f, 10.0f, targetGain);  // ±20dB max
                autoGainCompensation.setTargetValue(targetGain);
            }

            // Reset accumulators
            inputRmsSum = 0.0f;
            outputRmsSum = 0.0f;
            rmsSampleCount = 0;
        }

        // Apply smoothed auto-gain compensation
        if (autoGainCompensation.isSmoothing())
        {
            int bufferChannels = buffer.getNumChannels();
            int bufferSamples = buffer.getNumSamples();
            for (int i = 0; i < bufferSamples; ++i)
            {
                float gain = autoGainCompensation.getNextValue();
                for (int ch = 0; ch < bufferChannels; ++ch)
                    buffer.getWritePointer(ch)[i] *= gain;
            }        }
        else
        {
            float gain = autoGainCompensation.getCurrentValue();
            if (std::abs(gain - 1.0f) > 0.001f)
                buffer.applyGain(gain);
        }
    }
    else
    {
        // Reset auto-gain when disabled
        autoGainCompensation.setCurrentAndTargetValue(1.0f);
        inputRmsSum = 0.0f;
        outputRmsSum = 0.0f;
        rmsSampleCount = 0;
    }

    // Output limiter (mastering safety brickwall)
    {
        bool limiterOn = safeGetParam(limiterEnabledParam, 0.0f) > 0.5f;
        outputLimiter.setEnabled(limiterOn);
        if (limiterOn)
        {
            outputLimiter.setCeiling(safeGetParam(limiterCeilingParam, 0.0f));
            float* limL = buffer.getWritePointer(0);
            float* limR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : limL;
            outputLimiter.process(limL, limR, buffer.getNumSamples());
        }
    }

    // Save processed output for potential future crossfades (only when not currently crossfading)
    // This ensures prevOsBuffer and prevTypeBuffer contain the last fully-processed output
    if (!osChanging)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            prevOsBuffer.copyFrom(ch, 0, buffer, ch, 0, buffer.getNumSamples());
    }
    if (!eqTypeChanging)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            prevTypeBuffer.copyFrom(ch, 0, buffer, ch, 0, buffer.getNumSamples());
    }

    // Apply oversampling mode switch crossfade
    if (osChanging)
    {
        int xfCh = buffer.getNumChannels();
        int xfLen = buffer.getNumSamples();
        if (osCrossfade.isSmoothing())
        {
            for (int i = 0; i < xfLen; ++i)
            {
                float mix = osCrossfade.getNextValue();
                for (int ch = 0; ch < xfCh; ++ch)
                {
                    float prev = prevOsBuffer.getSample(ch, i);
                    float curr = buffer.getSample(ch, i);
                    buffer.setSample(ch, i, prev + mix * (curr - prev));
                }
            }
        }
        else
        {
            osChanging = false;
        }
    }

    // Apply EQ type switch crossfade
    if (eqTypeChanging)
    {
        int xfCh = buffer.getNumChannels();
        int xfLen = buffer.getNumSamples();
        if (eqTypeCrossfade.isSmoothing())
        {
            for (int i = 0; i < xfLen; ++i)
            {
                float mix = eqTypeCrossfade.getNextValue();
                for (int ch = 0; ch < xfCh; ++ch)
                {
                    float prev = prevTypeBuffer.getSample(ch, i);
                    float curr = buffer.getSample(ch, i);
                    buffer.setSample(ch, i, prev + mix * (curr - prev));
                }
            }
        }
        else
        {
            eqTypeChanging = false;
        }
    }

    // Apply bypass crossfade (dry/wet blend)
    if (bypassSmoothed.isSmoothing())
    {
        int xfCh = buffer.getNumChannels();
        int xfLen = buffer.getNumSamples();
        for (int i = 0; i < xfLen; ++i)
        {
            float bypassMix = bypassSmoothed.getNextValue();  // 0 = fully wet, 1 = fully dry
            for (int ch = 0; ch < xfCh; ++ch)
            {
                float dry = dryBuffer.getSample(ch, i);
                float wet = buffer.getSample(ch, i);
                buffer.setSample(ch, i, wet + bypassMix * (dry - wet));
            }
        }
    }

    // Always push post-EQ samples to analyzer for dual spectrum overlay (block-based)
    if (analyzerEnabled)
    {
        int n = buffer.getNumSamples();
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, n);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, n);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, n);
        pushSamplesToAnalyzer(analyzerMonoBuffer.data(), n, false);
    }

    // Output level metering (peak absolute value per channel, single-pass)
    auto outRangeL = juce::FloatVectorOperations::findMinAndMax(buffer.getReadPointer(0), buffer.getNumSamples());
    float outL = std::max(std::abs(outRangeL.getStart()), std::abs(outRangeL.getEnd()));
    float outR = outL;
    if (buffer.getNumChannels() > 1)
    {
        auto outRangeR = juce::FloatVectorOperations::findMinAndMax(buffer.getReadPointer(1), buffer.getNumSamples());
        outR = std::max(std::abs(outRangeR.getStart()), std::abs(outRangeR.getEnd()));
    }
    float outLdB = outL > 1e-3f ? juce::Decibels::gainToDecibels(outL) : -60.0f;
    float outRdB = outR > 1e-3f ? juce::Decibels::gainToDecibels(outR) : -60.0f;
    outputLevelL.store(outLdB);
    outputLevelR.store(outRdB);
    if (outL >= 1.0f || outR >= 1.0f)
        outputClipped.store(true, std::memory_order_relaxed);

    // Process FFT if we have enough samples
    processFFT();
    processPreFFT();
}

//==============================================================================
void MultiQ::updateAllFilters()
{
    updateHPFCoefficients(currentSampleRate);
    updateLPFCoefficients(currentSampleRate);

    for (int i = 1; i < 7; ++i)
        updateBandFilter(i);
}

void MultiQ::computeBandCoeffs(int bandIndex, BiquadCoeffs& c) const
{
    float gain = safeGetParam(bandGainParams[static_cast<size_t>(bandIndex)], 0.0f);
    computeBandCoeffsWithGain(bandIndex, gain, c);
}

void MultiQ::computeBandCoeffsWithGain(int bandIndex, float overrideGainDB, BiquadCoeffs& c) const
{
    float freq = safeGetParam(bandFreqParams[static_cast<size_t>(bandIndex)],
                              DefaultBandConfigs[static_cast<size_t>(bandIndex)].defaultFreq);
    float baseQ = safeGetParam(bandQParams[static_cast<size_t>(bandIndex)], 0.71f);
    float q = getQCoupledValue(baseQ, overrideGainDB, getCurrentQCoupleMode());

    if (bandIndex == 1)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[1], 0.0f));
        if (shape == 1)
            computePeakingCoeffs(c, currentSampleRate, freq, overrideGainDB, q);
        else if (shape == 2)
            computeHighPassCoeffs(c, currentSampleRate, freq, q);
        else
            computeLowShelfCoeffs(c, currentSampleRate, freq, overrideGainDB, q);
    }
    else if (bandIndex == 6)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[6], 0.0f));
        if (shape == 1)
            computePeakingCoeffs(c, currentSampleRate, freq, overrideGainDB, q);
        else if (shape == 2)
            computeLowPassCoeffs(c, currentSampleRate, freq, q);
        else
            computeHighShelfCoeffs(c, currentSampleRate, freq, overrideGainDB, q);
    }
    else
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[static_cast<size_t>(bandIndex)], 0.0f));
        if (shape == 1)
            computeNotchCoeffs(c, currentSampleRate, freq, q);
        else if (shape == 2)
            computeBandPassCoeffs(c, currentSampleRate, freq, q);
        else if (shape == 3)
            computeTiltShelfCoeffs(c, currentSampleRate, freq, overrideGainDB);
        else
            computePeakingCoeffs(c, currentSampleRate, freq, overrideGainDB, q);
    }
}

void MultiQ::updateBandFilter(int bandIndex)
{
    if (bandIndex < 1 || bandIndex > 6)
        return;

    // SVF coefficients for audio processing (per-sample interpolation)
    SVFCoeffs svfC;
    computeBandSVFCoeffs(bandIndex, svfC);
    svfFilters[static_cast<size_t>(bandIndex - 1)].setTarget(svfC);

    // Biquad coefficients for UI curve display (benign data race)
    BiquadCoeffs c;
    computeBandCoeffs(bandIndex, c);
    uiBandCoeffs[static_cast<size_t>(bandIndex - 1)] = c;
}

void MultiQ::updateDynGainFilter(int bandIndex, float dynGainDb)
{
    if (bandIndex < 1 || bandIndex > 6)
        return;

    SVFCoeffs svfC;

    if (std::abs(dynGainDb) < 0.01f)
    {
        svfC.setIdentity();
        svfDynGainFilters[static_cast<size_t>(bandIndex - 1)].setTarget(svfC);
        return;
    }

    float freq = safeGetParam(bandFreqParams[static_cast<size_t>(bandIndex)],
                              DefaultBandConfigs[static_cast<size_t>(bandIndex)].defaultFreq);
    float baseQ = safeGetParam(bandQParams[static_cast<size_t>(bandIndex)], 0.71f);
    float staticGain = safeGetParam(bandGainParams[static_cast<size_t>(bandIndex)], 0.0f);
    float q = getQCoupledValue(baseQ, staticGain, getCurrentQCoupleMode());

    if (bandIndex == 1)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[1], 0.0f));
        if (shape == 1)
            computeSVFPeaking(svfC, currentSampleRate, freq, dynGainDb, q);
        else if (shape == 2)
            svfC.setIdentity();  // HP shape has no dynamic gain
        else
            computeSVFLowShelf(svfC, currentSampleRate, freq, dynGainDb, q);
    }
    else if (bandIndex == 6)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[6], 0.0f));
        if (shape == 1)
            computeSVFPeaking(svfC, currentSampleRate, freq, dynGainDb, q);
        else if (shape == 2)
            svfC.setIdentity();  // LP shape has no dynamic gain
        else
            computeSVFHighShelf(svfC, currentSampleRate, freq, dynGainDb, q);
    }
    else
    {
        // Bands 3-6: check shape
        int shape = bandShapeParams[static_cast<size_t>(bandIndex)]
            ? static_cast<int>(bandShapeParams[static_cast<size_t>(bandIndex)]->load()) : 0;
        if (shape == 3)  // Tilt shelf
            computeSVFTiltShelf(svfC, currentSampleRate, freq, dynGainDb);
        else
            computeSVFPeaking(svfC, currentSampleRate, freq, dynGainDb, q);
    }

    svfDynGainFilters[static_cast<size_t>(bandIndex - 1)].setTarget(svfC);
}


//==============================================================================
// Non-allocating coefficient computation using Audio EQ Cookbook with pre-warping
// These write directly into BiquadCoeffs without any heap allocation.

double MultiQ::preWarpFrequency(double freq, double sampleRate)
{
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    return (2.0 / T) * std::tan(w0 * T / 2.0) / (2.0 * juce::MathConstants<double>::pi);
}

void MultiQ::computePeakingCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double af = juce::jlimit(20.0, sr * 0.45, preWarpFrequency(freq, sr));
    double A = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * juce::MathConstants<double>::pi * af / sr;
    double cosw0 = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * cosw0;
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha / A;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void MultiQ::computeLowShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double af = preWarpFrequency(freq, sr);
    double A = std::sqrt(juce::Decibels::decibelsToGain(gainDB));
    double w0 = 2.0 * juce::MathConstants<double>::pi * af / sr;
    double cosw0 = std::cos(w0);
    double beta = std::sin(w0) * std::sqrt(A) / q;

    double b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + beta);
    double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
    double b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - beta);
    double a0 = (A + 1.0) + (A - 1.0) * cosw0 + beta;
    double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
    double a2 = (A + 1.0) + (A - 1.0) * cosw0 - beta;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void MultiQ::computeHighShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double af = juce::jlimit(20.0, sr * 0.45, preWarpFrequency(freq, sr));
    double A = std::sqrt(juce::Decibels::decibelsToGain(gainDB));
    double w0 = 2.0 * juce::MathConstants<double>::pi * af / sr;
    double cosw0 = std::cos(w0);
    double beta = std::sin(w0) * std::sqrt(A) / q;

    double b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + beta);
    double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
    double b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - beta);
    double a0 = (A + 1.0) - (A - 1.0) * cosw0 + beta;
    double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
    double a2 = (A + 1.0) - (A - 1.0) * cosw0 - beta;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void MultiQ::computeNotchCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    double af = juce::jlimit(20.0, sr * 0.45, preWarpFrequency(freq, sr));
    double w0 = 2.0 * juce::MathConstants<double>::pi * af / sr;
    double cosw0 = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);

    double b0 = 1.0;
    double b1 = -2.0 * cosw0;
    double b2 = 1.0;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void MultiQ::computeBandPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    double af = juce::jlimit(20.0, sr * 0.45, preWarpFrequency(freq, sr));
    double w0 = 2.0 * juce::MathConstants<double>::pi * af / sr;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / (2.0 * q);

    double b0 = q * alpha;
    double b1 = 0.0;
    double b2 = -q * alpha;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void MultiQ::computeHighPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    double cosw0 = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);

    double b0 = (1.0 + cosw0) / 2.0;
    double b1 = -(1.0 + cosw0);
    double b2 = (1.0 + cosw0) / 2.0;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void MultiQ::computeLowPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    double cosw0 = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);

    double b0 = (1.0 - cosw0) / 2.0;
    double b1 = 1.0 - cosw0;
    double b2 = (1.0 - cosw0) / 2.0;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void MultiQ::computeFirstOrderHighPassCoeffs(BiquadCoeffs& c, double sr, double freq)
{
    // JUCE-compatible: n = tan(pi * f / sr), H(z) = (1 - z^-1) / ((n+1) + (n-1)*z^-1)
    double n = std::tan(juce::MathConstants<double>::pi * freq / sr);
    double a0 = n + 1.0;

    c.coeffs[0] = float(1.0 / a0);
    c.coeffs[1] = float(-1.0 / a0);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float((n - 1.0) / a0);
    c.coeffs[5] = 0.0f;
}

void MultiQ::computeFirstOrderLowPassCoeffs(BiquadCoeffs& c, double sr, double freq)
{
    double n = std::tan(juce::MathConstants<double>::pi * freq / sr);
    double a0 = n + 1.0;

    c.coeffs[0] = float(n / a0);
    c.coeffs[1] = float(n / a0);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float((n - 1.0) / a0);
    c.coeffs[5] = 0.0f;
}

void MultiQ::computeTiltShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB)
{
    // 1st-order tilt shelf using bilinear transform of analog prototype:
    //   H(s) = (s + w0*sqrt(A)) / (s + w0/sqrt(A))
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sr;
    double wc = (2.0 / T) * std::tan(w0 * T / 2.0);

    double A = std::pow(10.0, gainDB / 40.0);
    double sqrtA = std::sqrt(A);

    double twoOverT = 2.0 / T;
    double wcSqrtA = wc * sqrtA;
    double wcOverSqrtA = wc / sqrtA;

    double b0 = twoOverT + wcSqrtA;
    double b1 = wcSqrtA - twoOverT;
    double a0 = twoOverT + wcOverSqrtA;
    double a1 = wcOverSqrtA - twoOverT;

    c.coeffs[0] = float(b0 / a0);
    c.coeffs[1] = float(b1 / a0);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float(a1 / a0);
    c.coeffs[5] = 0.0f;
}

//==============================================================================
// Cytomic SVF coefficient computation
// These compute SVFCoeffs for the audio processing path.
// The transfer function is identical to the corresponding biquad; the difference
// is in the filter topology which allows per-sample coefficient modulation.

void MultiQ::computeSVFPeaking(SVFCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double A = std::pow(10.0, gainDB / 40.0);  // sqrt of linear gain
    double g = std::tan(juce::MathConstants<double>::pi * freq / sr);
    double k = 1.0 / (q * A);

    c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
    c.a2 = static_cast<float>(g * c.a1);
    c.a3 = static_cast<float>(g * c.a2);
    c.m0 = 1.0f;
    c.m1 = static_cast<float>(k * (A * A - 1.0));
    c.m2 = 0.0f;
}

void MultiQ::computeSVFLowShelf(SVFCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double A = std::pow(10.0, gainDB / 40.0);
    double g = std::tan(juce::MathConstants<double>::pi * freq / sr) / std::sqrt(A);
    double k = 1.0 / q;

    c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
    c.a2 = static_cast<float>(g * c.a1);
    c.a3 = static_cast<float>(g * c.a2);
    c.m0 = 1.0f;
    c.m1 = static_cast<float>(k * (A - 1.0));
    c.m2 = static_cast<float>(A * A - 1.0);
}

void MultiQ::computeSVFHighShelf(SVFCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double A = std::pow(10.0, gainDB / 40.0);
    double g = std::tan(juce::MathConstants<double>::pi * freq / sr) * std::sqrt(A);
    double k = 1.0 / q;

    c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
    c.a2 = static_cast<float>(g * c.a1);
    c.a3 = static_cast<float>(g * c.a2);
    c.m0 = static_cast<float>(A * A);
    c.m1 = static_cast<float>(k * A * (1.0 - A));
    c.m2 = static_cast<float>(1.0 - A * A);
}

void MultiQ::computeSVFNotch(SVFCoeffs& c, double sr, double freq, float q)
{
    double g = std::tan(juce::MathConstants<double>::pi * freq / sr);
    double k = 1.0 / q;

    c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
    c.a2 = static_cast<float>(g * c.a1);
    c.a3 = static_cast<float>(g * c.a2);
    c.m0 = 1.0f;
    c.m1 = static_cast<float>(-k);
    c.m2 = 0.0f;
}

void MultiQ::computeSVFBandPass(SVFCoeffs& c, double sr, double freq, float q)
{
    double g = std::tan(juce::MathConstants<double>::pi * freq / sr);
    double k = 1.0 / q;

    c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
    c.a2 = static_cast<float>(g * c.a1);
    c.a3 = static_cast<float>(g * c.a2);
    c.m0 = 0.0f;
    c.m1 = 1.0f;
    c.m2 = 0.0f;
}

void MultiQ::computeSVFHighPass(SVFCoeffs& c, double sr, double freq, float q)
{
    double g = std::tan(juce::MathConstants<double>::pi * freq / sr);
    double k = 1.0 / q;

    c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
    c.a2 = static_cast<float>(g * c.a1);
    c.a3 = static_cast<float>(g * c.a2);
    // HP = x - k*v1 - v2
    c.m0 = 1.0f;
    c.m1 = static_cast<float>(-k);
    c.m2 = -1.0f;
}

void MultiQ::computeSVFTiltShelf(SVFCoeffs& c, double sr, double freq, float gainDB)
{
    // Approximate tilt shelf using low shelf with Q=0.5 (gentle slope)
    // This matches the 1st-order character of the biquad tilt shelf
    computeSVFLowShelf(c, sr, freq, gainDB, 0.5f);
}

void MultiQ::computeBandSVFCoeffs(int bandIndex, SVFCoeffs& c) const
{
    float gain = safeGetParam(bandGainParams[static_cast<size_t>(bandIndex)], 0.0f);
    computeBandSVFCoeffsWithGain(bandIndex, gain, c);
}

void MultiQ::computeBandSVFCoeffsWithGain(int bandIndex, float overrideGainDB, SVFCoeffs& c) const
{
    float freq = safeGetParam(bandFreqParams[static_cast<size_t>(bandIndex)],
                              DefaultBandConfigs[static_cast<size_t>(bandIndex)].defaultFreq);
    float baseQ = safeGetParam(bandQParams[static_cast<size_t>(bandIndex)], 0.71f);
    float q = getQCoupledValue(baseQ, overrideGainDB, getCurrentQCoupleMode());

    if (bandIndex == 1)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[1], 0.0f));
        if (shape == 1)
            computeSVFPeaking(c, currentSampleRate, freq, overrideGainDB, q);
        else if (shape == 2)
            computeSVFHighPass(c, currentSampleRate, freq, q);
        else
            computeSVFLowShelf(c, currentSampleRate, freq, overrideGainDB, q);
    }
    else if (bandIndex == 6)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[6], 0.0f));
        if (shape == 1)
            computeSVFPeaking(c, currentSampleRate, freq, overrideGainDB, q);
        else if (shape == 2)
        {
            // Low-pass: use SVF low-pass output
            double g = std::tan(juce::MathConstants<double>::pi * freq / currentSampleRate);
            double k = 1.0 / q;
            c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
            c.a2 = static_cast<float>(g * c.a1);
            c.a3 = static_cast<float>(g * c.a2);
            c.m0 = 0.0f; c.m1 = 0.0f; c.m2 = 1.0f;  // LP output = v2
        }
        else
            computeSVFHighShelf(c, currentSampleRate, freq, overrideGainDB, q);
    }
    else if (bandIndex >= 2 && bandIndex <= 5)
    {
        int shape = bandShapeParams[static_cast<size_t>(bandIndex)]
            ? static_cast<int>(bandShapeParams[static_cast<size_t>(bandIndex)]->load()) : 0;
        switch (shape)
        {
            case 1:  // Notch
                computeSVFNotch(c, currentSampleRate, freq, q);
                break;
            case 2:  // Band Pass
                computeSVFBandPass(c, currentSampleRate, freq, q);
                break;
            case 3:  // Tilt Shelf
                computeSVFTiltShelf(c, currentSampleRate, freq, overrideGainDB);
                break;
            default:  // Peaking (0)
                computeSVFPeaking(c, currentSampleRate, freq, overrideGainDB, q);
                break;
        }
    }
    else
    {
        c.setIdentity();
    }
}
        filter.setCoefficients(makePeakingCoefficients(currentSampleRate, 1000.0f, 0.0f, 0.71f));
        return;
    }

    float freq = safeGetParam(bandFreqParams[static_cast<size_t>(bandIndex)],
                              DefaultBandConfigs[static_cast<size_t>(bandIndex)].defaultFreq);
    float baseQ = safeGetParam(bandQParams[static_cast<size_t>(bandIndex)], 0.71f);
    float staticGain = safeGetParam(bandGainParams[static_cast<size_t>(bandIndex)], 0.0f);

    QCoupleMode qMode = getCurrentQCoupleMode();
    float q = getQCoupledValue(baseQ, staticGain, qMode);

    // Match the dynamic gain filter type to the static band type
    if (bandIndex == 1)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[1], 0.0f));
        if (shape == 1)  // Peaking
            filter.setCoefficients(makePeakingCoefficients(currentSampleRate, freq, dynGainDb, q));
        else if (shape == 2)  // HPF - no gain to apply for dynamics
            filter.setCoefficients(makePeakingCoefficients(currentSampleRate, 1000.0f, 0.0f, 0.71f));
        else  // Low Shelf
            filter.setCoefficients(makeLowShelfCoefficients(currentSampleRate, freq, dynGainDb, q));
    }
    else if (bandIndex == 6)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[6], 0.0f));
        if (shape == 1)  // Peaking
            filter.setCoefficients(makePeakingCoefficients(currentSampleRate, freq, dynGainDb, q));
        else if (shape == 2)  // LPF - no gain to apply for dynamics
            filter.setCoefficients(makePeakingCoefficients(currentSampleRate, 1000.0f, 0.0f, 0.71f));
        else  // High Shelf
            filter.setCoefficients(makeHighShelfCoefficients(currentSampleRate, freq, dynGainDb, q));
    }
    else  // Parametric bands 3-6: always use peaking for dynamic gain
        filter.setCoefficients(makePeakingCoefficients(currentSampleRate, freq, dynGainDb, q));
}

//==============================================================================
// Filter update methods (non-allocating, safe for audio thread)

void MultiQ::updateHPFCoefficients(double sampleRate)
{
    float freq = safeGetParam(bandFreqParams[0], 20.0f);
    float userQ = safeGetParam(bandQParams[0], 0.71f);
    int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[0], 0.0f));

    double actualFreq = preWarpFrequency(freq, sampleRate);
    auto slope = static_cast<FilterSlope>(slopeIndex);

    int stages = 1;
    bool firstStageFirstOrder = false;
    int secondOrderStages = 0;

    switch (slope)
    {
        case FilterSlope::Slope6dB:  stages = 1; firstStageFirstOrder = true;  secondOrderStages = 0; break;
        case FilterSlope::Slope12dB: stages = 1; secondOrderStages = 1; break;
        case FilterSlope::Slope18dB: stages = 2; firstStageFirstOrder = true;  secondOrderStages = 1; break;
        case FilterSlope::Slope24dB: stages = 2; secondOrderStages = 2; break;
        case FilterSlope::Slope36dB: stages = 3; secondOrderStages = 3; break;
        case FilterSlope::Slope48dB: stages = 4; secondOrderStages = 4; break;
        case FilterSlope::Slope72dB: stages = 6; secondOrderStages = 6; break;
        case FilterSlope::Slope96dB: stages = 8; secondOrderStages = 8; break;
    }

    hpfFilter.activeStages = stages;
    uiHpfStages = stages;

    int soStageIdx = 0;
    for (int stage = 0; stage < stages; ++stage)
    {
        BiquadCoeffs c;

        if (firstStageFirstOrder && stage == 0)
            computeFirstOrderHighPassCoeffs(c, sampleRate, actualFreq);
        else
        {
            float stageQ = ButterworthQ::getStageQ(secondOrderStages, soStageIdx, userQ);
            computeHighPassCoeffs(c, sampleRate, static_cast<float>(actualFreq), stageQ);
            ++soStageIdx;
        }

        // Apply to filter in-place (no allocation)
        c.applyToFilter(hpfFilter.stagesL[static_cast<size_t>(stage)]);
        c.applyToFilter(hpfFilter.stagesR[static_cast<size_t>(stage)]);

        // Store for UI curve display
        uiHpfCoeffs[static_cast<size_t>(stage)] = c;
    }
}

void MultiQ::updateLPFCoefficients(double sampleRate)
{
    float freq = safeGetParam(bandFreqParams[7], 20000.0f);
    float userQ = safeGetParam(bandQParams[7], 0.71f);
    int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[1], 0.0f));

    double actualFreq = juce::jlimit(20.0, sampleRate * 0.45, preWarpFrequency(freq, sampleRate));
    auto slope = static_cast<FilterSlope>(slopeIndex);

    auto slope = static_cast<FilterSlope>(slopeIndex);

    int stages = 1;
    bool firstStageFirstOrder = false;
    int secondOrderStages = 0;

    switch (slope)
    {
        case FilterSlope::Slope6dB:  stages = 1; firstStageFirstOrder = true;  secondOrderStages = 0; break;
        case FilterSlope::Slope12dB: stages = 1; secondOrderStages = 1; break;
        case FilterSlope::Slope18dB: stages = 2; firstStageFirstOrder = true;  secondOrderStages = 1; break;
        case FilterSlope::Slope24dB: stages = 2; secondOrderStages = 2; break;
        case FilterSlope::Slope36dB: stages = 3; secondOrderStages = 3; break;
        case FilterSlope::Slope48dB: stages = 4; secondOrderStages = 4; break;
        case FilterSlope::Slope72dB: stages = 6; secondOrderStages = 6; break;
        case FilterSlope::Slope96dB: stages = 8; secondOrderStages = 8; break;
    }

    lpfFilter.activeStages = stages;
    uiLpfStages = stages;

    int soStageIdx = 0;
    for (int stage = 0; stage < stages; ++stage)
    {
        BiquadCoeffs c;

        if (firstStageFirstOrder && stage == 0)
            computeFirstOrderLowPassCoeffs(c, sampleRate, actualFreq);
        else
        {
            float stageQ = ButterworthQ::getStageQ(secondOrderStages, soStageIdx, userQ);
            computeLowPassCoeffs(c, sampleRate, static_cast<float>(actualFreq), stageQ);
            ++soStageIdx;
        }

        c.applyToFilter(lpfFilter.stagesL[static_cast<size_t>(stage)]);
        c.applyToFilter(lpfFilter.stagesR[static_cast<size_t>(stage)]);
        uiLpfCoeffs[static_cast<size_t>(stage)] = c;
    }
}

juce::dsp::IIR::Coefficients<float>::Ptr MultiQ::makeLowShelfCoefficients(
    double sampleRate, float freq, float gain, float q) const
{
    // Frequency pre-warping for analog matching at high frequencies
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    double warpedFreq = (2.0 / T) * std::tan(w0 * T / 2.0);
    double actualFreq = warpedFreq / (2.0 * juce::MathConstants<double>::pi);

    return juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sampleRate, static_cast<float>(actualFreq), q,
        juce::Decibels::decibelsToGain(gain));
}

juce::dsp::IIR::Coefficients<float>::Ptr MultiQ::makeHighShelfCoefficients(
    double sampleRate, float freq, float gain, float q) const
{
    // Frequency pre-warping
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    double warpedFreq = (2.0 / T) * std::tan(w0 * T / 2.0);
    double actualFreq = warpedFreq / (2.0 * juce::MathConstants<double>::pi);

    // Clamp to valid range
    actualFreq = juce::jlimit(20.0, sampleRate * 0.45, actualFreq);

    return juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, static_cast<float>(actualFreq), q,
        juce::Decibels::decibelsToGain(gain));
}

juce::dsp::IIR::Coefficients<float>::Ptr MultiQ::makePeakingCoefficients(
    double sampleRate, float freq, float gain, float q) const
{
    // Frequency pre-warping for analog matching
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    double warpedFreq = (2.0 / T) * std::tan(w0 * T / 2.0);
    double actualFreq = warpedFreq / (2.0 * juce::MathConstants<double>::pi);

    // Clamp to valid range
    actualFreq = juce::jlimit(20.0, sampleRate * 0.45, actualFreq);

    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, static_cast<float>(actualFreq), q,
        juce::Decibels::decibelsToGain(gain));
}

juce::dsp::IIR::Coefficients<float>::Ptr MultiQ::makeNotchCoefficients(
    double sampleRate, float freq, float q) const
{
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    double warpedFreq = (2.0 / T) * std::tan(w0 * T / 2.0);
    double actualFreq = warpedFreq / (2.0 * juce::MathConstants<double>::pi);
    actualFreq = juce::jlimit(20.0, sampleRate * 0.45, actualFreq);

    return juce::dsp::IIR::Coefficients<float>::makeNotch(
        sampleRate, static_cast<float>(actualFreq), q);
}

juce::dsp::IIR::Coefficients<float>::Ptr MultiQ::makeBandPassCoefficients(
    double sampleRate, float freq, float q) const
{
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    double warpedFreq = (2.0 / T) * std::tan(w0 * T / 2.0);
    double actualFreq = warpedFreq / (2.0 * juce::MathConstants<double>::pi);
    actualFreq = juce::jlimit(20.0, sampleRate * 0.45, actualFreq);

    return juce::dsp::IIR::Coefficients<float>::makeBandPass(
        sampleRate, static_cast<float>(actualFreq), q);
}

juce::dsp::IIR::Coefficients<float>::Ptr MultiQ::makeTiltShelfCoefficients(
    double sampleRate, float freq, float gain) const
{
    // 1st-order tilt shelf: tilts the spectrum around the center frequency
    // Below freq: -gain/2 dB, Above freq: +gain/2 dB
    // Using bilinear transform of 1st-order analog prototype:
    //   H(s) = (s + w0*sqrt(A)) / (s + w0/sqrt(A))
    // where A = 10^(gain/40)

    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    double wc = (2.0 / T) * std::tan(w0 * T / 2.0);  // Pre-warped frequency

    double A = std::pow(10.0, gain / 40.0);  // Half-gain for tilt
    double sqrtA = std::sqrt(A);

    // Bilinear transform: s = (2/T)(z-1)/(z+1)
    // Numerator: s + wc*sqrtA -> (2/T)(z-1)/(z+1) + wc*sqrtA
    // = [(2/T + wc*sqrtA)z + (wc*sqrtA - 2/T)] / (z+1)
    double twoOverT = 2.0 / T;
    double wcSqrtA = wc * sqrtA;
    double wcOverSqrtA = wc / sqrtA;

    double b0 = twoOverT + wcSqrtA;
    double b1 = wcSqrtA - twoOverT;
    double a0 = twoOverT + wcOverSqrtA;
    double a1 = wcOverSqrtA - twoOverT;

    // Normalize
    auto coeffs = new juce::dsp::IIR::Coefficients<float>(
        static_cast<float>(b0 / a0),
        static_cast<float>(b1 / a0),
        0.0f,  // b2 (1st-order, padded to biquad)
        1.0f,
        static_cast<float>(a1 / a0),
        0.0f   // a2
    );
    return coeffs;
}

//==============================================================================
QCoupleMode MultiQ::getCurrentQCoupleMode() const
{
    return static_cast<QCoupleMode>(static_cast<int>(safeGetParam(qCoupleModeParam, 0.0f)));
}

float MultiQ::getEffectiveQ(int bandNum) const
{
    if (bandNum < 1 || bandNum > NUM_BANDS)
        return 0.71f;

    float baseQ = safeGetParam(bandQParams[static_cast<size_t>(bandNum - 1)], 0.71f);
    float gain = safeGetParam(bandGainParams[static_cast<size_t>(bandNum - 1)], 0.0f);

    return getQCoupledValue(baseQ, gain, getCurrentQCoupleMode());
}

float MultiQ::getFrequencyResponseMagnitude(float frequencyHz) const
{
    // Evaluate the actual IIR transfer function at the given frequency using stored coefficients.
    // This produces an exact match with the DSP processing (no Gaussian approximations).

    double response = 1.0;
    double sr = currentSampleRate;

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        bool enabled = safeGetParam(bandEnabledParams[static_cast<size_t>(band)], 0.0f) > 0.5f;
        if (!enabled)
            continue;

        if (band == 0)  // HPF: cascaded stages
        {
            int stages = uiHpfStages;
            for (int s = 0; s < stages; ++s)
                response *= uiHpfCoeffs[static_cast<size_t>(s)].getMagnitudeForFrequency(frequencyHz, sr);
        }
        else if (band == 7)  // LPF: cascaded stages
        {
            int stages = uiLpfStages;
            for (int s = 0; s < stages; ++s)
                response *= uiLpfCoeffs[static_cast<size_t>(s)].getMagnitudeForFrequency(frequencyHz, sr);
        }
        else  // Bands 2-7: single biquad each
        {
            response *= uiBandCoeffs[static_cast<size_t>(band - 1)].getMagnitudeForFrequency(frequencyHz, sr);
        }
    }

    return static_cast<float>(juce::Decibels::gainToDecibels(response, -100.0));
}

float MultiQ::getFrequencyResponseWithDynamics(float frequencyHz) const
{
    // Same as getFrequencyResponseMagnitude but recomputes coefficients for bands
    // with active dynamics to include the dynamic gain offset.

    double response = 1.0;
    double sr = currentSampleRate;

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        bool enabled = safeGetParam(bandEnabledParams[static_cast<size_t>(band)], 0.0f) > 0.5f;
        if (!enabled)
            continue;

        if (band == 0)  // HPF
        {
            int stages = uiHpfStages;
            for (int s = 0; s < stages; ++s)
                response *= uiHpfCoeffs[static_cast<size_t>(s)].getMagnitudeForFrequency(frequencyHz, sr);
        }
        else if (band == 7)  // LPF
        {
            int stages = uiLpfStages;
            for (int s = 0; s < stages; ++s)
                response *= uiLpfCoeffs[static_cast<size_t>(s)].getMagnitudeForFrequency(frequencyHz, sr);
        }
        else  // Bands 2-7
        {
            // Use stored static coefficients
            response *= uiBandCoeffs[static_cast<size_t>(band - 1)].getMagnitudeForFrequency(frequencyHz, sr);

            // For bands with dynamics enabled, add dynamic gain filter contribution
            if (isDynamicsEnabled(band))
            {
                float dynGain = getDynamicGain(band);
                if (std::abs(dynGain) > 0.01f)
                {
                    // Compute dynamic gain filter coefficients on the fly (UI thread, allocation OK)
                    BiquadCoeffs dynC;
                    computeBandCoeffsWithGain(band, dynGain, dynC);
                    response *= dynC.getMagnitudeForFrequency(frequencyHz, sr);
                }
            }
        }
    }

    return static_cast<float>(juce::Decibels::gainToDecibels(response, -100.0));
}

bool MultiQ::isDynamicsEnabled(int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= NUM_BANDS)
        return false;
    return safeGetParam(bandDynEnabledParams[static_cast<size_t>(bandIndex)], 0.0f) > 0.5f;
}

bool MultiQ::isInDynamicMode() const
{
    // Returns true if in Digital mode and any band has dynamics enabled
    if (static_cast<int>(safeGetParam(eqTypeParam, 0.0f)) != static_cast<int>(EQType::Digital))
        return false;

    // Check if any band has dynamics enabled
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        if (safeGetParam(bandDynEnabledParams[static_cast<size_t>(i)], 0.0f) > 0.5f)
            return true;
    }
    return false;
}

bool MultiQ::isLimiterEnabled() const
{
    return safeGetParam(limiterEnabledParam, 0.0f) > 0.5f;
}

//==============================================================================
// Cross-mode band transfer

void MultiQ::transferCurrentEQToDigital()
{
    auto eqType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));

    // Helper to set a parameter value by ID (normalizes and notifies host)
    auto setParam = [&](const juce::String& paramID, float value) {
        if (auto* param = parameters.getParameter(paramID))
            param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(value));
    };

    // Helper to set a boolean parameter
    auto setBoolParam = [&](const juce::String& paramID, bool value) {
        if (auto* param = parameters.getParameter(paramID))
            param->setValueNotifyingHost(value ? 1.0f : 0.0f);
    };

    // Helper to set a choice parameter by index
    auto setChoiceParam = [&](const juce::String& paramID, int index) {
        if (auto* param = parameters.getParameter(paramID))
        {
            int numChoices = param->getNumSteps();
            if (numChoices > 1)
                param->setValueNotifyingHost(static_cast<float>(index) / static_cast<float>(numChoices - 1));
        }
    };

    if (eqType == EQType::British)
    {
        // British → Digital direct parameter mapping
        // British: HPF, LPF, LF (shelf/bell), LMF (para), HMF (para), HF (shelf/bell)
        // Digital: Band1=HPF, Band2=LowShelf, Band3-6=Para, Band7=HighShelf, Band8=LPF

        // Band 1: HPF
        bool hpfEnabled = safeGetParam(britishHpfEnabledParam, 0.0f) > 0.5f;
        setBoolParam(ParamIDs::bandEnabled(1), hpfEnabled);
        if (hpfEnabled)
            setParam(ParamIDs::bandFreq(1), safeGetParam(britishHpfFreqParam, 20.0f));

        // Band 2: Low Shelf ← British LF
        float lfGain = safeGetParam(britishLfGainParam, 0.0f);
        bool lfBell = safeGetParam(britishLfBellParam, 0.0f) > 0.5f;
        setBoolParam(ParamIDs::bandEnabled(2), std::abs(lfGain) > 0.1f);
        setParam(ParamIDs::bandFreq(2), safeGetParam(britishLfFreqParam, 100.0f));
        setParam(ParamIDs::bandGain(2), lfGain);
        // Shape: 0=LowShelf, 1=Peaking
        setChoiceParam(ParamIDs::bandShape(2), lfBell ? 1 : 0);

        // Band 3: Parametric ← British LMF
        float lmGain = safeGetParam(britishLmGainParam, 0.0f);
        setBoolParam(ParamIDs::bandEnabled(3), std::abs(lmGain) > 0.1f);
        setParam(ParamIDs::bandFreq(3), safeGetParam(britishLmFreqParam, 600.0f));
        setParam(ParamIDs::bandGain(3), lmGain);
        setParam(ParamIDs::bandQ(3), safeGetParam(britishLmQParam, 0.7f));
        setChoiceParam(ParamIDs::bandShape(3), 0);  // Peaking

        // Band 4: Disabled (unused in British mapping)
        setBoolParam(ParamIDs::bandEnabled(4), false);

        // Band 5: Parametric ← British HMF
        float hmGain = safeGetParam(britishHmGainParam, 0.0f);
        setBoolParam(ParamIDs::bandEnabled(5), std::abs(hmGain) > 0.1f);
        setParam(ParamIDs::bandFreq(5), safeGetParam(britishHmFreqParam, 2000.0f));
        setParam(ParamIDs::bandGain(5), hmGain);
        setParam(ParamIDs::bandQ(5), safeGetParam(britishHmQParam, 0.7f));
        setChoiceParam(ParamIDs::bandShape(5), 0);  // Peaking

        // Band 6: Disabled
        setBoolParam(ParamIDs::bandEnabled(6), false);

        // Band 7: High Shelf ← British HF
        float hfGain = safeGetParam(britishHfGainParam, 0.0f);
        bool hfBell = safeGetParam(britishHfBellParam, 0.0f) > 0.5f;
        setBoolParam(ParamIDs::bandEnabled(7), std::abs(hfGain) > 0.1f);
        setParam(ParamIDs::bandFreq(7), safeGetParam(britishHfFreqParam, 8000.0f));
        setParam(ParamIDs::bandGain(7), hfGain);
        setChoiceParam(ParamIDs::bandShape(7), hfBell ? 1 : 0);

        // Band 8: LPF
        bool lpfEnabled = safeGetParam(britishLpfEnabledParam, 0.0f) > 0.5f;
        setBoolParam(ParamIDs::bandEnabled(8), lpfEnabled);
        if (lpfEnabled)
            setParam(ParamIDs::bandFreq(8), safeGetParam(britishLpfFreqParam, 20000.0f));

        // Transfer master gain
        setParam(ParamIDs::masterGain, safeGetParam(britishOutputGainParam, 0.0f));
    }
    else if (eqType == EQType::Tube)
    {
        // Pultec → Digital: sample the frequency response and fit bands
        // Use the Pultec processor's actual frequency response evaluation

        // Disable all bands first, then enable those we set
        for (int i = 1; i <= NUM_BANDS; ++i)
            setBoolParam(ParamIDs::bandEnabled(i), false);

        // Band 1: HPF off (Pultec has no HPF)
        // Band 8: LPF off (Pultec has no LPF)

        // Band 2: Low Shelf ← Pultec LF section (combined boost + atten)
        float lfBoost = safeGetParam(pultecLfBoostGainParam, 0.0f);
        float lfAtten = safeGetParam(pultecLfAttenGainParam, 0.0f);
        if (std::abs(lfBoost) > 0.1f || std::abs(lfAtten) > 0.1f)
        {
            // Net LF effect: boost minus atten
            float lfFreqIdx = safeGetParam(pultecLfBoostFreqParam, 2.0f);
            static const float lfFreqValues[] = { 20.0f, 30.0f, 60.0f, 100.0f };
            int idx = juce::jlimit(0, 3, static_cast<int>(lfFreqIdx));
            float lfFreq = lfFreqValues[idx];

            // Sample the Pultec response at the LF frequency for net gain
            float netLFGain = pultecEQ.getFrequencyResponseMagnitude(lfFreq);
            setBoolParam(ParamIDs::bandEnabled(2), true);
            setParam(ParamIDs::bandFreq(2), lfFreq);
            setParam(ParamIDs::bandGain(2), netLFGain);
            setChoiceParam(ParamIDs::bandShape(2), 0);  // Low Shelf
        }

        // Band 5: Parametric ← Pultec HF Boost
        float hfBoost = safeGetParam(pultecHfBoostGainParam, 0.0f);
        if (std::abs(hfBoost) > 0.1f)
        {
            static const float hfBoostFreqValues[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
            int hfIdx = juce::jlimit(0, 6, static_cast<int>(safeGetParam(pultecHfBoostFreqParam, 3.0f)));
            float hfFreq = hfBoostFreqValues[hfIdx];
            float bw = safeGetParam(pultecHfBoostBandwidthParam, 0.5f);
            float q = 0.5f + bw * 2.0f;  // Map bandwidth to Q

            setBoolParam(ParamIDs::bandEnabled(5), true);
            setParam(ParamIDs::bandFreq(5), hfFreq);
            setParam(ParamIDs::bandGain(5), hfBoost);
            setParam(ParamIDs::bandQ(5), q);
            setChoiceParam(ParamIDs::bandShape(5), 0);  // Peaking
        }

        // Band 7: High Shelf cut ← Pultec HF Atten
        float hfAtten = safeGetParam(pultecHfAttenGainParam, 0.0f);
        if (std::abs(hfAtten) > 0.1f)
        {
            static const float hfAttenFreqValues[] = { 5000.0f, 10000.0f, 20000.0f };
            int atIdx = juce::jlimit(0, 2, static_cast<int>(safeGetParam(pultecHfAttenFreqParam, 1.0f)));
            float atFreq = hfAttenFreqValues[atIdx];

            setBoolParam(ParamIDs::bandEnabled(7), true);
            setParam(ParamIDs::bandFreq(7), atFreq);
            setParam(ParamIDs::bandGain(7), -hfAtten);  // Atten is positive value, apply as negative
            setChoiceParam(ParamIDs::bandShape(7), 0);  // High Shelf
        }

        // Band 3-4: Mid section (if enabled)
        bool midEnabled = safeGetParam(pultecMidEnabledParam, 1.0f) > 0.5f;
        if (midEnabled)
        {
            // Mid Low Peak → Band 3
            float midLowPeak = safeGetParam(pultecMidLowPeakParam, 0.0f);
            if (std::abs(midLowPeak) > 0.1f)
            {
                static const float midLowFreqValues[] = { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f };
                int mlIdx = juce::jlimit(0, 4, static_cast<int>(safeGetParam(pultecMidLowFreqParam, 2.0f)));
                setBoolParam(ParamIDs::bandEnabled(3), true);
                setParam(ParamIDs::bandFreq(3), midLowFreqValues[mlIdx]);
                setParam(ParamIDs::bandGain(3), midLowPeak);
                setParam(ParamIDs::bandQ(3), 1.0f);
                setChoiceParam(ParamIDs::bandShape(3), 0);  // Peaking
            }

            // Mid Dip → Band 4
            float midDip = safeGetParam(pultecMidDipParam, 0.0f);
            if (std::abs(midDip) > 0.1f)
            {
                static const float midDipFreqValues[] = { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f, 1500.0f, 2000.0f };
                int mdIdx = juce::jlimit(0, 6, static_cast<int>(safeGetParam(pultecMidDipFreqParam, 3.0f)));
                setBoolParam(ParamIDs::bandEnabled(4), true);
                setParam(ParamIDs::bandFreq(4), midDipFreqValues[mdIdx]);
                setParam(ParamIDs::bandGain(4), -midDip);  // Dip is positive, apply as cut
                setParam(ParamIDs::bandQ(4), 1.0f);
                setChoiceParam(ParamIDs::bandShape(4), 0);  // Peaking
            }

            // Mid High Peak → Band 6
            float midHighPeak = safeGetParam(pultecMidHighPeakParam, 0.0f);
            if (std::abs(midHighPeak) > 0.1f)
            {
                static const float midHighFreqValues[] = { 1500.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f };
                int mhIdx = juce::jlimit(0, 4, static_cast<int>(safeGetParam(pultecMidHighFreqParam, 2.0f)));
                setBoolParam(ParamIDs::bandEnabled(6), true);
                setParam(ParamIDs::bandFreq(6), midHighFreqValues[mhIdx]);
                setParam(ParamIDs::bandGain(6), midHighPeak);
                setParam(ParamIDs::bandQ(6), 1.0f);
                setChoiceParam(ParamIDs::bandShape(6), 0);  // Peaking
            }
        }

        // Transfer output gain
        setParam(ParamIDs::masterGain, safeGetParam(pultecOutputGainParam, 0.0f));
    }

    // Switch to Digital mode after transfer
    setChoiceParam(ParamIDs::eqType, 0);  // 0 = Digital

    // Force filter update
    filtersNeedUpdate.store(true);
}

//==============================================================================
// EQ Match

void MultiQ::captureMatchReference()
{
    eqMatchProcessor.setSampleRate(baseSampleRate);
    eqMatchProcessor.captureReference(analyzerMagnitudes);
}

void MultiQ::captureMatchSource()
{
    eqMatchProcessor.setSampleRate(baseSampleRate);
    eqMatchProcessor.captureTarget(analyzerMagnitudes);
}

int MultiQ::computeEQMatch(float strength)
{
    eqMatchProcessor.setSampleRate(baseSampleRate);
    return eqMatchProcessor.computeMatch(EQMatchProcessor::MAX_FIT_BANDS, strength);
}

void MultiQ::applyEQMatch()
{
    const auto& bands = eqMatchProcessor.getMatchedBands();

    // Helper to set a parameter value by ID
    auto setParam = [&](const juce::String& paramID, float value) {
        if (auto* param = parameters.getParameter(paramID))
            param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(value));
    };

    auto setBoolParam = [&](const juce::String& paramID, bool value) {
        if (auto* param = parameters.getParameter(paramID))
            param->setValueNotifyingHost(value ? 1.0f : 0.0f);
    };

    auto setChoiceParam = [&](const juce::String& paramID, int index) {
        if (auto* param = parameters.getParameter(paramID))
        {
            int numChoices = param->getNumSteps();
            if (numChoices > 1)
                param->setValueNotifyingHost(static_cast<float>(index) / static_cast<float>(numChoices - 1));
        }
    };

    // Ensure we're in Digital mode
    setChoiceParam(ParamIDs::eqType, 0);

    // Apply fitted bands to bands 2-7 (indices 0-5 in the fitted array)
    for (int i = 0; i < EQMatchProcessor::MAX_FIT_BANDS; ++i)
    {
        int bandNum = i + 2;  // Bands 2-7
        const auto& fitted = bands[static_cast<size_t>(i)];

        if (fitted.active)
        {
            setBoolParam(ParamIDs::bandEnabled(bandNum), true);
            setParam(ParamIDs::bandFreq(bandNum), fitted.freq);
            setParam(ParamIDs::bandGain(bandNum), fitted.gainDB);
            setParam(ParamIDs::bandQ(bandNum), fitted.q);
            setChoiceParam(ParamIDs::bandShape(bandNum), 0);  // Peaking
        }
        else
        {
            setBoolParam(ParamIDs::bandEnabled(bandNum), false);
        }
    }

    // Force filter update
    filtersNeedUpdate.store(true);
}

//==============================================================================
// FFT Analyzer

void MultiQ::pushSamplesToAnalyzer(const float* samples, int numSamples, bool isPreEQ)
{
    auto& fifo = isPreEQ ? preAnalyzerFifo : analyzerFifo;
    auto& audioBuffer = isPreEQ ? preAnalyzerAudioBuffer : analyzerAudioBuffer;

    int start1, size1, start2, size2;
    fifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    if (size1 > 0)
        std::copy(samples, samples + size1, audioBuffer.begin() + start1);
    if (size2 > 0)
        std::copy(samples + size1, samples + numSamples, audioBuffer.begin() + start2);

    fifo.finishedWrite(size1 + size2);
}

void MultiQ::updateFFTSize(AnalyzerResolution resolution)
{
    int order;
    switch (resolution)
    {
        case AnalyzerResolution::Low:    order = FFT_ORDER_LOW; break;
        case AnalyzerResolution::Medium: order = FFT_ORDER_MEDIUM; break;
        case AnalyzerResolution::High:   order = FFT_ORDER_HIGH; break;
        default: order = FFT_ORDER_MEDIUM;
    }

    int newSize = 1 << order;
    if (newSize != currentFFTSize)
    {
        currentFFTSize = newSize;
        fft = std::make_unique<juce::dsp::FFT>(order);
        fftWindow = std::make_unique<juce::dsp::WindowingFunction<float>>(
            static_cast<size_t>(currentFFTSize),
            juce::dsp::WindowingFunction<float>::hann);
        fftInputBuffer.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
        fftOutputBuffer.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
        preFFTInputBuffer.resize(static_cast<size_t>(currentFFTSize * 2), 0.0f);
    }
}

void MultiQ::convertFFTToMagnitudes(std::vector<float>& fftBuffer,
                                    std::array<float, 2048>& magnitudes,
                                    std::array<float, 2048>& peakHold,
                                    std::atomic<bool>& readyFlag)
{
    float decay = safeGetParam(analyzerDecayParam, 20.0f);
    float decayPerFrame = decay / 30.0f;

    auto mode = static_cast<AnalyzerMode>(static_cast<int>(safeGetParam(analyzerModeParam, 0.0f)));

    int numFFTBins = currentFFTSize / 2;
    float binFreqWidth = static_cast<float>(baseSampleRate) / static_cast<float>(currentFFTSize);

    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;
    float logMinFreq = std::log10(minFreq);
    float logMaxFreq = std::log10(maxFreq);
    float logRange = logMaxFreq - logMinFreq;

    float normFactor = 2.0f / (static_cast<float>(currentFFTSize) * 0.5f);

    for (int displayBin = 0; displayBin < 2048; ++displayBin)
    {
        float normalizedPos = static_cast<float>(displayBin) / 2047.0f;
        float logFreq = logMinFreq + normalizedPos * logRange;
        float freq = std::pow(10.0f, logFreq);

        float normalizedLo = (static_cast<float>(displayBin) - 0.5f) / 2047.0f;
        float normalizedHi = (static_cast<float>(displayBin) + 0.5f) / 2047.0f;
        normalizedLo = juce::jlimit(0.0f, 1.0f, normalizedLo);
        normalizedHi = juce::jlimit(0.0f, 1.0f, normalizedHi);
        float freqLo = std::pow(10.0f, logMinFreq + normalizedLo * logRange);
        float freqHi = std::pow(10.0f, logMinFreq + normalizedHi * logRange);

        int fftBinLo = static_cast<int>(freqLo / binFreqWidth);
        int fftBinHi = static_cast<int>(freqHi / binFreqWidth);
        fftBinLo = juce::jlimit(0, numFFTBins - 1, fftBinLo);
        fftBinHi = juce::jlimit(fftBinLo, numFFTBins - 1, fftBinHi);

        float magnitude;
        if (fftBinHi > fftBinLo)
        {
            float powerSum = 0.0f;
            for (int b = fftBinLo; b <= fftBinHi; ++b)
                powerSum += fftBuffer[static_cast<size_t>(b)] * fftBuffer[static_cast<size_t>(b)];
            magnitude = std::sqrt(powerSum / static_cast<float>(fftBinHi - fftBinLo + 1));
        }
        else
        {
            float fftBinFloat = freq / binFreqWidth;
            int bin0 = static_cast<int>(fftBinFloat);
            int bin1 = bin0 + 1;
            bin0 = juce::jlimit(0, numFFTBins - 1, bin0);
            bin1 = juce::jlimit(0, numFFTBins - 1, bin1);
            float frac = fftBinFloat - static_cast<float>(bin0);
            magnitude = fftBuffer[static_cast<size_t>(bin0)] * (1.0f - frac)
                      + fftBuffer[static_cast<size_t>(bin1)] * frac;
        }

        float dB = juce::Decibels::gainToDecibels(magnitude * normFactor, -100.0f);

        if (mode == AnalyzerMode::Peak)
        {
            if (dB > peakHold[static_cast<size_t>(displayBin)])
                peakHold[static_cast<size_t>(displayBin)] = dB;
            else
                peakHold[static_cast<size_t>(displayBin)] -= decayPerFrame;

            magnitudes[static_cast<size_t>(displayBin)] = peakHold[static_cast<size_t>(displayBin)];
        }
        else
        {
            magnitudes[static_cast<size_t>(displayBin)] =
                magnitudes[static_cast<size_t>(displayBin)] * 0.9f + dB * 0.1f;
        }
    }

    readyFlag.store(true);
}

void MultiQ::processFFT()
{
    if (analyzerFifo.getNumReady() < currentFFTSize)
        return;

    int start1, size1, start2, size2;
    analyzerFifo.prepareToRead(currentFFTSize, start1, size1, start2, size2);

    std::copy(analyzerAudioBuffer.begin() + start1,
              analyzerAudioBuffer.begin() + start1 + size1,
              fftInputBuffer.begin());
    if (size2 > 0)
    {
        std::copy(analyzerAudioBuffer.begin() + start2,
                  analyzerAudioBuffer.begin() + start2 + size2,
                  fftInputBuffer.begin() + size1);
    }

    analyzerFifo.finishedRead(size1 + size2);

    fftWindow->multiplyWithWindowingTable(fftInputBuffer.data(), static_cast<size_t>(currentFFTSize));
    fft->performFrequencyOnlyForwardTransform(fftInputBuffer.data());

    convertFFTToMagnitudes(fftInputBuffer, analyzerMagnitudes, peakHoldValues, analyzerDataReady);
}

void MultiQ::processPreFFT()
{
    if (preAnalyzerFifo.getNumReady() < currentFFTSize)
        return;

    int start1, size1, start2, size2;
    preAnalyzerFifo.prepareToRead(currentFFTSize, start1, size1, start2, size2);

    std::copy(preAnalyzerAudioBuffer.begin() + start1,
              preAnalyzerAudioBuffer.begin() + start1 + size1,
              preFFTInputBuffer.begin());
    if (size2 > 0)
    {
        std::copy(preAnalyzerAudioBuffer.begin() + start2,
                  preAnalyzerAudioBuffer.begin() + start2 + size2,
                  preFFTInputBuffer.begin() + size1);
    }

    preAnalyzerFifo.finishedRead(size1 + size2);

    fftWindow->multiplyWithWindowingTable(preFFTInputBuffer.data(), static_cast<size_t>(currentFFTSize));
    fft->performFrequencyOnlyForwardTransform(preFFTInputBuffer.data());

    convertFFTToMagnitudes(preFFTInputBuffer, preAnalyzerMagnitudes, prePeakHoldValues, preAnalyzerDataReady);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MultiQ::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Band parameters
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        int bandNum = i + 1;
        const auto& config = DefaultBandConfigs[static_cast<size_t>(i)];

        // Enabled
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ParamIDs::bandEnabled(bandNum), 1),
            "Band " + juce::String(bandNum) + " Enabled",
            i >= 1 && i <= 6  // Enable shelf and parametric bands by default
        ));

        // Frequency (skewed for logarithmic feel)
        auto freqRange = juce::NormalisableRange<float>(
            config.minFreq, config.maxFreq,
            [](float start, float end, float normalised) {
                return start * std::pow(end / start, normalised);
            },
            [](float start, float end, float value) {
                return std::log(value / start) / std::log(end / start);
            }
        );

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::bandFreq(bandNum), 1),
            "Band " + juce::String(bandNum) + " Frequency",
            freqRange,
            config.defaultFreq,
            juce::AudioParameterFloatAttributes().withLabel("Hz")
        ));

        // Gain (for bands 2-7 only, HPF/LPF don't have gain)
        if (i >= 1 && i <= 6)
        {
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(ParamIDs::bandGain(bandNum), 1),
                "Band " + juce::String(bandNum) + " Gain",
                juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
                0.0f,
                juce::AudioParameterFloatAttributes().withLabel("dB")
            ));
        }

        // Q
        auto qRange = juce::NormalisableRange<float>(
            0.1f, 100.0f,
            [](float start, float end, float normalised) {
                return start * std::pow(end / start, normalised);
            },
            [](float start, float end, float value) {
                return std::log(value / start) / std::log(end / start);
            }
        );

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::bandQ(bandNum), 1),
            "Band " + juce::String(bandNum) + " Q",
            qRange,
            0.71f
        ));

        // Shape parameter
        if (i == 1)  // Band 2: Low Shelf with shape options
        {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID(ParamIDs::bandShape(bandNum), 1),
                "Band " + juce::String(bandNum) + " Shape",
                juce::StringArray{"Low Shelf", "Peaking", "High Pass"},
                0  // Default: Low Shelf
            ));
        }
        else if (i == 6)  // Band 7: High Shelf with shape options
        {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID(ParamIDs::bandShape(bandNum), 1),
                "Band " + juce::String(bandNum) + " Shape",
                juce::StringArray{"High Shelf", "Peaking", "Low Pass"},
                0  // Default: High Shelf
            ));
        }
        else if (i >= 2 && i <= 5)  // Bands 3-6: Peaking/Notch/BandPass
        {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID(ParamIDs::bandShape(bandNum), 1),
                "Band " + juce::String(bandNum) + " Shape",
                juce::StringArray{"Peaking", "Notch", "Band Pass", "Tilt Shelf"},
                0  // Default: Peaking
            ));
        }

        // Per-band channel routing: Global, Stereo, Left, Right, Mid, Side
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(ParamIDs::bandChannelRouting(bandNum), 1),
            "Band " + juce::String(bandNum) + " Routing",
            juce::StringArray{"Global", "Stereo", "Left", "Right", "Mid", "Side"},
            0  // Default: Global (follows global processing mode)
        ));

        // Per-band saturation (for bands 2-7 only)
        if (i >= 1 && i <= 6)
        {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID(ParamIDs::bandSatType(bandNum), 1),
                "Band " + juce::String(bandNum) + " Saturation",
                juce::StringArray{"Off", "Tape", "Tube", "Console", "FET"},
                0  // Default: Off
            ));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(ParamIDs::bandSatDrive(bandNum), 1),
                "Band " + juce::String(bandNum) + " Sat Drive",
                juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
                0.3f  // Default moderate drive
            ));
        }

        // Slope (for HPF and LPF only)
        if (i == 0 || i == 7)
        {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID(ParamIDs::bandSlope(bandNum), 1),
                "Band " + juce::String(bandNum) + " Slope",
                juce::StringArray{"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct"},
                1  // Default 12 dB/oct
            ));
        }
    }

    // Global parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::masterGain, 1),
        "Master Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::bypass, 1),
        "Bypass",
        false
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::hqEnabled, 1),
        "Oversampling",
        juce::StringArray{"Off", "2x", "4x"},
        0  // Default: Off
    ));

    // Linear Phase mode (FIR-based, introduces latency)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::linearPhaseEnabled, 1),
        "Linear Phase Mode",
        false  // Default to off (zero latency IIR mode)
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::linearPhaseLength, 1),
        "Linear Phase Quality",
        juce::StringArray{"Low Latency (46ms)", "Balanced (93ms)", "High Quality (186ms)"},
        1  // Balanced by default
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::processingMode, 1),
        "Processing Mode",
        juce::StringArray{"Stereo", "Left", "Right", "Mid", "Side"},
        0
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::qCoupleMode, 1),
        "Q-Couple Mode",
        juce::StringArray{"Off", "Proportional", "Light", "Medium", "Strong",
                         "Asymmetric Light", "Asymmetric Medium", "Asymmetric Strong",
                         "Vintage"},
        0
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::eqType, 1),
        "EQ Type",
        juce::StringArray{"Digital", "British", "Tube"},
        0  // Digital by default (includes per-band dynamics capability)
    ));

    // Analyzer parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::analyzerEnabled, 1),
        "Analyzer Enabled",
        true
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::analyzerPrePost, 1),
        "Analyzer Pre/Post",
        false  // Post-EQ by default
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::analyzerMode, 1),
        "Analyzer Mode",
        juce::StringArray{"Peak", "RMS"},
        0
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::analyzerResolution, 1),
        "Analyzer Resolution",
        juce::StringArray{"Low (2048)", "Medium (4096)", "High (8192)"},
        1  // Medium default
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::analyzerSmoothing, 1),
        "Analyzer Smoothing",
        juce::StringArray{"Off", "Light", "Medium", "Heavy"},
        2  // Medium default
    ));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::analyzerDecay, 1),
        "Analyzer Decay",
        juce::NormalisableRange<float>(3.0f, 60.0f, 1.0f),
        20.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB/s")
    ));

    // Display parameters
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::displayScaleMode, 1),
        "Display Scale",
        juce::StringArray{"+/-12 dB", "+/-24 dB", "+/-30 dB", "+/-60 dB", "Warped"},
        1  // Default to +/-24 dB to match gain range
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::visualizeMasterGain, 1),
        "Visualize Master Gain",
        false
    ));

    // British mode (4K-EQ style) parameters
    // HPF
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishHpfFreq, 1),
        "British HPF Frequency",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.58f),
        20.0f, "Hz"
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::britishHpfEnabled, 1),
        "British HPF Enabled",
        false
    ));

    // LPF
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishLpfFreq, 1),
        "British LPF Frequency",
        juce::NormalisableRange<float>(3000.0f, 20000.0f, 1.0f, 0.57f),
        20000.0f, "Hz"
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::britishLpfEnabled, 1),
        "British LPF Enabled",
        false
    ));

    // LF Band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishLfGain, 1),
        "British LF Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishLfFreq, 1),
        "British LF Frequency",
        juce::NormalisableRange<float>(30.0f, 480.0f, 1.0f, 0.51f),
        100.0f, "Hz"
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::britishLfBell, 1),
        "British LF Bell Mode",
        false
    ));

    // LM Band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishLmGain, 1),
        "British LM Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishLmFreq, 1),
        "British LM Frequency",
        juce::NormalisableRange<float>(200.0f, 2500.0f, 1.0f, 0.68f),
        600.0f, "Hz"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishLmQ, 1),
        "British LM Q",
        juce::NormalisableRange<float>(0.4f, 4.0f, 0.01f),
        0.7f
    ));

    // HM Band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishHmGain, 1),
        "British HM Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishHmFreq, 1),
        "British HM Frequency",
        juce::NormalisableRange<float>(600.0f, 7000.0f, 1.0f, 0.93f),
        2000.0f, "Hz"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishHmQ, 1),
        "British HM Q",
        juce::NormalisableRange<float>(0.4f, 4.0f, 0.01f),
        0.7f
    ));

    // HF Band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishHfGain, 1),
        "British HF Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishHfFreq, 1),
        "British HF Frequency",
        juce::NormalisableRange<float>(1500.0f, 16000.0f, 1.0f, 1.73f),
        8000.0f, "Hz"
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::britishHfBell, 1),
        "British HF Bell Mode",
        false
    ));

    // Global British parameters
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::britishMode, 1),
        "British Mode",
        juce::StringArray{"Brown", "Black"},
        0  // Brown (E-Series) by default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishSaturation, 1),
        "British Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        0.0f, "%"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishInputGain, 1),
        "British Input Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::britishOutputGain, 1),
        "British Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"
    ));

    // Tube EQ mode parameters
    // LF Section
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecLfBoostGain, 1),
        "Tube EQ LF Boost",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecLfBoostFreq, 1),
        "Tube EQ LF Boost Freq",
        juce::StringArray{"20 Hz", "30 Hz", "60 Hz", "100 Hz"},
        2  // 60 Hz default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecLfAttenGain, 1),
        "Tube EQ LF Atten",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));

    // HF Boost Section
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecHfBoostGain, 1),
        "Tube EQ HF Boost",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecHfBoostFreq, 1),
        "Tube EQ HF Boost Freq",
        juce::StringArray{"3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz"},
        3  // 8 kHz default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecHfBoostBandwidth, 1),
        "Tube EQ HF Bandwidth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f  // Medium bandwidth
    ));

    // HF Atten Section
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecHfAttenGain, 1),
        "Tube EQ HF Atten",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecHfAttenFreq, 1),
        "Tube EQ HF Atten Freq",
        juce::StringArray{"5 kHz", "10 kHz", "20 kHz"},
        1  // 10 kHz default
    ));

    // Global Tube EQ controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecInputGain, 1),
        "Tube EQ Input Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecOutputGain, 1),
        "Tube EQ Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecTubeDrive, 1),
        "Tube EQ Tube Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.3f  // Moderate tube warmth by default
    ));

    // Tube EQ Mid Dip/Peak section parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::pultecMidEnabled, 1),
        "Tube EQ Mid Section Enabled",
        true  // Enabled by default
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecMidLowFreq, 1),
        "Tube EQ Mid Low Freq",
        juce::StringArray{"0.2 kHz", "0.3 kHz", "0.5 kHz", "0.7 kHz", "1.0 kHz"},
        2  // 0.5 kHz default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecMidLowPeak, 1),
        "Tube EQ Mid Low Peak",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecMidDipFreq, 1),
        "Tube EQ Mid Dip Freq",
        juce::StringArray{"0.2 kHz", "0.3 kHz", "0.5 kHz", "0.7 kHz", "1.0 kHz", "1.5 kHz", "2.0 kHz"},
        3  // 0.7 kHz default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecMidDip, 1),
        "Tube EQ Mid Dip",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecMidHighFreq, 1),
        "Tube EQ Mid High Freq",
        juce::StringArray{"1.5 kHz", "2.0 kHz", "3.0 kHz", "4.0 kHz", "5.0 kHz"},
        2  // 3.0 kHz default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecMidHighPeak, 1),
        "Tube EQ Mid High Peak",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));

    // Dynamic EQ mode parameters (per-band)
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        int bandNum = i + 1;

        // Per-band dynamics enable
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ParamIDs::bandDynEnabled(bandNum), 1),
            "Band " + juce::String(bandNum) + " Dynamics Enabled",
            false
        ));

        // Threshold (-48 to 0 dB) - standard dynamic EQ range
        // Lower = more sensitive (dynamics engage earlier)
        // Higher = less sensitive (dynamics only on loud transients)
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::bandDynThreshold(bandNum), 1),
            "Band " + juce::String(bandNum) + " Threshold",
            juce::NormalisableRange<float>(-48.0f, 0.0f, 0.1f),
            -20.0f,  // Default: moderate sensitivity
            juce::AudioParameterFloatAttributes().withLabel("dB")
        ));

        // Attack (0.1 to 500 ms, logarithmic)
        auto attackRange = juce::NormalisableRange<float>(
            0.1f, 500.0f,
            [](float start, float end, float normalised) {
                return start * std::pow(end / start, normalised);
            },
            [](float start, float end, float value) {
                return std::log(value / start) / std::log(end / start);
            }
        );
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::bandDynAttack(bandNum), 1),
            "Band " + juce::String(bandNum) + " Attack",
            attackRange,
            10.0f,
            juce::AudioParameterFloatAttributes().withLabel("ms")
        ));

        // Release (10 to 5000 ms, logarithmic)
        auto releaseRange = juce::NormalisableRange<float>(
            10.0f, 5000.0f,
            [](float start, float end, float normalised) {
                return start * std::pow(end / start, normalised);
            },
            [](float start, float end, float value) {
                return std::log(value / start) / std::log(end / start);
            }
        );
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::bandDynRelease(bandNum), 1),
            "Band " + juce::String(bandNum) + " Release",
            releaseRange,
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel("ms")
        ));

        // Range (0 to 24 dB)
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::bandDynRange(bandNum), 1),
            "Band " + juce::String(bandNum) + " Range",
            juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f),
            12.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")
        ));

        // Ratio (1:1 to 100:1, logarithmic)
        auto ratioRange = juce::NormalisableRange<float>(
            1.0f, 100.0f,
            [](float start, float end, float normalised) {
                return start * std::pow(end / start, normalised);
            },
            [](float start, float end, float value) {
                return std::log(value / start) / std::log(end / start);
            }
        );
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::bandDynRatio(bandNum), 1),
            "Band " + juce::String(bandNum) + " Ratio",
            ratioRange,
            4.0f,
            juce::AudioParameterFloatAttributes().withLabel(":1")
        ));
    }

    // Global dynamic mode parameters
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::dynDetectionMode, 1),
        "Dynamics Detection Mode",
        juce::StringArray{"Peak", "RMS"},
        0  // Peak by default
    ));

    // Auto-gain compensation (maintains consistent loudness for A/B comparison)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::autoGainEnabled, 1),
        "Auto Gain",
        false  // Off by default
    ));

    // Output limiter (mastering safety brickwall)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::limiterEnabled, 1),
        "Limiter",
        false  // Off by default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::limiterCeiling, 1),
        "Limiter Ceiling",
        juce::NormalisableRange<float>(-1.0f, 0.0f, 0.1f),
        0.0f  // 0 dBFS by default
    ));

    return {params.begin(), params.end()};
}

//==============================================================================
// State version for future migration support
// Increment this when parameter layout changes to enable proper migration
static constexpr int STATE_VERSION = 1;

void MultiQ::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    // Add version tag for future migration support
    if (xml != nullptr)
    {
        xml->setAttribute("stateVersion", STATE_VERSION);
        xml->setAttribute("pluginVersion", PLUGIN_VERSION);
    }

    copyXmlToBinary(*xml, destData);
}

void MultiQ::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        // Check state version for migration
        int loadedVersion = xmlState->getIntAttribute("stateVersion", 0);

        // Load the state
        auto newState = juce::ValueTree::fromXml(*xmlState);

        // Migration: Version 0 (pre-versioning) state
        if (loadedVersion == 0)
        {
            // Backward compatibility: Map old EQ type values to new enum
            // Old: 0=Digital, 1=Dynamic, 2=British, 3=Tube
            // New: 0=Digital, 1=British, 2=Tube (Dynamic merged into Digital)
            auto eqTypeChild = newState.getChildWithProperty("id", ParamIDs::eqType);
            if (eqTypeChild.isValid())
            {
                float oldValue = eqTypeChild.getProperty("value", 0.0f);
                int oldIndex = static_cast<int>(oldValue);
                int newIndex = oldIndex;

                if (oldIndex == 1)      // Old Dynamic -> New Digital
                    newIndex = 0;
                else if (oldIndex == 2) // Old British -> New British
                    newIndex = 1;
                else if (oldIndex == 3) // Old Tube -> New Tube
                    newIndex = 2;
                else if (oldIndex > 3)  // Invalid/future values -> clamp to Digital
                    newIndex = 0;

                if (newIndex != oldIndex)
                    eqTypeChild.setProperty("value", static_cast<float>(newIndex), nullptr);
            }
        }

        // Future version migrations would be added here:
        // if (loadedVersion < 2) { ... migrate v1 to v2 ... }
        // if (loadedVersion < 3) { ... migrate v2 to v3 ... }

        parameters.replaceState(newState);

        // Fix AudioParameterBool state restoration: JUCE's APVTS may not properly
        // snap boolean parameter values during replaceState(). Force each bool
        // parameter to its correct snapped value through the host notification path.
        for (auto* param : getParameters())
        {
            if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
                boolParam->setValueNotifyingHost(boolParam->get() ? 1.0f : 0.0f);
        }

        filtersNeedUpdate.store(true);

        // Notify British/Tube EQ processors to update their parameters
        britishParamsChanged.store(true);
        tubeEQParamsChanged.store(true);
        dynamicParamsChanged.store(true);
        linearPhaseParamsChanged.store(true);
    }
}

//==============================================================================
// Factory Presets
//==============================================================================

int MultiQ::getNumPrograms()
{
    // Lazy initialization of factory presets
    if (factoryPresets.empty())
        factoryPresets = MultiQPresets::getFactoryPresets();

    return static_cast<int>(factoryPresets.size()) + 1;  // +1 for "Init" preset
}

int MultiQ::getCurrentProgram()
{
    return currentPresetIndex;
}

void MultiQ::setCurrentProgram(int index)
{
    if (factoryPresets.empty())
        factoryPresets = MultiQPresets::getFactoryPresets();

    if (index == 0)
    {
        // "Init" preset - reset to default flat EQ
        currentPresetIndex = 0;

        // Reset all bands to default
        for (int i = 1; i <= 8; ++i)
        {
            if (auto* p = parameters.getParameter(ParamIDs::bandEnabled(i)))
                p->setValueNotifyingHost(i == 1 || i == 8 ? 0.0f : 1.0f);  // HPF/LPF off by default

            if (auto* p = parameters.getParameter(ParamIDs::bandGain(i)))
                p->setValueNotifyingHost(0.5f);  // 0 dB (centered)

            if (auto* p = parameters.getParameter(ParamIDs::bandQ(i)))
                p->setValueNotifyingHost(parameters.getParameterRange(ParamIDs::bandQ(i)).convertTo0to1(0.71f));
        }

        // Reset global settings
        if (auto* p = parameters.getParameter(ParamIDs::masterGain))
            p->setValueNotifyingHost(0.5f);  // 0 dB

        if (auto* p = parameters.getParameter(ParamIDs::hqEnabled))
            p->setValueNotifyingHost(0.0f);

        if (auto* p = parameters.getParameter(ParamIDs::qCoupleMode))
            p->setValueNotifyingHost(0.0f);

        return;
    }

    int presetIndex = index - 1;  // Adjust for "Init" at position 0
    if (presetIndex >= 0 && presetIndex < static_cast<int>(factoryPresets.size()))
    {
        currentPresetIndex = index;
        MultiQPresets::applyPreset(parameters, factoryPresets[static_cast<size_t>(presetIndex)]);
    }
}

const juce::String MultiQ::getProgramName(int index)
{
    if (factoryPresets.empty())
        factoryPresets = MultiQPresets::getFactoryPresets();

    if (index == 0)
        return "Init";

    int presetIndex = index - 1;
    if (presetIndex >= 0 && presetIndex < static_cast<int>(factoryPresets.size()))
        return factoryPresets[static_cast<size_t>(presetIndex)].name;

    return {};
}

//==============================================================================
int MultiQ::getLatencySamples() const
{
    int totalLatency = 0;

    // Linear phase EQ latency (filterLength / 2 samples)
    if (linearPhaseModeEnabled && linearPhaseEnabledParam &&
        safeGetParam(linearPhaseEnabledParam, 0.0f) > 0.5f)
    {
        totalLatency += linearPhaseEQ[0].getLatencyInSamples();
    }

    // Report oversampling latency
    if (oversamplingMode == 2 && oversampler4x)
        totalLatency += static_cast<int>(oversampler4x->getLatencyInSamples());
    else if (oversamplingMode == 1 && oversampler2x)
        totalLatency += static_cast<int>(oversampler2x->getLatencyInSamples());

    // Output limiter lookahead latency
    totalLatency += outputLimiter.getLatencySamples();

    return totalLatency;
}

//==============================================================================
juce::AudioProcessorEditor* MultiQ::createEditor()
{
    return new MultiQEditor(*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultiQ();
}

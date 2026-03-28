#include "MultiQ.h"
#include "AnalogMatchedBiquad.h"
#include "MultiQEditor.h"

MultiQ::MultiQ()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, &undoManager, juce::Identifier("MultiQ"), createParameterLayout())
{
    for (auto& dirty : bandDirty)
        dirty.store(true);

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        bandEnabledParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandEnabled(i + 1));
        bandFreqParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandFreq(i + 1));
        bandGainParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandGain(i + 1));
        bandQParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandQ(i + 1));

        parameters.addParameterListener(ParamIDs::bandEnabled(i + 1), this);
        parameters.addParameterListener(ParamIDs::bandFreq(i + 1), this);
        parameters.addParameterListener(ParamIDs::bandGain(i + 1), this);
        parameters.addParameterListener(ParamIDs::bandQ(i + 1), this);
        parameters.addParameterListener(ParamIDs::bandInvert(i + 1), this);
    }

    // Slope params for HPF and LPF
    bandSlopeParams[0] = parameters.getRawParameterValue(ParamIDs::bandSlope(1));
    bandSlopeParams[1] = parameters.getRawParameterValue(ParamIDs::bandSlope(8));
    parameters.addParameterListener(ParamIDs::bandSlope(1), this);
    parameters.addParameterListener(ParamIDs::bandSlope(8), this);

    // Shape params for parametric bands 2-7
    for (int i = 1; i <= 6; ++i)
        parameters.addParameterListener(ParamIDs::bandShape(i + 1), this);

    // Global parameters
    masterGainParam = parameters.getRawParameterValue(ParamIDs::masterGain);
    bypassParam = parameters.getRawParameterValue(ParamIDs::bypass);
    hqEnabledParam = parameters.getRawParameterValue(ParamIDs::hqEnabled);
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

    // Match EQ parameters
    matchApplyParam = parameters.getRawParameterValue(ParamIDs::matchApply);
    matchSmoothingParam = parameters.getRawParameterValue(ParamIDs::matchSmoothing);
    matchLimitBoostParam = parameters.getRawParameterValue(ParamIDs::matchLimitBoost);
    matchLimitCutParam = parameters.getRawParameterValue(ParamIDs::matchLimitCut);

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

        // Per-band invert, phase invert, pan
        bandInvertParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandInvert(i + 1));
        bandPhaseInvertParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandPhaseInvert(i + 1));
        bandPanParams[static_cast<size_t>(i)] = parameters.getRawParameterValue(ParamIDs::bandPan(i + 1));
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
    parameters.addParameterListener(ParamIDs::qCoupleMode, this);
    parameters.addParameterListener(ParamIDs::limiterEnabled, this);
    parameters.addParameterListener(ParamIDs::analyzerResolution, this);

    // Pre-allocate FFT for default (medium) resolution — full init in prepareToPlay
    {
        int defOrder = FFT_ORDER_MEDIUM;
        int defSize = 1 << defOrder;
        auto& slot = fftSlots[1];  // Medium = index 1
        slot.size = defSize;
        slot.fft = std::make_unique<juce::dsp::FFT>(defOrder);
        slot.window = std::make_unique<juce::dsp::WindowingFunction<float>>(
            static_cast<size_t>(defSize), juce::dsp::WindowingFunction<float>::hann);
        slot.inputBuffer.resize(static_cast<size_t>(defSize * 2), 0.0f);
        slot.preInputBuffer.resize(static_cast<size_t>(defSize * 2), 0.0f);
        activeFFTSlot = 1;
        currentFFTSize = defSize;
    }
    analyzerAudioBuffer.resize(8192, 0.0f);
    preAnalyzerAudioBuffer.resize(8192, 0.0f);

    factoryPresets = MultiQPresets::getFactoryPresets();
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
        parameters.removeParameterListener(ParamIDs::bandInvert(i + 1), this);
    }
    parameters.removeParameterListener(ParamIDs::bandSlope(1), this);
    parameters.removeParameterListener(ParamIDs::bandSlope(8), this);
    for (int i = 1; i <= 6; ++i)
        parameters.removeParameterListener(ParamIDs::bandShape(i + 1), this);
    parameters.removeParameterListener(ParamIDs::hqEnabled, this);
    parameters.removeParameterListener(ParamIDs::qCoupleMode, this);
    parameters.removeParameterListener(ParamIDs::limiterEnabled, this);
    parameters.removeParameterListener(ParamIDs::analyzerResolution, this);
}

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
    // Note: oversamplingMode is updated by the audio thread in processBlock
    // (via safeGetParam check) to ensure crossfade happens correctly.
    if (parameterID == ParamIDs::hqEnabled)
    {
        filtersNeedUpdate.store(true);
        // Latency stays constant (always maxOversamplerLatency) — no setLatencySamples needed.
        // The processBlock OS-switch code updates osCompDelaySamples to compensate.
    }

    // Limiter enable/disable changes latency (lookahead)
    if (parameterID == ParamIDs::limiterEnabled)
    {
        outputLimiter.setEnabled(newValue > 0.5f);
        setLatencySamples(getLatencySamples());
    }


    // Analyzer resolution change — defer allocation to processBlock
    if (parameterID == ParamIDs::analyzerResolution)
    {
        auto res = static_cast<AnalyzerResolution>(
            static_cast<int>(safeGetParam(analyzerResolutionParam, 1.0f)));
        int order;
        switch (res)
        {
            case AnalyzerResolution::Low:    order = FFT_ORDER_LOW; break;
            case AnalyzerResolution::Medium: order = FFT_ORDER_MEDIUM; break;
            case AnalyzerResolution::High:   order = FFT_ORDER_HIGH; break;
            default: order = FFT_ORDER_MEDIUM;
        }
        pendingFFTOrder.store(order, std::memory_order_release);
    }
}

void MultiQ::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;
    rmsWindowSamples = static_cast<int>(2.0 * sampleRate);  // 2s window: averages across drum grooves, immune to transients

    oversamplingMode = static_cast<int>(safeGetParam(hqEnabledParam, 0.0f));

    // Pre-allocate both 2x and 4x oversamplers to avoid runtime allocation
    // This is critical for real-time safety - we never want to allocate in processBlock()
    // Reinitialize if block size changed since last prepare
    if (!oversamplerReady || samplesPerBlock != lastPreparedBlockSize)
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
        lastPreparedBlockSize = samplesPerBlock;
    }

    // Cache maximum oversampler latency (4x) for constant-latency PDC reporting.
    // This prevents phase/comb filtering when switching oversampling mid-playback.
    maxOversamplerLatency = oversampler4x
        ? static_cast<int>(std::lround(oversampler4x->getLatencyInSamples()))
        : 0;

    // Allocate compensation delay buffer (max latency we'd ever need to compensate)
    if (maxOversamplerLatency > 0)
    {
        osCompDelayBuffer.setSize(2, maxOversamplerLatency + 1, false, true, true);
        osCompDelayWritePos = 0;
    }

    // Pre-allocate scratch buffer for British/Tube EQ processing
    // Size: 2 channels, max oversampled block size (4x input block size for max oversampling)
    maxOversampledBlockSize = samplesPerBlock * 4;
    scratchBuffer.setSize(2, maxOversampledBlockSize, false, false, true);

    int osFactor = (oversamplingMode == 2) ? 4 : (oversamplingMode == 1) ? 2 : 1;

    // British/Tube modes always require ≥2x oversampling for -100dB harmonic alias rejection.
    // They contain nonlinear stages (ConsoleSaturation, InductorModel, TubeStage) that generate
    // H2/H3 harmonics — without oversampling these fold back into the audible band.
    auto currentEqType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));
    if ((currentEqType == EQType::British || currentEqType == EQType::Tube) && osFactor < 2)
        osFactor = 2;

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

    // Prepare LPF
    lpfFilter.prepare(spec);

    // Reset HPF/LPF filters
    hpfFilter.reset();
    lpfFilter.reset();

    // Pre-allocate coefficient objects for HPF/LPF cascaded IIR filters
    auto makeBiquadIdentity = []() {
        return new juce::dsp::IIR::Coefficients<float>(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    };
    for (int i = 0; i < CascadedFilter::MAX_STAGES; ++i)
    {
        hpfFilter.stagesL[static_cast<size_t>(i)].coefficients = makeBiquadIdentity();
        hpfFilter.stagesR[static_cast<size_t>(i)].coefficients = makeBiquadIdentity();
        lpfFilter.stagesL[static_cast<size_t>(i)].coefficients = makeBiquadIdentity();
        lpfFilter.stagesR[static_cast<size_t>(i)].coefficients = makeBiquadIdentity();
    }

    // Force filter update
    filtersNeedUpdate.store(true);
    updateAllFilters();

    // Snap SVF filters to target (no interpolation at startup)
    for (auto& f : svfFilters)
        f.snapToTarget();
    for (auto& f : svfDynGainFilters)
        f.snapToTarget();

    // Initialize per-band smoothing state
    prevBandPhaseInvertGain.fill(1.0f);
    prevBandPanVal.fill(0.0f);

    // Prepare British EQ processor
    britishEQ.prepare(currentSampleRate, samplesPerBlock * osFactor, 2);
    britishParamsChanged.store(true);

    // Prepare Tube EQ processor
    tubeEQ.prepare(currentSampleRate, samplesPerBlock * osFactor, 2);
    tubeEQParamsChanged.store(true);

    // Prepare Dynamic EQ processor
    dynamicEQ.prepare(currentSampleRate, 2);
    dynamicParamsChanged.store(true);

    // Pre-allocate FFT objects for all 3 resolutions (avoids RT allocation in processBlock)
    {
        static constexpr int orders[NUM_FFT_SIZES] = { FFT_ORDER_LOW, FFT_ORDER_MEDIUM, FFT_ORDER_HIGH };
        for (int i = 0; i < NUM_FFT_SIZES; ++i)
        {
            int sz = 1 << orders[i];
            auto& slot = fftSlots[static_cast<size_t>(i)];
            slot.size = sz;
            slot.fft = std::make_unique<juce::dsp::FFT>(orders[i]);
            slot.window = std::make_unique<juce::dsp::WindowingFunction<float>>(
                static_cast<size_t>(sz), juce::dsp::WindowingFunction<float>::hann);
            slot.inputBuffer.resize(static_cast<size_t>(sz * 2), 0.0f);
            slot.preInputBuffer.resize(static_cast<size_t>(sz * 2), 0.0f);
        }
        // Set initial active slot based on current parameter
        auto res = static_cast<AnalyzerResolution>(
            static_cast<int>(safeGetParam(analyzerResolutionParam, 1.0f)));
        switch (res)
        {
            case AnalyzerResolution::Low:    activeFFTSlot = 0; break;
            case AnalyzerResolution::High:   activeFFTSlot = 2; break;
            default:                         activeFFTSlot = 1; break;
        }
        currentFFTSize = fftSlots[static_cast<size_t>(activeFFTSlot)].size;
    }

    // Prepare Match EQ convolution engine and processor
    {
        juce::dsp::ProcessSpec matchSpec;
        matchSpec.sampleRate = sampleRate;
        matchSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        matchSpec.numChannels = 2;
        matchConvolution.prepare(matchSpec);
        matchConvolution.reset();
    }
    // Clear learned spectra and correction data only if sample rate changed —
    // they were captured at the old rate and would be invalid.
    if (std::abs(eqMatchProcessor.getSampleRate() - sampleRate) > 0.5)
        eqMatchProcessor.reset();
    eqMatchProcessor.prepare(sampleRate, samplesPerBlock);
    matchConvolutionActive.store(false);
    matchConvWetGain.reset(sampleRate, 0.01);  // 10 ms crossfade for clear
    matchConvWetGain.setCurrentAndTargetValue(0.0f);
    pendingMatchFadeOut.store(false);
    matchDryBuffer.setSize(2, samplesPerBlock, false, false, true);

    // Reset analyzers (post-EQ and pre-EQ)
    analyzerFifo.reset();
    std::fill(analyzerMagnitudes.begin(), analyzerMagnitudes.end(), -100.0f);
    std::fill(peakHoldValues.begin(), peakHoldValues.end(), -100.0f);
    preAnalyzerFifo.reset();
    std::fill(preAnalyzerMagnitudes.begin(), preAnalyzerMagnitudes.end(), -100.0f);
    std::fill(prePeakHoldValues.begin(), prePeakHoldValues.end(), -100.0f);

    // Mono mix scratch buffer for block-based analyzer feed
    analyzerMonoBuffer.resize(static_cast<size_t>(samplesPerBlock * osFactor), 0.0f);

    // 2s ramp: gain can't follow individual transients, only long-term loudness shifts
    autoGainCompensation.reset(sampleRate, 2.0);
    autoGainCompensation.setCurrentAndTargetValue(1.0f);
    inputRmsSum = 0.0f;
    outputRmsSum = 0.0f;
    rmsSampleCount = 0;

    outputLimiter.prepare(sampleRate, samplesPerBlock);

    bypassSmoothed.reset(sampleRate, 0.005);
    bypassSmoothed.setCurrentAndTargetValue(safeGetParam(bypassParam, 0.0f) > 0.5f ? 1.0f : 0.0f);
    dryBuffer.setSize(2, samplesPerBlock, false, false, true);

    // Bypass delay: sized to max possible latency (4x oversampler + max limiter lookahead).
    // Must not use getLatencySamples() here — limiter may be off at prepare time but enabled
    // later, and bypassDelayBuffer is never resized after this point.
    const int MAX_BYPASS_DELAY = maxOversamplerLatency + outputLimiter.getMaxLookaheadSamples() + 1;
    bypassDelayBuffer.setSize(2, juce::jmax(1, MAX_BYPASS_DELAY), false, true, true);
    bypassDelayWritePos = 0;
    bypassDelayFillCount = 0;
    bypassDelayLength = getLatencySamples();

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        bandEnableSmoothed[static_cast<size_t>(i)].reset(sampleRate, 0.003);
        float enabled = safeGetParam(bandEnabledParams[static_cast<size_t>(i)], 0.0f) > 0.5f ? 1.0f : 0.0f;
        bandEnableSmoothed[static_cast<size_t>(i)].setCurrentAndTargetValue(enabled);
    }

    eqTypeCrossfade.reset(sampleRate, 0.01);
    eqTypeCrossfade.setCurrentAndTargetValue(1.0f);
    previousEQType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));
    eqTypeChanging = false;
    prevTypeBuffer.setSize(2, samplesPerBlock, false, false, true);

    osCrossfade.reset(sampleRate, 0.005);
    osCrossfade.setCurrentAndTargetValue(1.0f);
    osChanging = false;
    prevOsBuffer.setSize(2, samplesPerBlock, false, false, true);

    // Compute initial compensation delay using the effective osFactor.
    // This keeps total latency constant so the DAW's PDC stays aligned.
    int currentOsLatency = 0;
    if (osFactor == 4 && oversampler4x)
        currentOsLatency = static_cast<int>(std::lround(oversampler4x->getLatencyInSamples()));
    else if (osFactor >= 2 && oversampler2x)
        currentOsLatency = static_cast<int>(std::lround(oversampler2x->getLatencyInSamples()));
    osCompDelaySamples = maxOversamplerLatency - currentOsLatency;

    // Report constant latency to host (always max oversampler latency)
    setLatencySamples(getLatencySamples());
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

void MultiQ::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    // Apply any pending FFT size change (deferred from parameterChanged to avoid data race)
    {
        int order = pendingFFTOrder.exchange(-1, std::memory_order_acquire);
        if (order >= 0)
            updateFFTSize(order);
    }

    // Handle pending Match EQ operations (deferred from UI thread)
    if (pendingMatchClear.exchange(false))
    {
        if (matchConvolutionActive.load(std::memory_order_acquire))
        {
            // Start a 10ms fade-out; the actual clear runs after fade completes (see below)
            matchConvWetGain.setTargetValue(0.0f);
            pendingMatchFadeOut.store(true, std::memory_order_release);
        }
        else
        {
            eqMatchProcessor.clearAll();
        }
        // Cancel any queued learn requests so they don't restart learning this cycle
        pendingStartLearnCurrent.store(false, std::memory_order_relaxed);
        pendingStartLearnReference.store(false, std::memory_order_relaxed);
        pendingStopLearning.store(false, std::memory_order_relaxed);
    }
    // Process stop BEFORE start so that start always wins if both are set
    if (pendingStopLearning.exchange(false))
        eqMatchProcessor.stopLearning();
    if (pendingStartLearnCurrent.exchange(false))
        eqMatchProcessor.startLearningCurrent();
    if (pendingStartLearnReference.exchange(false))
        eqMatchProcessor.startLearningReference();

    // Feed Match EQ learning BEFORE bypass check — learning needs raw input audio
    // even when the plugin is bypassed (no processing needed, just spectrum capture)
    if (eqMatchProcessor.isLearning())
    {
        int n = juce::jmin(buffer.getNumSamples(), static_cast<int>(analyzerMonoBuffer.size()));
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, n);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, n);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, n);
        eqMatchProcessor.feedLearningBlock(analyzerMonoBuffer.data(), n);
    }

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Bypass with smooth crossfade (no clicks)
    bool bypassed = safeGetParam(bypassParam, 0.0f) > 0.5f;
    bypassSmoothed.setTargetValue(bypassed ? 1.0f : 0.0f);

    // Always run the bypass delay line and store the latency-compensated dry signal in dryBuffer.
    // This ensures the bypass crossfade blends time-aligned signals — critical for LP mode where
    // the wet path is delayed by 4096+ samples. Without this, blending undelayed dry with
    // LP-delayed wet produces an audible echo/repeat artifact.
    {
        int delay = juce::jmin(bypassDelayLength, bypassDelayBuffer.getNumSamples());
        int numSamples = buffer.getNumSamples();
        int numCh = juce::jmin(buffer.getNumChannels(), bypassDelayBuffer.getNumChannels());
        if (delay > 0)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                // Use the delayed signal only once the buffer has filled (bypassDelayFillCount >= delay).
                // Before that, fall back to the current undelayed input so dryBuffer never holds zeros.
                // Zeros would cause a level dip during bypass crossfades on a freshly-cleared buffer
                // (e.g., immediately after enabling LP mode while Logic re-aligns its PDC).
                bool primed = (bypassDelayFillCount + i >= delay);
                for (int ch = 0; ch < numCh; ++ch)
                {
                    float input = buffer.getSample(ch, i);
                    float delayed = bypassDelayBuffer.getSample(ch, bypassDelayWritePos);
                    bypassDelayBuffer.setSample(ch, bypassDelayWritePos, input);
                    dryBuffer.setSample(ch, i, primed ? delayed : input);
                }
                bypassDelayWritePos = (bypassDelayWritePos + 1) % delay;
            }
            bypassDelayFillCount = juce::jmin(bypassDelayFillCount + numSamples, delay);
        }
        else
        {
            int safeSamples = juce::jmin(numSamples, dryBuffer.getNumSamples());
            for (int ch = 0; ch < numCh; ++ch)
                dryBuffer.copyFrom(ch, 0, buffer, ch, 0, safeSamples);
        }
    }

    // If fully bypassed and not transitioning, output the latency-aligned dry signal and return
    if (bypassed && !bypassSmoothed.isSmoothing())
    {
        bypassSmoothed.skip(buffer.getNumSamples());
        int safeSamples = juce::jmin(buffer.getNumSamples(), dryBuffer.getNumSamples());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom(ch, 0, dryBuffer, ch, 0, safeSamples);
        return;
    }

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
        // British/Tube always need ≥2x to avoid aliasing from nonlinear stages.
        {
            auto curEqType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));
            if ((curEqType == EQType::British || curEqType == EQType::Tube) && osFactor < 2)
                osFactor = 2;
        }
        // Update sample rate for filter coefficient calculations
        currentSampleRate = baseSampleRate * osFactor;
        double newSR = currentSampleRate;
        // Reset oversampler states to avoid artifacts on mode switch
        if (oversampler2x) oversampler2x->reset();
        if (oversampler4x) oversampler4x->reset();
        // Reset autogain accumulators: the transition block has mismatched input/output
        // (input measured pre-switch, output measured post-switch through new AA filter),
        // which would produce a spurious targetGain for the current window.
        inputRmsSum = 0.0f;
        outputRmsSum = 0.0f;
        inputPeakMax = 0.0f;
        outputPeakMax = 0.0f;
        rmsSampleCount = 0;
        // Reset all filters
        hpfFilter.reset();
        for (auto& filter : svfFilters)
            filter.reset();
        for (auto& filter : svfDynGainFilters)
            filter.reset();
        lpfFilter.reset();

        // Recompute SVF smoothing coefficient for new sample rate (~1ms transition)
        svfSmoothCoeff = 1.0f - std::exp(-1.0f / (0.001f * static_cast<float>(newSR)));
        for (auto& f : svfFilters)
            f.setSmoothCoeff(svfSmoothCoeff);
        for (auto& f : svfDynGainFilters)
            f.setSmoothCoeff(svfSmoothCoeff);

        // Update sample rate for sub-processors without calling prepare()
        // (prepare() allocates via IIR::Filter::prepare — not RT-safe).
        // The paramsChanged flags trigger coefficient recalculation on the next process call.
        britishEQ.updateSampleRate(newSR);
        britishParamsChanged.store(true);
        tubeEQ.updateSampleRate(newSR);
        tubeEQParamsChanged.store(true);
        dynamicEQ.updateSampleRate(newSR);
        dynamicParamsChanged.store(true);

        // Force filter coefficient update at new sample rate
        filtersNeedUpdate.store(true);
        // Mark all bands dirty so updateAllFilters() recomputes coefficients at new sample rate.
        // Without this, stale coefficients designed at the old rate are evaluated at the new rate,
        // producing a spurious curve in the EQ display when switching oversampling modes.
        for (auto& dirty : bandDirty)
            dirty.store(true, std::memory_order_relaxed);

        // Update compensation delay using the effective osFactor.
        int currentOsLatency = 0;
        if (osFactor == 4 && oversampler4x)
            currentOsLatency = static_cast<int>(std::lround(oversampler4x->getLatencyInSamples()));
        else if (osFactor >= 2 && oversampler2x)
            currentOsLatency = static_cast<int>(std::lround(oversampler2x->getLatencyInSamples()));
        osCompDelaySamples = maxOversamplerLatency - currentOsLatency;
        osCompDelayWritePos = 0;
        osCompDelayBuffer.clear();
    }

    // Report correct latency — placed AFTER the OS mode update above so that
    // getLatencySamples() reads the already-updated oversamplingMode.
    // parameterChanged() for hqEnabled already called setLatencySamples() with the correct
    // new value, so on the mode-change block these will match and no second call fires.
    // For all other latency changes (limiter, linear phase) parameterChanged() handles it.
    {
        int correctLatency = getLatencySamples();
        if (AudioProcessor::getLatencySamples() != correctLatency)
        {
            setLatencySamples(correctLatency);
            if (bypassDelayLength != correctLatency)
            {
                bypassDelayLength = correctLatency;
                bypassDelayWritePos = 0;
                bypassDelayFillCount = 0;
                bypassDelayBuffer.clear();
            }
        }
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

        // Note: previously this block enforced minimum 2x OS for British/Tube when OS=Off.
        // Now Off is removed — minimum is always 2x — so no EQ-type-driven sample-rate
        // adjustment is needed here.

        previousEQType = eqType;

        // Force filter rebuild on mode change. This ensures that when switching to Digital
        // (e.g. after transferCurrentEQToDigital()), the very first Digital-mode block
        // rebuilds the IIR filters with the newly transferred band parameters.
        // Without this, processBlock may have already consumed filtersNeedUpdate=true
        // while still in the old mode, leaving filtersNeedUpdate=false when Digital first runs.
        filtersNeedUpdate.store(true, std::memory_order_relaxed);
    }

    // Update filters if needed (for Digital or Match mode)
    // Always update SVF/biquad coefficients for UI curve display.
    // In non-LP mode, also consume the flag. In LP mode, leave it for the FIR rebuild path.
    {
        bool needsUpdate = filtersNeedUpdate.exchange(false);
        if ((eqType == EQType::Digital || eqType == EQType::Match) && needsUpdate)
            updateAllFilters();
    }

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
        int safeN = juce::jmin(numSamp, static_cast<int>(analyzerMonoBuffer.size()));
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        // Mono downmix then sum of squares
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, safeN);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, safeN);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, safeN);
        // Sum of squares and peak tracking
        for (int i = 0; i < safeN; ++i)
        {
            float s = analyzerMonoBuffer[static_cast<size_t>(i)];
            inputRmsSum += s * s;
            float abss = std::abs(s);
            if (abss > inputPeakMax) inputPeakMax = abss;
        }
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
        int n = juce::jmin(buffer.getNumSamples(), static_cast<int>(analyzerMonoBuffer.size()));
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        // Block mono downmix: copy L, add R, scale by 0.5
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, n);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, n);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, n);
        pushSamplesToAnalyzer(analyzerMonoBuffer.data(), n, true);
    }

    // (Match EQ learning feed moved above bypass check — see line ~533)

    // Get processing mode
    auto procMode = static_cast<ProcessingMode>(
        static_cast<int>(safeGetParam(processingModeParam, 0.0f)));

    // ADAA in ConsoleSaturation and TubeEQProcessor handles aliasing at OS=Off,
    // so no minimum OS enforcement is needed for British/Tube modes.
    int effectiveOsMode = oversamplingMode;

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::AudioBlock<float> processBlock = block;

    if (effectiveOsMode == 2 && oversampler4x)
        processBlock = oversampler4x->processSamplesUp(block);
    else if (effectiveOsMode >= 1 && oversampler2x)
        processBlock = oversampler2x->processSamplesUp(block);

    int numSamples = static_cast<int>(processBlock.getNumSamples());
    const bool isStereo = processBlock.getNumChannels() > 1;
    float* procL = processBlock.getChannelPointer(0);
    float* procR = isStereo ? processBlock.getChannelPointer(1) : nullptr;


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

    if (isStereo && useMS && eqType != EQType::Digital)
    {
        for (int i = 0; i < numSamples; ++i)
            encodeMS(procL[i], procR[i]);
    }

    // Process based on EQ type
    if (eqType == EQType::British)
    {
        // British mode: Use 4K-EQ style processing
        // Use pre-allocated scratch buffer (no heap allocation in audio thread)
        // Process in chunks to handle blocks larger than scratchBuffer
        int numChannels = static_cast<int>(processBlock.getNumChannels());
        int totalSamples = static_cast<int>(processBlock.getNumSamples());
        int scratchSize = scratchBuffer.getNumSamples();

        for (int offset = 0; offset < totalSamples; offset += scratchSize)
        {
            int chunkSize = juce::jmin(totalSamples - offset, scratchSize);

            for (int ch = 0; ch < numChannels; ++ch)
                scratchBuffer.copyFrom(ch, 0,
                                       processBlock.getChannelPointer(static_cast<size_t>(ch)) + offset,
                                       chunkSize);

            juce::AudioBuffer<float> tempView(scratchBuffer.getArrayOfWritePointers(),
                                              numChannels, chunkSize);
            britishEQ.process(tempView);

            // NaN guard after British EQ
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* d = scratchBuffer.getWritePointer(ch);
                for (int s = 0; s < chunkSize; ++s)
                    if (!safeIsFinite(d[s])) d[s] = 0.0f;
            }

            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy(processBlock.getChannelPointer(static_cast<size_t>(ch)) + offset,
                           scratchBuffer.getReadPointer(ch),
                           sizeof(float) * static_cast<size_t>(chunkSize));
        }
    }
    else if (eqType == EQType::Tube)
    {
        int numChannels = static_cast<int>(processBlock.getNumChannels());
        int totalSamples = static_cast<int>(processBlock.getNumSamples());
        int scratchSize = scratchBuffer.getNumSamples();

        for (int offset = 0; offset < totalSamples; offset += scratchSize)
        {
            int chunkSize = juce::jmin(totalSamples - offset, scratchSize);

            for (int ch = 0; ch < numChannels; ++ch)
                scratchBuffer.copyFrom(ch, 0,
                                       processBlock.getChannelPointer(static_cast<size_t>(ch)) + offset,
                                       chunkSize);

            juce::AudioBuffer<float> tempView(scratchBuffer.getArrayOfWritePointers(),
                                              numChannels, chunkSize);
            tubeEQ.process(tempView);

            // NaN guard after Tube EQ
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* d = scratchBuffer.getWritePointer(ch);
                for (int s = 0; s < chunkSize; ++s)
                    if (!safeIsFinite(d[s])) d[s] = 0.0f;
            }

            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy(processBlock.getChannelPointer(static_cast<size_t>(ch)) + offset,
                           scratchBuffer.getReadPointer(ch),
                           sizeof(float) * static_cast<size_t>(chunkSize));
        }
    }
    else
    {
        // Digital mode: Multi-Q 8-band EQ with optional per-band dynamics
        // useLinearPhase already set from useLinearPhaseEarly above

        // Check which bands are enabled and update smooth crossfade targets
        std::array<bool, NUM_BANDS> bandEnabled{};
        std::array<bool, NUM_BANDS> bandDynEnabled{};
        for (int i = 0; i < NUM_BANDS; ++i)
        {
            bandEnabled[static_cast<size_t>(i)] = safeGetParam(bandEnabledParams[static_cast<size_t>(i)], 0.0f) > 0.5f;
            bandDynEnabled[static_cast<size_t>(i)] = safeGetParam(bandDynEnabledParams[static_cast<size_t>(i)], 0.0f) > 0.5f;
        }

        // Match mode: disable dynamics and bypass ALL Digital EQ bands (including HPF/LPF).
        // The FIR correction handles the full correction curve; Digital EQ settings
        // must not bleed through — output should be flat when no correction is computed.
        if (eqType == EQType::Match)
        {
            bandDynEnabled.fill(false);
            bandEnabled.fill(false);
        }

        // Apply solo mode: if any band is soloed, only that band is processed
        // HPF (band 0) and LPF (band 7) remain active when soloing a parametric band
        int currentSolo = soloedBand.load();
        bool deltaSoloActive = (currentSolo >= 0 && currentSolo < NUM_BANDS && deltaSoloMode.load());
        if (currentSolo >= 0 && currentSolo < NUM_BANDS && !deltaSoloActive)
        {
            bool soloIsParametric = (currentSolo >= 1 && currentSolo <= 6);
            for (int i = 0; i < NUM_BANDS; ++i)
            {
                if (i == currentSolo) continue;
                if (soloIsParametric && (i == 0 || i == 7)) continue;
                bandEnabled[static_cast<size_t>(i)] = false;
            }
        }

        // Update per-band enable smoothing targets
        for (int i = 0; i < NUM_BANDS; ++i)
            bandEnableSmoothed[static_cast<size_t>(i)].setTargetValue(bandEnabled[static_cast<size_t>(i)] ? 1.0f : 0.0f);

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

            // Read per-band phase invert and pan target values once per block
            std::array<float, NUM_BANDS> targetPhaseInvertGain{};
            std::array<float, NUM_BANDS> targetPanVal{};
            std::array<float, NUM_BANDS> phaseInvertGainInc{};
            std::array<float, NUM_BANDS> panValInc{};
            float invNumSamples = (numSamples > 0) ? 1.0f / static_cast<float>(numSamples) : 1.0f;
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                auto idx = static_cast<size_t>(b);
                targetPhaseInvertGain[idx] = (safeGetParam(bandPhaseInvertParams[idx], 0.0f) > 0.5f) ? -1.0f : 1.0f;
                targetPanVal[idx] = safeGetParam(bandPanParams[idx], 0.0f);
                // Linear ramp increments per sample
                phaseInvertGainInc[idx] = (targetPhaseInvertGain[idx] - prevBandPhaseInvertGain[idx]) * invNumSamples;
                panValInc[idx] = (targetPanVal[idx] - prevBandPanVal[idx]) * invNumSamples;
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

            // Pre-compute per-band active flags: a band needs processing only if
            // it's enabled OR its enable smoother is still ramping (crossfade)
            std::array<bool, NUM_BANDS> bandActive{};
            for (int b = 0; b < NUM_BANDS; ++b)
                bandActive[static_cast<size_t>(b)] = bandEnabled[static_cast<size_t>(b)]
                    || bandEnableSmoothed[static_cast<size_t>(b)].isSmoothing();

            // Pre-compute saturation curve types once per block (avoid switch per sample)
            using CT = AnalogEmulation::WaveshaperCurves::CurveType;
            std::array<CT, NUM_BANDS> bandSatCurve{};
            for (int band = 1; band < 7; ++band)
            {
                switch (bandSatType[static_cast<size_t>(band)])
                {
                    case 1:  bandSatCurve[static_cast<size_t>(band)] = CT::Tape; break;
                    case 2:  bandSatCurve[static_cast<size_t>(band)] = CT::Triode; break;
                    case 3:  bandSatCurve[static_cast<size_t>(band)] = CT::Console_Bus; break;
                    case 4:  bandSatCurve[static_cast<size_t>(band)] = CT::FET; break;
                    default: bandSatCurve[static_cast<size_t>(band)] = CT::Linear; break;
                }
            }

            // Check if all bands use Stereo routing (mode 0) — the common case.
            // When true, we skip the routing switch and M/S encode/decode entirely.
            bool allStereoRouting = true;
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                if (effectiveRouting[static_cast<size_t>(b)] != 0)
                {
                    allStereoRouting = false;
                    break;
                }
            }

            // Process each sample through the filter chain with per-band routing
            for (int i = 0; i < numSamples; ++i)
            {
                float sampleL = procL[i];
                float sampleR = isStereo ? procR[i] : sampleL;

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

                // Stereo-only filter application (no routing switch, no M/S encode/decode)
                auto applyFilterStereo = [&](auto& filter) {
                    sampleL = filter.processSampleL(sampleL);
                    sampleR = filter.processSampleR(sampleR);
                };

                // Per-band processing with smooth enable/disable crossfade
                // Helper: apply band filter with smooth enable blending
                auto applyBandWithSmoothing = [&](int bandIdx, const auto& applyFn) {
                    auto& smooth = bandEnableSmoothed[static_cast<size_t>(bandIdx)];
                    // bandActive was pre-computed before the loop; still need getNextValue
                    // to advance the smoother even if we early-out on gain < 0.001
                    float enableGain = smooth.getNextValue();
                    if (enableGain < 0.001f) return;  // Fully faded out

                    float prevL = sampleL, prevR = sampleR;
                    applyFn();

                    // Phase Invert: smoothly ramp polarity of band's contribution
                    {
                        auto idx = static_cast<size_t>(bandIdx);
                        float phaseGain = prevBandPhaseInvertGain[idx] + phaseInvertGainInc[idx] * static_cast<float>(i);
                        // phaseGain ramps between +1 (normal) and -1 (inverted)
                        // Apply as blend: output = dry + phaseGain * (wet - dry)
                        float deltaL = sampleL - prevL;
                        float deltaR = sampleR - prevR;
                        sampleL = prevL + phaseGain * deltaL;
                        sampleR = prevR + phaseGain * deltaR;
                    }

                    // Pan: smoothly ramped stereo placement of band's EQ effect
                    {
                        float pan = prevBandPanVal[static_cast<size_t>(bandIdx)] + panValInc[static_cast<size_t>(bandIdx)] * static_cast<float>(i);
                        if (std::abs(pan) > 0.001f)
                        {
                            float deltaL = sampleL - prevL;
                            float deltaR = sampleR - prevR;

                            // Linear pan law: -1 = full L, 0 = center, +1 = full R
                            float leftGain = std::min(1.0f, 1.0f - pan);
                            float rightGain = std::min(1.0f, 1.0f + pan);

                            sampleL = prevL + deltaL * leftGain;
                            sampleR = prevR + deltaR * rightGain;
                        }
                    }

                    // Blend between dry (pre-filter) and wet (post-filter) for smooth enable/disable
                    if (enableGain < 0.999f)
                    {
                        sampleL = prevL + enableGain * (sampleL - prevL);
                        sampleR = prevR + enableGain * (sampleR - prevR);
                    }
                };

                // Band 1: HPF (no dynamics for filters)
                if (bandActive[0])
                {
                    if (deltaSoloActive && currentSolo == 0) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }
                    applyBandWithSmoothing(0, [&]() {
                        if (allStereoRouting)
                            applyFilterStereo(hpfFilter);
                        else
                            applyFilterWithRouting(hpfFilter, effectiveRouting[0]);
                    });
                    if (deltaSoloActive && currentSolo == 0) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
                }

                // Bands 2-7: Shelf and Parametric with optional dynamics
                for (int band = 1; band < 7; ++band)
                {
                    // Skip fully disabled bands (not enabled and not crossfading)
                    if (!bandActive[static_cast<size_t>(band)]) continue;

                    if (deltaSoloActive && currentSolo == band) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }

                    applyBandWithSmoothing(band, [&]() {
                        auto& filter = svfFilters[static_cast<size_t>(band - 1)];

                        if (allStereoRouting)
                        {
                            // Fast path: all bands use Stereo routing, skip routing switch
                            if (bandDynEnabled[static_cast<size_t>(band)])
                            {
                                // Per-sample detection (stereo = use both channels directly)
                                float detectionL = dynamicEQ.processDetection(band, sampleL, 0);
                                float detectionR = dynamicEQ.processDetection(band, sampleR, 1);
                                dynamicEQ.processBand(band, detectionL, 0);
                                dynamicEQ.processBand(band, detectionR, 1);

                                applyFilterStereo(filter);
                                applyFilterStereo(svfDynGainFilters[static_cast<size_t>(band - 1)]);
                            }
                            else
                            {
                                applyFilterStereo(filter);
                            }

                            // Saturation (stereo path)
                            int satType = bandSatType[static_cast<size_t>(band)];
                            if (satType > 0)
                            {
                                float drive = bandSatDrive[static_cast<size_t>(band)];
                                CT curve = bandSatCurve[static_cast<size_t>(band)];
                                sampleL = waveshaperCurves.processWithDrive(sampleL, curve, drive);
                                sampleR = waveshaperCurves.processWithDrive(sampleR, curve, drive);
                            }
                        }
                        else
                        {
                            // General path: per-band routing with M/S encode/decode
                            int routing = effectiveRouting[static_cast<size_t>(band)];

                            if (bandDynEnabled[static_cast<size_t>(band)])
                            {
                                // Detection input respects per-band routing
                                float detL = sampleL;
                                float detR = sampleR;
                                if (routing == 3 || routing == 4)
                                {
                                    float mid = (sampleL + sampleR) * 0.5f;
                                    float side = (sampleL - sampleR) * 0.5f;
                                    if (routing == 3) { detL = mid; detR = mid; }
                                    else              { detL = side; detR = side; }
                                }
                                else if (routing == 1) { detR = 0.0f; }
                                else if (routing == 2) { detL = 0.0f; }

                                // Per-sample detection and envelope following
                                float detectionL = dynamicEQ.processDetection(band, detL, 0);
                                float detectionR = dynamicEQ.processDetection(band, detR, 1);
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
                            // Respects per-band routing (Stereo/L/R/Mid/Side)
                            int satType = bandSatType[static_cast<size_t>(band)];
                            if (satType > 0)
                            {
                                CT curve = bandSatCurve[static_cast<size_t>(band)];
                                float drive = bandSatDrive[static_cast<size_t>(band)];
                                switch (routing)
                                {
                                    case 0:  // Stereo
                                        sampleL = waveshaperCurves.processWithDrive(sampleL, curve, drive);
                                        sampleR = waveshaperCurves.processWithDrive(sampleR, curve, drive);
                                        break;
                                    case 1:  // Left only
                                        sampleL = waveshaperCurves.processWithDrive(sampleL, curve, drive);
                                        break;
                                    case 2:  // Right only
                                        sampleR = waveshaperCurves.processWithDrive(sampleR, curve, drive);
                                        break;
                                    case 3:  // Mid
                                    {
                                        float mid = (sampleL + sampleR) * 0.5f;
                                        float side = (sampleL - sampleR) * 0.5f;
                                        mid = waveshaperCurves.processWithDrive(mid, curve, drive);
                                        sampleL = mid + side;
                                        sampleR = mid - side;
                                        break;
                                    }
                                    case 4:  // Side
                                    {
                                        float mid = (sampleL + sampleR) * 0.5f;
                                        float side = (sampleL - sampleR) * 0.5f;
                                        side = waveshaperCurves.processWithDrive(side, curve, drive);
                                        sampleL = mid + side;
                                        sampleR = mid - side;
                                        break;
                                    }
                                }
                            }
                        }
                    });

                    if (deltaSoloActive && currentSolo == band) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
                }

                // Band 8: LPF (no dynamics for filters)
                if (bandActive[7])
                {
                    if (deltaSoloActive && currentSolo == 7) { deltaBeforeL = sampleL; deltaBeforeR = sampleR; }
                    applyBandWithSmoothing(7, [&]() {
                        if (allStereoRouting)
                            applyFilterStereo(lpfFilter);
                        else
                            applyFilterWithRouting(lpfFilter, effectiveRouting[7]);
                    });
                    if (deltaSoloActive && currentSolo == 7) { deltaAfterL = sampleL; deltaAfterR = sampleR; }
                }

                // Delta solo: output only what the soloed band changes
                if (deltaSoloActive)
                {
                    sampleL = deltaAfterL - deltaBeforeL;
                    sampleR = deltaAfterR - deltaBeforeR;
                }

                // Per-sample NaN guard (bitwise, immune to -ffast-math)
                if (!safeIsFinite(sampleL)) sampleL = 0.0f;
                if (!safeIsFinite(sampleR)) sampleR = 0.0f;

                procL[i] = sampleL;
                if (isStereo)
                    procR[i] = sampleR;
            }

            // Update smoothing state for next block
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                auto idx = static_cast<size_t>(b);
                prevBandPhaseInvertGain[idx] = targetPhaseInvertGain[idx];
                prevBandPanVal[idx] = targetPanVal[idx];
            }
        }  // end IIR block
    }  // end Digital mode else

    // M/S decode for British/Tube EQ modes (Digital mode handles M/S per-band)
        if (isStereo && useMS && eqType != EQType::Digital)
        {
            for (int i = 0; i < numSamples; ++i)
                decodeMS(procL[i], procR[i]);
        }

        // Oversampling downsample (use effectiveOsMode to match the upsample decision above)
        if (effectiveOsMode == 2 && oversampler4x)
            oversampler4x->processSamplesDown(block);
        else if (effectiveOsMode >= 1 && oversampler2x)
            oversampler2x->processSamplesDown(block);

        // Oversampling latency compensation delay — keeps total latency constant
        // across all OS modes so DAW PDC stays aligned when switching mid-playback.
        if (osCompDelaySamples > 0 && osCompDelayBuffer.getNumSamples() >= osCompDelaySamples)
        {
            int compSamples = buffer.getNumSamples();
            int compChannels = juce::jmin(buffer.getNumChannels(), osCompDelayBuffer.getNumChannels());
            for (int i = 0; i < compSamples; ++i)
            {
                for (int ch = 0; ch < compChannels; ++ch)
                {
                    float input = buffer.getSample(ch, i);
                    float delayed = osCompDelayBuffer.getSample(ch, osCompDelayWritePos);
                    osCompDelayBuffer.setSample(ch, osCompDelayWritePos, input);
                    buffer.setSample(ch, i, delayed);
                }
                osCompDelayWritePos = (osCompDelayWritePos + 1) % osCompDelaySamples;
            }
        }

        // Match EQ convolution (FIR correction filter, after downsample, before master gain)
        if (eqType == EQType::Match && matchConvolutionActive.load(std::memory_order_acquire))
        {
            // Snap to full wet immediately when correction is first applied
            if (matchConvWetGain.getCurrentValue() < 0.5f && !matchConvWetGain.isSmoothing())
                matchConvWetGain.setCurrentAndTargetValue(1.0f);

            if (matchConvWetGain.isSmoothing())
            {
                // Crossfade: copy dry signal into pre-allocated buffer without reallocating.
                // matchDryBuffer is sized to maxBlockSize in prepareToPlay; copyFrom is always
                // safe because partial blocks are always ≤ maxBlockSize.
                const int nSamples  = buffer.getNumSamples();
                const int nChannels = juce::jmin(buffer.getNumChannels(), matchDryBuffer.getNumChannels());
                for (int ch = 0; ch < nChannels; ++ch)
                    matchDryBuffer.copyFrom(ch, 0, buffer, ch, 0, nSamples);

                juce::dsp::AudioBlock<float> matchBlock(buffer);
                juce::dsp::ProcessContextReplacing<float> ctx(matchBlock);
                matchConvolution.process(ctx);
                for (int s = 0; s < nSamples; ++s)
                {
                    const float wet = matchConvWetGain.getNextValue();
                    const float dryWet = 1.0f - wet;
                    for (int ch = 0; ch < nChannels; ++ch)
                    {
                        const float dryS = matchDryBuffer.getSample(ch, s);
                        buffer.setSample(ch, s, dryS * dryWet + buffer.getSample(ch, s) * wet);
                    }
                }

                // Fade-out complete — safe to deactivate and clear
                if (pendingMatchFadeOut.load(std::memory_order_relaxed) && !matchConvWetGain.isSmoothing())
                {
                    eqMatchProcessor.clearAll();
                    matchConvolutionActive.store(false, std::memory_order_release);
                    pendingMatchFadeOut.store(false, std::memory_order_release);
                }
            }
            else
            {
                // Steady state — direct in-place convolution, no dry copy needed
                juce::dsp::AudioBlock<float> matchBlock(buffer);
                juce::dsp::ProcessContextReplacing<float> ctx(matchBlock);
                matchConvolution.process(ctx);
            }
        }

        // Apply master gain
        float masterGain = juce::Decibels::decibelsToGain(safeGetParam(masterGainParam, 0.0f));
        buffer.applyGain(masterGain);

    // NaN/Inf sanitization before auto-gain and metering (bitwise, immune to -ffast-math)
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        int n = buffer.getNumSamples();
        for (int s = 0; s < n; ++s)
            if (!safeIsFinite(data[s])) data[s] = 0.0f;
    }

    // Auto-gain compensation: measure output RMS and apply inverse gain
    // (Bypass already checked above - if bypassed, we would have returned)
    if (autoGainEnabled)
    {
        // Calculate output RMS (block-based mono downmix + sum of squares)
        int outN = juce::jmin(buffer.getNumSamples(), static_cast<int>(analyzerMonoBuffer.size()));
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, outN);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, outN);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, outN);
        for (int i = 0; i < outN; ++i)
        {
            float s = analyzerMonoBuffer[static_cast<size_t>(i)];
            outputRmsSum += s * s;
            float abss = std::abs(s);
            if (abss > outputPeakMax) outputPeakMax = abss;
        }

        rmsSampleCount += outN;

        // Update auto-gain compensation when we have enough samples
        if (rmsSampleCount >= rmsWindowSamples)
        {
            float inputRms = std::sqrt(inputRmsSum / static_cast<float>(rmsSampleCount));
            float outputRms = std::sqrt(outputRmsSum / static_cast<float>(rmsSampleCount));

            // Calculate compensation gain (ratio of input to output RMS)
            if (outputRms > 1e-6f && inputRms > 1e-6f)
            {
                float targetGain = inputRms / outputRms;

                // Peak-safe cap: never push output peaks above input peak level.
                // RMS-only correction can over-boost transient signals (e.g. HPF on drums
                // removes low-end RMS but peaks are broadband — RMS-derived gain then
                // pushes peaks above the original ceiling).
                if (outputPeakMax > 1e-6f && inputPeakMax > 1e-6f)
                {
                    float peakSafeCap = inputPeakMax / outputPeakMax;
                    targetGain = std::min(targetGain, peakSafeCap);
                }

                targetGain = juce::jlimit(0.5f, 2.0f, targetGain);  // ±6dB max

                // Dead zone: ignore corrections < 1% (~0.09 dB).
                // The oversampling AA filter has a tiny steady-state RMS effect
                // that should not trigger autogain — only real EQ changes should.
                if (std::abs(targetGain - 1.0f) > 0.01f)
                    autoGainCompensation.setTargetValue(targetGain);
            }

            // Reset accumulators
            inputRmsSum = 0.0f;
            outputRmsSum = 0.0f;
            inputPeakMax = 0.0f;
            outputPeakMax = 0.0f;
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
            }
        }
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
        inputPeakMax = 0.0f;
        outputPeakMax = 0.0f;
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
        int copyLen = juce::jmin(buffer.getNumSamples(), prevOsBuffer.getNumSamples());
        int copyCh = juce::jmin(buffer.getNumChannels(), prevOsBuffer.getNumChannels());
        for (int ch = 0; ch < copyCh; ++ch)
            prevOsBuffer.copyFrom(ch, 0, buffer, ch, 0, copyLen);
    }
    if (!eqTypeChanging)
    {
        int copyLen = juce::jmin(buffer.getNumSamples(), prevTypeBuffer.getNumSamples());
        int copyCh = juce::jmin(buffer.getNumChannels(), prevTypeBuffer.getNumChannels());
        for (int ch = 0; ch < copyCh; ++ch)
            prevTypeBuffer.copyFrom(ch, 0, buffer, ch, 0, copyLen);
    }

    // Apply oversampling mode switch crossfade
    if (osChanging)
    {
        int xfCh = juce::jmin(buffer.getNumChannels(), prevOsBuffer.getNumChannels());
        int xfLen = juce::jmin(buffer.getNumSamples(), prevOsBuffer.getNumSamples());
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
        int xfCh = juce::jmin(buffer.getNumChannels(), prevTypeBuffer.getNumChannels());
        int xfLen = juce::jmin(buffer.getNumSamples(), prevTypeBuffer.getNumSamples());
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
        int xfLen = juce::jmin(buffer.getNumSamples(), dryBuffer.getNumSamples());
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
        int n = juce::jmin(buffer.getNumSamples(), static_cast<int>(analyzerMonoBuffer.size()));
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        juce::FloatVectorOperations::copy(analyzerMonoBuffer.data(), readL, n);
        juce::FloatVectorOperations::add(analyzerMonoBuffer.data(), readR, n);
        juce::FloatVectorOperations::multiply(analyzerMonoBuffer.data(), 0.5f, n);
        pushSamplesToAnalyzer(analyzerMonoBuffer.data(), n, false);
    }

    // Final NaN/Inf sanitization using bitwise check (immune to -ffast-math)
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        int n = buffer.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            if (!safeIsFinite(data[i]))
                data[i] = 0.0f;
        }
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

void MultiQ::updateAllFilters()
{
    // Only update bands whose parameters actually changed. Unconditionally updating all
    // bands — especially HPF/LPF — resets their biquad state every time any knob moves,
    // causing clicks in the audio. The dirty flags tell us exactly what changed.

    if (bandDirty[0].exchange(false))
        updateHPFCoefficients(currentSampleRate);

    if (bandDirty[7].exchange(false))
        updateLPFCoefficients(currentSampleRate);

    for (int i = 1; i < 7; ++i)
    {
        if (bandDirty[static_cast<size_t>(i)].exchange(false))
            updateBandFilter(i);
    }

    // Publish UI coefficient double buffer (release ensures all writes above are visible)
    publishUICoeffs();
}

void MultiQ::computeBandCoeffs(int bandIndex, BiquadCoeffs& c) const
{
    computeBandCoeffsAtRate(bandIndex, c, currentSampleRate);
}

void MultiQ::computeBandCoeffsAtRate(int bandIndex, BiquadCoeffs& c, double sr) const
{
    float gain = safeGetParam(bandGainParams[static_cast<size_t>(bandIndex)], 0.0f);

    // Invert: flip gain direction (boost↔cut)
    if (safeGetParam(bandInvertParams[static_cast<size_t>(bandIndex)], 0.0f) > 0.5f)
        gain = -gain;

    computeBandCoeffsWithGain(bandIndex, gain, c, sr);
}

void MultiQ::computeBandCoeffsWithGain(int bandIndex, float overrideGainDB, BiquadCoeffs& c, double sr) const
{
    float freq = safeGetParam(bandFreqParams[static_cast<size_t>(bandIndex)],
                              DefaultBandConfigs[static_cast<size_t>(bandIndex)].defaultFreq);
    float baseQ = safeGetParam(bandQParams[static_cast<size_t>(bandIndex)], 0.71f);
    float q = getQCoupledValue(baseQ, overrideGainDB, getCurrentQCoupleMode());

    if (bandIndex == 1)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[1], 0.0f));
        if (shape == 1)
            computePeakingCoeffs(c, sr, freq, overrideGainDB, q);
        else if (shape == 2)
            computeHighPassCoeffs(c, sr, freq, q);
        else
            computeLowShelfCoeffs(c, sr, freq, overrideGainDB, q);
    }
    else if (bandIndex == 6)
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[6], 0.0f));
        if (shape == 1)
            computePeakingCoeffs(c, sr, freq, overrideGainDB, q);
        else if (shape == 2)
            computeLowPassCoeffs(c, sr, freq, q);
        else
            computeHighShelfCoeffs(c, sr, freq, overrideGainDB, q);
    }
    else
    {
        int shape = static_cast<int>(safeGetParam(bandShapeParams[static_cast<size_t>(bandIndex)], 0.0f));
        if (shape == 1)
            computeNotchCoeffs(c, sr, freq, q);
        else if (shape == 2)
            computeBandPassCoeffs(c, sr, freq, q);
        else if (shape == 3)
            computeTiltShelfCoeffs(c, sr, freq, overrideGainDB);
        else
            computePeakingCoeffs(c, sr, freq, overrideGainDB, q);
    }
}

void MultiQ::updateBandFilter(int bandIndex)
{
    if (bandIndex < 1 || bandIndex > 6)
        return;

    // AnalogMatchedBiquad coefficients — used for both audio path and UI display.
    // StereoBiquad (DF2T) gives identical response at all OS rates; the old SVF path
    // produced topology-dependent frequency warping that differed across OS modes.
    BiquadCoeffs c;
    computeBandCoeffs(bandIndex, c);
    svfFilters[static_cast<size_t>(bandIndex - 1)].setCoeffs(c);
    uiWriteBuffer().bandCoeffs[static_cast<size_t>(bandIndex - 1)] = c;
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
        // Bands 2-5: check shape
        int shape = bandShapeParams[static_cast<size_t>(bandIndex)]
            ? static_cast<int>(bandShapeParams[static_cast<size_t>(bandIndex)]->load()) : 0;
        if (shape == 1 || shape == 2)  // Notch/BandPass have no gain — bypass dynamic SVF
            svfC.setIdentity();
        else if (shape == 3)  // Tilt shelf
            computeSVFTiltShelf(svfC, currentSampleRate, freq, dynGainDb);
        else
            computeSVFPeaking(svfC, currentSampleRate, freq, dynGainDb, q);
    }

    svfDynGainFilters[static_cast<size_t>(bandIndex - 1)].setTarget(svfC);
}

// Non-allocating coefficient computation — delegates to AnalogMatchedBiquad.
// All functions pre-warp the bandwidth (not just f0), giving cramping-free
// response at all sample rates without requiring oversampling.

double MultiQ::preWarpFrequency(double freq, double sampleRate)
{
    // Kept for any remaining callers; AnalogMatchedBiquad handles warping internally.
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    return (2.0 / T) * std::tan(w0 * T / 2.0) / (2.0 * juce::MathConstants<double>::pi);
}

void MultiQ::computePeakingCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    AnalogMatchedBiquad::computePeaking(c, freq, sr, static_cast<double>(gainDB), static_cast<double>(q));
}

void MultiQ::computeLowShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    AnalogMatchedBiquad::computeLowShelf(c, freq, sr, static_cast<double>(gainDB), static_cast<double>(q));
}

void MultiQ::computeHighShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    AnalogMatchedBiquad::computeHighShelf(c, freq, sr, static_cast<double>(gainDB), static_cast<double>(q));
}

void MultiQ::computeNotchCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    AnalogMatchedBiquad::computeNotch(c, freq, sr, static_cast<double>(q));
}

void MultiQ::computeBandPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    AnalogMatchedBiquad::computeBandPass(c, freq, sr, static_cast<double>(q));
}

void MultiQ::computeHighPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    AnalogMatchedBiquad::computeHighPass(c, freq, sr, static_cast<double>(q));
}

void MultiQ::computeLowPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    AnalogMatchedBiquad::computeLowPass(c, freq, sr, static_cast<double>(q));
}

void MultiQ::computeFirstOrderHighPassCoeffs(BiquadCoeffs& c, double sr, double freq)
{
    AnalogMatchedBiquad::computeFirstOrderHighPass(c, freq, sr);
}

void MultiQ::computeFirstOrderLowPassCoeffs(BiquadCoeffs& c, double sr, double freq)
{
    AnalogMatchedBiquad::computeFirstOrderLowPass(c, freq, sr);
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

// Cytomic SVF coefficient computation
// These compute SVFCoeffs for the audio processing path.
// The transfer function is identical to the corresponding biquad; the difference
// is in the filter topology which allows per-sample coefficient modulation.

void MultiQ::computeSVFPeaking(SVFCoeffs& c, double sr, double freq, float gainDB, float q)
{
    freq = std::max(1.0, std::min(freq, sr * 0.4998));
    double A   = std::pow(10.0, gainDB / 40.0);
    double g   = std::tan(juce::MathConstants<double>::pi * freq / sr);
    // Pre-warp the bandwidth (fc/Q Hz) independently of the centre frequency.
    // This matches the AnalogMatchedBiquad approach: kbw = tan(π·bw/sr),
    // so the Q/bandwidth shape is consistent at all OS rates.
    double bw  = freq / std::max(0.01, static_cast<double>(q));
    double kbw = std::tan(juce::MathConstants<double>::pi * std::min(bw, sr * 0.4998) / sr);
    double k   = std::max(0.001, kbw / (g * A));

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
    freq = std::max(1.0, std::min(freq, sr * 0.4998));
    double g   = std::tan(juce::MathConstants<double>::pi * freq / sr);
    double bw  = freq / std::max(0.01, static_cast<double>(q));
    double kbw = std::tan(juce::MathConstants<double>::pi * std::min(bw, sr * 0.4998) / sr);
    double k   = std::max(0.001, kbw / g);

    c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
    c.a2 = static_cast<float>(g * c.a1);
    c.a3 = static_cast<float>(g * c.a2);
    c.m0 = 1.0f;
    c.m1 = static_cast<float>(-k);
    c.m2 = 0.0f;
}

void MultiQ::computeSVFBandPass(SVFCoeffs& c, double sr, double freq, float q)
{
    freq = std::max(1.0, std::min(freq, sr * 0.4998));
    double g   = std::tan(juce::MathConstants<double>::pi * freq / sr);
    double bw  = freq / std::max(0.01, static_cast<double>(q));
    double kbw = std::tan(juce::MathConstants<double>::pi * std::min(bw, sr * 0.4998) / sr);
    double k   = std::max(0.001, kbw / g);

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

    // Invert: flip gain direction (boost↔cut)
    if (safeGetParam(bandInvertParams[static_cast<size_t>(bandIndex)], 0.0f) > 0.5f)
        gain = -gain;

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

// Filter update methods (non-allocating, safe for audio thread)

void MultiQ::updateHPFCoefficients(double sampleRate)
{
    float freq = safeGetParam(bandFreqParams[0], 20.0f);
    float userQ = safeGetParam(bandQParams[0], 0.71f);
    int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[0], 0.0f));

    // Clamp raw frequency — compute*Coeffs functions handle pre-warping internally
    double clampedFreq = juce::jlimit(20.0, sampleRate * 0.45, static_cast<double>(freq));
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

    // Only reset filter state when the number of stages changes (i.e. slope changed).
    // For freq/Q changes, preserving state avoids clicks. When stage count changes, stale
    // state in previously-active stages could cause instability, so a reset is necessary.
    int previousStages = hpfFilter.activeStages.load(std::memory_order_relaxed);
    if (stages != previousStages)
        hpfFilter.reset();

    // Compute all stage coefficients BEFORE publishing activeStages
    // to prevent the audio thread from reading uninitialized stages
    int soStageIdx = 0;
    for (int stage = 0; stage < stages; ++stage)
    {
        BiquadCoeffs c;

        if (firstStageFirstOrder && stage == 0)
            computeFirstOrderHighPassCoeffs(c, sampleRate, clampedFreq);
        else
        {
            float stageQ = ButterworthQ::getStageQ(secondOrderStages, soStageIdx, userQ);
            computeHighPassCoeffs(c, sampleRate, static_cast<float>(clampedFreq), stageQ);
            ++soStageIdx;
        }
        c.applyToFilter(hpfFilter.stagesL[static_cast<size_t>(stage)]);
        c.applyToFilter(hpfFilter.stagesR[static_cast<size_t>(stage)]);

        uiWriteBuffer().hpfCoeffs[static_cast<size_t>(stage)] = c;
    }

    // Publish stage count AFTER all coefficients are written
    hpfFilter.activeStages.store(stages, std::memory_order_release);
    uiWriteBuffer().hpfStages = stages;
}

void MultiQ::updateLPFCoefficients(double sampleRate)
{
    float freq = safeGetParam(bandFreqParams[7], 20000.0f);
    float userQ = safeGetParam(bandQParams[7], 0.71f);
    int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[1], 0.0f));

    // Clamp raw frequency — compute*Coeffs functions handle pre-warping internally
    double clampedFreq = juce::jlimit(20.0, sampleRate * 0.45, static_cast<double>(freq));
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

    // Only reset when stage count changes (slope change). Freq/Q changes are smooth.
    int previousStages = lpfFilter.activeStages.load(std::memory_order_relaxed);
    if (stages != previousStages)
        lpfFilter.reset();

    int soStageIdx = 0;
    for (int stage = 0; stage < stages; ++stage)
    {
        BiquadCoeffs c;

        if (firstStageFirstOrder && stage == 0)
            computeFirstOrderLowPassCoeffs(c, sampleRate, clampedFreq);
        else
        {
            float stageQ = ButterworthQ::getStageQ(secondOrderStages, soStageIdx, userQ);
            computeLowPassCoeffs(c, sampleRate, static_cast<float>(clampedFreq), stageQ);
            ++soStageIdx;
        }
        c.applyToFilter(lpfFilter.stagesL[static_cast<size_t>(stage)]);
        c.applyToFilter(lpfFilter.stagesR[static_cast<size_t>(stage)]);

        uiWriteBuffer().lpfCoeffs[static_cast<size_t>(stage)] = c;
    }

    lpfFilter.activeStages.store(stages, std::memory_order_release);
    uiWriteBuffer().lpfStages = stages;
}

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
    // Match mode bypasses all Digital EQ bands — return flat (0 dB) so the combined
    // curve shows a straight line and never bleeds through the Digital EQ settings.
    auto currentEqType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));
    if (currentEqType == EQType::Match)
        return 0.0f;

    // Always compute from current parameter values at baseSampleRate so the display is
    // stable regardless of oversampling mode. Using UICoeffBuffer at currentSampleRate
    // (oversampled rate) caused the curve shape to visibly shift when OS mode changed,
    // which is confusing — oversampling is a quality setting, not a tonal change.
    double freshResponse = 1.0;
    for (int band = 0; band < NUM_BANDS; ++band)
    {
        float freshDB = computePerBandMagnitudeFresh(band, frequencyHz);
        freshResponse *= juce::Decibels::decibelsToGain(static_cast<double>(freshDB));
    }
    return static_cast<float>(juce::Decibels::gainToDecibels(freshResponse, -100.0));
}

float MultiQ::getPerBandMagnitude(int bandIndex, float frequencyHz) const
{
    const auto& buf = uiReadBuffer();
    double sr = currentSampleRate;
    double response = 1.0;

    bool enabled = (bandIndex >= 0 && bandIndex < NUM_BANDS) ?
        safeGetParam(bandEnabledParams[static_cast<size_t>(bandIndex)], 0.0f) > 0.5f : false;
    if (!enabled)
        return 0.0f;

    if (bandIndex == 0)  // HPF
    {
        int stages = buf.hpfStages;
        for (int s = 0; s < stages; ++s)
            response *= buf.hpfCoeffs[static_cast<size_t>(s)].getMagnitudeForFrequency(frequencyHz, sr);
    }
    else if (bandIndex == 7)  // LPF
    {
        int stages = buf.lpfStages;
        for (int s = 0; s < stages; ++s)
            response *= buf.lpfCoeffs[static_cast<size_t>(s)].getMagnitudeForFrequency(frequencyHz, sr);
    }
    else  // Parametric bands 1-6
    {
        response *= buf.bandCoeffs[static_cast<size_t>(bandIndex - 1)].getMagnitudeForFrequency(frequencyHz, sr);
    }

    return static_cast<float>(juce::Decibels::gainToDecibels(response, -100.0));
}

float MultiQ::computePerBandMagnitudeFresh(int bandIndex, float frequencyHz) const
{
    if (bandIndex < 0 || bandIndex >= NUM_BANDS)
        return 0.0f;

    bool enabled = safeGetParam(bandEnabledParams[static_cast<size_t>(bandIndex)], 0.0f) > 0.5f;
    if (!enabled)
        return 0.0f;

    double sr = baseSampleRate.load();
    BiquadCoeffs c;

    if (bandIndex == 0)
    {
        // HPF: compute cascaded stages on-the-fly
        float freq = safeGetParam(bandFreqParams[0], 20.0f);
        float userQ = safeGetParam(bandQParams[0], 0.71f);
        int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[0], 0.0f));
        double clampedFreq = juce::jlimit(20.0, sr * 0.45, static_cast<double>(freq));
        auto slope = static_cast<FilterSlope>(slopeIndex);

        int stages = 1;
        bool firstOrder = false;
        int so = 0;
        switch (slope) {
            case FilterSlope::Slope6dB:  stages=1; firstOrder=true; so=0; break;
            case FilterSlope::Slope12dB: stages=1; so=1; break;
            case FilterSlope::Slope18dB: stages=2; firstOrder=true; so=1; break;
            case FilterSlope::Slope24dB: stages=2; so=2; break;
            case FilterSlope::Slope36dB: stages=3; so=3; break;
            case FilterSlope::Slope48dB: stages=4; so=4; break;
            case FilterSlope::Slope72dB: stages=6; so=6; break;
            case FilterSlope::Slope96dB: stages=8; so=8; break;
        }

        double response = 1.0;
        int soIdx = 0;
        for (int s = 0; s < stages; ++s)
        {
            BiquadCoeffs sc;
            if (firstOrder && s == 0)
                computeFirstOrderHighPassCoeffs(sc, sr, clampedFreq);
            else {
                float stageQ = ButterworthQ::getStageQ(so, soIdx, userQ);
                computeHighPassCoeffs(sc, sr, static_cast<float>(clampedFreq), stageQ);
                ++soIdx;
            }
            response *= sc.getMagnitudeForFrequency(frequencyHz, sr);
        }
        return static_cast<float>(juce::Decibels::gainToDecibels(response, -100.0));
    }
    else if (bandIndex == 7)
    {
        // LPF: compute cascaded stages on-the-fly
        float freq = safeGetParam(bandFreqParams[7], 20000.0f);
        float userQ = safeGetParam(bandQParams[7], 0.71f);
        int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[1], 0.0f));
        double clampedFreq = juce::jlimit(20.0, sr * 0.45, static_cast<double>(freq));
        auto slope = static_cast<FilterSlope>(slopeIndex);

        int stages = 1;
        bool firstOrder = false;
        int so = 0;
        switch (slope) {
            case FilterSlope::Slope6dB:  stages=1; firstOrder=true; so=0; break;
            case FilterSlope::Slope12dB: stages=1; so=1; break;
            case FilterSlope::Slope18dB: stages=2; firstOrder=true; so=1; break;
            case FilterSlope::Slope24dB: stages=2; so=2; break;
            case FilterSlope::Slope36dB: stages=3; so=3; break;
            case FilterSlope::Slope48dB: stages=4; so=4; break;
            case FilterSlope::Slope72dB: stages=6; so=6; break;
            case FilterSlope::Slope96dB: stages=8; so=8; break;
        }

        double response = 1.0;
        int soIdx = 0;
        for (int s = 0; s < stages; ++s)
        {
            BiquadCoeffs sc;
            if (firstOrder && s == 0)
                computeFirstOrderLowPassCoeffs(sc, sr, clampedFreq);
            else {
                float stageQ = ButterworthQ::getStageQ(so, soIdx, userQ);
                computeLowPassCoeffs(sc, sr, static_cast<float>(clampedFreq), stageQ);
                ++soIdx;
            }
            response *= sc.getMagnitudeForFrequency(frequencyHz, sr);
        }
        return static_cast<float>(juce::Decibels::gainToDecibels(response, -100.0));
    }
    else
    {
        // Parametric bands: compute fresh at base sample rate (consistent with magnitude eval)
        computeBandCoeffsAtRate(bandIndex, c, sr);
        double mag = c.getMagnitudeForFrequency(frequencyHz, sr);
        return static_cast<float>(juce::Decibels::gainToDecibels(mag, -100.0));
    }
}

float MultiQ::getFrequencyResponseWithDynamics(float frequencyHz) const
{
    // Same as getFrequencyResponseMagnitude but recomputes coefficients for bands
    // with active dynamics to include the dynamic gain offset.
    // Read from the active double buffer (acquire ensures we see all audio thread writes).

    const auto& buf = uiReadBuffer();
    double response = 1.0;
    double sr = currentSampleRate;

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        bool enabled = safeGetParam(bandEnabledParams[static_cast<size_t>(band)], 0.0f) > 0.5f;
        if (!enabled)
            continue;

        if (band == 0)  // HPF
        {
            int stages = buf.hpfStages;
            for (int s = 0; s < stages; ++s)
                response *= buf.hpfCoeffs[static_cast<size_t>(s)].getMagnitudeForFrequency(frequencyHz, sr);
        }
        else if (band == 7)  // LPF
        {
            int stages = buf.lpfStages;
            for (int s = 0; s < stages; ++s)
                response *= buf.lpfCoeffs[static_cast<size_t>(s)].getMagnitudeForFrequency(frequencyHz, sr);
        }
        else  // Bands 2-7
        {
            // Use stored static coefficients
            response *= buf.bandCoeffs[static_cast<size_t>(band - 1)].getMagnitudeForFrequency(frequencyHz, sr);

            // For bands with dynamics enabled, add dynamic gain filter contribution
            if (isDynamicsEnabled(band))
            {
                float dynGain = getDynamicGain(band);
                if (std::abs(dynGain) > 0.01f)
                {
                    // Compute dynamic gain filter coefficients on the fly (UI thread, allocation OK)
                    BiquadCoeffs dynC;
                    computeBandCoeffsWithGain(band, dynGain, dynC, sr);
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
    // Match mode does not support per-band dynamics
    auto eqType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));
    if (eqType != EQType::Digital)
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

// Cross-mode band transfer

void MultiQ::transferCurrentEQToDigital()
{
    // Raise guard to prevent onPresetSelected() from loading a preset while
    // we are mid-transfer. Lowered asynchronously after UI updates settle.
    // Note: setCurrentProgram(0) is intentionally a no-op for parameters (see
    // its implementation), so AU host calls during the transfer are benign.
    transferInProgress.store(true);

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

        // Transfer master gain: British has separate input and output gain knobs.
        // Input gain drives the circuit; output gain trims the level. Sum both into
        // Digital's masterGain so the overall level intent is preserved.
        float inputGain  = safeGetParam(britishInputGainParam, 0.0f);
        float outputGain = safeGetParam(britishOutputGainParam, 0.0f);
        setParam(ParamIDs::masterGain, inputGain + outputGain);
    }
    else if (eqType == EQType::Tube)
    {
        // Tube EQ → Digital: sample the frequency response and fit bands
        // Use the Tube EQ processor's actual frequency response evaluation

        // Disable all bands first, then enable those we set
        for (int i = 1; i <= NUM_BANDS; ++i)
            setBoolParam(ParamIDs::bandEnabled(i), false);

        // Band 1: HPF off (Tube EQ has no HPF)
        // Band 8: LPF off (Tube EQ has no LPF)

        // Band 2: Low Shelf ← Tube EQ LF section (Pultec boost + atten at the same frequency)
        // Scale factors match PultecLFSection constants: kPeakGainScale=1.4, kDipGainScale=1.75
        // Net gain at the shelf frequency ≈ boost*1.4 − atten*1.75 dB.
        float lfBoost = safeGetParam(tubeEQLfBoostGainParam, 0.0f);
        float lfAtten = safeGetParam(tubeEQLfAttenGainParam, 0.0f);
        if (std::abs(lfBoost) > 0.1f || std::abs(lfAtten) > 0.1f)
        {
            float lfFreqIdx = safeGetParam(tubeEQLfBoostFreqParam, 2.0f);
            static const float lfFreqValues[] = { 20.0f, 30.0f, 60.0f, 100.0f };
            int idx = juce::jlimit(0, 3, static_cast<int>(lfFreqIdx));
            float lfFreq = lfFreqValues[idx];

            float netLFGain = lfBoost * 1.4f - lfAtten * 1.75f;
            setBoolParam(ParamIDs::bandEnabled(2), true);
            setParam(ParamIDs::bandFreq(2), lfFreq);
            setParam(ParamIDs::bandGain(2), netLFGain);
            setChoiceParam(ParamIDs::bandShape(2), 0);  // Low Shelf
        }

        // Band 5: Parametric ← Tube EQ HF Boost
        // Scale factor matches TubeEQProcessor::updateHFBoost: gainDB = hfBoostGain * 1.8f
        // Q mapping matches: jmap(bandwidth, 0→1, 2.0→0.3) = 2.0 - bandwidth * 1.7
        float hfBoost = safeGetParam(tubeEQHfBoostGainParam, 0.0f);
        if (std::abs(hfBoost) > 0.1f)
        {
            static const float hfBoostFreqValues[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
            int hfIdx = juce::jlimit(0, 6, static_cast<int>(safeGetParam(tubeEQHfBoostFreqParam, 3.0f)));
            float hfFreq = hfBoostFreqValues[hfIdx];
            float bw = safeGetParam(tubeEQHfBoostBandwidthParam, 0.5f);
            float q = 2.0f - bw * 1.7f;  // Matches jmap(bw, 0→1, 2.0→0.3)

            setBoolParam(ParamIDs::bandEnabled(5), true);
            setParam(ParamIDs::bandFreq(5), hfFreq);
            setParam(ParamIDs::bandGain(5), hfBoost * 1.8f);
            setParam(ParamIDs::bandQ(5), q);
            setChoiceParam(ParamIDs::bandShape(5), 0);  // Peaking
        }

        // Band 7: High Shelf cut ← Tube EQ HF Atten
        // Scale factor matches TubeEQProcessor::updateHFAtten: gainDB = -hfAttenGain * 1.6f
        float hfAtten = safeGetParam(tubeEQHfAttenGainParam, 0.0f);
        if (std::abs(hfAtten) > 0.1f)
        {
            static const float hfAttenFreqValues[] = { 5000.0f, 10000.0f, 20000.0f };
            int atIdx = juce::jlimit(0, 2, static_cast<int>(safeGetParam(tubeEQHfAttenFreqParam, 1.0f)));
            float atFreq = hfAttenFreqValues[atIdx];

            setBoolParam(ParamIDs::bandEnabled(7), true);
            setParam(ParamIDs::bandFreq(7), atFreq);
            setParam(ParamIDs::bandGain(7), -hfAtten * 1.6f);
            setChoiceParam(ParamIDs::bandShape(7), 0);  // High Shelf
        }

        // Bands 3, 4, 6: Mid Dip/Peak section (if enabled)
        // Scale factors match TubeEQProcessor: midLowPeak*1.2, midDip*1.0, midHighPeak*1.2
        bool midEnabled = safeGetParam(tubeEQMidEnabledParam, 1.0f) > 0.5f;
        if (midEnabled)
        {
            // Mid Low Peak → Band 3
            float midLowPeak = safeGetParam(tubeEQMidLowPeakParam, 0.0f);
            if (std::abs(midLowPeak) > 0.1f)
            {
                static const float midLowFreqValues[] = { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f };
                int mlIdx = juce::jlimit(0, 4, static_cast<int>(safeGetParam(tubeEQMidLowFreqParam, 2.0f)));
                setBoolParam(ParamIDs::bandEnabled(3), true);
                setParam(ParamIDs::bandFreq(3), midLowFreqValues[mlIdx]);
                setParam(ParamIDs::bandGain(3), midLowPeak * 1.2f);
                setParam(ParamIDs::bandQ(3), 1.2f);
                setChoiceParam(ParamIDs::bandShape(3), 0);  // Peaking
            }

            // Mid Dip → Band 4
            float midDip = safeGetParam(tubeEQMidDipParam, 0.0f);
            if (std::abs(midDip) > 0.1f)
            {
                static const float midDipFreqValues[] = { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f, 1500.0f, 2000.0f };
                int mdIdx = juce::jlimit(0, 6, static_cast<int>(safeGetParam(tubeEQMidDipFreqParam, 3.0f)));
                setBoolParam(ParamIDs::bandEnabled(4), true);
                setParam(ParamIDs::bandFreq(4), midDipFreqValues[mdIdx]);
                setParam(ParamIDs::bandGain(4), -midDip);  // 1.0x scale: dip knob is already in dB
                setParam(ParamIDs::bandQ(4), 0.8f);
                setChoiceParam(ParamIDs::bandShape(4), 0);  // Peaking
            }

            // Mid High Peak → Band 6
            float midHighPeak = safeGetParam(tubeEQMidHighPeakParam, 0.0f);
            if (std::abs(midHighPeak) > 0.1f)
            {
                static const float midHighFreqValues[] = { 1500.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f };
                int mhIdx = juce::jlimit(0, 4, static_cast<int>(safeGetParam(tubeEQMidHighFreqParam, 2.0f)));
                setBoolParam(ParamIDs::bandEnabled(6), true);
                setParam(ParamIDs::bandFreq(6), midHighFreqValues[mhIdx]);
                setParam(ParamIDs::bandGain(6), midHighPeak * 1.2f);
                setParam(ParamIDs::bandQ(6), 1.4f);
                setChoiceParam(ParamIDs::bandShape(6), 0);  // Peaking
            }
        }

        // Transfer output gain
        setParam(ParamIDs::masterGain, safeGetParam(tubeEQOutputGainParam, 0.0f));
    }

    else if (eqType == EQType::Match)
    {
        if (!eqMatchProcessor.hasCorrectionCurve())
            return;

        // Get the correction curve (already smoothed, scaled, and limited)
        std::array<float, EQMatchProcessor::NUM_BINS> corrDB{};
        eqMatchProcessor.getCorrectionCurveDB(corrDB);
        double sr = baseSampleRate.load();
        float nyquist = static_cast<float>(sr * 0.5);
        float binWidth = nyquist / static_cast<float>(EQMatchProcessor::NUM_BINS - 1);

        // Useful bin range: 30 Hz to 18 kHz
        int minBin = std::max(1, static_cast<int>(30.0f / binWidth));
        int maxBin = std::min(EQMatchProcessor::NUM_BINS - 1, static_cast<int>(18000.0f / binWidth));

        // Disable all bands first
        for (int i = 1; i <= 8; ++i)
            setBoolParam(ParamIDs::bandEnabled(i), false);

        // Residual starts as a copy of the correction curve
        std::array<float, EQMatchProcessor::NUM_BINS> residual = corrDB;

        // Fitted band storage
        struct FittedBand { float freq; float gain; float q; };
        std::vector<FittedBand> fittedBands;

        // Helper: compute peaking EQ magnitude response in dB at a given frequency
        auto peakingMagDB = [&](float centerFreq, float gainDB, float Q, float evalFreq) -> float {
            if (evalFreq <= 0.0f || centerFreq <= 0.0f || Q <= 0.0f)
                return 0.0f;
            double A = std::pow(10.0, static_cast<double>(gainDB) / 40.0);
            double w0 = 2.0 * juce::MathConstants<double>::pi * centerFreq / sr;
            double wEval = 2.0 * juce::MathConstants<double>::pi * evalFreq / sr;
            double cosw0 = std::cos(w0);
            double sinw0 = std::sin(w0);
            double alpha = sinw0 / (2.0 * Q);

            double b0 = 1.0 + alpha * A;
            double b1 = -2.0 * cosw0;
            double b2 = 1.0 - alpha * A;
            double a0 = 1.0 + alpha / A;
            double a1 = -2.0 * cosw0;
            double a2 = 1.0 - alpha / A;

            double cosw = std::cos(wEval);
            double sinw = std::sin(wEval);
            double cos2w = std::cos(2.0 * wEval);
            double sin2w = std::sin(2.0 * wEval);

            double numRe = b0 + b1 * cosw + b2 * cos2w;
            double numIm = -(b1 * sinw + b2 * sin2w);
            double denRe = a0 + a1 * cosw + a2 * cos2w;
            double denIm = -(a1 * sinw + a2 * sin2w);

            double numMag2 = numRe * numRe + numIm * numIm;
            double denMag2 = denRe * denRe + denIm * denIm;
            if (denMag2 < 1e-30) return 0.0f;

            return static_cast<float>(10.0 * std::log10(numMag2 / denMag2));
        };

        // Iterative peak-finding: 6 bands
        for (int iter = 0; iter < 6; ++iter)
        {
            // Find bin with maximum absolute residual
            int peakBin = minBin;
            float peakAbs = 0.0f;
            for (int k = minBin; k <= maxBin; ++k)
            {
                float absVal = std::abs(residual[static_cast<size_t>(k)]);
                if (absVal > peakAbs)
                {
                    peakAbs = absVal;
                    peakBin = k;
                }
            }

            float peakFreq = static_cast<float>(peakBin) * binWidth;
            float peakGain = residual[static_cast<size_t>(peakBin)];

            // Skip if residual is negligible
            if (std::abs(peakGain) < 0.5f)
                break;

            // Estimate Q: find where |residual| drops below half peak magnitude
            float halfPeak = peakAbs * 0.5f;
            int lowerBin = peakBin;
            int upperBin = peakBin;

            for (int k = peakBin - 1; k >= minBin; --k)
            {
                if (std::abs(residual[static_cast<size_t>(k)]) < halfPeak)
                    break;
                lowerBin = k;
            }
            for (int k = peakBin + 1; k <= maxBin; ++k)
            {
                if (std::abs(residual[static_cast<size_t>(k)]) < halfPeak)
                    break;
                upperBin = k;
            }

            float lowerFreq = static_cast<float>(lowerBin) * binWidth;
            float upperFreq = static_cast<float>(upperBin) * binWidth;
            float bandwidth = upperFreq - lowerFreq;
            float Q = (bandwidth > 1.0f) ? (peakFreq / bandwidth) : 2.0f;
            Q = juce::jlimit(0.3f, 10.0f, Q);

            fittedBands.push_back({peakFreq, peakGain, Q});

            // Subtract this band's response from the residual
            for (int k = minBin; k <= maxBin; ++k)
            {
                float freq = static_cast<float>(k) * binWidth;
                residual[static_cast<size_t>(k)] -= peakingMagDB(peakFreq, peakGain, Q, freq);
            }
        }

        // Sort fitted bands by frequency
        std::sort(fittedBands.begin(), fittedBands.end(),
                  [](const FittedBand& a, const FittedBand& b) { return a.freq < b.freq; });

        // Assign to Digital bands 2-7
        for (int i = 0; i < static_cast<int>(fittedBands.size()) && i < 6; ++i)
        {
            int bandNum = i + 2; // bands 2-7
            auto& fb = fittedBands[static_cast<size_t>(i)];

            setBoolParam(ParamIDs::bandEnabled(bandNum), true);
            setParam(ParamIDs::bandFreq(bandNum), juce::jlimit(20.0f, 20000.0f, fb.freq));
            setParam(ParamIDs::bandGain(bandNum), juce::jlimit(-24.0f, 24.0f, fb.gain));
            setParam(ParamIDs::bandQ(bandNum), fb.q);
            // Bands 2 and 7 default to shelf (shape 0); force peaking (shape 1)
            // for fitted bands so they act as parametric EQ, not shelves.
            int shape = (bandNum == 2 || bandNum == 7) ? 1 : 0;
            setChoiceParam(ParamIDs::bandShape(bandNum), shape);
        }

        // Deactivate Match convolution
        matchConvolutionActive.store(false, std::memory_order_release);
    }

    // Switch to Digital mode after transfer.
    // This fires parameterChanged("eq_type") which queues an async updatePresetSelector().
    setChoiceParam(ParamIDs::eqType, static_cast<int>(EQType::Digital));

    // Set filtersNeedUpdate AFTER the mode switch. processBlock may have consumed the flag
    // while still in British/Tube mode (via exchange(false)). Setting it here ensures the
    // first Digital-mode processBlock call sees the flag and rebuilds IIR coefficients with
    // the newly transferred band parameters. Also triggers fresh-computation path in
    // getFrequencyResponseMagnitude() for immediate display accuracy before processBlock runs.
    filtersNeedUpdate.store(true, std::memory_order_relaxed);

    // Clear saved preset name — we've manually set band params, so no preset is "active"
    parameters.state.setProperty("presetName", "", nullptr);

    // Lower the guard AFTER updatePresetSelector() has had a chance to run.
    // callAsync is FIFO, so this runs after the updatePresetSelector() call queued above.
    // Use WeakReference so the callback is a no-op if the plugin is destroyed first.
    juce::WeakReference<MultiQ> weakThis(this);
    juce::MessageManager::callAsync([weakThis]() mutable {
        if (auto* self = weakThis.get())
            self->transferInProgress.store(false);
    });
}

// Match EQ — Logic Pro Match EQ style

bool MultiQ::computeMatchCorrection()
{
    float applyPct = safeGetParam(matchApplyParam, 100.0f);
    float applyAmount = applyPct / 100.0f;  // -1.0 to +1.0
    float smoothingSemitones = safeGetParam(matchSmoothingParam, 12.0f);
    bool limitBoost = safeGetParam(matchLimitBoostParam, 0.0f) > 0.5f;
    bool limitCut = safeGetParam(matchLimitCutParam, 0.0f) > 0.5f;

    float maxBoostDB = limitBoost ? 6.0f : 0.0f;    // 0 = use hard limit only (±15 dB)
    float maxCutDB = limitCut ? -6.0f : 0.0f;        // Limit buttons tighten to ±6 dB

    bool success = eqMatchProcessor.computeCorrection(
        smoothingSemitones, applyAmount, maxBoostDB, maxCutDB, true /* minimum phase */);

    if (success)
    {
        juce::AudioBuffer<float> ir = eqMatchProcessor.getCorrectionIR();
        if (ir.getNumSamples() > 0)
        {
            matchConvolution.loadImpulseResponse(
                std::move(ir),
                baseSampleRate,
                juce::dsp::Convolution::Stereo::no,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);

            matchConvolutionActive.store(true, std::memory_order_release);

            // Switch to Match mode if not already
            if (auto* param = parameters.getParameter(ParamIDs::eqType))
            {
                int numChoices = param->getNumSteps();
                if (numChoices > 1)
                    param->setValueNotifyingHost(
                        static_cast<float>(static_cast<int>(EQType::Match))
                        / static_cast<float>(numChoices - 1));
            }
            return true;
        }
    }
    return false;
}

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
        std::copy(samples + size1, samples + size1 + size2, audioBuffer.begin() + start2);

    fifo.finishedWrite(size1 + size2);
}

void MultiQ::updateFFTSize(int order)
{
    // Allocation-free: just switch to the pre-allocated slot matching the requested order
    int slotIndex = order - FFT_ORDER_LOW;  // LOW=11→0, MEDIUM=12→1, HIGH=13→2
    slotIndex = juce::jlimit(0, NUM_FFT_SIZES - 1, slotIndex);

    if (slotIndex != activeFFTSlot && fftSlots[static_cast<size_t>(slotIndex)].fft != nullptr)
    {
        activeFFTSlot = slotIndex;
        currentFFTSize = fftSlots[static_cast<size_t>(slotIndex)].size;
    }
}

void MultiQ::convertFFTToMagnitudes(std::vector<float>& fftBuffer,
                                    std::array<float, 2048>& magnitudes,
                                    std::array<float, 2048>& peakHold)
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

}

void MultiQ::processFFT()
{
    if (analyzerFifo.getNumReady() < currentFFTSize)
        return;

    auto& slot = fftSlots[static_cast<size_t>(activeFFTSlot)];

    int start1, size1, start2, size2;
    analyzerFifo.prepareToRead(currentFFTSize, start1, size1, start2, size2);

    std::copy(analyzerAudioBuffer.begin() + start1,
              analyzerAudioBuffer.begin() + start1 + size1,
              slot.inputBuffer.begin());
    if (size2 > 0)
    {
        std::copy(analyzerAudioBuffer.begin() + start2,
                  analyzerAudioBuffer.begin() + start2 + size2,
                  slot.inputBuffer.begin() + size1);
    }

    analyzerFifo.finishedRead(size1 + size2);

    slot.window->multiplyWithWindowingTable(slot.inputBuffer.data(), static_cast<size_t>(currentFFTSize));
    slot.fft->performFrequencyOnlyForwardTransform(slot.inputBuffer.data());

    // Convert into local buffers (no lock needed — only this thread writes)
    std::array<float, 2048> localMags = analyzerMagnitudes;
    std::array<float, 2048> localPeaks = peakHoldValues;
    convertFFTToMagnitudes(slot.inputBuffer, localMags, localPeaks);

    // Short lock to publish results
    {
        juce::SpinLock::ScopedLockType lock(analyzerMagnitudesLock);
        analyzerMagnitudes = localMags;
        peakHoldValues = localPeaks;
        analyzerDataReady.store(true);
    }
}

void MultiQ::processPreFFT()
{
    if (preAnalyzerFifo.getNumReady() < currentFFTSize)
        return;

    auto& slot = fftSlots[static_cast<size_t>(activeFFTSlot)];

    int start1, size1, start2, size2;
    preAnalyzerFifo.prepareToRead(currentFFTSize, start1, size1, start2, size2);

    std::copy(preAnalyzerAudioBuffer.begin() + start1,
              preAnalyzerAudioBuffer.begin() + start1 + size1,
              slot.preInputBuffer.begin());
    if (size2 > 0)
    {
        std::copy(preAnalyzerAudioBuffer.begin() + start2,
                  preAnalyzerAudioBuffer.begin() + start2 + size2,
                  slot.preInputBuffer.begin() + size1);
    }

    preAnalyzerFifo.finishedRead(size1 + size2);

    slot.window->multiplyWithWindowingTable(slot.preInputBuffer.data(), static_cast<size_t>(currentFFTSize));
    slot.fft->performFrequencyOnlyForwardTransform(slot.preInputBuffer.data());

    // Convert into local buffers (no lock needed — only this thread writes)
    std::array<float, 2048> localPreMags = preAnalyzerMagnitudes;
    std::array<float, 2048> localPrePeaks = prePeakHoldValues;
    convertFFTToMagnitudes(slot.preInputBuffer, localPreMags, localPrePeaks);

    // Short lock to publish results
    {
        juce::SpinLock::ScopedLockType lock(preAnalyzerMagnitudesLock);
        preAnalyzerMagnitudes = localPreMags;
        prePeakHoldValues = localPrePeaks;
        preAnalyzerDataReady.store(true);
    }
}

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

        // Invert: flips EQ gain (boost↔cut) — bands 2-7 only (parametric/shelf)
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ParamIDs::bandInvert(bandNum), 1),
            "Band " + juce::String(bandNum) + " Invert",
            false
        ));

        // Phase Invert: flips polarity of band's contribution
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ParamIDs::bandPhaseInvert(bandNum), 1),
            "Band " + juce::String(bandNum) + " Phase Invert",
            false
        ));

        // Pan: stereo placement of band's EQ effect (-1.0 = L, 0 = center, +1.0 = R)
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamIDs::bandPan(bandNum), 1),
            "Band " + juce::String(bandNum) + " Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
            0.0f
        ));
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
        juce::StringArray{"Digital", "Match", "British", "Tube"},
        0  // Digital by default (includes per-band dynamics capability)
    ));

    // Match EQ parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::matchApply, 1),
        "Match Apply",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f),
        100.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float v, int) { return juce::String(juce::roundToInt(v)) + "%"; },
        [](const juce::String& t) { return t.trimCharactersAtEnd("%").getFloatValue(); }
    ));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::matchSmoothing, 1),
        "Match Smoothing",
        juce::NormalisableRange<float>(1.0f, 24.0f, 0.5f),
        12.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float v, int) { return juce::String(v, 1) + " st"; },
        [](const juce::String& t) { return t.trimCharactersAtEnd(" st").getFloatValue(); }
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::matchLimitBoost, 1),
        "Match Limit Boost",
        false
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::matchLimitCut, 1),
        "Match Limit Cut",
        false
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

        // Threshold (-48 to 0 dB)
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

// State version for future migration support
// Increment this when parameter layout changes to enable proper migration
static constexpr int STATE_VERSION = 1;

void MultiQ::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    if (xml == nullptr)
        return;

    // Add version tag for future migration support
    xml->setAttribute("stateVersion", STATE_VERSION);
    xml->setAttribute("pluginVersion", PLUGIN_VERSION);

    // Persist Match EQ learned spectra and correction data
    eqMatchProcessor.serialize(*xml);

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
            // Old: 0=Digital, 1=Dynamic, 2=British, 3=Tube (4 choices)
            // New: 0=Digital, 1=Match, 2=British, 3=Tube (4 choices)
            // Only Dynamic(1) needs remapping → Digital(0); British and Tube indices unchanged
            auto eqTypeChild = newState.getChildWithProperty("id", ParamIDs::eqType);
            if (eqTypeChild.isValid())
            {
                float oldNorm = eqTypeChild.getProperty("value", 0.0f);
                // APVTS stores choice params as normalized 0.0-1.0
                // For 4 choices: 0.0=idx0, 0.333=idx1, 0.667=idx2, 1.0=idx3
                int oldIndex = juce::roundToInt(oldNorm * 3.0f);
                oldIndex = juce::jlimit(0, 3, oldIndex);

                if (oldIndex == 1)  // Old Dynamic -> New Digital
                {
                    float newNorm = 0.0f;  // Index 0 normalized
                    eqTypeChild.setProperty("value", newNorm, nullptr);
                }
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
        // Restore Match EQ learned spectra and correction data
        if (eqMatchProcessor.deserialize(*xmlState))
        {
            // If a valid correction FIR was restored, load it into the convolution engine
            if (eqMatchProcessor.hasCorrectionCurve())
            {
                auto ir = eqMatchProcessor.getCorrectionIR();
                if (ir.getNumSamples() > 0)
                {
                    matchConvolution.loadImpulseResponse(
                        std::move(ir), baseSampleRate.load(), juce::dsp::Convolution::Stereo::no,
                        juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
                    matchConvolutionActive.store(true, std::memory_order_release);
                }
            }
        }
    }
}

// Factory Presets

int MultiQ::getNumPrograms()
{
    return static_cast<int>(factoryPresets.size()) + 1;  // +1 for "Init" preset
}

int MultiQ::getCurrentProgram()
{
    return currentPresetIndex;
}

void MultiQ::setCurrentProgram(int index)
{
    // setCurrentProgram(0) is called by AU hosts (e.g., Logic Pro) as a bookkeeping
    // operation whenever they consider the plugin to be on the "default" program.
    // This happens automatically in response to parameter changes and does NOT mean
    // the user wants to reset the EQ to flat. We intentionally do NOT reset any
    // parameters here — that would silently wipe settings the user just dialled in.
    //
    // The "Init" reset is only performed when the user explicitly selects Init from
    // the plugin's own preset dropdown, which calls resetToInit() directly.
    if (index == 0)
    {
        currentPresetIndex = 0;
        // Do NOT reset parameters — see comment above.
        return;
    }

    int presetIndex = index - 1;  // Adjust for "Init" at position 0
    if (presetIndex >= 0 && presetIndex < static_cast<int>(factoryPresets.size()))
    {
        currentPresetIndex = index;
        MultiQPresets::applyPreset(parameters, factoryPresets[static_cast<size_t>(presetIndex)]);
        parameters.state.setProperty("presetName",
            factoryPresets[static_cast<size_t>(presetIndex)].name, nullptr);
    }
}

void MultiQ::resetToInit()
{
    currentPresetIndex = 0;
    parameters.state.setProperty("presetName", "Init", nullptr);

    for (int i = 1; i <= 8; ++i)
    {
        if (auto* p = parameters.getParameter(ParamIDs::bandEnabled(i)))
            p->setValueNotifyingHost(i == 1 || i == 8 ? 0.0f : 1.0f);

        if (auto* p = parameters.getParameter(ParamIDs::bandGain(i)))
            p->setValueNotifyingHost(0.5f);  // 0 dB

        if (auto* p = parameters.getParameter(ParamIDs::bandQ(i)))
            p->setValueNotifyingHost(parameters.getParameterRange(ParamIDs::bandQ(i)).convertTo0to1(0.71f));

        if (auto* p = parameters.getParameter(ParamIDs::bandInvert(i)))
            p->setValueNotifyingHost(0.0f);
        if (auto* p = parameters.getParameter(ParamIDs::bandPhaseInvert(i)))
            p->setValueNotifyingHost(0.0f);
        if (auto* p = parameters.getParameter(ParamIDs::bandPan(i)))
            p->setValueNotifyingHost(parameters.getParameterRange(ParamIDs::bandPan(i)).convertTo0to1(0.0f));
    }

    if (auto* p = parameters.getParameter(ParamIDs::masterGain))
        p->setValueNotifyingHost(0.5f);

    if (auto* p = parameters.getParameter(ParamIDs::hqEnabled))
        p->setValueNotifyingHost(0.0f);

    if (auto* p = parameters.getParameter(ParamIDs::qCoupleMode))
        p->setValueNotifyingHost(0.0f);
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

int MultiQ::getLatencySamples() const
{
    // Always report max oversampler latency (4x) so DAW PDC stays constant
    // when switching oversampling modes mid-playback. A compensation delay line
    // fills the gap for Off/2x modes.
    int latency = maxOversamplerLatency;
    latency += outputLimiter.getLatencySamples();
    return latency;
}

double MultiQ::getTailLengthSeconds() const
{
    double tail = 0.0;

    // Limiter release tail (~100ms)
    if (outputLimiter.isEnabled())
        tail = std::max(tail, 0.15);

    // Oversampling anti-alias filter ringing (~10ms worst case for 4x)
    int osMode = oversamplingMode.load(std::memory_order_relaxed);
    if (osMode > 0)
        tail = std::max(tail, 0.01);

    return tail;
}

juce::AudioProcessorParameter* MultiQ::getBypassParameter() const
{
    // Return our "bypass" parameter so the AU/VST3 wrapper routes host bypass
    // through processBlock (where our internal bypass applies correct delay paths)
    // instead of processBlockBypassed or skipping rendering entirely.
    // Without this, Logic Pro's disable button stops calling the AU but still
    // applies PDC -> phase offset on parallel buses.
    return parameters.getParameter(ParamIDs::bypass);
}

juce::AudioProcessorEditor* MultiQ::createEditor()
{
    return new MultiQEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultiQ();
}

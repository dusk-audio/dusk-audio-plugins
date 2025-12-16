#include "MultiQ.h"
#include "MultiQEditor.h"

//==============================================================================
MultiQ::MultiQ()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("MultiQ"), createParameterLayout())
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

    // Pultec mode parameters
    pultecLfBoostGainParam = parameters.getRawParameterValue(ParamIDs::pultecLfBoostGain);
    pultecLfBoostFreqParam = parameters.getRawParameterValue(ParamIDs::pultecLfBoostFreq);
    pultecLfAttenGainParam = parameters.getRawParameterValue(ParamIDs::pultecLfAttenGain);
    pultecHfBoostGainParam = parameters.getRawParameterValue(ParamIDs::pultecHfBoostGain);
    pultecHfBoostFreqParam = parameters.getRawParameterValue(ParamIDs::pultecHfBoostFreq);
    pultecHfBoostBandwidthParam = parameters.getRawParameterValue(ParamIDs::pultecHfBoostBandwidth);
    pultecHfAttenGainParam = parameters.getRawParameterValue(ParamIDs::pultecHfAttenGain);
    pultecHfAttenFreqParam = parameters.getRawParameterValue(ParamIDs::pultecHfAttenFreq);
    pultecInputGainParam = parameters.getRawParameterValue(ParamIDs::pultecInputGain);
    pultecOutputGainParam = parameters.getRawParameterValue(ParamIDs::pultecOutputGain);
    pultecTubeDriveParam = parameters.getRawParameterValue(ParamIDs::pultecTubeDrive);

    // Add global parameter listeners
    parameters.addParameterListener(ParamIDs::hqEnabled, this);
    parameters.addParameterListener(ParamIDs::qCoupleMode, this);
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
    parameters.removeParameterListener(ParamIDs::hqEnabled, this);
    parameters.removeParameterListener(ParamIDs::qCoupleMode, this);
    parameters.removeParameterListener(ParamIDs::analyzerResolution, this);
}

//==============================================================================
void MultiQ::parameterChanged(const juce::String& parameterID, float /*newValue*/)
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

    // HQ mode change requires full re-preparation
    if (parameterID == ParamIDs::hqEnabled)
    {
        // Will be handled in processBlock
        filtersNeedUpdate.store(true);
    }

    // Analyzer resolution change
    if (parameterID == ParamIDs::analyzerResolution)
    {
        auto res = static_cast<AnalyzerResolution>(
            static_cast<int>(safeGetParam(analyzerResolutionParam, 1.0f)));
        updateFFTSize(res);
    }
}

//==============================================================================
void MultiQ::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;

    // Check HQ mode (2x oversampling for analog-matched response)
    hqModeEnabled = safeGetParam(hqEnabledParam, 0.0f) > 0.5f;

    if (hqModeEnabled)
    {
        // 2x oversampling - use minimum phase filtering for lower latency
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
        oversampler->initProcessing(static_cast<size_t>(samplesPerBlock));
        currentSampleRate = sampleRate * 2.0;
    }
    else
    {
        oversampler.reset();
        currentSampleRate = sampleRate;
    }

    // Prepare filter spec
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock * (hqModeEnabled ? 2 : 1));
    spec.numChannels = 2;

    // Prepare HPF
    hpfFilter.prepare(spec);

    // Prepare EQ filters (bands 2-7)
    for (auto& filter : eqFilters)
        filter.prepare(spec);

    // Prepare LPF
    lpfFilter.prepare(spec);

    // Reset all filters
    hpfFilter.reset();
    for (auto& filter : eqFilters)
        filter.reset();
    lpfFilter.reset();

    // Force filter update
    filtersNeedUpdate.store(true);
    updateAllFilters();

    // Prepare British EQ processor
    britishEQ.prepare(currentSampleRate, samplesPerBlock * (hqModeEnabled ? 2 : 1), 2);
    britishParamsChanged.store(true);

    // Prepare Pultec EQ processor
    pultecEQ.prepare(currentSampleRate, samplesPerBlock * (hqModeEnabled ? 2 : 1), 2);
    pultecParamsChanged.store(true);

    // Reset analyzer
    analyzerFifo.reset();
    std::fill(analyzerMagnitudes.begin(), analyzerMagnitudes.end(), -100.0f);
    std::fill(peakHoldValues.begin(), peakHoldValues.end(), -100.0f);
}

void MultiQ::releaseResources()
{
    oversampler.reset();
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

    // Check bypass
    if (safeGetParam(bypassParam, 0.0f) > 0.5f)
        return;

    // Check if HQ mode changed
    bool newHqMode = safeGetParam(hqEnabledParam, 0.0f) > 0.5f;
    if (newHqMode != hqModeEnabled)
    {
        hqModeEnabled = newHqMode;
        prepareToPlay(baseSampleRate, buffer.getNumSamples());
    }

    // Check EQ type (Digital, British, or Tube)
    auto eqType = static_cast<EQType>(static_cast<int>(safeGetParam(eqTypeParam, 0.0f)));

    // Update filters if needed (only for Digital mode)
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

    // Update Pultec EQ parameters if needed
    if (eqType == EQType::Tube)
    {
        // LF boost frequency lookup table: 20, 30, 60, 100 Hz
        static const float lfFreqValues[] = { 20.0f, 30.0f, 60.0f, 100.0f };
        // HF boost frequency lookup table: 3k, 4k, 5k, 8k, 10k, 12k, 16k Hz
        static const float hfBoostFreqValues[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
        // HF atten frequency lookup table: 5k, 10k, 20k Hz
        static const float hfAttenFreqValues[] = { 5000.0f, 10000.0f, 20000.0f };

        PultecProcessor::Parameters pultecParams;
        pultecParams.lfBoostGain = safeGetParam(pultecLfBoostGainParam, 0.0f);

        int lfFreqIdx = static_cast<int>(safeGetParam(pultecLfBoostFreqParam, 2.0f));
        lfFreqIdx = juce::jlimit(0, 3, lfFreqIdx);
        pultecParams.lfBoostFreq = lfFreqValues[lfFreqIdx];

        pultecParams.lfAttenGain = safeGetParam(pultecLfAttenGainParam, 0.0f);
        pultecParams.hfBoostGain = safeGetParam(pultecHfBoostGainParam, 0.0f);

        int hfBoostFreqIdx = static_cast<int>(safeGetParam(pultecHfBoostFreqParam, 3.0f));
        hfBoostFreqIdx = juce::jlimit(0, 6, hfBoostFreqIdx);
        pultecParams.hfBoostFreq = hfBoostFreqValues[hfBoostFreqIdx];

        pultecParams.hfBoostBandwidth = safeGetParam(pultecHfBoostBandwidthParam, 0.5f);
        pultecParams.hfAttenGain = safeGetParam(pultecHfAttenGainParam, 0.0f);

        int hfAttenFreqIdx = static_cast<int>(safeGetParam(pultecHfAttenFreqParam, 1.0f));
        hfAttenFreqIdx = juce::jlimit(0, 2, hfAttenFreqIdx);
        pultecParams.hfAttenFreq = hfAttenFreqValues[hfAttenFreqIdx];

        pultecParams.inputGain = safeGetParam(pultecInputGainParam, 0.0f);
        pultecParams.outputGain = safeGetParam(pultecOutputGainParam, 0.0f);
        pultecParams.tubeDrive = safeGetParam(pultecTubeDriveParam, 0.3f);
        pultecEQ.setParameters(pultecParams);
    }

    // Input level metering (using peak values to match DAW meters)
    float inL = buffer.findMinMax(0, 0, buffer.getNumSamples()).getEnd();
    inL = std::max(std::abs(inL), std::abs(buffer.findMinMax(0, 0, buffer.getNumSamples()).getStart()));
    float inR = buffer.getNumChannels() > 1
        ? std::max(std::abs(buffer.findMinMax(1, 0, buffer.getNumSamples()).getEnd()),
                   std::abs(buffer.findMinMax(1, 0, buffer.getNumSamples()).getStart()))
        : inL;
    inputLevelL.store(juce::Decibels::gainToDecibels(inL, -96.0f));
    inputLevelR.store(juce::Decibels::gainToDecibels(inR, -96.0f));

    // Push pre-EQ samples to analyzer if enabled
    bool analyzerEnabled = safeGetParam(analyzerEnabledParam, 0.0f) > 0.5f;
    bool analyzerPreEQ = safeGetParam(analyzerPrePostParam, 0.0f) > 0.5f;
    if (analyzerEnabled && analyzerPreEQ)
    {
        // Mix to mono for analyzer
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float mono = (readL[i] + readR[i]) * 0.5f;
            pushSamplesToAnalyzer(&mono, 1, true);
        }
    }

    // Get processing mode
    auto procMode = static_cast<ProcessingMode>(
        static_cast<int>(safeGetParam(processingModeParam, 0.0f)));

    // Get pointers
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    // Oversampling upsample
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::AudioBlock<float> processBlock = block;

    if (hqModeEnabled && oversampler)
    {
        processBlock = oversampler->processSamplesUp(block);
    }

    int numSamples = static_cast<int>(processBlock.getNumSamples());
    float* procL = processBlock.getChannelPointer(0);
    float* procR = processBlock.getNumChannels() > 1 ? processBlock.getChannelPointer(1) : procL;

    // M/S encode if needed
    bool useMS = (procMode == ProcessingMode::Mid || procMode == ProcessingMode::Side);
    if (useMS)
    {
        for (int i = 0; i < numSamples; ++i)
            encodeMS(procL[i], procR[i]);
    }

    // Process based on EQ type
    if (eqType == EQType::British)
    {
        // British mode: Use 4K-EQ style processing
        // Create a temporary buffer from the processBlock for British EQ
        juce::AudioBuffer<float> tempBuffer(static_cast<int>(processBlock.getNumChannels()),
                                            static_cast<int>(processBlock.getNumSamples()));
        for (size_t ch = 0; ch < processBlock.getNumChannels(); ++ch)
        {
            tempBuffer.copyFrom(static_cast<int>(ch), 0,
                               processBlock.getChannelPointer(ch),
                               static_cast<int>(processBlock.getNumSamples()));
        }

        britishEQ.process(tempBuffer);

        // Copy back to processBlock
        for (size_t ch = 0; ch < processBlock.getNumChannels(); ++ch)
        {
            std::memcpy(processBlock.getChannelPointer(ch),
                       tempBuffer.getReadPointer(static_cast<int>(ch)),
                       sizeof(float) * processBlock.getNumSamples());
        }
    }
    else if (eqType == EQType::Tube)
    {
        // Pultec/Tube mode: Use Pultec EQP-1A style processing
        // Create a temporary buffer from the processBlock for Pultec EQ
        juce::AudioBuffer<float> tempBuffer(static_cast<int>(processBlock.getNumChannels()),
                                            static_cast<int>(processBlock.getNumSamples()));
        for (size_t ch = 0; ch < processBlock.getNumChannels(); ++ch)
        {
            tempBuffer.copyFrom(static_cast<int>(ch), 0,
                               processBlock.getChannelPointer(ch),
                               static_cast<int>(processBlock.getNumSamples()));
        }

        pultecEQ.process(tempBuffer);

        // Copy back to processBlock
        for (size_t ch = 0; ch < processBlock.getNumChannels(); ++ch)
        {
            std::memcpy(processBlock.getChannelPointer(ch),
                       tempBuffer.getReadPointer(static_cast<int>(ch)),
                       sizeof(float) * processBlock.getNumSamples());
        }
    }
    else
    {
        // Digital mode: Use Multi-Q 8-band EQ processing
        // Check which bands are enabled
        std::array<bool, NUM_BANDS> bandEnabled{};
        for (int i = 0; i < NUM_BANDS; ++i)
            bandEnabled[static_cast<size_t>(i)] = safeGetParam(bandEnabledParams[static_cast<size_t>(i)], 0.0f) > 0.5f;

        // Process each sample through the filter chain
        for (int i = 0; i < numSamples; ++i)
        {
            float sampleL = procL[i];
            float sampleR = procR[i];

            // Determine which channel(s) to process based on mode
            bool processLeft = (procMode == ProcessingMode::Stereo ||
                               procMode == ProcessingMode::Left ||
                               procMode == ProcessingMode::Mid);
            bool processRight = (procMode == ProcessingMode::Stereo ||
                                procMode == ProcessingMode::Right ||
                                procMode == ProcessingMode::Side);

            // Band 1: HPF
            if (bandEnabled[0])
            {
                if (processLeft) sampleL = hpfFilter.processSampleL(sampleL);
                if (processRight) sampleR = hpfFilter.processSampleR(sampleR);
            }

            // Bands 2-7: Shelf and Parametric
            for (int band = 1; band < 7; ++band)
            {
                if (bandEnabled[static_cast<size_t>(band)])
                {
                    auto& filter = eqFilters[static_cast<size_t>(band - 1)];
                    if (processLeft) sampleL = filter.processSampleL(sampleL);
                    if (processRight) sampleR = filter.processSampleR(sampleR);
                }
            }

            // Band 8: LPF
            if (bandEnabled[7])
            {
                if (processLeft) sampleL = lpfFilter.processSampleL(sampleL);
                if (processRight) sampleR = lpfFilter.processSampleR(sampleR);
            }

            procL[i] = sampleL;
            procR[i] = sampleR;
        }
    }

    // M/S decode if needed
    if (useMS)
    {
        for (int i = 0; i < numSamples; ++i)
            decodeMS(procL[i], procR[i]);
    }

    // Oversampling downsample
    if (hqModeEnabled && oversampler)
    {
        oversampler->processSamplesDown(block);
    }

    // Apply master gain
    float masterGain = juce::Decibels::decibelsToGain(safeGetParam(masterGainParam, 0.0f));
    buffer.applyGain(masterGain);

    // Push post-EQ samples to analyzer if enabled
    if (analyzerEnabled && !analyzerPreEQ)
    {
        const float* readL = buffer.getReadPointer(0);
        const float* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : readL;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float mono = (readL[i] + readR[i]) * 0.5f;
            pushSamplesToAnalyzer(&mono, 1, false);
        }
    }

    // Output level metering (using peak values to match DAW meters)
    float outL = buffer.findMinMax(0, 0, buffer.getNumSamples()).getEnd();
    outL = std::max(std::abs(outL), std::abs(buffer.findMinMax(0, 0, buffer.getNumSamples()).getStart()));
    float outR = buffer.getNumChannels() > 1
        ? std::max(std::abs(buffer.findMinMax(1, 0, buffer.getNumSamples()).getEnd()),
                   std::abs(buffer.findMinMax(1, 0, buffer.getNumSamples()).getStart()))
        : outL;
    outputLevelL.store(juce::Decibels::gainToDecibels(outL, -96.0f));
    outputLevelR.store(juce::Decibels::gainToDecibels(outR, -96.0f));

    // Process FFT if we have enough samples
    processFFT();
}

//==============================================================================
void MultiQ::updateAllFilters()
{
    updateHPFCoefficients(currentSampleRate);
    updateLPFCoefficients(currentSampleRate);

    for (int i = 1; i < 7; ++i)
        updateBandFilter(i);
}

void MultiQ::updateBandFilter(int bandIndex)
{
    if (bandIndex < 1 || bandIndex > 6)
        return;  // Only bands 2-7 use standard EQ filters

    float freq = safeGetParam(bandFreqParams[static_cast<size_t>(bandIndex)], DefaultBandConfigs[static_cast<size_t>(bandIndex)].defaultFreq);
    float gain = safeGetParam(bandGainParams[static_cast<size_t>(bandIndex)], 0.0f);
    float baseQ = safeGetParam(bandQParams[static_cast<size_t>(bandIndex)], 0.71f);

    // Apply Q-coupling
    QCoupleMode qMode = getCurrentQCoupleMode();
    float q = getQCoupledValue(baseQ, gain, qMode);

    auto& filter = eqFilters[static_cast<size_t>(bandIndex - 1)];

    // Band 2 (index 1): Low Shelf
    if (bandIndex == 1)
    {
        filter.setCoefficients(makeLowShelfCoefficients(currentSampleRate, freq, gain, q));
    }
    // Band 7 (index 6): High Shelf
    else if (bandIndex == 6)
    {
        filter.setCoefficients(makeHighShelfCoefficients(currentSampleRate, freq, gain, q));
    }
    // Bands 3-6 (indices 2-5): Parametric
    else
    {
        filter.setCoefficients(makePeakingCoefficients(currentSampleRate, freq, gain, q));
    }
}

//==============================================================================
// Analog-matched filter coefficient calculations using bilinear transform with pre-warping

void MultiQ::updateHPFCoefficients(double sampleRate)
{
    float freq = safeGetParam(bandFreqParams[0], 20.0f);
    float q = safeGetParam(bandQParams[0], 0.71f);
    int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[0], 0.0f));

    // Frequency pre-warping for analog matching
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    double warpedFreq = (2.0 / T) * std::tan(w0 * T / 2.0);
    double actualFreq = warpedFreq / (2.0 * juce::MathConstants<double>::pi);

    // Determine number of stages based on slope
    int stages = 1;
    switch (static_cast<FilterSlope>(slopeIndex))
    {
        case FilterSlope::Slope6dB:  stages = 1; break;
        case FilterSlope::Slope12dB: stages = 1; break;  // Single 2nd order
        case FilterSlope::Slope18dB: stages = 2; break;  // 1st + 2nd order
        case FilterSlope::Slope24dB: stages = 2; break;  // Two 2nd order
        case FilterSlope::Slope36dB: stages = 3; break;
        case FilterSlope::Slope48dB: stages = 4; break;
    }

    hpfFilter.activeStages = stages;

    // Create Butterworth cascade
    for (int stage = 0; stage < stages; ++stage)
    {
        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

        if (static_cast<FilterSlope>(slopeIndex) == FilterSlope::Slope6dB && stage == 0)
        {
            // 1st order HPF
            coeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(
                sampleRate, static_cast<float>(actualFreq));
        }
        else if (static_cast<FilterSlope>(slopeIndex) == FilterSlope::Slope18dB && stage == 0)
        {
            // First stage is 1st order for 18dB
            coeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(
                sampleRate, static_cast<float>(actualFreq));
        }
        else
        {
            // 2nd order HPF with user-specified Q (non-Butterworth if Q != 0.707)
            coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
                sampleRate, static_cast<float>(actualFreq), q);
        }
        hpfFilter.stagesL[static_cast<size_t>(stage)].coefficients = coeffs;
        hpfFilter.stagesR[static_cast<size_t>(stage)].coefficients = coeffs;
    }
}

void MultiQ::updateLPFCoefficients(double sampleRate)
{
    float freq = safeGetParam(bandFreqParams[7], 20000.0f);
    float q = safeGetParam(bandQParams[7], 0.71f);
    int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[1], 0.0f));

    // Frequency pre-warping for analog matching
    double w0 = 2.0 * juce::MathConstants<double>::pi * freq;
    double T = 1.0 / sampleRate;
    double warpedFreq = (2.0 / T) * std::tan(w0 * T / 2.0);
    double actualFreq = warpedFreq / (2.0 * juce::MathConstants<double>::pi);

    // Clamp to valid range
    actualFreq = juce::jlimit(20.0, sampleRate * 0.45, actualFreq);

    int stages = 1;
    switch (static_cast<FilterSlope>(slopeIndex))
    {
        case FilterSlope::Slope6dB:  stages = 1; break;
        case FilterSlope::Slope12dB: stages = 1; break;
        case FilterSlope::Slope18dB: stages = 2; break;
        case FilterSlope::Slope24dB: stages = 2; break;
        case FilterSlope::Slope36dB: stages = 3; break;
        case FilterSlope::Slope48dB: stages = 4; break;
    }

    lpfFilter.activeStages = stages;

    for (int stage = 0; stage < stages; ++stage)
    {
        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

        if (static_cast<FilterSlope>(slopeIndex) == FilterSlope::Slope6dB && stage == 0)
        {
            coeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass(
                sampleRate, static_cast<float>(actualFreq));
        }
        else if (static_cast<FilterSlope>(slopeIndex) == FilterSlope::Slope18dB && stage == 0)
        {
            coeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass(
                sampleRate, static_cast<float>(actualFreq));
        }
        else
        {
            coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
                sampleRate, static_cast<float>(actualFreq), q);
        }

        lpfFilter.stagesL[static_cast<size_t>(stage)].coefficients = coeffs;
        lpfFilter.stagesR[static_cast<size_t>(stage)].coefficients = coeffs;
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
    // Calculate the combined magnitude response at a given frequency
    // This is used for drawing the EQ curve in the UI

    double response = 1.0;

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        bool enabled = safeGetParam(bandEnabledParams[static_cast<size_t>(band)], 0.0f) > 0.5f;
        if (!enabled)
            continue;

        float freq = safeGetParam(bandFreqParams[static_cast<size_t>(band)],
                                  DefaultBandConfigs[static_cast<size_t>(band)].defaultFreq);
        float gain = safeGetParam(bandGainParams[static_cast<size_t>(band)], 0.0f);
        float q = safeGetParam(bandQParams[static_cast<size_t>(band)], 0.71f);

        // Simple approximation for curve display
        // This gives a reasonable visual representation

        if (band == 0)  // HPF
        {
            // HPF response approximation
            float ratio = frequencyHz / freq;
            if (ratio < 1.0f)
            {
                int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[0], 0.0f));
                float slopeDB = 6.0f * (slopeIndex + 1);
                response *= std::pow(ratio, slopeDB / 6.0);
            }
        }
        else if (band == 7)  // LPF
        {
            float ratio = freq / frequencyHz;
            if (ratio < 1.0f)
            {
                int slopeIndex = static_cast<int>(safeGetParam(bandSlopeParams[1], 0.0f));
                float slopeDB = 6.0f * (slopeIndex + 1);
                response *= std::pow(ratio, slopeDB / 6.0);
            }
        }
        else if (band == 1)  // Low Shelf
        {
            float gainLinear = juce::Decibels::decibelsToGain(gain);
            float ratio = frequencyHz / freq;
            if (ratio < 1.0f)
                response *= gainLinear;
            else
            {
                float transition = std::pow(ratio, -2.0f / q);
                response *= 1.0 + (gainLinear - 1.0) * transition;
            }
        }
        else if (band == 6)  // High Shelf
        {
            float gainLinear = juce::Decibels::decibelsToGain(gain);
            float ratio = frequencyHz / freq;
            if (ratio > 1.0f)
                response *= gainLinear;
            else
            {
                float transition = std::pow(ratio, 2.0f / q);
                response *= 1.0 + (gainLinear - 1.0) * transition;
            }
        }
        else  // Parametric
        {
            float effectiveQ = getQCoupledValue(q, gain, getCurrentQCoupleMode());
            float gainLinear = juce::Decibels::decibelsToGain(gain);
            float ratio = frequencyHz / freq;
            float logRatio = std::log2(ratio);
            float bandwidth = 1.0f / effectiveQ;
            float envelope = std::exp(-logRatio * logRatio / (bandwidth * bandwidth * 0.5f));
            response *= 1.0 + (gainLinear - 1.0) * envelope;
        }
    }

    return static_cast<float>(juce::Decibels::gainToDecibels(response, -100.0));
}

//==============================================================================
// FFT Analyzer

void MultiQ::pushSamplesToAnalyzer(const float* samples, int numSamples, bool /*isPreEQ*/)
{
    int start1, size1, start2, size2;
    analyzerFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    if (size1 > 0)
        std::copy(samples, samples + size1, analyzerAudioBuffer.begin() + start1);
    if (size2 > 0)
        std::copy(samples + size1, samples + numSamples, analyzerAudioBuffer.begin() + start2);

    analyzerFifo.finishedWrite(size1 + size2);
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
    }
}

void MultiQ::processFFT()
{
    // Check if we have enough samples
    if (analyzerFifo.getNumReady() < currentFFTSize)
        return;

    // Read samples from FIFO
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

    // Apply window
    fftWindow->multiplyWithWindowingTable(fftInputBuffer.data(), static_cast<size_t>(currentFFTSize));

    // Perform FFT
    fft->performFrequencyOnlyForwardTransform(fftInputBuffer.data());

    // Convert to dB and map from linear FFT bins to logarithmic display bins
    float decay = safeGetParam(analyzerDecayParam, 20.0f);
    float decayPerFrame = decay / 30.0f;  // Assuming 30 Hz refresh rate

    auto mode = static_cast<AnalyzerMode>(static_cast<int>(safeGetParam(analyzerModeParam, 0.0f)));

    int numFFTBins = currentFFTSize / 2;
    float binFreqWidth = static_cast<float>(baseSampleRate) / static_cast<float>(currentFFTSize);

    // Logarithmic frequency range for display: 20 Hz to 20 kHz
    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;
    float logMinFreq = std::log10(minFreq);
    float logMaxFreq = std::log10(maxFreq);
    float logRange = logMaxFreq - logMinFreq;

    for (int displayBin = 0; displayBin < 2048; ++displayBin)
    {
        // Map display bin (0-2047) to frequency using logarithmic scale
        float normalizedPos = static_cast<float>(displayBin) / 2047.0f;
        float logFreq = logMinFreq + normalizedPos * logRange;
        float freq = std::pow(10.0f, logFreq);

        // Find corresponding FFT bin (linear frequency mapping)
        float fftBinFloat = freq / binFreqWidth;
        int fftBin = static_cast<int>(fftBinFloat);
        fftBin = juce::jlimit(0, numFFTBins - 1, fftBin);

        // Get magnitude from FFT (the FFT output is already magnitude after performFrequencyOnlyForwardTransform)
        float magnitude = fftInputBuffer[static_cast<size_t>(fftBin)];

        // Normalize by FFT size and convert to dB
        float dB = juce::Decibels::gainToDecibels(magnitude * 2.0f / static_cast<float>(currentFFTSize), -100.0f);

        if (mode == AnalyzerMode::Peak)
        {
            // Peak hold with decay
            if (dB > peakHoldValues[static_cast<size_t>(displayBin)])
                peakHoldValues[static_cast<size_t>(displayBin)] = dB;
            else
                peakHoldValues[static_cast<size_t>(displayBin)] -= decayPerFrame;

            analyzerMagnitudes[static_cast<size_t>(displayBin)] = peakHoldValues[static_cast<size_t>(displayBin)];
        }
        else  // RMS
        {
            // Smoothed averaging
            analyzerMagnitudes[static_cast<size_t>(displayBin)] =
                analyzerMagnitudes[static_cast<size_t>(displayBin)] * 0.9f + dB * 0.1f;
        }
    }

    analyzerDataReady.store(true);
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

        // Slope (for HPF and LPF only)
        if (i == 0 || i == 7)
        {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID(ParamIDs::bandSlope(bandNum), 1),
                "Band " + juce::String(bandNum) + " Slope",
                juce::StringArray{"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"},
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

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamIDs::hqEnabled, 1),
        "HQ Mode (2x Oversampling)",
        false
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
                         "Asymmetric Light", "Asymmetric Medium", "Asymmetric Strong"},
        0
    ));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::eqType, 1),
        "EQ Type",
        juce::StringArray{"Digital", "British", "Tube"},
        0  // Digital by default
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
        juce::StringArray{"±12 dB", "±24 dB", "±30 dB", "±60 dB", "Warped"},
        1  // Default to ±24 dB to match gain range
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

    // Pultec (Tube) mode parameters
    // LF Section
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecLfBoostGain, 1),
        "Pultec LF Boost",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecLfBoostFreq, 1),
        "Pultec LF Boost Freq",
        juce::StringArray{"20 Hz", "30 Hz", "60 Hz", "100 Hz"},
        2  // 60 Hz default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecLfAttenGain, 1),
        "Pultec LF Atten",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));

    // HF Boost Section
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecHfBoostGain, 1),
        "Pultec HF Boost",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecHfBoostFreq, 1),
        "Pultec HF Boost Freq",
        juce::StringArray{"3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz"},
        3  // 8 kHz default
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecHfBoostBandwidth, 1),
        "Pultec HF Bandwidth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f  // Medium bandwidth
    ));

    // HF Atten Section
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecHfAttenGain, 1),
        "Pultec HF Atten",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f
    ));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamIDs::pultecHfAttenFreq, 1),
        "Pultec HF Atten Freq",
        juce::StringArray{"5 kHz", "10 kHz", "20 kHz"},
        1  // 10 kHz default
    ));

    // Global Pultec controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecInputGain, 1),
        "Pultec Input Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecOutputGain, 1),
        "Pultec Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"
    ));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamIDs::pultecTubeDrive, 1),
        "Pultec Tube Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.3f  // Moderate tube warmth by default
    ));

    return {params.begin(), params.end()};
}

//==============================================================================
void MultiQ::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MultiQ::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
        filtersNeedUpdate.store(true);
    }
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

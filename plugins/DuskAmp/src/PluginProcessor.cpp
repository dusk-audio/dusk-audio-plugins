#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

#include <cstring>

juce::AudioProcessorValueTreeState::ParameterLayout DuskAmpProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Amp Mode (DSP / NAM)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { DuskAmpParams::AMP_MODE, 1 }, "Amp Mode",
        juce::StringArray { "DSP", "NAM" }, 0));

    // Amp Model (Round / Chime / Punch)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { DuskAmpParams::AMP_MODEL, 1 }, "Amp Model",
        juce::StringArray { "Blackface", "British Combo", "Plexi" }, 0));

    // Input
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::INPUT_GAIN, 1 }, "Input Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::GATE_THRESHOLD, 1 }, "Gate Threshold",
        juce::NormalisableRange<float> (-80.0f, 0.0f, 0.0f, 0.5f), -60.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::GATE_RELEASE, 1 }, "Gate Release",
        juce::NormalisableRange<float> (5.0f, 500.0f, 0.0f, 0.5f), 50.0f));

    // Drive (single knob replaces preamp gain + power drive)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::DRIVE, 1 }, "Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    // Tone Stack
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::BASS, 1 }, "Bass",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::MID, 1 }, "Mid",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::TREBLE, 1 }, "Treble",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    // Power Amp
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::PRESENCE, 1 }, "Presence",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::RESONANCE, 1 }, "Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::POWER_AMP_ENABLED, 1 }, "Power Amp", true));

    // Cabinet
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::CAB_ENABLED, 1 }, "Cabinet", true));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::CAB_MIX, 1 }, "Cab Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::CAB_HICUT, 1 }, "Cab Hi Cut",
        juce::NormalisableRange<float> (1000.0f, 20000.0f, 0.0f, 0.5f), 12000.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::CAB_LOCUT, 1 }, "Cab Lo Cut",
        juce::NormalisableRange<float> (20.0f, 500.0f, 0.0f, 0.5f), 60.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::CAB_AUTOGAIN, 1 }, "Cab Auto Gain", false));

    // Effects - Delay
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::DELAY_ENABLED, 1 }, "Delay", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::DELAY_TIME, 1 }, "Delay Time",
        juce::NormalisableRange<float> (1.0f, 2000.0f, 0.0f, 0.4f), 350.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::DELAY_FEEDBACK, 1 }, "Delay Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.3f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::DELAY_MIX, 1 }, "Delay Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.2f));

    // Effects - Reverb
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::REVERB_ENABLED, 1 }, "Reverb", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::REVERB_MIX, 1 }, "Reverb Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::REVERB_DECAY, 1 }, "Reverb Decay",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    // NAM levels (only active in NAM mode)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::NAM_INPUT_LEVEL, 1 }, "NAM Input",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::NAM_OUTPUT_LEVEL, 1 }, "NAM Output",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));

    // Output
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::OUTPUT_LEVEL, 1 }, "Output Level",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.0f, 1.0f), 0.0f));

    // Global
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { DuskAmpParams::OVERSAMPLING, 1 }, "Oversampling",
        juce::StringArray { "2x", "4x", "8x" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::BYPASS, 1 }, "Bypass", false));

    return layout;
}

DuskAmpProcessor::DuskAmpProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("DuskAmp"), createParameterLayout())
{
    // Discrete / choice params
    ampModeParam_       = parameters.getRawParameterValue (DuskAmpParams::AMP_MODE);
    ampModelParam_      = parameters.getRawParameterValue (DuskAmpParams::AMP_MODEL);
    oversamplingParam_  = parameters.getRawParameterValue (DuskAmpParams::OVERSAMPLING);
    cabEnabledParam_    = parameters.getRawParameterValue (DuskAmpParams::CAB_ENABLED);
    powerAmpEnabledParam_ = parameters.getRawParameterValue (DuskAmpParams::POWER_AMP_ENABLED);
    delayEnabledParam_  = parameters.getRawParameterValue (DuskAmpParams::DELAY_ENABLED);
    reverbEnabledParam_ = parameters.getRawParameterValue (DuskAmpParams::REVERB_ENABLED);

    // Continuous float params
    inputGainParam_     = parameters.getRawParameterValue (DuskAmpParams::INPUT_GAIN);
    gateThresholdParam_ = parameters.getRawParameterValue (DuskAmpParams::GATE_THRESHOLD);
    gateReleaseParam_   = parameters.getRawParameterValue (DuskAmpParams::GATE_RELEASE);
    driveParam_         = parameters.getRawParameterValue (DuskAmpParams::DRIVE);
    bassParam_          = parameters.getRawParameterValue (DuskAmpParams::BASS);
    midParam_           = parameters.getRawParameterValue (DuskAmpParams::MID);
    trebleParam_        = parameters.getRawParameterValue (DuskAmpParams::TREBLE);
    presenceParam_      = parameters.getRawParameterValue (DuskAmpParams::PRESENCE);
    resonanceParam_     = parameters.getRawParameterValue (DuskAmpParams::RESONANCE);
    cabMixParam_        = parameters.getRawParameterValue (DuskAmpParams::CAB_MIX);
    cabHiCutParam_      = parameters.getRawParameterValue (DuskAmpParams::CAB_HICUT);
    cabLoCutParam_      = parameters.getRawParameterValue (DuskAmpParams::CAB_LOCUT);
    cabAutoGainParam_   = parameters.getRawParameterValue (DuskAmpParams::CAB_AUTOGAIN);
    delayTimeParam_     = parameters.getRawParameterValue (DuskAmpParams::DELAY_TIME);
    delayFeedbackParam_ = parameters.getRawParameterValue (DuskAmpParams::DELAY_FEEDBACK);
    delayMixParam_      = parameters.getRawParameterValue (DuskAmpParams::DELAY_MIX);
    reverbMixParam_     = parameters.getRawParameterValue (DuskAmpParams::REVERB_MIX);
    reverbDecayParam_   = parameters.getRawParameterValue (DuskAmpParams::REVERB_DECAY);
    namInputLevelParam_ = parameters.getRawParameterValue (DuskAmpParams::NAM_INPUT_LEVEL);
    namOutputLevelParam_= parameters.getRawParameterValue (DuskAmpParams::NAM_OUTPUT_LEVEL);
    outputLevelParam_   = parameters.getRawParameterValue (DuskAmpParams::OUTPUT_LEVEL);

    bypassParam_ = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (DuskAmpParams::BYPASS));
}

bool DuskAmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto inputSet  = layouts.getMainInputChannelSet();
    auto outputSet = layouts.getMainOutputChannelSet();

    if (outputSet != juce::AudioChannelSet::stereo())
        return false;

    return inputSet == juce::AudioChannelSet::mono()
        || inputSet == juce::AudioChannelSet::stereo();
}

void DuskAmpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine_.getLatencyInSamples());

    // Bypass latency compensation: allocate delay buffer for max possible latency.
    // Always size for 8x oversampling so switching oversampling at runtime is safe.
    int maxLatency = engine_.getMaxLatencyInSamples() + 64;
    bypassDelayL_.resize (static_cast<size_t> (maxLatency), 0.0f);
    bypassDelayR_.resize (static_cast<size_t> (maxLatency), 0.0f);
    bypassDelayWritePos_ = 0;
    bypassDelaySamples_ = engine_.getLatencyInSamples();

    // Initialize discrete params from saved state
    cachedAmpMode_       = static_cast<int> (ampModeParam_->load());
    cachedAmpModel_      = static_cast<int> (ampModelParam_->load());
    cachedOversampling_  = static_cast<int> (oversamplingParam_->load());
    cachedCabEnabled_    = cabEnabledParam_->load() >= 0.5f;
    cachedPowerAmpEnabled_ = powerAmpEnabledParam_->load() >= 0.5f;
    cachedDelayEnabled_  = delayEnabledParam_->load() >= 0.5f;
    cachedReverbEnabled_ = reverbEnabledParam_->load() >= 0.5f;

    engine_.setAmpMode (static_cast<DuskAmpEngine::AmpMode> (cachedAmpMode_));
    engine_.setAmpModel (cachedAmpModel_);
    engine_.setOversamplingFactor (cachedOversampling_ >= 2 ? 8 : cachedOversampling_ >= 1 ? 4 : 2);
    engine_.setCabinetEnabled (cachedCabEnabled_);
    engine_.setPowerAmpEnabled (cachedPowerAmpEnabled_);
    engine_.setDelayEnabled (cachedDelayEnabled_);
    engine_.setReverbEnabled (cachedReverbEnabled_);

    auto rampSamples = static_cast<double> (kSmoothingBlockSize);

    inputGainSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    gateThresholdSmooth_.reset (sampleRate, rampSamples / sampleRate);
    gateReleaseSmooth_  .reset (sampleRate, rampSamples / sampleRate);
    driveSmooth_        .reset (sampleRate, rampSamples / sampleRate);
    bassSmooth_         .reset (sampleRate, rampSamples / sampleRate);
    midSmooth_          .reset (sampleRate, rampSamples / sampleRate);
    trebleSmooth_       .reset (sampleRate, rampSamples / sampleRate);
    presenceSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    resonanceSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    cabMixSmooth_       .reset (sampleRate, rampSamples / sampleRate);
    cabHiCutSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    cabLoCutSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    delayTimeSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    delayFeedbackSmooth_.reset (sampleRate, rampSamples / sampleRate);
    delayMixSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    reverbMixSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    reverbDecaySmooth_  .reset (sampleRate, rampSamples / sampleRate);
    namInputLevelSmooth_.reset (sampleRate, rampSamples / sampleRate);
    namOutputLevelSmooth_.reset (sampleRate, rampSamples / sampleRate);
    outputLevelSmooth_  .reset (sampleRate, rampSamples / sampleRate);

    inputGainSmooth_    .setCurrentAndTargetValue (inputGainParam_->load());
    gateThresholdSmooth_.setCurrentAndTargetValue (gateThresholdParam_->load());
    gateReleaseSmooth_  .setCurrentAndTargetValue (gateReleaseParam_->load());
    driveSmooth_        .setCurrentAndTargetValue (driveParam_->load());
    bassSmooth_         .setCurrentAndTargetValue (bassParam_->load());
    midSmooth_          .setCurrentAndTargetValue (midParam_->load());
    trebleSmooth_       .setCurrentAndTargetValue (trebleParam_->load());
    presenceSmooth_     .setCurrentAndTargetValue (presenceParam_->load());
    resonanceSmooth_    .setCurrentAndTargetValue (resonanceParam_->load());
    cabMixSmooth_       .setCurrentAndTargetValue (cabMixParam_->load());
    cabHiCutSmooth_     .setCurrentAndTargetValue (cabHiCutParam_->load());
    cabLoCutSmooth_     .setCurrentAndTargetValue (cabLoCutParam_->load());
    delayTimeSmooth_    .setCurrentAndTargetValue (delayTimeParam_->load());
    delayFeedbackSmooth_.setCurrentAndTargetValue (delayFeedbackParam_->load());
    delayMixSmooth_     .setCurrentAndTargetValue (delayMixParam_->load());
    reverbMixSmooth_    .setCurrentAndTargetValue (reverbMixParam_->load());
    reverbDecaySmooth_  .setCurrentAndTargetValue (reverbDecayParam_->load());
    namInputLevelSmooth_.setCurrentAndTargetValue (namInputLevelParam_->load());
    namOutputLevelSmooth_.setCurrentAndTargetValue (namOutputLevelParam_->load());
    outputLevelSmooth_  .setCurrentAndTargetValue (outputLevelParam_->load());
}

void DuskAmpProcessor::releaseResources()
{
}

void DuskAmpProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    int numSamples = buffer.getNumSamples();

    // Clear unused output channels
    for (int ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Bypass: pass input through with latency compensation
    // (host has already compensated for our reported latency, so we must delay
    // the bypassed signal to maintain phase coherence with parallel tracks)
    if (bypassParam_ != nullptr && bypassParam_->get())
    {
        if (totalNumInputChannels == 1 && totalNumOutputChannels == 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

        if (bypassDelaySamples_ > 0 && ! bypassDelayL_.empty())
        {
            int delaySize = static_cast<int> (bypassDelayL_.size());
            float* left  = buffer.getWritePointer (0);
            float* right = totalNumOutputChannels > 1 ? buffer.getWritePointer (1) : left;

            for (int i = 0; i < numSamples; ++i)
            {
                // Write current sample
                bypassDelayL_[static_cast<size_t> (bypassDelayWritePos_)] = left[i];
                bypassDelayR_[static_cast<size_t> (bypassDelayWritePos_)] = right[i];

                // Read delayed sample
                int readPos = bypassDelayWritePos_ - bypassDelaySamples_;
                if (readPos < 0) readPos += delaySize;

                left[i]  = bypassDelayL_[static_cast<size_t> (readPos)];
                right[i] = bypassDelayR_[static_cast<size_t> (readPos)];

                bypassDelayWritePos_ = (bypassDelayWritePos_ + 1) % delaySize;
            }
        }
        return;
    }

    // Measure input level from channel 0 BEFORE any processing touches it.
    // Use getReadPointer to avoid any aliasing issues.
    {
        const float* rawInput = buffer.getReadPointer (0);
        float pk = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            pk = std::max (pk, std::abs (rawInput[i]));
        // Gate at -60dB — the LED meter's minimum display threshold.
        // Anything below this is interface noise, not real signal.
        float lvl = pk > 1.0e-3f ? juce::Decibels::gainToDecibels (pk) : -100.0f;
        inputLevelL_.store (lvl, std::memory_order_relaxed);
        inputLevelR_.store (lvl, std::memory_order_relaxed);
    }

    // Duplicate mono input to channel 1 so engine gets stereo
    if (totalNumInputChannels == 1 && totalNumOutputChannels == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (std::min (1, totalNumOutputChannels - 1));

    // Check discrete params for changes
    int ampMode = static_cast<int> (ampModeParam_->load());
    if (ampMode != cachedAmpMode_)
    {
        cachedAmpMode_ = ampMode;
        engine_.setAmpMode (static_cast<DuskAmpEngine::AmpMode> (ampMode));
    }

    int ampModel = static_cast<int> (ampModelParam_->load());
    if (ampModel != cachedAmpModel_)
    {
        cachedAmpModel_ = ampModel;
        engine_.setAmpModel (ampModel);
    }

    int oversampling = static_cast<int> (oversamplingParam_->load());
    if (oversampling != cachedOversampling_)
    {
        cachedOversampling_ = oversampling;
        engine_.setOversamplingFactor (oversampling >= 2 ? 8 : oversampling >= 1 ? 4 : 2);
        setLatencySamples (engine_.getLatencyInSamples());
        bypassDelaySamples_ = engine_.getLatencyInSamples();
    }

    bool cabEnabled = cabEnabledParam_->load() >= 0.5f;
    if (cabEnabled != cachedCabEnabled_)
    {
        cachedCabEnabled_ = cabEnabled;
        engine_.setCabinetEnabled (cabEnabled);
    }

    bool cabAutoGain = cabAutoGainParam_->load() >= 0.5f;
    if (cabAutoGain != cachedCabAutoGain_)
    {
        cachedCabAutoGain_ = cabAutoGain;
        engine_.setCabinetAutoGain (cabAutoGain);
    }

    bool powerAmpEnabled = powerAmpEnabledParam_->load() >= 0.5f;
    if (powerAmpEnabled != cachedPowerAmpEnabled_)
    {
        cachedPowerAmpEnabled_ = powerAmpEnabled;
        engine_.setPowerAmpEnabled (powerAmpEnabled);
    }

    bool delayEnabled = delayEnabledParam_->load() >= 0.5f;
    if (delayEnabled != cachedDelayEnabled_)
    {
        cachedDelayEnabled_ = delayEnabled;
        engine_.setDelayEnabled (delayEnabled);
    }

    bool reverbEnabled = reverbEnabledParam_->load() >= 0.5f;
    if (reverbEnabled != cachedReverbEnabled_)
    {
        cachedReverbEnabled_ = reverbEnabled;
        engine_.setReverbEnabled (reverbEnabled);
    }

    // Set smoothing targets
    inputGainSmooth_    .setTargetValue (inputGainParam_->load());
    gateThresholdSmooth_.setTargetValue (gateThresholdParam_->load());
    gateReleaseSmooth_  .setTargetValue (gateReleaseParam_->load());
    driveSmooth_        .setTargetValue (driveParam_->load());
    bassSmooth_         .setTargetValue (bassParam_->load());
    midSmooth_          .setTargetValue (midParam_->load());
    trebleSmooth_       .setTargetValue (trebleParam_->load());
    presenceSmooth_     .setTargetValue (presenceParam_->load());
    resonanceSmooth_    .setTargetValue (resonanceParam_->load());
    cabMixSmooth_       .setTargetValue (cabMixParam_->load());
    cabHiCutSmooth_     .setTargetValue (cabHiCutParam_->load());
    cabLoCutSmooth_     .setTargetValue (cabLoCutParam_->load());
    delayTimeSmooth_    .setTargetValue (delayTimeParam_->load());
    delayFeedbackSmooth_.setTargetValue (delayFeedbackParam_->load());
    delayMixSmooth_     .setTargetValue (delayMixParam_->load());
    reverbMixSmooth_    .setTargetValue (reverbMixParam_->load());
    reverbDecaySmooth_  .setTargetValue (reverbDecayParam_->load());
    namInputLevelSmooth_.setTargetValue (namInputLevelParam_->load());
    namOutputLevelSmooth_.setTargetValue (namOutputLevelParam_->load());
    outputLevelSmooth_  .setTargetValue (outputLevelParam_->load());

    // Sub-block processing for smooth parameter transitions
    int samplesRemaining = numSamples;
    int offset = 0;

    while (samplesRemaining > 0)
    {
        int blockSize = std::min (samplesRemaining, kSmoothingBlockSize);

        engine_.setInputGain     (inputGainSmooth_.skip (blockSize));
        engine_.setGateThreshold (gateThresholdSmooth_.skip (blockSize));
        engine_.setGateRelease   (gateReleaseSmooth_.skip (blockSize));
        engine_.setDrive          (driveSmooth_.skip (blockSize));
        engine_.setBass           (bassSmooth_.skip (blockSize));
        engine_.setMid            (midSmooth_.skip (blockSize));
        engine_.setTreble         (trebleSmooth_.skip (blockSize));
        engine_.setPresence       (presenceSmooth_.skip (blockSize));
        engine_.setResonance      (resonanceSmooth_.skip (blockSize));
        engine_.setCabinetMix     (cabMixSmooth_.skip (blockSize));
        engine_.setCabinetHiCut   (cabHiCutSmooth_.skip (blockSize));
        engine_.setCabinetLoCut   (cabLoCutSmooth_.skip (blockSize));
        engine_.setDelayTime      (delayTimeSmooth_.skip (blockSize));
        engine_.setDelayFeedback  (delayFeedbackSmooth_.skip (blockSize));
        engine_.setDelayMix       (delayMixSmooth_.skip (blockSize));
        engine_.setReverbMix      (reverbMixSmooth_.skip (blockSize));
        engine_.setReverbDecay    (reverbDecaySmooth_.skip (blockSize));

#if DUSKAMP_NAM_SUPPORT
        engine_.setNAMInputLevel  (namInputLevelSmooth_.skip (blockSize));
        engine_.setNAMOutputLevel (namOutputLevelSmooth_.skip (blockSize));
#else
        namInputLevelSmooth_.skip (blockSize);
        namOutputLevelSmooth_.skip (blockSize);
#endif
        engine_.setOutputLevel    (outputLevelSmooth_.skip (blockSize));

        engine_.process (left + offset, right + offset, blockSize);

        offset += blockSize;
        samplesRemaining -= blockSize;
    }

    // Update sag level for UI indicator
    sagLevel_.store (engine_.getSagLevel(), std::memory_order_relaxed);

    // Measure output levels (with noise floor gate at -90dB)
    {
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        constexpr float kNoiseFloor = 1e-7f;  // -140dB (effectively no gate on output)
        outputLevelL_.store (peakL > kNoiseFloor ? juce::Decibels::gainToDecibels (peakL) : -100.0f,
                             std::memory_order_relaxed);
        outputLevelR_.store (peakR > kNoiseFloor ? juce::Decibels::gainToDecibels (peakR) : -100.0f,
                             std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* DuskAmpProcessor::createEditor()
{
    return new DuskAmpEditor (*this);
}

void DuskAmpProcessor::loadNAMModel (const juce::File& file)
{
#if DUSKAMP_NAM_SUPPORT
    lastNAMStatus_ = "loading: " + file.getFileName();
    auto& nam = engine_.getNAMProcessor();
    if (nam.loadModel (file))
    {
        namModelPath_ = file.getFullPathName();
        lastNAMStatus_ = "OK: " + file.getFileNameWithoutExtension();
    }
    else
    {
        lastNAMStatus_ = "FAIL: " + nam.getLastError();
    }
#else
    juce::ignoreUnused (file);
    lastNAMStatus_ = "NAM not compiled";
#endif
}

void DuskAmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("cabIRPath", cabIRPath_, nullptr);
    state.setProperty ("namModelPath", namModelPath_, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DuskAmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        setStateXML (*xml);
}

std::unique_ptr<juce::XmlElement> DuskAmpProcessor::getStateXML()
{
    auto state = parameters.copyState();
    state.setProperty ("cabIRPath", cabIRPath_, nullptr);
    state.setProperty ("namModelPath", namModelPath_, nullptr);
    return state.createXml();
}

void DuskAmpProcessor::setStateXML (const juce::XmlElement& xml)
{
    if (xml.hasTagName (parameters.state.getType()))
    {
        parameters.replaceState (juce::ValueTree::fromXml (xml));

        cabIRPath_ = parameters.state.getProperty ("cabIRPath", "").toString();
        if (cabIRPath_.isNotEmpty())
        {
            juce::File cabFile (cabIRPath_);
            if (cabFile.existsAsFile())
                engine_.getCabinetIR().loadIR (cabFile);
        }

        namModelPath_ = parameters.state.getProperty ("namModelPath", "").toString();
        if (namModelPath_.isNotEmpty())
        {
            juce::File namFile (namModelPath_);
            // loadNAMModel calls NAMProcessor::loadModel which sets lastError_
            // on failure (including "file not found"), so the editor shows feedback
            loadNAMModel (namFile);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskAmpProcessor();
}

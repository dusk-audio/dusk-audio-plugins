#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

#include <cstring>

juce::AudioProcessorValueTreeState::ParameterLayout DuskAmpProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Amp Mode
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { DuskAmpParams::AMP_MODE, 1 }, "Amp Mode",
        juce::StringArray { "DSP", "NAM" }, 0));

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

    // Amp Type (selects preamp model + tone stack topology)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { DuskAmpParams::AMP_TYPE, 1 }, "Amp Type",
        juce::StringArray { "Fender", "Marshall", "Vox" }, 1));

    // Preamp
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::PREAMP_GAIN, 1 }, "Preamp Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::PREAMP_BRIGHT, 1 }, "Bright", false));

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
        juce::ParameterID { DuskAmpParams::POWER_DRIVE, 1 }, "Power Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::PRESENCE, 1 }, "Presence",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::RESONANCE, 1 }, "Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::SAG, 1 }, "Sag",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f));

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

    // Stomp Box (Boost Pedal)
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::BOOST_ENABLED, 1 }, "Boost", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::BOOST_GAIN, 1 }, "Boost Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::BOOST_TONE, 1 }, "Boost Tone",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::BOOST_LEVEL, 1 }, "Boost Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    // Effects - Delay
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::DELAY_ENABLED, 1 }, "Delay", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { DuskAmpParams::DELAY_TYPE, 1 }, "Delay Type",
        juce::StringArray { "Digital", "Analog", "Tape" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::DELAY_TIME, 1 }, "Delay Time",
        juce::NormalisableRange<float> (1.0f, 2000.0f, 0.0f, 0.4f), 350.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::DELAY_FEEDBACK, 1 }, "Delay Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.3f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::DELAY_MIX, 1 }, "Delay Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.2f));

    // Effects - Reverb (Dattorro plate)
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { DuskAmpParams::REVERB_ENABLED, 1 }, "Reverb", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::REVERB_MIX, 1 }, "Reverb Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::REVERB_DECAY, 1 }, "Reverb Decay",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::REVERB_PREDELAY, 1 }, "Reverb Pre-Delay",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.0f, 0.5f), 20.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::REVERB_DAMPING, 1 }, "Reverb Damping",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::REVERB_SIZE, 1 }, "Reverb Size",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    // Output
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { DuskAmpParams::OUTPUT_LEVEL, 1 }, "Output Level",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.0f, 1.0f), 0.0f));

    // Global
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { DuskAmpParams::OVERSAMPLING, 1 }, "Oversampling",
        juce::StringArray { "2x", "4x" }, 0));

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
    ampTypeParam_       = parameters.getRawParameterValue (DuskAmpParams::AMP_TYPE);
    oversamplingParam_  = parameters.getRawParameterValue (DuskAmpParams::OVERSAMPLING);
    cabEnabledParam_    = parameters.getRawParameterValue (DuskAmpParams::CAB_ENABLED);
    brightParam_        = parameters.getRawParameterValue (DuskAmpParams::PREAMP_BRIGHT);
    boostEnabledParam_  = parameters.getRawParameterValue (DuskAmpParams::BOOST_ENABLED);
    delayEnabledParam_  = parameters.getRawParameterValue (DuskAmpParams::DELAY_ENABLED);
    delayTypeParam_     = parameters.getRawParameterValue (DuskAmpParams::DELAY_TYPE);
    reverbEnabledParam_ = parameters.getRawParameterValue (DuskAmpParams::REVERB_ENABLED);

    // Continuous float params
    inputGainParam_      = parameters.getRawParameterValue (DuskAmpParams::INPUT_GAIN);
    gateThresholdParam_  = parameters.getRawParameterValue (DuskAmpParams::GATE_THRESHOLD);
    gateReleaseParam_    = parameters.getRawParameterValue (DuskAmpParams::GATE_RELEASE);
    preampGainParam_     = parameters.getRawParameterValue (DuskAmpParams::PREAMP_GAIN);
    bassParam_           = parameters.getRawParameterValue (DuskAmpParams::BASS);
    midParam_            = parameters.getRawParameterValue (DuskAmpParams::MID);
    trebleParam_         = parameters.getRawParameterValue (DuskAmpParams::TREBLE);
    powerDriveParam_     = parameters.getRawParameterValue (DuskAmpParams::POWER_DRIVE);
    presenceParam_       = parameters.getRawParameterValue (DuskAmpParams::PRESENCE);
    resonanceParam_      = parameters.getRawParameterValue (DuskAmpParams::RESONANCE);
    sagParam_            = parameters.getRawParameterValue (DuskAmpParams::SAG);
    cabMixParam_         = parameters.getRawParameterValue (DuskAmpParams::CAB_MIX);
    cabHiCutParam_       = parameters.getRawParameterValue (DuskAmpParams::CAB_HICUT);
    cabLoCutParam_       = parameters.getRawParameterValue (DuskAmpParams::CAB_LOCUT);
    boostGainParam_      = parameters.getRawParameterValue (DuskAmpParams::BOOST_GAIN);
    boostToneParam_      = parameters.getRawParameterValue (DuskAmpParams::BOOST_TONE);
    boostLevelParam_     = parameters.getRawParameterValue (DuskAmpParams::BOOST_LEVEL);
    delayTimeParam_      = parameters.getRawParameterValue (DuskAmpParams::DELAY_TIME);
    delayFeedbackParam_  = parameters.getRawParameterValue (DuskAmpParams::DELAY_FEEDBACK);
    delayMixParam_       = parameters.getRawParameterValue (DuskAmpParams::DELAY_MIX);
    reverbMixParam_      = parameters.getRawParameterValue (DuskAmpParams::REVERB_MIX);
    reverbDecayParam_    = parameters.getRawParameterValue (DuskAmpParams::REVERB_DECAY);
    reverbPreDelayParam_ = parameters.getRawParameterValue (DuskAmpParams::REVERB_PREDELAY);
    reverbDampingParam_  = parameters.getRawParameterValue (DuskAmpParams::REVERB_DAMPING);
    reverbSizeParam_     = parameters.getRawParameterValue (DuskAmpParams::REVERB_SIZE);
    outputLevelParam_    = parameters.getRawParameterValue (DuskAmpParams::OUTPUT_LEVEL);

    bypassParam_ = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (DuskAmpParams::BYPASS));
}

bool DuskAmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto outputSet = layouts.getMainOutputChannelSet();

    if (outputSet != juce::AudioChannelSet::stereo())
        return false;

    auto inputSet = layouts.getMainInputChannelSet();

    return inputSet == juce::AudioChannelSet::mono()
        || inputSet == juce::AudioChannelSet::stereo();
}

void DuskAmpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine_.getLatencyInSamples());

    // Initialize discrete params from saved state
    cachedAmpMode_       = static_cast<int> (ampModeParam_->load());
    cachedAmpType_       = static_cast<int> (ampTypeParam_->load());
    cachedOversampling_  = static_cast<int> (oversamplingParam_->load());
    cachedCabEnabled_    = cabEnabledParam_->load() >= 0.5f;
    cachedBright_        = brightParam_->load() >= 0.5f;
    cachedBoostEnabled_  = boostEnabledParam_->load() >= 0.5f;
    cachedDelayEnabled_  = delayEnabledParam_->load() >= 0.5f;
    cachedDelayType_     = static_cast<int> (delayTypeParam_->load());
    cachedReverbEnabled_ = reverbEnabledParam_->load() >= 0.5f;

    engine_.setAmpType (cachedAmpType_);
    engine_.setAmpMode (static_cast<DuskAmpEngine::AmpMode> (cachedAmpMode_));
    engine_.setOversamplingFactor (cachedOversampling_ >= 1 ? 4 : 2);
    engine_.setCabinetEnabled (cachedCabEnabled_);
    engine_.setPreampBright (cachedBright_);
    engine_.setBoostEnabled (cachedBoostEnabled_);
    engine_.setDelayEnabled (cachedDelayEnabled_);
    engine_.setDelayType (cachedDelayType_);
    engine_.setReverbEnabled (cachedReverbEnabled_);

    auto rampSamples = static_cast<double> (kSmoothingBlockSize);

    inputGainSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    gateThresholdSmooth_.reset (sampleRate, rampSamples / sampleRate);
    gateReleaseSmooth_  .reset (sampleRate, rampSamples / sampleRate);
    preampGainSmooth_   .reset (sampleRate, rampSamples / sampleRate);
    bassSmooth_         .reset (sampleRate, rampSamples / sampleRate);
    midSmooth_          .reset (sampleRate, rampSamples / sampleRate);
    trebleSmooth_       .reset (sampleRate, rampSamples / sampleRate);
    powerDriveSmooth_   .reset (sampleRate, rampSamples / sampleRate);
    presenceSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    resonanceSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    sagSmooth_          .reset (sampleRate, rampSamples / sampleRate);
    cabMixSmooth_       .reset (sampleRate, rampSamples / sampleRate);
    cabHiCutSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    cabLoCutSmooth_      .reset (sampleRate, rampSamples / sampleRate);
    boostGainSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    boostToneSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    boostLevelSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    delayTimeSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    delayFeedbackSmooth_ .reset (sampleRate, rampSamples / sampleRate);
    delayMixSmooth_      .reset (sampleRate, rampSamples / sampleRate);
    reverbMixSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    reverbDecaySmooth_   .reset (sampleRate, rampSamples / sampleRate);
    reverbPreDelaySmooth_.reset (sampleRate, rampSamples / sampleRate);
    reverbDampingSmooth_ .reset (sampleRate, rampSamples / sampleRate);
    reverbSizeSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    outputLevelSmooth_   .reset (sampleRate, rampSamples / sampleRate);

    inputGainSmooth_     .setCurrentAndTargetValue (inputGainParam_->load());
    gateThresholdSmooth_ .setCurrentAndTargetValue (gateThresholdParam_->load());
    gateReleaseSmooth_   .setCurrentAndTargetValue (gateReleaseParam_->load());
    preampGainSmooth_    .setCurrentAndTargetValue (preampGainParam_->load());
    bassSmooth_          .setCurrentAndTargetValue (bassParam_->load());
    midSmooth_           .setCurrentAndTargetValue (midParam_->load());
    trebleSmooth_        .setCurrentAndTargetValue (trebleParam_->load());
    powerDriveSmooth_    .setCurrentAndTargetValue (powerDriveParam_->load());
    presenceSmooth_      .setCurrentAndTargetValue (presenceParam_->load());
    resonanceSmooth_     .setCurrentAndTargetValue (resonanceParam_->load());
    sagSmooth_           .setCurrentAndTargetValue (sagParam_->load());
    cabMixSmooth_        .setCurrentAndTargetValue (cabMixParam_->load());
    cabHiCutSmooth_      .setCurrentAndTargetValue (cabHiCutParam_->load());
    cabLoCutSmooth_      .setCurrentAndTargetValue (cabLoCutParam_->load());
    boostGainSmooth_     .setCurrentAndTargetValue (boostGainParam_->load());
    boostToneSmooth_     .setCurrentAndTargetValue (boostToneParam_->load());
    boostLevelSmooth_    .setCurrentAndTargetValue (boostLevelParam_->load());
    delayTimeSmooth_     .setCurrentAndTargetValue (delayTimeParam_->load());
    delayFeedbackSmooth_ .setCurrentAndTargetValue (delayFeedbackParam_->load());
    delayMixSmooth_      .setCurrentAndTargetValue (delayMixParam_->load());
    reverbMixSmooth_     .setCurrentAndTargetValue (reverbMixParam_->load());
    reverbDecaySmooth_   .setCurrentAndTargetValue (reverbDecayParam_->load());
    reverbPreDelaySmooth_.setCurrentAndTargetValue (reverbPreDelayParam_->load());
    reverbDampingSmooth_ .setCurrentAndTargetValue (reverbDampingParam_->load());
    reverbSizeSmooth_    .setCurrentAndTargetValue (reverbSizeParam_->load());
    outputLevelSmooth_   .setCurrentAndTargetValue (outputLevelParam_->load());
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

    // Clear any unused output channels
    for (int ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Bypass: pass audio through unprocessed
    if (bypassParam_ != nullptr && bypassParam_->get())
        return;

    // Handle mono input: duplicate channel 0 to channel 1
    if (totalNumInputChannels == 1 && totalNumOutputChannels == 2)
    {
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    }

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    // Measure input levels
    {
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        inputLevelL_.store (peakL > 0.0f ? juce::Decibels::gainToDecibels (peakL) : -100.0f,
                            std::memory_order_relaxed);
        inputLevelR_.store (peakR > 0.0f ? juce::Decibels::gainToDecibels (peakR) : -100.0f,
                            std::memory_order_relaxed);
    }

    // Check discrete params for changes
    int ampMode = static_cast<int> (ampModeParam_->load());
    if (ampMode != cachedAmpMode_)
    {
        cachedAmpMode_ = ampMode;
        engine_.setAmpMode (static_cast<DuskAmpEngine::AmpMode> (ampMode));
    }

    int ampType = static_cast<int> (ampTypeParam_->load());
    if (ampType != cachedAmpType_)
    {
        cachedAmpType_ = ampType;
        engine_.setAmpType (ampType);
    }

    int oversampling = static_cast<int> (oversamplingParam_->load());
    if (oversampling != cachedOversampling_)
    {
        cachedOversampling_ = oversampling;
        engine_.setOversamplingFactor (oversampling >= 1 ? 4 : 2);
        setLatencySamples (engine_.getLatencyInSamples());
    }

    bool cabEnabled = cabEnabledParam_->load() >= 0.5f;
    if (cabEnabled != cachedCabEnabled_)
    {
        cachedCabEnabled_ = cabEnabled;
        engine_.setCabinetEnabled (cabEnabled);
    }

    bool bright = brightParam_->load() >= 0.5f;
    if (bright != cachedBright_)
    {
        cachedBright_ = bright;
        engine_.setPreampBright (bright);
    }

    bool boostEnabled = boostEnabledParam_->load() >= 0.5f;
    if (boostEnabled != cachedBoostEnabled_)
    {
        cachedBoostEnabled_ = boostEnabled;
        engine_.setBoostEnabled (boostEnabled);
    }

    bool delayEnabled = delayEnabledParam_->load() >= 0.5f;
    if (delayEnabled != cachedDelayEnabled_)
    {
        cachedDelayEnabled_ = delayEnabled;
        engine_.setDelayEnabled (delayEnabled);
    }

    int delayType = static_cast<int> (delayTypeParam_->load());
    if (delayType != cachedDelayType_)
    {
        cachedDelayType_ = delayType;
        engine_.setDelayType (delayType);
    }

    bool reverbEnabled = reverbEnabledParam_->load() >= 0.5f;
    if (reverbEnabled != cachedReverbEnabled_)
    {
        cachedReverbEnabled_ = reverbEnabled;
        engine_.setReverbEnabled (reverbEnabled);
    }

    // Set smoothing targets from current parameter values
    inputGainSmooth_     .setTargetValue (inputGainParam_->load());
    gateThresholdSmooth_ .setTargetValue (gateThresholdParam_->load());
    gateReleaseSmooth_   .setTargetValue (gateReleaseParam_->load());
    preampGainSmooth_    .setTargetValue (preampGainParam_->load());
    bassSmooth_          .setTargetValue (bassParam_->load());
    midSmooth_           .setTargetValue (midParam_->load());
    trebleSmooth_        .setTargetValue (trebleParam_->load());
    powerDriveSmooth_    .setTargetValue (powerDriveParam_->load());
    presenceSmooth_      .setTargetValue (presenceParam_->load());
    resonanceSmooth_     .setTargetValue (resonanceParam_->load());
    sagSmooth_           .setTargetValue (sagParam_->load());
    cabMixSmooth_        .setTargetValue (cabMixParam_->load());
    cabHiCutSmooth_      .setTargetValue (cabHiCutParam_->load());
    cabLoCutSmooth_      .setTargetValue (cabLoCutParam_->load());
    boostGainSmooth_     .setTargetValue (boostGainParam_->load());
    boostToneSmooth_     .setTargetValue (boostToneParam_->load());
    boostLevelSmooth_    .setTargetValue (boostLevelParam_->load());
    delayTimeSmooth_     .setTargetValue (delayTimeParam_->load());
    delayFeedbackSmooth_ .setTargetValue (delayFeedbackParam_->load());
    delayMixSmooth_      .setTargetValue (delayMixParam_->load());
    reverbMixSmooth_     .setTargetValue (reverbMixParam_->load());
    reverbDecaySmooth_   .setTargetValue (reverbDecayParam_->load());
    reverbPreDelaySmooth_.setTargetValue (reverbPreDelayParam_->load());
    reverbDampingSmooth_ .setTargetValue (reverbDampingParam_->load());
    reverbSizeSmooth_    .setTargetValue (reverbSizeParam_->load());
    outputLevelSmooth_   .setTargetValue (outputLevelParam_->load());

    // Sub-block processing for smooth parameter transitions
    int samplesRemaining = numSamples;
    int offset = 0;

    while (samplesRemaining > 0)
    {
        int blockSize = std::min (samplesRemaining, kSmoothingBlockSize);

        // Advance smoothed values and apply to engine
        engine_.setInputGain     (inputGainSmooth_.skip (blockSize));
        engine_.setGateThreshold (gateThresholdSmooth_.skip (blockSize));
        engine_.setGateRelease   (gateReleaseSmooth_.skip (blockSize));
        engine_.setPreampGain    (preampGainSmooth_.skip (blockSize));
        engine_.setBass           (bassSmooth_.skip (blockSize));
        engine_.setMid            (midSmooth_.skip (blockSize));
        engine_.setTreble         (trebleSmooth_.skip (blockSize));
        engine_.setPowerDrive     (powerDriveSmooth_.skip (blockSize));
        engine_.setPresence       (presenceSmooth_.skip (blockSize));
        engine_.setResonance      (resonanceSmooth_.skip (blockSize));
        engine_.setSag            (sagSmooth_.skip (blockSize));
        engine_.setCabinetMix     (cabMixSmooth_.skip (blockSize));
        engine_.setCabinetHiCut   (cabHiCutSmooth_.skip (blockSize));
        engine_.setCabinetLoCut   (cabLoCutSmooth_.skip (blockSize));
        engine_.setBoostGain      (boostGainSmooth_.skip (blockSize));
        engine_.setBoostTone      (boostToneSmooth_.skip (blockSize));
        engine_.setBoostLevel     (boostLevelSmooth_.skip (blockSize));
        engine_.setDelayTime      (delayTimeSmooth_.skip (blockSize));
        engine_.setDelayFeedback  (delayFeedbackSmooth_.skip (blockSize));
        engine_.setDelayMix       (delayMixSmooth_.skip (blockSize));
        engine_.setReverbMix      (reverbMixSmooth_.skip (blockSize));
        engine_.setReverbDecay    (reverbDecaySmooth_.skip (blockSize));
        engine_.setReverbPreDelay (reverbPreDelaySmooth_.skip (blockSize));
        engine_.setReverbDamping  (reverbDampingSmooth_.skip (blockSize));
        engine_.setReverbSize     (reverbSizeSmooth_.skip (blockSize));
        engine_.setOutputLevel    (outputLevelSmooth_.skip (blockSize));

        engine_.process (left + offset, right + offset, blockSize);

        offset += blockSize;
        samplesRemaining -= blockSize;
    }

    // Measure output levels
    {
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        outputLevelL_.store (peakL > 0.0f ? juce::Decibels::gainToDecibels (peakL) : -100.0f,
                             std::memory_order_relaxed);
        outputLevelR_.store (peakR > 0.0f ? juce::Decibels::gainToDecibels (peakR) : -100.0f,
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
    // Store file paths as properties
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

        // Restore cab IR path from saved state
        cabIRPath_ = parameters.state.getProperty ("cabIRPath", "").toString();
        if (cabIRPath_.isNotEmpty())
        {
            juce::File cabFile (cabIRPath_);
            if (cabFile.existsAsFile())
                engine_.getCabinetIR().loadIR (cabFile);
        }

        // Restore NAM model path from saved state
        namModelPath_ = parameters.state.getProperty ("namModelPath", "").toString();
        if (namModelPath_.isNotEmpty())
        {
            juce::File namFile (namModelPath_);
            if (namFile.existsAsFile())
                loadNAMModel (namFile);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskAmpProcessor();
}

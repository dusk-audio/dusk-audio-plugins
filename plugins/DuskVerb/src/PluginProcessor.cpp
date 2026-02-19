#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

juce::AudioProcessorValueTreeState::ParameterLayout DuskVerbProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "algorithm", 1 }, "Algorithm",
        juce::StringArray { "Plate", "Hall", "Chamber", "Room", "Ambient" }, 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay", 1 }, "Decay Time",
        juce::NormalisableRange<float> (0.2f, 30.0f, 0.0f, 0.4f), 2.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "predelay", 1 }, "Pre-Delay",
        juce::NormalisableRange<float> (0.0f, 250.0f, 0.0f, 1.0f), 15.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "size", 1 }, "Size",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "damping", 1 }, "Treble Multiply",
        juce::NormalisableRange<float> (0.1f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_mult", 1 }, "Bass Multiply",
        juce::NormalisableRange<float> (0.5f, 2.0f), 1.2f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "crossover", 1 }, "Crossover",
        juce::NormalisableRange<float> (200.0f, 4000.0f, 0.0f, 0.5f), 1000.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "diffusion", 1 }, "Diffusion",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.75f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mod_depth", 1 }, "Mod Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mod_rate", 1 }, "Mod Rate",
        juce::NormalisableRange<float> (0.1f, 3.0f), 0.8f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_level", 1 }, "Early Ref Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_size", 1 }, "Early Ref Size",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lo_cut", 1 }, "Lo Cut",
        juce::NormalisableRange<float> (20.0f, 500.0f, 0.0f, 0.5f), 20.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hi_cut", 1 }, "Hi Cut",
        juce::NormalisableRange<float> (1000.0f, 20000.0f, 0.0f, 0.5f), 20000.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "width", 1 }, "Width",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "freeze", 1 }, "Freeze", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "predelay_sync", 1 }, "Pre-Delay Sync",
        juce::StringArray { "Free", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bus_mode", 1 }, "Bus Mode", false));

    return layout;
}

DuskVerbProcessor::DuskVerbProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("DuskVerb"), createParameterLayout())
{
    algorithmParam_ = parameters.getRawParameterValue ("algorithm");
    decayParam_     = parameters.getRawParameterValue ("decay");
    preDelayParam_  = parameters.getRawParameterValue ("predelay");
    sizeParam_      = parameters.getRawParameterValue ("size");
    dampingParam_   = parameters.getRawParameterValue ("damping");
    bassMultParam_  = parameters.getRawParameterValue ("bass_mult");
    crossoverParam_ = parameters.getRawParameterValue ("crossover");
    diffusionParam_ = parameters.getRawParameterValue ("diffusion");
    modDepthParam_  = parameters.getRawParameterValue ("mod_depth");
    modRateParam_   = parameters.getRawParameterValue ("mod_rate");
    erLevelParam_   = parameters.getRawParameterValue ("er_level");
    erSizeParam_    = parameters.getRawParameterValue ("er_size");
    mixParam_       = parameters.getRawParameterValue ("mix");
    loCutParam_     = parameters.getRawParameterValue ("lo_cut");
    hiCutParam_     = parameters.getRawParameterValue ("hi_cut");
    widthParam_     = parameters.getRawParameterValue ("width");
    freezeParam_    = parameters.getRawParameterValue ("freeze");
    predelaySyncParam_ = parameters.getRawParameterValue ("predelay_sync");
    busModeParam_ = parameters.getRawParameterValue ("bus_mode");
}

bool DuskVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto outputSet = layouts.getMainOutputChannelSet();

    if (outputSet != juce::AudioChannelSet::stereo())
        return false;

    auto inputSet = layouts.getMainInputChannelSet();

    return inputSet == juce::AudioChannelSet::mono()
        || inputSet == juce::AudioChannelSet::stereo();
}

void DuskVerbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate, samplesPerBlock);

    // Initialize algorithm from saved state (edge-case: DAW restores state before first processBlock)
    cachedAlgorithm_ = static_cast<int> (algorithmParam_->load());
    engine_.setAlgorithm (cachedAlgorithm_);

    auto rampSamples = static_cast<double> (kSmoothingBlockSize);

    decaySmooth_    .reset (sampleRate, rampSamples / sampleRate);
    preDelaySmooth_ .reset (sampleRate, rampSamples / sampleRate);
    sizeSmooth_     .reset (sampleRate, rampSamples / sampleRate);
    dampingSmooth_  .reset (sampleRate, rampSamples / sampleRate);
    bassMultSmooth_ .reset (sampleRate, rampSamples / sampleRate);
    crossoverSmooth_.reset (sampleRate, rampSamples / sampleRate);
    diffusionSmooth_.reset (sampleRate, rampSamples / sampleRate);
    modDepthSmooth_ .reset (sampleRate, rampSamples / sampleRate);
    modRateSmooth_  .reset (sampleRate, rampSamples / sampleRate);
    erLevelSmooth_  .reset (sampleRate, rampSamples / sampleRate);
    erSizeSmooth_   .reset (sampleRate, rampSamples / sampleRate);
    mixSmooth_      .reset (sampleRate, rampSamples / sampleRate);
    loCutSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    hiCutSmooth_    .reset (sampleRate, rampSamples / sampleRate);
    widthSmooth_    .reset (sampleRate, rampSamples / sampleRate);

    decaySmooth_    .setCurrentAndTargetValue (decayParam_->load());
    preDelaySmooth_ .setCurrentAndTargetValue (preDelayParam_->load());
    sizeSmooth_     .setCurrentAndTargetValue (sizeParam_->load());
    dampingSmooth_  .setCurrentAndTargetValue (dampingParam_->load());
    bassMultSmooth_ .setCurrentAndTargetValue (bassMultParam_->load());
    crossoverSmooth_.setCurrentAndTargetValue (crossoverParam_->load());
    diffusionSmooth_.setCurrentAndTargetValue (diffusionParam_->load());
    modDepthSmooth_ .setCurrentAndTargetValue (modDepthParam_->load());
    modRateSmooth_  .setCurrentAndTargetValue (modRateParam_->load());
    erLevelSmooth_  .setCurrentAndTargetValue (erLevelParam_->load());
    erSizeSmooth_   .setCurrentAndTargetValue (erSizeParam_->load());
    mixSmooth_      .setCurrentAndTargetValue (mixParam_->load());
    loCutSmooth_    .setCurrentAndTargetValue (loCutParam_->load());
    hiCutSmooth_    .setCurrentAndTargetValue (hiCutParam_->load());
    widthSmooth_    .setCurrentAndTargetValue (widthParam_->load());
}

void DuskVerbProcessor::releaseResources()
{
}

void DuskVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    int numSamples = buffer.getNumSamples();

    // Clear any unused output channels
    for (int ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

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

    // Check for algorithm change (discrete, no smoothing needed)
    int algoIndex = static_cast<int> (algorithmParam_->load());
    if (algoIndex != cachedAlgorithm_)
    {
        cachedAlgorithm_ = algoIndex;
        engine_.setAlgorithm (algoIndex);
    }

    // Pre-delay: use tempo sync if enabled, otherwise use manual value
    float preDelayMs = preDelayParam_->load();
    int syncIndex = static_cast<int> (predelaySyncParam_->load());
    if (syncIndex > 0)
    {
        // Note value multipliers: 1/32, 1/16, 1/8, 1/4, 1/2, 1/1
        static constexpr float kNoteMultipliers[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        float noteBeats = kNoteMultipliers[syncIndex - 1];

        if (auto pos = getPlayHead() ? getPlayHead()->getPosition() : std::nullopt)
        {
            if (auto bpm = pos->getBpm())
            {
                float msPerBeat = 60000.0f / static_cast<float> (*bpm);
                preDelayMs = std::clamp (msPerBeat * noteBeats, 0.0f, 250.0f);
            }
        }
    }

    // Set smoothing targets from current parameter values
    decaySmooth_    .setTargetValue (decayParam_->load());
    preDelaySmooth_ .setTargetValue (preDelayMs);
    sizeSmooth_     .setTargetValue (sizeParam_->load());
    dampingSmooth_  .setTargetValue (dampingParam_->load());
    bassMultSmooth_ .setTargetValue (bassMultParam_->load());
    crossoverSmooth_.setTargetValue (crossoverParam_->load());
    diffusionSmooth_.setTargetValue (diffusionParam_->load());
    modDepthSmooth_ .setTargetValue (modDepthParam_->load());
    modRateSmooth_  .setTargetValue (modRateParam_->load());
    erLevelSmooth_  .setTargetValue (erLevelParam_->load());
    erSizeSmooth_   .setTargetValue (erSizeParam_->load());
    mixSmooth_      .setTargetValue (busModeParam_->load() >= 0.5f ? 1.0f : mixParam_->load());
    loCutSmooth_    .setTargetValue (loCutParam_->load());
    hiCutSmooth_    .setTargetValue (hiCutParam_->load());
    widthSmooth_    .setTargetValue (widthParam_->load());

    // Freeze is discrete (boolean), no smoothing needed
    engine_.setFreeze (freezeParam_->load() >= 0.5f);

    // Sub-block processing for smooth parameter transitions
    int samplesRemaining = numSamples;
    int offset = 0;

    while (samplesRemaining > 0)
    {
        int blockSize = std::min (samplesRemaining, kSmoothingBlockSize);

        // Advance smoothed values and apply to engine
        engine_.setDecayTime       (decaySmooth_.skip (blockSize));
        engine_.setPreDelay        (preDelaySmooth_.skip (blockSize));
        engine_.setSize            (sizeSmooth_.skip (blockSize));
        engine_.setTrebleMultiply  (dampingSmooth_.skip (blockSize));
        engine_.setBassMultiply    (bassMultSmooth_.skip (blockSize));
        engine_.setCrossoverFreq   (crossoverSmooth_.skip (blockSize));

        float diffVal = diffusionSmooth_.skip (blockSize);
        engine_.setDiffusion       (diffVal);
        engine_.setOutputDiffusion (diffVal * 0.6f);

        engine_.setModDepth        (modDepthSmooth_.skip (blockSize));
        engine_.setModRate         (modRateSmooth_.skip (blockSize));
        engine_.setERLevel         (erLevelSmooth_.skip (blockSize));
        engine_.setERSize          (erSizeSmooth_.skip (blockSize));
        engine_.setMix             (mixSmooth_.skip (blockSize));
        engine_.setLoCut           (loCutSmooth_.skip (blockSize));
        engine_.setHiCut           (hiCutSmooth_.skip (blockSize));
        engine_.setWidth           (widthSmooth_.skip (blockSize));

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

juce::AudioProcessorEditor* DuskVerbProcessor::createEditor()
{
    return new DuskVerbEditor (*this);
}

void DuskVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = getStateXML())
        copyXmlToBinary (*xml, destData);
}

void DuskVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        setStateXML (*xml);
}

std::unique_ptr<juce::XmlElement> DuskVerbProcessor::getStateXML()
{
    return parameters.copyState().createXml();
}

void DuskVerbProcessor::setStateXML (const juce::XmlElement& xml)
{
    if (xml.hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskVerbProcessor();
}

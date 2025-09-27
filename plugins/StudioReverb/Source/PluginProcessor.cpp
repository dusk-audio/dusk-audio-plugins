#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
StudioReverbAudioProcessor::StudioReverbAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
       , apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Get parameter pointers from APVTS
    reverbType = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("reverbType"));
    plateType = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("plateType"));

    // Mix controls - matching Dragonfly exactly
    dryLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("dryLevel"));
    earlyLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("earlyLevel"));
    earlySend = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("earlySend"));
    lateLevel = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lateLevel"));

    // Basic parameters
    size = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("size"));
    width = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("width"));
    preDelay = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("preDelay"));
    decay = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("decay"));
    diffuse = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("diffuse"));

    // Modulation
    spin = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("spin"));
    wander = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("wander"));
    modulation = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("modulation"));

    // Filters
    highCut = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("highCut"));
    lowCut = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lowCut"));
    dampen = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("dampen"));
    earlyDamp = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("earlyDamp"));
    lateDamp = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lateDamp"));

    // Room-specific boost
    lowBoost = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lowBoost"));
    boostFreq = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("boostFreq"));

    // Hall-specific
    lowCross = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lowCross"));
    highCross = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("highCross"));
    lowMult = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lowMult"));
    highMult = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("highMult"));

    // Add parameter listeners for all parameters
    for (const auto& id : getParameterIDs())
    {
        apvts.addParameterListener(id, this);
    }

    reverb = std::make_unique<DragonflyReverb>();
    //reverb = std::make_unique<SimpleReverb>();  // Temporarily using simple reverb

    // Initialize reverb with current parameter values
    updateReverbParameters();
}

StudioReverbAudioProcessor::~StudioReverbAudioProcessor()
{
    // Remove all parameter listeners
    for (const auto& id : getParameterIDs())
    {
        apvts.removeParameterListener(id, this);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout StudioReverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Algorithm selection
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "reverbType", "Reverb Type",
        juce::StringArray{"Room", "Hall", "Plate", "Early Reflections"},
        1)); // Default to Hall

    // Plate algorithm selection (only used when reverbType is Plate)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "plateType", "Plate Type",
        juce::StringArray{"Simple", "Nested", "Tank"}, 1));  // Default to Nested

    // === Mix Controls - Separate Dry and Wet for better control ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dryLevel", "Dry Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 80.0f,  // Default 80% dry for subtle effect
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    // === Mix Controls (matching Dragonfly) ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "earlyLevel", "Early Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f,  // More early reflections
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "earlySend", "Early Send",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 35.0f,  // Better blend
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lateLevel", "Late Level",  // This is what Dragonfly shows in UI
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 20.0f,  // Default 20% for subtle reverb
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    // === Basic Reverb Parameters ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "size", "Size",
        juce::NormalisableRange<float>(10.0f, 60.0f, 0.1f), 30.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " m"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "width", "Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preDelay", "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ms"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decay", "Decay",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 2.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " s"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "diffuse", "Diffuse",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 75.0f,  // More diffusion for smoother sound
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    // === Modulation Controls ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "spin", "Spin",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 0.5f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wander", "Wander",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f,  // Default 25ms to match Dragonfly
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 0) + " ms"; }));

    // Hall-specific modulation
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modulation", "Modulation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; }));

    // === Filter Controls ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highCut", "High Cut",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f), 16000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowCut", "Low Cut",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    // Plate-specific damping control
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dampen", "Dampen",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f), 10000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    // Room-specific damping controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "earlyDamp", "Early Damp",
        juce::NormalisableRange<float>(1000.0f, 16000.0f, 1.0f), 10000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lateDamp", "Late Damp",
        juce::NormalisableRange<float>(1000.0f, 16000.0f, 1.0f), 9000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    // Room-specific boost controls
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowBoost", "Low Boost",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 0) + "%"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "boostFreq", "Boost Freq",
        juce::NormalisableRange<float>(50.0f, 4000.0f, 1.0f), 600.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    // === Hall-specific Crossover Controls ===
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowCross", "Low Cross",
        juce::NormalisableRange<float>(50.0f, 1000.0f, 1.0f), 200.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highCross", "High Cross",
        juce::NormalisableRange<float>(1000.0f, 10000.0f, 1.0f), 3000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String((int)value) + " Hz"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowMult", "Low Mult",
        juce::NormalisableRange<float>(0.1f, 2.0f, 0.01f), 1.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + "x"; }));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highMult", "High Mult",
        juce::NormalisableRange<float>(0.1f, 2.0f, 0.01f), 0.8f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + "x"; }));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String StudioReverbAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool StudioReverbAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool StudioReverbAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool StudioReverbAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double StudioReverbAudioProcessor::getTailLengthSeconds() const
{
    // Return the maximum possible reverb tail (decay time + predelay)
    // Add null checks to prevent crashes if parameters aren't initialized
    double decayValue = (decay != nullptr) ? decay->get() : 0.0;
    double preDelayValue = (preDelay != nullptr) ? preDelay->get() : 0.0;
    return decayValue + (preDelayValue / 1000.0);
}

int StudioReverbAudioProcessor::getNumPrograms()
{
    return 1;
}

int StudioReverbAudioProcessor::getCurrentProgram()
{
    return 0;
}

void StudioReverbAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String StudioReverbAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void StudioReverbAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void StudioReverbAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Validate input parameters
    if (sampleRate <= 0.0 || samplesPerBlock <= 0)
    {
        jassertfalse;
        return;
    }

    if (reverb)
    {
        reverb->prepare(sampleRate, samplesPerBlock);
        // DON'T call reset() here - it mutes all the reverb engines!
        // reverb->reset();
        updateReverbParameters();
    }
}

void StudioReverbAudioProcessor::releaseResources()
{
    if (reverb)
        reverb->reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool StudioReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // Accept mono or stereo output
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    // REVERB SPECIAL CASE: Allow mono input with stereo output (common for reverbs)
    // Also allow matching configurations
    auto inputChannels = layouts.getMainInputChannelSet();
    auto outputChannels = layouts.getMainOutputChannelSet();

    // Allow: mono->mono, mono->stereo, stereo->stereo
    bool validConfig = (inputChannels == juce::AudioChannelSet::mono() &&
                       (outputChannels == juce::AudioChannelSet::mono() ||
                        outputChannels == juce::AudioChannelSet::stereo())) ||
                      (inputChannels == juce::AudioChannelSet::stereo() &&
                       outputChannels == juce::AudioChannelSet::stereo());

    if (!validConfig)
        return false;
   #endif

    return true;
  #endif
}
#endif

void StudioReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Only update parameters if they've changed (with thread safety)
    if (parametersChanged.load(std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedLockType lock(parameterLock);
        if (parametersChanged.exchange(false, std::memory_order_acq_rel))
            updateReverbParameters();
    }

    if (reverb)
    {
        static int debugCounter = 0;
        debugCounter++;
        bool debugThisBlock = (debugCounter % 100 == 0);

        // Store original for comparison in debug mode
        juce::AudioBuffer<float> originalBuffer;
        if (debugThisBlock)
        {
            originalBuffer.makeCopyOf(buffer);
        }

        // Debug: Check buffer before processing (commented out to avoid unused variable warning)
        // float inputLevel = buffer.getMagnitude(0, buffer.getNumSamples());

        // Handle mono input by duplicating to stereo for reverb processing
        juce::AudioBuffer<float> stereoBuffer;
        if (buffer.getNumChannels() == 1)
        {
            // Create stereo buffer from mono input
            stereoBuffer.setSize(2, buffer.getNumSamples());
            stereoBuffer.copyFrom(0, 0, buffer, 0, 0, buffer.getNumSamples());
            stereoBuffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());

            // Process stereo buffer
            reverb->processBlock(stereoBuffer);

            // Copy back based on output configuration
            if (totalNumOutputChannels == 1)
            {
                // Mono output: mix stereo to mono
                buffer.copyFrom(0, 0, stereoBuffer, 0, 0, buffer.getNumSamples());
                buffer.addFrom(0, 0, stereoBuffer, 1, 0, buffer.getNumSamples(), 1.0f);
                buffer.applyGain(0.5f);
            }
            else if (totalNumOutputChannels >= 2)
            {
                // Stereo output from mono input
                buffer.setSize(2, buffer.getNumSamples(), true, true, true);
                buffer.copyFrom(0, 0, stereoBuffer, 0, 0, buffer.getNumSamples());
                buffer.copyFrom(1, 0, stereoBuffer, 1, 0, buffer.getNumSamples());
            }
        }
        else
        {
            // Already stereo, process directly
            reverb->processBlock(buffer);
        }

        // Debug: Check buffer after processing (commented out to avoid unused variable warning)
        // float outputLevel = buffer.getMagnitude(0, buffer.getNumSamples());

        if (debugThisBlock)  // Print every 100 blocks to avoid spam
        {
            DBG("\n=== Block #" << debugCounter << " ===");
            DBG("Input level: " << inputLevel << ", Output level: " << outputLevel);

            // Calculate actual change
            float totalDiff = 0.0f;
            int numChannels = juce::jmin(2, buffer.getNumChannels());
            for (int ch = 0; ch < numChannels; ++ch)
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    float orig = originalBuffer.getSample(ch, i);
                    float proc = buffer.getSample(ch, i);
                    totalDiff += std::abs(proc - orig);
                }
            }

            DBG("Total difference: " << totalDiff);

            if (totalDiff < 0.001f)
            {
                DBG("WARNING: NO REVERB DETECTED!");
                DBG("  Algorithm: " << (reverbType ? reverbType->getIndex() : -1));
                DBG("  Dry: " << (dryLevel ? dryLevel->get() : 0.0f) << "%");
                DBG("  Late: " << (lateLevel ? lateLevel->get() : 0.0f) << "%");
            }
            else
            {
                DBG("Reverb IS processing (difference = " << totalDiff << ")");
            }
        }
    }
    else
    {
        DBG("ERROR: Reverb object is NULL!");
    }
}

void StudioReverbAudioProcessor::updateReverbParameters()
{
    if (!reverb)
        return;

    // TEMPORARY: Simple reverb only has dry/wet
    // Comment out all the complex parameter setting for now

    /* DISABLED FOR SIMPLE REVERB TEST
    // Set reverb algorithm
    int algIndex = reverbType ? reverbType->getIndex() : 0;
    DBG("updateReverbParameters - Setting algorithm to: " << algIndex
        << " (" << reverbType->getCurrentChoiceName() << ")");
    reverb->setAlgorithm(static_cast<DragonflyReverb::Algorithm>(algIndex));

    // Set core parameters with null checks
    if (size) reverb->setSize(size->get());
    if (preDelay) reverb->setPreDelay(preDelay->get());
    if (decay) reverb->setDecay(decay->get());
    if (diffuse) reverb->setDiffuse(diffuse->get());
    if (width) reverb->setWidth(width->get());
    */

    // Get separate dry and wet levels and convert to 0-1 range
    float dryPercent = dryLevel ? dryLevel->get() : 80.0f;   // Default 80% dry

    // Convert percentages to normalized 0-1 range
    float dryNorm = dryPercent / 100.0f;

    // Get algorithm index
    int algIndex = reverbType ? static_cast<int>(reverbType->getIndex()) : 1;

    // Set algorithm and basic parameters
    reverb->setAlgorithm(static_cast<DragonflyReverb::Algorithm>(algIndex));
    reverb->setPreDelay(preDelay ? preDelay->get() : 0.0f);
    reverb->setDecay(decay ? decay->get() : 2.0f);
    reverb->setSize(size ? size->get() : 30.0f);
    reverb->setDiffuse(diffuse ? diffuse->get() : 75.0f);
    reverb->setWidth(width ? width->get() : 100.0f);

    // Set mix levels
    reverb->setDryLevel(dryNorm);

    // Room and Hall algorithms expose early level and early send in UI
    if (algIndex == 0 || algIndex == 1)  // Room or Hall
    {
        float earlyPercent = earlyLevel ? earlyLevel->get() : 30.0f;
        float earlyNorm = earlyPercent / 100.0f;
        reverb->setEarlyLevel(earlyNorm);

        float earlySendPercent = earlySend ? earlySend->get() : 35.0f;
        float earlySendNorm = earlySendPercent / 100.0f;
        reverb->setEarlySend(earlySendNorm);

        // Late level for Room and Hall
        float latePercent = lateLevel ? lateLevel->get() : 20.0f;
        float lateNorm = latePercent / 100.0f;
        printf("updateReverbParameters: Setting late level for %s: %f%% (normalized: %f)\n",
               (algIndex == 0 ? "Room" : "Hall"), latePercent, lateNorm);
        fflush(stdout);
        reverb->setLateLevel(lateNorm);
    }
    else if (algIndex == 3)  // Early Reflections
    {
        // For Early Reflections mode, the "Level" control (lateLevel parameter)
        // actually controls the early reflections level since there's no late reverb
        float levelPercent = lateLevel ? lateLevel->get() : 20.0f;
        float levelNorm = levelPercent / 100.0f;
        reverb->setEarlyLevel(levelNorm);
        // No late level needed for Early Reflections mode
    }
    else  // Plate
    {
        // Plate uses late level as wet level
        float latePercent = lateLevel ? lateLevel->get() : 20.0f;
        float lateNorm = latePercent / 100.0f;
        reverb->setLateLevel(lateNorm);
    }

    // Set filter controls
    if (lowCut) reverb->setLowCut(lowCut->get());
    if (highCut) reverb->setHighCut(highCut->get());

    // Mode-specific parameter handling
    if (algIndex == 0)  // Room
    {
        // Room-specific modulation
        if (spin) reverb->setSpin(spin->get());
        if (wander) reverb->setWander(wander->get());

        // Room-specific damping
        if (earlyDamp && earlyDamp->get() > 0.0f)
            reverb->setEarlyDamp(earlyDamp->get());
        if (lateDamp && lateDamp->get() > 0.0f)
            reverb->setLateDamp(lateDamp->get());

        // Room-specific boost controls
        if (lowBoost && lowBoost->get() >= 0.0f)
            reverb->setLowBoost(lowBoost->get());
        if (boostFreq && boostFreq->get() > 0.0f)
            reverb->setBoostFreq(boostFreq->get());
    }
    else if (algIndex == 1)  // Hall
    {
        // Hall-specific modulation
        if (spin) reverb->setSpin(spin->get());
        if (wander) reverb->setWander(wander->get());
        if (modulation && modulation->get() >= 0.0f)
            reverb->setModulation(modulation->get());

        // Hall-specific crossover controls
        if (lowCross) reverb->setLowCrossover(lowCross->get());
        if (highCross) reverb->setHighCrossover(highCross->get());
        if (lowMult) reverb->setLowMult(lowMult->get());
        if (highMult) reverb->setHighMult(highMult->get());
    }
    else if (algIndex == 2)  // Plate
    {
        // Set plate algorithm based on plateType parameter
        if (plateType)
        {
            int plateAlg = plateType->getIndex();
            reverb->setPlateAlgorithm(static_cast<DragonflyReverb::PlateAlgorithm>(plateAlg));
        }

        // Plate-specific damping
        if (dampen && dampen->get() > 0.0f)
            reverb->setDamping(dampen->get());
    }
}

void StudioReverbAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);

    DBG("Parameter changed: " << parameterID);
    if (parameterID == "reverbType")
    {
        DBG("  Reverb type changed to index: " << reverbType->getIndex()
            << " (" << reverbType->getCurrentChoiceName() << ")");
    }

    // Debug late level parameter changes
    if (parameterID == "lateLevel")
    {
        printf("Late level parameter changed to: %f (raw value)\n", newValue);
        fflush(stdout);
        if (lateLevel)
        {
            printf("  Late level get(): %f%%\n", lateLevel->get());
            fflush(stdout);
        }
        else
        {
            printf("  ERROR: lateLevel parameter pointer is null!\n");
            fflush(stdout);
        }
    }

    // Don't trigger individual updates if we're loading a preset
    if (!isLoadingPreset.load(std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedLockType lock(parameterLock);
        parametersChanged.store(true, std::memory_order_release);
    }
}

void StudioReverbAudioProcessor::loadPreset(const juce::String& presetName)
{
    // Use the parameter's current algorithm index
    int algorithmIndex = reverbType ? reverbType->getIndex() : 0;
    loadPresetForAlgorithm(presetName, algorithmIndex);
}

void StudioReverbAudioProcessor::loadPresetForAlgorithm(const juce::String& presetName, int algorithmIndex)
{
    DBG("StudioReverbAudioProcessor::loadPresetForAlgorithm called with: " << presetName
        << " for algorithm " << algorithmIndex);

    if (presetName == "-- Select Preset --" || presetName.isEmpty())
    {
        DBG("  Ignoring preset selection header");
        return;
    }

    DBG("  Loading preset for algorithm index: " << algorithmIndex);

    auto preset = presetManager.getPreset(algorithmIndex, presetName);

    if (preset.name.isEmpty())
    {
        DBG("  ERROR: Preset not found!");
        return;
    }

    DBG("  Found preset: " << preset.name << " with " << preset.parameters.size() << " parameters");

    // Set flag to batch parameter updates
    isLoadingPreset = true;

    // Special handling for Plate algorithm selection based on preset name
    if (algorithmIndex == 2) // Plate reverb
    {
        // Match Dragonfly Plate preset algorithm selection
        if (presetName.contains("Dark") || presetName.contains("Clear") ||
            presetName.contains("Bright") || presetName.contains("Abrupt"))
        {
            // These use the Nested algorithm (nrevb)
            if (plateType) plateType->setValueNotifyingHost(1.0f / 2.0f); // Index 1 = Nested
            reverb->setPlateAlgorithm(DragonflyReverb::PlateAlgorithm::Nested);
            DBG("  Setting Plate algorithm to Nested for preset: " << presetName);
        }
        else if (presetName.contains("Foil") || presetName.contains("Metal"))
        {
            // These use the Simple algorithm (nrev)
            if (plateType) plateType->setValueNotifyingHost(0.0f); // Index 0 = Simple
            reverb->setPlateAlgorithm(DragonflyReverb::PlateAlgorithm::Simple);
            DBG("  Setting Plate algorithm to Simple for preset: " << presetName);
        }
        else if (presetName.contains("Tank"))
        {
            // These use the Tank algorithm (strev)
            if (plateType) plateType->setValueNotifyingHost(2.0f / 2.0f); // Index 2 = Tank (normalized to 0-1)
            reverb->setPlateAlgorithm(DragonflyReverb::PlateAlgorithm::Tank);
            DBG("  Setting Plate algorithm to Tank for preset: " << presetName);
        }
        else
        {
            // Default to Nested for other plate presets
            if (plateType) plateType->setValueNotifyingHost(1.0f / 2.0f); // Index 1 = Nested
            reverb->setPlateAlgorithm(DragonflyReverb::PlateAlgorithm::Nested);
            DBG("  Setting Plate algorithm to Nested (default) for preset: " << presetName);
        }
    }

    // Load preset parameters
    for (const auto& param : preset.parameters)
    {
        // Use the AudioParameterFloat directly for proper value conversion
        if (param.first == "dryLevel" && dryLevel)
            dryLevel->setValueNotifyingHost(dryLevel->convertTo0to1(param.second));
        else if (param.first == "earlyLevel" && earlyLevel)
            earlyLevel->setValueNotifyingHost(earlyLevel->convertTo0to1(param.second));
        else if (param.first == "earlySend" && earlySend)
            earlySend->setValueNotifyingHost(earlySend->convertTo0to1(param.second));
        else if (param.first == "lateLevel" && lateLevel)
            lateLevel->setValueNotifyingHost(lateLevel->convertTo0to1(param.second));
        else if (param.first == "size" && size) {
            DBG("  Setting size to " << param.second << " (normalized: " << size->convertTo0to1(param.second) << ")");
            size->setValueNotifyingHost(size->convertTo0to1(param.second));
            DBG("  After setting, size value is: " << size->get());
        }
        else if (param.first == "width" && width)
            width->setValueNotifyingHost(width->convertTo0to1(param.second));
        else if (param.first == "preDelay" && preDelay)
            preDelay->setValueNotifyingHost(preDelay->convertTo0to1(param.second));
        else if (param.first == "decay" && decay)
            decay->setValueNotifyingHost(decay->convertTo0to1(param.second));
        else if (param.first == "diffuse" && diffuse)
            diffuse->setValueNotifyingHost(diffuse->convertTo0to1(param.second));
        else if (param.first == "spin" && spin)
            spin->setValueNotifyingHost(spin->convertTo0to1(param.second));
        else if (param.first == "wander" && wander)
            wander->setValueNotifyingHost(wander->convertTo0to1(param.second));
        else if (param.first == "highCut" && highCut)
            highCut->setValueNotifyingHost(highCut->convertTo0to1(param.second));
        else if (param.first == "lowCut" && lowCut)
            lowCut->setValueNotifyingHost(lowCut->convertTo0to1(param.second));
        else if (param.first == "lowCross" && lowCross)
            lowCross->setValueNotifyingHost(lowCross->convertTo0to1(param.second));
        else if (param.first == "highCross" && highCross)
            highCross->setValueNotifyingHost(highCross->convertTo0to1(param.second));
        else if (param.first == "lowMult" && lowMult)
            lowMult->setValueNotifyingHost(lowMult->convertTo0to1(param.second));
        else if (param.first == "highMult" && highMult)
            highMult->setValueNotifyingHost(highMult->convertTo0to1(param.second));
        else if (param.first == "dampen" && dampen)
            dampen->setValueNotifyingHost(dampen->convertTo0to1(param.second));
    }

    // Clear flag and trigger single update
    isLoadingPreset = false;
    {
        const juce::SpinLock::ScopedLockType lock(parameterLock);
        parametersChanged = true;
    }
}

//==============================================================================
bool StudioReverbAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* StudioReverbAudioProcessor::createEditor()
{
    return new StudioReverbAudioProcessorEditor (*this);
}

//==============================================================================
void StudioReverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void StudioReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StudioReverbAudioProcessor();
}
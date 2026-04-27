#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

#include <algorithm>
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout DuskVerbProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Algorithm dropdown — names mirror getAlgorithmConfig() ----
    juce::StringArray algorithmNames;
    for (int i = 0; i < getNumAlgorithms(); ++i)
        algorithmNames.add (getAlgorithmConfig (i).name);

    // Seed every default from factory preset 0 so a host "reset to defaults"
    // matches the startup voicing the constructor would otherwise apply
    // post-hoc. Single source of truth — if preset 0 changes, defaults track.
    const auto& fp0 = getFactoryPresets().front();

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "algorithm", 1 }, "Algorithm", algorithmNames, fp0.algorithm));

    // ---- The 21 parameters ----
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f), fp0.mix));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bus_mode", 1 }, "Bus Mode", fp0.busMode));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "predelay", 1 }, "Pre-Delay",
        juce::NormalisableRange<float> (0.0f, 250.0f, 0.0f, 0.4f), fp0.predelay));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "predelay_sync", 1 }, "Pre-Delay Sync",
        juce::StringArray { "Free", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }, fp0.predelaySync));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay", 1 }, "Decay Time",
        juce::NormalisableRange<float> (0.2f, 30.0f, 0.0f, 0.4f), fp0.decay));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "size", 1 }, "Size",
        juce::NormalisableRange<float> (0.0f, 1.0f), fp0.size));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mod_depth", 1 }, "Mod Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), fp0.modDepth));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mod_rate", 1 }, "Mod Rate",
        juce::NormalisableRange<float> (0.10f, 10.0f, 0.0f, 0.5f), fp0.modRate));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_mult", 1 }, "Bass Multiply",
        juce::NormalisableRange<float> (0.3f, 2.5f), fp0.bassMult));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mid_mult", 1 }, "Mid Multiply",
        juce::NormalisableRange<float> (0.3f, 2.5f), fp0.midMult));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "damping", 1 }, "Treble Multiply",
        juce::NormalisableRange<float> (0.1f, 1.5f), fp0.damping));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "crossover", 1 }, "Low Crossover",
        juce::NormalisableRange<float> (200.0f, 4000.0f, 0.0f, 0.5f), fp0.crossover));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "high_crossover", 1 }, "High Crossover",
        juce::NormalisableRange<float> (1000.0f, 12000.0f, 0.0f, 0.5f), fp0.highCrossover));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "saturation", 1 }, "Saturation",
        juce::NormalisableRange<float> (0.0f, 1.0f), fp0.saturation));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "diffusion", 1 }, "Diffusion",
        juce::NormalisableRange<float> (0.0f, 1.0f), fp0.diffusion));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_level", 1 }, "Early Ref Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), fp0.erLevel));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_size", 1 }, "Early Ref Size",
        juce::NormalisableRange<float> (0.0f, 1.0f), fp0.erSize));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lo_cut", 1 }, "Lo Cut",
        juce::NormalisableRange<float> (5.0f, 500.0f, 0.0f, 0.3f), fp0.loCut));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hi_cut", 1 }, "Hi Cut",
        juce::NormalisableRange<float> (1000.0f, 20000.0f, 0.0f, 0.3f), fp0.hiCut));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "width", 1 }, "Width",
        juce::NormalisableRange<float> (0.0f, 2.0f), fp0.width));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "freeze", 1 }, "Freeze", fp0.freeze));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain_trim", 1 }, "Gain Trim",
        juce::NormalisableRange<float> (-48.0f, 48.0f, 0.1f), fp0.gainTrim));

    // Mono Maker — sums L+R below this cutoff to mono. 20 Hz = effectively
    // bypass (sub-audible). Skewed log-style to give finer control across
    // the typical 60–200 Hz mono region.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mono_below", 1 }, "Mono Below",
        juce::NormalisableRange<float> (20.0f, 300.0f, 0.0f, 0.5f), fp0.monoBelow));

    return layout;
}

DuskVerbProcessor::DuskVerbProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("DuskVerb"), createParameterLayout())
{
    algorithmParam_     = parameters.getRawParameterValue ("algorithm");
    mixParam_           = parameters.getRawParameterValue ("mix");
    busModeParam_       = parameters.getRawParameterValue ("bus_mode");
    preDelayParam_      = parameters.getRawParameterValue ("predelay");
    preDelaySyncParam_  = parameters.getRawParameterValue ("predelay_sync");
    decayParam_         = parameters.getRawParameterValue ("decay");
    sizeParam_          = parameters.getRawParameterValue ("size");
    modDepthParam_      = parameters.getRawParameterValue ("mod_depth");
    modRateParam_       = parameters.getRawParameterValue ("mod_rate");
    dampingParam_       = parameters.getRawParameterValue ("damping");
    bassMultParam_      = parameters.getRawParameterValue ("bass_mult");
    midMultParam_       = parameters.getRawParameterValue ("mid_mult");
    crossoverParam_     = parameters.getRawParameterValue ("crossover");
    highCrossoverParam_ = parameters.getRawParameterValue ("high_crossover");
    saturationParam_    = parameters.getRawParameterValue ("saturation");
    diffusionParam_     = parameters.getRawParameterValue ("diffusion");
    erLevelParam_       = parameters.getRawParameterValue ("er_level");
    erSizeParam_        = parameters.getRawParameterValue ("er_size");
    loCutParam_         = parameters.getRawParameterValue ("lo_cut");
    hiCutParam_         = parameters.getRawParameterValue ("hi_cut");
    widthParam_         = parameters.getRawParameterValue ("width");
    freezeParam_        = parameters.getRawParameterValue ("freeze");
    gainTrimParam_      = parameters.getRawParameterValue ("gain_trim");
    monoBelowParam_     = parameters.getRawParameterValue ("mono_below");

    bypassParam_ = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter ("bypass"));

    // Startup voicing comes from createParameterLayout() seeding every default
    // from factory preset 0 — host "reset to defaults" therefore reproduces
    // exactly what a fresh instance plays. setStateInformation() overwrites
    // this for hosts that restore saved state.
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
    constexpr int kMinPreparedBlockSize = 4096;
    int safeBlockSize = std::max (samplesPerBlock, kMinPreparedBlockSize);
    bool needsReprepare =
        (preparedSampleRate_ != sampleRate || safeBlockSize > preparedBlockSize_);

    if (needsReprepare)
    {
        preparedSampleRate_ = sampleRate;
        preparedBlockSize_  = safeBlockSize;
        engine_.prepare (sampleRate, safeBlockSize);

        // Force re-push of every cached value next process() call.
        cachedAlgorithm_ = -1;
        lastDecaySec_ = lastSize_ = lastDamping_ = lastBassMult_ = lastMidMult_ =
        lastCrossover_ = lastHighCrossover_ = lastSaturation_ =
        lastDiffusion_ = lastModDepth_ = lastModRate_ = lastERSize_ = lastPreDelayMs_ =
        lastMix_ = lastLoCut_ = lastHiCut_ = lastWidth_ = lastMonoBelow_ = -1.0f;
        lastERLevel_ = -2.0f;
        lastGainTrim_ = -999.0f;
        haveLastFreeze_ = false;
    }

    setLatencySamples (0);
}

void DuskVerbProcessor::releaseResources()
{
    preparedSampleRate_ = 0.0;
    preparedBlockSize_  = 0;
}

void DuskVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    int numSamples = buffer.getNumSamples();

    if (numSamples == 0)
        return;

    // Promote mono input to stereo before any other processing.
    if (totalNumInputChannels == 1 && totalNumOutputChannels == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    for (int ch = std::max (totalNumInputChannels, 2); ch < totalNumOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // ---- Bypass: pass through, still update meters ----
    if (bypassParam_ != nullptr && bypassParam_->get())
    {
        float* left  = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        const float dbL = peakL > 0.0f ? juce::Decibels::gainToDecibels (peakL) : -100.0f;
        const float dbR = peakR > 0.0f ? juce::Decibels::gainToDecibels (peakR) : -100.0f;
        inputLevelL_.store  (dbL, std::memory_order_relaxed);
        inputLevelR_.store  (dbR, std::memory_order_relaxed);
        outputLevelL_.store (dbL, std::memory_order_relaxed);
        outputLevelR_.store (dbR, std::memory_order_relaxed);
        return;
    }

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    // Input metering.
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

    // ---- Push parameter changes to the engine on edges only ----

    // Algorithm
    int algoIdx = static_cast<int> (algorithmParam_->load());
    if (algoIdx != cachedAlgorithm_)
    {
        cachedAlgorithm_ = algoIdx;
        engine_.setAlgorithm (algoIdx);
    }

    // Pre-delay (with optional tempo sync)
    float preDelayMs = preDelayParam_->load();
    int syncIndex = static_cast<int> (preDelaySyncParam_->load());
    if (syncIndex > 0)
    {
        // 1/32, 1/16, 1/8, 1/4, 1/2, 1/1 in beats
        static constexpr float kNoteBeats[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        float beats = kNoteBeats[syncIndex - 1];
        if (auto pos = getPlayHead() ? getPlayHead()->getPosition() : std::nullopt)
        {
            if (auto bpm = pos->getBpm())
                preDelayMs = std::clamp (60000.0f / static_cast<float> (*bpm) * beats, 0.0f, 250.0f);
        }
    }
    if (preDelayMs != lastPreDelayMs_)
    {
        lastPreDelayMs_ = preDelayMs;
        engine_.setPreDelay (preDelayMs);
    }

    // Tank parameters
    auto pushIfChanged = [] (float& last, float current, auto setter) {
        if (current != last) { last = current; setter (current); }
    };

    pushIfChanged (lastDecaySec_,  decayParam_->load(),     [this] (float v) { engine_.setDecayTime (v); });
    pushIfChanged (lastSize_,      sizeParam_->load(),      [this] (float v) { engine_.setSize (v); });
    pushIfChanged (lastDamping_,   dampingParam_->load(),   [this] (float v) { engine_.setTrebleMultiply (v); });
    pushIfChanged (lastBassMult_,  bassMultParam_->load(),  [this] (float v) { engine_.setBassMultiply (v); });
    pushIfChanged (lastMidMult_,   midMultParam_->load(),   [this] (float v) { engine_.setMidMultiply (v); });
    pushIfChanged (lastCrossover_, crossoverParam_->load(), [this] (float v) { engine_.setCrossoverFreq (v); });
    pushIfChanged (lastHighCrossover_, highCrossoverParam_->load(), [this] (float v) { engine_.setHighCrossoverFreq (v); });
    pushIfChanged (lastSaturation_,    saturationParam_->load(),    [this] (float v) { engine_.setSaturation (v); });
    pushIfChanged (lastDiffusion_, diffusionParam_->load(), [this] (float v) { engine_.setDiffusion (v); });
    pushIfChanged (lastModDepth_,  modDepthParam_->load(),  [this] (float v) { engine_.setModDepth (v); });
    pushIfChanged (lastModRate_,   modRateParam_->load(),   [this] (float v) { engine_.setModRate (v); });
    pushIfChanged (lastERSize_,    erSizeParam_->load(),    [this] (float v) { engine_.setERSize (v); });
    pushIfChanged (lastERLevel_,   erLevelParam_->load(),   [this] (float v) { engine_.setERLevel (v); });
    pushIfChanged (lastLoCut_,     loCutParam_->load(),     [this] (float v) { engine_.setLoCut (v); });
    pushIfChanged (lastHiCut_,     hiCutParam_->load(),     [this] (float v) { engine_.setHiCut (v); });
    pushIfChanged (lastWidth_,     widthParam_->load(),     [this] (float v) { engine_.setWidth (v); });
    pushIfChanged (lastGainTrim_,  gainTrimParam_->load(),  [this] (float v) { engine_.setGainTrim (v); });
    pushIfChanged (lastMonoBelow_, monoBelowParam_->load(), [this] (float v) { engine_.setMonoBelow (v); });

    // Mix: bus_mode forces 100 % wet (override of user mix knob).
    const bool busMode = busModeParam_->load() >= 0.5f;
    const float mixVal = busMode ? 1.0f : mixParam_->load();
    pushIfChanged (lastMix_, mixVal, [this] (float v) { engine_.setMix (v); });

    // Freeze (boolean — push only on transitions).
    const bool freezeNow = freezeParam_->load() >= 0.5f;
    if (! haveLastFreeze_ || freezeNow != lastFreeze_)
    {
        engine_.setFreeze (freezeNow);
        lastFreeze_     = freezeNow;
        haveLastFreeze_ = true;
    }

    engine_.process (left, right, numSamples);

    // Output metering.
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

// State schema version. Bump whenever the parameter set, ranges, or
// per-parameter semantics change in a way that needs a migration. Newer
// plugin versions can read older states by branching on this; older plugin
// versions reading a newer state will see the unknown number and refuse to
// apply (preserves user's session rather than silently mis-mapping).
static constexpr int kStateVersion = 1;
static const juce::Identifier kStateVersionId { "stateVersion" };

void DuskVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (kStateVersionId, kStateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DuskVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    // Pre-versioning states (no property present) default to 1; they were
    // already wire-compatible with v1's parameter layout.
    const int version = tree.hasProperty (kStateVersionId)
                      ? static_cast<int> (tree.getProperty (kStateVersionId))
                      : 1;
    if (version > kStateVersion)
        return;  // future state — keep current params, don't risk mis-mapping

    parameters.replaceState (tree);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskVerbProcessor();
}

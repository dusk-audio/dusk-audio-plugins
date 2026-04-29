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

    // Gate enable (NonLinear engine only — no-op on other algorithms).
    // Default true for backward compat with existing presets.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "gate_enabled", 1 }, "Gate", true));

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
    gateEnabledParam_   = parameters.getRawParameterValue ("gate_enabled");
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
        // Both engines stay prepared for the lifetime of the session — only
        // one runs at any moment except during the brief preset-fade window.
        engineA_.prepare (sampleRate, safeBlockSize);
        engineB_.prepare (sampleRate, safeBlockSize);

        // Fade scratch buffers: sized to the largest block we'll ever see so
        // processBlock never reallocates. The fade window itself is ~50 ms,
        // but the buffers only need to hold one block's worth of previous-
        // engine output at a time.
        fadeBufL_.assign (static_cast<size_t> (safeBlockSize), 0.0f);
        fadeBufR_.assign (static_cast<size_t> (safeBlockSize), 0.0f);

        // Cancel any in-flight fade — both engines are reset, no tail to
        // continue.
        previousEngine_ = nullptr;
        presetFadeRemaining_ = 0;
        presetFadeTotal_     = 0;

        // Force re-push of every cached value next process() call.
        cachedAlgorithm_ = -1;
        lastDecaySec_ = lastSize_ = lastDamping_ = lastBassMult_ = lastMidMult_ =
        lastCrossover_ = lastHighCrossover_ = lastSaturation_ =
        lastDiffusion_ = lastModDepth_ = lastModRate_ = lastERSize_ = lastPreDelayMs_ =
        lastMix_ = lastLoCut_ = lastHiCut_ = lastWidth_ = lastMonoBelow_ = -1.0f;
        lastERLevel_ = -2.0f;
        lastGainTrim_ = -999.0f;
        haveLastFreeze_ = false;
        haveLastGateEnabled_ = false;
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

    // ---- Detect preset-apply request from the message thread ----
    // Acquire pairs with the release store in applyFactoryPreset() so the
    // SixAPBrightnessState writes preceding the flag are visible here. The
    // swap reconfigures the idle engine from the just-applied APVTS values
    // and arms the equal-power crossfade window.
    if (pendingPresetSwap_.exchange (false, std::memory_order_acquire))
        performPresetSwap();

    // ---- Push parameter changes to the engine on edges only ----

    // Algorithm
    int algoIdx = static_cast<int> (algorithmParam_->load());
    if (algoIdx != cachedAlgorithm_)
    {
        cachedAlgorithm_ = algoIdx;
        activeEngine_->setAlgorithm (algoIdx);
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
        activeEngine_->setPreDelay (preDelayMs);
    }

    // Tank parameters
    auto pushIfChanged = [] (float& last, float current, auto setter) {
        if (current != last) { last = current; setter (current); }
    };

    pushIfChanged (lastDecaySec_,  decayParam_->load(),     [this] (float v) { activeEngine_->setDecayTime (v); });
    pushIfChanged (lastSize_,      sizeParam_->load(),      [this] (float v) { activeEngine_->setSize (v); });
    pushIfChanged (lastDamping_,   dampingParam_->load(),   [this] (float v) { activeEngine_->setTrebleMultiply (v); });
    pushIfChanged (lastBassMult_,  bassMultParam_->load(),  [this] (float v) { activeEngine_->setBassMultiply (v); });
    pushIfChanged (lastMidMult_,   midMultParam_->load(),   [this] (float v) { activeEngine_->setMidMultiply (v); });
    pushIfChanged (lastCrossover_, crossoverParam_->load(), [this] (float v) { activeEngine_->setCrossoverFreq (v); });
    pushIfChanged (lastHighCrossover_, highCrossoverParam_->load(), [this] (float v) { activeEngine_->setHighCrossoverFreq (v); });
    pushIfChanged (lastSaturation_,    saturationParam_->load(),    [this] (float v) { activeEngine_->setSaturation (v); });
    pushIfChanged (lastDiffusion_, diffusionParam_->load(), [this] (float v) { activeEngine_->setDiffusion (v); });
    pushIfChanged (lastModDepth_,  modDepthParam_->load(),  [this] (float v) { activeEngine_->setModDepth (v); });
    pushIfChanged (lastModRate_,   modRateParam_->load(),   [this] (float v) { activeEngine_->setModRate (v); });
    pushIfChanged (lastERSize_,    erSizeParam_->load(),    [this] (float v) { activeEngine_->setERSize (v); });
    pushIfChanged (lastERLevel_,   erLevelParam_->load(),   [this] (float v) { activeEngine_->setERLevel (v); });
    pushIfChanged (lastLoCut_,     loCutParam_->load(),     [this] (float v) { activeEngine_->setLoCut (v); });
    pushIfChanged (lastHiCut_,     hiCutParam_->load(),     [this] (float v) { activeEngine_->setHiCut (v); });
    pushIfChanged (lastWidth_,     widthParam_->load(),     [this] (float v) { activeEngine_->setWidth (v); });
    pushIfChanged (lastGainTrim_,  gainTrimParam_->load(),  [this] (float v) { activeEngine_->setGainTrim (v); });
    pushIfChanged (lastMonoBelow_, monoBelowParam_->load(), [this] (float v) { activeEngine_->setMonoBelow (v); });

    // Mix: bus_mode forces 100 % wet (override of user mix knob).
    const bool busMode = busModeParam_->load() >= 0.5f;
    const float mixVal = busMode ? 1.0f : mixParam_->load();
    pushIfChanged (lastMix_, mixVal, [this] (float v) { activeEngine_->setMix (v); });

    // Freeze (boolean — push only on transitions).
    const bool freezeNow = freezeParam_->load() >= 0.5f;
    if (! haveLastFreeze_ || freezeNow != lastFreeze_)
    {
        activeEngine_->setFreeze (freezeNow);
        lastFreeze_     = freezeNow;
        haveLastFreeze_ = true;
    }

    // Gate enable (NonLinear-engine only — wrapper forwards to NonLinear,
    // no-op for other algorithms). Push only on transitions.
    const bool gateEnabledNow = gateEnabledParam_->load() >= 0.5f;
    if (! haveLastGateEnabled_ || gateEnabledNow != lastGateEnabled_)
    {
        activeEngine_->setNonLinearGateEnabled (gateEnabledNow);
        lastGateEnabled_     = gateEnabledNow;
        haveLastGateEnabled_ = true;
    }

    // Save a copy of the input for the previous engine before the active
    // engine consumes the buffer in place. Only needed during a preset
    // crossfade — outside that window the second engine sits idle.
    const bool fading = (presetFadeRemaining_ > 0 && previousEngine_ != nullptr);
    if (fading)
    {
        std::memcpy (fadeBufL_.data(), left,  static_cast<size_t> (numSamples) * sizeof (float));
        std::memcpy (fadeBufR_.data(), right, static_cast<size_t> (numSamples) * sizeof (float));
    }

    activeEngine_->process (left, right, numSamples);

    if (fading)
    {
        // Run the saved input through the previous engine so its tail keeps
        // evolving (modulation, internal feedback) instead of being a static
        // snapshot. Then equal-power crossfade the two engines' outputs.
        previousEngine_->process (fadeBufL_.data(), fadeBufR_.data(), numSamples);

        const int samplesToCrossfade = std::min (numSamples, presetFadeRemaining_);
        const float invTotal = (presetFadeTotal_ > 0)
                                  ? 1.0f / static_cast<float> (presetFadeTotal_)
                                  : 0.0f;
        const int idxBase = presetFadeTotal_ - presetFadeRemaining_;
        constexpr float kHalfPi = 1.5707963267948966f;

        for (int i = 0; i < samplesToCrossfade; ++i)
        {
            const float t    = static_cast<float> (idxBase + i) * invTotal;
            const float gNew = std::sin (t * kHalfPi);
            const float gOld = std::cos (t * kHalfPi);
            left[i]  = left[i]  * gNew + fadeBufL_[i] * gOld;
            right[i] = right[i] * gNew + fadeBufR_[i] * gOld;
        }

        presetFadeRemaining_ -= samplesToCrossfade;
        if (presetFadeRemaining_ <= 0)
        {
            previousEngine_      = nullptr;
            presetFadeRemaining_ = 0;
        }
        // Samples past samplesToCrossfade in this block are pure activeEngine_
        // output (already in left/right) — leave them alone.
    }

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
// v2 (2026-04-28): adds per-preset SixAPTank brightness/density properties
// (sixAPDensityBaseline, sixAPBloomCeiling, sixAPBloomStagger0..5,
// sixAPEarlyMix, sixAPOutputTrim) to the state ValueTree. Loading a v1
// save file is fully supported — missing properties fall through to the
// engine's default values, which match the v1 historical behavior.
static constexpr int kStateVersion = 2;
static const juce::Identifier kStateVersionId { "stateVersion" };

void DuskVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (kStateVersionId, kStateVersion, nullptr);
    // SixAPTank brightness/density (per-preset, non-APVTS). Defaults equal
    // historical engine constants — files saved with v1 won't have these
    // and will fall through to the same defaults on load.
    state.setProperty ("sixAPDensityBaseline", sixAPBrightness_.densityBaseline, nullptr);
    state.setProperty ("sixAPBloomCeiling",    sixAPBrightness_.bloomCeiling,    nullptr);
    state.setProperty ("sixAPEarlyMix",        sixAPBrightness_.earlyMix,        nullptr);
    state.setProperty ("sixAPOutputTrim",      sixAPBrightness_.outputTrim,      nullptr);
    for (int i = 0; i < 6; ++i)
        state.setProperty (juce::Identifier ("sixAPBloomStagger" + juce::String (i)),
                          sixAPBrightness_.bloomStagger[i], nullptr);
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
    // already wire-compatible with v1's parameter layout. v2 added the
    // SixAPTank brightness state — falls through to defaults if absent.
    const int version = tree.hasProperty (kStateVersionId)
                      ? static_cast<int> (tree.getProperty (kStateVersionId))
                      : 1;
    if (version > kStateVersion)
        return;  // future state — keep current params, don't risk mis-mapping

    parameters.replaceState (tree);

    // Reset SixAPTank brightness state to engine defaults BEFORE reading the
    // optional v2+ properties. Without this reset, loading a v1 state file
    // (no sixAP properties) AFTER having loaded a preset that sets brighter
    // values (e.g. Black Hole) would silently inherit those values — applying
    // Black Hole's brightness to a non-Black-Hole preset's reverb. The default
    // values of SixAPBrightnessState exactly match the engine's historical
    // hardcoded constants, so v1 states correctly round-trip to identical
    // sound after this reset + the property reads below.
    sixAPBrightness_ = SixAPBrightnessState{};

    // Read SixAPTank brightness state — overrides defaults only if present
    // (any subset of properties may be missing on partial v2 saves).
    if (tree.hasProperty ("sixAPDensityBaseline"))
        sixAPBrightness_.densityBaseline = static_cast<float> (tree.getProperty ("sixAPDensityBaseline"));
    if (tree.hasProperty ("sixAPBloomCeiling"))
        sixAPBrightness_.bloomCeiling = static_cast<float> (tree.getProperty ("sixAPBloomCeiling"));
    if (tree.hasProperty ("sixAPEarlyMix"))
        sixAPBrightness_.earlyMix = static_cast<float> (tree.getProperty ("sixAPEarlyMix"));
    if (tree.hasProperty ("sixAPOutputTrim"))
        sixAPBrightness_.outputTrim = static_cast<float> (tree.getProperty ("sixAPOutputTrim"));
    for (int i = 0; i < 6; ++i)
    {
        const juce::Identifier id ("sixAPBloomStagger" + juce::String (i));
        if (tree.hasProperty (id))
            sixAPBrightness_.bloomStagger[i] = static_cast<float> (tree.getProperty (id));
    }
    // State load happens before audio starts (or via host-driven message-thread
    // call). Push to both engines so the idle one is in sync for the next
    // preset swap. No fade involved — this is a hard reset to the saved state.
    pushSixAPBrightnessTo (engineA_);
    pushSixAPBrightnessTo (engineB_);
}

void DuskVerbProcessor::pushSixAPBrightnessTo (DuskVerbEngine& target)
{
    target.setSixAPDensityBaseline (sixAPBrightness_.densityBaseline);
    target.setSixAPBloomCeiling    (sixAPBrightness_.bloomCeiling);
    target.setSixAPBloomStagger    (sixAPBrightness_.bloomStagger);
    target.setSixAPEarlyMix        (sixAPBrightness_.earlyMix);
    target.setSixAPOutputTrim      (sixAPBrightness_.outputTrim);
}

void DuskVerbProcessor::forcePushAllParametersTo (DuskVerbEngine* target)
{
    // Audio thread. Reads the current APVTS values + cached SixAPBrightness
    // state and pushes everything unconditionally. Used at preset-swap time
    // so the freshly-cleared idle engine starts the fade already configured
    // with the new preset's voicing — no edge-detection-driven trickle-in.
    target->setAlgorithm (static_cast<int> (algorithmParam_->load()));

    // Pre-delay: push the raw param value (without tempo-sync resolution).
    // Any sync-driven delta will be picked up by the next processBlock's
    // edge detection — a 1-2 sample drift is inaudible and avoids duplicating
    // the playhead/BPM lookup logic here.
    target->setPreDelay (preDelayParam_->load());

    target->setDecayTime         (decayParam_->load());
    target->setSize              (sizeParam_->load());
    target->setTrebleMultiply    (dampingParam_->load());
    target->setBassMultiply      (bassMultParam_->load());
    target->setMidMultiply       (midMultParam_->load());
    target->setCrossoverFreq     (crossoverParam_->load());
    target->setHighCrossoverFreq (highCrossoverParam_->load());
    target->setSaturation        (saturationParam_->load());
    target->setDiffusion         (diffusionParam_->load());
    target->setModDepth          (modDepthParam_->load());
    target->setModRate           (modRateParam_->load());
    target->setERSize            (erSizeParam_->load());
    target->setERLevel           (erLevelParam_->load());
    target->setLoCut             (loCutParam_->load());
    target->setHiCut             (hiCutParam_->load());
    target->setWidth             (widthParam_->load());
    target->setGainTrim          (gainTrimParam_->load());
    target->setMonoBelow         (monoBelowParam_->load());

    const bool busMode = busModeParam_->load() >= 0.5f;
    target->setMix (busMode ? 1.0f : mixParam_->load());

    target->setFreeze              (freezeParam_->load() >= 0.5f);
    target->setNonLinearGateEnabled(gateEnabledParam_->load() >= 0.5f);

    pushSixAPBrightnessTo (*target);
}

void DuskVerbProcessor::syncParameterCacheToCurrent()
{
    // Update edge-detection cache so the next processBlock doesn't re-push
    // values we just force-pushed to the new active engine.
    cachedAlgorithm_   = static_cast<int> (algorithmParam_->load());
    lastPreDelayMs_    = preDelayParam_->load();
    lastDecaySec_      = decayParam_->load();
    lastSize_          = sizeParam_->load();
    lastDamping_       = dampingParam_->load();
    lastBassMult_      = bassMultParam_->load();
    lastMidMult_       = midMultParam_->load();
    lastCrossover_     = crossoverParam_->load();
    lastHighCrossover_ = highCrossoverParam_->load();
    lastSaturation_    = saturationParam_->load();
    lastDiffusion_     = diffusionParam_->load();
    lastModDepth_      = modDepthParam_->load();
    lastModRate_       = modRateParam_->load();
    lastERSize_        = erSizeParam_->load();
    lastERLevel_       = erLevelParam_->load();
    lastLoCut_         = loCutParam_->load();
    lastHiCut_         = hiCutParam_->load();
    lastWidth_         = widthParam_->load();
    lastGainTrim_      = gainTrimParam_->load();
    lastMonoBelow_     = monoBelowParam_->load();

    const bool busMode = busModeParam_->load() >= 0.5f;
    lastMix_ = busMode ? 1.0f : mixParam_->load();

    lastFreeze_          = freezeParam_->load()      >= 0.5f;
    haveLastFreeze_      = true;
    lastGateEnabled_     = gateEnabledParam_->load() >= 0.5f;
    haveLastGateEnabled_ = true;
}

void DuskVerbProcessor::performPresetSwap()
{
    // Audio thread. Pick the idle engine (the one not currently producing
    // output), reset it to silence, force-push the just-applied parameters
    // and brightness state, snap its shell smoothers to the new targets,
    // then swap pointers and arm the equal-power crossfade.
    DuskVerbEngine* newActive = (activeEngine_ == &engineA_) ? &engineB_ : &engineA_;

    newActive->clearAllBuffers();
    forcePushAllParametersTo (newActive);
    newActive->snapSmoothersToTargets();

    // If a fade was already in progress, the prior fading-out engine gets
    // dropped here — the current active becomes the new fading-out engine
    // instead. Cleaner than queuing fades, and rapid preset cycling stays
    // responsive (the user hears the latest preset within one fade window).
    previousEngine_ = activeEngine_;
    activeEngine_   = newActive;

    // ~50 ms equal-power fade. Long enough to mask the swap transient,
    // short enough that the new preset's onset is audible quickly. Clamped
    // to the prepared block size lower bound so fades complete within a
    // few processBlock calls even on tiny buffer sizes.
    constexpr float kFadeSeconds = 0.050f;
    presetFadeTotal_     = std::max (1, static_cast<int> (preparedSampleRate_ * kFadeSeconds));
    presetFadeRemaining_ = presetFadeTotal_;

    syncParameterCacheToCurrent();
}

void DuskVerbProcessor::applyFactoryPreset (const FactoryPreset& preset)
{
    // Message thread. Update APVTS + cache the engine-config state, then arm
    // the swap flag. The audio thread picks it up at the top of the next
    // processBlock(), runs performPresetSwap() to reconfigure the idle engine
    // from the just-applied APVTS values, and starts the crossfade. Touching
    // the engine directly here would race with processBlock.
    preset.applyTo (parameters);

    sixAPBrightness_.densityBaseline = preset.sixAPDensityBaseline;
    sixAPBrightness_.bloomCeiling    = preset.sixAPBloomCeiling;
    sixAPBrightness_.earlyMix        = preset.sixAPEarlyMix;
    sixAPBrightness_.outputTrim      = preset.sixAPOutputTrim;
    for (int i = 0; i < 6; ++i)
        sixAPBrightness_.bloomStagger[i] = preset.sixAPBloomStagger[i];

    // Release ordering pairs with the acquire load in processBlock to publish
    // the brightness writes above.
    pendingPresetSwap_.store (true, std::memory_order_release);
}

// Out-of-line definition of FactoryPreset::applyEngineConfig — placed here
// where DuskVerbEngine's full type is visible.
void FactoryPreset::applyEngineConfig (DuskVerbEngine& engine) const
{
    engine.setSixAPDensityBaseline (sixAPDensityBaseline);
    engine.setSixAPBloomCeiling    (sixAPBloomCeiling);
    engine.setSixAPBloomStagger    (sixAPBloomStagger);
    engine.setSixAPEarlyMix        (sixAPEarlyMix);
    engine.setSixAPOutputTrim      (sixAPOutputTrim);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskVerbProcessor();
}

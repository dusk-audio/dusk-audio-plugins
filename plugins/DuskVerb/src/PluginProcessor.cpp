#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

#include <cstring>

juce::AudioProcessorValueTreeState::ParameterLayout DuskVerbProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "algorithm", 1 }, "Algorithm",
        juce::StringArray { "PresetHomestarBladeRunner", "PresetPadHall", "PresetHugeSynthHall", "PresetSnareHall", "PresetVocalHall", "PresetDrumPlate", "PresetLargePlate", "PresetVocalPlate", "PresetDarkVocalRoom", "PresetExcitingSnareRoom", "PresetShortDarkSnareRoom", "PresetClearChamber", "PresetFatPlate", "PresetLargeChamber", "PresetMediumGate", "PresetSmallChamber1", "PresetSnarePlate", "PresetThinPlate", "PresetBigAmbienceGate", "PresetDrumAir", "PresetGatedSnare", "PresetMedAmbience", "PresetSmallDrumRoom", "PresetTightAmbienceGate", "PresetVerySmallAmbience" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay", 1 }, "Decay Time",
        juce::NormalisableRange<float> (0.2f, 30.0f, 0.0f, 0.4f), 2.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "predelay", 1 }, "Pre-Delay",
        juce::NormalisableRange<float> (0.0f, 250.0f, 0.0f, 1.0f), 15.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "size", 1 }, "Size",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    // Treble range expanded to [0.1, 1.5] so factory values can honestly
    // reflect the per-preset treble multiplier (the old quadratic curve in the
    // wrapper is removed — knob now maps linearly to engine multiplier).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "damping", 1 }, "Treble Multiply",
        juce::NormalisableRange<float> (0.1f, 1.5f), 1.0f));

    // Bass Multiply range expanded to [0.3, 2.5] so factory values can honestly
    // reflect the per-preset kBakedBassMultScale baked into the engine.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_mult", 1 }, "Bass Multiply",
        juce::NormalisableRange<float> (0.3f, 2.5f), 1.2f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "crossover", 1 }, "Crossover",
        juce::NormalisableRange<float> (200.0f, 4000.0f, 0.0f, 0.5f), 1000.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "diffusion", 1 }, "Diffusion",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.75f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mod_depth", 1 }, "Mod Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mod_rate", 1 }, "Mod Rate",
        juce::NormalisableRange<float> (0.10f, 10000.0f, 0.0f, 0.12f), 0.8f));

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
        juce::NormalisableRange<float> (0.0f, 1.5f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "freeze", 1 }, "Freeze", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "predelay_sync", 1 }, "Pre-Delay Sync",
        juce::StringArray { "Free", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bus_mode", 1 }, "Bus Mode", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gate_hold", 1 }, "Gate Hold",
        juce::NormalisableRange<float> (0.0f, 2000.0f, 0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gate_release", 1 }, "Gate Release",
        juce::NormalisableRange<float> (1.0f, 500.0f, 0.0f, 1.0f), 50.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain_trim", 1 }, "Gain Trim",
        juce::NormalisableRange<float> (-48.0f, 48.0f, 0.1f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input_onset", 1 }, "Input Onset",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delay_scale", 1 }, "Delay Scale",
        juce::NormalisableRange<float> (0.25f, 4.0f, 0.01f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "soft_onset", 1 }, "Soft Onset",
        juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f), 0.0f));

    // late_feed_fwd: -1 = use algorithm default (0.15). Range includes -1 as sentinel.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "late_feed_fwd", 1 }, "Late Feed Forward",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), -1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "limiter_thresh", 1 }, "Limiter Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), 0.0f));  // 0 = disabled

    // Hidden tap position parameters: 7 left + 7 right = 14 tap position fractions (0-1)
    // These control WHERE in the DattorroTank's delay buffers the output is read from.
    // Adjustable at runtime via pedalboard for closed-loop temporal envelope optimization.
    for (int i = 0; i < 14; ++i)
    {
        auto id = juce::String ("tap_pos_") + juce::String (i);
        auto name = juce::String ("Tap Pos ") + juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.05f, 0.99f, 0.01f), 0.75f));
    }

    // Hidden tap gain parameters: 7 left + 7 right = 14 per-tap amplitude weights (0-2)
    // Controls HOW MUCH energy each output tap contributes. Shapes onset envelope.
    // < 1.0 attenuates early taps (slows onset), > 1.0 boosts (faster onset).
    for (int i = 0; i < 14; ++i)
    {
        auto id = juce::String ("tap_gain_") + juce::String (i);
        auto name = juce::String ("Tap Gain ") + juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f));
    }

    // Hidden preset ID for triggering per-preset ER tap loading (0 = none, 1-53 = preset index)
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "preset_id", 1 }, "Preset ID", 0, 53, 0));

    // --- Optimizer-tunable overrides ---
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "air_damping", 1 }, "Air Damping",
        juce::NormalisableRange<float> (-1.0f, 2.0f, 0.01f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "high_crossover", 1 }, "High Crossover",
        juce::NormalisableRange<float> (-1.0f, 20000.0f, 1.0f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "noise_mod", 1 }, "Noise Mod",
        juce::NormalisableRange<float> (-1.0f, 20.0f, 0.1f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "inline_diffusion", 1 }, "Inline Diffusion",
        juce::NormalisableRange<float> (-1.0f, 0.75f, 0.01f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "stereo_coupling", 1 }, "Stereo Coupling",
        juce::NormalisableRange<float> (-2.0f, 0.75f, 0.01f), -2.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "chorus_depth", 1 }, "Chorus Depth",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "chorus_rate", 1 }, "Chorus Rate",
        juce::NormalisableRange<float> (-1.0f, 5.0f, 0.01f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_gain", 1 }, "Output Gain",
        juce::NormalisableRange<float> (-1.0f, 20.0f, 0.01f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_crossfeed", 1 }, "ER Crossfeed",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay_time_scale", 1 }, "Decay Time Scale",
        juce::NormalisableRange<float> (-1.0f, 10.0f, 0.01f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay_boost", 1 }, "Decay Boost",
        juce::NormalisableRange<float> (-1.0f, 2.0f, 0.01f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "structural_hf_damp", 1 }, "Structural HF Damp",
        juce::NormalisableRange<float> (-1.0f, 20000.0f, 1.0f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_low_shelf_db", 1 }, "Output Low Shelf dB",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_high_shelf_db", 1 }, "Output High Shelf dB",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_high_shelf_hz", 1 }, "Output High Shelf Hz",
        juce::NormalisableRange<float> (0.0f, 16000.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_mid_eq_db", 1 }, "Output Mid EQ dB",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_mid_eq_hz", 1 }, "Output Mid EQ Hz",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "terminal_threshold", 1 }, "Terminal Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "terminal_factor", 1 }, "Terminal Factor",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_air_ceiling", 1 }, "ER Air Ceiling Hz",
        juce::NormalisableRange<float> (0.0f, 20000.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_air_floor", 1 }, "ER Air Floor Hz",
        juce::NormalisableRange<float> (0.0f, 20000.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

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
    gateHoldParam_ = parameters.getRawParameterValue ("gate_hold");
    gateReleaseParam_ = parameters.getRawParameterValue ("gate_release");
    gainTrimParam_ = parameters.getRawParameterValue ("gain_trim");
    inputOnsetParam_ = parameters.getRawParameterValue ("input_onset");
    delayScaleParam_ = parameters.getRawParameterValue ("delay_scale");
    softOnsetParam_ = parameters.getRawParameterValue ("soft_onset");
    lateFeedFwdParam_ = parameters.getRawParameterValue ("late_feed_fwd");
    limiterThreshParam_ = parameters.getRawParameterValue ("limiter_thresh");
    for (int i = 0; i < 14; ++i)
        tapPosParams_[i] = parameters.getRawParameterValue (
            juce::String ("tap_pos_") + juce::String (i));
    for (int i = 0; i < 14; ++i)
        tapGainParams_[i] = parameters.getRawParameterValue (
            juce::String ("tap_gain_") + juce::String (i));
    presetIdParam_ = parameters.getRawParameterValue ("preset_id");
    airDampingParam_ = parameters.getRawParameterValue ("air_damping");
    highCrossoverParam_ = parameters.getRawParameterValue ("high_crossover");
    noiseModParam_ = parameters.getRawParameterValue ("noise_mod");
    inlineDiffParam_ = parameters.getRawParameterValue ("inline_diffusion");
    stereoCouplingParam_ = parameters.getRawParameterValue ("stereo_coupling");
    chorusDepthParam_ = parameters.getRawParameterValue ("chorus_depth");
    chorusRateParam_ = parameters.getRawParameterValue ("chorus_rate");
    outputGainParam_ = parameters.getRawParameterValue ("output_gain");
    erCrossfeedParam_ = parameters.getRawParameterValue ("er_crossfeed");
    decayTimeScaleParam_ = parameters.getRawParameterValue ("decay_time_scale");
    decayBoostParam_ = parameters.getRawParameterValue ("decay_boost");
    structHFDampParam_ = parameters.getRawParameterValue ("structural_hf_damp");
    outputLowShelfDBParam_ = parameters.getRawParameterValue ("output_low_shelf_db");
    outputHighShelfDBParam_ = parameters.getRawParameterValue ("output_high_shelf_db");
    outputHighShelfHzParam_ = parameters.getRawParameterValue ("output_high_shelf_hz");
    outputMidEQDBParam_ = parameters.getRawParameterValue ("output_mid_eq_db");
    outputMidEQHzParam_ = parameters.getRawParameterValue ("output_mid_eq_hz");
    terminalThresholdParam_ = parameters.getRawParameterValue ("terminal_threshold");
    terminalFactorParam_ = parameters.getRawParameterValue ("terminal_factor");
    erAirCeilingParam_ = parameters.getRawParameterValue ("er_air_ceiling");
    erAirFloorParam_ = parameters.getRawParameterValue ("er_air_floor");
    bypassParam_ = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter ("bypass"));
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
    // Skip the full re-prepare if the sample rate is unchanged and the new
    // block size fits in the previously-allocated buffers. Pedalboard (and
    // some hosts) calls prepareToPlay redundantly before each process() call
    // with the current input buffer size; without this guard the engine is
    // destroyed and rebuilt every time, which makes LFO phase
    // non-deterministic across calls.
    //
    // To handle hosts that escalate block sizes (pedalboard sends bs=512 on
    // load, then bs=N to match each call's input length), we always prepare
    // with at least kMinPreparedBlockSize so subsequent calls fit without
    // forcing a re-prepare.
    constexpr int kMinPreparedBlockSize = 4096;
    int safeBlockSize = std::max (samplesPerBlock, kMinPreparedBlockSize);
    bool needsReallocation = (preparedSampleRate_ != sampleRate || safeBlockSize > preparedBlockSize_);

    if (needsReallocation)
    {
        preparedSampleRate_ = sampleRate;
        preparedBlockSize_ = safeBlockSize;

        // Forward saved delay scale BEFORE prepare() so DattorroTank allocates
        // buffers large enough for the restored parameter value.
        engine_.setDattorroDelayScale (delayScaleParam_->load());

        engine_.prepare (sampleRate, safeBlockSize);
        originalLatencySamples_ = 0;  // FDN reverb has zero inherent latency
        setLatencySamples (originalLatencySamples_);

        // Invalidate preset ER reload sentinels so loadPresetERTaps/loadCorrectionFilter
        // run on the next processBlock after re-initialization.
        lastPresetId_ = -1;
        lastPresetPreDelayMs_ = -1.0f;

        // Initialize algorithm from saved state (edge-case: DAW restores state before first processBlock)
        cachedAlgorithm_ = static_cast<int> (algorithmParam_->load());
        engine_.setAlgorithm (cachedAlgorithm_);
    }

    // Sync cached algorithm on redundant prepareToPlay(). If the host
    // restored a different algorithm (e.g. state recall before first
    // processBlock), we must call engine_.setAlgorithm() here — otherwise
    // processBlock() would see cachedAlgorithm_ already matching the param
    // and skip the change, leaving the engine on the old algorithm.
    if (! needsReallocation)
    {
        int newAlgorithm = static_cast<int> (algorithmParam_->load());
        if (newAlgorithm != cachedAlgorithm_)
        {
            cachedAlgorithm_ = newAlgorithm;
            engine_.setAlgorithm (newAlgorithm);
        }
    }

    // Only reset smoothers when the engine was actually (re)initialized.
    // Hosts that redundantly call prepareToPlay() with the same sample rate
    // would otherwise kill in-progress parameter ramps, causing discontinuities.
    if (needsReallocation)
    {
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

    startTimerHz (30);
}

void DuskVerbProcessor::releaseResources()
{
    stopTimer();

    // Invalidate prepare guard so next prepareToPlay() calls engine_.prepare()
    preparedSampleRate_ = 0.0;
    preparedBlockSize_ = 0;

    // Invalidate preset ER sentinels so the next processBlock rebuilds ER state
    lastPresetId_ = -1;
    lastPresetPreDelayMs_ = -1.0f;
}

void DuskVerbProcessor::timerCallback()
{
    // exchange() atomically drains the pending value so a concurrent write
    // from processBlock between a load and a store can't be lost.
    int latency = pendingLatency_.exchange (-1, std::memory_order_relaxed);
    if (latency >= 0 && latency != getLatencySamples())
        setLatencySamples (latency);
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

    // Handle mono input: duplicate channel 0 to channel 1 BEFORE bypass,
    // so mono-in/stereo-out bypass still outputs audio on both channels.
    if (totalNumInputChannels == 1 && totalNumOutputChannels == 2)
    {
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    }

    // Clear any further unused output channels (3+)
    for (int ch = std::max (totalNumInputChannels, 2); ch < totalNumOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Bypass: pass audio through unprocessed but still update input + output meters
    if (bypassParam_ != nullptr && bypassParam_->get())
    {
        // Signal latency change for timer to pick up on the message thread
        if (pendingLatency_.load (std::memory_order_relaxed) != 0)
            pendingLatency_.store (0, std::memory_order_relaxed);
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
        inputLevelL_.store (dbL, std::memory_order_relaxed);
        inputLevelR_.store (dbR, std::memory_order_relaxed);
        outputLevelL_.store (dbL, std::memory_order_relaxed);
        outputLevelR_.store (dbR, std::memory_order_relaxed);
        return;
    }

    // Restore latency after bypass (timer will apply on the message thread)
    if (getLatencySamples() != originalLatencySamples_
        && pendingLatency_.load (std::memory_order_relaxed) != originalLatencySamples_)
        pendingLatency_.store (originalLatencySamples_, std::memory_order_relaxed);

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

    // Gate: discrete parameters, no smoothing needed
    engine_.setGateParams (gateHoldParam_->load(), gateReleaseParam_->load());

    // Gain trim: per-preset level correction (dB)
    engine_.setGainTrim (gainTrimParam_->load());

    // Input onset ramp: shapes FDN input to match VV's serial onset
    engine_.setInputOnset (inputOnsetParam_->load());

    // Runtime delay scale: controls DattorroTank loop length per-preset
    engine_.setDattorroDelayScale (delayScaleParam_->load());

    // Soft onset: output transient smoothing for DattorroTank (0 = disabled)
    engine_.setDattorroSoftOnsetMs (softOnsetParam_->load());

    // Late feed-forward level: -1 = use algorithm default, >= 0 = override
    {
        float lff = lateFeedFwdParam_->load();
        if (lff >= 0.0f)
            engine_.setLateFeedForwardLevel (lff);
        else
            engine_.resetLateFeedForwardLevel();
    }

    // Per-preset ER taps: load VV-extracted taps when preset_id changes
    // OR when the effective pre-delay changes (tap timing depends on it).
    // Must apply pre-delay first so tap timing is computed correctly
    // (VV tap times are absolute; DV's pre-delay is subtracted).
    {
        int presetId = static_cast<int> (presetIdParam_->load());
        bool presetChanged = presetId != lastPresetId_;
        bool preDelayChanged = std::abs (preDelayMs - lastPresetPreDelayMs_) > 0.001f;

        if (presetChanged)
        {
            // Full preset update: tap positions, correction filter, and ER taps
            lastPresetId_ = presetId;
            lastPresetPreDelayMs_ = preDelayMs;

            engine_.setPreDelay (preDelayMs);
            preDelaySmooth_.setCurrentAndTargetValue (preDelayMs);

            if (presetId > 0 && presetId <= 53)
            {
                const auto& presets = getFactoryPresets();
                int idx = presetId - 1;
                if (idx < static_cast<int> (presets.size()))
                {
                    engine_.loadPresetERTaps (presets[static_cast<size_t> (idx)].name);
                    engine_.loadPresetTapPositions (idx);
                    engine_.loadCorrectionFilter (idx);
                }
                else
                {
                    engine_.setCustomERTaps (nullptr, 0);
                    engine_.loadCorrectionFilter (-1);
                }
            }
            else
            {
                engine_.setCustomERTaps (nullptr, 0);  // Revert to generated
                engine_.loadCorrectionFilter (-1);      // Disable correction
            }
        }
        else if (preDelayChanged)
        {
            // Pre-delay only: update timing without reloading tap positions or correction filter
            lastPresetPreDelayMs_ = preDelayMs;

            engine_.setPreDelay (preDelayMs);
            preDelaySmooth_.setCurrentAndTargetValue (preDelayMs);

            if (presetId > 0 && presetId <= 53)
            {
                const auto& presets = getFactoryPresets();
                int idx = presetId - 1;
                if (idx < static_cast<int> (presets.size()))
                    engine_.loadPresetERTaps (presets[static_cast<size_t> (idx)].name);
                else
                    engine_.setCustomERTaps (nullptr, 0);
            }
            else
            {
                engine_.setCustomERTaps (nullptr, 0);
            }
        }
    }

    // Apply runtime tap gains on top of preset tap positions.
    // Tap positions come from PresetTapPositions.h (loaded by loadPresetTapPositions).
    // Tap gains come from runtime parameters (tunable via pedalboard).
    // When preset_id == 0, tap_pos params also override positions.
    {
        int presetId = static_cast<int> (presetIdParam_->load());
        if (presetId == 0)
        {
            // No preset — use runtime tap_pos and tap_gain params directly
            engine_.updateTapPositionsAndGains (
                tapPosParams_[0]->load(), tapPosParams_[1]->load(), tapPosParams_[2]->load(),
                tapPosParams_[3]->load(), tapPosParams_[4]->load(), tapPosParams_[5]->load(),
                tapPosParams_[6]->load(), tapPosParams_[7]->load(), tapPosParams_[8]->load(),
                tapPosParams_[9]->load(), tapPosParams_[10]->load(), tapPosParams_[11]->load(),
                tapPosParams_[12]->load(), tapPosParams_[13]->load(),
                tapGainParams_[0]->load(), tapGainParams_[1]->load(), tapGainParams_[2]->load(),
                tapGainParams_[3]->load(), tapGainParams_[4]->load(), tapGainParams_[5]->load(),
                tapGainParams_[6]->load(), tapGainParams_[7]->load(), tapGainParams_[8]->load(),
                tapGainParams_[9]->load(), tapGainParams_[10]->load(), tapGainParams_[11]->load(),
                tapGainParams_[12]->load(), tapGainParams_[13]->load());
        }
        else
        {
            // Preset loaded — always apply tap_gain params on top of preset's
            // tap positions so returning all gains to 1.0 updates the engine.
            float gains[14];
            for (int i = 0; i < 14; ++i)
                gains[i] = tapGainParams_[i]->load();
            engine_.applyTapGains (gains[0], gains[1], gains[2], gains[3], gains[4], gains[5], gains[6],
                                   gains[7], gains[8], gains[9], gains[10], gains[11], gains[12], gains[13]);
        }
    }

    // Per-preset peak limiter on DattorroTank output (0 dB = disabled).
    // Must be set AFTER preset loading (which may call applyAlgorithmConfig).
    engine_.setDattorroLimiter (limiterThreshParam_->load(), 2.0f);

    // --- Optimizer-tunable overrides ---
    // Always forward to the engine. Sentinel values (negative or param-specific)
    // are handled inside each engine setter, which restores the config default so
    // moving a control back to sentinel clears a previously latched override.
    {
        engine_.setAirDampingOverride (airDampingParam_->load());
        engine_.setHighCrossoverOverride (highCrossoverParam_->load());
        engine_.setNoiseModOverride (noiseModParam_->load());
        engine_.setInlineDiffusionOverride (inlineDiffParam_->load());
        engine_.setStereoCouplingOverride (stereoCouplingParam_->load());
        engine_.setChorusDepthOverride (chorusDepthParam_->load());
        engine_.setChorusRateOverride (chorusRateParam_->load());
        engine_.setOutputGainOverride (outputGainParam_->load());
        engine_.setERCrossfeedOverride (erCrossfeedParam_->load());
        engine_.setDecayTimeScaleOverride (decayTimeScaleParam_->load());
        engine_.setDecayBoostOverride (decayBoostParam_->load());
        engine_.setStructuralHFDampingOverride (structHFDampParam_->load());

        // EQ overrides: 0 dB disables the filter inside the engine.
        engine_.setOutputLowShelfOverride (outputLowShelfDBParam_->load());

        float oHighDB = outputHighShelfDBParam_->load();
        float oHighHz = outputHighShelfHzParam_->load();
        engine_.setOutputHighShelfOverride (oHighDB, oHighHz > 0.0f ? oHighHz : 5000.0f);

        float oMidDB = outputMidEQDBParam_->load();
        float oMidHz = outputMidEQHzParam_->load();
        engine_.setOutputMidEQOverride (oMidDB, oMidHz > 0.0f ? oMidHz : 1000.0f);

        {
            float factor = terminalFactorParam_->load();
            engine_.setTerminalDecayOverride (terminalThresholdParam_->load(), factor);
        }

        engine_.setERAirCeilingOverride (erAirCeilingParam_->load());
        engine_.setERAirFloorOverride (erAirFloorParam_->load());
    }

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
        engine_.setOutputDiffusion (diffVal * 0.85f);

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
    {
        parameters.replaceState (juce::ValueTree::fromXml (xml));

        // Restore per-preset gain trim from the restored APVTS parameter.
        if (gainTrimParam_ != nullptr)
            engine_.setGainTrim (gainTrimParam_->load());
    }
}

void DuskVerbProcessor::setCustomERTaps (const CustomERTap* taps, int numTaps)
{
    engine_.setCustomERTaps (taps, numTaps);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskVerbProcessor();
}

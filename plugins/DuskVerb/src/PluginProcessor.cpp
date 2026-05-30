#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

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

    // FiveBandDamping (Phase 2, FDN only) — two extra decay plateaus + their
    // crossovers. Defaults map to the first preset's bass/treble mults so a
    // blank instance is transparent; factory presets override via applyTo.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sub_mult", 1 }, "Sub Multiply",
        juce::NormalisableRange<float> (0.1f, 2.0f), fp0.bassMult));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hi_mid_mult", 1 }, "Hi-Mid Multiply",
        juce::NormalisableRange<float> (0.1f, 2.0f), fp0.damping));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "crossover_sub", 1 }, "Sub Crossover",
        juce::NormalisableRange<float> (20.0f, 200.0f, 0.0f, 0.5f), 120.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "crossover_air", 1 }, "Air Crossover",
        juce::NormalisableRange<float> (4000.0f, 20000.0f, 0.0f, 0.5f), 8000.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_choke", 1 }, "Bass Choke",
        juce::NormalisableRange<float> (20.0f, 500.0f, 0.0f, 0.5f), fp0.bassChoke));

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

    // Phase 1 engine surgery (2026-05-28): the Hi Cut filter was upgraded
    // from a brick-wall biquad LP to a 2nd-order RBJ high-shelf. This
    // controls the shelf attenuation depth — 0 dB = shelf flat (Hi Cut
    // does nothing), -24 dB = deep tuck. Per-preset, automatable, and
    // tunable via the staged sweep. Not currently surfaced on the main
    // UI grid; lives in the "internal" automation category.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hi_cut_shelf_db", 1 }, "Hi Cut Shelf",
        juce::NormalisableRange<float> (-24.0f, 0.0f, 0.0f, 1.0f), fp0.hiCutShelfGainDb));

    // Phase α — PostTankEQ exposed as 4 GAIN APVTS params so the staged
    // tuner can sweep them in Stage 3 (the Polish stage that owns spectral
    // axes). Freq + Q stay in the per-preset kPostTankEQByName map for now
    // (12 user-visible knobs is a UI clutter problem; gain is the highest-
    // leverage axis the optimizer needs to touch). Defaults 0 dB = unity
    // bypass; no audible effect for presets that don't override.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pteq_band0_gain_db", 1 }, "PostTankEQ Band 0 Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pteq_band1_gain_db", 1 }, "PostTankEQ Band 1 Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pteq_band2_gain_db", 1 }, "PostTankEQ Band 2 Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pteq_band3_gain_db", 1 }, "PostTankEQ Band 3 Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));

    // Phase γ (2026-05-29): decoupled per-band linear gain trim post-tank.
    // 4 contiguous regions (Sub / Low-Mid / Mid-High / Air) over fixed
    // crossovers. Range [-8, +8] dB — corrective scalpel, not a tone shaper.
    // All defaults 0 dB → bit-identical bypass for presets that don't opt in.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "post_band_sub_db", 1 }, "Post Band Sub Gain",
        juce::NormalisableRange<float> (-8.0f, 8.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "post_band_lowmid_db", 1 }, "Post Band Low-Mid Gain",
        juce::NormalisableRange<float> (-8.0f, 8.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "post_band_midhi_db", 1 }, "Post Band Mid-High Gain",
        juce::NormalisableRange<float> (-8.0f, 8.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "post_band_air_db", 1 }, "Post Band Air Gain",
        juce::NormalisableRange<float> (-8.0f, 8.0f, 0.0f, 1.0f), 0.0f));

    // Phase δ (2026-05-29): per-band EDT shape — 4 regions × {attack_db,
    // tau_ms}. attack_db = 0 → AttackRamp bypassed (unity gain) → bit-
    // identical pass-through. Ranges: attack ±12 dB; tau 5..500 ms.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "edt_sub_attack_db", 1 }, "EDT Sub Attack",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "edt_sub_tau_ms", 1 }, "EDT Sub Tau",
        juce::NormalisableRange<float> (5.0f, 500.0f, 0.0f, 0.5f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "edt_lowmid_attack_db", 1 }, "EDT Low-Mid Attack",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "edt_lowmid_tau_ms", 1 }, "EDT Low-Mid Tau",
        juce::NormalisableRange<float> (5.0f, 500.0f, 0.0f, 0.5f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "edt_midhi_attack_db", 1 }, "EDT Mid-High Attack",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "edt_midhi_tau_ms", 1 }, "EDT Mid-High Tau",
        juce::NormalisableRange<float> (5.0f, 500.0f, 0.0f, 0.5f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "edt_air_attack_db", 1 }, "EDT Air Attack",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "edt_air_tau_ms", 1 }, "EDT Air Tau",
        juce::NormalisableRange<float> (5.0f, 500.0f, 0.0f, 0.5f), 100.0f));

    // Phase ε (2026-05-29): in-loop narrow-Q peaking — single peaking biquad
    // per FDN line, designed once at param-change time. gainDb = 0 →
    // designUnity → bit-identical bypass. Used to reinforce specific modal
    // frequencies inside the feedback loop (closes sine1k cold by boosting
    // the 1 kHz mode's loop gain).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "in_loop_peak_hz", 1 }, "In-Loop Peak Freq",
        juce::NormalisableRange<float> (200.0f, 8000.0f, 0.0f, 0.5f), 1000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "in_loop_peak_q", 1 }, "In-Loop Peak Q",
        juce::NormalisableRange<float> (0.5f, 10.0f, 0.0f, 0.7f), 2.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "in_loop_peak_db", 1 }, "In-Loop Peak Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));

    // Phase η (2026-05-29): per-line dual-time-constant bass shelf — both
    // gains default 0 dB → bit-identical bypass. fastFc/slowFc/transitionMs
    // are shape controls; FastGain/SlowGain are the active sweep axes.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_shelf_fast_fc", 1 }, "Bass Shelf Fast Fc",
        juce::NormalisableRange<float> (100.0f, 1000.0f, 0.0f, 0.5f), 400.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_shelf_slow_fc", 1 }, "Bass Shelf Slow Fc",
        juce::NormalisableRange<float> (50.0f, 500.0f, 0.0f, 0.5f), 200.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_shelf_fast_db", 1 }, "Bass Shelf Fast Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_shelf_slow_db", 1 }, "Bass Shelf Slow Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_shelf_transition_ms", 1 }, "Bass Shelf Transition",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.0f, 0.5f), 100.0f));

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

    // DattorroPlateVintage (algo 1) corrective EQ + brightness controls.
    // Exposed as APVTS so Optuna can sweep them via --param overrides and
    // the host can automate them. Active only when algorithm=1; ignored
    // (no audible effect) on other engines.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dpv_hf_shelf_db", 1 }, "DPV HF Shelf Gain",
        juce::NormalisableRange<float> (-12.0f, 24.0f, 0.1f), fp0.dpvHfShelfGainDb));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dpv_hf_shelf_hz", 1 }, "DPV HF Shelf Freq",
        juce::NormalisableRange<float> (2000.0f, 20000.0f, 1.0f, 0.5f), fp0.dpvHfShelfFreqHz));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dpv_struct_hf_damp_hz", 1 }, "DPV Struct HF Damp",
        juce::NormalisableRange<float> (2000.0f, 18000.0f, 1.0f, 0.5f), fp0.dpvStructHfDampHz));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dpv_box_cut_db", 1 }, "DPV Box Cut Gain",
        juce::NormalisableRange<float> (-12.0f, 6.0f, 0.1f), fp0.dpvBoxCutGainDb));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dpv_box_cut_hz", 1 }, "DPV Box Cut Freq",
        juce::NormalisableRange<float> (100.0f, 800.0f, 1.0f, 0.5f), fp0.dpvBoxCutFreqHz));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dpv_bass_shelf_db", 1 }, "DPV Bass Shelf Gain",
        juce::NormalisableRange<float> (-6.0f, 18.0f, 0.1f), fp0.dpvBassShelfGainDb));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dpv_bass_shelf_hz", 1 }, "DPV Bass Shelf Freq",
        juce::NormalisableRange<float> (60.0f, 500.0f, 1.0f, 0.5f), fp0.dpvBassShelfFreqHz));

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
    subMultParam_       = parameters.getRawParameterValue ("sub_mult");
    hiMidMultParam_     = parameters.getRawParameterValue ("hi_mid_mult");
    crossoverSubParam_  = parameters.getRawParameterValue ("crossover_sub");
    crossoverAirParam_  = parameters.getRawParameterValue ("crossover_air");
    crossoverParam_     = parameters.getRawParameterValue ("crossover");
    highCrossoverParam_ = parameters.getRawParameterValue ("high_crossover");
    bassChokeParam_     = parameters.getRawParameterValue ("bass_choke");
    saturationParam_    = parameters.getRawParameterValue ("saturation");
    diffusionParam_     = parameters.getRawParameterValue ("diffusion");
    erLevelParam_       = parameters.getRawParameterValue ("er_level");
    erSizeParam_        = parameters.getRawParameterValue ("er_size");
    loCutParam_         = parameters.getRawParameterValue ("lo_cut");
    hiCutParam_         = parameters.getRawParameterValue ("hi_cut");
    hiCutShelfDbParam_  = parameters.getRawParameterValue ("hi_cut_shelf_db");
    pteqBand0GainParam_ = parameters.getRawParameterValue ("pteq_band0_gain_db");
    pteqBand1GainParam_ = parameters.getRawParameterValue ("pteq_band1_gain_db");
    pteqBand2GainParam_ = parameters.getRawParameterValue ("pteq_band2_gain_db");
    pteqBand3GainParam_ = parameters.getRawParameterValue ("pteq_band3_gain_db");
    postBandSubParam_    = parameters.getRawParameterValue ("post_band_sub_db");
    postBandLowMidParam_ = parameters.getRawParameterValue ("post_band_lowmid_db");
    postBandMidHiParam_  = parameters.getRawParameterValue ("post_band_midhi_db");
    postBandAirParam_    = parameters.getRawParameterValue ("post_band_air_db");

    edtSubAtkParam_   = parameters.getRawParameterValue ("edt_sub_attack_db");
    edtSubTauParam_   = parameters.getRawParameterValue ("edt_sub_tau_ms");
    edtLowMidAtkParam_= parameters.getRawParameterValue ("edt_lowmid_attack_db");
    edtLowMidTauParam_= parameters.getRawParameterValue ("edt_lowmid_tau_ms");
    edtMidHiAtkParam_ = parameters.getRawParameterValue ("edt_midhi_attack_db");
    edtMidHiTauParam_ = parameters.getRawParameterValue ("edt_midhi_tau_ms");
    edtAirAtkParam_   = parameters.getRawParameterValue ("edt_air_attack_db");
    edtAirTauParam_   = parameters.getRawParameterValue ("edt_air_tau_ms");

    inLoopPeakHzParam_ = parameters.getRawParameterValue ("in_loop_peak_hz");
    inLoopPeakQParam_  = parameters.getRawParameterValue ("in_loop_peak_q");
    inLoopPeakDbParam_ = parameters.getRawParameterValue ("in_loop_peak_db");

    bassShelfFastFcParam_       = parameters.getRawParameterValue ("bass_shelf_fast_fc");
    bassShelfSlowFcParam_       = parameters.getRawParameterValue ("bass_shelf_slow_fc");
    bassShelfFastDbParam_       = parameters.getRawParameterValue ("bass_shelf_fast_db");
    bassShelfSlowDbParam_       = parameters.getRawParameterValue ("bass_shelf_slow_db");
    bassShelfTransitionParam_   = parameters.getRawParameterValue ("bass_shelf_transition_ms");
    widthParam_         = parameters.getRawParameterValue ("width");
    freezeParam_        = parameters.getRawParameterValue ("freeze");
    gateEnabledParam_   = parameters.getRawParameterValue ("gate_enabled");
    gainTrimParam_      = parameters.getRawParameterValue ("gain_trim");
    monoBelowParam_     = parameters.getRawParameterValue ("mono_below");

    dpvHfShelfDbParam_       = parameters.getRawParameterValue ("dpv_hf_shelf_db");
    dpvHfShelfHzParam_       = parameters.getRawParameterValue ("dpv_hf_shelf_hz");
    dpvStructHfDampHzParam_  = parameters.getRawParameterValue ("dpv_struct_hf_damp_hz");
    dpvBoxCutDbParam_        = parameters.getRawParameterValue ("dpv_box_cut_db");
    dpvBoxCutHzParam_        = parameters.getRawParameterValue ("dpv_box_cut_hz");
    dpvBassShelfDbParam_     = parameters.getRawParameterValue ("dpv_bass_shelf_db");
    dpvBassShelfHzParam_     = parameters.getRawParameterValue ("dpv_bass_shelf_hz");

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
        dryBufL_ .assign (static_cast<size_t> (safeBlockSize), 0.0f);
        dryBufR_ .assign (static_cast<size_t> (safeBlockSize), 0.0f);

        // Dry/wet mix smoother lives on the processor — see PluginProcessor.h.
        // 2 ms matches the engine's old internal mix-smoothing time so user
        // knob movements stay responsive but any preset-driven mix jump
        // ramps instead of stepping.
        mixSmoother_.setSmoothingTime (sampleRate, 2.0f);
        const bool busMode = busModeParam_ && busModeParam_->load() >= 0.5f;
        const float initialMix = busMode ? 1.0f
                                         : (mixParam_ ? mixParam_->load() : 1.0f);
        mixSmoother_.reset (initialMix);

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
        lastHiCutShelfDb_ = 999.0f;   // out-of-range sentinel forces push
        haveLastFreeze_ = false;
        haveLastGateEnabled_ = false;
    }

    // Re-install engine config on BOTH engines. The host may issue multiple
    // release+prepare cycles during init (VST3 wrapper, --param overrides,
    // state-load sequences). Each prepare resets ParametricBand coefficients
    // to designUnity via postTankEQ_.prepare → designUnity. Without this
    // re-install, setBand calls in performPresetSwap() would land BEFORE the
    // final prepare cycle, leaving the EQ flat at render time. Re-applying
    // here guarantees the PostTankEQ + modulation topology + sixAP overrides
    // survive every prepare cycle.
    if (auto* preset = lastAppliedPreset_.load (std::memory_order_acquire))
    {
        preset->applyEngineConfig (engineA_);
        preset->applyEngineConfig (engineB_);
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

    // Defensive clamp. JUCE contract: numSamples <= samplesPerBlock passed to
    // prepareToPlay(), and fadeBufL_/R_ + dryBufL_/R_ are sized to that
    // (kMinPreparedBlockSize-floored). Some hosts violate the contract on
    // tempo-sync edge cases or aggressive lookahead — without this clamp the
    // memcpy + indexed access below would overrun. Cap at preparedBlockSize_
    // so the worst case is a partially-processed block, not a crash. The
    // unprocessed tail is zeroed so the host doesn't see stale input or
    // garbage past the clamp boundary.
    if (preparedBlockSize_ > 0 && numSamples > preparedBlockSize_)
    {
        for (int ch = 0; ch < totalNumOutputChannels; ++ch)
            buffer.clear (ch, preparedBlockSize_, numSamples - preparedBlockSize_);
        numSamples = preparedBlockSize_;
    }

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
    //
    // Gated on `presetFadeRemaining_ == 0`: starting a new swap mid-fade
    // would mean clearAllBuffers'ing the engine that's currently fading out,
    // dropping its tail abruptly — audible as a click on rapid preset
    // cycling. Holding the flag until the current fade completes defers the
    // swap by up to ~50 ms; the UI stays responsive (each click registers,
    // it just applies after the prior tail finishes).
    if (presetFadeRemaining_ == 0
        && pendingPresetSwap_.exchange (false, std::memory_order_acquire))
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
    pushIfChanged (lastDamping_,   dampingParam_->load(),   [this] (float v) {
        activeEngine_->setTrebleMultiply (v);
        // Bug-fix 2026-05-30: setTrebleMultiply writes to FDNReverb::trebleMultiply_
        // which is never read in the loop. Push to setAirTrebleMultiply too so
        // damping edits actually drive FDN's per-line gHigh feedback gain.
        activeEngine_->setAirTrebleMultiply (v);
    });
    pushIfChanged (lastBassMult_,  bassMultParam_->load(),  [this] (float v) { activeEngine_->setBassMultiply (v); });
    pushIfChanged (lastMidMult_,   midMultParam_->load(),   [this] (float v) { activeEngine_->setMidMultiply (v); });
    pushIfChanged (lastSubMult_,   subMultParam_->load(),   [this] (float v) { activeEngine_->setSubMultiply (v); });
    pushIfChanged (lastHiMidMult_, hiMidMultParam_->load(), [this] (float v) { activeEngine_->setHiMidMultiply (v); });
    pushIfChanged (lastCrossoverSub_, crossoverSubParam_->load(), [this] (float v) { activeEngine_->setSubCrossoverFreq (v); });
    pushIfChanged (lastCrossoverAir_, crossoverAirParam_->load(), [this] (float v) { activeEngine_->setAirCrossoverFreq (v); });
    pushIfChanged (lastCrossover_, crossoverParam_->load(), [this] (float v) { activeEngine_->setCrossoverFreq (v); });
    pushIfChanged (lastHighCrossover_, highCrossoverParam_->load(), [this] (float v) { activeEngine_->setHighCrossoverFreq (v); });
    pushIfChanged (lastBassChoke_,     bassChokeParam_->load(),     [this] (float v) { activeEngine_->setBassChokeHz (v); });
    pushIfChanged (lastSaturation_,    saturationParam_->load(),    [this] (float v) { activeEngine_->setSaturation (v); });
    pushIfChanged (lastDiffusion_, diffusionParam_->load(), [this] (float v) { activeEngine_->setDiffusion (v); });
    pushIfChanged (lastModDepth_,  modDepthParam_->load(),  [this] (float v) { activeEngine_->setModDepth (v); });
    pushIfChanged (lastModRate_,   modRateParam_->load(),   [this] (float v) { activeEngine_->setModRate (v); });
    pushIfChanged (lastERSize_,    erSizeParam_->load(),    [this] (float v) { activeEngine_->setERSize (v); });
    pushIfChanged (lastERLevel_,   erLevelParam_->load(),   [this] (float v) { activeEngine_->setERLevel (v); });
    pushIfChanged (lastLoCut_,     loCutParam_->load(),     [this] (float v) { activeEngine_->setLoCut (v); });
    pushIfChanged (lastHiCut_,     hiCutParam_->load(),     [this] (float v) { activeEngine_->setHiCut (v); });
    pushIfChanged (lastHiCutShelfDb_, hiCutShelfDbParam_->load(),
                   [this] (float v) { activeEngine_->setHiCutShelfGainDb (v); });

    // Phase α PostTankEQ 4-band gain edge-detect. Each band's freq + Q
    // are cached at preset-swap time from kPostTankEQByName (or defaults
    // when no per-preset entry exists); the APVTS-driven gain triggers a
    // full setPostTankEQBand re-install with the cached freq+Q on change.
    pushIfChanged (lastPteqBand0Gain_, pteqBand0GainParam_->load(),
                   [this] (float v) { activeEngine_->setPostTankEQBand (0, pteqBandFreq_[0], pteqBandQ_[0], v); });
    pushIfChanged (lastPteqBand1Gain_, pteqBand1GainParam_->load(),
                   [this] (float v) { activeEngine_->setPostTankEQBand (1, pteqBandFreq_[1], pteqBandQ_[1], v); });
    pushIfChanged (lastPteqBand2Gain_, pteqBand2GainParam_->load(),
                   [this] (float v) { activeEngine_->setPostTankEQBand (2, pteqBandFreq_[2], pteqBandQ_[2], v); });
    pushIfChanged (lastPteqBand3Gain_, pteqBand3GainParam_->load(),
                   [this] (float v) { activeEngine_->setPostTankEQBand (3, pteqBandFreq_[3], pteqBandQ_[3], v); });

    // Phase γ PostTankBandTrim region gain edge-detect. Crossovers are
    // fixed (set on the engine in pushAllParametersTo) — only the 4 region
    // gain dB values are user-controllable / sweep-active.
    pushIfChanged (lastPostBandSub_,    postBandSubParam_->load(),
                   [this] (float v) { activeEngine_->setPostTankBandTrimGainDb (0, v); });
    pushIfChanged (lastPostBandLowMid_, postBandLowMidParam_->load(),
                   [this] (float v) { activeEngine_->setPostTankBandTrimGainDb (1, v); });
    pushIfChanged (lastPostBandMidHi_,  postBandMidHiParam_->load(),
                   [this] (float v) { activeEngine_->setPostTankBandTrimGainDb (2, v); });
    pushIfChanged (lastPostBandAir_,    postBandAirParam_->load(),
                   [this] (float v) { activeEngine_->setPostTankBandTrimGainDb (3, v); });

    // Phase δ per-band EDT attack/tau edge-detect. Setter recomputes both
    // (cheap — just stores new attackDb + tau on the AttackRamp), so the
    // pair is updated whenever either axis changes.
    auto pushEDT = [this] (int region, float& lastAtk, float& lastTau,
                            std::atomic<float>* atkParam, std::atomic<float>* tauParam) {
        const float atk = atkParam->load();
        const float tau = tauParam->load();
        if (atk != lastAtk || tau != lastTau) {
            activeEngine_->setPerBandEDTShape (region, atk, tau);
            lastAtk = atk;
            lastTau = tau;
        }
    };
    pushEDT (0, lastEDTSubAtk_,    lastEDTSubTau_,    edtSubAtkParam_,    edtSubTauParam_);
    pushEDT (1, lastEDTLowMidAtk_, lastEDTLowMidTau_, edtLowMidAtkParam_, edtLowMidTauParam_);
    pushEDT (2, lastEDTMidHiAtk_,  lastEDTMidHiTau_,  edtMidHiAtkParam_,  edtMidHiTauParam_);
    pushEDT (3, lastEDTAirAtk_,    lastEDTAirTau_,    edtAirAtkParam_,    edtAirTauParam_);

    // Phase ε: in-loop peaking — 3 axes shared, re-design biquad on ANY
    // change. Bypass guard inside FDNReverb skips the 5-mul per line when
    // gainDb is 0 dB, so leaving these at default costs zero in the loop.
    {
        const float pHz = inLoopPeakHzParam_->load();
        const float pQ  = inLoopPeakQParam_->load();
        const float pDb = inLoopPeakDbParam_->load();
        if (pHz != lastInLoopPeakHz_ || pQ != lastInLoopPeakQ_ || pDb != lastInLoopPeakDb_)
        {
            activeEngine_->setFDNInLoopPeaking (pHz, pQ, pDb);
            lastInLoopPeakHz_ = pHz;
            lastInLoopPeakQ_  = pQ;
            lastInLoopPeakDb_ = pDb;
        }
    }

    // Phase η: dual-time-constant bass shelf — 5 axes. Bypass guard inside
    // FDNReverb (dualBassShelfActive_) skips the per-line work when both
    // gains are 0 dB; default state preserves bit-identical bypass.
    {
        const float fastFc = bassShelfFastFcParam_->load();
        const float slowFc = bassShelfSlowFcParam_->load();
        const float fastDb = bassShelfFastDbParam_->load();
        const float slowDb = bassShelfSlowDbParam_->load();
        const float trans  = bassShelfTransitionParam_->load();
        if (fastFc != lastBassShelfFastFc_ || slowFc != lastBassShelfSlowFc_
            || fastDb != lastBassShelfFastDb_ || slowDb != lastBassShelfSlowDb_
            || trans  != lastBassShelfTransition_)
        {
            activeEngine_->setFDNDualBassShelf (fastFc, slowFc, fastDb, slowDb, trans);
            lastBassShelfFastFc_     = fastFc;
            lastBassShelfSlowFc_     = slowFc;
            lastBassShelfFastDb_     = fastDb;
            lastBassShelfSlowDb_     = slowDb;
            lastBassShelfTransition_ = trans;
        }
    }

    pushIfChanged (lastWidth_,     widthParam_->load(),     [this] (float v) { activeEngine_->setWidth (v); });
    pushIfChanged (lastGainTrim_,  gainTrimParam_->load(),  [this] (float v) { activeEngine_->setGainTrim (v); });
    pushIfChanged (lastMonoBelow_, monoBelowParam_->load(), [this] (float v) { activeEngine_->setMonoBelow (v); });

    // DPV EQ + brightness — only the DattorroPlateVintage engine listens;
    // others forward to no-op setters via DuskVerbEngine glue.
    pushIfChanged (lastDpvHfShelfDb_,    dpvHfShelfDbParam_->load(),      [this] (float v) { activeEngine_->setDpvHfShelfGainDb    (v); });
    pushIfChanged (lastDpvHfShelfHz_,    dpvHfShelfHzParam_->load(),      [this] (float v) { activeEngine_->setDpvHfShelfFreqHz    (v); });
    pushIfChanged (lastDpvStructHfDamp_, dpvStructHfDampHzParam_->load(), [this] (float v) { activeEngine_->setDpvStructHfDampHz   (v); });
    pushIfChanged (lastDpvBoxCutDb_,     dpvBoxCutDbParam_->load(),       [this] (float v) { activeEngine_->setDpvBoxCutGainDb     (v); });
    pushIfChanged (lastDpvBoxCutHz_,     dpvBoxCutHzParam_->load(),       [this] (float v) { activeEngine_->setDpvBoxCutFreqHz     (v); });
    pushIfChanged (lastDpvBassShelfDb_,  dpvBassShelfDbParam_->load(),    [this] (float v) { activeEngine_->setDpvBassShelfGainDb  (v); });
    pushIfChanged (lastDpvBassShelfHz_,  dpvBassShelfHzParam_->load(),    [this] (float v) { activeEngine_->setDpvBassShelfFreqHz  (v); });

    // Mix: bus_mode forces 100 % wet (override of user mix knob). The mix
    // smoother lives on the processor (see PluginProcessor.h) so the dry
    // signal is added AFTER any preset crossfade and stays correlated
    // across the swap.
    const bool busMode = busModeParam_->load() >= 0.5f;
    const float mixVal = busMode ? 1.0f : mixParam_->load();
    pushIfChanged (lastMix_, mixVal, [this] (float v) { mixSmoother_.setTarget (v); });

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

    // Save the dry input BEFORE either engine consumes it. The engines now
    // output WET-ONLY (see DuskVerbEngine::process tail) so the dry/wet mix
    // is applied here after the crossfade — keeps the dry signal correlated
    // across the swap and eliminates the +3 dB midpoint swell that
    // equal-power blending produced when each engine had its own dry path.
    std::memcpy (dryBufL_.data(), left,  static_cast<size_t> (numSamples) * sizeof (float));
    std::memcpy (dryBufR_.data(), right, static_cast<size_t> (numSamples) * sizeof (float));

    const bool fading = (presetFadeRemaining_ > 0 && previousEngine_ != nullptr);
    if (fading)
    {
        // Snapshot input for the previous engine. activeEngine_->process will
        // overwrite left/right with the new engine's wet, so the previous
        // engine has to read from a saved copy.
        std::memcpy (fadeBufL_.data(), left,  static_cast<size_t> (numSamples) * sizeof (float));
        std::memcpy (fadeBufR_.data(), right, static_cast<size_t> (numSamples) * sizeof (float));
    }

    activeEngine_->process (left, right, numSamples);

    if (fading)
    {
        // Run the saved input through the previous engine so its tail keeps
        // evolving (modulation, internal feedback) instead of being a static
        // snapshot. Then equal-power crossfade the two engines' WET outputs
        // — now mathematically correct since the dry was pulled out.
        previousEngine_->process (fadeBufL_.data(), fadeBufR_.data(), numSamples);

        const int samplesToCrossfade = std::min (numSamples, presetFadeRemaining_);
        // Denominator is (total - 1) so the LAST fade sample lands at t=1
        // exactly (gOld=0). Using `total` instead would leave a residual
        // ~cos(π/2 - π/(2·total)) at the boundary that drops to 0 in one
        // sample post-fade — audible as a tiny tick on long sustained tails.
        const int spanDen = std::max (1, presetFadeTotal_ - 1);
        const float invSpan = 1.0f / static_cast<float> (spanDen);
        const int idxBase = presetFadeTotal_ - presetFadeRemaining_;
        constexpr float kHalfPi = 1.5707963267948966f;

        for (int i = 0; i < samplesToCrossfade; ++i)
        {
            const float t    = std::min (1.0f, static_cast<float> (idxBase + i) * invSpan);
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
        // wet output (already in left/right) — leave them alone.
    }

    // ---- Dry/wet mix ----
    // Apply once, outside the engines, so the dry passthrough is unity-gain
    // and shared between the two engines during a crossfade.
    {
        constexpr float kHalfPi = 1.5707963267948966f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float mix = mixSmoother_.next();
            const float wetGain = std::sin (mix * kHalfPi);
            const float dryGain = std::cos (mix * kHalfPi);
            const size_t idx = static_cast<size_t> (i);
            left[i]  = dryBufL_[idx] * dryGain + left[i]  * wetGain;
            right[i] = dryBufR_[idx] * dryGain + right[i] * wetGain;
        }
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
static constexpr int kStateVersion = 3;
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
    // DattorroPlateVintage brightness + corrective EQ — APVTS already serializes
    // the 7 dpv* params via parameters.copyState() above. These duplicate custom
    // properties exist solely for downgrade safety: pre-2026-05-25 plugin binaries
    // ignore the new APVTS dpv params and read only these custom properties to
    // populate the (legacy) dpvBrightness_ struct. Source = current APVTS atomic
    // values; do not delete this path without also bumping kStateVersion and
    // dropping downgrade compatibility.
    state.setProperty ("dpvHfShelfGainDb",    dpvHfShelfDbParam_->load(),      nullptr);
    state.setProperty ("dpvHfShelfFreqHz",    dpvHfShelfHzParam_->load(),      nullptr);
    state.setProperty ("dpvStructHfDampHz",   dpvStructHfDampHzParam_->load(), nullptr);
    state.setProperty ("dpvBoxCutGainDb",     dpvBoxCutDbParam_->load(),       nullptr);
    state.setProperty ("dpvBoxCutFreqHz",     dpvBoxCutHzParam_->load(),       nullptr);
    state.setProperty ("dpvBassShelfGainDb",  dpvBassShelfDbParam_->load(),    nullptr);
    state.setProperty ("dpvBassShelfFreqHz",  dpvBassShelfHzParam_->load(),    nullptr);
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

    // v3 reordered the algorithm enum so the two Dattorro variants sit
    // adjacent in the dropdown. Old saved sessions store algorithm as
    // the pre-reorder index; remap to the new layout before replaceState
    // so the loaded plugin selects the engine the user originally saved.
    if (version < 3)
    {
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto child = tree.getChild (i);
            if (child.getProperty ("id").toString() == "algorithm")
            {
                const int oldIdx = static_cast<int> (child.getProperty ("value"));
                const int newIdx = migrateLegacyAlgorithmIndex (oldIdx);
                child.setProperty ("value", newIdx, nullptr);
                break;
            }
        }
    }

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
    // DPV brightness state is APVTS-driven as of 2026-05-25. Legacy sessions
    // saved before APVTS migration may carry these as custom properties only;
    // forward the legacy values into APVTS so the processBlock edge-detect
    // doesn't immediately overwrite them with the new APVTS defaults.
    auto migrateLegacyDpvProp = [this, &tree] (const char* propName, const char* paramId) {
        if (tree.hasProperty (propName))
        {
            const float v = static_cast<float> (tree.getProperty (propName));
            if (auto* p = parameters.getParameter (paramId))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        }
    };
    migrateLegacyDpvProp ("dpvHfShelfGainDb",   "dpv_hf_shelf_db");
    migrateLegacyDpvProp ("dpvHfShelfFreqHz",   "dpv_hf_shelf_hz");
    migrateLegacyDpvProp ("dpvStructHfDampHz",  "dpv_struct_hf_damp_hz");
    migrateLegacyDpvProp ("dpvBoxCutGainDb",    "dpv_box_cut_db");
    migrateLegacyDpvProp ("dpvBoxCutFreqHz",    "dpv_box_cut_hz");
    migrateLegacyDpvProp ("dpvBassShelfGainDb", "dpv_bass_shelf_db");
    migrateLegacyDpvProp ("dpvBassShelfFreqHz", "dpv_bass_shelf_hz");
    // State load happens before audio starts (or via host-driven message-thread
    // call). Push SixAP brightness to both engines so the idle one is in sync
    // for the next preset swap. DPV: APVTS is single source of truth — the
    // next processBlock edge-detects the migrated values and pushes to the
    // active engine; forcePushAllParametersTo handles the idle engine at swap
    // time. No message-thread biquad coefficient write here (avoids the race
    // against audio-thread process() reads).
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
    // Bug-fix 2026-05-30: see comment in pushIfChanged for damping above.
    target->setAirTrebleMultiply (dampingParam_->load());
    target->setBassMultiply      (bassMultParam_->load());
    target->setMidMultiply       (midMultParam_->load());
    target->setSubMultiply       (subMultParam_->load());
    target->setHiMidMultiply     (hiMidMultParam_->load());
    target->setSubCrossoverFreq  (crossoverSubParam_->load());
    target->setAirCrossoverFreq  (crossoverAirParam_->load());
    target->setCrossoverFreq     (crossoverParam_->load());
    target->setHighCrossoverFreq (highCrossoverParam_->load());
    target->setBassChokeHz (bassChokeParam_->load());
    target->setSaturation        (saturationParam_->load());
    target->setDiffusion         (diffusionParam_->load());
    target->setModDepth          (modDepthParam_->load());
    target->setModRate           (modRateParam_->load());
    target->setERSize            (erSizeParam_->load());
    target->setERLevel           (erLevelParam_->load());
    target->setLoCut             (loCutParam_->load());
    target->setHiCut             (hiCutParam_->load());
    target->setHiCutShelfGainDb  (hiCutShelfDbParam_->load());

    // Phase γ: PostTankBandTrim crossovers (fixed defaults, not yet per-
    // preset overridable) + 4 region gain dB values.
    target->setPostTankBandTrimCrossovers (200.0f, 800.0f, 3000.0f);
    target->setPostTankBandTrimGainDb (0, postBandSubParam_->load());
    target->setPostTankBandTrimGainDb (1, postBandLowMidParam_->load());
    target->setPostTankBandTrimGainDb (2, postBandMidHiParam_->load());
    target->setPostTankBandTrimGainDb (3, postBandAirParam_->load());

    // Phase δ per-band EDT shaping. Crossovers fixed to match PostTankBandTrim.
    target->setPerBandEDTCrossovers (200.0f, 800.0f, 3000.0f);
    target->setPerBandEDTShape (0, edtSubAtkParam_->load(),    edtSubTauParam_->load());
    target->setPerBandEDTShape (1, edtLowMidAtkParam_->load(), edtLowMidTauParam_->load());
    target->setPerBandEDTShape (2, edtMidHiAtkParam_->load(),  edtMidHiTauParam_->load());
    target->setPerBandEDTShape (3, edtAirAtkParam_->load(),    edtAirTauParam_->load());

    // Phase ε in-loop peaking.
    target->setFDNInLoopPeaking (inLoopPeakHzParam_->load(),
                                  inLoopPeakQParam_->load(),
                                  inLoopPeakDbParam_->load());

    // Phase η per-line dual-time-constant bass shelf.
    target->setFDNDualBassShelf (bassShelfFastFcParam_->load(),
                                  bassShelfSlowFcParam_->load(),
                                  bassShelfFastDbParam_->load(),
                                  bassShelfSlowDbParam_->load(),
                                  bassShelfTransitionParam_->load());

    target->setWidth             (widthParam_->load());
    target->setGainTrim          (gainTrimParam_->load());
    target->setMonoBelow         (monoBelowParam_->load());

    // Mix lives on the processor — not pushed to the engine. The
    // processor's mixSmoother target is updated in performPresetSwap so
    // it picks up the new preset's value without re-pushing here.

    target->setFreeze              (freezeParam_->load() >= 0.5f);
    target->setNonLinearGateEnabled(gateEnabledParam_->load() >= 0.5f);

    pushSixAPBrightnessTo (*target);
    // DPV EQ + brightness: read from APVTS atomics (single source of truth
    // since 2026-05-25 migration). Legacy custom properties bridge to APVTS
    // via migrateLegacyDpvProp() in setStateInformation.
    target->setDpvHfShelfGainDb    (dpvHfShelfDbParam_->load());
    target->setDpvHfShelfFreqHz    (dpvHfShelfHzParam_->load());
    target->setDpvStructHfDampHz   (dpvStructHfDampHzParam_->load());
    target->setDpvBoxCutGainDb     (dpvBoxCutDbParam_->load());
    target->setDpvBoxCutFreqHz     (dpvBoxCutHzParam_->load());
    target->setDpvBassShelfGainDb  (dpvBassShelfDbParam_->load());
    target->setDpvBassShelfFreqHz  (dpvBassShelfHzParam_->load());
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
    lastSubMult_       = subMultParam_->load();
    lastHiMidMult_     = hiMidMultParam_->load();
    lastCrossoverSub_  = crossoverSubParam_->load();
    lastCrossoverAir_  = crossoverAirParam_->load();
    lastCrossover_     = crossoverParam_->load();
    lastHighCrossover_ = highCrossoverParam_->load();
    lastBassChoke_     = bassChokeParam_->load();
    lastSaturation_    = saturationParam_->load();
    lastDiffusion_     = diffusionParam_->load();
    lastModDepth_      = modDepthParam_->load();
    lastModRate_       = modRateParam_->load();
    lastERSize_        = erSizeParam_->load();
    lastERLevel_       = erLevelParam_->load();
    lastLoCut_         = loCutParam_->load();
    lastHiCut_         = hiCutParam_->load();
    lastHiCutShelfDb_  = hiCutShelfDbParam_->load();
    lastWidth_         = widthParam_->load();
    lastGainTrim_      = gainTrimParam_->load();
    lastMonoBelow_     = monoBelowParam_->load();
    lastDpvHfShelfDb_    = dpvHfShelfDbParam_->load();
    lastDpvHfShelfHz_    = dpvHfShelfHzParam_->load();
    lastDpvStructHfDamp_ = dpvStructHfDampHzParam_->load();
    lastDpvBoxCutDb_     = dpvBoxCutDbParam_->load();
    lastDpvBoxCutHz_     = dpvBoxCutHzParam_->load();
    lastDpvBassShelfDb_  = dpvBassShelfDbParam_->load();
    lastDpvBassShelfHz_  = dpvBassShelfHzParam_->load();

    lastPostBandSub_     = postBandSubParam_->load();
    lastPostBandLowMid_  = postBandLowMidParam_->load();
    lastPostBandMidHi_   = postBandMidHiParam_->load();
    lastPostBandAir_     = postBandAirParam_->load();

    lastEDTSubAtk_    = edtSubAtkParam_->load();
    lastEDTSubTau_    = edtSubTauParam_->load();
    lastEDTLowMidAtk_ = edtLowMidAtkParam_->load();
    lastEDTLowMidTau_ = edtLowMidTauParam_->load();
    lastEDTMidHiAtk_  = edtMidHiAtkParam_->load();
    lastEDTMidHiTau_  = edtMidHiTauParam_->load();
    lastEDTAirAtk_    = edtAirAtkParam_->load();
    lastEDTAirTau_    = edtAirTauParam_->load();

    lastInLoopPeakHz_ = inLoopPeakHzParam_->load();
    lastInLoopPeakQ_  = inLoopPeakQParam_->load();
    lastInLoopPeakDb_ = inLoopPeakDbParam_->load();

    lastBassShelfFastFc_     = bassShelfFastFcParam_->load();
    lastBassShelfSlowFc_     = bassShelfSlowFcParam_->load();
    lastBassShelfFastDb_     = bassShelfFastDbParam_->load();
    lastBassShelfSlowDb_     = bassShelfSlowDbParam_->load();
    lastBassShelfTransition_ = bassShelfTransitionParam_->load();

    const bool busMode = busModeParam_->load() >= 0.5f;
    lastMix_ = busMode ? 1.0f : mixParam_->load();

    lastFreeze_          = freezeParam_->load()      >= 0.5f;
    haveLastFreeze_      = true;
    lastGateEnabled_     = gateEnabledParam_->load() >= 0.5f;
    haveLastGateEnabled_ = true;
}

// ─── File-scope shared PostTankEQ lookup ──────────────────────────────────
// Hoisted above performPresetSwap so the cache-populate path can call it.
namespace {
    struct PostTankEQConfig { float freq[4]; float q[4]; float gain[4]; };
    static constexpr float kPteqDefaultFreq[4] = {  80.0f,  500.0f, 3000.0f, 10000.0f };
    static constexpr float kPteqDefaultQ   [4] = {   1.5f,    1.0f,    1.5f,     0.8f };
    static constexpr float kPteqZeroGain   [4] = {   0.0f,    0.0f,    0.0f,     0.0f };
    static const std::unordered_map<std::string_view, PostTankEQConfig>&
    pteqByName()
    {
        static const std::unordered_map<std::string_view, PostTankEQConfig> m = {
            // Vocal Hall (Steps 3 + 5 + 13 + 14 on v15 baseline, 2026-05-30):
            //   Band 0 —  70 Hz Q=1.2 -2.5 dB: Step 13 post-tank sub-bass
            //                                   scoop. Vents muddy 40-100 Hz
            //                                   acoustic bloom (boom sub).
            //   Band 1 — 1000 Hz Q=2.5 -3.0 dB: Step 14 surgical notch on
            //                                   the 1 kHz modal resonance
            //                                   that left sine1k stuck at
            //                                   +4.97 dB hot for 5 steps.
            //   Band 2 — 2560 Hz Q=2.5 -3.0 dB: Step 5 narrow modal notch.
            //   Band 3 — 8000 Hz Q=1.2 -2.5 dB: Step 15 deepens the
            //                                   high-mid trim from -1.5 to
            //                                   -2.5 dB. Targets remaining
            //                                   subjective brightness sheen
            //                                   + 12.9 kHz resonant tip
            //                                   without disturbing 1 kHz.
            { "Vocal Hall", {
                {  70.0f, 1000.0f, 2560.0f,  8000.0f },
                {   1.2f,    2.5f,    2.5f,     1.2f },
                {  -2.5f,   -3.0f,   -3.0f,    -2.5f },
            } },
            // Bright Hall (BH-6 on VintageTank algo=8, 2026-05-30):
            //   Band 0 —  60 Hz Q=1.0 -3.0 dB: BH-2 tight sub-bass scoop.
            //   Band 1 — 1000 Hz Q=6.0 -5.5 dB: BH-4 hyper-narrow notch.
            //   Band 2 — 3500 Hz Q=2.0 -2.0 dB: BH-6 deepens -1.5 → -2.0 dB
            //                                   to push spec_L1 mean
            //                                   off the 2.00 gate boundary.
            //   Band 3 — 10000 Hz Q=0.8 0 dB:   unity, reserved.
            { "Bright Hall", {
                {  60.0f, 1000.0f, 3500.0f, 10000.0f },
                {   1.0f,    6.0f,    2.0f,     0.8f },
                {  -3.0f,   -5.5f,   -2.0f,     0.0f },
            } },
            // Small Drum Room (SDR-NL3 on NonLinear algo=6, 2026-05-30):
            //   Band 0/1/3 — defaults / reserved.
            //   Band 2 — 1500 Hz Q=0.5 -3.5 dB: wide gentle mid scoop
            //              to counter NonLinear's mid-bulge. Paired with
            //              gainTrim +6.20 in the preset row: wings get
            //              full +6.20 dB lift, mids get ~+2.7 dB net.
            { "Small Drum Room", {
                {  101.0f, 1000.0f, 1500.0f, 5000.0f },
                {  3.00f,  4.00f,  1.00f,  1.50f },
                { -5.00f, +3.50f, -3.00f, -4.50f },
            } },
            // Blade Runner 224 (BR-9 on FDN algo=4, 2026-05-30):
            //   Band 0 —  600 Hz Q=2.0 -2.0 dB: low-mid steady-state scoop.
            //   Band 1 — 3000 Hz Q=1.5 -3.5 dB: lower bloom suppressor.
            //   Band 2 — 6000 Hz Q=1.0 -5.0 dB: bloom 4-8k closed BR-8.
            //   Band 3 —10000 Hz Q=1.2 -6.0 dB: BR-9 deepens -5.0 → -6.0
            //                                    to clamp bloom 8-12k (was
            //                                    0.53 dB over gate).
            { "Blade Runner 224", {
                {  600.0f, 3000.0f, 6000.0f, 10000.0f },
                {   2.00f,   1.50f,   1.00f,    1.20f },
                {  -2.00f,  -3.50f,  -5.00f,   -6.00f },
            } },
        };
        return m;
    }
    inline void resolvePteqFreqQ (const char* name, float fOut[4], float qOut[4])
    {
        auto& m = pteqByName();
        auto it = m.find (std::string_view (name));
        const float* fSrc = (it != m.end()) ? it->second.freq : kPteqDefaultFreq;
        const float* qSrc = (it != m.end()) ? it->second.q    : kPteqDefaultQ;
        for (int b = 0; b < 4; ++b) { fOut[b] = fSrc[b]; qOut[b] = qSrc[b]; }
    }
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

    // Engine config (modulation topology, PostTankEQ bands, sixAP-only
    // tunables that live outside APVTS). Acquires the preset pointer that
    // applyFactoryPreset() cached on the message thread.
    if (auto* preset = lastAppliedPreset_.load (std::memory_order_acquire))
    {
        preset->applyEngineConfig (*newActive);
        // Cache freq + Q on the processor so subsequent APVTS gain edge-
        // detects in processBlock can re-issue setPostTankEQBand without
        // re-consulting the kPostTankEQByName map. Single source of truth.
        resolvePteqFreqQ (preset->name, pteqBandFreq_, pteqBandQ_);
    }


    newActive->snapSmoothersToTargets();

    // Inherit pre-tank input history (pre-delay buffer + ER signal state)
    // from the currently-active engine. Without this the new engine's ER
    // taps fire from silence over their 8-80 ms delay range, producing
    // audible discrete onsets that the 50 ms equal-power crossfade can't
    // fully mask. Tank state stays cleared — pre-filling it across a
    // potentially different algorithm topology would feed wrong-coefficient
    // history into the new tank's feedback loop.
    newActive->copyInputHistoryFrom (*activeEngine_);

    // Retarget the processor's mix smoother to the new preset's value.
    // forcePushAllParametersTo doesn't touch mix (it lives on the processor),
    // and syncParameterCacheToCurrent below will set lastMix_ so the regular
    // pushIfChanged loop won't re-push it either. Without this update the
    // smoother would keep gliding toward the OLD preset's mix target.
    const bool busMode = busModeParam_->load() >= 0.5f;
    mixSmoother_.setTarget (busMode ? 1.0f : mixParam_->load());

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

// VST3 program API — wires getFactoryPresets() to host-visible programs.
// Hosts (and the render harness) can route `setCurrentProgram(idx)` through
// applyFactoryPreset() so the full engine config installs alongside APVTS.
int DuskVerbProcessor::getNumPrograms()
{
    return static_cast<int> (getFactoryPresets().size());
}

void DuskVerbProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (index < 0 || index >= static_cast<int> (presets.size()))
        return;
    currentProgram_ = index;
    applyFactoryPreset (presets[static_cast<size_t> (index)]);
}

const juce::String DuskVerbProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    if (index < 0 || index >= static_cast<int> (presets.size()))
        return {};
    return juce::String (presets[static_cast<size_t> (index)].name);
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

    // DPV brightness + corrective EQ: preset.applyTo above wrote the 7 dpv*
    // values into APVTS via setIfExists. Next processBlock edge-detect picks
    // them up; performPresetSwap's forcePushAllParametersTo reads APVTS for
    // the idle engine. No struct cache write needed (single source of truth).

    // Release ordering pairs with the acquire load in processBlock to publish
    // the brightness writes above.
    // Cache the preset pointer for the audio thread. performPresetSwap()
    // will pick it up and run preset.applyEngineConfig() on the new engine —
    // forcePushAllParametersTo only handles APVTS-routed params; engine-config
    // (mod topology, sixAP brightness fwd, PostTankEQ bands) lives here.
    lastAppliedPreset_.store (&preset, std::memory_order_release);

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
    engine.setDpvHfShelfGainDb     (dpvHfShelfGainDb);
    engine.setDpvHfShelfFreqHz     (dpvHfShelfFreqHz);
    engine.setDpvStructHfDampHz    (dpvStructHfDampHz);
    engine.setDpvBoxCutGainDb      (dpvBoxCutGainDb);
    engine.setDpvBoxCutFreqHz      (dpvBoxCutFreqHz);
    engine.setDpvBassShelfGainDb   (dpvBassShelfGainDb);
    engine.setDpvBassShelfFreqHz   (dpvBassShelfFreqHz);

    // ─── Post-tank parametric EQ — per-preset 4-band overrides ────────────
    // Per-preset entries live in pteqByName() (file-scope, shared with the
    // processor's performPresetSwap freq/Q cache path). Presets not in the
    // map use the defaults (kPteqDefaultFreq/Q) + zero gain → unity
    // coefficients → bit-identical bypass.
    //
    // Vocal Hall keeps the v30 polish values (80 Hz -1.0 dB Q=1.5; 254 Hz
    // +2.0 dB Q=1.0; 5120 Hz -3.0 dB Q=1.5; 12.9 k +6.0 dB Q=0.8). Other
    // presets get defaults (80 / 500 / 3000 / 10000 Hz, Q 1.5/1.0/1.5/0.8,
    // gain 0 dB) so the optimizer's APVTS gain sweeps land on meaningful
    // frequencies even without a per-preset map entry.
    {
        auto& m = pteqByName();
        auto it = m.find (std::string_view (name));
        const float* fSrc = (it != m.end()) ? it->second.freq : kPteqDefaultFreq;
        const float* qSrc = (it != m.end()) ? it->second.q    : kPteqDefaultQ;
        const float* gSrc = (it != m.end()) ? it->second.gain : kPteqZeroGain;
        for (int b = 0; b < 4; ++b)
            engine.setPostTankEQBand (b, fSrc[b], qSrc[b], gSrc[b]);
    }

    // hi_cut_shelf_db now flows through APVTS (set in FactoryPreset::applyTo),
    // so no explicit engine setter call here.

    // Phase 2 modulation topology — per-preset opt-in via name lookup so we
    // don't have to add a new field to FactoryPreset (would break the
    // aggregate-initializability of the brace-init preset list).
    // Names that aren't listed get RandomWalk (legacy bit-identical).
    static const std::unordered_map<std::string_view, DspUtils::ModulationTopology> kTopologyByName = {
        // Phase 2 opt-ins (2026-05-28). Final list after listening +
        // measurement validation:
        //
        // Cathedral — CONFIRMED. CoherentLoop closed 6 gates (14→8). FDN
        //   feedback's 16 lines on phase-paired sine LFO produce coherent
        //   macro pumping that matches the cathedral character. osc P2P
        //   went from -2.66 → +0.74 (dead-on); all 8 ss-band energies pass.
        //
        // Vocal Hall  — REVERTED. Coherent topology reinforced the 320 Hz
        //   modal resonance that random-walk averaging used to mask.
        //
        // 79 Vocal Chamber — REVERTED. QuadTank's quadrature phase mapping
        //   re-energized the 12.9 kHz parasitic spur (76 dB spec_L1 max).
        //   QuadTank engine prefers independent random-walk LFOs to
        //   suppress its cross-coupling artifacts.
        //
        // Net Phase 2 verdict: FDN halls with lush motion character (long
        // tail, light modal content) are the sweet spot. QuadTank rooms +
        // FDN presets with sensitive modal resonances stay on RandomWalk.
        // 2026-05-28 retry after render.cpp harness shelf bug fix:
        //   79 Vocal Chamber — marginal -1 gate vs RandomWalk (22→21).
        //     osc P2P closes (-2.87 dB) but QuadTank's 12.9 kHz HF spur
        //     persists even at -23.5 dB shelf — architectural artifact.
        //   Ambience — regressed +5 vs RandomWalk (21→26). Reverted.
        { "79 Vocal Chamber",     DspUtils::ModulationTopology::CoherentLoop },
        // Phase α (2026-05-29): promote Bright Hall, Cathedral Large Hall,
        // and Drum Plate to ModulatedDamping. The static-line + slow-drift
        // damping topology eliminates the per-line LFO harmonic-stacking
        // signature (DV mod at wrong per-band rates vs VVV) AND clears the
        // path for per-line frequency-indexed decay scaling (Phase α
        // setPerLineDecayTilt) to sculpt RT60 per band without Doppler.
        // Cathedral previously used CoherentLoop (+6 gates closed in Phase 2)
        // — superseded here because per-band RT60 shape was unmeasured at
        // that time; the un-blinded gates show Cathedral needs the bass-tail
        // extension only per-line decay scaling can deliver.
        { "Bright Hall",          DspUtils::ModulationTopology::ModulatedDamping },
        { "Cathedral Large Hall", DspUtils::ModulationTopology::ModulatedDamping },
        { "Drum Plate",           DspUtils::ModulationTopology::ModulatedDamping },
        // Vocal Hall — REVERTED to RandomWalk default on 2026-05-29 with
        // the v15 preset-row rollback. ModulatedDamping was added as a
        // Phase 3 anti-Doppler fix layered on top of v18-manual params;
        // with VH now back on v15, fall through to the default and let
        // the per-line random LFOs handle modulation as they did when
        // the FDN path was last known stable.
    };
    DspUtils::ModulationTopology topo = DspUtils::ModulationTopology::RandomWalk;
    auto it = kTopologyByName.find (std::string_view (name));
    if (it != kTopologyByName.end())
        topo = it->second;
    engine.setModulationTopology (topo);

    // ─── Phase α: per-line frequency-indexed decay-tilt overrides ─────────
    // Long lines (low-band-dominant) get longLineScale; short lines (high-
    // band-dominant) get shortLineScale. Defaults 1.0/1.0 = backward
    // identical for presets not in this map. The aggressive 0.5/1.7 spread
    // for Halls + Cathedral is calibrated against VVV's measured 5.3× T60
    // ratio (63 Hz vs 16 kHz on Bright Hall) — gives the FDN feedback path
    // a structural lever to deliver per-band RT60 shape that 3-band damping
    // multiplexing alone cannot reach (proven by the BH "sacrificial sweep"
    // verdict).
    struct DecayTiltConfig { float shortLine, longLine; };
    static const std::unordered_map<std::string_view, DecayTiltConfig> kDecayTiltByName = {
        // Phase α audit (2026-05-29): per-line tilt was designed to shape
        // RT60 per band but in practice DUMPS level into the long-line
        // band and STARVES level on the short-line band.
        //   VH (0.55/1.65): -1.8 dB cold at 4-16 kHz (loss of clarity).
        //   BH (0.35/2.20): +4.6 dB hot at 63-125 Hz (boom) + -2 to -6 dB
        //                    cold across 1-16 kHz (loss of clarity).
        // Listening tests on both presets confirmed the level damage
        // dominates audibly over the modest RT60 shape benefit (2/9 bands
        // closed). VH + BH reverted to flat (no entry → default 1.0/1.0).
        // Cathedral + Drum Plate untested with tilt — likely same issue;
        // disabled pending audition. Phase α infrastructure stays in
        // place for future presets if a different topology (e.g. shelving
        // damping with separate gain-vs-decay axes) reveals a clean win.
        // VH disabled — listening test showed HF cold by 1.8 dB. Factory row
        // was tuned at v15 WITHOUT tilt — disable returns to factory shape.
        // BH kept at MILD tilt 0.55/1.65 — its factory row IS v33 sweep best
        // which was tuned WITH tilt active; full disable broke that balance
        // (mids/highs went -3 to -7 dB cold). Milder tilt preserves Phase α
        // bass-extension benefit at less aggressive level dump.
        // { "Vocal Hall",           { 0.55f, 1.65f } },  // disabled
        // { "Bright Hall",          { 0.55f, 1.65f } },  // disabled — factory row reverted to v1, tilt no longer needed
        // { "Cathedral Large Hall", { 0.45f, 1.85f } },  // disabled pending audition
        // { "Drum Plate",           { 0.70f, 1.30f } },  // disabled pending audition
    };
    float shortS = 1.0f, longS = 1.0f;
    auto tiltIt = kDecayTiltByName.find (std::string_view (name));
    if (tiltIt != kDecayTiltByName.end())
    {
        shortS = tiltIt->second.shortLine;
        longS  = tiltIt->second.longLine;
    }
    engine.setPerLineDecayTilt (shortS, longS);

    // ─── Phase β: per-preset FDN base delays ──────────────────────────────
    // Replaces the engine's log-spaced-prime kDefaultDelays[16] for presets
    // whose VVV anchor exhibits a distinct per-band Hilbert-FFT modal-beat
    // pattern that the default delays can't reproduce. Each 16-int set must
    // fit inside FDNReverb::kMaxBaseDelay (6700 samples).
    //
    // Presets not in this map keep the engine default (log-spaced primes
    // 1151..6451 samples) — bit-identical to pre-Phase β behavior.
    struct BaseDelaySet { int delays[16]; };
    static const std::unordered_map<std::string_view, BaseDelaySet> kBaseDelaysByName = {
        // Vocal Hall (Step 2 on v15 baseline, 2026-05-30): restored Phase β
        // sweep result. Without this override the engine's default log-
        // spaced-prime delays push the dominant pairwise (1/T_i - 1/T_j)
        // beats to 4-8 Hz, while the VVV anchor sits at 1.5-2.5 Hz. The
        // sweep-tuned array below aligns the Hilbert envelope mod peaks
        // back to anchor without touching the RandomWalk topology.
        // Sweep ID: baseDelay_sweep.py trial 299, 500 trials TPE.
        // Per-band Hilbert peaks at the time of fit:
        //   bass 1.92 Hz (target 1.92, 0.1% err)
        //   lowmid 1.37 Hz (target 1.56, 12% err)
        //   high 2.56 Hz (target 2.47, 3.8% err)
        { "Vocal Hall", { { 1005, 1007, 1050, 1256, 1440, 1921, 2357, 2694,
                            3069, 3253, 3588, 4086, 4178, 5043, 6014, 6414 } } },
        // Bright Hall (Phase β sweep, 2026-05-29, 500 trials TPE):
        // trial 27 — per-band Hilbert peaks bass 3.20Hz (target 3.02,
        // 6.1% err), lowmid 1.74Hz (target 2.01, 13.5% err), mid 4.30Hz
        // (target 4.76, 9.6% err), high 6.59Hz (target 7.23, 8.8% err).
        // All 4 mod gates pass ±30%.
        { "Bright Hall", { { 707, 814, 1594, 1613, 1791, 2767, 3212, 3692,
                              4030, 4268, 4579, 4866, 5319, 5659, 5889, 5912 } } },
    };

    // ─── Phase γ: per-preset post-tank band-trim region gains ────────────
    // 4 floats per preset (Sub / LowMid / MidHi / Air) in dB. Defaults 0 →
    // unity-coefficient shelves + 1.0 makeup gain → bit-identical bypass.
    // Presets not in the map skip the call entirely (APVTS-default 0 dB
    // already pushed via forcePushAllParametersTo at swap time).
    struct PostBandTrimConfig { float sub, lowMid, midHi, air; };
    static const std::unordered_map<std::string_view, PostBandTrimConfig> kPostBandTrimByName = {
        // Vocal Hall post-band trim override removed on 2026-05-29 v15
        // revert. The +1/+2/-1 dB lift was a v18-manual presence rescue;
        // v15 baseline does not need it.
        // Bright Hall (VintageTank V2.0): PostBandTrim disabled — the round-4
        // sweep ran with the FDN-era preset row, where PostBandTrim cuts
        // helped. Under the new VintageTank row (baked round-3 axes), the
        // same cuts dropped 13 additional gates net. Leaving PostBandTrim
        // at unity (no map entry) gives the cleanest current result.
    };
    auto pbtIt = kPostBandTrimByName.find (std::string_view (name));
    if (pbtIt != kPostBandTrimByName.end())
    {
        engine.setPostTankBandTrimGainDb (0, pbtIt->second.sub);
        engine.setPostTankBandTrimGainDb (1, pbtIt->second.lowMid);
        engine.setPostTankBandTrimGainDb (2, pbtIt->second.midHi);
        engine.setPostTankBandTrimGainDb (3, pbtIt->second.air);
    }
    else
    {
        // Defensively zero — applyEngineConfig may run after APVTS has set
        // non-zero post-band trims from a prior preset that opted in. Without
        // this reset, switching from VH (opt-in) to e.g. Vocal Plate (no
        // opt-in) would inherit VH's trim values.
        for (int r = 0; r < 4; ++r)
            engine.setPostTankBandTrimGainDb (r, 0.0f);
    }

    // Sweep override: DUSKVERB_FDN_DELAYS env var wins over the map. CSV of
    // 16 positive ints in samples. Set by the sweep harness per trial; absent
    // in normal sessions. Bypasses state-blob roundtrip entirely (JUCE's
    // copyXmlToBinary drops root-XML attributes added after createXml — so
    // setAttribute injection from a host doesn't survive the deserialization
    // path inside setStateInformation).
    const char* envCsv = std::getenv ("DUSKVERB_FDN_DELAYS");
    if (envCsv != nullptr && envCsv[0] != '\0')
    {
        juce::StringArray tokens;
        tokens.addTokens (juce::String (envCsv), ",", "");
        if (tokens.size() == 16)
        {
            int parsed[16];
            bool ok = true;
            for (int i = 0; i < 16; ++i)
            {
                const int v = tokens[i].trim().getIntValue();
                if (v <= 0) { ok = false; break; }
                parsed[i] = v;
            }
            if (ok)
            {
                engine.setFDNBaseDelays (parsed);
                return;
            }
        }
    }
    auto bdIt = kBaseDelaysByName.find (std::string_view (name));
    if (bdIt != kBaseDelaysByName.end())
        engine.setFDNBaseDelays (bdIt->second.delays);

    // ─── Phase ζ + η: per-preset in-loop peaking + dual-time-constant bass shelf ──
    // Both default to bypass (0 dB) when no per-preset entry exists. VH/BH
    // opt in to sweep-best values via combined_sweep.py.
    struct ZetaEtaConfig
    {
        float ilpHz;     // ζ: in-loop peak frequency (Hz)
        float ilpQ;      // ζ: in-loop peak Q
        float ilpDb;     // ζ: in-loop peak gain (dB)
        float bsFastFc;  // η: dual bass shelf fast cutoff (Hz)
        float bsSlowFc;  // η: dual bass shelf slow cutoff (Hz)
        float bsFastDb;  // η: dual bass shelf fast gain (dB)
        float bsSlowDb;  // η: dual bass shelf slow gain (dB)
        float bsTransMs; // η: dual bass shelf transition time (ms)
    };
    static const std::unordered_map<std::string_view, ZetaEtaConfig> kZetaEtaByName = {
        // VH (and any other ζ+η-opt-in preset) now applies values via
        // FactoryPreset::applyTo writing APVTS. Map kept as the documented
        // canonical defaults but no longer applied — APVTS path handles
        // all the smoother ramping and inter-block coefficient sync.
    };
    // Note: kZetaEtaByName map intentionally NOT applied here. ζ+η per-
    // preset values now flow through APVTS via FactoryPreset::applyTo +
    // forcePushAllParametersTo so they share the same smoother/edge-detect
    // path as the sweep --param flow. Setting engine directly here would
    // overwrite the APVTS-driven values and reintroduce the inter-block
    // instability that was observed on VH at +10.88 dB peak.
    (void) name;  // silence unused warning if kZetaEtaByName never grows
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskVerbProcessor();
}

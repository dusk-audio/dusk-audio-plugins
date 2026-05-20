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

    // Bass / Mid / Treble Multiply ranges widened 2026-05-18 (originally
    // 2.5 / 2.5 / 1.5 max) so PlateEngine's Lex-Vintage-Plate tunes can
    // push bandGain → 1.0 at edge bands. Existing FactoryPreset values
    // stay well within the new range; the wider headroom only activates
    // for presets that opt in. Engine clamps (e.g. PlateEngine's internal
    // kMaxBandGain 0.98) keep the final gain bounded.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass_mult", 1 }, "Bass Multiply",
        juce::NormalisableRange<float> (0.3f, 4.0f), fp0.bassMult));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mid_mult", 1 }, "Mid Multiply",
        juce::NormalisableRange<float> (0.3f, 4.0f), fp0.midMult));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "damping", 1 }, "Treble Multiply",
        juce::NormalisableRange<float> (0.1f, 2.5f), fp0.damping));

    // Low Crossover lower bound widened 2026-05-18 from 200 → 80 Hz so
    // PlateEngine's LR4 split can place fLow well below 125 Hz when
    // a preset needs the bass band to extend to LF roots without LP
    // attenuation. Engine-side LR4 clamps to >= 20 Hz internally.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "crossover", 1 }, "Low Crossover",
        juce::NormalisableRange<float> (80.0f, 4000.0f, 0.0f, 0.5f), fp0.crossover));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "high_crossover", 1 }, "High Crossover",
        juce::NormalisableRange<float> (1000.0f, 12000.0f, 0.0f, 0.5f), fp0.highCrossover));

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

    // FirstReflections specular taps. Default gains = -60 dB (silent) so any
    // preset that doesn't opt in is unchanged. Hall-style presets (Concert
    // Hall, Cathedral) set L=3-5 ms / 0 dB, R=8-12 ms / -3 dB to match the
    // Lex PCM Native L_Rfl_Dly + R_Rfl_Dly specular onset.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "first_refl_l_dly", 1 }, "First Refl L Dly",
        juce::NormalisableRange<float> (1.0f, 50.0f, 0.0f, 0.5f), fp0.firstReflLDlyMs));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "first_refl_r_dly", 1 }, "First Refl R Dly",
        juce::NormalisableRange<float> (1.0f, 50.0f, 0.0f, 0.5f), fp0.firstReflRDlyMs));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "first_refl_l_gain", 1 }, "First Refl L Gain",
        juce::NormalisableRange<float> (-60.0f, 6.0f), fp0.firstReflLGainDb));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "first_refl_r_gain", 1 }, "First Refl R Gain",
        juce::NormalisableRange<float> (-60.0f, 6.0f), fp0.firstReflRGainDb));
    // HF lowpass on FirstReflections taps. Default 20 kHz = bypass.
    // Halls opt in to ~3-6 kHz to model air absorption (Lex Concert Hall
    // uses Rvb Out Freq=7 kHz; the specular taps need similar HF rolloff
    // or treble_ratio overshoots).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "first_refl_hf_cut", 1 }, "First Refl HF Cut",
        juce::NormalisableRange<float> (100.0f, 20000.0f, 0.0f, 0.5f), fp0.firstReflHFCutHz));

    // ───── HallReverb advanced params (algo 10 only) ─────────────────────
    // These map 1-to-1 onto HallReverb's per-band internal controls so the
    // tuner (CMA-ES) can search engine internals that the standard
    // setBassMultiply / setMidMultiply / setSaturation / etc. don't cover.
    // Other engines ignore these param values (DuskVerbEngine forwards
    // them only to hall_). The editor hides these knobs when the active
    // algorithm isn't "Hall (Lex)".
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_damping", 1 }, "Hall Bass Damping",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.40f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_damping", 1 }, "Hall Mid Damping",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.25f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_damping", 1 }, "Hall Treble Damping",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.05f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_gain", 1 }, "Hall Bass Gain",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_gain", 1 }, "Hall Mid Gain",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_gain", 1 }, "Hall Treble Gain",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_inline_diffusion", 1 }, "Hall Inline Diffusion",
        juce::NormalisableRange<float> (0.0f, 0.85f), 0.30f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_stereo_width", 1 }, "Hall Stereo Width",
        juce::NormalisableRange<float> (-0.4f, 0.4f), 0.0f));

    // Multi-tap input injection — 6 taps × {ms, weight}. Defaults match
    // HallReverb::kDefaultTapTimesMs / kDefaultTapWeights. Max tap time
    // = 250 ms matches HallReverb::kMaxTapTimeMs; the predelay ring is
    // sized for that maximum so live setter changes never realloc on
    // the audio thread.
    static constexpr float kTapMsDefaults[6] = { 22.0f, 35.0f, 55.0f, 90.0f, 140.0f, 200.0f };
    static constexpr float kTapWDefaults [6] = {  1.50f, 0.80f, 0.30f, 0.10f, 0.05f, 0.02f };
    const char* const kTapMsNames[6] = {
        "Hall Tap 0 Ms", "Hall Tap 1 Ms", "Hall Tap 2 Ms",
        "Hall Tap 3 Ms", "Hall Tap 4 Ms", "Hall Tap 5 Ms"
    };
    const char* const kTapMsIds[6] = {
        "hall_tap_0_ms", "hall_tap_1_ms", "hall_tap_2_ms",
        "hall_tap_3_ms", "hall_tap_4_ms", "hall_tap_5_ms"
    };
    const char* const kTapWNames[6] = {
        "Hall Tap 0 Weight", "Hall Tap 1 Weight", "Hall Tap 2 Weight",
        "Hall Tap 3 Weight", "Hall Tap 4 Weight", "Hall Tap 5 Weight"
    };
    const char* const kTapWIds[6] = {
        "hall_tap_0_w", "hall_tap_1_w", "hall_tap_2_w",
        "hall_tap_3_w", "hall_tap_4_w", "hall_tap_5_w"
    };
    for (int i = 0; i < 6; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kTapMsIds[i], 1 }, kTapMsNames[i],
            juce::NormalisableRange<float> (0.0f, 250.0f, 0.0f, 0.5f), kTapMsDefaults[i]));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kTapWIds[i], 1 }, kTapWNames[i],
            juce::NormalisableRange<float> (0.0f, 2.0f), kTapWDefaults[i]));
    }

    // P8b specular taps (4 × {ms, weight} + shared HF cut)
    static constexpr float kSpecMsDefaults[4] = { 3.0f, 6.0f, 11.0f, 17.0f };
    static constexpr float kSpecWDefaults [4] = { 0.80f, 0.60f, 0.40f, 0.20f };
    const char* const kSpecMsNames[4] = {
        "Hall Spec 0 Ms", "Hall Spec 1 Ms", "Hall Spec 2 Ms", "Hall Spec 3 Ms"
    };
    const char* const kSpecMsIds[4] = {
        "hall_spec_0_ms", "hall_spec_1_ms", "hall_spec_2_ms", "hall_spec_3_ms"
    };
    const char* const kSpecWNames[4] = {
        "Hall Spec 0 Weight", "Hall Spec 1 Weight", "Hall Spec 2 Weight", "Hall Spec 3 Weight"
    };
    const char* const kSpecWIds[4] = {
        "hall_spec_0_w", "hall_spec_1_w", "hall_spec_2_w", "hall_spec_3_w"
    };
    for (int i = 0; i < 4; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kSpecMsIds[i], 1 }, kSpecMsNames[i],
            juce::NormalisableRange<float> (0.0f, 50.0f, 0.0f, 0.7f), kSpecMsDefaults[i]));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kSpecWIds[i], 1 }, kSpecWNames[i],
            juce::NormalisableRange<float> (0.0f, 2.0f), kSpecWDefaults[i]));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_spec_hf_cut", 1 }, "Hall Spec HF Cut",
        juce::NormalisableRange<float> (200.0f, 20000.0f, 0.0f, 0.5f), 6000.0f));

    // P10 per-band peaking EQ — 3 bands × {gain dB, Q}. Fixed fc inside
    // HallReverb (250 / 1500 / 8000 Hz). 0 dB default = bypass (peaking
    // biquad with gain=0 is unity passthrough).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_eq_gain", 1 }, "Hall Bass EQ Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_eq_q", 1 }, "Hall Bass EQ Q",
        juce::NormalisableRange<float> (0.3f, 6.0f), 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_eq_gain", 1 }, "Hall Mid EQ Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_eq_q", 1 }, "Hall Mid EQ Q",
        juce::NormalisableRange<float> (0.3f, 6.0f), 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_eq_gain", 1 }, "Hall Treble EQ Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_eq_q", 1 }, "Hall Treble EQ Q",
        juce::NormalisableRange<float> (0.3f, 6.0f), 0.707f));

    // Per-band EQ centre frequencies (added 2026-05-20 to attack
    // box_ratio_db's 380 Hz modal spike that the fixed fc defaults
    // couldn't reach). Mid defaults to 380 Hz so the box-ratio target
    // is in immediate reach; bass/treble keep their previous defaults.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_eq_fc", 1 }, "Hall Bass EQ Fc",
        juce::NormalisableRange<float> (50.0f, 800.0f, 0.0f, 0.5f), 250.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_eq_fc", 1 }, "Hall Mid EQ Fc",
        juce::NormalisableRange<float> (200.0f, 5000.0f, 0.0f, 0.5f), 380.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_eq_fc", 1 }, "Hall Treble EQ Fc",
        juce::NormalisableRange<float> (2000.0f, 18000.0f, 0.0f, 0.5f), 8000.0f));

    // Per-band damping LP cutoff (Hz). Paired with hall_{bass/mid/treble}_damping
    // amount to form a 1-pole high-shelf per band. Independent fc per
    // band lets the optimizer match Lex's per-octave HF rolloff signature
    // (closes centroid_drift_per_band).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_damping_fc", 1 }, "Hall Bass Damping Fc",
        juce::NormalisableRange<float> (100.0f, 20000.0f, 0.0f, 0.5f), 8000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_damping_fc", 1 }, "Hall Mid Damping Fc",
        juce::NormalisableRange<float> (100.0f, 20000.0f, 0.0f, 0.5f), 2000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_damping_fc", 1 }, "Hall Treble Damping Fc",
        juce::NormalisableRange<float> (100.0f, 20000.0f, 0.0f, 0.5f), 12000.0f));

    // Per-band modulation depth + rate. Bass default = slow + deep
    // (matches Lex's bass-band wander), treble default = faster +
    // shallower (matches Lex's HF spin pattern). Mid centred on the
    // Lex Med Hall Spin 2.9 Hz / Wander 15 ms baseline.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_mod_depth", 1 }, "Hall Bass Mod Depth",
        juce::NormalisableRange<float> (0.0f, 16.0f), 0.20f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_mod_rate", 1 }, "Hall Bass Mod Rate",
        juce::NormalisableRange<float> (0.01f, 10.0f, 0.0f, 0.5f), 1.2f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_mod_depth", 1 }, "Hall Mid Mod Depth",
        juce::NormalisableRange<float> (0.0f, 16.0f), 0.15f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_mod_rate", 1 }, "Hall Mid Mod Rate",
        juce::NormalisableRange<float> (0.01f, 10.0f, 0.0f, 0.5f), 2.9f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_mod_depth", 1 }, "Hall Treble Mod Depth",
        juce::NormalisableRange<float> (0.0f, 16.0f), 0.10f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_mod_rate", 1 }, "Hall Treble Mod Rate",
        juce::NormalisableRange<float> (0.01f, 10.0f, 0.0f, 0.5f), 5.5f));

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
    bassChokeParam_     = parameters.getRawParameterValue ("bass_choke");
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
    firstReflLDlyParam_  = parameters.getRawParameterValue ("first_refl_l_dly");
    firstReflRDlyParam_  = parameters.getRawParameterValue ("first_refl_r_dly");
    firstReflLGainParam_ = parameters.getRawParameterValue ("first_refl_l_gain");
    firstReflRGainParam_ = parameters.getRawParameterValue ("first_refl_r_gain");
    firstReflHFCutParam_ = parameters.getRawParameterValue ("first_refl_hf_cut");

    hallBassDampingParam_     = parameters.getRawParameterValue ("hall_bass_damping");
    hallMidDampingParam_      = parameters.getRawParameterValue ("hall_mid_damping");
    hallTrebleDampingParam_   = parameters.getRawParameterValue ("hall_treble_damping");
    hallBassGainParam_        = parameters.getRawParameterValue ("hall_bass_gain");
    hallMidGainParam_         = parameters.getRawParameterValue ("hall_mid_gain");
    hallTrebleGainParam_      = parameters.getRawParameterValue ("hall_treble_gain");
    hallInlineDiffusionParam_ = parameters.getRawParameterValue ("hall_inline_diffusion");
    hallStereoWidthParam_     = parameters.getRawParameterValue ("hall_stereo_width");

    hallTap0MsParam_     = parameters.getRawParameterValue ("hall_tap_0_ms");
    hallTap1MsParam_     = parameters.getRawParameterValue ("hall_tap_1_ms");
    hallTap2MsParam_     = parameters.getRawParameterValue ("hall_tap_2_ms");
    hallTap3MsParam_     = parameters.getRawParameterValue ("hall_tap_3_ms");
    hallTap4MsParam_     = parameters.getRawParameterValue ("hall_tap_4_ms");
    hallTap5MsParam_     = parameters.getRawParameterValue ("hall_tap_5_ms");
    hallTap0WeightParam_ = parameters.getRawParameterValue ("hall_tap_0_w");
    hallTap1WeightParam_ = parameters.getRawParameterValue ("hall_tap_1_w");
    hallTap2WeightParam_ = parameters.getRawParameterValue ("hall_tap_2_w");
    hallTap3WeightParam_ = parameters.getRawParameterValue ("hall_tap_3_w");
    hallTap4WeightParam_ = parameters.getRawParameterValue ("hall_tap_4_w");
    hallTap5WeightParam_ = parameters.getRawParameterValue ("hall_tap_5_w");

    hallSpec0MsParam_     = parameters.getRawParameterValue ("hall_spec_0_ms");
    hallSpec1MsParam_     = parameters.getRawParameterValue ("hall_spec_1_ms");
    hallSpec2MsParam_     = parameters.getRawParameterValue ("hall_spec_2_ms");
    hallSpec3MsParam_     = parameters.getRawParameterValue ("hall_spec_3_ms");
    hallSpec0WeightParam_ = parameters.getRawParameterValue ("hall_spec_0_w");
    hallSpec1WeightParam_ = parameters.getRawParameterValue ("hall_spec_1_w");
    hallSpec2WeightParam_ = parameters.getRawParameterValue ("hall_spec_2_w");
    hallSpec3WeightParam_ = parameters.getRawParameterValue ("hall_spec_3_w");
    hallSpecHFCutParam_   = parameters.getRawParameterValue ("hall_spec_hf_cut");

    hallBassEQGainParam_   = parameters.getRawParameterValue ("hall_bass_eq_gain");
    hallBassEQQParam_      = parameters.getRawParameterValue ("hall_bass_eq_q");
    hallMidEQGainParam_    = parameters.getRawParameterValue ("hall_mid_eq_gain");
    hallMidEQQParam_       = parameters.getRawParameterValue ("hall_mid_eq_q");
    hallTrebleEQGainParam_ = parameters.getRawParameterValue ("hall_treble_eq_gain");
    hallTrebleEQQParam_    = parameters.getRawParameterValue ("hall_treble_eq_q");
    hallBassEQFcParam_     = parameters.getRawParameterValue ("hall_bass_eq_fc");
    hallMidEQFcParam_      = parameters.getRawParameterValue ("hall_mid_eq_fc");
    hallTrebleEQFcParam_   = parameters.getRawParameterValue ("hall_treble_eq_fc");
    hallBassDampingFcParam_   = parameters.getRawParameterValue ("hall_bass_damping_fc");
    hallMidDampingFcParam_    = parameters.getRawParameterValue ("hall_mid_damping_fc");
    hallTrebleDampingFcParam_ = parameters.getRawParameterValue ("hall_treble_damping_fc");
    hallBassModDepthParam_    = parameters.getRawParameterValue ("hall_bass_mod_depth");
    hallBassModRateParam_     = parameters.getRawParameterValue ("hall_bass_mod_rate");
    hallMidModDepthParam_     = parameters.getRawParameterValue ("hall_mid_mod_depth");
    hallMidModRateParam_      = parameters.getRawParameterValue ("hall_mid_mod_rate");
    hallTrebleModDepthParam_  = parameters.getRawParameterValue ("hall_treble_mod_depth");
    hallTrebleModRateParam_   = parameters.getRawParameterValue ("hall_treble_mod_rate");

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
        lastFirstReflLDly_ = lastFirstReflRDly_ = -1.0f;
        lastFirstReflLGain_ = lastFirstReflRGain_ = -999.0f;
        lastFirstReflHFCut_ = -1.0f;
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
    pushIfChanged (lastDamping_,   dampingParam_->load(),   [this] (float v) { activeEngine_->setTrebleMultiply (v); });
    pushIfChanged (lastBassMult_,  bassMultParam_->load(),  [this] (float v) { activeEngine_->setBassMultiply (v); });
    pushIfChanged (lastMidMult_,   midMultParam_->load(),   [this] (float v) { activeEngine_->setMidMultiply (v); });
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
    pushIfChanged (lastWidth_,     widthParam_->load(),     [this] (float v) { activeEngine_->setWidth (v); });
    pushIfChanged (lastGainTrim_,  gainTrimParam_->load(),  [this] (float v) { activeEngine_->setGainTrim (v); });
    pushIfChanged (lastMonoBelow_, monoBelowParam_->load(), [this] (float v) { activeEngine_->setMonoBelow (v); });
    pushIfChanged (lastFirstReflLDly_,  firstReflLDlyParam_->load(),  [this] (float v) { activeEngine_->setFirstReflLDelayMs (v); });
    pushIfChanged (lastFirstReflRDly_,  firstReflRDlyParam_->load(),  [this] (float v) { activeEngine_->setFirstReflRDelayMs (v); });
    pushIfChanged (lastFirstReflLGain_, firstReflLGainParam_->load(), [this] (float v) { activeEngine_->setFirstReflLGainDb  (v); });
    pushIfChanged (lastFirstReflRGain_, firstReflRGainParam_->load(), [this] (float v) { activeEngine_->setFirstReflRGainDb  (v); });
    pushIfChanged (lastFirstReflHFCut_, firstReflHFCutParam_->load(), [this] (float v) { activeEngine_->setFirstReflHFCutHz  (v); });

    // ───── Hall (Lex) advanced — algo 10 only; other engines no-op ─────
    pushIfChanged (lastHallBassDamping_,     hallBassDampingParam_->load(),     [this] (float v) { activeEngine_->setHallBassDamping     (v); });
    pushIfChanged (lastHallMidDamping_,      hallMidDampingParam_->load(),      [this] (float v) { activeEngine_->setHallMidDamping      (v); });
    pushIfChanged (lastHallTrebleDamping_,   hallTrebleDampingParam_->load(),   [this] (float v) { activeEngine_->setHallTrebleDamping   (v); });
    pushIfChanged (lastHallBassGain_,        hallBassGainParam_->load(),        [this] (float v) { activeEngine_->setHallBassGain        (v); });
    pushIfChanged (lastHallMidGain_,         hallMidGainParam_->load(),         [this] (float v) { activeEngine_->setHallMidGain         (v); });
    pushIfChanged (lastHallTrebleGain_,      hallTrebleGainParam_->load(),      [this] (float v) { activeEngine_->setHallTrebleGain      (v); });
    pushIfChanged (lastHallInlineDiffusion_, hallInlineDiffusionParam_->load(), [this] (float v) { activeEngine_->setHallInlineDiffusion (v); });
    pushIfChanged (lastHallStereoWidth_,     hallStereoWidthParam_->load(),     [this] (float v) { activeEngine_->setHallStereoWidth     (v); });
    pushIfChanged (lastHallTap0Ms_,     hallTap0MsParam_->load(),     [this] (float v) { activeEngine_->setHallTap0Ms     (v); });
    pushIfChanged (lastHallTap1Ms_,     hallTap1MsParam_->load(),     [this] (float v) { activeEngine_->setHallTap1Ms     (v); });
    pushIfChanged (lastHallTap2Ms_,     hallTap2MsParam_->load(),     [this] (float v) { activeEngine_->setHallTap2Ms     (v); });
    pushIfChanged (lastHallTap3Ms_,     hallTap3MsParam_->load(),     [this] (float v) { activeEngine_->setHallTap3Ms     (v); });
    pushIfChanged (lastHallTap4Ms_,     hallTap4MsParam_->load(),     [this] (float v) { activeEngine_->setHallTap4Ms     (v); });
    pushIfChanged (lastHallTap5Ms_,     hallTap5MsParam_->load(),     [this] (float v) { activeEngine_->setHallTap5Ms     (v); });
    pushIfChanged (lastHallTap0Weight_, hallTap0WeightParam_->load(), [this] (float v) { activeEngine_->setHallTap0Weight (v); });
    pushIfChanged (lastHallTap1Weight_, hallTap1WeightParam_->load(), [this] (float v) { activeEngine_->setHallTap1Weight (v); });
    pushIfChanged (lastHallTap2Weight_, hallTap2WeightParam_->load(), [this] (float v) { activeEngine_->setHallTap2Weight (v); });
    pushIfChanged (lastHallTap3Weight_, hallTap3WeightParam_->load(), [this] (float v) { activeEngine_->setHallTap3Weight (v); });
    pushIfChanged (lastHallTap4Weight_, hallTap4WeightParam_->load(), [this] (float v) { activeEngine_->setHallTap4Weight (v); });
    pushIfChanged (lastHallTap5Weight_, hallTap5WeightParam_->load(), [this] (float v) { activeEngine_->setHallTap5Weight (v); });
    pushIfChanged (lastHallSpec0Ms_,     hallSpec0MsParam_->load(),     [this] (float v) { activeEngine_->setHallSpec0Ms     (v); });
    pushIfChanged (lastHallSpec1Ms_,     hallSpec1MsParam_->load(),     [this] (float v) { activeEngine_->setHallSpec1Ms     (v); });
    pushIfChanged (lastHallSpec2Ms_,     hallSpec2MsParam_->load(),     [this] (float v) { activeEngine_->setHallSpec2Ms     (v); });
    pushIfChanged (lastHallSpec3Ms_,     hallSpec3MsParam_->load(),     [this] (float v) { activeEngine_->setHallSpec3Ms     (v); });
    pushIfChanged (lastHallSpec0Weight_, hallSpec0WeightParam_->load(), [this] (float v) { activeEngine_->setHallSpec0Weight (v); });
    pushIfChanged (lastHallSpec1Weight_, hallSpec1WeightParam_->load(), [this] (float v) { activeEngine_->setHallSpec1Weight (v); });
    pushIfChanged (lastHallSpec2Weight_, hallSpec2WeightParam_->load(), [this] (float v) { activeEngine_->setHallSpec2Weight (v); });
    pushIfChanged (lastHallSpec3Weight_, hallSpec3WeightParam_->load(), [this] (float v) { activeEngine_->setHallSpec3Weight (v); });
    pushIfChanged (lastHallSpecHFCut_,   hallSpecHFCutParam_->load(),   [this] (float v) { activeEngine_->setHallSpecHFCutHz (v); });
    pushIfChanged (lastHallBassEQGain_,   hallBassEQGainParam_->load(),   [this] (float v) { activeEngine_->setHallBassEQGain   (v); });
    pushIfChanged (lastHallBassEQQ_,      hallBassEQQParam_->load(),      [this] (float v) { activeEngine_->setHallBassEQQ      (v); });
    pushIfChanged (lastHallMidEQGain_,    hallMidEQGainParam_->load(),    [this] (float v) { activeEngine_->setHallMidEQGain    (v); });
    pushIfChanged (lastHallMidEQQ_,       hallMidEQQParam_->load(),       [this] (float v) { activeEngine_->setHallMidEQQ       (v); });
    pushIfChanged (lastHallTrebleEQGain_, hallTrebleEQGainParam_->load(), [this] (float v) { activeEngine_->setHallTrebleEQGain (v); });
    pushIfChanged (lastHallTrebleEQQ_,    hallTrebleEQQParam_->load(),    [this] (float v) { activeEngine_->setHallTrebleEQQ    (v); });
    pushIfChanged (lastHallBassEQFc_,     hallBassEQFcParam_->load(),     [this] (float v) { activeEngine_->setHallBassEQFc     (v); });
    pushIfChanged (lastHallMidEQFc_,      hallMidEQFcParam_->load(),      [this] (float v) { activeEngine_->setHallMidEQFc      (v); });
    pushIfChanged (lastHallTrebleEQFc_,   hallTrebleEQFcParam_->load(),   [this] (float v) { activeEngine_->setHallTrebleEQFc   (v); });
    pushIfChanged (lastHallBassDampingFc_,   hallBassDampingFcParam_->load(),   [this] (float v) { activeEngine_->setHallBassDampingFc   (v); });
    pushIfChanged (lastHallMidDampingFc_,    hallMidDampingFcParam_->load(),    [this] (float v) { activeEngine_->setHallMidDampingFc    (v); });
    pushIfChanged (lastHallTrebleDampingFc_, hallTrebleDampingFcParam_->load(), [this] (float v) { activeEngine_->setHallTrebleDampingFc (v); });
    pushIfChanged (lastHallBassModDepth_,    hallBassModDepthParam_->load(),    [this] (float v) { activeEngine_->setHallBassModDepth    (v); });
    pushIfChanged (lastHallBassModRate_,     hallBassModRateParam_->load(),     [this] (float v) { activeEngine_->setHallBassModRate     (v); });
    pushIfChanged (lastHallMidModDepth_,     hallMidModDepthParam_->load(),     [this] (float v) { activeEngine_->setHallMidModDepth     (v); });
    pushIfChanged (lastHallMidModRate_,      hallMidModRateParam_->load(),      [this] (float v) { activeEngine_->setHallMidModRate      (v); });
    pushIfChanged (lastHallTrebleModDepth_,  hallTrebleModDepthParam_->load(),  [this] (float v) { activeEngine_->setHallTrebleModDepth  (v); });
    pushIfChanged (lastHallTrebleModRate_,   hallTrebleModRateParam_->load(),   [this] (float v) { activeEngine_->setHallTrebleModRate   (v); });

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
    state.setProperty ("sixAPEarlyHighpassHz", sixAPBrightness_.earlyHighpassHz, nullptr);
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
    if (tree.hasProperty ("sixAPEarlyHighpassHz"))
        sixAPBrightness_.earlyHighpassHz = static_cast<float> (tree.getProperty ("sixAPEarlyHighpassHz"));
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
    target.setSixAPEarlyHighpassHz (sixAPBrightness_.earlyHighpassHz);
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
    target->setBassChokeHz (bassChokeParam_->load());
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
    target->setFirstReflLDelayMs (firstReflLDlyParam_->load());
    target->setFirstReflRDelayMs (firstReflRDlyParam_->load());
    target->setFirstReflLGainDb  (firstReflLGainParam_->load());
    target->setFirstReflRGainDb  (firstReflRGainParam_->load());
    target->setFirstReflHFCutHz  (firstReflHFCutParam_->load());

    // Hall (Lex) advanced — DuskVerbEngine forwards only to hall_; other
    // engines see no effect, so this is safe to push unconditionally.
    target->setHallBassDamping      (hallBassDampingParam_->load());
    target->setHallMidDamping       (hallMidDampingParam_->load());
    target->setHallTrebleDamping    (hallTrebleDampingParam_->load());
    target->setHallBassGain         (hallBassGainParam_->load());
    target->setHallMidGain          (hallMidGainParam_->load());
    target->setHallTrebleGain       (hallTrebleGainParam_->load());
    target->setHallInlineDiffusion  (hallInlineDiffusionParam_->load());
    target->setHallStereoWidth      (hallStereoWidthParam_->load());
    target->setHallTap0Ms     (hallTap0MsParam_->load());
    target->setHallTap1Ms     (hallTap1MsParam_->load());
    target->setHallTap2Ms     (hallTap2MsParam_->load());
    target->setHallTap3Ms     (hallTap3MsParam_->load());
    target->setHallTap4Ms     (hallTap4MsParam_->load());
    target->setHallTap5Ms     (hallTap5MsParam_->load());
    target->setHallTap0Weight (hallTap0WeightParam_->load());
    target->setHallTap1Weight (hallTap1WeightParam_->load());
    target->setHallTap2Weight (hallTap2WeightParam_->load());
    target->setHallTap3Weight (hallTap3WeightParam_->load());
    target->setHallTap4Weight (hallTap4WeightParam_->load());
    target->setHallTap5Weight (hallTap5WeightParam_->load());
    target->setHallSpec0Ms     (hallSpec0MsParam_->load());
    target->setHallSpec1Ms     (hallSpec1MsParam_->load());
    target->setHallSpec2Ms     (hallSpec2MsParam_->load());
    target->setHallSpec3Ms     (hallSpec3MsParam_->load());
    target->setHallSpec0Weight (hallSpec0WeightParam_->load());
    target->setHallSpec1Weight (hallSpec1WeightParam_->load());
    target->setHallSpec2Weight (hallSpec2WeightParam_->load());
    target->setHallSpec3Weight (hallSpec3WeightParam_->load());
    target->setHallSpecHFCutHz (hallSpecHFCutParam_->load());
    target->setHallBassEQGain   (hallBassEQGainParam_->load());
    target->setHallBassEQQ      (hallBassEQQParam_->load());
    target->setHallMidEQGain    (hallMidEQGainParam_->load());
    target->setHallMidEQQ       (hallMidEQQParam_->load());
    target->setHallTrebleEQGain (hallTrebleEQGainParam_->load());
    target->setHallTrebleEQQ    (hallTrebleEQQParam_->load());
    target->setHallBassEQFc     (hallBassEQFcParam_->load());
    target->setHallMidEQFc      (hallMidEQFcParam_->load());
    target->setHallTrebleEQFc   (hallTrebleEQFcParam_->load());
    target->setHallBassDampingFc   (hallBassDampingFcParam_->load());
    target->setHallMidDampingFc    (hallMidDampingFcParam_->load());
    target->setHallTrebleDampingFc (hallTrebleDampingFcParam_->load());
    target->setHallBassModDepth    (hallBassModDepthParam_->load());
    target->setHallBassModRate     (hallBassModRateParam_->load());
    target->setHallMidModDepth     (hallMidModDepthParam_->load());
    target->setHallMidModRate      (hallMidModRateParam_->load());
    target->setHallTrebleModDepth  (hallTrebleModDepthParam_->load());
    target->setHallTrebleModRate   (hallTrebleModRateParam_->load());

    // Mix lives on the processor — not pushed to the engine. The
    // processor's mixSmoother target is updated in performPresetSwap so
    // it picks up the new preset's value without re-pushing here.

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
    lastBassChoke_     = bassChokeParam_->load();
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
    lastFirstReflLDly_  = firstReflLDlyParam_->load();
    lastFirstReflRDly_  = firstReflRDlyParam_->load();
    lastFirstReflLGain_ = firstReflLGainParam_->load();
    lastFirstReflRGain_ = firstReflRGainParam_->load();
    lastFirstReflHFCut_ = firstReflHFCutParam_->load();

    lastHallBassDamping_     = hallBassDampingParam_->load();
    lastHallMidDamping_      = hallMidDampingParam_->load();
    lastHallTrebleDamping_   = hallTrebleDampingParam_->load();
    lastHallBassGain_        = hallBassGainParam_->load();
    lastHallMidGain_         = hallMidGainParam_->load();
    lastHallTrebleGain_      = hallTrebleGainParam_->load();
    lastHallInlineDiffusion_ = hallInlineDiffusionParam_->load();
    lastHallStereoWidth_     = hallStereoWidthParam_->load();
    lastHallTap0Ms_     = hallTap0MsParam_->load();
    lastHallTap1Ms_     = hallTap1MsParam_->load();
    lastHallTap2Ms_     = hallTap2MsParam_->load();
    lastHallTap3Ms_     = hallTap3MsParam_->load();
    lastHallTap4Ms_     = hallTap4MsParam_->load();
    lastHallTap5Ms_     = hallTap5MsParam_->load();
    lastHallTap0Weight_ = hallTap0WeightParam_->load();
    lastHallTap1Weight_ = hallTap1WeightParam_->load();
    lastHallTap2Weight_ = hallTap2WeightParam_->load();
    lastHallTap3Weight_ = hallTap3WeightParam_->load();
    lastHallTap4Weight_ = hallTap4WeightParam_->load();
    lastHallTap5Weight_ = hallTap5WeightParam_->load();
    lastHallSpec0Ms_     = hallSpec0MsParam_->load();
    lastHallSpec1Ms_     = hallSpec1MsParam_->load();
    lastHallSpec2Ms_     = hallSpec2MsParam_->load();
    lastHallSpec3Ms_     = hallSpec3MsParam_->load();
    lastHallSpec0Weight_ = hallSpec0WeightParam_->load();
    lastHallSpec1Weight_ = hallSpec1WeightParam_->load();
    lastHallSpec2Weight_ = hallSpec2WeightParam_->load();
    lastHallSpec3Weight_ = hallSpec3WeightParam_->load();
    lastHallSpecHFCut_   = hallSpecHFCutParam_->load();
    lastHallBassEQGain_   = hallBassEQGainParam_->load();
    lastHallBassEQQ_      = hallBassEQQParam_->load();
    lastHallMidEQGain_    = hallMidEQGainParam_->load();
    lastHallMidEQQ_       = hallMidEQQParam_->load();
    lastHallTrebleEQGain_ = hallTrebleEQGainParam_->load();
    lastHallTrebleEQQ_    = hallTrebleEQQParam_->load();
    lastHallBassEQFc_     = hallBassEQFcParam_->load();
    lastHallMidEQFc_      = hallMidEQFcParam_->load();
    lastHallTrebleEQFc_   = hallTrebleEQFcParam_->load();
    lastHallBassDampingFc_   = hallBassDampingFcParam_->load();
    lastHallMidDampingFc_    = hallMidDampingFcParam_->load();
    lastHallTrebleDampingFc_ = hallTrebleDampingFcParam_->load();
    lastHallBassModDepth_    = hallBassModDepthParam_->load();
    lastHallBassModRate_     = hallBassModRateParam_->load();
    lastHallMidModDepth_     = hallMidModDepthParam_->load();
    lastHallMidModRate_      = hallMidModRateParam_->load();
    lastHallTrebleModDepth_  = hallTrebleModDepthParam_->load();
    lastHallTrebleModRate_   = hallTrebleModRateParam_->load();

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
    sixAPBrightness_.earlyHighpassHz = preset.sixAPEarlyHighpassHz;
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
    engine.setSixAPEarlyHighpassHz (sixAPEarlyHighpassHz);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskVerbProcessor();
}

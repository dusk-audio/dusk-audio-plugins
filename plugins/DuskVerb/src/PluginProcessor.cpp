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
    //
    // P13 Heavy Spin & Wander defaults — 16-channel topology static comb
    // resonance is defeated by Lexicon-style heavy time-varying delay
    // modulation. Depth range extended 16 → 64 samples (Lex Wander 15 ms
    // ≈ 720 samples at 48 kHz; even our 64-cap is a fraction). Default
    // shape = 1.0 (full random-walk, no static sine LFO sidebands).
    // Default rate = 5.0 Hz (Lex Spin range 1-10 Hz). Default depth =
    // 8 samples — enough to slide each delay tap by ±8 / N samples
    // sample-by-sample so combs never settle into resonance.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_mod_depth", 1 }, "Hall Bass Mod Depth",
        juce::NormalisableRange<float> (0.0f, 64.0f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_mod_rate", 1 }, "Hall Bass Mod Rate",
        juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.5f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_mod_depth", 1 }, "Hall Mid Mod Depth",
        juce::NormalisableRange<float> (0.0f, 64.0f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_mod_rate", 1 }, "Hall Mid Mod Rate",
        juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.5f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_mod_depth", 1 }, "Hall Treble Mod Depth",
        juce::NormalisableRange<float> (0.0f, 64.0f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_mod_rate", 1 }, "Hall Treble Mod Rate",
        juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.5f), 5.0f));

    // Per-band modulation shape — 0 = sine (legacy), 1 = bounded
    // random-walk per channel. P13 default flipped to 1.0 (full
    // random-walk) to defeat the 16-channel topology's static comb
    // resonance — sine creates discrete LFO sidebands that align with
    // delay modes, random-walk produces continuous spectral smear that
    // prevents coherent mode buildup.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_mod_shape", 1 }, "Hall Bass Mod Shape",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_mod_shape", 1 }, "Hall Mid Mod Shape",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_mod_shape", 1 }, "Hall Treble Mod Shape",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    // Per-band channel feedback gain spread — Lex-style decoherence.
    // Range 0..0.4: 0 = identical gain across the 8 channels of the band
    // (synchronous modal decay → centroid_drift peaks); 0.4 = ±20 %
    // per-channel gain variation. Frequency-flat by construction, so no
    // c80/d50 trade-off the in-feedback damping LP imposes. Default 0
    // preserves backwards compatibility; calibration target is non-zero.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_chan_gain_spread", 1 }, "Hall Bass Chan Gain Spread",
        juce::NormalisableRange<float> (0.0f, 0.4f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_chan_gain_spread", 1 }, "Hall Mid Chan Gain Spread",
        juce::NormalisableRange<float> (0.0f, 0.4f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_chan_gain_spread", 1 }, "Hall Treble Chan Gain Spread",
        juce::NormalisableRange<float> (0.0f, 0.4f), 0.0f));

    // P11 per-band post-tank high-shelf — decoupled HF rolloff. Replaces
    // the role the in-feedback damping LP plays in the tail spectrum
    // (closes c80/d50 via natural HF rolloff) WITHOUT the in-loop drift
    // an LP causes inside the FDN. Default gain 0 dB = flat passthrough
    // → zero regression on 7/19 baseline; calibration target is negative
    // gain on bass/mid/treble shelves combined with low in-feedback damping.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_shelf_gain", 1 }, "Hall Bass Shelf Gain",
        juce::NormalisableRange<float> (-24.0f, 6.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_bass_shelf_fc", 1 }, "Hall Bass Shelf Fc",
        juce::NormalisableRange<float> (100.0f, 16000.0f, 0.0f, 0.3f), 4000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_shelf_gain", 1 }, "Hall Mid Shelf Gain",
        juce::NormalisableRange<float> (-24.0f, 6.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_mid_shelf_fc", 1 }, "Hall Mid Shelf Fc",
        juce::NormalisableRange<float> (100.0f, 16000.0f, 0.0f, 0.3f), 4000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_shelf_gain", 1 }, "Hall Treble Shelf Gain",
        juce::NormalisableRange<float> (-24.0f, 6.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_treble_shelf_fc", 1 }, "Hall Treble Shelf Fc",
        juce::NormalisableRange<float> (100.0f, 16000.0f, 0.0f, 0.3f), 4000.0f));

    // P14 input diffuser coefficient — 4-stage Schroeder allpass cascade
    // applied BEFORE the LR4 split into bands. 0 = bypass; 0.65 = classic
    // Schroeder; max 0.85 (stays well shy of allpass instability margin).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hall_input_diffusion", 1 }, "Hall Input Diffusion",
        juce::NormalisableRange<float> (0.0f, 0.85f), 0.65f));

    // ─── P15 Hall (Ring) — Griesinger sequential ring topology ───
    // 7 ring-specific axes (Decay Time + Size are shared engine axes).
    // Defaults sit at Lex Med Hall reference: 1.69 s RT60 + 0.625 Shape
    // (Schroeder classic) + 2.9 Hz Spin + 8-sample Wander (≈ Lex docs).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ring_damping", 1 }, "Ring Damping",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.20f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ring_damping_fc", 1 }, "Ring Damping Fc",
        juce::NormalisableRange<float> (100.0f, 20000.0f, 0.0f, 0.3f), 6000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ring_spread", 1 }, "Ring Spread",
        juce::NormalisableRange<float> (0.5f, 2.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ring_shape", 1 }, "Ring Shape",
        juce::NormalisableRange<float> (0.0f, 0.85f), 0.625f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ring_spin", 1 }, "Ring Spin",
        juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.5f), 2.9f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ring_wander", 1 }, "Ring Wander",
        juce::NormalisableRange<float> (0.0f, 64.0f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ring_stereo_width", 1 }, "Ring Stereo Width",
        juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));

    // ─── P16 Hall (Hybrid) — parallel ER + Ring + macro mix + shelves ─
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_ring_level", 1 }, "Hybrid Ring Level",
        juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_er_level", 1 }, "Hybrid ER Level",
        juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_er_w1", 1 }, "Hybrid ER W1",
        juce::NormalisableRange<float> (0.0f, 2.0f), 0.65f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_er_w2", 1 }, "Hybrid ER W2",
        juce::NormalisableRange<float> (0.0f, 2.0f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_er_w3", 1 }, "Hybrid ER W3",
        juce::NormalisableRange<float> (0.0f, 2.0f), 0.30f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_low_shelf_gain", 1 }, "Hybrid Low Shelf Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_low_shelf_fc", 1 }, "Hybrid Low Shelf Fc",
        juce::NormalisableRange<float> (60.0f, 2000.0f, 0.0f, 0.3f), 250.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_high_shelf_gain", 1 }, "Hybrid High Shelf Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_high_shelf_fc", 1 }, "Hybrid High Shelf Fc",
        juce::NormalisableRange<float> (1000.0f, 16000.0f, 0.0f, 0.3f), 6000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_ring_damping", 1 }, "Hybrid Ring Damping",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.20f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_ring_damping_fc", 1 }, "Hybrid Ring Damping Fc",
        juce::NormalisableRange<float> (100.0f, 20000.0f, 0.0f, 0.3f), 6000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_ring_spin", 1 }, "Hybrid Ring Spin",
        juce::NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.5f), 2.9f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_ring_wander", 1 }, "Hybrid Ring Wander",
        juce::NormalisableRange<float> (0.0f, 64.0f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_ring_stereo", 1 }, "Hybrid Ring Stereo",
        juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));

    // ─── P17 Hall (TrueLex) — ER TDL + FDN tank + post-mix Schroeder AP ─
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex_er_level", 1 }, "TrueLex ER Level",
        juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex_tank_level", 1 }, "TrueLex Tank Level",
        juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex_er_w0", 1 }, "TrueLex ER W0",
        juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex_er_w1", 1 }, "TrueLex ER W1",
        juce::NormalisableRange<float> (0.0f, 4.0f), 0.65f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex_er_w2", 1 }, "TrueLex ER W2",
        juce::NormalisableRange<float> (0.0f, 4.0f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex_er_w3", 1 }, "TrueLex ER W3",
        juce::NormalisableRange<float> (0.0f, 4.0f), 0.30f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex_ap_coeff", 1 }, "TrueLex AP Coeff",
        juce::NormalisableRange<float> (-0.85f, 0.85f), 0.5f));

    // ─── P18 Hall (TrueLex 16) — 16-ch FDN variant ────────────────────
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex16_er_level", 1 }, "TrueLex16 ER Level",
        juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex16_tank_level", 1 }, "TrueLex16 Tank Level",
        juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex16_er_w0", 1 }, "TrueLex16 ER W0",
        juce::NormalisableRange<float> (0.0f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex16_er_w1", 1 }, "TrueLex16 ER W1",
        juce::NormalisableRange<float> (0.0f, 4.0f), 0.65f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex16_er_w2", 1 }, "TrueLex16 ER W2",
        juce::NormalisableRange<float> (0.0f, 4.0f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex16_er_w3", 1 }, "TrueLex16 ER W3",
        juce::NormalisableRange<float> (0.0f, 4.0f), 0.30f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex16_er_w4", 1 }, "TrueLex16 ER W4",
        juce::NormalisableRange<float> (0.0f, 4.0f), 0.0f));   // C80-only fill tap @ 49ms
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "truelex16_ap_coeff", 1 }, "TrueLex16 AP Coeff",
        juce::NormalisableRange<float> (-0.85f, 0.85f), 0.5f));

    // ─── P19 Hall (LexFigure8) — exposed structural HF damping ─────
    // In-loop one-pole damping cutoff. Lower fc = HF dies faster
    // inside the recirculation loop → closes rt60_per_band 8k/16k
    // bins that floor at Lex's RT_HiCut spec (4500 Hz).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_struct_hf", 1 }, "LexFig8 Struct HF",
        juce::NormalisableRange<float> (1000.0f, 16000.0f, 0.0f, 0.3f), 8000.0f));

    // Phase A — Pre-tank ER TDL. 4 taps with independent delay (ms) +
    // gain (dB) + stereo offset. Defaults seed Lex Med Hall peak_locations
    // anchor [0.0, 4.0, 7.52, 9.79] ms with progressive -3/-6/-9/-12 dB
    // attenuation (ITDG decay typical of medium hall ER pattern).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_tap0_dly", 1 }, "LexFig8 ER Tap0 Dly",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.0f, 0.5f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_tap1_dly", 1 }, "LexFig8 ER Tap1 Dly",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.0f, 0.5f), 4.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_tap2_dly", 1 }, "LexFig8 ER Tap2 Dly",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.0f, 0.5f), 7.52f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_tap3_dly", 1 }, "LexFig8 ER Tap3 Dly",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.0f, 0.5f), 9.79f));
    // ER tap gains default to -60 dB (effectively muted). Engine 15
    // returns to v5 12/19 baseline by default; tuner can unmute taps
    // when exploring.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_tap0_gain", 1 }, "LexFig8 ER Tap0 Gain",
        juce::NormalisableRange<float> (-60.0f, 6.0f), -60.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_tap1_gain", 1 }, "LexFig8 ER Tap1 Gain",
        juce::NormalisableRange<float> (-60.0f, 6.0f), -60.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_tap2_gain", 1 }, "LexFig8 ER Tap2 Gain",
        juce::NormalisableRange<float> (-60.0f, 6.0f), -60.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_tap3_gain", 1 }, "LexFig8 ER Tap3 Gain",
        juce::NormalisableRange<float> (-60.0f, 6.0f), -60.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_er_stereo_offset", 1 }, "LexFig8 ER Stereo Offset",
        juce::NormalisableRange<float> (-2.0f, 2.0f), 0.20f));
    // Tank output attenuation 0..1. 1.0 = full tank (Phase A default).
    // Lower lets ER taps dominate the early window.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_tank_atten", 1 }, "LexFig8 Tank Atten",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    // Tank INPUT scale 0..1. Pre-attenuates audio fed into tank so
    // density-AP early peaks (at +0.25/1.27/2.27 ms) shrink while ER
    // taps stay full amplitude. Key knob for peak_locations_ms PASS.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_tank_in", 1 }, "LexFig8 Tank In",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    // Tank pre-delay 0..20 ms. Shifts tank density-AP early peaks
    // PAST ER tap times so peak_locations detector finds ER peaks
    // as the dominant first 4.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_tank_pdly", 1 }, "LexFig8 Tank PreDly",
        juce::NormalisableRange<float> (0.0f, 20.0f), 0.0f));
    // Density-AP jitter depth fraction (0..0.20). Drives the
    // RandomWalkLFO modulation amplitude on each density allpass —
    // higher values smear modal comb teeth in 200-500 Hz (target
    // box_ratio + spectral_crest). Default 0.02 matches the
    // pre-Phase-B-redux hardcoded value so engine state is unchanged
    // unless this knob is moved.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_density_jitter", 1 }, "LexFig8 Density Jitter",
        juce::NormalisableRange<float> (0.0f, 0.20f), 0.02f));
    // Density-AP jitter rate (Hz). Sub-audio (≤3 Hz typical) keeps the
    // FM sidebands inaudible while wider depth smears the comb teeth.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_density_rate", 1 }, "LexFig8 Density Rate",
        juce::NormalisableRange<float> (0.1f, 10.0f), 1.5f));
    // Phase C — sub-bass band damping (4-band split). Multiply=1.0 default
    // = neutral / shelf bypassed; values >1 extend sub-bass decay, <1
    // shorten it independently of the main bass multiplier.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_sub_bass_mult", 1 }, "LexFig8 Sub-Bass Mult",
        juce::NormalisableRange<float> (0.1f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_sub_bass_xover", 1 }, "LexFig8 Sub-Bass Xover",
        juce::NormalisableRange<float> (50.0f, 600.0f, 0.0f, 0.5f), 300.0f));
    // Phase D — in-loop tilt high-shelf at 2 kHz pivot. Negative dB darkens
    // late tail (centroid drifts DOWN more), positive brightens. Default 0
    // = neutral / shelf bypassed → Engine 10 + plates unaffected.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_tilt_db_oct", 1 }, "LexFig8 Tilt",
        juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f));
    // Phase F — 4th damping band (air, above 8 kHz). Default 1.0 = neutral
    // / shelf bypassed → Engine 10 + plates preserved bit-for-bit.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_air_mult", 1 }, "LexFig8 Air Mult",
        juce::NormalisableRange<float> (0.1f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_air_xover", 1 }, "LexFig8 Air Xover",
        juce::NormalisableRange<float> (4000.0f, 12000.0f, 0.0f, 0.5f), 8000.0f));
    // Phase G — 8-band damping. Per-octave multipliers (idx 0..7 =
    // 125/250/500/1k/2k/4k/8k/16k Hz). All default 1.0 keeps
    // eightBandActive_ false → uses ThreeBandDamping (Engine 10 +
    // plates preserved bit-for-bit when never set).
    for (int i = 0; i < 8; ++i)
    {
        const juce::String id   = "lexfig8_band_mult_" + juce::String (i);
        const juce::String name = "LexFig8 Band " + juce::String (i) + " Mult";
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.1f, 4.0f), 1.0f));
    }
    // Phase J — per-stage density-AP delay override (ms at 44.1 kHz reference).
    // 4 stages, default 0 = no override → engine uses hardcoded hall-scale
    // densityAPBase. Range 0..50 ms covers the Lex peak_locations anchor
    // span (0/4/7.52/9.79 ms) with headroom. Engine 10 + plates ignore.
    for (int i = 0; i < 4; ++i)
    {
        const juce::String id   = "lexfig8_dap_delay_" + juce::String (i);
        const juce::String name = "LexFig8 DAP " + juce::String (i) + " Dly";
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.0f, 50.0f), 0.0f));
    }
    // Phase K — first-4 output-tap positionFrac overrides per channel
    // (v28 output-tap rearch). Defaults match DattorroTank::kLeftOutputTaps[0..3]
    // and kRightOutputTaps[0..3] so the audible behavior is bit-for-bit
    // identical until the tuner moves them. Range 0.0..1.0 covers full
    // delay-line span. Engine 10 + plates ignore (each engine owns its
    // own DattorroTank instance and never calls the new setter).
    constexpr float kOtapDefaultL[4] = { 0.120f, 0.675f, 0.480f, 0.450f };
    constexpr float kOtapDefaultR[4] = { 0.140f, 0.710f, 0.520f, 0.410f };
    for (int i = 0; i < 4; ++i)
    {
        const juce::String idL   = "lexfig8_otap_L" + juce::String (i);
        const juce::String nameL = "LexFig8 OTap L" + juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { idL, 1 }, nameL,
            juce::NormalisableRange<float> (0.0f, 1.0f), kOtapDefaultL[i]));
        const juce::String idR   = "lexfig8_otap_R" + juce::String (i);
        const juce::String nameR = "LexFig8 OTap R" + juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { idR, 1 }, nameR,
            juce::NormalisableRange<float> (0.0f, 1.0f), kOtapDefaultR[i]));
    }
    // Phase L — per-channel delay1/delay2 base ms override (v29 Lexicon-primes
    // rearch). 0 = no override → uses Tank::delay1BaseDelay / delay2BaseDelay
    // (set by setHallScale, hall = 4507 / 3769 L, 4219 / 3299 R samples @
    // 44.1 kHz = 102.2 / 85.5 / 95.7 / 74.8 ms). Range 0..150 ms covers ±50%
    // around the hall defaults. Engine 10 + plates ignore.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_del1_L", 1 }, "LexFig8 Del1 L",
        juce::NormalisableRange<float> (0.0f, 150.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_del1_R", 1 }, "LexFig8 Del1 R",
        juce::NormalisableRange<float> (0.0f, 150.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_del2_L", 1 }, "LexFig8 Del2 L",
        juce::NormalisableRange<float> (0.0f, 150.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_del2_R", 1 }, "LexFig8 Del2 R",
        juce::NormalisableRange<float> (0.0f, 150.0f), 0.0f));
    // Phase M — per-channel AP1/AP2 base ms override (v30 diffuser rearch).
    // 0 = no override → tank uses Tank::ap1BaseDelay / ap2BaseDelay (set by
    // setHallScale, hall = 709/953 AP1, 1871/2749 AP2 @ 44.1 kHz =
    // 16.1/21.6/42.4/62.3 ms). Range 0..100 ms covers full diffuser reshape.
    // Engine 10 + plates ignore.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_ap1_L", 1 }, "LexFig8 AP1 L",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_ap1_R", 1 }, "LexFig8 AP1 R",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_ap2_L", 1 }, "LexFig8 AP2 L",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_ap2_R", 1 }, "LexFig8 AP2 R",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f));
    // Phase N (v31) — per-channel cross-feed coefficient. Default 1.0 =
    // Dattorro canonical figure-8 bleed (current behavior). Range 0..2.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_xfd_L", 1 }, "LexFig8 XFd L",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_xfd_R", 1 }, "LexFig8 XFd R",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    // Phase O (v31) — diffuser bypass (LexFigure8 only). Default 0 = diffuser
    // active (canonical behavior, identical to pre-v31). 1 = skipped entirely.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_bypass_diff", 1 }, "LexFig8 Bypass Diff",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    // Phase H — transient-gated tank ducker. Depth default 0 = disabled
    // (Engine 10 + plates never touch these params → preserved). When
    // depth > 0, envelope follower on raw input modulates per-sample
    // gain on tank input. ER taps bypass entirely.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_duck_thresh", 1 }, "LexFig8 Duck Thresh",
        juce::NormalisableRange<float> (0.001f, 1.0f, 0.0f, 0.4f), 0.10f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_duck_atk", 1 }, "LexFig8 Duck Atk",
        juce::NormalisableRange<float> (0.1f, 50.0f, 0.0f, 0.4f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_duck_rel", 1 }, "LexFig8 Duck Rel",
        juce::NormalisableRange<float> (0.1f, 500.0f, 0.0f, 0.4f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lexfig8_duck_depth", 1 }, "LexFig8 Duck Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // LexiconMTDL (algo 16) tuning surface. ER tap delays are LOCKED at
    // 0/4/7.52/9.79 ms inside LexiconMTDLEngine — not exposed to the
    // optimizer because they're the measured Lex Med Hall anchor positions.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mtdl_feedback", 1 }, "MTDL Feedback",
        juce::NormalisableRange<float> (0.50f, 0.999f, 0.0f, 1.5f), 0.98f));
    // v35 per-line damping (8) + v36 per-line feedback override (8)
    // + v36 per-tap ER gain dB (4) + v36 per-line LFO depth ms (8).
    for (int i = 0; i < 8; ++i)
    {
        const juce::String id   = juce::String ("mtdl_damping_hz_") + juce::String (i);
        const juce::String name = juce::String ("MTDL Damping Hz ") + juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (1000.0f, 18000.0f, 0.0f, 0.3f), 10000.0f));
    }
    for (int i = 0; i < 8; ++i)
    {
        const juce::String id   = juce::String ("mtdl_fb_") + juce::String (i);
        const juce::String name = juce::String ("MTDL Feedback ") + juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.5f, 1.5f), 1.0f));
    }
    for (int i = 0; i < 4; ++i)
    {
        const juce::String id   = juce::String ("mtdl_er_tap_gain_db_") + juce::String (i);
        const juce::String name = juce::String ("MTDL ER Tap Gain dB ") + juce::String (i);
        const float defaults[4] = { -15.0f, -18.0f, -21.0f, -24.0f };
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (-36.0f, -3.0f), defaults[i]));
    }
    // v37 — LFO axes LOCKED to 0..0 (proven toxic to spectral metrics
    // across 1494 v36 trials). Params retained for state-load compat.
    for (int i = 0; i < 8; ++i)
    {
        const juce::String id   = juce::String ("mtdl_line_mod_ms_") + juce::String (i);
        const juce::String name = juce::String ("MTDL Line Mod ms ") + juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.5f), 0.0f));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mtdl_er_level", 1 }, "MTDL ER Level",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mtdl_late_level", 1 }, "MTDL Late Level",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    // v37 — Schroeder pre-diffuser (multiplies modal density before FDN)
    // + tilt EQ (one knob targets box_ratio_db).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mtdl_schroeder_coeff", 1 }, "MTDL Schroeder",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mtdl_tilt_db", 1 }, "MTDL Tilt dB",
        juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f));

    // v38 — Engine 17 hybrid mix axes. Wash = Engine 15 (Dattorro) output
    // weight; Chatter = Engine 16 (MTDL) output weight. Linear gain.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_wash_level", 1 }, "Hybrid Wash",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hybrid_chatter_level", 1 }, "Hybrid Chatter",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));

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
    hallBassModShapeParam_    = parameters.getRawParameterValue ("hall_bass_mod_shape");
    hallMidModShapeParam_     = parameters.getRawParameterValue ("hall_mid_mod_shape");
    hallTrebleModShapeParam_  = parameters.getRawParameterValue ("hall_treble_mod_shape");
    hallBassChanGainSpreadParam_   = parameters.getRawParameterValue ("hall_bass_chan_gain_spread");
    hallMidChanGainSpreadParam_    = parameters.getRawParameterValue ("hall_mid_chan_gain_spread");
    hallTrebleChanGainSpreadParam_ = parameters.getRawParameterValue ("hall_treble_chan_gain_spread");
    hallBassShelfGainParam_   = parameters.getRawParameterValue ("hall_bass_shelf_gain");
    hallBassShelfFcParam_     = parameters.getRawParameterValue ("hall_bass_shelf_fc");
    hallMidShelfGainParam_    = parameters.getRawParameterValue ("hall_mid_shelf_gain");
    hallMidShelfFcParam_      = parameters.getRawParameterValue ("hall_mid_shelf_fc");
    hallTrebleShelfGainParam_ = parameters.getRawParameterValue ("hall_treble_shelf_gain");
    hallTrebleShelfFcParam_   = parameters.getRawParameterValue ("hall_treble_shelf_fc");
    hallInputDiffusionParam_  = parameters.getRawParameterValue ("hall_input_diffusion");
    ringDampingParam_         = parameters.getRawParameterValue ("ring_damping");
    ringDampingFcParam_       = parameters.getRawParameterValue ("ring_damping_fc");
    ringSpreadParam_          = parameters.getRawParameterValue ("ring_spread");
    ringShapeParam_           = parameters.getRawParameterValue ("ring_shape");
    ringSpinParam_            = parameters.getRawParameterValue ("ring_spin");
    ringWanderParam_          = parameters.getRawParameterValue ("ring_wander");
    ringStereoWidthParam_     = parameters.getRawParameterValue ("ring_stereo_width");
    hybridRingLevelParam_     = parameters.getRawParameterValue ("hybrid_ring_level");
    hybridERLevelParam_       = parameters.getRawParameterValue ("hybrid_er_level");
    hybridERW1Param_          = parameters.getRawParameterValue ("hybrid_er_w1");
    hybridERW2Param_          = parameters.getRawParameterValue ("hybrid_er_w2");
    hybridERW3Param_          = parameters.getRawParameterValue ("hybrid_er_w3");
    hybridLowShelfGainParam_  = parameters.getRawParameterValue ("hybrid_low_shelf_gain");
    hybridLowShelfFcParam_    = parameters.getRawParameterValue ("hybrid_low_shelf_fc");
    hybridHighShelfGainParam_ = parameters.getRawParameterValue ("hybrid_high_shelf_gain");
    hybridHighShelfFcParam_   = parameters.getRawParameterValue ("hybrid_high_shelf_fc");
    hybridRingDampingParam_   = parameters.getRawParameterValue ("hybrid_ring_damping");
    hybridRingDampingFcParam_ = parameters.getRawParameterValue ("hybrid_ring_damping_fc");
    hybridRingSpinParam_      = parameters.getRawParameterValue ("hybrid_ring_spin");
    hybridRingWanderParam_    = parameters.getRawParameterValue ("hybrid_ring_wander");
    hybridRingStereoParam_    = parameters.getRawParameterValue ("hybrid_ring_stereo");
    truelexERLevelParam_      = parameters.getRawParameterValue ("truelex_er_level");
    truelexTankLevelParam_    = parameters.getRawParameterValue ("truelex_tank_level");
    truelexERW0Param_         = parameters.getRawParameterValue ("truelex_er_w0");
    truelexERW1Param_         = parameters.getRawParameterValue ("truelex_er_w1");
    truelexERW2Param_         = parameters.getRawParameterValue ("truelex_er_w2");
    truelexERW3Param_         = parameters.getRawParameterValue ("truelex_er_w3");
    truelexAPCoeffParam_      = parameters.getRawParameterValue ("truelex_ap_coeff");
    truelex16ERLevelParam_    = parameters.getRawParameterValue ("truelex16_er_level");
    truelex16TankLevelParam_  = parameters.getRawParameterValue ("truelex16_tank_level");
    truelex16ERW0Param_       = parameters.getRawParameterValue ("truelex16_er_w0");
    truelex16ERW1Param_       = parameters.getRawParameterValue ("truelex16_er_w1");
    truelex16ERW2Param_       = parameters.getRawParameterValue ("truelex16_er_w2");
    truelex16ERW3Param_       = parameters.getRawParameterValue ("truelex16_er_w3");
    truelex16ERW4Param_       = parameters.getRawParameterValue ("truelex16_er_w4");
    truelex16APCoeffParam_    = parameters.getRawParameterValue ("truelex16_ap_coeff");
    lexFig8StructHFParam_     = parameters.getRawParameterValue ("lexfig8_struct_hf");
    lexFig8ERTap0DlyParam_    = parameters.getRawParameterValue ("lexfig8_er_tap0_dly");
    lexFig8ERTap1DlyParam_    = parameters.getRawParameterValue ("lexfig8_er_tap1_dly");
    lexFig8ERTap2DlyParam_    = parameters.getRawParameterValue ("lexfig8_er_tap2_dly");
    lexFig8ERTap3DlyParam_    = parameters.getRawParameterValue ("lexfig8_er_tap3_dly");
    lexFig8ERTap0GainParam_   = parameters.getRawParameterValue ("lexfig8_er_tap0_gain");
    lexFig8ERTap1GainParam_   = parameters.getRawParameterValue ("lexfig8_er_tap1_gain");
    lexFig8ERTap2GainParam_   = parameters.getRawParameterValue ("lexfig8_er_tap2_gain");
    lexFig8ERTap3GainParam_   = parameters.getRawParameterValue ("lexfig8_er_tap3_gain");
    lexFig8ERStereoOffsetParam_ = parameters.getRawParameterValue ("lexfig8_er_stereo_offset");
    lexFig8TankAttenParam_    = parameters.getRawParameterValue ("lexfig8_tank_atten");
    lexFig8TankInParam_       = parameters.getRawParameterValue ("lexfig8_tank_in");
    lexFig8TankPDlyParam_     = parameters.getRawParameterValue ("lexfig8_tank_pdly");
    lexFig8DensityJitterParam_ = parameters.getRawParameterValue ("lexfig8_density_jitter");
    lexFig8DensityRateParam_   = parameters.getRawParameterValue ("lexfig8_density_rate");
    lexFig8SubBassMultParam_   = parameters.getRawParameterValue ("lexfig8_sub_bass_mult");
    lexFig8SubBassXoverParam_  = parameters.getRawParameterValue ("lexfig8_sub_bass_xover");
    lexFig8TiltParam_          = parameters.getRawParameterValue ("lexfig8_tilt_db_oct");
    lexFig8AirMultParam_       = parameters.getRawParameterValue ("lexfig8_air_mult");
    lexFig8AirXoverParam_      = parameters.getRawParameterValue ("lexfig8_air_xover");
    for (int i = 0; i < 8; ++i)
    {
        const juce::String id = "lexfig8_band_mult_" + juce::String (i);
        lexFig8BandMultParam_[i] = parameters.getRawParameterValue (id);
    }
    for (int i = 0; i < 4; ++i)
    {
        const juce::String id = "lexfig8_dap_delay_" + juce::String (i);
        lexFig8DapDelayParam_[i] = parameters.getRawParameterValue (id);
    }
    for (int i = 0; i < 4; ++i)
    {
        lexFig8OTapLParam_[i] = parameters.getRawParameterValue ("lexfig8_otap_L" + juce::String (i));
        lexFig8OTapRParam_[i] = parameters.getRawParameterValue ("lexfig8_otap_R" + juce::String (i));
    }
    lexFig8Del1LParam_       = parameters.getRawParameterValue ("lexfig8_del1_L");
    lexFig8Del1RParam_       = parameters.getRawParameterValue ("lexfig8_del1_R");
    lexFig8Del2LParam_       = parameters.getRawParameterValue ("lexfig8_del2_L");
    lexFig8Del2RParam_       = parameters.getRawParameterValue ("lexfig8_del2_R");
    lexFig8AP1LParam_        = parameters.getRawParameterValue ("lexfig8_ap1_L");
    lexFig8AP1RParam_        = parameters.getRawParameterValue ("lexfig8_ap1_R");
    lexFig8AP2LParam_        = parameters.getRawParameterValue ("lexfig8_ap2_L");
    lexFig8AP2RParam_        = parameters.getRawParameterValue ("lexfig8_ap2_R");
    lexFig8XFdLParam_        = parameters.getRawParameterValue ("lexfig8_xfd_L");
    lexFig8XFdRParam_        = parameters.getRawParameterValue ("lexfig8_xfd_R");
    lexFig8BypassDiffParam_  = parameters.getRawParameterValue ("lexfig8_bypass_diff");
    lexFig8DuckThreshParam_  = parameters.getRawParameterValue ("lexfig8_duck_thresh");
    lexFig8DuckAtkParam_     = parameters.getRawParameterValue ("lexfig8_duck_atk");
    lexFig8DuckRelParam_     = parameters.getRawParameterValue ("lexfig8_duck_rel");
    lexFig8DuckDepthParam_   = parameters.getRawParameterValue ("lexfig8_duck_depth");

    mtdlFeedbackParam_  = parameters.getRawParameterValue ("mtdl_feedback");
    for (int i = 0; i < 8; ++i)
    {
        const juce::String dampId = juce::String ("mtdl_damping_hz_") + juce::String (i);
        mtdlDampingHzParam_[i] = parameters.getRawParameterValue (dampId);
        const juce::String fbId = juce::String ("mtdl_fb_") + juce::String (i);
        mtdlFeedbackAtParam_[i] = parameters.getRawParameterValue (fbId);
        const juce::String modId = juce::String ("mtdl_line_mod_ms_") + juce::String (i);
        mtdlLineModMsParam_[i] = parameters.getRawParameterValue (modId);
    }
    for (int i = 0; i < 4; ++i)
    {
        const juce::String erId = juce::String ("mtdl_er_tap_gain_db_") + juce::String (i);
        mtdlERTapGainDbParam_[i] = parameters.getRawParameterValue (erId);
    }
    mtdlERLevelParam_   = parameters.getRawParameterValue ("mtdl_er_level");
    mtdlLateLevelParam_ = parameters.getRawParameterValue ("mtdl_late_level");
    mtdlSchroederCoeffParam_ = parameters.getRawParameterValue ("mtdl_schroeder_coeff");
    mtdlTiltDbParam_         = parameters.getRawParameterValue ("mtdl_tilt_db");
    hybridWashLevelParam_    = parameters.getRawParameterValue ("hybrid_wash_level");
    hybridChatterLevelParam_ = parameters.getRawParameterValue ("hybrid_chatter_level");

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

        // Invalidate all Hall advanced cached values so the active engine
        // gets its Hall parameters force-replayed on the next process() call
        // (mirrors the FirstReflections/ER/gainTrim sentinel reset above).
        lastHallBassDamping_ = lastHallMidDamping_ = lastHallTrebleDamping_ =
        lastHallBassGain_ = lastHallMidGain_ = lastHallTrebleGain_ =
        lastHallInlineDiffusion_ = lastHallStereoWidth_ = -999.0f;
        lastHallTap0Ms_ = lastHallTap1Ms_ = lastHallTap2Ms_ =
        lastHallTap3Ms_ = lastHallTap4Ms_ = lastHallTap5Ms_ = -999.0f;
        lastHallTap0Weight_ = lastHallTap1Weight_ = lastHallTap2Weight_ =
        lastHallTap3Weight_ = lastHallTap4Weight_ = lastHallTap5Weight_ = -999.0f;
        lastHallSpec0Ms_ = lastHallSpec1Ms_ = lastHallSpec2Ms_ = lastHallSpec3Ms_ = -999.0f;
        lastHallSpec0Weight_ = lastHallSpec1Weight_ =
        lastHallSpec2Weight_ = lastHallSpec3Weight_ = -999.0f;
        lastHallSpecHFCut_ = -999.0f;
        lastHallBassEQGain_ = lastHallBassEQQ_ =
        lastHallMidEQGain_  = lastHallMidEQQ_ =
        lastHallTrebleEQGain_ = lastHallTrebleEQQ_ = -999.0f;
        lastHallBassEQFc_ = lastHallMidEQFc_ = lastHallTrebleEQFc_ = -999.0f;
        lastHallBassDampingFc_ = lastHallMidDampingFc_ = lastHallTrebleDampingFc_ = -999.0f;
        lastHallBassModDepth_ = lastHallBassModRate_ =
        lastHallMidModDepth_  = lastHallMidModRate_ =
        lastHallTrebleModDepth_ = lastHallTrebleModRate_ = -999.0f;
        lastHallBassModShape_ = lastHallMidModShape_ = lastHallTrebleModShape_ = -999.0f;
        lastHallBassChanGainSpread_ = lastHallMidChanGainSpread_ =
        lastHallTrebleChanGainSpread_ = -999.0f;
        lastHallBassShelfGain_ = lastHallBassShelfFc_ =
        lastHallMidShelfGain_  = lastHallMidShelfFc_ =
        lastHallTrebleShelfGain_ = lastHallTrebleShelfFc_ = -999.0f;
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
    pushIfChanged (lastHallBassModShape_,    hallBassModShapeParam_->load(),    [this] (float v) { activeEngine_->setHallBassModShape    (v); });
    pushIfChanged (lastHallMidModShape_,     hallMidModShapeParam_->load(),     [this] (float v) { activeEngine_->setHallMidModShape     (v); });
    pushIfChanged (lastHallTrebleModShape_,  hallTrebleModShapeParam_->load(),  [this] (float v) { activeEngine_->setHallTrebleModShape  (v); });
    pushIfChanged (lastHallBassChanGainSpread_,   hallBassChanGainSpreadParam_->load(),   [this] (float v) { activeEngine_->setHallBassChannelGainSpread   (v); });
    pushIfChanged (lastHallMidChanGainSpread_,    hallMidChanGainSpreadParam_->load(),    [this] (float v) { activeEngine_->setHallMidChannelGainSpread    (v); });
    pushIfChanged (lastHallTrebleChanGainSpread_, hallTrebleChanGainSpreadParam_->load(), [this] (float v) { activeEngine_->setHallTrebleChannelGainSpread (v); });
    pushIfChanged (lastHallBassShelfGain_,   hallBassShelfGainParam_->load(),   [this] (float v) { activeEngine_->setHallBassShelfGain   (v); });
    pushIfChanged (lastHallBassShelfFc_,     hallBassShelfFcParam_->load(),     [this] (float v) { activeEngine_->setHallBassShelfFc     (v); });
    pushIfChanged (lastHallMidShelfGain_,    hallMidShelfGainParam_->load(),    [this] (float v) { activeEngine_->setHallMidShelfGain    (v); });
    pushIfChanged (lastHallMidShelfFc_,      hallMidShelfFcParam_->load(),      [this] (float v) { activeEngine_->setHallMidShelfFc      (v); });
    pushIfChanged (lastHallTrebleShelfGain_, hallTrebleShelfGainParam_->load(), [this] (float v) { activeEngine_->setHallTrebleShelfGain (v); });
    pushIfChanged (lastHallTrebleShelfFc_,   hallTrebleShelfFcParam_->load(),   [this] (float v) { activeEngine_->setHallTrebleShelfFc   (v); });
    pushIfChanged (lastHallInputDiffusion_,  hallInputDiffusionParam_->load(),  [this] (float v) { activeEngine_->setHallInputDiffusion  (v); });
    pushIfChanged (lastRingDamping_,     ringDampingParam_->load(),     [this] (float v) { activeEngine_->setRingDamping     (v); });
    pushIfChanged (lastRingDampingFc_,   ringDampingFcParam_->load(),   [this] (float v) { activeEngine_->setRingDampingFc   (v); });
    pushIfChanged (lastRingSpread_,      ringSpreadParam_->load(),      [this] (float v) { activeEngine_->setRingSpread      (v); });
    pushIfChanged (lastRingShape_,       ringShapeParam_->load(),       [this] (float v) { activeEngine_->setRingShape       (v); });
    pushIfChanged (lastRingSpin_,        ringSpinParam_->load(),        [this] (float v) { activeEngine_->setRingSpin        (v); });
    pushIfChanged (lastRingWander_,      ringWanderParam_->load(),      [this] (float v) { activeEngine_->setRingWander      (v); });
    pushIfChanged (lastRingStereoWidth_, ringStereoWidthParam_->load(), [this] (float v) { activeEngine_->setRingStereoWidth (v); });
    pushIfChanged (lastHybridRingLevel_,   hybridRingLevelParam_->load(),   [this] (float v) { activeEngine_->setHybridRingLevel   (v); });
    pushIfChanged (lastHybridERLevel_,     hybridERLevelParam_->load(),     [this] (float v) { activeEngine_->setHybridERLevel     (v); });
    pushIfChanged (lastHybridERW1_,        hybridERW1Param_->load(),        [this] (float v) { activeEngine_->setHybridERTapWeight (1, v); });
    pushIfChanged (lastHybridERW2_,        hybridERW2Param_->load(),        [this] (float v) { activeEngine_->setHybridERTapWeight (2, v); });
    pushIfChanged (lastHybridERW3_,        hybridERW3Param_->load(),        [this] (float v) { activeEngine_->setHybridERTapWeight (3, v); });
    // Shelf gain + fc applied together via single setter; trigger on EITHER change.
    {
        const float lg = hybridLowShelfGainParam_->load();
        const float lf = hybridLowShelfFcParam_->load();
        if (lg != lastHybridLowShelfGain_ || lf != lastHybridLowShelfFc_) {
            activeEngine_->setHybridLowShelf (lg, lf);
            lastHybridLowShelfGain_ = lg;
            lastHybridLowShelfFc_ = lf;
        }
        const float hg = hybridHighShelfGainParam_->load();
        const float hf = hybridHighShelfFcParam_->load();
        if (hg != lastHybridHighShelfGain_ || hf != lastHybridHighShelfFc_) {
            activeEngine_->setHybridHighShelf (hg, hf);
            lastHybridHighShelfGain_ = hg;
            lastHybridHighShelfFc_ = hf;
        }
    }
    pushIfChanged (lastHybridRingDamping_,   hybridRingDampingParam_->load(),   [this] (float v) { activeEngine_->setHybridRingDamping   (v); });
    pushIfChanged (lastHybridRingDampingFc_, hybridRingDampingFcParam_->load(), [this] (float v) { activeEngine_->setHybridRingDampingFc (v); });
    pushIfChanged (lastHybridRingSpin_,      hybridRingSpinParam_->load(),      [this] (float v) { activeEngine_->setHybridRingSpin      (v); });
    pushIfChanged (lastHybridRingWander_,    hybridRingWanderParam_->load(),    [this] (float v) { activeEngine_->setHybridRingWander    (v); });
    pushIfChanged (lastHybridRingStereo_,    hybridRingStereoParam_->load(),    [this] (float v) { activeEngine_->setHybridRingStereoWidth (v); });

    // ─── Engine 13 (TrueLex) — 7 axes; tank knobs share Hall (Lex) APVTS ──
    pushIfChanged (lastTrueLexERLevel_,   truelexERLevelParam_->load(),   [this] (float v) { activeEngine_->setTrueLexERLevel   (v); });
    pushIfChanged (lastTrueLexTankLevel_, truelexTankLevelParam_->load(), [this] (float v) { activeEngine_->setTrueLexTankLevel (v); });
    pushIfChanged (lastTrueLexERW0_,      truelexERW0Param_->load(),      [this] (float v) { activeEngine_->setTrueLexERTapWeight (0, v); });
    pushIfChanged (lastTrueLexERW1_,      truelexERW1Param_->load(),      [this] (float v) { activeEngine_->setTrueLexERTapWeight (1, v); });
    pushIfChanged (lastTrueLexERW2_,      truelexERW2Param_->load(),      [this] (float v) { activeEngine_->setTrueLexERTapWeight (2, v); });
    pushIfChanged (lastTrueLexERW3_,      truelexERW3Param_->load(),      [this] (float v) { activeEngine_->setTrueLexERTapWeight (3, v); });
    pushIfChanged (lastTrueLexAPCoeff_,   truelexAPCoeffParam_->load(),   [this] (float v) { activeEngine_->setTrueLexAPCoeff   (v); });

    // ─── Engine 14 (TrueLex 16) — 7 axes ──────────────────────────────
    pushIfChanged (lastTrueLex16ERLevel_,   truelex16ERLevelParam_->load(),   [this] (float v) { activeEngine_->setTrueLex16ERLevel   (v); });
    pushIfChanged (lastTrueLex16TankLevel_, truelex16TankLevelParam_->load(), [this] (float v) { activeEngine_->setTrueLex16TankLevel (v); });
    pushIfChanged (lastTrueLex16ERW0_,      truelex16ERW0Param_->load(),      [this] (float v) { activeEngine_->setTrueLex16ERTapWeight (0, v); });
    pushIfChanged (lastTrueLex16ERW1_,      truelex16ERW1Param_->load(),      [this] (float v) { activeEngine_->setTrueLex16ERTapWeight (1, v); });
    pushIfChanged (lastTrueLex16ERW2_,      truelex16ERW2Param_->load(),      [this] (float v) { activeEngine_->setTrueLex16ERTapWeight (2, v); });
    pushIfChanged (lastTrueLex16ERW3_,      truelex16ERW3Param_->load(),      [this] (float v) { activeEngine_->setTrueLex16ERTapWeight (3, v); });
    pushIfChanged (lastTrueLex16ERW4_,      truelex16ERW4Param_->load(),      [this] (float v) { activeEngine_->setTrueLex16ERTapWeight (4, v); });
    pushIfChanged (lastTrueLex16APCoeff_,   truelex16APCoeffParam_->load(),   [this] (float v) { activeEngine_->setTrueLex16APCoeff   (v); });
    pushIfChanged (lastLexFig8StructHF_,    lexFig8StructHFParam_->load(),    [this] (float v) { activeEngine_->setLexFig8StructuralHFDamping (v); });
    pushIfChanged (lastLexFig8ERTap0Dly_,   lexFig8ERTap0DlyParam_->load(),   [this] (float v) { activeEngine_->setLexFig8ERTapDelay (0, v); });
    pushIfChanged (lastLexFig8ERTap1Dly_,   lexFig8ERTap1DlyParam_->load(),   [this] (float v) { activeEngine_->setLexFig8ERTapDelay (1, v); });
    pushIfChanged (lastLexFig8ERTap2Dly_,   lexFig8ERTap2DlyParam_->load(),   [this] (float v) { activeEngine_->setLexFig8ERTapDelay (2, v); });
    pushIfChanged (lastLexFig8ERTap3Dly_,   lexFig8ERTap3DlyParam_->load(),   [this] (float v) { activeEngine_->setLexFig8ERTapDelay (3, v); });
    pushIfChanged (lastLexFig8ERTap0Gain_,  lexFig8ERTap0GainParam_->load(),  [this] (float v) { activeEngine_->setLexFig8ERTapGainDb (0, v); });
    pushIfChanged (lastLexFig8ERTap1Gain_,  lexFig8ERTap1GainParam_->load(),  [this] (float v) { activeEngine_->setLexFig8ERTapGainDb (1, v); });
    pushIfChanged (lastLexFig8ERTap2Gain_,  lexFig8ERTap2GainParam_->load(),  [this] (float v) { activeEngine_->setLexFig8ERTapGainDb (2, v); });
    pushIfChanged (lastLexFig8ERTap3Gain_,  lexFig8ERTap3GainParam_->load(),  [this] (float v) { activeEngine_->setLexFig8ERTapGainDb (3, v); });
    pushIfChanged (lastLexFig8ERStereoOffset_, lexFig8ERStereoOffsetParam_->load(), [this] (float v) { activeEngine_->setLexFig8ERStereoOffset (v); });
    pushIfChanged (lastLexFig8TankAtten_,   lexFig8TankAttenParam_->load(),   [this] (float v) { activeEngine_->setLexFig8TankAtten (v); });
    pushIfChanged (lastLexFig8TankIn_,      lexFig8TankInParam_->load(),      [this] (float v) { activeEngine_->setLexFig8TankInputScale (v); });
    pushIfChanged (lastLexFig8TankPDly_,    lexFig8TankPDlyParam_->load(),    [this] (float v) { activeEngine_->setLexFig8TankPreDelay (v); });
    pushIfChanged (lastLexFig8DensityJitter_, lexFig8DensityJitterParam_->load(), [this] (float v) { activeEngine_->setLexFig8DensityJitterDepth (v); });
    pushIfChanged (lastLexFig8DensityRate_,   lexFig8DensityRateParam_->load(),   [this] (float v) { activeEngine_->setLexFig8DensityJitterRate  (v); });
    pushIfChanged (lastLexFig8SubBassMult_,   lexFig8SubBassMultParam_->load(),   [this] (float v) { activeEngine_->setLexFig8SubBassMultiply (v); });
    pushIfChanged (lastLexFig8SubBassXover_,  lexFig8SubBassXoverParam_->load(),  [this] (float v) { activeEngine_->setLexFig8SubBassCrossover (v); });
    pushIfChanged (lastLexFig8Tilt_,          lexFig8TiltParam_->load(),          [this] (float v) { activeEngine_->setLexFig8StructuralTilt (v); });
    pushIfChanged (lastLexFig8AirMult_,       lexFig8AirMultParam_->load(),       [this] (float v) { activeEngine_->setLexFig8AirMultiply  (v); });
    pushIfChanged (lastLexFig8AirXover_,      lexFig8AirXoverParam_->load(),      [this] (float v) { activeEngine_->setLexFig8AirCrossover (v); });
    for (int i = 0; i < 8; ++i)
    {
        const int bandIdx = i;
        pushIfChanged (lastLexFig8BandMult_[i], lexFig8BandMultParam_[i]->load(),
                       [this, bandIdx] (float v) { activeEngine_->setLexFig8BandMultiply (bandIdx, v); });
    }
    for (int i = 0; i < 4; ++i)
    {
        const int stageIdx = i;
        pushIfChanged (lastLexFig8DapDelay_[i], lexFig8DapDelayParam_[i]->load(),
                       [this, stageIdx] (float v) { activeEngine_->setLexFig8DensityAPDelayMs (stageIdx, v); });
    }
    for (int i = 0; i < 4; ++i)
    {
        const int tapIdx = i;
        pushIfChanged (lastLexFig8OTapL_[i], lexFig8OTapLParam_[i]->load(),
                       [this, tapIdx] (float v) { activeEngine_->setLexFig8OutputTapFraction (0, tapIdx, v); });
        pushIfChanged (lastLexFig8OTapR_[i], lexFig8OTapRParam_[i]->load(),
                       [this, tapIdx] (float v) { activeEngine_->setLexFig8OutputTapFraction (1, tapIdx, v); });
    }
    pushIfChanged (lastLexFig8Del1L_, lexFig8Del1LParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8DelayBaseMs (0, 0, v); });
    pushIfChanged (lastLexFig8Del1R_, lexFig8Del1RParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8DelayBaseMs (1, 0, v); });
    pushIfChanged (lastLexFig8Del2L_, lexFig8Del2LParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8DelayBaseMs (0, 1, v); });
    pushIfChanged (lastLexFig8Del2R_, lexFig8Del2RParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8DelayBaseMs (1, 1, v); });
    pushIfChanged (lastLexFig8AP1L_, lexFig8AP1LParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8APBaseMs (0, 0, v); });
    pushIfChanged (lastLexFig8AP1R_, lexFig8AP1RParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8APBaseMs (1, 0, v); });
    pushIfChanged (lastLexFig8AP2L_, lexFig8AP2LParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8APBaseMs (0, 1, v); });
    pushIfChanged (lastLexFig8AP2R_, lexFig8AP2RParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8APBaseMs (1, 1, v); });
    pushIfChanged (lastLexFig8XFdL_, lexFig8XFdLParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8CrossFeedCoeff (0, v); });
    pushIfChanged (lastLexFig8XFdR_, lexFig8XFdRParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8CrossFeedCoeff (1, v); });
    pushIfChanged (lastLexFig8BypassDiff_, lexFig8BypassDiffParam_->load(),
                   [this] (float v) { activeEngine_->setLexFig8BypassDiffuser (v >= 0.5f); });
    pushIfChanged (lastLexFig8DuckThresh_, lexFig8DuckThreshParam_->load(), [this] (float v) { activeEngine_->setLexFig8DuckerThreshold (v); });
    pushIfChanged (lastLexFig8DuckAtk_,    lexFig8DuckAtkParam_->load(),    [this] (float v) { activeEngine_->setLexFig8DuckerAttackMs  (v); });
    pushIfChanged (lastLexFig8DuckRel_,    lexFig8DuckRelParam_->load(),    [this] (float v) { activeEngine_->setLexFig8DuckerReleaseMs (v); });
    pushIfChanged (lastLexFig8DuckDepth_,  lexFig8DuckDepthParam_->load(),  [this] (float v) { activeEngine_->setLexFig8DuckerDepth     (v); });

    pushIfChanged (lastMTDLFeedback_,  mtdlFeedbackParam_->load(),  [this] (float v) { activeEngine_->setLexMTDLFeedbackScale (v); });
    for (int i = 0; i < 8; ++i)
    {
        const int idx = i;
        pushIfChanged (lastMTDLDampingHz_[i], mtdlDampingHzParam_[i]->load(),
                       [this, idx] (float v) { activeEngine_->setLexMTDLDampingHzAt (idx, v); });
        pushIfChanged (lastMTDLFeedbackAt_[i], mtdlFeedbackAtParam_[i]->load(),
                       [this, idx] (float v) { activeEngine_->setLexMTDLFeedbackAt (idx, v); });
        pushIfChanged (lastMTDLLineModMs_[i], mtdlLineModMsParam_[i]->load(),
                       [this, idx] (float v) { activeEngine_->setLexMTDLLineModDepthMsAt (idx, v); });
    }
    for (int i = 0; i < 4; ++i)
    {
        const int idx = i;
        pushIfChanged (lastMTDLERTapGainDb_[i], mtdlERTapGainDbParam_[i]->load(),
                       [this, idx] (float v) { activeEngine_->setLexMTDLERTapGainDbAt (idx, v); });
    }
    pushIfChanged (lastMTDLERLevel_,   mtdlERLevelParam_->load(),   [this] (float v) { activeEngine_->setLexMTDLERLevel       (v); });
    pushIfChanged (lastMTDLLateLevel_, mtdlLateLevelParam_->load(), [this] (float v) { activeEngine_->setLexMTDLLateLevel     (v); });
    pushIfChanged (lastMTDLSchroederCoeff_, mtdlSchroederCoeffParam_->load(),
                   [this] (float v) { activeEngine_->setLexMTDLSchroederCoeff (v); });
    pushIfChanged (lastMTDLTiltDb_, mtdlTiltDbParam_->load(),
                   [this] (float v) { activeEngine_->setLexMTDLTiltDb (v); });
    pushIfChanged (lastHybridWashLevel_,    hybridWashLevelParam_->load(),
                   [this] (float v) { activeEngine_->setLexHybridWashLevel    (v); });
    pushIfChanged (lastHybridChatterLevel_, hybridChatterLevelParam_->load(),
                   [this] (float v) { activeEngine_->setLexHybridChatterLevel (v); });

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
    target->setHallBassModShape    (hallBassModShapeParam_->load());
    target->setHallMidModShape     (hallMidModShapeParam_->load());
    target->setHallTrebleModShape  (hallTrebleModShapeParam_->load());
    target->setHallBassChannelGainSpread   (hallBassChanGainSpreadParam_->load());
    target->setHallMidChannelGainSpread    (hallMidChanGainSpreadParam_->load());
    target->setHallTrebleChannelGainSpread (hallTrebleChanGainSpreadParam_->load());
    target->setHallBassShelfGain   (hallBassShelfGainParam_->load());
    target->setHallBassShelfFc     (hallBassShelfFcParam_->load());
    target->setHallMidShelfGain    (hallMidShelfGainParam_->load());
    target->setHallMidShelfFc      (hallMidShelfFcParam_->load());
    target->setHallTrebleShelfGain (hallTrebleShelfGainParam_->load());
    target->setHallTrebleShelfFc   (hallTrebleShelfFcParam_->load());
    target->setHallInputDiffusion  (hallInputDiffusionParam_->load());
    target->setRingDamping     (ringDampingParam_->load());
    target->setRingDampingFc   (ringDampingFcParam_->load());
    target->setRingSpread      (ringSpreadParam_->load());
    target->setRingShape       (ringShapeParam_->load());
    target->setRingSpin        (ringSpinParam_->load());
    target->setRingWander      (ringWanderParam_->load());
    target->setRingStereoWidth (ringStereoWidthParam_->load());
    target->setHybridRingLevel   (hybridRingLevelParam_->load());
    target->setHybridERLevel     (hybridERLevelParam_->load());
    target->setHybridERTapWeight (1, hybridERW1Param_->load());
    target->setHybridERTapWeight (2, hybridERW2Param_->load());
    target->setHybridERTapWeight (3, hybridERW3Param_->load());
    target->setHybridLowShelf  (hybridLowShelfGainParam_->load(),  hybridLowShelfFcParam_->load());
    target->setHybridHighShelf (hybridHighShelfGainParam_->load(), hybridHighShelfFcParam_->load());
    target->setHybridRingDamping     (hybridRingDampingParam_->load());
    target->setHybridRingDampingFc   (hybridRingDampingFcParam_->load());
    target->setHybridRingSpin        (hybridRingSpinParam_->load());
    target->setHybridRingWander      (hybridRingWanderParam_->load());
    target->setHybridRingStereoWidth (hybridRingStereoParam_->load());

    target->setTrueLexERLevel      (truelexERLevelParam_->load());
    target->setTrueLexTankLevel    (truelexTankLevelParam_->load());
    target->setTrueLexERTapWeight  (0, truelexERW0Param_->load());
    target->setTrueLexERTapWeight  (1, truelexERW1Param_->load());
    target->setTrueLexERTapWeight  (2, truelexERW2Param_->load());
    target->setTrueLexERTapWeight  (3, truelexERW3Param_->load());
    target->setTrueLexAPCoeff      (truelexAPCoeffParam_->load());

    target->setTrueLex16ERLevel      (truelex16ERLevelParam_->load());
    target->setTrueLex16TankLevel    (truelex16TankLevelParam_->load());
    target->setTrueLex16ERTapWeight  (0, truelex16ERW0Param_->load());
    target->setTrueLex16ERTapWeight  (1, truelex16ERW1Param_->load());
    target->setTrueLex16ERTapWeight  (2, truelex16ERW2Param_->load());
    target->setTrueLex16ERTapWeight  (3, truelex16ERW3Param_->load());
    target->setTrueLex16ERTapWeight  (4, truelex16ERW4Param_->load());
    target->setTrueLex16APCoeff      (truelex16APCoeffParam_->load());
    target->setLexFig8StructuralHFDamping (lexFig8StructHFParam_->load());
    target->setLexFig8ERTapDelay     (0, lexFig8ERTap0DlyParam_->load());
    target->setLexFig8ERTapDelay     (1, lexFig8ERTap1DlyParam_->load());
    target->setLexFig8ERTapDelay     (2, lexFig8ERTap2DlyParam_->load());
    target->setLexFig8ERTapDelay     (3, lexFig8ERTap3DlyParam_->load());
    target->setLexFig8ERTapGainDb    (0, lexFig8ERTap0GainParam_->load());
    target->setLexFig8ERTapGainDb    (1, lexFig8ERTap1GainParam_->load());
    target->setLexFig8ERTapGainDb    (2, lexFig8ERTap2GainParam_->load());
    target->setLexFig8ERTapGainDb    (3, lexFig8ERTap3GainParam_->load());
    target->setLexFig8ERStereoOffset (lexFig8ERStereoOffsetParam_->load());
    target->setLexFig8TankAtten      (lexFig8TankAttenParam_->load());
    target->setLexFig8TankInputScale (lexFig8TankInParam_->load());
    target->setLexFig8TankPreDelay   (lexFig8TankPDlyParam_->load());
    target->setLexFig8DensityJitterDepth (lexFig8DensityJitterParam_->load());
    target->setLexFig8DensityJitterRate  (lexFig8DensityRateParam_->load());
    target->setLexFig8SubBassMultiply  (lexFig8SubBassMultParam_->load());
    target->setLexFig8SubBassCrossover (lexFig8SubBassXoverParam_->load());
    target->setLexFig8StructuralTilt   (lexFig8TiltParam_->load());
    target->setLexFig8AirMultiply      (lexFig8AirMultParam_->load());
    target->setLexFig8AirCrossover     (lexFig8AirXoverParam_->load());
    for (int i = 0; i < 8; ++i)
        target->setLexFig8BandMultiply (i, lexFig8BandMultParam_[i]->load());
    for (int i = 0; i < 4; ++i)
        target->setLexFig8DensityAPDelayMs (i, lexFig8DapDelayParam_[i]->load());
    for (int i = 0; i < 4; ++i)
    {
        target->setLexFig8OutputTapFraction (0, i, lexFig8OTapLParam_[i]->load());
        target->setLexFig8OutputTapFraction (1, i, lexFig8OTapRParam_[i]->load());
    }
    target->setLexFig8DelayBaseMs (0, 0, lexFig8Del1LParam_->load());
    target->setLexFig8DelayBaseMs (1, 0, lexFig8Del1RParam_->load());
    target->setLexFig8DelayBaseMs (0, 1, lexFig8Del2LParam_->load());
    target->setLexFig8DelayBaseMs (1, 1, lexFig8Del2RParam_->load());
    target->setLexFig8APBaseMs (0, 0, lexFig8AP1LParam_->load());
    target->setLexFig8APBaseMs (1, 0, lexFig8AP1RParam_->load());
    target->setLexFig8APBaseMs (0, 1, lexFig8AP2LParam_->load());
    target->setLexFig8APBaseMs (1, 1, lexFig8AP2RParam_->load());
    target->setLexFig8CrossFeedCoeff (0, lexFig8XFdLParam_->load());
    target->setLexFig8CrossFeedCoeff (1, lexFig8XFdRParam_->load());
    target->setLexFig8BypassDiffuser (lexFig8BypassDiffParam_->load() >= 0.5f);
    target->setLexFig8DuckerThreshold  (lexFig8DuckThreshParam_->load());
    target->setLexFig8DuckerAttackMs   (lexFig8DuckAtkParam_->load());
    target->setLexFig8DuckerReleaseMs  (lexFig8DuckRelParam_->load());
    target->setLexFig8DuckerDepth      (lexFig8DuckDepthParam_->load());

    target->setLexMTDLFeedbackScale (mtdlFeedbackParam_->load());
    for (int i = 0; i < 8; ++i)
    {
        target->setLexMTDLDampingHzAt (i, mtdlDampingHzParam_[i]->load());
        target->setLexMTDLFeedbackAt  (i, mtdlFeedbackAtParam_[i]->load());
        target->setLexMTDLLineModDepthMsAt (i, mtdlLineModMsParam_[i]->load());
    }
    for (int i = 0; i < 4; ++i)
        target->setLexMTDLERTapGainDbAt (i, mtdlERTapGainDbParam_[i]->load());
    target->setLexMTDLERLevel       (mtdlERLevelParam_->load());
    target->setLexMTDLLateLevel     (mtdlLateLevelParam_->load());
    target->setLexMTDLSchroederCoeff (mtdlSchroederCoeffParam_->load());
    target->setLexMTDLTiltDb         (mtdlTiltDbParam_->load());
    target->setLexHybridWashLevel    (hybridWashLevelParam_->load());
    target->setLexHybridChatterLevel (hybridChatterLevelParam_->load());

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
    lastHallBassModShape_    = hallBassModShapeParam_->load();
    lastHallMidModShape_     = hallMidModShapeParam_->load();
    lastHallTrebleModShape_  = hallTrebleModShapeParam_->load();
    lastHallBassChanGainSpread_   = hallBassChanGainSpreadParam_->load();
    lastHallMidChanGainSpread_    = hallMidChanGainSpreadParam_->load();
    lastHallTrebleChanGainSpread_ = hallTrebleChanGainSpreadParam_->load();
    lastHallBassShelfGain_   = hallBassShelfGainParam_->load();
    lastHallBassShelfFc_     = hallBassShelfFcParam_->load();
    lastHallMidShelfGain_    = hallMidShelfGainParam_->load();
    lastHallMidShelfFc_      = hallMidShelfFcParam_->load();
    lastHallTrebleShelfGain_ = hallTrebleShelfGainParam_->load();
    lastHallTrebleShelfFc_   = hallTrebleShelfFcParam_->load();
    lastHallInputDiffusion_  = hallInputDiffusionParam_->load();
    lastRingDamping_     = ringDampingParam_->load();
    lastRingDampingFc_   = ringDampingFcParam_->load();
    lastRingSpread_      = ringSpreadParam_->load();
    lastRingShape_       = ringShapeParam_->load();
    lastRingSpin_        = ringSpinParam_->load();
    lastRingWander_      = ringWanderParam_->load();
    lastRingStereoWidth_ = ringStereoWidthParam_->load();
    lastHybridRingLevel_     = hybridRingLevelParam_->load();
    lastHybridERLevel_       = hybridERLevelParam_->load();
    lastHybridERW1_          = hybridERW1Param_->load();
    lastHybridERW2_          = hybridERW2Param_->load();
    lastHybridERW3_          = hybridERW3Param_->load();
    lastHybridLowShelfGain_  = hybridLowShelfGainParam_->load();
    lastHybridLowShelfFc_    = hybridLowShelfFcParam_->load();
    lastHybridHighShelfGain_ = hybridHighShelfGainParam_->load();
    lastHybridHighShelfFc_   = hybridHighShelfFcParam_->load();
    lastHybridRingDamping_   = hybridRingDampingParam_->load();
    lastHybridRingDampingFc_ = hybridRingDampingFcParam_->load();
    lastHybridRingSpin_      = hybridRingSpinParam_->load();
    lastHybridRingWander_    = hybridRingWanderParam_->load();
    lastHybridRingStereo_    = hybridRingStereoParam_->load();
    lastTrueLexERLevel_      = truelexERLevelParam_->load();
    lastTrueLexTankLevel_    = truelexTankLevelParam_->load();
    lastTrueLexERW0_         = truelexERW0Param_->load();
    lastTrueLexERW1_         = truelexERW1Param_->load();
    lastTrueLexERW2_         = truelexERW2Param_->load();
    lastTrueLexERW3_         = truelexERW3Param_->load();
    lastTrueLexAPCoeff_      = truelexAPCoeffParam_->load();
    lastTrueLex16ERLevel_    = truelex16ERLevelParam_->load();
    lastTrueLex16TankLevel_  = truelex16TankLevelParam_->load();
    lastTrueLex16ERW0_       = truelex16ERW0Param_->load();
    lastTrueLex16ERW1_       = truelex16ERW1Param_->load();
    lastTrueLex16ERW2_       = truelex16ERW2Param_->load();
    lastTrueLex16ERW3_       = truelex16ERW3Param_->load();
    lastTrueLex16ERW4_       = truelex16ERW4Param_->load();
    lastTrueLex16APCoeff_    = truelex16APCoeffParam_->load();
    lastLexFig8StructHF_     = lexFig8StructHFParam_->load();
    lastLexFig8ERTap0Dly_    = lexFig8ERTap0DlyParam_->load();
    lastLexFig8ERTap1Dly_    = lexFig8ERTap1DlyParam_->load();
    lastLexFig8ERTap2Dly_    = lexFig8ERTap2DlyParam_->load();
    lastLexFig8ERTap3Dly_    = lexFig8ERTap3DlyParam_->load();
    lastLexFig8ERTap0Gain_   = lexFig8ERTap0GainParam_->load();
    lastLexFig8ERTap1Gain_   = lexFig8ERTap1GainParam_->load();
    lastLexFig8ERTap2Gain_   = lexFig8ERTap2GainParam_->load();
    lastLexFig8ERTap3Gain_   = lexFig8ERTap3GainParam_->load();
    lastLexFig8ERStereoOffset_ = lexFig8ERStereoOffsetParam_->load();
    lastLexFig8TankAtten_    = lexFig8TankAttenParam_->load();
    lastLexFig8TankIn_       = lexFig8TankInParam_->load();
    lastLexFig8TankPDly_     = lexFig8TankPDlyParam_->load();
    lastLexFig8DensityJitter_ = lexFig8DensityJitterParam_->load();
    lastLexFig8DensityRate_   = lexFig8DensityRateParam_->load();
    lastLexFig8SubBassMult_   = lexFig8SubBassMultParam_->load();
    lastLexFig8SubBassXover_  = lexFig8SubBassXoverParam_->load();
    lastLexFig8Tilt_          = lexFig8TiltParam_->load();
    lastLexFig8AirMult_       = lexFig8AirMultParam_->load();
    lastLexFig8AirXover_      = lexFig8AirXoverParam_->load();
    for (int i = 0; i < 8; ++i)
        lastLexFig8BandMult_[i] = lexFig8BandMultParam_[i]->load();
    for (int i = 0; i < 4; ++i)
        lastLexFig8DapDelay_[i] = lexFig8DapDelayParam_[i]->load();
    for (int i = 0; i < 4; ++i)
    {
        lastLexFig8OTapL_[i] = lexFig8OTapLParam_[i]->load();
        lastLexFig8OTapR_[i] = lexFig8OTapRParam_[i]->load();
    }
    lastLexFig8Del1L_ = lexFig8Del1LParam_->load();
    lastLexFig8Del1R_ = lexFig8Del1RParam_->load();
    lastLexFig8Del2L_ = lexFig8Del2LParam_->load();
    lastLexFig8Del2R_ = lexFig8Del2RParam_->load();
    lastLexFig8AP1L_  = lexFig8AP1LParam_->load();
    lastLexFig8AP1R_  = lexFig8AP1RParam_->load();
    lastLexFig8AP2L_  = lexFig8AP2LParam_->load();
    lastLexFig8AP2R_  = lexFig8AP2RParam_->load();
    lastLexFig8XFdL_  = lexFig8XFdLParam_->load();
    lastLexFig8XFdR_  = lexFig8XFdRParam_->load();
    lastLexFig8BypassDiff_ = lexFig8BypassDiffParam_->load();
    lastLexFig8DuckThresh_ = lexFig8DuckThreshParam_->load();
    lastLexFig8DuckAtk_    = lexFig8DuckAtkParam_->load();
    lastLexFig8DuckRel_    = lexFig8DuckRelParam_->load();
    lastLexFig8DuckDepth_  = lexFig8DuckDepthParam_->load();

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

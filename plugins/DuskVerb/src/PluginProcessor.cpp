#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "../core/DuskVerbPresetEngineConfig.h"

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
        juce::ParameterID { "tail_spin_depth", 1 }, "Tail Spin Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), fp0.tailSpinDepth));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tail_spin_rate", 1 }, "Tail Spin Rate",
        juce::NormalisableRange<float> (0.10f, 10.0f, 0.0f, 0.5f), fp0.tailSpinRate));

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

    // Low-Band Transient Shaper (FDN only). Depth 0 = bypass (Phase A default).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "transient_shaper", 1 }, "Transient Shaper",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "shaper_time", 1 }, "Shaper Time",
        juce::NormalisableRange<float> (20.0f, 300.0f, 0.0f, 0.5f), 120.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "shaper_xover", 1 }, "Shaper Xover",
        juce::NormalisableRange<float> (120.0f, 500.0f, 0.0f, 0.5f), 250.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "shaper_sens", 1 }, "Shaper Sens",
        juce::NormalisableRange<float> (0.5f, 4.0f), 1.5f));

    // Block 2: feed-forward input energy makeup (FDN only). 0 dB = bypass.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input_sub_gain", 1 }, "Input Sub Gain",
        juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input_mid_gain", 1 }, "Input Mid Gain",
        juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input_high_gain", 1 }, "Input High Gain",
        juce::NormalisableRange<float> (-6.0f, 6.0f), 0.0f));

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

    // Phase 4 (option 2): early-field ER boost. Default 1.0 → ×1.0 exact →
    // bit-identical for every existing preset/state (old states without this
    // param fall back to the 1.0 default). >1 lets the parallel ER own the
    // 0-26 ms attack the FDN tank can't reach. Per-preset via kERBoostByName.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_boost", 1 }, "Early Ref Boost",
        juce::NormalisableRange<float> (1.0f, 8.0f), 1.0f));

    // QuadTank 5-band damping split (hi-mid 4-8k / air >8k). -1 = inherit the
    // legacy treble rate → bit-identical 3-band response (every preset except
    // those listed in kQuadBandByName). Lets QuadTank presets shorten the 8 kHz
    // and 16 kHz tails independently — the FDN-only FiveBand path doesn't cover
    // QuadTank. Per-preset via kQuadBandByName.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "qt_himid_mult", 1 }, "QT Hi-Mid Multiply",
        juce::NormalisableRange<float> (-1.0f, 2.0f), -1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "qt_air_mult", 1 }, "QT Air Multiply",
        juce::NormalisableRange<float> (-1.0f, 2.0f), -1.0f));

    // Phase 4 (option 2): rising-onset ER envelope peak time. 0 = legacy
    // first-tap rolloff → bit-identical. >0 swells the ER to a peak at this ms,
    // matching VVV's gentle attack instead of an instantaneous spike.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_rise", 1 }, "Early Ref Rise",
        juce::NormalisableRange<float> (0.0f, 40.0f), 0.0f));

    // Energy-arrival campaign (2026-06-08): ER-bus spectral correction shelves.
    // The parallel ER is a 500 Hz-2 kHz midrange bump; boosting it to front-
    // load energy dumps a mid hump and adds no low/HF. These two shelves (low
    // ~400 Hz, high ~3 kHz) flatten the ER bus to the tank's full spectrum so a
    // boosted ER front-loads CLEANLY. 0 dB → unity → ER bus bit-identical →
    // every non-opting preset byte-for-byte unchanged. Per-preset via kERBusEQByName.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_bus_low_gain", 1 }, "ER Bus Low Gain",
        juce::NormalisableRange<float> (-12.0f, 18.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_bus_high_gain", 1 }, "ER Bus High Gain",
        juce::NormalisableRange<float> (-12.0f, 18.0f), 0.0f));

    // Tank-output level (energy-arrival rebalance). 1.0 → bit-identical. <1
    // lowers the late FDN tank so a raised ER front-loads at constant total
    // energy (RT60 decay rate unchanged). Per-preset via kTankLevelByName.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tank_level", 1 }, "Tank Level",
        juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));

    // Tank-level crossover (Phase 3). 0 = broadband (bit-identical). >0 = low
    // band (below this Hz) stays unity, mid/high scaled by Tank Level. Keeps the
    // correlated low while front-loading the mid/high bloom. Per-preset.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tank_split_hz", 1 }, "Tank Split Hz",
        juce::NormalisableRange<float> (0.0f, 1000.0f), 0.0f));

    // ER stereo-neutral mode (Phase 2). 0 = legacy opposed-sign R taps → bit-
    // identical. 1 = independent R taps → uniform ~0 L/R corr (VVV-like), no
    // anti-phase low. Needed so the front-load tank-rebalance keeps a clean
    // stereo image. Per-preset via kERStereoNeutralByName.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_stereo_neutral", 1 }, "ER Stereo Neutral",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // ER decorrelation allpass depth (Phase 2). 0 = bypassed → bit-identical.
    // Different prime delays per channel push L/R correlation toward 0 (VVV's
    // uniform-neutral image) without the anti-phase of sign-opposition.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "er_decorr", 1 }, "ER Decorr",
        juce::NormalisableRange<float> (0.0f, 0.7f), 0.0f));

    // Phase 4 (Change 2): output HF cross-talk decorrelation depth. 0 = no
    // cross-feed → bit-identical. Dedicated param (not coupled to Width) so the
    // whole fleet stays bit-exact; per-preset via kXTalkByName.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "xtalk", 1 }, "HF Cross-Talk",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // Phase 5 (T60 fix): parallel-multiband FDN. mb_enable 0 = single legacy
    // tank = bit-identical. The 3 per-band decays (<=0 = inherit full-band) let
    // a preset set Low/Mid/High RT60 independently once enabled.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "mb_enable", 1 }, "Multiband", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mb_low_decay", 1 }, "MB Low Decay",
        juce::NormalisableRange<float> (0.0f, 12.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mb_mid_decay", 1 }, "MB Mid Decay",
        juce::NormalisableRange<float> (0.0f, 12.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mb_high_decay", 1 }, "MB High Decay",
        juce::NormalisableRange<float> (0.0f, 12.0f), 0.0f));

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

    // Partial mono-below blend. 1.0 = full mono (legacy, bit-identical); <1
    // leaves the lows partially decorrelated (VVV lows ~-0.03 corr, not mono).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mono_below_depth", 1 }, "Mono Below Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

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

    // Wet ducking depth (ChromaVerb-style). Sidechained off the dry input;
    // pushes the tail down on transients so the source stays clear, then the
    // tail swells back as the dry decays. 0 = off (default, bit-identical).
    // Appended last so existing host-stored parameter indices are unaffected.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "duck", 1 }, "Duck",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // ── Macro / morph layer (ChromaVerb-style global shapers) ──────────────
    // Layer on TOP of each preset's baked values (effective = base x macro), so
    // one knob reshapes every space. Defaults are no-ops -> existing presets
    // render bit-identical. Appended last (host param indices stable).
    //   Tone:      spectral tilt, -1 dark .. +1 bright (Treble Mult + HiCut).
    //   Character: movement/grit, 0 .. 1 (Mod Depth + Saturation).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tone", 1 }, "Tone",
        juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "character", 1 }, "Character",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // Jot tonal correction (AccurateHall algo 10): flatten per-band steady-state
    // energy so decay no longer changes tone. Default OFF = bit-identical.
    // Appended last so existing host parameter indices are unaffected.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "tonal_correction", 1 }, "Tonal Correction", false));

    return layout;
}

DuskVerbProcessor::DuskVerbProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("DuskVerb"), createParameterLayout())
{
    // Prime the tuning-env cache on the message thread (this ctor) so the
    // one-time std::getenv() reads never land on the audio thread, where
    // applyEngineConfig() later consumes the cached pointers.
    duskverb::primeTuningEnvCache();

    algorithmParam_     = parameters.getRawParameterValue ("algorithm");
    mixParam_           = parameters.getRawParameterValue ("mix");
    busModeParam_       = parameters.getRawParameterValue ("bus_mode");
    preDelayParam_      = parameters.getRawParameterValue ("predelay");
    preDelaySyncParam_  = parameters.getRawParameterValue ("predelay_sync");
    decayParam_         = parameters.getRawParameterValue ("decay");
    sizeParam_          = parameters.getRawParameterValue ("size");
    modDepthParam_      = parameters.getRawParameterValue ("mod_depth");
    modRateParam_       = parameters.getRawParameterValue ("mod_rate");
    tailSpinDepthParam_ = parameters.getRawParameterValue ("tail_spin_depth");
    tailSpinRateParam_  = parameters.getRawParameterValue ("tail_spin_rate");
    dampingParam_       = parameters.getRawParameterValue ("damping");
    bassMultParam_      = parameters.getRawParameterValue ("bass_mult");
    midMultParam_       = parameters.getRawParameterValue ("mid_mult");
    subMultParam_       = parameters.getRawParameterValue ("sub_mult");
    hiMidMultParam_     = parameters.getRawParameterValue ("hi_mid_mult");
    crossoverSubParam_  = parameters.getRawParameterValue ("crossover_sub");
    crossoverAirParam_  = parameters.getRawParameterValue ("crossover_air");
    shaperDepthParam_   = parameters.getRawParameterValue ("transient_shaper");
    shaperTimeParam_    = parameters.getRawParameterValue ("shaper_time");
    shaperXoverParam_   = parameters.getRawParameterValue ("shaper_xover");
    shaperSensParam_    = parameters.getRawParameterValue ("shaper_sens");
    inputSubGainParam_  = parameters.getRawParameterValue ("input_sub_gain");
    inputMidGainParam_  = parameters.getRawParameterValue ("input_mid_gain");
    inputHighGainParam_ = parameters.getRawParameterValue ("input_high_gain");
    crossoverParam_     = parameters.getRawParameterValue ("crossover");
    highCrossoverParam_ = parameters.getRawParameterValue ("high_crossover");
    bassChokeParam_     = parameters.getRawParameterValue ("bass_choke");
    saturationParam_    = parameters.getRawParameterValue ("saturation");
    diffusionParam_     = parameters.getRawParameterValue ("diffusion");
    erLevelParam_       = parameters.getRawParameterValue ("er_level");
    erSizeParam_        = parameters.getRawParameterValue ("er_size");
    erBoostParam_       = parameters.getRawParameterValue ("er_boost");
    qtHiMidMultParam_   = parameters.getRawParameterValue ("qt_himid_mult");
    qtAirMultParam_     = parameters.getRawParameterValue ("qt_air_mult");
    erRiseParam_        = parameters.getRawParameterValue ("er_rise");
    erBusLowGainParam_  = parameters.getRawParameterValue ("er_bus_low_gain");
    erBusHighGainParam_ = parameters.getRawParameterValue ("er_bus_high_gain");
    tankLevelParam_     = parameters.getRawParameterValue ("tank_level");
    tankSplitHzParam_   = parameters.getRawParameterValue ("tank_split_hz");
    erStereoNeutralParam_ = parameters.getRawParameterValue ("er_stereo_neutral");
    erDecorrParam_      = parameters.getRawParameterValue ("er_decorr");
    xtalkParam_         = parameters.getRawParameterValue ("xtalk");
    mbEnableParam_      = parameters.getRawParameterValue ("mb_enable");
    mbLowDecayParam_    = parameters.getRawParameterValue ("mb_low_decay");
    mbMidDecayParam_    = parameters.getRawParameterValue ("mb_mid_decay");
    mbHighDecayParam_   = parameters.getRawParameterValue ("mb_high_decay");
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
    monoBelowDepthParam_= parameters.getRawParameterValue ("mono_below_depth");
    duckParam_          = parameters.getRawParameterValue ("duck");
    tonalCorrParam_     = parameters.getRawParameterValue ("tonal_correction");
    toneParam_          = parameters.getRawParameterValue ("tone");
    characterParam_     = parameters.getRawParameterValue ("character");

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

        ducker_.prepare (sampleRate);

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
        lastMix_ = lastLoCut_ = lastHiCut_ = lastWidth_ = lastMonoBelow_ = lastMonoBelowDepth_ =
        lastTonalCorr_ = -1.0f;
        lastERLevel_ = -2.0f;
        lastERBoost_ = -1.0f;
        lastERRise_  = -1.0f;
        lastERBusLow_ = lastERBusHigh_ = -99.0f;
        lastTankLevel_ = -1.0f;
        lastTankSplitHz_ = -1.0f;
        lastERStereoNeutral_ = -1.0f;
        lastERDecorr_ = -1.0f;
        lastXTalk_   = -1.0f;
        lastMbEnable_ = false;
        lastMbLow_ = lastMbMid_ = lastMbHigh_ = -1.0f;
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

    // ── Macro / morph layer ── folded into the base param values below so they
    // layer on every space. Tone = spectral tilt (treble x2^tone, hicut shift);
    // Character = movement/grit (additive mod depth + saturation). Defaults
    // (tone 0, character 0) -> factor 1.0 / +0.0 -> effective == base -> bit-null.
    const float tone = toneParam_ ? toneParam_->load() : 0.0f;
    const float chr  = characterParam_ ? characterParam_->load() : 0.0f;
    const float toneTreble = std::pow (2.0f, tone);         // -1 -> 0.5x, +1 -> 2x
    const float toneHiCut  = std::pow (2.0f, tone * 0.6f);  // shift hicut with tone

    pushIfChanged (lastDecaySec_,  decayParam_->load(),     [this] (float v) { activeEngine_->setDecayTime (v); });
    pushIfChanged (lastSize_,      sizeParam_->load(),      [this] (float v) { activeEngine_->setSize (v); });
    pushIfChanged (lastDamping_,   dampingParam_->load() * toneTreble, [this] (float v) {
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
    pushIfChanged (lastShaperDepth_, shaperDepthParam_->load(), [this] (float v) { activeEngine_->setShaperDepth (v); });
    pushIfChanged (lastShaperTime_,  shaperTimeParam_->load(),  [this] (float v) { activeEngine_->setShaperTimeMs (v); });
    pushIfChanged (lastShaperXover_, shaperXoverParam_->load(), [this] (float v) { activeEngine_->setShaperXoverHz (v); });
    pushIfChanged (lastShaperSens_,  shaperSensParam_->load(),  [this] (float v) { activeEngine_->setShaperSens (v); });
    pushIfChanged (lastInputSubGain_, inputSubGainParam_->load(), [this] (float v) { activeEngine_->setInputSubGainDb (v); });
    pushIfChanged (lastInputMidGain_, inputMidGainParam_->load(), [this] (float v) { activeEngine_->setInputMidGainDb (v); });
    pushIfChanged (lastInputHighGain_, inputHighGainParam_->load(), [this] (float v) { activeEngine_->setInputHighGainDb (v); });
    pushIfChanged (lastCrossover_, crossoverParam_->load(), [this] (float v) { activeEngine_->setCrossoverFreq (v); });
    pushIfChanged (lastHighCrossover_, highCrossoverParam_->load(), [this] (float v) { activeEngine_->setHighCrossoverFreq (v); });
    pushIfChanged (lastBassChoke_,     bassChokeParam_->load(),     [this] (float v) { activeEngine_->setBassChokeHz (v); });
    pushIfChanged (lastSaturation_,    std::clamp (saturationParam_->load() + chr * 0.35f, 0.0f, 1.0f), [this] (float v) { activeEngine_->setSaturation (v); });
    pushIfChanged (lastDiffusion_, diffusionParam_->load(), [this] (float v) { activeEngine_->setDiffusion (v); });
    pushIfChanged (lastTonalCorr_, tonalCorrParam_ ? tonalCorrParam_->load() : 0.0f, [this] (float v) { activeEngine_->setTonalCorrection (v >= 0.5f); });
    pushIfChanged (lastModDepth_,  std::clamp (modDepthParam_->load() + chr * 0.5f, 0.0f, 1.0f), [this] (float v) { activeEngine_->setModDepth (v); });
    pushIfChanged (lastModRate_,   modRateParam_->load(),   [this] (float v) { activeEngine_->setModRate (v); });
    pushIfChanged (lastTailSpinDepth_, tailSpinDepthParam_->load(), [this] (float v) { activeEngine_->setTailSpinDepth (v); });
    pushIfChanged (lastTailSpinRate_,  tailSpinRateParam_->load(),  [this] (float v) { activeEngine_->setTailSpinRate (v); });
    pushIfChanged (lastERSize_,    erSizeParam_->load(),    [this] (float v) { activeEngine_->setERSize (v); });
    pushIfChanged (lastERLevel_,   erLevelParam_->load(),   [this] (float v) { activeEngine_->setERLevel (v); });
    pushIfChanged (lastERBoost_,   erBoostParam_->load(),   [this] (float v) { activeEngine_->setEREarlyBoost (v); });
    pushIfChanged (lastQtHiMidMult_, qtHiMidMultParam_->load(), [this] (float v) { activeEngine_->setQuadHiMidMultiply (v); });
    pushIfChanged (lastQtAirMult_,   qtAirMultParam_->load(),   [this] (float v) { activeEngine_->setQuadAirMultiply (v); });
    pushIfChanged (lastERRise_,    erRiseParam_->load(),    [this] (float v) { activeEngine_->setEROnsetRiseMs (v); });
    // ER-bus shelves take both gains at once; push if EITHER changed.
    {
        const float lo = erBusLowGainParam_->load(), hi = erBusHighGainParam_->load();
        if (lo != lastERBusLow_ || hi != lastERBusHigh_)
        {
            lastERBusLow_ = lo; lastERBusHigh_ = hi;
            activeEngine_->setERBusShelves (lo, hi);
        }
    }
    pushIfChanged (lastTankLevel_, tankLevelParam_->load(), [this] (float v) { activeEngine_->setTankOutputLevel (v); });
    pushIfChanged (lastTankSplitHz_, tankSplitHzParam_->load(), [this] (float v) { activeEngine_->setTankSplitHz (v); });
    pushIfChanged (lastERStereoNeutral_, erStereoNeutralParam_->load(), [this] (float v) { activeEngine_->setERStereoNeutral (v >= 0.5f); });
    pushIfChanged (lastERDecorr_, erDecorrParam_->load(), [this] (float v) { activeEngine_->setERDecorr (v); });
    pushIfChanged (lastXTalk_,     xtalkParam_->load(),     [this] (float v) { activeEngine_->setOutputCrossTalk (v); });
    // Phase 5 multiband: enable flag + the 3 per-band decays (combined setter).
    {
        const bool  mbEn = mbEnableParam_->load() >= 0.5f;
        const float mbLo = mbLowDecayParam_->load();
        const float mbMi = mbMidDecayParam_->load();
        const float mbHi = mbHighDecayParam_->load();
        if (mbEn != lastMbEnable_) { lastMbEnable_ = mbEn; activeEngine_->setMultibandEnabled (mbEn); }
        if (mbLo != lastMbLow_ || mbMi != lastMbMid_ || mbHi != lastMbHigh_)
        {
            lastMbLow_ = mbLo; lastMbMid_ = mbMi; lastMbHigh_ = mbHi;
            activeEngine_->setMultibandDecays (mbLo, mbMi, mbHi);
        }
    }
    pushIfChanged (lastLoCut_,     loCutParam_->load(),     [this] (float v) { activeEngine_->setLoCut (v); });
    pushIfChanged (lastHiCut_,     std::clamp (hiCutParam_->load() * toneHiCut, 200.0f, 20000.0f), [this] (float v) { activeEngine_->setHiCut (v); });
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
    pushIfChanged (lastMonoBelowDepth_, monoBelowDepthParam_->load(), [this] (float v) { activeEngine_->setMonoBelowDepth (v); });

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

    // ---- Wet ducking (ChromaVerb-style) ----
    // Sidechained off the dry snapshot (dryBuf*). Depth 0 → ReverbDucker early-
    // outs and the wet is untouched, so non-ducked presets stay bit-identical.
    {
        const float duck = duckParam_ ? duckParam_->load() : 0.0f;
        ducker_.setDepth (duck);
        ducker_.process (dryBufL_.data(), dryBufR_.data(), left, right, numSamples);
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
// v4 (2026-06-15): adds the "tonal_correction" APVTS bool. Older states
// omit it and fall through to its default (false = bit-identical).
static constexpr int kStateVersion = 4;
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
    // Preset identity. The name-keyed engine config (PostTankEQ bands,
    // modulation topology, FDN base-delay overrides, post-band trims) lives
    // OUTSIDE APVTS and is reconstructed by FactoryPreset::applyEngineConfig()
    // via name lookup. Persist the name so a reloaded session reapplies the
    // correct engine config instead of falling back to the engine defaults
    // (RandomWalk topology + flat PostTankEQ + default base delays). Additive
    // property — old binaries ignore it, and sessions saved before this (or
    // with no preset applied) simply omit it and keep prior behavior.
    if (auto* preset = lastAppliedPreset_.load (std::memory_order_acquire))
        state.setProperty ("lastPresetName", juce::String (preset->name), nullptr);
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

    // Snap every parameter object to its restored tree value. JUCE's discrete
    // parameters (AudioParameterBool/Choice) store the RAW normalized value
    // from setValue() but the APVTS tree records the QUANTIZED denormalized
    // value. If a host parked a discrete param at a fractional normalized
    // value (pluginval's randomized state-restore test does exactly this),
    // the restored tree value can EQUAL the tree's current record, so
    // replaceState's ValueTree no-op suppression skips the parameter callback
    // and the parameter object keeps the stale fractional value — "Bus Mode
    // not restored on setStateInformation" at strictness 7+. Message thread,
    // once per state load; the no-change guard keeps normal loads silent.
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        if (! child.hasProperty ("id") || ! child.hasProperty ("value"))
            continue;
        if (auto* rp = parameters.getParameter (child.getProperty ("id").toString()))
        {
            const float norm = rp->convertTo0to1 (static_cast<float> (child.getProperty ("value")));
            if (std::fabs (rp->getValue() - norm) > 1.0e-6f)
                rp->setValueNotifyingHost (norm);
        }
    }

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

    // Reconstruct the name-keyed engine config (see getStateInformation):
    // match the saved preset by name and arm a swap so the audio thread
    // reapplies its PostTankEQ bands / modulation topology / FDN base delays /
    // post-band trims via performPresetSwap() -> applyEngineConfig(). We
    // deliberately do NOT call applyTo() here — replaceState() already restored
    // the user's saved APVTS values, which must not be overwritten with the
    // preset's defaults. The same lastAppliedPreset_ pointer also feeds the
    // prepareToPlay() re-install path, so engine config is correct whether the
    // host calls prepareToPlay before or after setStateInformation.
    bool matchedPreset = false;
    if (tree.hasProperty ("lastPresetName"))
    {
        const auto savedName = tree.getProperty ("lastPresetName").toString();
        for (const auto& preset : getFactoryPresets())
        {
            if (savedName == juce::String (preset.name))
            {
                lastAppliedPreset_.store (&preset, std::memory_order_release);
                pendingPresetSwap_.store (true, std::memory_order_release);
                matchedPreset = true;
                break;
            }
        }
    }
    if (! matchedPreset)
    {
        // No persisted identity (older save / no preset applied at save) or the
        // saved name no longer maps to a factory preset (renamed/removed).
        // Clear the pointer so prepareToPlay()/performPresetSwap() don't reapply
        // an unrelated preset's config, AND arm a swap. Clearing alone is not
        // enough: a reused instance's engine still holds the PREVIOUS preset's
        // name-keyed config (PostTankEQ bands, modulation topology, FDN base
        // delays — none of which forcePushAllParametersTo touches). The armed
        // swap runs performPresetSwap()'s null branch → reapplyNeutralEngineConfig(),
        // resetting those to defaults on the audio thread (so no message-thread
        // biquad-coefficient write races process()).
        lastAppliedPreset_.store (nullptr, std::memory_order_release);
        pendingPresetSwap_.store (true, std::memory_order_release);
    }
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
    target->setShaperDepth       (shaperDepthParam_->load());
    target->setShaperTimeMs      (shaperTimeParam_->load());
    target->setShaperXoverHz     (shaperXoverParam_->load());
    target->setShaperSens        (shaperSensParam_->load());
    target->setInputSubGainDb    (inputSubGainParam_->load());
    target->setInputMidGainDb    (inputMidGainParam_->load());
    target->setInputHighGainDb   (inputHighGainParam_->load());
    target->setCrossoverFreq     (crossoverParam_->load());
    target->setHighCrossoverFreq (highCrossoverParam_->load());
    target->setBassChokeHz (bassChokeParam_->load());
    target->setSaturation        (saturationParam_->load());
    target->setDiffusion         (diffusionParam_->load());
    target->setModDepth          (modDepthParam_->load());
    target->setModRate           (modRateParam_->load());
    target->setTailSpinDepth     (tailSpinDepthParam_->load());
    target->setTailSpinRate      (tailSpinRateParam_->load());
    target->setERSize            (erSizeParam_->load());
    target->setERLevel           (erLevelParam_->load());
    target->setEREarlyBoost      (erBoostParam_->load());
    target->setQuadHiMidMultiply (qtHiMidMultParam_->load());
    target->setQuadAirMultiply   (qtAirMultParam_->load());
    target->setEROnsetRiseMs     (erRiseParam_->load());
    target->setERBusShelves      (erBusLowGainParam_->load(), erBusHighGainParam_->load());
    target->setTankOutputLevel   (tankLevelParam_->load());
    target->setTankSplitHz       (tankSplitHzParam_->load());
    target->setERStereoNeutral   (erStereoNeutralParam_->load() >= 0.5f);
    target->setERDecorr          (erDecorrParam_->load());
    target->setOutputCrossTalk   (xtalkParam_->load());
    target->setMultibandEnabled  (mbEnableParam_->load() >= 0.5f);
    target->setMultibandDecays   (mbLowDecayParam_->load(), mbMidDecayParam_->load(), mbHighDecayParam_->load());
    target->setLoCut             (loCutParam_->load());
    target->setHiCut             (hiCutParam_->load());
    target->setHiCutShelfGainDb  (hiCutShelfDbParam_->load());
    // Tonal correction (AccurateHall Jot GEQ on/off): force-push at swap so the
    // idle engine can't retain a stale on/off state — the pushIfChanged cache
    // would otherwise suppress the corrective update on the next block.
    target->setTonalCorrection   (tonalCorrParam_ ? tonalCorrParam_->load() >= 0.5f : false);

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
    target->setMonoBelowDepth    (monoBelowDepthParam_->load());

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
    lastShaperDepth_   = shaperDepthParam_->load();
    lastShaperTime_    = shaperTimeParam_->load();
    lastShaperXover_   = shaperXoverParam_->load();
    lastShaperSens_    = shaperSensParam_->load();
    lastInputSubGain_  = inputSubGainParam_->load();
    lastInputMidGain_  = inputMidGainParam_->load();
    lastInputHighGain_ = inputHighGainParam_->load();
    lastCrossover_     = crossoverParam_->load();
    lastHighCrossover_ = highCrossoverParam_->load();
    lastBassChoke_     = bassChokeParam_->load();
    lastSaturation_    = saturationParam_->load();
    lastDiffusion_     = diffusionParam_->load();
    lastModDepth_      = modDepthParam_->load();
    lastModRate_       = modRateParam_->load();
    lastTailSpinDepth_ = tailSpinDepthParam_->load();
    lastTailSpinRate_  = tailSpinRateParam_->load();
    lastERSize_        = erSizeParam_->load();
    lastERLevel_       = erLevelParam_->load();
    lastERBoost_       = erBoostParam_->load();
    lastQtHiMidMult_   = qtHiMidMultParam_->load();
    lastQtAirMult_     = qtAirMultParam_->load();
    lastERRise_        = erRiseParam_->load();
    lastERBusLow_      = erBusLowGainParam_->load();
    lastERBusHigh_     = erBusHighGainParam_->load();
    lastTankLevel_     = tankLevelParam_->load();
    lastTankSplitHz_   = tankSplitHzParam_->load();
    lastERStereoNeutral_ = erStereoNeutralParam_->load();
    lastERDecorr_      = erDecorrParam_->load();
    lastXTalk_         = xtalkParam_->load();
    lastMbEnable_      = mbEnableParam_->load() >= 0.5f;
    lastMbLow_         = mbLowDecayParam_->load();
    lastMbMid_         = mbMidDecayParam_->load();
    lastMbHigh_        = mbHighDecayParam_->load();
    lastLoCut_         = loCutParam_->load();
    lastHiCut_         = hiCutParam_->load();
    lastHiCutShelfDb_  = hiCutShelfDbParam_->load();
    lastWidth_         = widthParam_->load();
    lastGainTrim_      = gainTrimParam_->load();
    lastMonoBelow_     = monoBelowParam_->load();
    lastMonoBelowDepth_= monoBelowDepthParam_->load();
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
        duskverb::resolvePteqFreqQ (preset->name, pteqBandFreq_, pteqBandQ_);
    }
    else
    {
        // No known preset (session loaded with no/unknown identity). Reset the
        // name-keyed engine config to defaults so a preset previously applied
        // to this reused instance can't leak its PostTankEQ / topology / base
        // delays onto the restored session. resolvePteqFreqQ("") yields the
        // default freq/Q so the processBlock gain edge-detect stays consistent.
        newActive->reapplyNeutralEngineConfig();
        duskverb::resolvePteqFreqQ ("", pteqBandFreq_, pteqBandQ_);
        // reapplyNeutralEngineConfig() flattened the PostTankEQ, but the restored
        // session still carries its own pteq_band*_gain_db in APVTS. Re-install
        // those gains now (at the default freq/Q) — otherwise syncParameterCache-
        // ToCurrent() below caches them as already-applied and the processBlock
        // edge-detect (pushIfChanged) never fires, leaving the bands stuck flat
        // despite non-zero restored gains.
        newActive->setPostTankEQBand (0, pteqBandFreq_[0], pteqBandQ_[0], pteqBand0GainParam_->load());
        newActive->setPostTankEQBand (1, pteqBandFreq_[1], pteqBandQ_[1], pteqBand1GainParam_->load());
        newActive->setPostTankEQBand (2, pteqBandFreq_[2], pteqBandQ_[2], pteqBand2GainParam_->load());
        newActive->setPostTankEQBand (3, pteqBandFreq_[3], pteqBandQ_[3], pteqBand3GainParam_->load());
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

// APVTS side of the preset apply. FactoryPreset::collectParameters() is
// framework-free (the DAF build compiles the same table); this is the JUCE
// adapter that used to be FactoryPreset::applyTo(). Same call, same order,
// same convertTo0to1 round trip.
static void applyPresetToApvts (const FactoryPreset& preset,
                                juce::AudioProcessorValueTreeState& apvts)
{
    preset.collectParameters ([&apvts] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    });
}

void DuskVerbProcessor::applyFactoryPreset (const FactoryPreset& preset)
{
    // Message thread. Update APVTS + cache the engine-config state, then arm
    // the swap flag. The audio thread picks it up at the top of the next
    // processBlock(), runs performPresetSwap() to reconfigure the idle engine
    // from the just-applied APVTS values, and starts the crossfade. Touching
    // the engine directly here would race with processBlock.
    applyPresetToApvts (preset, parameters);

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


juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuskVerbProcessor();
}

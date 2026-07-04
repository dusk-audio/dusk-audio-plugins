#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
// Tuning-sweep env overrides (DUSKVERB_*) let the offline render harness drive
// params without a rebuild. They are STATIC for the process lifetime (the sweep
// exports them before the host loads the plugin), so read each EXACTLY ONCE and
// cache the pointer. CRITICAL: FactoryPreset::applyEngineConfig() — which consumes
// these — runs on the AUDIO THREAD (performPresetSwap), and std::getenv() is not
// real-time-safe (reads the environ global, unguarded vs setenv). The cache is
// primed from the processor constructor (message thread); applyEngineConfig only
// ever reads the already-initialized pointers (a plain load).
struct TuningEnv
{
    const char* pteq;
    const char* outdiff;
    const char* bandtrim;
    const char* tankfeed;
    const char* densjit;
    const char* fdnDelays;
    const char* tiledRoom;
    const char* matcheq;
    const char* tankonset;
    const char* dens;
    const char* modred;
    const char* indiff;
    const char* softonset;
    const char* octt60;
    const char* octref;
    const char* bloom;
    const char* tonal;
    const char* bloomexp;
    const char* widthbands;
    const char* dhoct;
    const char* dhtc;
    const char* dhrefl;
    const char* roomfill;
    const char* maindet;
    const char* buildup;
    const char* shimmerdown;
    const char* shimmersub;
    const char* shimmerhpf;
    const char* shimmerstereo;
    const char* shimmerair;
    const char* shimmerdense;
    const char* shimmerspin;
    const char* shimmerupv;
    const char* shimmeroct;
    const char* shimmernoise;
    const char* shimmerhfs;
    const char* ertaps;
    const char* datnotch;
    const char* dhlowlim;
    const char* pmb;
    const char* frontload;
    const char* dpvrefl;
    const char* densefield;
    const char* ddensefield;
    const char* airshelf;
    const char* lowshelf;
    const char* differ;
    TuningEnv()
        : pteq      (std::getenv ("DUSKVERB_PTEQ")),
          outdiff   (std::getenv ("DUSKVERB_OUTDIFF")),
          bandtrim  (std::getenv ("DUSKVERB_BANDTRIM")),
          tankfeed  (std::getenv ("DUSKVERB_TANKFEED")),
          densjit   (std::getenv ("DUSKVERB_DENSJIT")),
          fdnDelays (std::getenv ("DUSKVERB_FDN_DELAYS")),
          tiledRoom (std::getenv ("DUSKVERB_TILEDROOM")),
          matcheq   (std::getenv ("DUSKVERB_MATCHEQ")),
          tankonset (std::getenv ("DUSKVERB_TANKONSET")),
          dens      (std::getenv ("DUSKVERB_DENS")),
          modred    (std::getenv ("DUSKVERB_MODRED")),
          indiff    (std::getenv ("DUSKVERB_INDIFF")),
          softonset (std::getenv ("DUSKVERB_SOFTONSET")),
          octt60    (std::getenv ("DUSKVERB_OCTT60")),
          octref    (std::getenv ("DUSKVERB_OCTREF")),
          bloom     (std::getenv ("DUSKVERB_BLOOM")),
          tonal     (std::getenv ("DUSKVERB_TONAL")),
          bloomexp  (std::getenv ("DUSKVERB_BLOOMEXP")),
          widthbands (std::getenv ("DUSKVERB_WIDTHBANDS")),
          dhoct     (std::getenv ("DUSKVERB_DHOCT")),
          dhtc      (std::getenv ("DUSKVERB_DHTC")),
          dhrefl    (std::getenv ("DUSKVERB_DHREFL")),
          roomfill  (std::getenv ("DUSKVERB_ROOMFILL")),
          maindet   (std::getenv ("DUSKVERB_MAINDET")),
          buildup   (std::getenv ("DUSKVERB_BUILDUP")),
          shimmerdown (std::getenv ("DUSKVERB_SHIMMERDOWN")),
          shimmersub (std::getenv ("DUSKVERB_SHIMMERSUB")),
          shimmerhpf (std::getenv ("DUSKVERB_SHIMMERHPF")),
          shimmerstereo (std::getenv ("DUSKVERB_SHIMMERSTEREO")),
          shimmerair (std::getenv ("DUSKVERB_SHIMMERAIR")),
          shimmerdense (std::getenv ("DUSKVERB_SHIMMERDENSE")),
          shimmerspin (std::getenv ("DUSKVERB_SHIMMERSPIN")),
          shimmerupv (std::getenv ("DUSKVERB_SHIMMERUPV")),
          shimmeroct (std::getenv ("DUSKVERB_SHIMMEROCT")),
          shimmernoise (std::getenv ("DUSKVERB_SHIMMERNOISE")),
          shimmerhfs (std::getenv ("DUSKVERB_SHIMMERHFS")),
          ertaps (std::getenv ("DUSKVERB_ERTAPS")),
          datnotch (std::getenv ("DUSKVERB_DATNOTCH")),
          dhlowlim (std::getenv ("DUSKVERB_DHLOWLIM")),
          pmb (std::getenv ("DUSKVERB_PMB")),
          frontload (std::getenv ("DUSKVERB_FRONTLOAD")),
          dpvrefl   (std::getenv ("DUSKVERB_DPVREFL")),
          densefield (std::getenv ("DUSKVERB_DENSEFIELD")),
          ddensefield (std::getenv ("DUSKVERB_DDENSEFIELD")),
          airshelf  (std::getenv ("DUSKVERB_AIRSHELF")),
          lowshelf  (std::getenv ("DUSKVERB_LOWSHELF")),
          differ    (std::getenv ("DUSKVERB_DIFFER")) {}
};
const TuningEnv& tuningEnv()
{
    static const TuningEnv cache;   // C++11 thread-safe, one-time init
    return cache;
}
} // namespace

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
    tuningEnv();

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

// ─── File-scope shared PostTankEQ lookup ──────────────────────────────────
// Hoisted above performPresetSwap so the cache-populate path can call it.
namespace {
    struct PostTankEQConfig { float freq[4]; float q[4]; float gain[4]; };
    static constexpr float kPteqDefaultFreq[4] = {  80.0f,  500.0f, 3000.0f, 10000.0f };
    static constexpr float kPteqDefaultQ   [4] = {   1.5f,    1.0f,    1.5f,     0.8f };
    static constexpr float kPteqZeroGain   [4] = {   0.0f,    0.0f,    0.0f,     0.0f };
    // File-scope (NOT a function-local static): initializes at static-init time on
    // the loader thread, never on the first audio-thread call. applyEngineConfig()
    // reads this during performPresetSwap (audio thread); a function-local static
    // would take the one-time init guard + heap-allocate its buckets on that first
    // swap. Same RT-safety rationale as the constexpr name→config arrays elsewhere.
    static const std::unordered_map<std::string_view, PostTankEQConfig> kPteqByName = {
            // Drum Plate (2026-06-23 workflow): gentle post-tank shape — 3.5kHz Q1.2
            // -1.5dB tames bloom 2-4k/4-8k; 12kHz Q0.8 +2.0dB fills the HF-top the
            // 8kHz Hi Cut creates (the win driver). 17->12. Post-tank + gentle (unlike
            // the heavy MATCHEQ cut+makeup removed earlier) but EAR-CHECK the tail grain.
            { "Drum Plate", {
                {   80.0f,  500.0f, 3500.0f, 12000.0f },
                {    1.00f,   1.00f,   1.20f,    0.80f },
                {    0.00f,   0.00f,  -1.50f,    2.00f },
            } },
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
                {   1.2f,    1.0f,    2.5f,     1.2f },
                {  -2.0f,   -1.5f,   -3.0f,    +1.0f },   // 70Hz -4.5->-2.0 (2026-06-10): the deep boom cut was tuned for tank_level 0.42; at the rebalanced 0.55 it starved boom/body 80-500. 1kHz Q1.0 -1.5 (sine1k tame), 8kHz +1.0 (air restore), 2560 modal notch unchanged.
            } },
            // Bright Hall (BH-6 on VintageTank 2026-05-30; Bands 1+3 re-tuned
            // 2026-06-10 for the AccurateHall migration):
            //   Band 0 —  60 Hz Q=1.0 -3.0 dB: BH-2 tight sub-bass scoop
            //                                   (still load-bearing on FDN —
            //                                   removing it opened 5 boom
            //                                   gates).
            //   Band 1 — 1000 Hz Q=6.0 +2.0 dB: was -5.5 (VintageTank modal
            //                                   ring, gone on FDN); +2 lifts
            //                                   sine1k toward the anchor.
            //   Band 2 — 3500 Hz Q=2.0 -2.0 dB: BH-6 spec_L1 mean trim.
            //   Band 3 — 10000 Hz Q=0.8 +9.0 dB: post-tank early-HF restore.
            //                                   The FDN loop (hiCutShelf -6)
            //                                   runs darker than VintageTank;
            //                                   brightening the LOOP lifted
            //                                   late bloom 8-12k instead
            //                                   (gain==decay==level), so the
            //                                   tilt is post-tank. Closes
            //                                   cent_50/cent_500/hi/spec.
            { "Bright Hall", {
                {  60.0f, 1000.0f, 5000.0f, 10000.0f },
                {   1.0f,    6.0f,    1.2f,     0.8f },
                { -3.0f,   +2.0f,   +3.0f,    +4.0f },
            } },
            // Small Drum Room (SDR-NL3 on NonLinear algo=6, 2026-05-30):
            //   Band 0/1/3 — defaults / reserved.
            //   Band 2 — 1500 Hz Q=0.5 -3.5 dB: wide gentle mid scoop
            //              to counter NonLinear's mid-bulge. Paired with
            //              gainTrim +6.20 in the preset row: wings get
            //              full +6.20 dB lift, mids get ~+2.7 dB net.
            { "Small Drum Room", {
                {  250.0f, 1000.0f, 1500.0f, 5000.0f },   // 2026-06-16 EAR: Band 0 101->250 Hz — tame the ~250 Hz "boxy" resonance vs Valhalla (gentle, narrow Q)
                {  3.50f,  4.00f,  1.00f,  1.50f },
                { -2.50f, +3.50f, -3.00f, -4.50f },
            } },
            // Blade Runner 224 (BR-9 2026-05-30; Band 0 re-tuned 2026-06-09
            // with the composite-exact octave GEQ re-calibration):
            //   Band 0 —  350 Hz Q=1.2 -3.0 dB: late low-mid decouple. The
            //                                    accurate 250 Hz T60 target
            //                                    (gain==decay==level coupling)
            //                                    left 100-700 Hz late energy
            //                                    ~+2.5 dB hot; this post-tank
            //                                    cut closes sub-bass/boom-low/
            //                                    spec_L1/mid without touching
            //                                    decay. (Was 600 Hz Q=2 -2.0.)
            //   Band 1 — 3000 Hz Q=1.5 -3.5 dB: lower bloom suppressor.
            //   Band 2 — 6000 Hz Q=1.0 -5.0 dB: bloom 4-8k closed BR-8.
            //   Band 3 —10000 Hz Q=1.2 -6.0 dB: BR-9 deepens -5.0 → -6.0
            //                                    to clamp bloom 8-12k (was
            //                                    0.53 dB over gate).
            // Medium Drum Room (AccurateHall, 2026-06-10): dark tilt — the
            // tank tail runs far brighter than the eighties-color anchor
            // (blooms +5..+12 dB, cent_500 +124%); 1 kHz tames the hot sine1k.
            { "Medium Drum Room", {
                {  150.0f, 1000.0f, 3500.0f, 10000.0f },
                {   1.00f,   2.50f,   0.70f,    1.00f },
                {   0.00f,  -3.50f,  -5.50f,   -8.00f },
            } },
            // Vocal Plate (AccurateHall, 2026-06-10): HF tilt for the bright
            // late field (cent_500 +72%) — closes mid/bloom/snare via the
            // renorm. A 1 kHz +6 lift for sine1k -12.4 was probed and is
            // POISON here (15/19 vs 13) — VP's post-tank 1 kHz response is
            // inconsistent (also documented in the FDN era); leave Band 1 flat.
            { "Vocal Plate", {
                {  150.0f, 1000.0f, 5000.0f, 10000.0f },
                {   1.00f,   2.50f,   0.80f,    1.00f },
                {   0.00f,    0.00f,  -3.50f,    0.00f },
            } },
            // Tiled Room (AccurateHall trial 2026-06-10): dark post-tank tilt —
            // the calibrated octave T60s fixed decay but the tail LEVEL runs
            // bright (cent_500 +215%, blooms 2-12k +2..+4.4).
            { "Tiled Room", {
                {  150.0f, 1000.0f, 3500.0f,  9000.0f },
                {   1.00f,   1.00f,   0.70f,    1.00f },
                {   0.00f,    0.00f,  -5.00f,   -2.50f },
            } },
            { "Blade Runner 224", {
                {  350.0f, 3000.0f, 6000.0f, 10000.0f },
                {   1.20f,   1.50f,   1.00f,    1.20f },
                {  -3.00f,  -3.50f,  -5.00f,   -6.00f },
            } },
            // 79 Vocal Chamber (2026-06-25 ear: "dull vs VVV"): the QuadTank's 5-10k
            // inversion + the earlier qt_air damp left ss-hi 5-10k -5.3dB. A 6.5kHz
            // Q1.2 +4.5dB peak lifts the dull region (ss-hi -5.3 -> ~-1.8) without
            // touching the hot >10k. COST: re-brightens the late tail (cent_500 fails)
            // — the coupling wall (anchor's 5-10k is present early then decays; a static
            // peak brightens early+late). EAR-CHECK: un-dull vs cent_500. Revertable.
            { "79 Vocal Chamber", {
                { 6500.0f,  500.0f, 1000.0f, 16000.0f },
                {    1.20f,   1.00f,   1.00f,     1.00f },
                {    4.50f,   0.00f,   0.00f,     0.00f },
            } },
    };
    static const std::unordered_map<std::string_view, PostTankEQConfig>&
    pteqByName() { return kPteqByName; }
    inline void resolvePteqFreqQ (const char* name, float fOut[4], float qOut[4])
    {
        // Env sweep override (DUSKVERB_PTEQ = 12 CSV floats f,q,g per band x4)
        // takes precedence — must match the freq/Q that applyEngineConfig()
        // pushes to the engine, else performPresetSwap()/the pteq gain
        // edge-detect would revert the bands to the preset/default centres.
        if (const char* envPq = tuningEnv().pteq; envPq != nullptr && envPq[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (envPq), ",", "");
            if (t.size() == 12)
            {
                for (int b = 0; b < 4; ++b)
                { fOut[b] = t[b * 3].getFloatValue(); qOut[b] = t[b * 3 + 1].getFloatValue(); }
                return;
            }
        }
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
    else
    {
        // No known preset (session loaded with no/unknown identity). Reset the
        // name-keyed engine config to defaults so a preset previously applied
        // to this reused instance can't leak its PostTankEQ / topology / base
        // delays onto the restored session. resolvePteqFreqQ("") yields the
        // default freq/Q so the processBlock gain edge-detect stays consistent.
        newActive->reapplyNeutralEngineConfig();
        resolvePteqFreqQ ("", pteqBandFreq_, pteqBandQ_);
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

// Linear-scan lookup for the small per-preset name→config tables below. They are
// static constexpr std::array (NOT std::map) because applyEngineConfig runs on the
// AUDIO thread (performPresetSwap) — a function-local std::map would heap-allocate
// its RB-tree nodes and take the static-init guard lock on the first preset swap.
// Returns nullptr when the name isn't listed.
template <typename V, std::size_t N>
static const V* findPresetConfig (const std::array<std::pair<std::string_view, V>, N>& table,
                                  std::string_view nv) noexcept
{
    for (const auto& e : table)
        if (e.first == nv) return &e.second;
    return nullptr;
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
        // Sweep override: DUSKVERB_PTEQ = 12 CSV floats (f,q,g per band x4).
        // Direct-call path (same as the map), so it bypasses the APVTS desync
        // that blinds --param pteq edits. Absent in normal sessions.
        if (const char* envPq = tuningEnv().pteq; envPq != nullptr && envPq[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (envPq), ",", "");
            if (t.size() == 12)
            {
                for (int b = 0; b < 4; ++b)
                    engine.setPostTankEQBand (b, t[b * 3].getFloatValue(),
                                              t[b * 3 + 1].getFloatValue(),
                                              t[b * 3 + 2].getFloatValue());
            }
        }
        else
        for (int b = 0; b < 4; ++b)
            engine.setPostTankEQBand (b, fSrc[b], qSrc[b], gSrc[b]);
    }

    // ─── AccurateHall (algo 10) per-OCTAVE T60 — Jot/Schlecht accurate-RT ──
    // Nine octave T60 targets (63 Hz..16 kHz) routed to FDNReverbT<true> via a
    // direct engine call (no APVTS param → no edge-detect desync, like the
    // TimeVaryingHiDamp call above). This is the per-octave decay control the
    // FiveBandDamping FDN could not give (9 octave T60 gates vs 5 damping
    // bands). Presets not in the map reset ALL octaves flat (<=0 → that octave
    // inherits the broadband Decay Time), which both makes AccurateHall ≡ FDN
    // and clears any stale octave config left by a previously-loaded preset.
    // No-op on every other engine — the GEQ exists only in accurateHall_.
    {
        struct OctaveT60Override { float t60[9]; };
        // constexpr std::array + linear scan (NOT std::map) — applyEngineConfig runs
        // on the AUDIO thread (performPresetSwap); a function-local std::map would
        // heap-allocate its RB-tree nodes + take the static-init guard lock on the
        // first preset swap. Same RT-safe pattern as the DenseHall GEQ map below.
        static constexpr std::array<std::pair<std::string_view, OctaveT60Override>, 3> kAccurateHallT60ByName = {{
            // Per-octave T60 targets (63 Hz..16 kHz), calibrated to land each
            // octave within ±5% of the VVV anchor (Schroeder backward-int on
            // the noiseburst tail). The 9-vs-5 coupling wall the FDN
            // FiveBandDamping floored on; the octave GEQ sets each directly.
            // Since the composite-exact GEQ design (OctaveGEQDesign.cpp) the
            // shelf cascade realizes its commanded gains AT the octave centres,
            // so these targets are close to raw anchor T60 — the small residual
            // compensates other in-loop losses (structural/AA HF damping,
            // mod-interpolation). Mostly SR-invariant (sr cancels in
            // g=10^(-3·L/(T·sr)) since L∝sr); the AA-loss share is not.
            // Blade Runner 250 Hz is pinned at 27 s: measured T60 walls at
            // ~8.8 s (anchor 9.7) regardless of target — per-line decay tilt
            // blends fast lines into that band; higher targets only raise
            // sine1k THD. Its late-250 level excess is decoupled by the
            // 350 Hz PostTankEQ cut instead.
            // LIVE AccurateHall-tail presets ONLY (read by accurateHall_.setOctaveT60):
            // Vocal Plate (algo 10), Ambience (algo 10), Tiled Room (algo 13 — its
            // COMPOSITE tail IS accurateHall_.process(), so this table is live; it was
            // wrongly deleted as "dormant" 2026-06-16 and restored same day). Any other
            // name here would be inert. calibrate_octave_t60.py regenerates this block
            // (wipes inline comments) — durable rationale lives in this header.
            // Vocal Plate + Ambience recalibrated 2026-06-16 vs the TRUSTED screenshot
            // anchors → octave 9/9 within ±5% (was Vocal Plate 6/9 / Ambience 16k −37%).
            // BEGIN_OCTAVE_T60_MAP (maintained by tools/tuner/calibrate_octave_t60.py)
            { "Vocal Plate", {{ 0.6600f, 0.6460f, 0.7250f, 0.6490f, 0.6990f, 0.6700f, 0.6120f, 0.6100f, 0.3900f }} },  // 2026-07-03 audit: PRESET-SPACE WALLED at 19. 63's +26% cal drift is LOAD-BEARING (realized 0.83 ~= anchor 0.88; "correcting" to 0.50 -> 21). 125 0.646->0.53 passes T60-125 but flips boom 80-200/100-300 quiet (level-vs-ring coupling) = net 19 either way; kept original for least character change.  // 2026-06-16 re-tuned vs CLEAN screenshot anchor (octave recal, 34->~20). 63 settled to ~0.88. Residual = AccurateHall early-field wall (attack/onset/cent dark) + 12.9k spike + mod = structural.
            { "Ambience", {{ 1.2590f, 1.5841f, 1.1106f, 0.7390f, 0.7470f, 0.8101f, 0.8043f, 0.8688f, 1.0640f }} },  // 2026-06-16 octave recal vs CORRECTED vvv-ambience anchor (Newton: 63 down, 500/16k up, 1k down). Residual = sparse-comb-vs-modal structural wall.
            { "Tiled Room", {{ 0.6370f, 0.5615f, 0.6482f, 0.7510f, 0.6489f, 0.6736f, 0.6699f, 0.4520f, 0.1680f }} },  // algo-13 composite tail IS accurateHall_ → live (removing it regressed 14→17). 16k 0.168s = the bright tiled-room top.
            // 2026-06-16: 6 DORMANT entries removed (Vocal Hall / Blade Runner 224 /
            // Cathedral Large Hall / Bright Hall migrated to DenseHall algo 14; Drum
            // Plate / Medium Drum Room to Dattorro algo 0). setAccurateHallOctaveT60()
            // routes to accurateHall_, so those were inert config no engine reads - they
            // misled a live diagnosis (the table "commanded" a T60 the engine never saw).
            // Caught by fleet_audit.py --verify-tables. (Tiled Room's algo-13 composite
            // DOES call accurateHall_, so its entry stays live — see header.)
            // END_OCTAVE_T60_MAP
        }};
        const OctaveT60Override* hit = findPresetConfig (kAccurateHallT60ByName, std::string_view (name));
        for (int b = 0; b < 9; ++b)
            engine.setAccurateHallOctaveT60 (b, hit ? hit->t60[b] : 0.0f);
        // Couple the Decay knob to the octave curve: reference = this preset's
        // baked Decay, so loading reproduces the calibrated curve exactly while
        // the live knob scales it. Unmapped presets clear the ref (scale 1.0).
        engine.setAccurateHallOctaveDecayRef (hit ? decay : 0.0f);
    }

    // ─── DenseHall (algo 14) per-OCTAVE T60 GEQ — FORK #2 (2026-06-16) ──────
    // Decoupled 9-octave damping replacing DenseHall's 3-band shelf (cent dark +
    // T60-16k dies on Vocal Hall / Cathedral / Bright Hall / Blade Runner / Large
    // Chamber). Env DUSKVERB_DHOCT="t0,..,t8" drives the calibration sweep without
    // a rebuild; else the per-preset bake; else all-flat (octaves inherit Decay →
    // the legacy 3-band path runs → bit-identical). No-op on non-DenseHall engines.
    {
        // constexpr std::array + linear scan (NOT std::map) — applyEngineConfig runs
        // on the AUDIO thread (performPresetSwap); a function-local std::map would
        // heap-allocate its RB-tree nodes + take the static-init guard lock on the
        // first preset swap. Same RT-safe pattern as kWidthBandsByName.
        struct OctaveT60Override { float t60[9]; };
        static constexpr std::array<std::pair<std::string_view, OctaveT60Override>, 5> kDenseHallOctaveT60ByName = {{
            // 2026-06-16: 16k UNWALLED by the cubicHermite read fix (DenseHall was on
            // linear interp — the HF-loss bug). Per-octave T60 now reachable, so these
            // curves (Newton-calibrated via dh_calib.py vs the trusted anchors) close
            // cent + the T60 cluster on the dark halls. The high 16k/63 values are the
            // over-command compensating residual loss (realized lands at the anchor).
            // 2026-06-17 re-calibrated AFTER raising the preset decays to the anchor
            // LOW T60 (the lows were decay-walled = "weak/no low end"). Now the low
            // octaves reach the anchor's long low tail; GEQ still cuts mids/highs down.
            { "Vocal Hall",           {{ 5.9265f, 5.6973f, 4.9098f, 4.0592f, 3.1336f, 2.6866f, 2.4414f, 2.2168f, 16.0000f }} },  // 2026-06-24 16k cmd 6.11->16.0: realized T60-16k 1.51->1.66s (anchor 1.71, was -11.7% short) = more top-octave tail sparkle (user: "decay sounds too short" = dark top). AA loss bounds it; 8k/edt/tail unchanged.
            { "Cathedral Large Hall", {{ 4.8689f, 4.5163f, 3.8568f, 3.3999f, 3.1294f, 2.6089f, 2.1764f, 1.9856f, 5.2543f }} },
            { "Bright Hall",          {{ 7.7700f, 7.5129f, 5.8751f, 5.3244f, 4.4570f, 3.9710f, 3.3470f, 2.5017f, 3.3005f }} },
            { "Blade Runner 224",     {{ 15.6953f, 10.5048f, 13.3874f, 11.0317f, 10.7739f, 6.6164f, 4.4356f, 2.3364f, 2.2943f }} },
            { "Large Chamber",        {{ 4.7966f, 4.3140f, 3.5555f, 3.1402f, 2.8053f, 2.0729f, 1.6100f, 1.0830f, 1.5257f }} },  // FLOOR 30. Tunable levers exhausted 2026-06-17: octave low-cut flips boom; HF-raise over-brightens sustained but cent_50 (early-transient) stays dark; decay-lower no-op (octave GEQ overrides broadband). Residual structural: dark early transient, ss-hot coupling, inconsistent anchor sub (noiseburst 4.15s vs sustained 1.53s), front-load, sine1k, per-band width.
        }};
        float dh[9] = {0,0,0,0,0,0,0,0,0};
        bool haveDH = false;
        const char* env = tuningEnv().dhoct;
        if (env != nullptr && env[0] != '\0')
        {
            auto toks = juce::StringArray::fromTokens (juce::String (env), ",", "");
            if (toks.size() == 9) { for (int b = 0; b < 9; ++b) dh[b] = toks[b].getFloatValue(); haveDH = true; }
        }
        else
        {
            const std::string_view nv (name);
            for (const auto& e : kDenseHallOctaveT60ByName)
                if (e.first == nv) { for (int b = 0; b < 9; ++b) dh[b] = e.second.t60[b]; haveDH = true; break; }
        }
        for (int b = 0; b < 9; ++b) engine.setDenseHallOctaveT60 (b, dh[b]);
        engine.setDenseHallOctaveDecayRef (haveDH ? decay : 0.0f);

        // FORK B — Jot tonal-correction opt-in per DenseHall preset (decouple the
        // per-octave T60 from steady-state LEVEL → fixes the ss-hot/cent-dark
        // coupling). Env DUSKVERB_DHTC="1"/"0" forces on/off for the sweep; else
        // the per-preset set. Default off → bit-identical (also needs octaveActive).
        {
            // OPT-IN only where it nets gains. Measured 2026-06-17: Large Chamber
            // 30→25, Vocal Hall 15→12 — but tonal-correction WRECKS the tilted-anchor
            // presets (Blade Runner 15→28, Cathedral 17→25, Bright Hall +1), exactly
            // like the AccurateHall Phase-1 flat-correction hurt Ambience. So only the
            // two presets whose anchors are near-flat-energy enable it.
            static constexpr std::array<std::string_view, 2> kDenseHallTCByName = { "Vocal Hall", "Large Chamber" };
            bool tc = false;
            { const std::string_view nv (name); for (const auto& e : kDenseHallTCByName) if (e == nv) { tc = haveDH; break; } }
            const char* tcEnv = tuningEnv().dhtc;
            if (tcEnv != nullptr && tcEnv[0] != '\0') tc = haveDH && (tcEnv[0] == '1');
            engine.setDenseHallTonalCorrection (tc);
        }
    }

    // ─── Per-preset post-tank OUTPUT diffusion (Bright Hall metallic-ring) ──
    // The 16-line FDN leaves sparse, isolated HF tail modes above 4 kHz (tail
    // spectral kurtosis ~19 vs the dense anchor ~12) that read as a metallic
    // ring on a bright, long-HF preset. A 4-stage allpass cascade on the wet
    // tail smears them into a denser wash. Unlisted presets → disabled (the
    // process() call is skipped → bit-null). amount / lfoScale / delayScale
    // tuned by tools/tuner/outdiff_kurtosis_sweep.py.
    {
        struct OutDiffConfig { float amount, lfoScale, delayScale; };
        // constexpr std::array + linear scan (NOT std::map) — RT-safe on the audio
        // thread; same pattern as kOutputAirShelfByName. See findPresetConfig().
        static constexpr std::array<std::pair<std::string_view, OutDiffConfig>, 0> kOutputDiffusionByName = {{
            // BEGIN_OUTDIFF_MAP (maintained by outdiff_kurtosis_sweep.py)
            // END_OUTDIFF_MAP
            // (Cathedral's FDN-era output-diffuser entry removed 2026-06-13 — it
            // migrated to the DenseHall engine, which is already dense; the
            // post-tank diffuser would over-smear it.)
        }};
        // Sweep override: DUSKVERB_OUTDIFF="amount,lfoScale,delayScale" forces
        // the diffuser ON for the preset being rendered (the kurtosis sweep
        // drives it without a rebuild, like DUSKVERB_FDN_DELAYS).
        if (const char* env = tuningEnv().outdiff; env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");
            if (t.size() == 3)
                engine.setOutputDiffusion (true, t[0].getFloatValue(),
                                           t[1].getFloatValue(), t[2].getFloatValue());
            else
                engine.setOutputDiffusion (false, 0.0f, 0.0f, 1.0f);
        }
        else if (const auto* od = findPresetConfig (kOutputDiffusionByName, std::string_view (name)))
            engine.setOutputDiffusion (true, od->amount, od->lfoScale, od->delayScale);
        else
            engine.setOutputDiffusion (false, 0.0f, 0.0f, 1.0f);   // bypass → bit-null
    }

    // ─── FORK A: discrete early-reflection tap ("duh-duh") ────────────────────
    // The VVV halls have a prominent SECOND arrival ~90-110 ms post-onset; DV's
    // smooth tank decays through it (the user A/B: Blade Runner + Bright Hall
    // "missing the delay sound, two snare hits"). A single delayed dry tap
    // (R-offset for width, 6 kHz-darkened) summed to the wet restores it. Unlisted
    // presets → setReflectionTap(0,0) → reflActive_ false → block skipped →
    // bit-identical. Env DUSKVERB_DHREFL="ms,gain" forces it for tuning; "0"/"off"
    // disables. gain is linear (0.30 ≈ −10 dB rel the dry hit).
    {
        // constexpr std::array (NOT std::map) — audio-thread path; an empty std::map
        // still trips the static-init guard lock on the first swap, and the first
        // entry added would silently become an RT heap alloc. std::array can never
        // heap-allocate on the swap path. Empty today (Fork A is env-only):
        // Measurement 2026-06-17: a tap ALONE fills DV's late-swelling early field;
        // the anchors' "duh-DUH" needs a DIP first (front-load + a mild tank-onset,
        // adjacent to the reverted "literal delay" lever). Left OFF pending the user's
        // A/B ear test; the tuned combos render via DUSKVERB_TANKONSET + DUSKVERB_DHREFL.
        struct ReflConfig { std::string_view name; float ms, gain, lpFc = 11000.0f; };
        // 79 Vocal Chamber 2026-06-18 (ear): a discrete early tap (~100ms, the anchor's
        // loudest arrival) ON TOP of the front-load composite. NOW the CLEAN+BRIGHT
        // Fork A (pre-diffuser dry feed + 11kHz LP) — the earlier 6kHz dark/smeared
        // version was "very hard to hear" + cloudy. Bright tap also lifts the early
        // field (cent dark −11% → "VVV brighter on top"). gain 0.5 = prominent.
        static constexpr std::array<ReflConfig, 3> kReflectionByName = {{
            // 95ms/1.2 (ear 2026-06-18/19): gain 1.2 balanced (2.0 too loud, 0.5 too
            // quiet). ms 112→95 — the tap "sounded longer than VVV" (DV's fuller blob
            // reads as a longer delay), so pull it earlier. NOTE: anchor metric prom
            // 28.5 dB is a SHARP tick; DV's blob sounds louder at lower prom — match
            // the EAR, not the metric target.
            { "79 Vocal Chamber", 95.0f, 1.2f },
            // Bright Hall 2026-06-19: the discrete 2nd-hit, NOW POSSIBLE on top of the
            // BuildupDiffuser. The tap alone always swallowed the attack (no dip on the
            // dense-from-zero tank). The buildup carves a deep dip (≈−48 dB @100-130 ms),
            // so a clean discrete tick AT the anchor's time sits IN the dip without
            // becoming the attack (the dip is the 40-dB-down reference attack_time works
            // back to). gain 0.20 → early_tap 18.0 dB@149 ms ≈ anchor 18.8@139, attack
            // stays 13 ms. The buildup's smooth density peak alone was inaudible (user:
            // "no duh-DUH, buried"); this clean tick is the audible 2nd hit.
            // Ear 2026-06-19: ms 139→129 (DV's tap was ~10 ms LATER than VVV — pull it in
            // to ~139 ms measured); lpFc 3500 (11 k sharp tick → 5.5 k → 3.5 k: ear said
            // the tap was still "bright/harsher", VVV's is "smoother", so keep rolling the
            // top off — a real room reflection is darker than the dry snare transient).
            { "Bright Hall", 129.0f, 0.20f, 3500.0f },
        }};
        if (const char* env = tuningEnv().dhrefl; env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");   // "ms,gain[,lpFc]"
            if (t.size() >= 2 && t[1].getFloatValue() > 0.0f)
                engine.setReflectionTap (t[0].getFloatValue(), t[1].getFloatValue(),
                                         t.size() >= 3 ? t[2].getFloatValue() : 11000.0f);
            else
                engine.setReflectionTap (0.0f, 0.0f);   // "0" / "off" → bit-null
        }
        else
        {
            const std::string_view nv (name);
            float ms = 0.0f, gain = 0.0f, lpFc = 11000.0f;   // default off → bit-null for unlisted presets
            for (const auto& e : kReflectionByName) if (e.name == nv) { ms = e.ms; gain = e.gain; lpFc = e.lpFc; break; }
            engine.setReflectionTap (ms, gain, lpFc);
        }
    }

    // Early-tap BANK — up to 8 discrete reflections at the ANCHOR's measured
    // arrival times (the early-refl-count gate wants 2-10 arrivals; the single
    // Fork-A tap above can't match a pattern). Env DUSKVERB_ERTAPS=
    // "t1:g1,t2:g2,...[;lpHz]" for rebuild-free sweeps; else the per-preset
    // bake; else off (count 0 → bit-identical fleet).
    {
        struct ETapConfig { std::string_view name; int n; float ms[8]; float g[8]; float lpFc; };
        static constexpr std::array<ETapConfig, 1> kEarlyTapsByName = {{
            // BEGIN_ERTAPS_MAP (times from full_check 'early refl' anchor readouts)
            { "", 0, {}, {}, 9000.0f },   // placeholder — filled per-preset after env sweeps
            // END_ERTAPS_MAP
        }};
        float ms[8] = {}, g[8] = {}; int n = 0; float lpFc = 9000.0f;
        if (const char* env = tuningEnv().ertaps; env != nullptr && env[0] != '\0')
        {
            juce::String s (env);
            const int semi = s.indexOfChar (';');
            if (semi >= 0) { lpFc = s.substring (semi + 1).getFloatValue(); s = s.substring (0, semi); }
            juce::StringArray pairs; pairs.addTokens (s, ",", "");
            for (int i = 0; i < pairs.size() && n < 8; ++i)
            {
                const int colon = pairs[i].indexOfChar (':');
                if (colon <= 0) continue;
                ms[n] = pairs[i].substring (0, colon).getFloatValue();
                g[n]  = pairs[i].substring (colon + 1).getFloatValue();
                ++n;
            }
        }
        else
        {
            const std::string_view nv (name);
            for (const auto& e : kEarlyTapsByName)
                if (e.n > 0 && e.name == nv)
                { n = e.n; for (int i = 0; i < n; ++i) { ms[i] = e.ms[i]; g[i] = e.g[i]; } lpFc = e.lpFc; break; }
        }
        engine.setEarlyTapBank (ms, g, n, lpFc);
    }

    // Phase 3 output match-EQ: per-octave (9-band, 63 Hz..16 kHz) cut-only output
    // gains shaping the wet steady-state envelope toward the anchor. Env override
    // DUSKVERB_MATCHEQ="g0,..,g8" drives it without a rebuild (offline corr sweep);
    // else the per-preset baked map; else flat → bit-null.
    {
        // constexpr std::array + linear scan (NOT std::map) — RT-safe on the audio
        // thread; same pattern as kOutputAirShelfByName. See findPresetConfig().
        static constexpr std::array<std::pair<std::string_view, std::array<float, 9>>, 8> kOutputMatchEQByName = {{
            // BEGIN_MATCHEQ_MAP (offline anchor-envelope fit, octave 63Hz..16kHz, cut-only)
            { "Vocal Plate",          { 0.8573f, 0.6909f, 0.6482f, 0.7116f, 0.7168f, 0.7990f, 0.9528f, 0.8954f, 1.0000f } },
            // Drum Plate + Vintage Gold Plate REMOVED 2026-06-15 (ear: heavy cut + big
            // makeup unmasked the Dattorro tail grain vs the smooth anchor — gates win,
            // sound loss). Dattorro plates keep their natural tail.
            { "Cathedral Large Hall", { 0.8778f, 0.9426f, 0.8631f, 1.0000f, 0.7319f, 0.5983f, 0.4500f, 0.4000f, 0.2200f } },  // 2026-07-03 8k 0.45->0.40 + 16k 0.36->0.22: ss-air ran +5.5 hot; closes ss-air/bloom 8-12k/flux/env_shape/decay_tail (20->17). Gentler 16k=0.28 scores WORSE (20) — the 12.9k spec residual is a DV notch, cutting around it trades better elsewhere.  // 2026-06-23 workflow: 4k/8k 0.58/0.59->0.45 (with BUILDUP) tames bloom 4-8k/8-12k
            { "Reverse Taps",         { 0.6750f, 0.8359f, 0.9478f, 0.3300f, 0.3600f, 0.3300f, 0.4000f, 0.7776f, 1.0000f } },  // 2026-07-03 mid cut 500/1k/2k/4k (0.46/0.52/0.45/0.48 -> 0.33/0.36/0.33/0.40): ss mid +5/upper-mid +5.3 hot vs anchor; closes sine1k + ss-deep-sub + spec means (27->25). 63-cut+8k-lift REGRESSES (sub-bass + ss-hi). Residual spec@80: DV mode ~64 Hz vs anchor ~80 Hz (+13.8/-11 dB adjacent bands) = velvet band-structure, not EQ.
            // strength-iterated (partial s) — gentler tables that rescue presets full strength over-corrected:
            { "Black Hole",           { 0.3911f, 0.4127f, 0.4340f, 0.4046f, 0.3763f, 0.3884f, 0.3867f, 0.4817f, 1.0000f } },
            { "Vintage Vocal Plate",  { 0.7424f, 0.9964f, 1.0000f, 0.9505f, 0.9176f, 0.8638f, 0.6995f, 0.5468f, 0.7873f } },
            { "Bright Hall",          { 0.9951f, 0.8682f, 0.8703f, 0.8686f, 0.8598f, 0.8872f, 0.5800f, 0.6800f, 1.0000f } },  // 2026-06-19 EAR + tone-match: 4k 0.85->0.58 + 8k 0.91->0.68 — DV ran +3.3dB over VVV at 5-10k; the match-EQ 4k/8k cut (the Treble param is dead-wired on DenseHall) flattens the whole 5-10k to within ±1.1dB of VVV (6.3-8k +2.4->+0.5).
            { "79 Vocal Chamber",     { 0.6672f, 0.6200f, 0.7059f, 0.7068f, 0.7097f, 0.7092f, 0.7763f, 0.5500f, 0.3505f } },  // 2026-06-23 workflow: 8k 1.0->0.55 (spec_L1 + tail-chorus, 18->16) + 125Hz 0.59->0.62 (body 125-250, 16->15)
            { "Small Drum Room",      { 0.9079f, 0.9794f, 0.8922f, 0.7830f, 0.7596f, 0.9453f, 1.0000f, 0.6000f, 0.1000f } },  // 2026-07-03 8k 0.689->0.60 + 16k 0.212->0.10: ss-air ran +12 dB hot (yet T60-16k short — loud-but-dying top). With Bass 0.85: 26->23. 8k 0.50 over-cuts (hi 4-12k breaks).
            // END_MATCHEQ_MAP
        }};
        float flat[9] = { 1,1,1,1,1,1,1,1,1 };
        if (const char* env = tuningEnv().matcheq; env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");
            if (t.size() == 9) { float g[9]; for (int k = 0; k < 9; ++k) g[k] = t[k].getFloatValue(); engine.setOutputMatchEQ (g); }
            else engine.setOutputMatchEQ (flat);
        }
        else if (const auto* me = findPresetConfig (kOutputMatchEQByName, std::string_view (name)))
            engine.setOutputMatchEQ (me->data());
        else
            engine.setOutputMatchEQ (flat);   // bit-null
    }

    // Output air-shelf — per-preset HF voicing BOOST (the only HF up-tilt lever;
    // the match-EQ above is cut-only). Post-tank feed-forward so boost is stable.
    // Env DUSKVERB_AIRSHELF="freqHz,gainDb" drives the rebuild-free sweep; else the
    // per-preset bake; else 0 dB → inactive → bit-null. Lifts the HF-deficient
    // plates/halls toward the bright references (cent_50 / cent_500 fleet audit).
    {
        struct AirShelf { float freqHz, gainDb; };
        // constexpr std::array + linear scan (NOT std::map) — applyEngineConfig runs on
        // the AUDIO thread; same RT-safe pattern as kDiffuseERByName.
        static constexpr std::array<std::pair<std::string_view, AirShelf>, 6> kOutputAirShelfByName = {{
            { "Vocal Hall",           { 9000.0f,   2.0f } },   // 2026-07-04 EAR "VVV richer and brighter": +2 dB @ 9k. Gates +1 (body 500-1k trades via gain-match) — ear-driven, the anchor IS brighter.
            // BEGIN_AIRSHELF_MAP (per-preset HF air-shelf, 2026-06-24 fleet cent match
            // vs anchors; env-swept DUSKVERB_AIRSHELF, then baked. {freqHz, gainDb})
            // The air-shelf is an HF-LEVEL lever — it only belongs where the deficit is
            // a genuine HF LEVEL gap (present-but-quiet HF, reachable, decay-neutral).
            // These 4 close BOTH centroid gates cleanly at moderate gain:
            { "Bright Hall",          { 4000.0f,   2.0f } },   // 2026-06-24 +7->+2: ear "too bright" + snare-tail tilt (anchor +0.83 dB/oct vs DV+7 +2.02) + VVV panel is DARK (HighShelf -24dB@6k, HiCut 8k). +7 over-tilted the tail; +2 keeps a touch of early lift. (was cent_50 -30.7->-7.9)
            { "Vintage Gold Plate",   { 5000.0f,   7.5f } },   // cent_50 -30.5->-5.4, cent_500 -19.5->+5.9
            { "Deep Blue Day",        { 3000.0f,   6.0f } },   // cent_50 -14.4->+7.0, cent_500 -29.1->-9.5
            { "Ambience",             { 3000.0f,  -5.0f } },   // bright-late: cent_500 +45.3->+5.1, cent_50 +12.5->-11.7
            { "Black Hole",           { 3000.0f,   2.5f } },   // 2026-06-29 gain 12->2.5 (EAR "DV brighter" + snare/noiseburst): +12 over-cranked the 4-12k presence ~9 dB hot vs Valhalla (snare HF tilt -6 vs -15) — harsh-bright. 2.5 matches the 4-12k presence (tilt ~-15 = Valhalla) -> n_fail 34->25. DV stays darker by CENTROID (no >12k air) — that's the 12 kHz pitch-shifter AA ceiling, structural. (Prev +12 chased cent_50 on the early window but over-brightened the band the ear hears.)
            // NOT here (cent_500 is a LATE-tail per-band-DECAY / low-mid-LEVEL problem the
            // air-shelf can't fix; fixed at the proper in-loop lever instead):
            //   Vocal Plate     -> AccurateHall low-mid LEVEL cut (pteqByName) — 63/250Hz +10dB hot
            //   Live Room       -> Dattorro per-octave HF-T60 (kDattorroOctaveT60ByName) — mids/HF decay 8-12dB too fast
            //   79 Vocal Chamber-> QuadTank qt_air_mult (was 0.5, inverted: air decayed slower than hi-mid)
            //   Large Chamber   -> early-transient dark (structural floor), shelf can't fix
            // END_AIRSHELF_MAP
        }};
        const char* env = tuningEnv().airshelf;
        if (env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");
            if (t.size() == 2) engine.setOutputAirShelf (t[0].getFloatValue(), t[1].getFloatValue());
            else               engine.setOutputAirShelf (8000.0f, 0.0f);
        }
        else if (const auto* e = findPresetConfig (kOutputAirShelfByName, std::string_view (name)))
            engine.setOutputAirShelf (e->freqHz, e->gainDb);
        else
            engine.setOutputAirShelf (8000.0f, 0.0f);   // bit-null
    }

    // Output low-shelf — per-preset deep-sub "fullness" BOOST (the LF counterpart of
    // the air-shelf; the boom gates start at 40Hz, leaving 20-40Hz uncovered, and a
    // preset's Lo Cut strips the deep weight the references keep). Post-tank feed-
    // forward so boost is stable. Env DUSKVERB_LOWSHELF="freqHz,gainDb" rebuild-free;
    // else the per-preset bake; else 0 dB → inactive → bit-null. Caught by the
    // deep-sub 20-40Hz full_check gate.
    {
        struct LowShelf { float freqHz, gainDb; };
        // constexpr std::array + linear scan (NOT std::map) — applyEngineConfig runs on
        // the AUDIO thread; same RT-safe pattern as kDiffuseERByName.
        static constexpr std::array<std::pair<std::string_view, LowShelf>, 2> kOutputLowShelfByName = {{
            // BEGIN_LOWSHELF_MAP (per-preset deep-sub low-shelf, 2026-06-25. {freqHz, gainDb})
            // Restores the 20-40Hz deep-sub "fullness" the references keep but DV's Lo Cut
            // strips. Closes the deep-sub gate cleanly (no boom regression):
            { "Bright Hall",          { 35.0f,  5.0f } },   // deep-sub -3.0 -> -0.9 (user: "fuller"); 35Hz corner keeps boost off the 40-100 boom -> no boom regression
            { "Cathedral Large Hall", { 45.0f,  8.0f } },   // deep-sub -6.5 -> -1.9, no boom
            // Blade Runner 224: NO low-shelf — its deep-sub deficit (-9.9dB) is too deep; any
            // boost big enough to close it over-booms 40-100Hz (coupled). Structural residual.
            // END_LOWSHELF_MAP
        }};
        const char* env = tuningEnv().lowshelf;
        if (env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");
            if (t.size() == 2) engine.setOutputLowShelf (t[0].getFloatValue(), t[1].getFloatValue());
            else               engine.setOutputLowShelf (60.0f, 0.0f);
        }
        else if (const auto* e = findPresetConfig (kOutputLowShelfByName, std::string_view (name)))
            engine.setOutputLowShelf (e->freqHz, e->gainDb);
        else
            engine.setOutputLowShelf (60.0f, 0.0f);   // bit-null
    }

    // Diffused discrete early reflections (DenseHall clarity / un-masking / two-taps):
    // a per-preset reflection comb (diffuse-then-tap → discrete-but-smooth). Env
    // DUSKVERB_DIFFER="diffusion,busGain,t1,g1,t2,g2,..." (ms,linear) drives the
    // rebuild-free sweep; else the per-preset bake; else empty → inactive → bit-null.
    // Caught by the early_refl_count + transient_def full_check gates.
    {
        struct DiffER { float diffusion, busGain; int n; float t[24]; float g[24]; };
        // constexpr std::array + linear scan (NOT std::map) — applyEngineConfig runs on
        // the AUDIO thread; same RT-safe pattern as kDenseHallOctaveT60ByName.
        static constexpr std::array<std::pair<std::string_view, DiffER>, 1> kDiffuseERByName = {{
            // BEGIN_DIFFER_MAP (per-preset discrete-ER comb, 2026-06-25; matched to anchor reflections)
            // Cathedral: VVV's discrete early-reflection comb (the "two distinct taps" /
            // clarity / un-masking). Closes early_refl_count (DV 1->9) + transient_def
            // (-3.2->+0.2), not metallic (HF kurtosis 13.7). COST: energy_first50/t50
            // open (+4 n_fail) — diffused bursts carry more energy than the anchor's
            // sharp reflections (front-load wall). EAR-CHECK: clarity vs energy balance.
            { "Cathedral Large Hall", { 0.6f, 0.3f, 9,
              { 16.0f, 31.0f, 40.0f, 56.0f, 70.0f, 81.0f, 101.0f, 117.0f, 126.0f },
              { 0.7f, 0.8f, 1.0f, 0.6f, 0.9f, 0.5f, 0.5f, 0.4f, 0.35f } } },
            // END_DIFFER_MAP
        }};
        float times[24] {}, gains[24] {}; int n = 0; float diffusion = 0.6f, busGain = 1.0f; bool have = false;
        const char* env = tuningEnv().differ;
        if (env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");
            if (t.size() >= 4)
            {
                diffusion = t[0].getFloatValue(); busGain = t[1].getFloatValue();
                for (int k = 2; k + 1 < t.size() && n < 24; k += 2)
                { times[n] = t[k].getFloatValue(); gains[n] = t[k + 1].getFloatValue(); ++n; }
                have = n > 0;
            }
        }
        else
        {
            const std::string_view nv (name);
            for (const auto& e : kDiffuseERByName)
                if (e.first == nv)
                {
                    const auto& d = e.second; diffusion = d.diffusion; busGain = d.busGain; n = d.n;
                    for (int i = 0; i < n && i < 24; ++i) { times[i] = d.t[i]; gains[i] = d.g[i]; }
                    have = n > 0; break;
                }
        }
        if (have) engine.setDiffuseER (times, gains, n, diffusion, busGain);
        else      engine.setDiffuseER (nullptr, nullptr, 0, 0.6f, 1.0f);   // bit-null
    }

    // Phase A early-field: tank-onset delay (DenseHall path). Env override
    // DUSKVERB_TANKONSET="ms" drives the make-or-break sweep without a rebuild;
    // else 0 (off → bit-null). Per-preset bake (kTankOnsetByName) comes later.
    {
        // Phase A front-loaded presets: tail delayed so the ER owns the early
        // window (moves energy_t50 toward the anchor). Front-loaded only — back-
        // loaded presets (Vocal Hall) need the complementary ER-boost (Phase B).
        // constexpr std::array + linear scan (NOT std::map) — RT-safe on the audio
        // thread; same pattern as kOutputAirShelfByName. See findPresetConfig().
        static constexpr std::array<std::pair<std::string_view, float>, 0> kTankOnsetByName = {{
            // REVERTED 2026-06-15 (ear: 100ms tail delay = audible ER->silence->tail
            // gap, "sounds like a delay"; the sparse ER can't bridge it). Phase-A
            // primitive kept (env/map-drivable) but no preset ships it until the ER
            // is dense enough to fill the gap (Phase B density-ramp).
        }};
        const char* env = tuningEnv().tankonset;
        float ms = 0.0f;
        if (env != nullptr && env[0] != '\0') ms = juce::String (env).getFloatValue();
        else if (const auto* it = findPresetConfig (kTankOnsetByName, std::string_view (name))) ms = *it;
        engine.setTankOnsetMs (ms);
    }

    // Per-band Width tilt (setWidthBands): independent low/mid/high stereo width.
    // The global frequency-flat Width traded bands net-negative on the fleet
    // (measured 2026-06-16); this sets each band to its anchor's L/R correlation.
    // Env override DUSKVERB_WIDTHBANDS="low,mid,hi" drives the sweep without a
    // rebuild; else the per-preset bake; else 1/1/1 (off → bit-null). 3-band gates:
    // width_low <300, width_mid .3-5k, width_hi >5k.
    {
        struct WB { float low, mid, hi; };
        // constexpr array (not std::map) — applyEngineConfig runs on the AUDIO
        // THREAD (performPresetSwap); a function-local static std::map would heap-
        // allocate its nodes on first init. A constexpr std::array has zero runtime
        // init and the linear scan over 7 entries is trivially cheap.
        //
        // 2026-06-16: per-band stereo-width tilt vs trusted anchors. The metric
        // is L/R CORRELATION (higher = more correlated/narrower); <1 narrows a
        // band, >1 widens it. These close the width_low/mid/hi + stereo_corr
        // gates the frequency-flat global Width could not (it traded bands).
        static constexpr std::array<std::pair<std::string_view, WB>, 11> kWidthBandsByName = {{
            { "Deep Blue Day",        { 1.00f, 1.00f, 0.50f } },
            { "Live Room",            { 1.00f, 0.20f, 0.20f } },  // 2026-07-04 vs the CORRECTED (fxp-truth) anchor: its mids/highs are near-MONO (corr +0.95) over a wide broadband tail; DV ran decorrelated (+0.10). Narrowing mid/hi side x0.2 passes all 3 width bands (with Decay 0.701->0.52: 40->32).  // 2026-07-03: narrow the >5k side — anchor has MONO-ISH highs (corr +0.95) over a decorrelated broadband tail; DV ran +0.77. 0.5 -> +0.87 = width-hi pass, zero collateral (25->24). Black Hole needs ~0.15 for the same flip but that breaks broadband stereo_corr — conflict case, not baked. Live Room's width-low is the INVERSE problem (anchor lows anti-correlated -0.85, DV near-mono) — scaling can't CREATE side energy; needs decorrelated low generation.
            { "Cathedral Large Hall", { 0.94f, 0.94f, 0.92f } },  // all 4 stereo gates → pass (22→19)
            { "Small Drum Room",      { 0.92f, 0.92f, 1.35f } },  // narrow lo/mid + WIDEN hi — conflict case (29→27)
            { "Bright Hall",          { 0.98f, 1.00f, 1.00f } },  // threads width-low vs stereo_corr (19→18)
            { "Blade Runner 224",     { 0.88f, 1.00f, 1.00f } },  // narrow low → width-low passes (29→28)
            { "Medium Drum Room",     { 1.12f, 1.12f, 1.00f } },  // WIDEN lo/mid → stereo_corr+width lo/mid (26→24)
            { "Reverse Taps",         { 0.50f, 0.88f, 0.88f } },  // strong narrow low + lo mid/hi (46→44)
            { "Vintage Vocal Plate",  { 0.85f, 1.00f, 1.00f } },  // narrow low → width-low passes (36→35). (2026-06-29: the 1.50 widen was front-load compensation; reverted with the front-load.)
            { "Vintage Gold Plate",   { 0.80f, 0.90f, 1.00f } },  // 2026-06-23 workflow: narrow low+mid → stereo_corr + width-low pass (14→12)
            { "Tiled Room",           { 0.88f, 1.00f, 1.00f } },  // 2026-06-23 workflow: narrow low → width-low pass, stereo_corr held (14→13)
            // No clean per-band fix (stereo fails trade / decouple): Drum Plate
            // (3 bands already match anchor; only broadband stereo_corr off),
            // Vocal Plate / 79 Vocal Chamber / Ambience / Large Chamber.
        }};
        float wl = 1.0f, wm = 1.0f, wh = 1.0f;
        const char* env = tuningEnv().widthbands;
        if (env != nullptr && env[0] != '\0')
        {
            auto toks = juce::StringArray::fromTokens (juce::String (env), ",", "");
            if (toks.size() == 3) { wl = toks[0].getFloatValue(); wm = toks[1].getFloatValue(); wh = toks[2].getFloatValue(); }
        }
        else
        {
            const std::string_view nameView (name);
            for (const auto& e : kWidthBandsByName)
                if (e.first == nameView) { wl = e.second.low; wm = e.second.mid; wh = e.second.hi; break; }
        }
        engine.setWidthBands (wl, wm, wh);
    }

    // Phase 3 (VH->0): per-line energy-following hi-shelf (TimeVaryingDamping).
    // VH only; every other preset gets a FLAT shelf (earlyMult==lateMult==1) →
    // tvHiActive_ false → call skipped in the loop → bit-identical. Direct engine
    // call (no APVTS param) so there is no edge-detect desync (cf. PostTankEQ).
    // earlyMult 0.5 cuts HF -6 dB while a line's energy is fresh → faster EARLY
    // hi decay (targets edt hi +78%); relaxes to flat as energy decays → late
    // T60 16k preserved. release 0.30 s shapes the early-tail window.
    // VH edt hi (+78%) does NOT close via this in-loop high-shelf: edt is a
    // decay-SLOPE (t10) metric governed by the same band energy as T60 2-8k, so
    // every cut that shortens edt hi also shortens T60 4k/8k/16k (within-band
    // edt-vs-T60 coupling; best achieved edt hi ~56%, n_fail 9 > baseline 8).
    // Left FLAT (bypass) for all presets — infra retained, bit-null-verified,
    // available for edt low_mid (low-shelf) or other presets. (2026-06-07)
    engine.setFDNTimeVaryingHiDamp (1.0f, 1.0f, 2000.0f, 0.45f, 3.0f);

    // hi_cut_shelf_db now flows through APVTS (set in FactoryPreset::applyTo),
    // so no explicit engine setter call here.

    // Phase 2 modulation topology — per-preset opt-in via name lookup so we
    // don't have to add a new field to FactoryPreset (would break the
    // aggregate-initializability of the brace-init preset list).
    // Names that aren't listed get RandomWalk (legacy bit-identical).
    // constexpr std::array + linear scan (NOT std::unordered_map) — RT-safe on the
    // audio thread (applyEngineConfig runs in performPresetSwap). See findPresetConfig().
    static constexpr std::array<std::pair<std::string_view, DspUtils::ModulationTopology>, 4> kTopologyByName = {{
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
        // Drum Plate -> RandomWalk (2026-06-10, tail-cleanliness review): under
        // ModulatedDamping the Mod Rate/Depth knobs are inert (fixed slow
        // drift), so the tail could not smear its comb modes — ripple +3..+3.8
        // dB rougher than the anchor and pitch wander 4.3 Hz vs the anchor
        // plate's 16.5 Hz. RandomWalk enables real line modulation; depth/rate
        // tuned to match the anchor's chorused-smooth tail.
        { "Drum Plate",           DspUtils::ModulationTopology::RandomWalk },
        // Vocal Hall — REVERTED to RandomWalk default on 2026-05-29 with
        // the v15 preset-row rollback. ModulatedDamping was added as a
        // Phase 3 anti-Doppler fix layered on top of v18-manual params;
        // with VH now back on v15, fall through to the default and let
        // the per-line random LFOs handle modulation as they did when
        // the FDN path was last known stable.
    }};
    DspUtils::ModulationTopology topo = DspUtils::ModulationTopology::RandomWalk;
    if (const auto* p = findPresetConfig (kTopologyByName, std::string_view (name)))
        topo = *p;
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
    static constexpr std::array<std::pair<std::string_view, DecayTiltConfig>, 0> kDecayTiltByName = {{
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
        // bass-extension benefit at less aggressive level dump. (The disabled
        // per-preset tilt rows that documented those audition values were removed
        // 2026-06-28 — the rationale above stands on its own; tilt is off fleet-wide.)
    }};
    float shortS = 1.0f, longS = 1.0f;
    if (const auto* p = findPresetConfig (kDecayTiltByName, std::string_view (name)))
    {
        shortS = p->shortLine;
        longS  = p->longLine;
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
    // 32 entries: the 16-line engines (fdn_/accurateHall_) read [0..15]; the
    // 32-line accurateHall32_ reads all 32. setFDNBaseDelays forwards ONE pointer
    // to every engine, so accurateHall32_.setBaseDelays always reads 32 — every
    // entry MUST define all 32 (else OOB). 16-line presets pad [16..31] with the
    // engine default tail primes (dormant: their active engine never reads them).
    struct BaseDelaySet { int delays[32]; };
    static constexpr std::array<std::pair<std::string_view, BaseDelaySet>, 5> kBaseDelaysByName = {{
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
                            3069, 3253, 3588, 4086, 4178, 5043, 6014, 6414,
                            // [16..31] dormant (Vocal Hall is 16-line algo 10):
                            1213, 1361, 1531, 1721, 1933, 2153, 2417, 2713,
                            3041, 3413, 3833, 4297, 4817, 5417, 6079, 6469 } } },
        // Bright Hall (AccurateHall32 algo 12, joint_dense32_sweep.py trial 76,
        // 2026-06-11): 32 prime delays, mean 3128 ~ the 16-line baseline mean 3121
        // (T60 held). Joint objective minimized full_check n_fail with a kurtosis
        // hold; after per-set octave-T60 recalibration this set scores n_fail 13
        // (baseline 16) AND tail 2-14k spectral kurtosis ~18 -> 14.9 (anchor 12.0,
        // the metallic ring) — a strict improvement on both axes. All 32 lines
        // active (AccurateHall32 reads all 32). Octave T60 in kAccurateHallT60ByName.
        { "Bright Hall", { { 1103, 1249, 1327, 1361, 1531, 1543, 1549, 1637,
                             1861, 1879, 2053, 2137, 2251, 2371, 2531, 2609,
                             2741, 3109, 3163, 3371, 3659, 3761, 3943, 4153,
                             4397, 4721, 4861, 5297, 5431, 5839, 6151, 6521 } } },
        // Ambience / Medium Drum Room (2026-06-10 ripple_delay_sweep): the
        // default log-spaced primes pushed the per-band envelope beats to
        // 4-8 Hz (high tail-mod-ripple std); these sets align the beats to
        // each anchor's slow rate, closing the ripple gate (Ambience high
        // +1.58->-0.04; MDR bass +2.56->-0.01) with all four bands passing.
        { "Ambience", { { 792, 970, 986, 1394, 1474, 2066, 2630, 2735,
                          3245, 3881, 4084, 4771, 4802, 5151, 6083, 6164,
                          1213, 1361, 1531, 1721, 1933, 2153, 2417, 2713,
                          3041, 3413, 3833, 4297, 4817, 5417, 6079, 6469 } } },
        { "Medium Drum Room", { { 967, 1110, 1195, 1226, 2190, 2376, 2945, 3198,
                                 3515, 3624, 4893, 5131, 5333, 5802, 6177, 6402,
                                 1213, 1361, 1531, 1721, 1933, 2153, 2417, 2713,
                                 3041, 3413, 3833, 4297, 4817, 5417, 6079, 6469 } } },
        // Drum Plate (2026-06-10 ripple_delay_sweep, ss term off — no sustained
        // anchor, ss_ref would be noiseburst noise floor): closes lowmid +2.68
        // and high +3.57 (the failing bands) while holding bass/mid + octave T60.
        { "Drum Plate", { { 813, 1147, 1447, 1476, 2306, 2392, 2638, 3313,
                           4055, 4594, 5184, 5202, 5808, 6246, 6423, 6678,
                           1213, 1361, 1531, 1721, 1933, 2153, 2417, 2713,
                           3041, 3413, 3833, 4297, 4817, 5417, 6079, 6469 } } },
    }};

    // ─── Phase γ: per-preset post-tank band-trim region gains ────────────
    // 4 floats per preset (Sub / LowMid / MidHi / Air) in dB. Defaults 0 →
    // unity-coefficient shelves + 1.0 makeup gain → bit-identical bypass.
    // Presets not in the map skip the call entirely (APVTS-default 0 dB
    // already pushed via forcePushAllParametersTo at swap time).
    struct PostBandTrimConfig { float sub, lowMid, midHi, air; };
    static constexpr std::array<std::pair<std::string_view, PostBandTrimConfig>, 5> kPostBandTrimByName = {{
        // Vocal Hall (front-load campaign 2026-06-08): PostTankBandTrim sub/low
        // lift TRIED to refill the cold sustained low left by the tank_level 0.42
        // front-load cut, but a flat region lift pushes boom-sub + snare level
        // HOT (ss-sub steady wants lift; boom-sub late + broadband don't) → net
        // WORSE (13-18 vs 11). Left at unity. The cold low is the front-load
        // tradeoff; see memory duskverb_energy_arrival_gate_and_wall.
        // (Prior) Vocal Hall post-band trim override removed on 2026-05-29 v15
        // revert. The +1/+2/-1 dB lift was a v18-manual presence rescue;
        // v15 baseline does not need it.
        // Bright Hall (VintageTank V2.0): PostBandTrim disabled — the round-4
        // sweep ran with the FDN-era preset row, where PostBandTrim cuts
        // helped. Under the new VintageTank row (baked round-3 axes), the
        // same cuts dropped 13 additional gates net. Leaving PostBandTrim
        // at unity (no map entry) gives the cleanest current result.
        // (Medium Drum Room UltraRoom decouple FALSIFIED — the static post-loop
        // trim couples to boom/body within-band; best 24 vs FDN 25. Reverted.)
        // Medium Drum Room (Dattorro re-engine, 2026-06-11): zeros = bit-null
        // placeholder that OPTS the preset into the DUSKVERB_BANDTRIM sweep
        // hook (the env override is gated on map membership). The post-loop
        // trim is the orthogonal level lever vs the in-loop coupling wall.
        { "Medium Drum Room", { 0.0f, 0.0f, 0.0f, 0.0f } },
        // Ambience (2026-06-11): zeros = bit-null placeholder unlocking the
        // DUSKVERB_BANDTRIM sweep hook for the boom-low post-loop cut.
        { "Ambience",         { 0.27389f, -4.81217f, -0.51326f, -2.40095f } },  // boom-low cut + 640 (lowMid -4.8) + air
        { "Blade Runner 224", { -1.05804f, -0.69518f, -1.16656f, -0.68592f } },  // mild broadband post-trim (sweep)
        { "Cathedral Large Hall", { -3.42900f, -3.50000f, -0.35222f, -4.19473f } },  // 2026-06-13: low-mid cut -7.9->-3.5 — the deep scoop hollowed the body + deepened the post-transient energy hole (pumping)
        // 79 Vocal Chamber 2026-06-19 (ear: "EQ/tone a little off"): the residual
        // 1/3-oct curve vs VVV = +5dB@100 (thick low) / −2.7@315 (over-scoop) /
        // −5dB@5-8k (dull presence). 4-region fix over crossovers 130/3500/9000:
        // Sub(≤130) −3.5 cuts the 100 bump; LowMid(130-3500) −1.5 keeps the hump tamed
        // without the 315 over-scoop; MidHi(3500-9000) +2.5 lifts the 5-8k presence;
        // Air(>9000) 0 (the >10k is the measurement ghost, leave).
        { "79 Vocal Chamber", { 0.0f, -2.5f, 0.0f, 0.0f } },
        // Bright Hall 2026-06-19 (AccurateHall32): HF cut TRIED + reverted. ss hi 5-10k
        // +3.7 (DV tail brighter than VVV) is a normalized-SHAPE excess — the post-band
        // trim is renorm-compensated (−2.5dB band cut nets only −0.8dB ss hi, the
        // gain-match scales it back) AND it darkens cent_500 away from the anchor
        // (tap-only cent −2.6% is the closest match). The 5-10k + spec_L1 @12.9k are the
        // HF-tilt SHAPE wall (no engine lever). Left at unity (no map entry); only the
        // reflection tap is baked. See duskverb_fleet_hf_tilt_wall.
        // 79 Vocal Chamber HF dig 2026-06-18 (NO clean fix): the >10k is hot+long
        // (ss_air +11, T60-16k +86%) but 5-10k is QUIET (ss_hi -6) — an HF-SHAPE
        // inversion. The QuadTank air mult (decay+level couple down) and the post-band
        // Air trim (region spans 5-10k → crushes the quiet band) both move the whole
        // upper region together; the fix needs an HF DOWN-TILT (+5-10k / ->10k) no
        // QuadTank lever provides. Left at the shipped air mult 0.5 (gate-best, 11).
        // HF-TILT campaign 2026-06-18 REVERTED — a static output HF shelf brightens
        // cent (early window) but DV's HF arrives LATE (early-dark + sustained-hot),
        // so it pushes the sustained ss bands hot → n_fail rose +2..+6 on every
        // lifted preset (Vocal Plate 17→23, Live 27→30, LC 19→21, etc.). Net WORSE
        // for "all gates within JND". The darkness needs an EARLY-FIELD-ONLY HF lift
        // (brighten the onset, leave the tail) — a real engine add, not a shelf. See
        // memory duskverb_fleet_hf_tilt_wall. Crossover infra kept (kPostBandCross,
        // bit-null when unused) for that future early-only lever.
    }};
    PostBandTrimConfig pbt { 0.0f, 0.0f, 0.0f, 0.0f };
    const auto* pbtEntry = findPresetConfig (kPostBandTrimByName, std::string_view (name));
    if (pbtEntry != nullptr)
        pbt = *pbtEntry;
    // Sweep override: DUSKVERB_BANDTRIM = "sub,lowMid,midHi,air" dB (the UltraRoom
    // decouple level lever). Rebuild-free; only the listed preset opts in.
    const char* envBt = tuningEnv().bandtrim;
    if (envBt != nullptr && envBt[0] != '\0' && pbtEntry != nullptr)
    {
        juce::StringArray t; t.addTokens (juce::String (envBt), ",", "");
        if (t.size() == 4)
            pbt = { t[0].getFloatValue(), t[1].getFloatValue(),
                    t[2].getFloatValue(), t[3].getFloatValue() };
    }
    // Always set (defensive: don't inherit a prior opt-in preset's trims).
    engine.setPostTankBandTrimGainDb (0, pbt.sub);
    engine.setPostTankBandTrimGainDb (1, pbt.lowMid);
    engine.setPostTankBandTrimGainDb (2, pbt.midHi);
    engine.setPostTankBandTrimGainDb (3, pbt.air);

    // Per-preset post-band crossovers. Default 200/800/3000 (Air=>3k = the whole HF
    // block) — but the HF-TILT presets use an HF-split 200/2000/9000 so MidHi isolates
    // the 2-9k presence and Air isolates >9k, giving an independent presence-vs-air
    // tilt (the lever the fleet HF deficiency + 79VC's inversion need). Unlisted →
    // the legacy 200/800/3000, so every existing baked trim is byte-identical.
    struct PBCross { float fLow, fMid, fHi; };
    static constexpr std::array<std::pair<std::string_view, PBCross>, 1> kPostBandCrossByName = {{
        { "79 Vocal Chamber", { 100.0f, 1000.0f, 3000.0f } },  // LowMid=100-1000 isolates the low-mid hump
    }};
    float cxLo = 200.0f, cxMid = 800.0f, cxHi = 3000.0f;
    { const std::string_view nv (name);
      for (const auto& e : kPostBandCrossByName) if (e.first == nv) { cxLo=e.second.fLow; cxMid=e.second.fMid; cxHi=e.second.fHi; break; } }
    engine.setPostTankBandTrimCrossovers (cxLo, cxMid, cxHi);

    // ─── Tank-feed EQ (Progenitor 'inputdamp', 2026-06-11) ────────────────
    // Low+high shelves on the TANK FEED only (post-diffuser; the parallel ER
    // taps earlier and stays bright). Feed-forward → per-band T60 untouched;
    // moves the late-field spectrum the in-loop/post-sum levers couldn't.
    // 0 dB gains (unlisted presets) → engine skips the branch → bit-identical.
    // Direct engine call (no APVTS → no edge-detect desync). Sweep override:
    // DUSKVERB_TANKFEED = "lowFc,lowDb,highFc,highDb".
    {
        struct TankFeedConfig { float lowFc, lowDb, highFc, highDb; };
        static constexpr std::array<std::pair<std::string_view, TankFeedConfig>, 1> kTankFeedEQByName = {{
            // BEGIN_TANKFEED_MAP (maintained by tools/tuner/mdr_progenitor_sweep.py)
            { "Medium Drum Room", { 250.0f, -0.16244f, 3736.08f, -0.84836f } },
            // END_TANKFEED_MAP
        }};
        TankFeedConfig tf { 200.0f, 0.0f, 2500.0f, 0.0f };
        if (const auto* p = findPresetConfig (kTankFeedEQByName, std::string_view (name)))
            tf = *p;
        if (const char* envTf = tuningEnv().tankfeed; envTf != nullptr && envTf[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (envTf), ",", "");
            if (t.size() == 4)
                tf = { t[0].getFloatValue(), t[1].getFloatValue(),
                       t[2].getFloatValue(), t[3].getFloatValue() };
        }
        engine.setTankFeedEQ (tf.lowFc, tf.lowDb, tf.highFc, tf.highDb);
    }

    // ─── Dattorro density-AP jitter (per-preset, 2026-06-11) ──────────────
    // The hardcoded 0.02 anti-ring wander is a time-varying in-loop element:
    // it FM-scatters tail energy broadband every pass, creating a flat late-
    // window HF plateau (~35 dB above a dark anchor's floor) that no static
    // EQ can remove + driving tail pitch-chorus. Dark short rooms can reduce
    // it. Unlisted presets keep 0.02 → bit-identical. Sweep override:
    // DUSKVERB_DENSJIT = "<fraction>".
    {
        struct DensityJitterConfig { float fraction; };
        static constexpr std::array<std::pair<std::string_view, DensityJitterConfig>, 2> kDensityJitterByName = {{
            // BEGIN_DENSJIT_MAP (maintained by tools/tuner/mdr_progenitor_sweep.py)
            { "Medium Drum Room", { 0.00474f } },
            // END_DENSJIT_MAP
            { "Vintage Gold Plate", { 0.0f } },   // 2026-06-13: kill the 2% density wander — it was the audible tail chorus (renamed from "Studio Plate")
        }};
        float dj = 0.02f;
        if (const auto* p = findPresetConfig (kDensityJitterByName, std::string_view (name)))
            dj = p->fraction;
        // NB: the DUSKVERB_* env-override checks throughout applyEngineConfig (densjit,
        // dens, maindet, matcheq, octt60, …) exist ONLY for the offline rebuild-free
        // tuning sweeps. In a normal/shipping session every getenv() returns null so
        // these branches are skipped entirely — the juce::StringArray::addTokens heap
        // allocations they perform are never reached at runtime (acceptable: dev-only).
        if (const char* envDj = tuningEnv().densjit; envDj != nullptr && envDj[0] != '\0')
            dj = juce::String (envDj).getFloatValue();
        engine.setDattorroDensityJitter (dj);
    }

    // In-loop mode notch (Dattorro tank) — narrow peaking CUT inside the
    // recirculation, compounding per pass: shortens ONE mode's decay (the room
    // "boing": Live 211 Hz, Medium 334, Small 963). An OUTPUT notch only cuts
    // level and the boing hops to the next mode; in-loop attacks its T60.
    // Env DUSKVERB_DATNOTCH="hz,cutDb,Q"; else per-preset bake; else off/bit-null.
    {
        struct MN { float hz, cutDb, q; };
        static constexpr std::array<std::pair<std::string_view, MN>, 1> kModeNotchByName = {{
            // BEGIN_DATNOTCH_MAP
            // 2026-07-03: wide gentle cut centered between the 211/247 Hz mode pair —
            // boing prominence 39.2 -> 26.5 dB (Δ vs anchor +20 -> +7.3, detector hops
            // to the weaker 317 mode) + ss-low-100-250 passes: 28 -> 27. Sharper/deeper
            // variants kill 211 harder but trade body 125-250 worse. Ported probes:
            // SDR 963 (hops to a LOUDER 1925 mode), MDR 334 (marginal), VGP 525
            // (T60-500/edt break) — all net-0 or worse, not baked.
            { "Live Room", { 228.0f, -4.0f, 3.5f } },
            // END_DATNOTCH_MAP
        }};
        float mhz = 0.0f, mcut = 0.0f, mq = 8.0f;
        if (const char* env = tuningEnv().datnotch; env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");
            if (t.size() >= 2) { mhz = t[0].getFloatValue(); mcut = t[1].getFloatValue(); }
            if (t.size() >= 3) mq = t[2].getFloatValue();
        }
        else
        {
            const std::string_view nv (name);
            for (const auto& e : kModeNotchByName)
                if (! e.first.empty() && e.first == nv) { mhz = e.second.hz; mcut = e.second.cutDb; mq = e.second.q; break; }
        }
        engine.setDattorroModeNotch (mhz, mcut, mq);
    }

    // DenseHall LOW ACCUMULATION LIMITER — drive-following sub-band charge
    // limiter (piano-gate defect: Vocal Hall 62 Hz grew +6.8 dB over the 22 s
    // piano stem; impulse tails never reach the threshold so noiseburst T60
    // gates are untouched). Env DUSKVERB_DHLOWLIM="threshDb,maxCut,splitHz";
    // else per-preset bake; else off/bit-null.
    {
        struct LL { float threshDb, maxCut, splitHz; };
        static constexpr std::array<std::pair<std::string_view, LL>, 1> kDhLowLimByName = {{
            // BEGIN_DHLOWLIM_MAP
            { "", { 0.0f, 0.0f, 90.0f } },   // placeholder — filled after env sweeps
            // END_DHLOWLIM_MAP
        }};
        float llT = 0.0f, llC = 0.0f, llH = 90.0f;
        if (const char* env = tuningEnv().dhlowlim; env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");
            if (t.size() >= 2) { llT = t[0].getFloatValue(); llC = t[1].getFloatValue(); }
            if (t.size() >= 3) llH = t[2].getFloatValue();
        }
        else
        {
            const std::string_view nv (name);
            for (const auto& e : kDhLowLimByName)
                if (! e.first.empty() && e.first == nv) { llT = e.second.threshDb; llC = e.second.maxCut; llH = e.second.splitHz; break; }
        }
        engine.setDenseHallLowAccumLimiter (llT, llC, llH);
    }

    // ParallelMultiband (algo 15) per-band table — {t60, level, direct, width}
    // per band (6 bands: <120 / 120-350 / 350-1k / 1k-3k / 3k-8k / >8k).
    // Env DUSKVERB_PMB="t60x6;levelx6;directx6;widthx6" (4 groups of 6, ';'
    // between groups, ',' within); else the per-preset bake; else engine
    // defaults. No-op on every other engine.
    {
        struct PMB { float t60[6], lvl[6], dir[6], wid[6]; };
        static constexpr std::array<std::pair<std::string_view, PMB>, 1> kPmbByName = {{
            // BEGIN_PMB_MAP
            { "", { {2,2.2f,2,1.6f,1.2f,0.8f}, {1,1,1,1,1,1}, {0,0,0,0,0,0}, {1,1,1,1,1,1} } },   // placeholder
            // END_PMB_MAP
        }};
        float t60v[6] = {2,2.2f,2,1.6f,1.2f,0.8f}, lvlv[6] = {1,1,1,1,1,1}, dirv[6] = {0,0,0,0,0,0}, widv[6] = {1,1,1,1,1,1};
        bool have = false;
        if (const char* env = tuningEnv().pmb; env != nullptr && env[0] != '\0')
        {
            juce::StringArray groups; groups.addTokens (juce::String (env), ";", "");
            float* dst[4] = { t60v, lvlv, dirv, widv };
            for (int gI = 0; gI < 4 && gI < groups.size(); ++gI)
            {
                juce::StringArray t; t.addTokens (groups[gI], ",", "");
                for (int k = 0; k < 6 && k < t.size(); ++k) dst[gI][k] = t[k].getFloatValue();
            }
            have = true;
        }
        else
        {
            const std::string_view nv (name);
            for (const auto& e : kPmbByName)
                if (! e.first.empty() && e.first == nv)
                {
                    for (int k = 0; k < 6; ++k) { t60v[k] = e.second.t60[k]; lvlv[k] = e.second.lvl[k]; dirv[k] = e.second.dir[k]; widv[k] = e.second.wid[k]; }
                    have = true; break;
                }
        }
        if (have)
            for (int k = 0; k < 6; ++k)
                engine.setPmbBand (k, t60v[k], lvlv[k], dirv[k], widv[k]);
    }

    // Plate density rework (algo 0 Dattorro + algo 1 DattorroPlateVintage):
    //   density  (0..1): 0 = legacy 3 density APs; >0 = dense 6 APs + coeff lift
    //                    (more echoes/sec = smoother tail, no added modulation).
    //   modred   (0..1): 1.0 = legacy modulation; <1.0 pulls AP1+delay mod toward
    //                    still (the Lex/VVV near-static tail).
    // Defaults (0 / 1.0) = byte-identical legacy. Env DUSKVERB_DENS/DUSKVERB_MODRED
    // drive the no-rebuild sweep; else the per-preset map; else legacy.
    {
        // density: 6 in-loop APs (late-tail density). indiff: input-diffuser coeff
        // scale (0 = OFF/bit-null; 1.0 = canonical = the first-60ms kurtosis fix).
        // modred: pulls the tail toward still. All three DECOUPLED.
        // softonset: tank output soft-onset ramp (ms; 0 = instant = bit-null).
        // Slows the early-field attack toward a slow-swell anchor (e.g. the Lex
        // vintage vocal plate's ~90ms attack) without touching the tail.
        // bloom: input-onset slow-attack swell (ms; 0 = off). For slow-bloom
        // anchors (Lex vintage vocal plate ~90ms attack-to-peak).
        // #87 boing fix: roomfill loads the hall density bases onto the room-scale
        // main lines (close-spaced coprime modes → kills the comb resonance); det[4]
        // = per-line incommensurate detune {Ldel1,Ldel2,Rdel1,Rdel2}. Defaults
        // (false / identity) = byte-identical for every non-room Dattorro preset.
        struct DensityConfig { float density = 0.0f, modred = 1.0f, indiff = 0.0f, softonset = 0.0f, bloom = 0.0f, bloomExp = 1.0f;
                               bool roomfill = false; float det[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; };
        static constexpr std::array<std::pair<std::string_view, DensityConfig>, 3> kDattorroDensityByName = {{
            // BEGIN_DATTDENS_MAP (offline density/modred/indiff sweep, key on exact row name)
            // Drum Plate: density 1.0 (12 in-loop APs) + Size up (row 0.337->0.8)
            // densify the modes → kill the ~360Hz tail "boing" (19.5->~15dB =
            // VVV). The octave GEQ holds T60 independent of the longer loop.
            // indiff 1.0 keeps the first-60ms kurtosis fix.
            { "Drum Plate", { 1.0f /*density*/, 1.0f /*modred*/, 1.0f /*indiff*/ } },
            // Vintage Vocal Plate: input diffusion densifies the early field
            // (24->22 vs the corrected anchor); octave GEQ left off (3-band path).
            { "Vintage Vocal Plate", { 0.0f /*density*/, 1.0f /*modred*/, 1.0f /*indiff*/, 0.0f /*softonset*/, 90.0f /*bloom*/, 2.0f /*bloomExp*/ } },  // 2026-06-29 density REVERTED to 0: the 12-AP cascade SHORTENED the HF tail (T60-16k 0.41->0.25 = dull top); the structHfDamp 9000 fix now supplies fullness (tailFlat 0.24 > anchor 0.20) AND the crispy HF sustain (16k 0.59 vs anchor 0.55) the EAR wants. + bloom-attack 90ms/exp 2.0: slow-build swell back-loads the onset WITHOUT pre-echo -> energy_t50/first50/transient_def/impulse-RMS. attack_time caps ~70ms; sine1k = 1k modal wall.
            // Vintage Gold Plate: density 1.0 engages the full 12-AP in-loop
            // cascade — more modes = shallower spectral comb (ripple 6.2->~2).
            // Pairs with the octave GEQ (re-calibrated for the longer loop).
            { "Vintage Gold Plate", { 1.0f /*density*/, 1.0f /*modred*/, 0.0f /*indiff*/ } },
            // #87 boing fix attempt (short rooms) REVERTED 2026-06-17: density 1.0 +
            // roomfill (hall density bases) DOES kill the boing (Small 10->0dB at long
            // decay) BUT the 12 hall APs make the loop ~200ms — gBase saturates near
            // its clamp floor → short-room T60 is uncontrollable (collapses -60% at the
            // row decay, runs away +200-600% when compensated, non-monotonic). Dead end:
            // dense low-mid modes need long density APs need a long loop, which a SHORT
            // room's decay can't sustain. The infra (setDensityRoomFill/setMainLineDetune
            // + DUSKVERB_ROOMFILL/MAINDET) stays — bit-null when unused — but the rooms
            // need ENGINE MIGRATION (FDN 16-line / velvet, dense modes w/o a runaway loop)
            // or accept the boing. See memory duskverb_boing_sparse_modal.
            // END_DATTDENS_MAP
        }};
        float density = 0.0f, modred = 1.0f, indiff = 0.0f, softonset = 0.0f, bloom = 0.0f, bloomExp = 1.0f;
        bool  roomfill = false; float det[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (const auto* p = findPresetConfig (kDattorroDensityByName, std::string_view (name)))
        { density = p->density; modred = p->modred; indiff = p->indiff; softonset = p->softonset; bloom = p->bloom; bloomExp = p->bloomExp;
          roomfill = p->roomfill; for (int b = 0; b < 4; ++b) det[b] = p->det[b]; }
        if (const char* e = tuningEnv().dens;   e != nullptr && e[0] != '\0') density   = juce::String (e).getFloatValue();
        if (const char* e = tuningEnv().modred; e != nullptr && e[0] != '\0') modred    = juce::String (e).getFloatValue();
        if (const char* e = tuningEnv().indiff; e != nullptr && e[0] != '\0') indiff    = juce::String (e).getFloatValue();
        if (const char* e = tuningEnv().softonset; e != nullptr && e[0] != '\0') softonset = juce::String (e).getFloatValue();
        if (const char* e = tuningEnv().bloom; e != nullptr && e[0] != '\0') bloom     = juce::String (e).getFloatValue();
        // #87 env overrides for no-rebuild tuning.
        if (const char* e = tuningEnv().roomfill; e != nullptr && e[0] != '\0') roomfill = (e[0] == '1');
        if (const char* e = tuningEnv().maindet; e != nullptr && e[0] != '\0')
        { juce::StringArray t; t.addTokens (juce::String (e), ",", "");
          if (t.size() == 4) for (int b = 0; b < 4; ++b) det[b] = t[b].getFloatValue(); }
        engine.setDattorroDensity (density);
        engine.setDattorroModReduction (modred);
        engine.setDattorroInputDiffusion (indiff);
        engine.setDattorroSoftOnsetMs (softonset);
        engine.setDattorroBloomAttackMs (bloom);
        engine.setDattorroDensityRoomFill (roomfill);
        engine.setDattorroMainLineDetune (det[0], det[1], det[2], det[3]);
        engine.setDattorroBloomExp (bloomExp);
        if (const char* e = tuningEnv().bloomexp; e != nullptr && e[0] != '\0') engine.setDattorroBloomExp (juce::String (e).getFloatValue());
    }

    // Per-octave GEQ damping for the plate (algo 0 Dattorro + algo 1 DPV). The
    // AccurateHall mechanism ported into the tank loop: nine ISO-octave T60
    // plateaus set independently, breaking the 3-band-vs-9-octave T60 wall the
    // ThreeBandDamping cannot pass. t60[k] = the corrected-anchor per-octave RT60
    // (63|125|250|500|1k|2k|4k|8k|16k Hz). decayRef = the baked Decay knob so the
    // knob scales the whole curve (scale = knob/ref). Unlisted presets → all-zero
    // → octave GEQ inactive → byte-identical legacy 3-band.
    {
        struct OctaveT60Config { float t60[9]; float decayRef; };
        static constexpr std::array<std::pair<std::string_view, OctaveT60Config>, 3> kDattorroOctaveT60ByName = {{
            // Re-tuned 2026-06-15 vs the CORRECTED LexVintagePlate anchors. Values
            // are the CALIBRATED commanded T60s (Newton-corrected so the REALIZED
            // per-octave RT60 lands within ±5% JND of the anchor — the raw anchor
            // T60s overshoot the mids ~+30% due to in-loop shelf-cascade leakage).
            // Gold: octave GEQ ON (its mixed-sign 7-band T60 needs it). Vocal NOT
            // listed — its short uniform decay is better served by the 3-band path
            // (octave disrupted its decay-rate/boom gates: 24->27); Vocal uses
            // Decay 0.50 (row) + input diffusion (kDattorroDensityByName) instead.
            { "Vintage Gold Plate",  { { 1.4928f, 2.0271f, 1.5359f, 1.5653f, 1.5761f, 1.5187f, 1.4181f, 1.3007f, 1.2370f }, 1.961f } },
            // Drum Plate: octave GEQ fixes per-octave T60 (the low-band decay/boom
            // the user heard as "too much bass" = hot-but-short low band, the
            // coupling wall the 3-band couldn't fix). 8/9 bands within JND; 16k
            // capped by Hi Cut 8kHz (anchor 1.39s @16k is near-floor). decayRef =
            // Drum row decay 2.263.
            { "Drum Plate",          { { 1.4647f, 1.8203f, 1.4117f, 1.6577f, 1.5226f, 1.4977f, 1.5204f, 1.6865f, 6.4223f }, 1.691f } },  // recal for Size 0.8 + 12-AP loop
            // Vintage Vocal Plate 2026-06-19: octave GEQ ON (was 3-band — the prior
            // "octave regressed 24->27" used RAW anchor T60s; these are Newton-corrected,
            // realized within JND). Decouples the T60 coupling wall: 3-band ran the LOW
            // too long (63Hz 0.89->0.61 anchor) and the TOP dead (16k 0.35->0.55). All
            // 9 bands now within ±10%. 16k commanded 1.21 beats the AA one-pole to reach
            // 0.55. NB low bands over-shorten the sustained sub decay-shape (decay-sub<100
            // -41%): within-band exponential-vs-anchor mismatch, a structural residual.
            // decayRef = VVP row Decay 0.50. Low bands (63/125) kept LONGER on purpose:
            // shortening them to pass T60-63 (Schroeder) broke the sustained decay-sub/
            // boom-sub/boom-low cluster (~5 gates) — the within-band exponential-vs-anchor
            // shape conflict. 63=0.65 is the boom-sub zero-crossing: decay-sub/decay-low +
            // boom-sub + boom-low ×2 all PASS, costing only T60-63/125 (the accepted trade).
            // Mid/high (4k 0.595, 16k 1.21 beats the AA one-pole) fixed cleanly. n_fail 29->22.
            // 2026-06-20 EAR "tail cut off / increase decay": decayRef 0.50->0.43
            // scales the whole octave curve ×1.16 → broadband tail_t60 0.75->0.86s
            // (the old 0.75s died before the Lexicon's audible ~0.93s = "cut off").
            // This OVERSHOOTS the per-band T60 gates (anchor per-band 0.6-0.8s) — but
            // the anchor's BROADBAND decay (0.93s) exceeds its own max per-band (0.80s),
            // so per-band-match and audible-broadband-match are mutually exclusive here;
            // the user hears broadband → ear over the per-band gates.
            { "Vintage Vocal Plate", { { 0.650f, 0.640f, 0.620f, 0.666f, 0.744f, 0.658f, 0.595f, 0.700f, 1.210f }, 0.700f } },  // 2026-06-29 decayRef 0.82->0.70 (EAR "Lex rings out longer / DV shorter"): LENGTHENS the tail to match the anchor's PERCEIVED ringout (mid-tail @1.2s -91->-81 ≈ anchor -79). UNDOES Surgery A's gate-driven shortening: the T60-25dB gate measures the anchor's EARLY slope (0.61s) but the Lex is NON-EXPONENTIAL (rings out far past that), so matching the gate made DV sound too short. Ear > gate here; raises n_fail (T60 "too long") but matches the audible tail.
        }};
        float t60[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        float ref = 0.0f;
        if (const auto* p = findPresetConfig (kDattorroOctaveT60ByName, std::string_view (name)))
        { for (int b = 0; b < 9; ++b) t60[b] = p->t60[b]; ref = p->decayRef; }
        // Calibration override: DUSKVERB_OCTT60="t0,t1,...,t8" (9 csv) + DUSKVERB_OCTREF.
        if (const char* e = tuningEnv().octt60; e != nullptr && e[0] != '\0')
        {
            juce::StringArray toks; toks.addTokens (juce::String (e), ",", "");
            for (int b = 0; b < 9 && b < toks.size(); ++b) t60[b] = toks[b].getFloatValue();
        }
        if (const char* e = tuningEnv().octref; e != nullptr && e[0] != '\0') ref = juce::String (e).getFloatValue();
        engine.setDattorroOctaveDecayRef (ref);
        for (int b = 0; b < 9; ++b) engine.setDattorroOctaveT60 (b, t60[b]);
    }

    // Static tonal-correction GEQ (per-octave cut-only level trim on the wet
    // output, decoupled from decay — the fix for the gain==decay==level coupling
    // wall: cut a band's steady-state level without shortening its decay).
    // Per-preset dB[9] (63|125|250|500|1k|2k|4k|8k|16k); 0 = identity = bit-null.
    {
        struct TonalCorrConfig { float db[9]; };
        static constexpr std::array<std::pair<std::string_view, TonalCorrConfig>, 1> kDattorroTonalCorrByName = {{
            // BEGIN_DATTTONAL_MAP (offline calibrated cut-only octave trims)
            // Vintage Vocal Plate 2026-06-19: cut-only post-tank trim of DV's broadly-
            // hot upper-mid/HF (mid 250-1k +3.3, hi 4-12k +4.4) + the 1k tank-mode
            // resonance (sine1k +14dB). Decoupled from decay (doesn't shorten the bands).
            // Brightens the EARLY centroid as a side-effect: cent_50 -26% -> -9.6% (PASS),
            // mid/hi/edt-low-mid now pass. n_fail 25->24. NB a sub cut here just shuffles
            // via the noiseburst gain-match (sub-bass pops hot) — left uncut.
            // 2026-06-20 EAR "brighter": eased the 4k/8k/16k cuts (-5/-4/-2 -> -2/-1/0)
            // so the (now longer + thus darker) tail keeps its top → cent_50 stays bright.
            { "Vintage Vocal Plate", { { 0.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 0.0f } } },  // 2026-06-29 EAR "muffled/midrange different": the old -6dB@1k + -3@500/2k scoop GOUGED the upper-mid presence (snare tail 630-2500Hz was -5 to -7dB under anchor). Flatten to a gentle -1 uniform trim -> tail upper-mid match 5.3->3.3 L1, n_fail 36->35. Residual jaggedness (peaks 125/400, dip 1k) is DPV modal structure vs Lexicon, not EQ-fixable. Sweep: tonal_sweep.py / tonal_refine.py.
            // (Drum Plate REMOVED 2026-06-15: the tonal cuts were treating a
            // SYMPTOM — the +6dB@125 tilt is the boing mode's energy. It gamed the
            // sustained gates (19->8 fiction), broke the impulse/hit loudness
            // (+2.7dB), and brightened (16k uncut + makeup) — all ear-confirmed.
            // The real fix is the boing itself: Size-up + density (modal density).)
            // END_DATTTONAL_MAP
        }};
        float db[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        if (const auto* p = findPresetConfig (kDattorroTonalCorrByName, std::string_view (name)))
            for (int b = 0; b < 9; ++b) db[b] = p->db[b];
        if (const char* e = tuningEnv().tonal; e != nullptr && e[0] != '\0')
        {
            juce::StringArray toks; toks.addTokens (juce::String (e), ",", "");
            for (int b = 0; b < 9 && b < toks.size(); ++b) db[b] = toks[b].getFloatValue();
        }
        for (int b = 0; b < 9; ++b) engine.setDattorroTonalCorrDb (b, db[b]);
    }

    // DattorroPlateVintage front-load early-reflection network (algo 1 only).
    // Sparse diffused early field + tank pre-delay → the slow-build envelope +
    // discrete early tap the Lexicon Vintage Plate has and the dense-from-onset
    // Dattorro tank lacks. Per-preset {erGain, predelayMs, tapMs, lpHz}; erGain 0
    // (unlisted) = bypassed = byte-identical. Env override DUSKVERB_FRONTLOAD=
    // "erGain,predelayMs,tapMs,lpHz" for rebuild-free sweeps.
    {
        struct FrontLoadConfig { float erGain, predelayMs, tapMs, lpHz; };
        // constexpr std::array (NOT std::unordered_map) — applyEngineConfig runs on the
        // AUDIO thread (performPresetSwap); a function-local static map would take the
        // static-init guard lock + heap-allocate on the first preset swap. Same RT-safe
        // pattern as kWidthBandsByName / kDenseHallOctaveT60ByName.
        static constexpr std::array<std::pair<std::string_view, FrontLoadConfig>, 0> kDpvFrontLoadByName = {
            // BEGIN_DPVFRONTLOAD_MAP
            // 2026-06-29 REVERTED: enabling the front-load ER for VVP closed the energy-arrival
            // gates (35->15) but fired ER taps 56/98ms BEFORE the delayed main (143ms) = audible
            // PRE-ECHO double-tap (user ear-caught). It gamed the gates with an artifact the anchor
            // does not have (anchor = main 83ms + POST-main second tap 143ms). The network can only
            // pre-echo on this engine config; a proper post-main second tap needs a discrete delay
            // tap the DPV lacks (engine work). Left off = byte-null.
            // END_DPVFRONTLOAD_MAP
        };
        float erGain = 0.0f, predelayMs = 60.0f, tapMs = 80.0f, lpHz = 7000.0f;
        const std::string_view nameView (name);
        for (const auto& e : kDpvFrontLoadByName)
            if (e.first == nameView)
            { erGain = e.second.erGain; predelayMs = e.second.predelayMs; tapMs = e.second.tapMs; lpHz = e.second.lpHz; break; }
        if (const char* e = tuningEnv().frontload; e != nullptr && e[0] != '\0')
        {
            juce::StringArray toks; toks.addTokens (juce::String (e), ",", "");
            if (toks.size() > 0) erGain     = toks[0].getFloatValue();
            if (toks.size() > 1) predelayMs = toks[1].getFloatValue();
            if (toks.size() > 2) tapMs      = toks[2].getFloatValue();
            if (toks.size() > 3) lpHz       = toks[3].getFloatValue();
        }
        engine.setDpvFrontLoad (erGain, predelayMs, tapMs, lpHz);

        // Post-main second reflection tap (the anchor's ~143 ms "duh-DUH"). A
        // darkened delayed copy summed POST-tank (arrives AFTER the main onset —
        // no pre-echo, unlike the front-load ER). Per-preset {ms, gain, lpHz};
        // gain 0 (unlisted) = off = byte-identical. Env DUSKVERB_DPVREFL="ms,gain,lpHz".
        struct PostMainTapConfig { float ms, gain, lpHz; };
        static constexpr std::array<std::pair<std::string_view, PostMainTapConfig>, 0> kDpvPostMainTapByName = {
            // BEGIN_DPVPOSTMAIN_MAP
            // END_DPVPOSTMAIN_MAP
        };
        float pmMs = 143.0f, pmGain = 0.0f, pmLpHz = 6000.0f;
        for (const auto& e : kDpvPostMainTapByName)
            if (e.first == nameView)
            { pmMs = e.second.ms; pmGain = e.second.gain; pmLpHz = e.second.lpHz; break; }
        if (const char* e = tuningEnv().dpvrefl; e != nullptr && e[0] != '\0')
        {
            juce::StringArray toks; toks.addTokens (juce::String (e), ",", "");
            if (toks.size() > 0) pmMs   = toks[0].getFloatValue();
            if (toks.size() > 1) pmGain = toks[1].getFloatValue();
            if (toks.size() > 2) pmLpHz = toks[2].getFloatValue();
        }
        engine.setDpvPostMainTap (pmMs, pmGain, pmLpHz);

        // Dense early-field: a compact Schroeder reverb summed POST-tank to fill
        // the loud post-onset 0.1-0.5s "shelf" the sparse Dattorro tank lacks (the
        // ear's "fuller"). Per-preset {gain, predelayMs, t60Ms}; gain 0 = off.
        // Env DUSKVERB_DENSEFIELD="gain,predelayMs,t60Ms".
        struct DenseFieldConfig { float gain, predelayMs, t60Ms; };
        static constexpr std::array<std::pair<std::string_view, DenseFieldConfig>, 1> kDpvDenseFieldByName = {{
            // BEGIN_DPVDENSEFIELD_MAP
            // 2026-06-29 (EAR "Lex fuller"): a compact Schroeder reverb (4 combs + 2
            // allpasses, predelayed 80 ms so it's POST-onset, T60 500 ms) summed post-
            // tank fills the loud 0.1-0.5 s "shelf" the sparse Dattorro tank lacks.
            // Closes the new decay_tail_l1 gate (the metric that matches the ear) with
            // no pre-echo / no flux spike / no clip. The shape floor the tuning couldn't.
            { "Vintage Vocal Plate", { 0.22f /*gain*/, 65.0f /*predelayMs*/, 650.0f /*t60Ms*/ } },  // 2026-06-29 fuller pass: longer T60 (650) SPREADS the early energy (fills @0.3s -30->-28 toward anchor -24) instead of a louder peak (gain 0.24 made a 140ms bump). Single clean onset, decay_tail passes, no flux.
            // END_DPVDENSEFIELD_MAP
        }};
        float dfGain = 0.0f, dfPre = 70.0f, dfT60 = 500.0f;
        for (const auto& e : kDpvDenseFieldByName)
            if (e.first == nameView)
            { dfGain = e.second.gain; dfPre = e.second.predelayMs; dfT60 = e.second.t60Ms; break; }
        if (const char* e = tuningEnv().densefield; e != nullptr && e[0] != '\0')
        {
            juce::StringArray toks; toks.addTokens (juce::String (e), ",", "");
            if (toks.size() > 0) dfGain = toks[0].getFloatValue();
            if (toks.size() > 1) dfPre  = toks[1].getFloatValue();
            if (toks.size() > 2) dfT60  = toks[2].getFloatValue();
        }
        engine.setDpvDenseField (dfGain, dfPre, dfT60);

        // Dattorro (algo 0) dense early-field — the SAME compact Schroeder field,
        // summed post-tank in DuskVerbEngine (DattorroTank.cpp untouched → every
        // algo-0 preset bit-null when off). Fills the thin tail of the short rooms
        // (Medium Drum Room: decay sub/low/mid -23..-30% vs the fat-snare anchor).
        // Per-preset {gain, predelayMs, t60Ms}; gain 0 (unlisted) = off.
        // Env DUSKVERB_DDENSEFIELD="gain,predelayMs,t60Ms" for rebuild-free sweeps.
        static constexpr std::array<std::pair<std::string_view, DenseFieldConfig>, 1> kDattorroDenseFieldByName = {{
            // BEGIN_DATTORRODENSEFIELD_MAP
            // 2026-06-29: the same dense-field win as VVP, ported to the algo-0 rooms.
            // Medium Drum Room's tail was thin (decay sub/low/mid -23..-30%, T60-500
            // -24% vs the fat-snare anchor); a compact Schroeder field (predelay 42 ms
            // = POST-onset, long T60 730 ms SPREADS energy into the 0.1-0.5 s window)
            // fills it: 25 -> 19. Residual 19 = boing (333 Hz sparse mode), T60-63
            // bass coupling, discrete-ER structure, cent/sine1k voicing -- structural.
            { "Medium Drum Room", { 0.27f /*gain*/, 42.0f /*predelayMs*/, 730.0f /*t60Ms*/ } },
            // END_DATTORRODENSEFIELD_MAP
        }};
        float ddGain = 0.0f, ddPre = 70.0f, ddT60 = 500.0f;
        for (const auto& e : kDattorroDenseFieldByName)
            if (e.first == nameView)
            { ddGain = e.second.gain; ddPre = e.second.predelayMs; ddT60 = e.second.t60Ms; break; }
        if (const char* e = tuningEnv().ddensefield; e != nullptr && e[0] != '\0')
        {
            juce::StringArray toks; toks.addTokens (juce::String (e), ",", "");
            if (toks.size() > 0) ddGain = toks[0].getFloatValue();
            if (toks.size() > 1) ddPre  = toks[1].getFloatValue();
            if (toks.size() > 2) ddT60  = toks[2].getFloatValue();
        }
        engine.setDattorroDenseField (ddGain, ddPre, ddT60);
    }

    // Sweep override: DUSKVERB_FDN_DELAYS env var wins over the map. CSV of
    // 16 positive ints in samples. Set by the sweep harness per trial; absent
    // in normal sessions. Bypasses state-blob roundtrip entirely (JUCE's
    // copyXmlToBinary drops root-XML attributes added after createXml — so
    // setAttribute injection from a host doesn't survive the deserialization
    // path inside setStateInformation).
    // CSV of 16 (16-line engines) OR 32 (AccurateHall32) positive ints in
    // samples. setFDNBaseDelays forwards to every engine; each copies its own
    // line count (fdn_/accurateHall_ read the first 16, accurateHall32_ reads
    // all 32). Zero-padded so a 16-token override leaves the 32-line tail
    // defined (it is not the render target in that case).
    bool fdnDelaysFromEnv = false;
    const char* envCsv = tuningEnv().fdnDelays;
    if (envCsv != nullptr && envCsv[0] != '\0')
    {
        juce::StringArray tokens;
        tokens.addTokens (juce::String (envCsv), ",", "");
        if (tokens.size() == 16 || tokens.size() == 32)
        {
            int parsed[32] = {};
            bool ok = true;
            for (int i = 0; i < tokens.size(); ++i)
            {
                const int v = tokens[i].trim().getIntValue();
                if (v <= 0) { ok = false; break; }
                parsed[i] = v;
            }
            if (ok)
            {
                // Apply the swept delays, but DON'T early-return — that skipped the
                // composite voicing / buildup / shimmer-down / etc. below. Just mark
                // that base delays are set so the per-preset base-delay map/reset
                // section doesn't overwrite them.
                engine.setFDNBaseDelays (parsed);
                fdnDelaysFromEnv = true;
            }
        }
    }

    // Algo-13 COMPOSITE voicing (sparse ER front-end + 16-line AccurateHall tail).
    // Per-preset ER config — only the ER + mix here; the tail (accurateHall_) is
    // configured by the preset's octave-T60 map + frozen mod. DUSKVERB_TILEDROOM
    // env (6 floats: erSize,onsetMs,erDecayMs,burst2Ms,sparseTailGain,spare)
    // overrides for the rebuild-free tuning sweep (applies to whichever composite
    // preset is loading).
    // 6th field erGain: ER level in the mix (1.0 = full). Backs off the ER for a
    // less-front-loaded room so it doesn't overshoot energy_first50.
    struct CompositeER { float erSize, onsetMs, erDecayMs, burst2Ms, sparseTailGain, erGain, burst2Gain = 0.0f, buildupAmount = 0.0f, buildupTime = 1.0f, buildupPostTank = 0.0f; };
    static constexpr std::array<std::pair<std::string_view, CompositeER>, 7> kCompositeERByName = {{
        // Tiled Room: tiledroom_voicing_sweep.py best (n_fail 26->16). Tight ER,
        // fast onset, short discharge, micro-burst ~18 ms, ER/tail mix 0.49, full ER.
        { "Tiled Room",       { 0.3248f, 1.4567f, 17.1838f, 17.749f, 0.4928f, 1.0000f } },
        // (Medium Drum Room composite/decouple FALSIFIED — best 24 vs FDN 25;
        // reverted to the FDN. See the FactoryPresets MDR comment.)
        // Cathedral (DenseHall, 2026-06-13): subtle SPARSE discrete-ER front over
        // the already-dense tank. erGain 0.35 (the dense tail is primary; ER adds
        // the few distinct early reflections a cathedral has). Wide hall ER size,
        // ~8 ms onset, long discharge. sparseTailGain unused by the DenseHall case.
        { "Cathedral Large Hall", { 0.55f, 8.0f, 45.0f, 28.0f, 0.50f, 0.35f, 0.0f, 1.0f, 1.5f } },  // 2026-06-23 workflow: buildupAmount 1.0 + timeScale 1.5 — gradual tail build closes energy_t50/first50ms + bloom 2-4k/4-8k/8-12k + ss hi/air (15->9)
        // Other halls migrated to DenseHall 2026-06-13. Sparse discrete-ER fronts
        // over the dense tank, voiced by character: Vocal Hall smoother/less ER,
        // Blade Runner bigger/wider, Bright Hall moderate. erGain = ER level.
        { "Vocal Hall",       { 0.45f, 6.0f, 35.0f, 20.0f, 0.50f, 0.25f } },
        // Bright Hall 2026-06-19: BUILDUP fork (8th field = 1.0) — the long allpass
        // cascade makes the dense tail BUILD gradually so the early window has a real
        // dip + a prominent 2nd-hit (the duh-DUH). Cleared the front-load wall the tap
        // alone couldn't: attack 13ms ✓, energy_t50 208ms ✓ (anchor 204), first50 24% ✓,
        // early_tap 14.9dB@152ms ✓ (the tail's natural density peak IS the 2nd hit — no
        // burst2 needed). n_fail 13→11. Residual = HF-tilt wall (spec_L1 12.9k) + marginal
        // level/decay. erGain 0.30 ER on top for the onset's discrete reflections.
        { "Bright Hall",      { 0.50f, 7.0f, 40.0f, 24.0f, 0.50f, 0.30f, 0.0f, 1.0f } },
        { "Blade Runner 224", { 0.65f, 12.0f, 55.0f, 34.0f, 0.50f, 0.40f } },
        { "Large Chamber",    { 0.45f, 7.0f, 38.0f, 22.0f, 0.50f, 0.30f, 0.0f, 1.0f, 1.7f } },  // 2026-06-23 workflow: BUILDUP 1.0 + timeScale 1.7 — gradual tank build closes mid 1-4k/hi 4-12k/energy_t50/bloom 2-4k/4-8k/env_shape (20->17; trades T60-16k/noiseburst — EAR-CHECK)
        // 79 Vocal Chamber (QuadTank front-load redesign 2026-06-18): the velvet
        // sparse field front-loads the early arrival the QuadTank's washy swell lacks
        // (energy_t50 +88ms late → "cloudy snare"). erGain high + sparseTailGain
        // reduced so the defined early field owns the onset, the tank the late body.
        // Tuned 2026-06-18: onset 110 / decay 30 / burst2 130 / tail 0.75 / erGain
        // 0.40 — the velvet energy concentrated ~110-165ms (energy_t50 165 vs anchor
        // 136, near-match; not the overshot 60ms of the first pass). Fixes the cloud
        // (edt mid +52%→+2%, edt hi +28%→−18%). erGain 0.40 keeps the QuadTank tail
        // primary so it's a chamber, not an ER slap.
        { "79 Vocal Chamber", { 0.40f, 110.0f, 30.0f, 130.0f, 0.92f, 0.40f } },   // 2026-07-04 tailGain 0.75->0.92 (EAR "VVV thicker/denser"): with Diffusion 0.95 closes body 125-250 / edt-hi / attack / deep-sub / flux (18->17); the fuller tank IS the density the ear heard missing.
    }};
    if (const auto* erEntry = findPresetConfig (kCompositeERByName, std::string_view (name)))
    {
        CompositeER c = *erEntry;
        const char* envTr = tuningEnv().tiledRoom;
        if (envTr != nullptr && envTr[0] != '\0')
        {
            juce::StringArray t;
            t.addTokens (juce::String (envTr), ",", "");
            if (t.size() == 6 || t.size() == 7)   // 7th (optional) = burst2Gain (the discrete late-tap level)
            {
                c.erSize = t[0].getFloatValue(); c.onsetMs = t[1].getFloatValue();
                c.erDecayMs = t[2].getFloatValue(); c.burst2Ms = t[3].getFloatValue();
                c.sparseTailGain = t[4].getFloatValue(); c.erGain = t[5].getFloatValue();
                if (t.size() == 7) c.burst2Gain = t[6].getFloatValue();
            }
        }
        engine.setTiledRoomVoicing (c.erSize, c.onsetMs, c.erDecayMs, c.burst2Ms, c.sparseTailGain, c.erGain);
        engine.setSparseFieldBurst2Gain (c.burst2Gain);   // discrete late tap (0 = bit-null for the other composites)
        // DenseHall tail buildup (0 = bypass/bit-null). Env DUSKVERB_BUILDUP="amount[,timeScale[,postTank]]".
        float buildupAmt = c.buildupAmount, buildupTime = c.buildupTime;
        bool  buildupPost = c.buildupPostTank > 0.5f;
        if (const char* envBu = tuningEnv().buildup; envBu != nullptr && envBu[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (envBu), ",", "");
            if (t.size() >= 1) buildupAmt = t[0].getFloatValue();   // keep default if no token
            if (t.size() >= 2) buildupTime = t[1].getFloatValue();
            if (t.size() >= 3) buildupPost = t[2].getFloatValue() > 0.5f;
        }
        engine.setBuildupAmount (buildupAmt);
        engine.setBuildupTimeScale (buildupTime);
        engine.setBuildupPostTank (buildupPost);
    }
    else
    {
        // 2026-06-23 review fix: unmapped algo-13/14 presets must reset the composite
        // voicing + buildup to engine defaults, else they latch the previously-loaded
        // composite preset's ER/buildup across an A/B swap (cross-preset bleed). All
        // current presets are mapped, so this is a latent guard for future presets.
        engine.setTiledRoomVoicing (1.0f, 14.0f, 55.0f, 115.0f, 0.45f, 1.0f);
        engine.setSparseFieldBurst2Gain (0.0f);
        engine.setBuildupAmount (0.0f);
        engine.setBuildupTimeScale (1.0f);
        engine.setBuildupPostTank (false);
    }

    // Shimmer octave-DOWN voice (the warm low Valhalla Shimmer's DeepBlueDay has via a
    // down-pitched voice — DV's up-only voices produced 0 dB at 500 Hz from a 1 kHz sine).
    // Per-preset; 0 = off → the voice is skipped → bit-null (Black Hole + non-shimmer).
    // Env DUSKVERB_SHIMMERDOWN="mix" for the rebuild-free sweep.
    {
        // constexpr std::array (NOT std::unordered_map) — applyEngineConfig runs on the
        // AUDIO thread (performPresetSwap); a function-local static map would take the
        // static-init guard lock + heap-allocate on first preset swap. Same RT-safe
        // pattern as kWidthBandsByName / kDenseHallOctaveT60ByName.
        static constexpr std::array<std::pair<std::string_view, float>, 1> kShimmerDownByName = {{
            { "Deep Blue Day", 0.35f },   // 2026-06-29: −1 oct voice DROPPED 1.5→0.35 — the new −2 oct SUB voice (below) does the low work far more efficiently (reaches 250 Hz in one step vs the −1 oct cascade dying out). 0.35 just tops up the 500 Hz rung. See kShimmerSubByName/kShimmerHpfByName for the validated 1 kHz-sine match to Valhalla Shimmer program 3.
        }};
        float downMix = 0.0f;
        const std::string_view nameView (name);
        for (const auto& e : kShimmerDownByName)
            if (e.first == nameView) { downMix = e.second; break; }
        if (const char* env = tuningEnv().shimmerdown; env != nullptr && env[0] != '\0')
            downMix = juce::String (env).getFloatValue();
        engine.setShimmerDownOctaveMix (downMix);

        // Sub voice (−2 oct → 250 Hz) + feedback-HPF corner: the −1 oct cascade dies out
        // before the deep lows; the sub voice reaches 250 Hz in one step and a lower HPF
        // corner lets the 60-250 Hz wash survive the loop. Matches Valhalla Shimmer
        // DeepBlueDay (program 3: 250 Hz −9 dB, 60-200 wash −15..−22 rel. fundamental).
        // 0 / default 60 = bit-null (Black Hole + non-shimmer untouched).
        // Env DUSKVERB_SHIMMERSUB="mix", DUSKVERB_SHIMMERHPF="hz" for rebuild-free sweeps.
        static constexpr std::array<std::pair<std::string_view, float>, 1> kShimmerSubByName = {{
            // 2026-06-29: −2 oct (×0.25 → 250 Hz) voice. Tuned vs Valhalla Shimmer program 3
            // on a sustained 1 kHz sine (rel. fundamental @ output level): lifts 31-60 Hz
            // −55→−30 (Val −22), 60-125 −54→−20 (Val −18), 250 Hz −28→−3 (Val −9). The deep
            // low warmth DV lacked. Residual: 250-500 runs ~+6 dB hot (the +12 st up voice
            // re-pitches the sub's 250 → 500) — structural, not tunable from here.
            { "Deep Blue Day", 1.50f },   // 2026-07-04 4.5->1.5 (EAR): the recirculating sub voice pushed the loop's low-register gain past unity — 62/125/250 rungs GREW +19..25 dB across a 20 s tail into a softClip equilibrium that NEVER fades (user: "buildup that never fades out"; Valhalla gently fades). Unity crossing measured at ~2.0; 1.5 fades -7 dB and the dry-fed octave cascade (below) restores the low fullness SAFELY (feed-forward, cannot build). Gates hid this: sinelong renders tone-only (no tail window), and the buildup inflated low T60 readings toward the anchor's long values.
        }};
        static constexpr std::array<std::pair<std::string_view, float>, 1> kShimmerHpfByName = {{
            { "Deep Blue Day", 24.0f },  // feedback HPF 60→24 so the regenerated 31-125 Hz wash survives the loop (24 still clears the ~12 Hz grain rumble). Flattens DV's over-steep low cascade toward Valhalla's.
        }};
        float subMix = 0.0f, hpfHz = 60.0f;
        for (const auto& e : kShimmerSubByName) if (e.first == nameView) { subMix = e.second; break; }
        for (const auto& e : kShimmerHpfByName) if (e.first == nameView) { hpfHz  = e.second; break; }
        if (const char* env = tuningEnv().shimmersub; env != nullptr && env[0] != '\0')
            subMix = juce::String (env).getFloatValue();
        if (const char* env = tuningEnv().shimmerhpf; env != nullptr && env[0] != '\0')
            hpfHz  = juce::String (env).getFloatValue();
        engine.setShimmerSubOctaveMix  (subMix);
        engine.setShimmerFeedbackHpfHz (hpfHz);

        // Wet stereo chorus/ensemble — Valhalla Shimmer Black Hole swings the image at
        // ~0.83 Hz (measured L-R mod rms 6.8 dB vs DV's static 2.2). A slow anti-phase
        // modulated-delay pair gives that moving field. {rateHz, depth}; depth 0 = off.
        // Env DUSKVERB_SHIMMERSTEREO="rateHz,depth".
        static constexpr std::array<std::pair<std::string_view, std::pair<float,float>>, 1> kShimmerStereoByName = {{
            // 2026-06-29: rate 0.42 Hz (the comb passes its notch TWICE per LFO cycle, so
            // the L-R movement lands at 2×0.42 = 0.83 Hz = Valhalla's measured rate), depth
            // 0.8. Result vs Valhalla Shimmer Black Hole: L-R mod rate 0.83 Hz (exact),
            // correlation +0.62 (Val +0.60), movement rms 4.2 dB (Val 6.8 — full magnitude
            // destabilises the rate, 4.2 is the musical max; up from DV's static 2.2).
            { "Black Hole", { 0.42f, 0.00f } },   // 2026-06-29 DORMANT: the sine-tuned chorus matched the 0.83 Hz movement but worsened the broadband stereo_corr gate (-0.22 vs anchor +0.12 = over-wide) — the real BH "stereo" gap is mono HIGHS (width hi +0.02 vs +0.95), addressed via width/brightness below. Infra kept for a future gentler pass.
        }};
        float smRate = 0.83f, smDepth = 0.0f;
        for (const auto& e : kShimmerStereoByName) if (e.first == nameView) { smRate = e.second.first; smDepth = e.second.second; break; }
        if (const char* env = tuningEnv().shimmerstereo; env != nullptr && env[0] != '\0')
        {
            juce::StringArray toks; toks.addTokens (juce::String (env), ",", "");
            if (toks.size() > 0) smRate  = toks[0].getFloatValue();
            if (toks.size() > 1) smDepth = toks[1].getFloatValue();
        }
        engine.setShimmerStereoMod (smRate, smDepth);

        // HF-air voice — the genuine >12 kHz air Valhalla Shimmer has (centroid ~9.9k/6.6k vs
        // DV ~6k/5k). A post-loop +12 st shifter on the wet 6-12 kHz makes 12-24 k air,
        // bypassing the reverb HF-damp + 14 k feedback LPF that cap it. mix 0 = off = bit-null.
        // Env DUSKVERB_SHIMMERAIR="mix".
        static constexpr std::array<std::pair<std::string_view, float>, 2> kShimmerAirByName = {{
            // 2026-06-30: post-loop +12 st air voice = the genuine >12 kHz air the engine
            // couldn't make (the reverb HF-damps + the 14 k feedback LPF cut the in-loop air).
            // Raises the snare centroid toward Valhalla WITHOUT harshening 4-12k (unlike the
            // air shelf): BH 6006→7029 (Val 9920), DBD 4936→5749 (Val 6620), tilt stays matched.
            // Capped at the max that doesn't regress the noiseburst ss_air gate (DV's residual
            // 12.9 k AA spike already runs the noiseburst air-heavy) — closes ~half the gap clean.
            { "Black Hole",    1.5f },
            { "Deep Blue Day", 1.3f },   // 2026-07-03 2.0->1.3: 2.0 pushed the snare cent_50 +19% bright (gate ±15) — 1.0-1.4 all land it in-gate. 1.3 (not 1.2) also closes spec_L1@12.9k: the residual AA-image NOTCH there is FILLED by this voice (air DOWN made spec_L1 worse — 1.05 -> 5.69 dB). Bloom 4-8k/8-12k barely react to air (late-HF rise is the loop's, not this voice's).
        }};
        float airMix = 0.0f;
        for (const auto& e : kShimmerAirByName) if (e.first == nameView) { airMix = e.second; break; }
        if (const char* env = tuningEnv().shimmerair; env != nullptr && env[0] != '\0')
            airMix = juce::String (env).getFloatValue();
        engine.setShimmerHFAir (airMix);

        // Dense-diffusion tank swap (fixes the metallic HF tail; kurt ~26 -> ~7).
        // Per-preset bake below; default false = legacy sparse FDN (bit-identical).
        // Env DUSKVERB_SHIMMERDENSE=1 forces it on for A/B.
        static constexpr std::array<std::pair<std::string_view, bool>, 2> kShimmerDenseByName = {{
            { "Black Hole",    false },   // flipped to true once re-voiced (Phase 3/4)
            { "Deep Blue Day", false },
        }};
        bool useDense = false;
        for (const auto& e : kShimmerDenseByName) if (e.first == nameView) { useDense = e.second; break; }
        if (const char* env = tuningEnv().shimmerdense; env != nullptr && env[0] != '\0')
            useDense = juce::String (env).getIntValue() != 0;
        engine.setShimmerUseDenseReverb (useDense);

        // Tail spin-comb: smears the FDN's metallic HF while keeping its cascade/width/HF
        // (Deep Blue Day's fix — its character fights the full DenseHall swap). Per-preset
        // bake below; env DUSKVERB_SHIMMERSPIN=1 for A/B. Only meaningful on the FDN path.
        static constexpr std::array<std::pair<std::string_view, bool>, 2> kShimmerTailSpinByName = {{
            { "Black Hole",    false },   // BH uses DenseHall, not the spin-comb
            { "Deep Blue Day", false },   // flipped to true once re-voiced
        }};
        bool useSpin = false;
        for (const auto& e : kShimmerTailSpinByName) if (e.first == nameView) { useSpin = e.second; break; }
        if (const char* env = tuningEnv().shimmerspin; env != nullptr && env[0] != '\0')
            useSpin = juce::String (env).getIntValue() != 0;
        engine.setShimmerUseTailSpin (useSpin);

        // Per-preset up-voice scale — fills the mid tail (250 Hz-1 kHz) on transients. Deep Blue
        // Day boosts to match Valhalla's fuller snare-body regeneration. 1.0/1.0 = bit-identical.
        static constexpr std::array<std::pair<std::string_view, std::pair<float, float>>, 2> kShimmerUpVoiceByName = {{
            { "Black Hole",    { 1.0f, 1.0f } },
            { "Deep Blue Day", { 1.0f, 1.0f } },   // tuned by the sweep below
        }};
        float upv1 = 1.0f, upv2 = 1.0f;
        for (const auto& e : kShimmerUpVoiceByName) if (e.first == nameView) { upv1 = e.second.first; upv2 = e.second.second; break; }
        if (const char* env = tuningEnv().shimmerupv; env != nullptr && env[0] != '\0')
        {
            juce::StringArray parts; parts.addTokens (juce::String (env), ",", "");
            if (parts.size() >= 2) { upv1 = parts[0].getFloatValue(); upv2 = parts[1].getFloatValue(); }
        }
        engine.setShimmerUpVoiceScale (upv1, upv2);

        // Dry-fed even octave cascade (500/250/125/62 Hz) — Deep Blue Day uses it INSTEAD of the
        // old feedback down/sub voices (which were subharmonic-masked + the sub ratio was clamped
        // to −12). All-zero = off/bit-null. Env DUSKVERB_SHIMMEROCT="g500,g250,g125,g62".
        static const std::array<std::pair<std::string_view, std::array<float, 4>>, 2> kShimmerOctaveByName = {{
            { "Black Hole",    { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "Deep Blue Day", { 0.0f, 0.8f, 0.9f, 0.5f } },   // 2026-07-04 {0,.5,.5,0}->{0,.8,.9,.5} (EAR, with sub 4.5->1.5): the dry-fed cascade takes over the low warmth the recirculating sub voice used to supply — feed-forward, so it decays WITH the tank and cannot build up. Recovers down-octave cascade L1 + boom vs the plain sub cut (30->27). g500 stays 0 (the +12 up voice already re-pitches 250->500).
        }};
        float octGains[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (const auto& e : kShimmerOctaveByName) if (e.first == nameView) { for (int i = 0; i < 4; ++i) octGains[i] = e.second[i]; break; }
        if (const char* env = tuningEnv().shimmeroct; env != nullptr && env[0] != '\0')
        {
            juce::StringArray parts; parts.addTokens (juce::String (env), ",", "");
            for (int i = 0; i < 4 && i < parts.size(); ++i) octGains[i] = parts[i].getFloatValue();
        }
        engine.setShimmerOctaveCascade (octGains);

        // Tail noise floor — the dense noise-like fade Valhalla has; masks the sparse-mode ring.
        // gain 0 = off/bit-null. Env DUSKVERB_SHIMMERNOISE.
        static constexpr std::array<std::pair<std::string_view, float>, 2> kShimmerTailNoiseByName = {{
            { "Black Hole",    0.0f },
            { "Deep Blue Day", 0.7f },   // 2026-07-04 EAR "missing the ocean white noise in the tail": 0.7 with the darker 150-3500 band + the 500 ms slow-bloom attack = gate-NEUTRAL (32=32, cents clean) — the wash fades with the tail and fills the gaps between the sparse modes the user hears ringing.
        }};
        float noiseGain = 0.0f, noiseHp = 250.0f, noiseLp = 7000.0f;
        for (const auto& e : kShimmerTailNoiseByName) if (e.first == nameView) { noiseGain = e.second; break; }
        // Deep Blue Day ocean color (2026-07-04 EAR): darker band so the wash
        // doesn't brighten the centroid. Env "gain[,hpHz,lpHz]" overrides.
        if (nameView == std::string_view ("Deep Blue Day")) { noiseHp = 150.0f; noiseLp = 3500.0f; }
        if (const char* env = tuningEnv().shimmernoise; env != nullptr && env[0] != '\0')
        {
            juce::StringArray t; t.addTokens (juce::String (env), ",", "");
            if (t.size() > 0) noiseGain = t[0].getFloatValue();
            if (t.size() > 1) noiseHp   = t[1].getFloatValue();
            if (t.size() > 2) noiseLp   = t[2].getFloatValue();
        }
        engine.setShimmerTailNoise (noiseGain, noiseHp, noiseLp);

        // Feedback-loop HF compensation shelf (lift above cornerHz per pass) —
        // the FDN tank's per-pass HF loss, not the loop LPF, caps HF T60
        // (T60-16k wall, ShimmerEngine.h). First-order: the corner is the
        // mid-isolation lever (4 kHz leaks lift into the mids and stretches
        // their decay; higher corners spare them). 0 dB = off/bit-null.
        // Env DUSKVERB_SHIMMERHFS="dB[,cornerHz]".
        static constexpr std::array<std::pair<std::string_view, std::pair<float, float>>, 2> kShimmerHFSustainByName = {{
            { "Black Hole",    { 0.0f, 4000.0f } },
            { "Deep Blue Day", { 0.0f, 4000.0f } },   // tuned by the sweep below
        }};
        float hfsDb = 0.0f, hfsHz = 4000.0f;
        for (const auto& e : kShimmerHFSustainByName) if (e.first == nameView) { hfsDb = e.second.first; hfsHz = e.second.second; break; }
        if (const char* env = tuningEnv().shimmerhfs; env != nullptr && env[0] != '\0')
        {
            juce::StringArray toks; toks.addTokens (juce::String (env), ",", "");
            if (toks.size() > 0) hfsDb = toks[0].getFloatValue();
            if (toks.size() > 1) hfsHz = toks[1].getFloatValue();
        }
        engine.setShimmerHFSustainDb (hfsDb, hfsHz);
    }

    if (! fdnDelaysFromEnv)   // an env DUSKVERB_FDN_DELAYS override owns the delays — don't clobber it
    {
        if (const auto* p = findPresetConfig (kBaseDelaysByName, std::string_view (name)))
            engine.setFDNBaseDelays (p->delays);
        else
            engine.resetFDNBaseDelays();   // no custom set → restore default so a prior preset's custom delays don't leak (setFDNBaseDelays(nullptr) only PRESERVES, never resets)
    }

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
    [[maybe_unused]] static constexpr std::array<std::pair<std::string_view, ZetaEtaConfig>, 0> kZetaEtaByName = {{
        // VH (and any other ζ+η-opt-in preset) now applies values via
        // FactoryPreset::applyTo writing APVTS. Map kept as the documented
        // canonical defaults but no longer applied — APVTS path handles
        // all the smoother ramping and inter-block coefficient sync.
    }};
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

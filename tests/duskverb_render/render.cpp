// DuskVerb render tool.
//
// Loads a plugin via juce::AudioPluginFormatManager (the same path hosting
// DAWs use), applies a named factory preset's parameter values, and renders
// test signals through it. Output WAVs go to tests/duskverb_render/output/
// (or --output-dir).
//
// Supported plugin formats (auto-detected by file extension):
//   .component  → AudioUnit  (macOS, requires JUCE_PLUGINHOST_AU=1)
//   .vst3       → VST3       (cross-platform, JUCE_PLUGINHOST_VST3=1)
//   .so / .dll  → VST2       (Linux/Windows, requires JUCE_PLUGINHOST_VST=1
//                              + VST2_SDK_DIR at build time; supports
//                              yabridge-bridged Windows VST2s on Linux)
//
// Why hosted (not Processor-link)? An engine-standalone or Processor-link
// test can drift from what the user hears in the DAW because the plugin
// wrapper layer (parameter scaling, channel layout, automation timing) is
// part of the audio result. Hosting via the actual plugin bundle eliminates
// that whole class of false negatives.
//
// Usage:
//   duskverb_render <preset_name>                              # default DuskVerb
//   duskverb_render "Lush Dark Hall"
//   duskverb_render --vst3 ~/.vst3/DuskVerb.vst3 "Vintage Vocal Plate"
//   duskverb_render --vst2 ~/.vst/yabridge/LexConcertHall.so --program "Concert Hall"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

namespace
{
    constexpr double kSampleRate   = 48000.0;
    // 2048 samples ≈ 43 ms blocks. 4096 triggered Wine-side stack overflows
    // inside Lex VST2 plugins after a full vpreset apply (yabridge handles
    // every Wine exception on a finite thread stack; deep call stacks
    // from plugin-internal FPU/SSE handling crashed at 4096). 256 is
    // historical default but hits yabridge IPC overhead. 2048 is the
    // measured sweet spot: 8× fewer round-trips than 256, no stack issue.
    constexpr int    kBlockSize    = 2048;
    constexpr int    kRenderSec    = 6;
    constexpr int    kTotalSamples = static_cast<int> (kSampleRate * kRenderSec);

    // Default DuskVerb plugin path: per-user install location for the
    // platform's native format. Overridable via --au / --vst3 / --vst2.
   #if JUCE_MAC
    const juce::String kDefaultPluginPath =
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Plug-Ins/Components/DuskVerb.component")
            .getFullPathName();
   #else
    const juce::String kDefaultPluginPath =
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile (".vst3/DuskVerb.vst3")
            .getFullPathName();
   #endif

    // Factory preset definitions — mirror FactoryPresets.h (we don't link
    // against the plugin's source, so we duplicate just the values we need).
    struct PresetParams
    {
        juce::String name;
        std::map<juce::String, float> values;  // parameter ID -> raw value
        // SixAPTank brightness/density overrides. Defaults match the engine's
        // historical hardcoded constants — set to non-default values only for
        // presets that opt in (e.g. Black Hole). These are NOT host parameters;
        // they're injected via plugin->setStateInformation() after the regular
        // param-set step, since they live on the plugin state ValueTree.
        bool  hasSixAP            = false;
        float sixAPDensityBaseline = 0.62f;
        float sixAPBloomCeiling    = 0.85f;
        float sixAPBloomStagger[6] = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f };
        float sixAPEarlyMix        = 0.5f;
        float sixAPOutputTrim      = 1.3f;
    };

    // ── Algorithm choice normalisation ──────────────────────────────────────
    // DuskVerb's "Algorithm" parameter is a JUCE AudioParameterChoice. A choice
    // param normalises index i as i / (numChoices - 1). The dropdown exposes 9
    // engines (getAlgorithmConfig() in AlgorithmConfig.h):
    //   0 Dattorro · 1 DattorroVintage · 2 SixAPTank · 3 QuadTank · 4 FDN
    //   5 Spring   · 6 NonLinear        · 7 Shimmer   · 8 VintageTank
    // This divisor MUST equal numAlgorithms - 1. It was 7 in the 8-engine era;
    // adding VintageTank (2026-05-30, commit 4362b22) made it 8. A stale divisor
    // silently misroutes the engine (e.g. FDN index 4 → Spring) — the bug this
    // branch (87-fix-fdn-quadtank) fixes. Keep kNumAlgorithms in sync with
    // getNumAlgorithms() whenever an engine is added or removed.
    // MUST equal AlgorithmConfig::getNumAlgorithms() (the harness is standalone
    // and cannot link the plugin enum, so bump this by hand when an engine is
    // added). Was a stale 9 after ReverseRoom became the 10th engine — that
    // off-by-one divisor misrouted --param "Algorithm" (e.g. FDN 4 → wrong engine).
    static constexpr int   kNumAlgorithms    = 14;   // 0..13: + 13 TiledRoom
    static constexpr float kAlgorithmDivisor = static_cast<float> (kNumAlgorithms - 1);  // 13.0f

    // Keys are the human-readable parameter NAMES (matching what the AU host
    // surfaces). The original string IDs from the plugin source are hashed to
    // ints by the AU wrapper, so we match on display name instead.
    PresetParams getLushDarkHall()
    {
        return {
            "Lush Dark Hall",
            {
                // Migrated from 6-AP → FDN on 2026-04-26.
                { "Algorithm",       4.0f / kAlgorithmDivisor},     // FDN — Realistic Space (choice 4 of 0..8)
                { "Dry/Wet",         1.0f },
                { "Bus Mode",        1.0f },
                { "Pre-Delay",       35.0f },
                { "Pre-Delay Sync",  0.0f },
                { "Decay Time",      3.30f },
                { "Size",            0.75f },
                { "Mod Depth",       0.12f },     // 0.22 → 0.12 (FDN tail already smooth)
                { "Mod Rate",        0.55f },
                { "Treble Multiply", 0.35f },     // 0.55 → 0.35 (darker)
                { "Bass Multiply",   1.40f },
                { "Mid Multiply",    1.10f },
                { "Low Crossover",   328.0f },
                { "High Crossover",  2700.0f },   // 3500 → 2700 (damp upper mids)
                { "Saturation",      0.20f },
                { "Diffusion",       0.75f },
                { "Early Ref Level", 0.25f },
                { "Early Ref Size",  0.55f },
                { "Lo Cut",          150.0f },
                { "Hi Cut",          7000.0f },   // 8000 → 7000 (dark scoring stage)
                { "Width",           1.40f },     // 1.30 → 1.40 (FDN takes wider spread)
                { "Freeze",          0.0f },
                { "Gain Trim",       -0.5f },      // 1 kHz sine calibration round 1
                { "Mono Below",      20.0f },
            }
        };
    }

    PresetParams getCathedral()
    {
        // v2 Phase 2 — CoherentLoop modulation topology + autonomous tuner
        // (--category Halls). Mirrors FactoryPresets.h "Cathedral" row.
        return {
            "Cathedral",
            {
                { "Algorithm",       4.0f / kAlgorithmDivisor},
                { "Dry/Wet",         1.0f },
                { "Bus Mode",        1.0f },
                { "Pre-Delay",       20.88f },
                { "Pre-Delay Sync",  0.0f },
                { "Decay Time",      3.59f },
                { "Size",            0.70f },
                { "Mod Depth",       0.18f },
                { "Mod Rate",        0.95f },
                { "Treble Multiply", 0.93f },
                { "Bass Multiply",   1.13f },
                { "Mid Multiply",    0.73f },
                { "Low Crossover",   418.0f },
                { "High Crossover", 7773.0f },
                { "Saturation",      0.35f },
                { "Diffusion",       0.26f },
                { "Early Ref Level", 0.48f },
                { "Early Ref Size",  0.36f },
                { "Lo Cut",          22.0f },
                { "Hi Cut",         4261.0f },
                { "Width",           0.89f },
                { "Freeze",          0.0f },
                { "Gain Trim",      -7.65f },
                { "Mono Below",      20.0f },
                { "Hi Cut Shelf",   -14.5f },
            }
        };
    }

    PresetParams getBladeRunner224()
    {
        return {
            "Blade Runner 224",
            {
                // 2026-05-25 re-anchor to Lex Large RHall 4 (the actual
                // 224 Random Hall algorithm used on the 1982 Blade Runner
                // score). Engine swapped from Dattorro (algo 0) to FDN
                // (algo 4) — Dattorro 2-AP topology can't replicate Random
                // Hall's multi-tap input + dense diffusion + heavy mod.
                { "Algorithm",       4.0f / kAlgorithmDivisor },     // 4 = Realistic Space (FDN)
                { "Dry/Wet",         1.0f },
                { "Bus Mode",        1.0f },
                { "Pre-Delay",       25.0f },
                { "Pre-Delay Sync",  0.0f },
                { "Decay Time",      4.99f },
                { "Size",            0.48f },
                { "Mod Depth",       0.35f },     // heavy mod — Random Hall signature
                { "Mod Rate",        0.64f },
                { "Treble Multiply", 0.93f },
                { "Bass Multiply",   2.08f },     // extended bass — Lex 1.5× BassRT
                { "Mid Multiply",    1.94f },
                { "Low Crossover",   550.0f },
                { "High Crossover",  1581.0f },
                { "Saturation",      0.39f },
                { "Diffusion",       0.97f },
                { "Early Ref Level", 0.00f },
                { "Early Ref Size",  0.50f },
                { "Lo Cut",          47.0f },
                { "Hi Cut",          19723.0f },
                { "Width",           1.10f },
                { "Freeze",          0.0f },
                { "Gain Trim",      -11.3f },     // level-matched to Lex Random Hall RHall 4
                { "Mono Below",      20.0f },
            }
        };
    }

    // Compact preset builder. Field order matches FactoryPresets.h exactly so
    // values can be transcribed 1:1 from the source.  Algorithm choice param
    // values are the choice INDEX (0..8); we normalise to index/(N-1) below.
    PresetParams makePreset (const char* name,
        int algoIdx, float mix, bool bus, float predelay,
        float decay, float size, float modDepth, float modRate,
        float damping, float bassMult, float crossover,
        float diffusion, float erLevel, float erSize,
        float loCut, float hiCut, float width, float gainTrim,
        float monoBelow = 20.0f,
        float midMult = 1.0f, float highCrossover = 4000.0f, float saturation = 0.0f)
    {
        return {
            juce::String (name),
            {
                // Divisor must equal (numAlgorithms - 1) = kAlgorithmDivisor.
                // 10 algorithms now (Dattorro / DattorroVintage / SixAPTank /
                // QuadTank / FDN / Spring / NonLinear / Shimmer / VintageTank /
                // ReverseRoom) → divisor = 9. Mismatching the divisor silently
                // misroutes the algorithm index (the FDN→Spring bug fixed on
                // branch 87-fix-fdn-quadtank).
                { "Algorithm",       static_cast<float> (algoIdx) / kAlgorithmDivisor },
                { "Dry/Wet",         mix },
                { "Bus Mode",        bus ? 1.0f : 0.0f },
                { "Pre-Delay",       predelay },
                { "Pre-Delay Sync",  0.0f },
                { "Decay Time",      decay },
                { "Size",            size },
                { "Mod Depth",       modDepth },
                { "Mod Rate",        modRate },
                { "Treble Multiply", damping },
                { "Bass Multiply",   bassMult },
                { "Mid Multiply",    midMult },
                { "Low Crossover",   crossover },
                { "High Crossover",  highCrossover },
                { "Saturation",      saturation },
                { "Diffusion",       diffusion },
                { "Early Ref Level", erLevel },
                { "Early Ref Size",  erSize },
                { "Lo Cut",          loCut },
                { "Hi Cut",          hiCut },
                { "Width",           width },
                { "Freeze",          0.0f },
                { "Gain Trim",       gainTrim },
                { "Mono Below",      monoBelow },
            }
        };
    }

    PresetParams getPresetByName (const juce::String& name)
    {
        // For Cathedral we keep the explicit definition (has bus_mode=1 for the
        // 100 % wet A/B against Valhalla). Otherwise transcribe FactoryPresets.h.
        if (name == "Cathedral")          return getCathedral();
        if (name == "Lush Dark Hall")     return getLushDarkHall();
        if (name == "Blade Runner 224")   return getBladeRunner224();
        // Plates — all rendered at 100 % wet for fair comparison via Bus Mode.
        // Trailing args: (mono, midMult, highCrossover, saturation) — mirror FactoryPresets.h retunes.
        // Vintage Vocal Plate — algo 1 (DattorroPlateVintage). Optuna-locked
        // 2026-05-27 (v5 final pipeline). Best trial, loss 3.35, full_check
        // 14/14 PASS after engine-ceiling tail_t30 gate relaxation. See
        // FactoryPresets.h "Vintage Vocal Plate" comment block for the full
        // measurement table.
        // Vintage Vocal Plate — algo 1 (DattorroPlateVintage). v8 FINAL.
        // All 8 sustained-pink steady-state band gates PASS within ±1.5 dB.
        // Gain Trim manually overridden 11.48 → 6.7 per drum-loop A/B.
        if (name == "Vintage Vocal Plate" || name == "Vocal Plate (Vintage)")
        {
            // v9 (2026-05-27): staged_tuner.py 3-stage CMA-ES, 1300 trials.
            // Mirrors FactoryPresets.h "Vintage Vocal Plate" row exactly so
            // the render harness produces the same IR as the shipped factory
            // preset. Gain Trim 6.7 = user's ear-calibrated value (v9 optimizer
            // returned 9.49; we keep 6.7 per the listening-test override).
            auto p = makePreset (name.toRawUTF8(), 1, 1.0f, true, 10.0f,
                /* decay  */ 0.74f,  /* size    */ 0.41f,  /* modD   */ 0.20f, /* modR  */ 0.81f,
                /* damp   */ 0.84f,  /* bassMlt */ 1.10f,  /* xover  */ 436.0f,
                /* diff   */ 0.37f,  /* erLvl   */ 0.00f,  /* erSz   */ 0.30f,
                /* loCut  */ 25.7f,  /* hiCut   */ 13357.0f, /* width */ 0.75f,
                /* trim   */ 6.7f,
                /* mono   */ 20.0f,  /* midMlt  */ 1.06f,  /* highX  */ 7342.0f, /* sat */ 0.20f);
            p.values["DPV HF Shelf Gain"]   = 3.25f;
            p.values["DPV HF Shelf Freq"]   = 4049.0f;
            p.values["DPV Struct HF Damp"]  = 6605.0f;
            p.values["DPV Box Cut Gain"]    = -1.52f;
            p.values["DPV Box Cut Freq"]    = 704.0f;
            p.values["DPV Bass Shelf Gain"] = 0.82f;
            p.values["DPV Bass Shelf Freq"] = 89.7f;
            return p;
        }
        // Other halls
        // Smooth Concert Hall — algo=3 (QuadTank) per FactoryPresets.h post-reorder.
        // Stress-test rendering: BUS=true, Mix=1.0 to expose any FDN/QuadTank
        // residual artifacts. Factory bus/mix/predelay otherwise preserved.
        // Rich Plate — algo=4 (FDN). Bright + diffuse Lexicon PCM-90 plate
        // anchor. Stress-rendered at BUS=true Mix=1.0 (factory is mix=0.40 bus=false).
        // Vocal Booth — algo=4 (FDN). Sub-second tight close-mic room.
        // Stress-rendered at BUS=true Mix=1.0 (factory is mix=0.30 bus=false).
        // Vocal Hall — v13 autonomous staged_tuner.py sweep (--category Halls,
        // post-listening Stage 2 mud-band + spec_L1_max loss fix).
        // Mirrors FactoryPresets.h "Vocal Hall" row.
        if (name == "Vocal Hall")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true, 22.0f, 2.04f, 0.76f, 0.123f, 0.54f, 0.86f, 1.50f, 396.0f, 0.12f, 0.45f, 0.55f, 33.0f, 5884.0f, 0.88f, -1.52f, 20.0f, 1.13f, 8042.0f, 0.32f);
        // Chambers
        if (name == "Realistic Chamber")
        {
            auto p = makePreset (name.toRawUTF8(), 3, 1.0f, true, 8.39f, 5.05f, 0.44f, 0.20f, 0.50f, 0.56f, 0.71f, 324.0f, 0.42f, 0.20f, 0.44f, 26.0f, 10060.0f, 0.96f, -8.55f, 20.0f, 1.14f, 5957.0f, 0.26f);
            p.values["Hi Cut Shelf"] = -23.5f;
            return p;
        }
        // Rooms
        if (name == "Tight Drum Room")
        {
            auto p = makePreset (name.toRawUTF8(), 3, 1.0f, true, 1.18f, 0.43f, 0.38f, 0.03f, 1.11f, 1.01f, 0.71f, 641.0f, 0.38f, 0.80f, 0.57f, 37.0f, 10005.0f, 1.04f, -2.13f, 20.0f, 0.84f, 7586.0f, 0.22f);
            p.values["Hi Cut Shelf"] = -23.5f;
            return p;
        }
        if (name == "Tiled Room")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true, 8.20f, 0.73f, 0.48f, 0.21f, 1.39f, 0.82f, 0.94f, 424.0f, 0.75f, 0.46f, 0.40f, 20.0f, 10007.0f, 0.85f, -2.18f, 20.0f, 1.17f, 6356.0f, 0.27f);
        // Ambient (bus_mode=true in source, mono_below set)
        // New presets (2026-04-26):
        if (name == "Snare Plate XL")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true, 12.0f, 2.02f, 0.22f, 0.54f, 0.98f, 1.00f, 0.76f, 481.0f, 0.41f, 0.30f, 0.55f, 24.0f, 10038.0f, 0.92f, -3.85f, 20.0f, 0.61f, 7265.0f, 0.20f);
        // Spring engine (algo 4) — Phase A v3.0:
        if (name == "Surf '63 Spring")
            return makePreset (name.toRawUTF8(), 5, 1.0f, true, 0.0f, 1.60f, 0.40f, 0.20f, 1.50f, 1.00f, 0.85f, 1000.0f, 0.45f, 0.10f, 0.30f, 80.0f, 4000.0f, 1.10f, 2.5f, 20.0f, 1.00f, 4000.0f, 0.10f);
        // Non-Linear engine (algo 5) — Phase B v3.0:
        if (name == "In The Air Tonight")
            return makePreset (name.toRawUTF8(), 6, 0.216f, false, 0.0f, 2.608f, 0.80f, 0.092f, 0.794f, 0.75f, 1.10f, 500.0f, 0.50f, 0.00f, 0.30f, 60.0f, 10000.0f, 1.30f, 0.0f, 20.0f, 0.75f, 4000.0f, 0.10f);
        // Shimmer engine (algo 6) — v8 Eno/Lanois topology (mirrors FactoryPresets.h):
        // mod_depth = PITCH (0..1 → 0..24 semis), mod_rate Hz → FEEDBACK gain.
        if (name == "Deep Blue Day")
            return makePreset (name.toRawUTF8(), 7, 0.38f, false, 25.0f,  10.30f, 1.00f, 0.50f, 2.395f, 1.00f, 1.10f,  800.0f, 0.85f, 0.20f, 0.50f, 60.0f, 7000.0f, 1.30f,  0.0f, 20.0f, 1.00f, 4000.0f, 0.05f);
        if (name == "Cascading Heaven")
            return makePreset (name.toRawUTF8(), 7, 0.361f, false, 60.0f,  6.00f, 0.85f, 1.00f, 2.705f, 0.95f, 1.10f,  800.0f, 0.85f, 0.20f, 0.50f, 60.0f, 6000.0f, 1.40f, -3.0f, 60.0f, 1.00f, 4000.0f, 0.10f);
        if (name == "Black Hole")
        {
            auto p = makePreset (name.toRawUTF8(), 2, 0.50f, false,  0.0f, 14.00f, 0.95f, 0.25f, 0.60f, 1.00f, 1.10f,  700.0f, 0.85f, 0.05f, 0.70f, 60.0f, 18000.0f, 1.40f, -2.0f, 60.0f, 1.10f, 8000.0f, 0.08f);
            p.hasSixAP = true;
            p.sixAPDensityBaseline = 0.72f;
            p.sixAPBloomCeiling    = 0.92f;
            p.sixAPBloomStagger[0] = 0.65f; p.sixAPBloomStagger[1] = 0.78f;
            p.sixAPBloomStagger[2] = 0.92f; p.sixAPBloomStagger[3] = 1.05f;
            p.sixAPBloomStagger[4] = 1.18f; p.sixAPBloomStagger[5] = 1.30f;
            p.sixAPEarlyMix   = 0.75f;
            p.sixAPOutputTrim = 1.10f;
            return p;
        }
        if (name == "Mobius Pad")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 45.0f, 5.50f, 0.90f, 0.25f, 0.35f, 0.45f, 1.50f, 500.0f, 0.85f, 0.20f, 0.85f, 80.0f, 9000.0f, 1.50f, 4.5f, 80.0f, 1.20f, 3200.0f, 0.10f);

        if (name == "Vocal Plate")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true, 29.16f, 0.65f, 0.79f, 0.25f, 0.26f, 0.80f, 0.70f, 384.0f, 0.44f, 0.25f, 0.76f, 27.0f, 17316.0f, 0.78f, -3.11f, 20.0f, 0.96f, 8418.0f, 0.18f);
        // Deep Blue removed 2026-05-31 (redundant SixAP hall; see FactoryPresets.h).
        // Bright Hall — v1 autonomous staged_tuner.py (--category Halls).
        // Mirrors FactoryPresets.h "Bright Hall" row.
        if (name == "Bright Hall")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true, 0.0f, 4.31f, 0.75f, 0.103f, 0.83f, 1.29f, 1.38f, 540.0f, 0.44f, 0.50f, 0.50f, 20.0f, 11112.0f, 0.99f, -2.84f, 20.0f, 0.68f, 8344.0f, 0.03f);
        // PCM 90 — Rooms (QuadTank / NonLinear):
        // Ambience — Optuna-aligned to VVV Ambience (all 5 metrics within strict noise floor, lowest loss 0.248).
        // ModDepth + ModRate pulled to VVV's actual values (0.36, 0.32 Hz) —
        // Ambience — v1 autonomous staged_tuner.py (--category Rooms).
        // Mirrors FactoryPresets.h "Ambience" row.
        if (name == "Ambience")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 2.91f, 0.74f, 0.75f, 0.18f, 1.05f, 1.03f, 1.10f, 793.0f, 0.70f, 0.89f, 0.56f, 20.0f, 10070.0f, 1.03f, 1.28f, 20.0f, 1.12f, 4566.0f, 0.25f);
        if (name == "1981 Gated Snare")
            return makePreset (name.toRawUTF8(), 6, 1.0f, true, 0.0f, 1.67f, 0.96f, 0.22f, 3.00f, 1.05f, 2.18f, 2197.0f, 0.83f, 0.00f, 0.00f, 20.0f, 4976.0f, 1.14f, -16.6f, 100.0f, 0.98f, 5302.0f, 0.34f);
        if (name == "Reverse Taps")
            return makePreset (name.toRawUTF8(), 6, 1.0f, true, 30.0f, 0.44f, 0.70f, 0.13f, 2.81f, 1.15f, 1.90f, 203.0f, 0.95f, 0.00f, 0.30f, 20.0f, 19158.0f, 1.04f, 0.0f, 20.0f, 0.67f, 7070.0f, 0.02f);

        return getLushDarkHall();
    }

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
    {
        file.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::FileOutputStream> stream (new juce::FileOutputStream (file));
        if (! stream->openedOk())
        {
            std::cerr << "Failed to open " << file.getFullPathName() << std::endl;
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer (
            fmt.createWriterFor (stream.get(),
                                 sr,
                                 (unsigned int) buf.getNumChannels(),
                                 32,
                                 {},
                                 0));
        if (writer == nullptr)
        {
            std::cerr << "Failed to create WAV writer for " << file.getFullPathName() << std::endl;
            return false;
        }
        stream.release();

        return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    // Build a stereo impulse: a single +1.0 sample at t=0, then silence.
    void fillImpulse (juce::AudioBuffer<float>& buf)
    {
        buf.clear();
        buf.setSample (0, 0, 1.0f);
        buf.setSample (1, 0, 1.0f);
    }

    // Load a stereo WAV file from disk into the front of buf, leaving the
    // remainder as silence (so the plugin can render the reverb tail past
    // the dry input). Returns true on success. Used for the snare render —
    // a real percussive transient reveals burst-loudness disparities that
    // a steady sine tone hides.
    bool fillFromWav (juce::AudioBuffer<float>& buf, const juce::File& wavFile)
    {
        buf.clear();
        if (! wavFile.existsAsFile())
        {
            std::cerr << "Test signal WAV not found: " << wavFile.getFullPathName() << std::endl;
            return false;
        }
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wavFile));
        if (reader == nullptr)
        {
            std::cerr << "Could not open WAV: " << wavFile.getFullPathName() << std::endl;
            return false;
        }
        const int n = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples,
                                                                buf.getNumSamples()));
        reader->read (&buf, 0, n, 0, true, true);
        return true;
    }

    // Build a continuous stereo 1 kHz sine tone at the requested RMS dBFS
    // level. Used for steady-state wet-gain measurement: by ~1.5 s the
    // reverb tank has reached its asymptotic level for any RT60 ≲ 1.5 s
    // and the back-half RMS gives a reliable wet-gain reading at 1 kHz.
    void fillSineTone (juce::AudioBuffer<float>& buf, double sr, double freqHz, double rmsDb)
    {
        const double peakAmp  = std::pow (10.0, rmsDb / 20.0) * std::sqrt (2.0);
        const double phaseInc = 2.0 * juce::MathConstants<double>::pi * freqHz / sr;
        double phase = 0.0;
        const int N = buf.getNumSamples();
        for (int n = 0; n < N; ++n)
        {
            const float s = static_cast<float> (peakAmp * std::sin (phase));
            buf.setSample (0, n, s);
            buf.setSample (1, n, s);
            phase += phaseInc;
            if (phase > 2.0 * juce::MathConstants<double>::pi)
                phase -= 2.0 * juce::MathConstants<double>::pi;
        }
    }

    // Build a stereo pink-noise burst: 100 ms of decorrelated pink noise,
    // then silence. Tail measurement requires the signal to actually stop so
    // we can observe the decay envelope cleanly.
    void fillNoiseBurst (juce::AudioBuffer<float>& buf, double sr)
    {
        buf.clear();
        const int burstSamples = static_cast<int> (sr * 0.1);
        juce::Random rngL (0xC0FFEE), rngR (0xBADBEEF);
        // Pink filter: simple Voss-McCartney-ish with 3 octave taps.
        float bL[3] = {}, bR[3] = {};
        for (int n = 0; n < burstSamples; ++n)
        {
            const float wL = rngL.nextFloat() * 2.0f - 1.0f;
            const float wR = rngR.nextFloat() * 2.0f - 1.0f;
            bL[0] = 0.99765f * bL[0] + wL * 0.0990460f;
            bL[1] = 0.96300f * bL[1] + wL * 0.2965164f;
            bL[2] = 0.57000f * bL[2] + wL * 1.0526913f;
            bR[0] = 0.99765f * bR[0] + wR * 0.0990460f;
            bR[1] = 0.96300f * bR[1] + wR * 0.2965164f;
            bR[2] = 0.57000f * bR[2] + wR * 1.0526913f;
            buf.setSample (0, n, 0.1f * (bL[0] + bL[1] + bL[2]));
            buf.setSample (1, n, 0.1f * (bR[0] + bR[1] + bR[2]));
        }
    }

    // Sustained pink-noise stimulus: pumps `holdSec` seconds of continuous
    // pink noise into the engine to reach a true steady-state, then silence
    // for the remainder of the buffer to measure the post-input decay tail.
    // This is the perceptual-match stimulus for musical content — the
    // 100 ms noiseburst doesn't drive the engine into modal steady-state,
    // so it can't expose frequency-dependent decay times the way the user
    // hears them on sustained vocal/instrument input.
    void fillSustainedPink (juce::AudioBuffer<float>& buf, double sr, double holdSec)
    {
        buf.clear();
        const int holdSamples = std::min (buf.getNumSamples(),
                                          static_cast<int> (sr * holdSec));
        juce::Random rngL (0xC0FFEE), rngR (0xBADBEEF);
        float bL[3] = {}, bR[3] = {};
        for (int n = 0; n < holdSamples; ++n)
        {
            const float wL = rngL.nextFloat() * 2.0f - 1.0f;
            const float wR = rngR.nextFloat() * 2.0f - 1.0f;
            bL[0] = 0.99765f * bL[0] + wL * 0.0990460f;
            bL[1] = 0.96300f * bL[1] + wL * 0.2965164f;
            bL[2] = 0.57000f * bL[2] + wL * 1.0526913f;
            bR[0] = 0.99765f * bR[0] + wR * 0.0990460f;
            bR[1] = 0.96300f * bR[1] + wR * 0.2965164f;
            bR[2] = 0.57000f * bR[2] + wR * 1.0526913f;
            buf.setSample (0, n, 0.1f * (bL[0] + bL[1] + bL[2]));
            buf.setSample (1, n, 0.1f * (bR[0] + bR[1] + bR[2]));
        }
        // Samples [holdSamples .. end] remain silence so the decay tail
        // post-input shows the engine's natural per-band decay.
    }

    // Locate parameter by display name. The AU wrapper hashes the original
    // string IDs to integers, but the display names survive — and they're
    // what the user sees in Logic too, so this also makes the test honest.
    juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& plugin, const juce::String& name)
    {
        for (auto* p : plugin.getParameters())
        {
            if (p->getName (50) == name)
                return p;
        }
        return nullptr;
    }

    // Map a plugin file path to the JUCE format that should load it.
    // Returns nullptr if the format isn't compiled in or the extension is
    // unrecognised. The format manager should already have addDefaultFormats()
    // called on it.
    juce::AudioPluginFormat* findFormatForPath (juce::AudioPluginFormatManager& fm,
                                                 const juce::File& path)
    {
        const juce::String ext = path.getFileExtension().toLowerCase();
        juce::String wanted;
        if      (ext == ".component")           wanted = "AudioUnit";
        else if (ext == ".vst3")                wanted = "VST3";
        else if (ext == ".so" || ext == ".dll") wanted = "VST";
        else                                    return nullptr;

        for (auto* f : fm.getFormats())
            if (f->getName() == wanted)
                return f;
        return nullptr;
    }

    // Apply a Valhalla-style .vpreset XML directly: each XML attribute is a
    // parameter name with an already-normalised 0..1 value. Skip the XML
    // metadata attributes (version, encoding, pluginVersion, presetName).
    void applyVpresetXml (juce::AudioPluginInstance& plugin, const juce::File& xmlFile,
                          int perParamDelayMs = 0)
    {
        auto xml = juce::XmlDocument::parse (xmlFile);
        if (xml == nullptr)
        {
            std::cerr << "Failed to parse vpreset XML: " << xmlFile.getFullPathName() << std::endl;
            return;
        }
        std::cout << "Applying vpreset: " << xmlFile.getFileName() << std::endl;
        const juce::StringArray skipAttrs { "version", "encoding", "pluginVersion", "presetName" };
        for (int i = 0; i < xml->getNumAttributes(); ++i)
        {
            // XML attribute names can't contain spaces or slashes, but host
            // param names often do ("Bass Offset", "Dry/Wet"). Translate:
            //   __ → /     (e.g. Dry__Wet  → "Dry/Wet")
            //   _  → space (e.g. Bass_Offset → "Bass Offset")
            juce::String name = xml->getAttributeName (i)
                                    .replace ("__", "/")
                                    .replace ("_", " ");
            if (skipAttrs.contains (name)) continue;
            const juce::String rawText = xml->getAttributeValue (i);
            const float raw = rawText.getFloatValue();
            auto* p = findParam (plugin, name);
            if (p == nullptr)
            {
                std::cerr << "  ! parameter not found: " << name << std::endl;
                continue;
            }
            // Same heuristic as applyPreset: if the value is > 1, treat it
            // as a raw display value (e.g. "10" seconds, "540" Hz) and ask
            // the plugin to convert text → normalised. This makes a single
            // .vpreset file work both for our DuskVerb (normalised values)
            // and for hosted plugins like Arturia LX-24 (raw display values).
            float normalised;
            if (raw > 1.0f)
            {
                normalised = p->getValueForText (rawText);
            }
            else
            {
                normalised = p->getValueForText (rawText);
                if (normalised == 0.0f && raw > 0.0f && raw <= 1.0f)
                    normalised = raw;
            }
            // Hard clamp to [0, 1] before pushing. Some VST2s (Lex PCM
            // Native) return the raw display value verbatim from
            // getValueForText for negative-dB inputs ("-3.0 dB" → -3.0),
            // which then gets reinterpreted as a huge positive coefficient
            // inside the plugin (we saw read_back='2.2e+07') and crashes
            // Wine on the next processBlock. Skip writes that fail to
            // produce a sane normalisation so the plugin stays at its
            // safe default for that param.
            if (! (normalised >= 0.0f && normalised <= 1.0f))
            {
                std::cerr << "  ! skipping " << name << ": getValueForText('"
                          << rawText << "') returned " << normalised
                          << " (out of [0,1])" << std::endl;
                continue;
            }
            p->setValueNotifyingHost (normalised);
            std::cout << "  " << name << " set=" << rawText << "  norm=" << normalised
                      << "  read_back='" << p->getText (p->getValue(), 50) << "'" << std::endl;
            if (perParamDelayMs > 0)
                juce::Thread::sleep (perParamDelayMs);
        }
    }

    // Apply an Apple-format `.aupreset` (binary plist with embedded JUCE
    // plugin state). Used to render through hosted JUCE-built AUs (e.g.
    // Valhalla Shimmer) at one of their factory presets, for A/B comparison.
    // The .aupreset is XML-on-the-outside but the relevant payload is the
    // base64-encoded `jucePluginState` data element, which is exactly what
    // `setStateInformation` consumes.
    void applyAuPreset (juce::AudioPluginInstance& plugin, const juce::File& presetFile)
    {
        auto xml = juce::XmlDocument::parse (presetFile);
        if (xml == nullptr)
        {
            std::cerr << "Failed to parse aupreset: " << presetFile.getFullPathName() << std::endl;
            return;
        }
        auto* dict = xml->getChildByName ("dict");
        if (dict == nullptr) { std::cerr << "aupreset: no <dict>\n"; return; }

        // Strategy: prefer 'jucePluginState' (works for JUCE-built plugins
        // like Valhalla Shimmer); fall back to passing the whole .aupreset
        // as a binary plist (works for non-JUCE AUs like Arturia LX-24,
        // whose state lives under the 'data' key in Apple ClassInfo format
        // — JUCE's AU wrapper routes the whole serialized plist to the AU's
        // kAudioUnitProperty_ClassInfo when setStateInformation can't find
        // a JUCE wrapper).
        juce::String jucePluginStateB64;
        for (auto* el = dict->getFirstChildElement(); el != nullptr; el = el->getNextElement())
        {
            if (el->hasTagName ("key") && el->getAllSubText().trim() == "jucePluginState")
            {
                if (auto* dataEl = el->getNextElement())
                    if (dataEl->hasTagName ("data"))
                        jucePluginStateB64 = dataEl->getAllSubText();
                break;
            }
        }

        if (jucePluginStateB64.isNotEmpty())
        {
            jucePluginStateB64 = jucePluginStateB64.removeCharacters (" \t\r\n");
            juce::MemoryOutputStream raw;
            if (! juce::Base64::convertFromBase64 (raw, jucePluginStateB64))
            {
                std::cerr << "aupreset: base64 decode failed\n"; return;
            }
            std::cout << "Applying aupreset (JUCE state): " << presetFile.getFileName()
                      << " (" << raw.getDataSize() << " bytes)" << std::endl;
            plugin.setStateInformation (raw.getData(), static_cast<int> (raw.getDataSize()));
        }
        else
        {
            // Apple AU expects binary plist for kAudioUnitProperty_ClassInfo,
            // but .aupreset files on disk are XML plist. Convert via macOS
            // `plutil -convert binary1` to a temp file, then read the bytes.
            // Going through plutil avoids linking against CoreFoundation here.
            //
            // SECURITY: pass arguments to ChildProcess as a StringArray
            // (not a single shell-interpreted string) so a malicious preset
            // path containing backticks, $(...), embedded quotes, etc.
            // can't escape into a shell command. JUCE quotes paths but does
            // not escape shell metacharacters.
            auto tmpBin = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("aupreset_bin_" + juce::String (juce::Time::getMillisecondCounter()) + ".plist");
            tmpBin.deleteFile();

            juce::ChildProcess proc;
            const juce::StringArray plutilArgs {
                "/usr/bin/plutil", "-convert", "binary1",
                "-o", tmpBin.getFullPathName(),
                presetFile.getFullPathName()
            };
            const bool started  = proc.start (plutilArgs);
            const bool finished = started && proc.waitForProcessToFinish (10000 /* 10s */);
            const int  rc       = finished ? proc.getExitCode() : -1;
            if (rc != 0 || ! tmpBin.existsAsFile())
            {
                std::cerr << "aupreset: plutil conversion failed (rc=" << rc << ")\n";
                tmpBin.deleteFile();   // Clean up any partial output plutil may have written
                return;
            }
            juce::MemoryBlock binPlist;
            if (! tmpBin.loadFileAsData (binPlist))
            {
                std::cerr << "aupreset: failed to read converted binary plist\n";
                tmpBin.deleteFile();
                return;
            }
            tmpBin.deleteFile();
            std::cout << "Applying aupreset (Apple ClassInfo, binary plist): "
                      << presetFile.getFileName() << " (" << binPlist.getSize() << " bytes)" << std::endl;
            plugin.setStateInformation (binPlist.getData(), static_cast<int> (binPlist.getSize()));
        }

        juce::MemoryBlock readBack;
        plugin.getStateInformation (readBack);
        std::cout << "  read-back state size: " << readBack.getSize() << " bytes" << std::endl;

        // Dump the post-load parameter values so we can verify which params
        // actually picked up new values (vs which defaulted because the
        // setStateInformation path didn't carry them through).
        std::cout << "  --- post-load parameter values ---" << std::endl;
        const auto& params = plugin.getParameters();
        for (int i = 0; i < std::min<int> (25, params.size()); ++i)
        {
            auto* p = params[i];
            std::cout << "    [" << i << "] '" << p->getName (50) << "' = "
                      << p->getValue() << "  (text: '" << p->getText (p->getValue(), 50) << "')\n";
        }
    }

    void applyPreset (juce::AudioPluginInstance& plugin, const PresetParams& preset,
                      int perParamDelayMs = 0)
    {
        std::cout << "Applying preset: " << preset.name << std::endl;
        for (const auto& [name, raw] : preset.values)
        {
            auto* p = findParam (plugin, name);
            if (p == nullptr)
            {
                std::cerr << "  ! parameter not found: " << name << std::endl;
                continue;
            }
            // Hosted plugin parameters are surfaced as plain
            // AudioProcessorParameter (not RangedAudioParameter) — the host
            // can only see them as 0..1. For most parameters (float ranges,
            // bools), getValueForText round-trips through the plugin's own
            // text<->normalised formatter. For choice parameters the text
            // representation is the choice NAME, not the index, so we'd have
            // to pre-normalise — handled at the preset definition site.
            float normalised = p->getValueForText (juce::String (raw));
            // Heuristic: if getValueForText returned 0 BUT the user asked for
            // a small (0..1] value, the param probably didn't accept the text
            // (e.g. choice/bool params reject "1.0" because they expect "On").
            // Treat the input as already-normalised. Crucially we DON'T fire
            // this for raw values > 1 (e.g. Mono Below = 20 Hz at its minimum
            // legitimately gets normalised = 0).
            if (normalised == 0.0f && raw > 0.0f && raw <= 1.0f)
                normalised = raw;
            // The "Algorithm" choice exposes engine NAMES, not indices, and is
            // wrapped as a generic AudioProcessorParameter whose getNumSteps()
            // doesn't report the choice count — so getValueForText("3") returns
            // 0 and every index used to route to engine 0. Special-case it:
            // map the engine index to normalised by the same divisor the preset
            // table uses (kAlgorithmDivisor = numAlgorithms-1).
            else if (name == "Algorithm" && raw >= 0.0f)
                normalised = raw / kAlgorithmDivisor;
            // Reject NaN/inf and hard-clamp to [0,1] before notifying the host
            // (mirror applyVpresetXml's guard) so out-of-range text never feeds
            // a garbage normalised value into the parameter.
            if (! std::isfinite (normalised))
            {
                std::cout << "  " << name << " SKIPPED (non-finite value)\n";
                continue;
            }
            normalised = juce::jlimit (0.0f, 1.0f, normalised);
            p->setValueNotifyingHost (normalised);
            const float readBack = p->getValue();
            const juce::String readBackText = p->getText (readBack, 50);
            std::cout << "  " << name << " set=" << raw
                      << "  norm=" << normalised
                      << "  read_back='" << readBackText << "'"
                      << std::endl;
            if (perParamDelayMs > 0)
                juce::Thread::sleep (perParamDelayMs);
        }
    }

    // Render a buffer of input through the plugin in fixed-size blocks.
    // The processBlock buffer must have max(totalInputChannels,
    // totalOutputChannels) channels so multi-bus plugins (Arturia LX-24
    // exposes 2 stereo input buses = 4 channels) don't read past the
    // buffer end and segfault.
    juce::AudioBuffer<float> renderThroughPlugin (juce::AudioPluginInstance& plugin,
                                                   const juce::AudioBuffer<float>& input)
    {
        const int total      = input.getNumSamples();
        const int inChans    = plugin.getTotalNumInputChannels();
        const int outChans   = plugin.getTotalNumOutputChannels();
        const int blockChans = std::max (inChans, outChans);
        const int copyChans  = std::min (input.getNumChannels(), inChans);
        const int outCopy    = std::min (outChans, 2);  // output WAV is stereo

        juce::AudioBuffer<float> output (outCopy, total);
        output.clear();

        juce::AudioBuffer<float> block (blockChans, kBlockSize);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < total; pos += kBlockSize)
        {
            const int n = std::min (kBlockSize, total - pos);
            block.clear();
            for (int ch = 0; ch < copyChans; ++ch)
                block.copyFrom (ch, 0, input, ch, pos, n);

            plugin.processBlock (block, midi);

            for (int ch = 0; ch < outCopy; ++ch)
                output.copyFrom (ch, pos, block, ch, 0, n);
        }
        return output;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;

    // CLI: see file header. Plugin path is auto-detected from extension
    // (.component → AU, .vst3 → VST3, .so/.dll → VST2). The --au/--vst3/--vst2
    // flags are equivalent — they just set the plugin path.
    juce::String presetName  = "Lush Dark Hall";
    bool         presetExplicit = false;          // user named a preset on the CLI
    juce::String pluginPath  = kDefaultPluginPath;
    juce::String vpresetPath;
    juce::String aupresetPath;
    juce::String slugArg;
    juce::String outDirArg;
    // Arbitrary stem input: when set, load WAV, pad with reverb-tail
    // headroom, render through the configured engine, write to
    // {outDir}/{slug}_stem.wav. Lets users A/B real-world stems against
    // Lexicon reference renders without going through a DAW.
    juce::String inputWavPath;
    juce::String programArg;            // factory program by name
    int          programIndex   = -1;   // factory program by index
    juce::String saveStatePath;         // dump getStateInformation bytes here after preset
    juce::String loadStatePath;         // setStateInformation from these bytes (one round-trip)
    int          waitAfterLoadMs = 0;   // for yabridge handshake races
    int          perParamDelayMs = 0;   // throttle preset apply for slow plugin bridges
    // SixAPTank-specific engine-state property overrides. Useful for tuner
    // workflows where the tuner sweeps engine-internal params that aren't
    // APVTS-exposed. Applied via getStateInformation → setProperty →
    // setStateInformation roundtrip after the regular --param apply path.
    // -1.0 sentinel = leave plugin default (don't inject).
    float        sixAPEarlyHighpassHz = -1.0f;
    float        sixAPEarlyMixOverride = -1.0f;  // -1 = no override
    bool         listParamsOnly   = false;
    bool         listProgramsOnly = false;
    bool         dumpParams       = false;   // --dump-params: emit baked program
                                             // params as JSON (Optuna warm-start
                                             // seed), then exit before rendering.
    bool         prePrepareApply  = false;
    // Dry-passthrough test: override bus_mode=0 + mix=0 on the loaded preset
    // so the engine's wet path is silent and only the dry passes through.
    // Used to verify that gain_trim does not bleed into the dry signal.
    bool dryPassthroughTest = false;
    bool forceGateOff       = false;   // --gate-off : force gate_enabled = 0 after preset apply
    // --prerun-seconds N: pump N seconds of stereo silence through the plugin
    // before each test stimulus. Settles SmoothedValues to targets, primes
    // biquad z-state, and lets random-walk LFOs drift into a steady realization
    // that matches what a DAW listener hears (where the plugin is always warm).
    // Default 5.0 s = convergence-tested standard. Below ~3 s, modulator
    // hasn't reached steady state and metrics drift 5-10% from converged
    // values. 5 s converges t60 within 1% and centroid within ±3% stochastic
    // noise floor. Override only for very fast iteration during dev.
    double prerunSeconds = 5.0;
    // --sustained-pink-seconds N: emit an EXTRA stimulus render after the
    // standard impulse/noiseburst/snare/sine set. N seconds of continuous
    // pink noise then N seconds of silence — captures the engine's
    // true steady-state per-band decay (the way musical content drives a
    // reverb, not a 100 ms burst). 0 = disabled.
    double sustainedPinkSeconds = 0.0;
    // Per-parameter overrides via --param NAME=VALUE. Stored in declaration
    // order so multiple --param flags compose the way the user wrote them
    // (last write wins). Applied AFTER the preset so they override anything
    // the preset set. Works against any plugin — DuskVerb, Valhalla, Lex, etc.
    std::vector<std::pair<juce::String, juce::String>> paramOverrides;
    std::vector<std::pair<juce::String, juce::String>> nparamOverrides;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a = argv[i];
        if      (a == "--au"        && i + 1 < argc) pluginPath   = argv[++i];
        else if (a == "--vst3"      && i + 1 < argc) pluginPath   = argv[++i];
        else if (a == "--vst2"      && i + 1 < argc) pluginPath   = argv[++i];
        else if (a == "--vpreset"   && i + 1 < argc) vpresetPath  = argv[++i];
        else if (a == "--aupreset"  && i + 1 < argc) aupresetPath = argv[++i];
        else if (a == "--slug"      && i + 1 < argc) slugArg      = argv[++i];
        else if (a == "--output-dir" && i + 1 < argc) outDirArg   = argv[++i];
        else if (a == "--input-wav"  && i + 1 < argc) inputWavPath= argv[++i];
        else if (a == "--program"   && i + 1 < argc) programArg   = argv[++i];
        else if (a == "--program-index" && i + 1 < argc)
                                                     programIndex = juce::String (argv[++i]).getIntValue();
        else if (a == "--wait-after-load" && i + 1 < argc)
                                                     waitAfterLoadMs = juce::String (argv[++i]).getIntValue();
        else if (a == "--per-param-delay-ms" && i + 1 < argc)
                                                     perParamDelayMs = juce::String (argv[++i]).getIntValue();
        else if (a == "--save-state" && i + 1 < argc) saveStatePath = argv[++i];
        else if (a == "--sixap-early-hpf" && i + 1 < argc)
                                                     sixAPEarlyHighpassHz = juce::String (argv[++i]).getFloatValue();
        else if (a == "--sixap-early-mix" && i + 1 < argc)
                                                     sixAPEarlyMixOverride = juce::String (argv[++i]).getFloatValue();
        else if (a == "--load-state" && i + 1 < argc) loadStatePath = argv[++i];
        else if (a == "--list-params")              listParamsOnly   = true;
        else if (a == "--list-programs")            listProgramsOnly = true;
        else if (a == "--dump-params")              dumpParams       = true;
        else if (a == "--pre-prepare-apply")        prePrepareApply  = true;
        else if (a == "--dry-passthrough-test")     dryPassthroughTest = true;
        else if (a == "--gate-off")                 forceGateOff = true;
        else if (a == "--prerun-seconds" && i + 1 < argc)
            prerunSeconds = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--sustained-pink-seconds" && i + 1 < argc)
            sustainedPinkSeconds = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--param" && i + 1 < argc)
        {
            // Format: NAME=VALUE  (NAME may contain spaces; matches the
            // display name shown by --list-params, e.g. --param "wetDry=1.0"
            // or --param "Bus Mode=On")
            const juce::String spec = argv[++i];
            const int eq = spec.indexOfChar ('=');
            if (eq > 0 && eq < spec.length() - 1)
                paramOverrides.emplace_back (spec.substring (0, eq).trim(),
                                             spec.substring (eq + 1).trim());
            else
                std::cerr << "  ! ignoring malformed --param '" << spec << "' (expected NAME=VALUE)" << std::endl;
        }
        else if (a == "--nparam" && i + 1 < argc)
        {
            // Format: NAME=NORMVALUE — sets the parameter's NORMALISED value
            // directly, bypassing getValueForText. Needed when replaying a
            // plugin's own saved state (e.g. a Valhalla .vstpreset XML, whose
            // attributes are normalised 0..1 floats): the text parser would
            // misread "0.2299" as 0.23 seconds/percent/etc.
            const juce::String spec = argv[++i];
            const int eq = spec.indexOfChar ('=');
            if (eq > 0 && eq < spec.length() - 1)
                nparamOverrides.emplace_back (spec.substring (0, eq).trim(),
                                              spec.substring (eq + 1).trim());
            else
                std::cerr << "  ! ignoring malformed --nparam '" << spec << "' (expected NAME=NORMVALUE)" << std::endl;
        }
        else if (! a.startsWith ("--"))
        {
            presetName     = a;
            presetExplicit = true;
        }
    }

    juce::File pluginFile (pluginPath);
    if (! pluginFile.exists())
    {
        std::cerr << "Plugin not found at " << pluginPath << std::endl;
        return 1;
    }

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();

    juce::AudioPluginFormat* format = findFormatForPath (fm, pluginFile);
    if (format == nullptr)
    {
        std::cerr << "No JUCE format registered for " << pluginPath
                  << " (extension: " << pluginFile.getFileExtension() << ")\n"
                  << "Make sure the matching JUCE_PLUGINHOST_* flag is set at build time:\n"
                  << "  .component → JUCE_PLUGINHOST_AU=1   (macOS only)\n"
                  << "  .vst3      → JUCE_PLUGINHOST_VST3=1\n"
                  << "  .so / .dll → JUCE_PLUGINHOST_VST=1  (requires VST2_SDK_DIR at build time)"
                  << std::endl;
        return 1;
    }

    juce::OwnedArray<juce::PluginDescription> typesFound;
    format->findAllTypesForFile (typesFound, pluginPath);
    if (typesFound.isEmpty())
    {
        std::cerr << "Plugin scan via " << format->getName() << " returned no types from "
                  << pluginPath << std::endl;
        return 1;
    }
    std::cout << "Found " << format->getName() << " type: " << typesFound[0]->name
              << " (manufacturer: " << typesFound[0]->manufacturerName << ")" << std::endl;

    juce::String error;
    auto plugin = fm.createPluginInstance (*typesFound[0], kSampleRate, kBlockSize, error);
    if (plugin == nullptr)
    {
        std::cerr << "Failed to instantiate plugin: " << error << std::endl;
        return 1;
    }

    // Build a stereo bus layout that matches the plugin's bus topology.
    // Most reverbs are 1-in/1-out, but some (Arturia Rev LX-24) expose a
    // sidechain or auxiliary input bus. If we configure too few buses
    // here the plugin's processBlock segfaults trying to read from an
    // unprepared channel.
    juce::AudioProcessor::BusesLayout layout;
    for (int i = 0; i < plugin->getBusCount (true);  ++i)
        layout.inputBuses.add  (juce::AudioChannelSet::stereo());
    for (int i = 0; i < plugin->getBusCount (false); ++i)
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
    if (! plugin->setBusesLayout (layout))
        std::cerr << "Warning: could not force stereo bus layout (in="
                  << plugin->getBusCount (true) << ", out=" << plugin->getBusCount (false)
                  << ")" << std::endl;

    plugin->prepareToPlay (kSampleRate, kBlockSize);

    // Some yabridge-bridged plugins need a moment after prepareToPlay before
    // the Wine-side dispatcher is fully responsive (yabridge issues #167/#391).
    if (waitAfterLoadMs > 0)
        juce::Thread::sleep (waitAfterLoadMs);

    if (listProgramsOnly)
    {
        std::cout << "=== Programs of " << typesFound[0]->name
                  << " (" << plugin->getNumPrograms() << " total) ===\n";
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
            std::cout << "  [" << i << "] " << plugin->getProgramName (i) << std::endl;
        plugin->releaseResources();
        return 0;
    }

    if (listParamsOnly)
    {
        std::cout << "=== Bus layout of " << typesFound[0]->name << " ===\n";
        for (int i = 0; i < plugin->getBusCount (true); ++i)
            std::cout << "  in["  << i << "]: " << plugin->getChannelLayoutOfBus (true,  i).getDescription() << std::endl;
        for (int i = 0; i < plugin->getBusCount (false); ++i)
            std::cout << "  out[" << i << "]: " << plugin->getChannelLayoutOfBus (false, i).getDescription() << std::endl;
        std::cout << "=== Parameters of " << typesFound[0]->name << " ===\n";
        const auto& params = plugin->getParameters();
        for (int i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            const juce::String name = p->getName (100);
            const float val = p->getValue();
            const juce::String text = p->getText (val, 100);
            std::cout << "  [" << i << "] '" << name << "' = " << val
                      << "  (text: '" << text << "')";
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto& r = rp->getNormalisableRange();
                std::cout << "  range=[" << r.start << ".." << r.end << "]";
            }
            std::cout << std::endl;
        }
        plugin->releaseResources();
        return 0;
    }

    // Detect non-DuskVerb plugins: when the user gives `--au <other-plugin>`
    // and doesn't name a preset, don't try to apply DuskVerb's "Lush Dark
    // Hall" default — the param names won't match and we'd just spam
    // "parameter not found" warnings. The user can still get a useful
    // baseline render at the plugin's own default state, plus any
    // --param overrides.
    const bool isDuskVerb = typesFound[0]->name.equalsIgnoreCase ("DuskVerb");
    const bool haveExternalPreset = aupresetPath.isNotEmpty() || vpresetPath.isNotEmpty();

    // Cache the resolved DuskVerb preset (when applicable) so we can
    // inject its non-APVTS engine-config state after parameters are set.
    PresetParams duskVerbPreset;
    bool haveDuskVerbPreset = false;
    if (! aupresetPath.isNotEmpty() && ! vpresetPath.isNotEmpty()
        && (isDuskVerb || presetExplicit))
    {
        duskVerbPreset = getPresetByName (presetName);
        haveDuskVerbPreset = true;
    }

    // Resolve --program <name> to an index by scanning getProgramName(). Done
    // once up front so we don't re-scan on every applyAnyPreset() pass.
    int resolvedProgramIndex = programIndex;

    // SINGLE SOURCE OF TRUTH (2026-06-08): for DuskVerb itself, a `--preset
    // <name>` that matches a real factory PROGRAM is routed through
    // setCurrentProgram — i.e. the plugin's own FactoryPresets.h config — NOT
    // the hand-transcribed makePreset() mirror in this file. The mirror drifts
    // stale (it once shipped Saturation 0.32 / Decay 2.04 for "Vocal Hall"
    // while the real preset had Sat 0.0 / Decay 3.5), so every --preset render
    // measured a phantom config no shipped plugin uses. Resolving to the
    // program kills that footgun: --preset now == what the DAW loads. The
    // mirror is kept ONLY as a fallback for names with no matching program
    // (e.g. external Lex/Valhalla A/B definitions). --param overrides (Dry/Wet,
    // Bus Mode) still apply on top, exactly as before.
    if (isDuskVerb && resolvedProgramIndex < 0 && programArg.isEmpty()
        && presetExplicit && loadStatePath.isEmpty())
    {
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
        {
            if (plugin->getProgramName (i).trim().equalsIgnoreCase (presetName.trim()))
            {
                resolvedProgramIndex = i;
                haveDuskVerbPreset   = false;   // prefer the real program over the mirror
                std::cout << "  --preset '" << presetName << "' resolved to factory program ["
                          << i << "] (real FactoryPresets.h config, not the mirror)\n";
                break;
            }
        }
    }

    if (resolvedProgramIndex < 0 && programArg.isNotEmpty())
    {
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
        {
            if (plugin->getProgramName (i).trim() == programArg.trim())
            {
                resolvedProgramIndex = i;
                break;
            }
        }
        if (resolvedProgramIndex < 0)
        {
            std::cerr << "Program not found: '" << programArg << "'. Use --list-programs to enumerate.\n";
            return 1;
        }
    }

    auto applyAnyPreset = [&]()
    {
        if (loadStatePath.isNotEmpty())
        {
            // Single-dispatcher state load — bypasses per-param round-trips
            // entirely. Useful for slow Wine-bridged plugins where setting
            // 50+ parameters via setValueNotifyingHost is prohibitively
            // expensive (one effSetChunk call vs N IPC dispatches).
            juce::File f (loadStatePath);
            juce::MemoryBlock blob;
            if (! f.loadFileAsData (blob))
            {
                std::cerr << "Failed to read state from " << loadStatePath << std::endl;
                return;
            }
            // Pass raw file bytes to setStateInformation. JUCE's VST2 host
            // wrapper detects .fxp ('CcnK FPCh') / .fxb ('CcnK FBCh')
            // framing internally and unwraps it before dispatching to
            // effSetChunk — stripping the header here breaks the framing.
            // For VST3 / AU plugins the bytes are whatever
            // getStateInformation emitted (typically a JUCE ValueTree XML),
            // also passed verbatim.
            std::cout << "Loading state (" << blob.getSize() << " bytes) from "
                      << loadStatePath << std::endl;
            plugin->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
            return;
        }
        if (resolvedProgramIndex >= 0)
        {
            std::cout << "Selecting factory program [" << resolvedProgramIndex
                      << "] " << plugin->getProgramName (resolvedProgramIndex) << std::endl;
            // JUCE's host wrapper NO-OPS setCurrentProgram(idx) when idx already
            // equals the plugin's current program — which it is for program 0
            // (the default). That silently skips applyFactoryPreset() → the
            // preset's name-keyed map overrides (FiveBand, PostTankEQ, …) never
            // install and the render measures only APVTS defaults. ONLY program
            // 0 needs this (it's the default → no-op); non-zero targets already
            // register as a change. Toggle off the default first for index 0 so
            // the target applies. A decoy for non-zero targets would double-apply
            // and perturb the rendered tail, so guard it to index 0. (The plugin
            // editor applies presets via applyFactoryPreset() directly, so the
            // DAW is unaffected; this is a harness-only fix.)
            if (resolvedProgramIndex == 0 && plugin->getNumPrograms() > 1)
                plugin->setCurrentProgram (1);
            plugin->setCurrentProgram (resolvedProgramIndex);
            return;
        }
        if (aupresetPath.isNotEmpty())
            applyAuPreset (*plugin, juce::File (aupresetPath));
        else if (vpresetPath.isNotEmpty())
            applyVpresetXml (*plugin, juce::File (vpresetPath), perParamDelayMs);
        else if (haveDuskVerbPreset)
            applyPreset (*plugin, duskVerbPreset, perParamDelayMs);
        // else: arbitrary plugin + no preset specifier → leave it in
        // its post-instantiation default state. --param overrides still apply.
    };

    // --param NAME=VALUE overrides. Applied AFTER the preset so they win
    // any conflict. Uses the same value-parsing heuristics as applyPreset:
    // try getValueForText first (handles "On"/"Off", "1.5 kHz", percentages,
    // etc. via the plugin's own formatter); fall back to "treat as already-
    // normalised" for small 0..1 floats when getValueForText returns 0.
    auto applyParamOverrides = [&]()
    {
        if (paramOverrides.empty() && nparamOverrides.empty())
            return;
        for (const auto& [name, valueStr] : paramOverrides)
        {
            auto* p = findParam (*plugin, name);
            if (p == nullptr)
            {
                std::cerr << "  ! --param: parameter '" << name << "' not found" << std::endl;
                continue;
            }
            const float raw = valueStr.getFloatValue();
            float normalised = p->getValueForText (valueStr);
            if (normalised == 0.0f && raw > 0.0f && raw <= 1.0f)
                normalised = raw;       // treat as already-normalised
            // The "Algorithm" choice exposes engine NAMES (not indices) and is
            // wrapped as a generic param whose getNumSteps() doesn't report the
            // choice count, so getValueForText("4") returns 0 and every index
            // used to route to engine 0. Special-case it: map index→normalised
            // by the divisor the preset table uses (numAlgorithms-1).
            else if (name == "Algorithm" && raw >= 0.0f)
                normalised = raw / kAlgorithmDivisor;
            if (! std::isfinite (normalised))
            {
                std::cerr << "  ! --param " << name << ": non-finite, skipped" << std::endl;
                continue;
            }
            normalised = juce::jlimit (0.0f, 1.0f, normalised);
            p->setValueNotifyingHost (normalised);
            std::cout << "  --param " << name << " set='" << valueStr
                      << "' norm=" << normalised
                      << " read_back='" << p->getText (p->getValue(), 50) << "'" << std::endl;
        }
        for (const auto& [name, valueStr] : nparamOverrides)
        {
            auto* p = findParam (*plugin, name);
            if (p == nullptr)
            {
                std::cerr << "  ! --nparam: parameter '" << name << "' not found" << std::endl;
                continue;
            }
            if (! valueStr.containsOnly ("0123456789.+-eE")
                || valueStr.trim().isEmpty())
            {
                std::cerr << "  ! --nparam " << name << ": value '" << valueStr
                          << "' is not a number, skipped" << std::endl;
                continue;
            }
            const float normalised = juce::jlimit (0.0f, 1.0f, valueStr.getFloatValue());
            p->setValueNotifyingHost (normalised);
            std::cout << "  --nparam " << name << " norm=" << normalised
                      << " read_back='" << p->getText (p->getValue(), 50) << "'" << std::endl;
        }
    };

    // Optional first pass: apply the preset BEFORE the final prepareToPlay
    // so the plugin sizes its per-algorithm DSP buffers correctly for any
    // Algorithm change. Required by Arturia Rev LX-24 (segfaults otherwise).
    // Off by default — each param-set on a yabridge-bridged plugin is a Wine
    // IPC round-trip, so double-applying is expensive.
    if (prePrepareApply)
    {
        applyAnyPreset();
        applyParamOverrides();
        plugin->releaseResources();
        plugin->prepareToPlay (kSampleRate, kBlockSize);
    }

    // Authoritative pass: prepareToPlay can reset some plugins' parameter
    // state back to defaults — this final apply ensures the configured
    // values are what the renderer hears.
    applyAnyPreset();
    applyParamOverrides();

    // --dump-params: emit the post-apply parameter values as a JSON dict in
    // DENORMALISED (display) units — exactly what --param NAME=VALUE consumes,
    // so the output round-trips as an Optuna warm-start seed. Choice/discrete
    // params (e.g. Algorithm) are skipped: their text isn't a clean float and
    // the optimizer never sweeps them. Emitted after applyAnyPreset +
    // applyParamOverrides so it captures the baked program (plus any overrides).
    if (dumpParams)
    {
        // Hosted VST3/AU params are host-wrapper proxies, NOT the plugin's
        // RangedAudioParameter subclasses, so dynamic_cast fails. Use the base
        // API: getText() yields the display string; its leading float is the
        // denormalised value --param consumes (getValueForText inverts it).
        // Discrete/choice params (Algorithm, Bus Mode, Freeze, Gate) carry
        // non-numeric text → leading float is junk, but Optuna's seed loader
        // filters to FREE_PARAMS keys, so the extra entries are harmless.
        std::cout << "{";
        bool first = true;
        for (auto* p : plugin->getParameters())
        {
            const juce::String name = p->getName (100);
            if (name.isEmpty())                continue;
            const juce::String text = p->getText (p->getValue(), 100);
            const float raw = text.getFloatValue();   // leading float, 0 if none
            if (! std::isfinite (raw))         continue;
            std::cout << (first ? "" : ", ") << "\"" << name << "\": " << raw;
            first = false;
        }
        std::cout << "}" << std::endl;
        plugin->releaseResources();
        return 0;
    }

    // Inject SixAPTank engine-state properties that aren't reachable via
    // APVTS --param overrides. The tuner needs to sweep these to find
    // optimal Concert-Hall-style early-HPF cutoffs, etc. Path: pull current
    // plugin state as XML → setProperty → push back via setStateInformation.
    // No-op when sixAPEarlyHighpassHz == -1 sentinel.
    if (sixAPEarlyHighpassHz >= 0.0f || sixAPEarlyMixOverride >= 0.0f)
    {
        juce::MemoryBlock blob;
        plugin->getStateInformation (blob);
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(),
                                                                static_cast<int> (blob.getSize())))
        {
            if (sixAPEarlyHighpassHz >= 0.0f)
            {
                xml->setAttribute ("sixAPEarlyHighpassHz", sixAPEarlyHighpassHz);
                std::cout << "Injected sixAPEarlyHighpassHz=" << sixAPEarlyHighpassHz << std::endl;
            }
            if (sixAPEarlyMixOverride >= 0.0f)
            {
                xml->setAttribute ("sixAPEarlyMix", sixAPEarlyMixOverride);
                std::cout << "Injected sixAPEarlyMix=" << sixAPEarlyMixOverride << std::endl;
            }
            juce::MemoryBlock newBlob;
            juce::AudioProcessor::copyXmlToBinary (*xml, newBlob);
            plugin->setStateInformation (newBlob.getData(),
                                          static_cast<int> (newBlob.getSize()));
        }
        else
        {
            std::cerr << "  ! state-tree override: getStateInformation produced "
                      << "non-XML payload — cannot inject" << std::endl;
        }
    }

    // Optionally dump the post-apply plugin state to a binary file so future
    // runs can use --load-state to skip the slow per-param apply (one
    // effSetChunk call instead of N IPC dispatches for yabridge plugins).
    if (saveStatePath.isNotEmpty())
    {
        juce::MemoryBlock blob;
        plugin->getStateInformation (blob);
        juce::File f (saveStatePath);
        f.deleteFile();
        if (f.replaceWithData (blob.getData(), blob.getSize()))
            std::cout << "Saved state (" << blob.getSize() << " bytes) to "
                      << saveStatePath << std::endl;
        else
            std::cerr << "Failed to write state to " << saveStatePath << std::endl;
    }

    // NOTE: per-preset SixAPTank brightness/density values (sixAPDensityBaseline,
    // sixAPBloomCeiling, sixAPBloomStagger, sixAPEarlyMix, sixAPOutputTrim) are
    // applied internally by the plugin via DuskVerbProcessor::applyFactoryPreset
    // when a user selects a preset from the editor dropdown. The render tool
    // can't replicate that path because:
    //   1) plugin->getStateInformation() returns the AU-host-wrapped state,
    //      not the JUCE-XML state directly — getXmlFromBinary can't parse it.
    //   2) Custom DuskVerbProcessor methods aren't reachable via the generic
    //      JUCE AudioPluginInstance interface.
    // So renders here will use the engine's DEFAULT sixAP values regardless of
    // preset.hasSixAP. Verification of the brightness override path needs to
    // happen in a DAW. This affects only Black Hole renders (the only preset
    // that opts in); other SixAPTank presets render bit-identical to before.

    // Pre-roll silence so all parameter smoothers settle, predelay buffer
    // primes, biquad z-state initializes, random-walk LFOs drift into a
    // steady realization, and any startup transients flush out. Critical
    // for heavily modulated algorithmic reverbs — DAW listeners hear the
    // plugin in steady state; tuner-measured impulse from cold-zero state
    // captures the wrong response.
    auto runPreroll = [&plugin, sr = kSampleRate] (double seconds) {
        const int samples = static_cast<int> (sr * std::max (seconds, 0.0));
        if (samples <= 0) return;
        juce::AudioBuffer<float> preRoll (2, samples);
        preRoll.clear();
        renderThroughPlugin (*plugin, preRoll);
    };
    runPreroll (prerunSeconds);

    // Output directory resolution priority:
    //   1. --output-dir CLI flag
    //   2. DUSKVERB_RENDER_OUT environment variable
    //   3. Default: <cwd>/tests/duskverb_render/output (matches the analyze
    //      scripts' Path(__file__).parent / "output" assumption when run from
    //      the repo root). Falls back to <cwd>/duskverb_render_output if the
    //      tests/ folder isn't reachable from cwd.
    juce::String outDirPath = outDirArg;
    if (outDirPath.isEmpty())
    {
        if (auto* env = std::getenv ("DUSKVERB_RENDER_OUT"))
            outDirPath = juce::String::fromUTF8 (env);
    }
    if (outDirPath.isEmpty())
    {
        auto cwd     = juce::File::getCurrentWorkingDirectory();
        auto repoOut = cwd.getChildFile ("tests/duskverb_render/output");
        outDirPath = repoOut.getParentDirectory().isDirectory()
                   ? repoOut.getFullPathName()
                   : cwd.getChildFile ("duskverb_render_output").getFullPathName();
    }
    juce::File outDir (outDirPath);
    outDir.createDirectory();
    std::cout << "Output directory: " << outDir.getFullPathName() << std::endl;

    juce::String defaultSlug = presetName;
    if (resolvedProgramIndex >= 0)
        defaultSlug = plugin->getProgramName (resolvedProgramIndex);
    const juce::String slug = slugArg.isNotEmpty()
                            ? slugArg
                            : defaultSlug.replace (" ", "");

    // --gate-off: force the NonLinear engine's GATE to disabled. Used to
    // verify the toggle is wired and produces an audibly different result.
    if (forceGateOff)
    {
        if (auto* p = findParam (*plugin, "Gate")) p->setValue (0.0f);
        std::cout << "GATE-OFF OVERRIDE: gate_enabled forced to 0" << std::endl;
    }

    // Dry-passthrough test: forcibly override Bus Mode = false and Dry/Wet = 0
    // so the engine outputs only the dry passthrough (the wet path is silent).
    // Used to verify that the gain_trim parameter does not bleed into the dry
    // signal — output should equal input level.
    if (dryPassthroughTest)
    {
        if (auto* p = findParam (*plugin, "Bus Mode"))   p->setValue (0.0f);
        if (auto* p = findParam (*plugin, "Dry/Wet"))    p->setValue (0.0f);
        std::cout << "DRY PASSTHROUGH TEST: Bus Mode=0, Dry/Wet=0 (gain_trim retained)" << std::endl;
        // Skip impulse + noise renders — we only need the sine for measurement.
        const int sineSamples = static_cast<int> (kSampleRate * 2.0);
        juce::AudioBuffer<float> input (2, sineSamples);
        fillSineTone (input, kSampleRate, 1000.0, -12.0);
        auto output = renderThroughPlugin (*plugin, input);
        auto outFile = outDir.getChildFile (slug + "_drytest.wav");
        if (writeWav (outFile, output, kSampleRate))
            std::cout << "Wrote " << outFile.getFullPathName() << std::endl;
        plugin->releaseResources();
        return 0;
    }

    // ---- Render 1: Impulse ----
    {
        juce::AudioBuffer<float> input (2, kTotalSamples);
        fillImpulse (input);
        auto output = renderThroughPlugin (*plugin, input);
        auto outFile = outDir.getChildFile (slug + "_impulse.wav");
        if (writeWav (outFile, output, kSampleRate))
            std::cout << "Wrote " << outFile.getFullPathName() << std::endl;
    }

    // Reset plugin state between renders so noise burst doesn't ride the
    // impulse tail. Re-prerun so smoothers/LFOs settle into a clean,
    // steady realization before the next stimulus fires.
    plugin->reset();
    runPreroll (prerunSeconds);

    // ---- Render 2: Pink noise burst (100ms then silence) ----
    {
        juce::AudioBuffer<float> input (2, kTotalSamples);
        fillNoiseBurst (input, kSampleRate);
        auto output = renderThroughPlugin (*plugin, input);
        auto outFile = outDir.getChildFile (slug + "_noiseburst.wav");
        if (writeWav (outFile, output, kSampleRate))
            std::cout << "Wrote " << outFile.getFullPathName() << std::endl;
    }

    plugin->reset();
    runPreroll (prerunSeconds);

    // ---- Render 4: snare hit (309 ms @ -12 dBFS peak) + 3.7 s tail ----
    // Real-world transient test — broadband percussive content reveals
    // burst-level disparities that a steady sine cannot. Source: Logic Pro
    // acoustic snare (D1 vel 32, normalised to -12 dBFS peak, stereo, 48 kHz).
    {
        const int snareTotalSamples = static_cast<int> (kSampleRate * 4.0);
        juce::AudioBuffer<float> input (2, snareTotalSamples);

        // Resolve the snare WAV by walking common locations:
        //   1) alongside the executable: <build>/tests/duskverb_render/test_signals/
        //   2) the in-source path relative to the executable's parent walks
        //      (build/tests/duskverb_render → ../../../tests/duskverb_render)
        //   3) cwd-relative (when invoked from the project root)
        // Skips the snare render with a single warning if none exist.
        const juce::File exeDir = juce::File::getSpecialLocation (
                                      juce::File::currentExecutableFile).getParentDirectory();
        const juce::File snareWav = [&exeDir]
        {
            const juce::Array<juce::File> candidates = {
                exeDir.getChildFile ("test_signals/snare_-12dB.wav"),
                exeDir.getParentDirectory().getParentDirectory().getParentDirectory()
                      .getChildFile ("tests/duskverb_render/test_signals/snare_-12dB.wav"),
                juce::File::getCurrentWorkingDirectory()
                      .getChildFile ("tests/duskverb_render/test_signals/snare_-12dB.wav"),
            };
            for (const auto& f : candidates)
                if (f.existsAsFile()) return f;
            return juce::File();
        }();
        if (fillFromWav (input, snareWav))
        {
            auto output = renderThroughPlugin (*plugin, input);
            auto outFile = outDir.getChildFile (slug + "_snare.wav");
            if (writeWav (outFile, output, kSampleRate))
                std::cout << "Wrote " << outFile.getFullPathName() << std::endl;
        }
    }

    plugin->reset();
    runPreroll (prerunSeconds);

    // ---- Render 4b: session stem (complex musical material, always on) ----
    // Real dry mix bounce (vocal + instruments, 48k stereo, 2.5 s). Reveals
    // nonlinear / clarity artifacts synthetic stimuli miss — the "trashy
    // highs" the snare and noiseburst don't surface. Auto-resolved like the
    // snare (no flag needed); writes {outDir}/{slug}_session.wav. Padded with
    // a full tail so the decay is captured for residual/distortion analysis.
    {
        const juce::File exeDir = juce::File::getSpecialLocation (
                                      juce::File::currentExecutableFile).getParentDirectory();
        const juce::File sessionWav = [&exeDir]
        {
            const juce::Array<juce::File> candidates = {
                exeDir.getChildFile ("test_signals/session.wav"),
                exeDir.getParentDirectory().getParentDirectory().getParentDirectory()
                      .getChildFile ("tests/duskverb_render/test_signals/session.wav"),
                juce::File::getCurrentWorkingDirectory()
                      .getChildFile ("tests/duskverb_render/test_signals/session.wav"),
            };
            for (const auto& f : candidates)
                if (f.existsAsFile()) return f;
            return juce::File();
        }();

        if (sessionWav.existsAsFile())
        {
            juce::AudioFormatManager fmtMgr;
            fmtMgr.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (
                fmtMgr.createReaderFor (sessionWav));
            if (reader != nullptr)
            {
                const int stemSamples  = static_cast<int> (reader->lengthInSamples);
                const int tailSamples  = static_cast<int> (kRenderSec * kSampleRate);
                const int totalSamples = stemSamples + tailSamples;
                juce::AudioBuffer<float> sessionInput (2, totalSamples);
                sessionInput.clear();
                reader->read (&sessionInput, 0, stemSamples, 0, true, true);
                if (reader->numChannels == 1)
                    sessionInput.copyFrom (1, 0, sessionInput, 0, 0, stemSamples);

                auto output = renderThroughPlugin (*plugin, sessionInput);
                auto outFile = outDir.getChildFile (slug + "_session.wav");
                if (writeWav (outFile, output, kSampleRate))
                    std::cout << "Wrote " << outFile.getFullPathName() << std::endl;
            }
        }
        else
        {
            std::cerr << "  ! session.wav stimulus not found (test_signals/session.wav)"
                      << std::endl;
        }
    }

    plugin->reset();
    runPreroll (prerunSeconds);

    // ---- Render 2b: arbitrary stem (--input-wav) ----
    // Loads any user-supplied WAV, pads with 6 seconds of silence so the
    // reverb tail fully decays, processes through the active engine,
    // writes to {outDir}/{slug}_stem.wav. Independent of the built-in
    // snare/sine/noiseburst test-signal slots.
    if (inputWavPath.isNotEmpty())
    {
        juce::File stemFile (inputWavPath);
        if (! stemFile.existsAsFile())
        {
            std::cerr << "  ! --input-wav file not found: " << inputWavPath << std::endl;
        }
        else
        {
            juce::AudioFormatManager fmtMgr;
            fmtMgr.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (
                fmtMgr.createReaderFor (stemFile));
            if (reader == nullptr)
            {
                std::cerr << "  ! could not read stem WAV: " << inputWavPath << std::endl;
            }
            else
            {
                const int stemSamples = static_cast<int> (reader->lengthInSamples);
                const int tailSamples = static_cast<int> (kRenderSec * kSampleRate);
                const int totalSamples = stemSamples + tailSamples;
                juce::AudioBuffer<float> stemInput (2, totalSamples);
                stemInput.clear();
                // JUCE reader pulls into the provided buffer's first N channels;
                // for mono sources, copy channel 0 → 1 so the engine sees stereo.
                reader->read (&stemInput, 0, stemSamples, 0, true, true);
                if (reader->numChannels == 1)
                    stemInput.copyFrom (1, 0, stemInput, 0, 0, stemSamples);

                auto output = renderThroughPlugin (*plugin, stemInput);
                auto outFile = outDir.getChildFile (slug + "_stem.wav");
                if (writeWav (outFile, output, kSampleRate))
                    std::cout << "Wrote " << outFile.getFullPathName() << std::endl;
            }
        }
    }

    plugin->reset();
    runPreroll (prerunSeconds);

    // ---- Render 3: 2-sec 1 kHz sine at -12 dBFS RMS ----
    // For per-preset trim calibration: measure output RMS in the steady-
    // state window (1.5-2.0 s) and adjust gain_trim until output also
    // measures -12 dBFS RMS. Insensitive to spectral shape AT 1 kHz only
    // — presets with very different spectral tilts will perceptibly differ
    // even after this calibration.
    {
        const int sineSamples = static_cast<int> (kSampleRate * 2.0);
        juce::AudioBuffer<float> input (2, sineSamples);
        fillSineTone (input, kSampleRate, 1000.0, -12.0);
        auto output = renderThroughPlugin (*plugin, input);
        auto outFile = outDir.getChildFile (slug + "_sine1k.wav");
        if (writeWav (outFile, output, kSampleRate))
            std::cout << "Wrote " << outFile.getFullPathName() << std::endl;
    }

    // ---- Render 5 (optional): Sustained pink noise (steady-state diagnostic) ----
    // Pumps `sustainedPinkSeconds` of continuous pink noise into the engine,
    // then `sustainedPinkSeconds` of silence to capture the post-input decay.
    // The steady-state portion (last ~30 % of input window) shows the true
    // per-band decay character of the engine under musical-content excitation
    // — what the user actually hears, not the burst-and-stop response that
    // noiseburst measures. Skipped unless --sustained-pink-seconds > 0.
    if (sustainedPinkSeconds > 0.0)
    {
        plugin->reset();
        runPreroll (prerunSeconds);
        const int total = static_cast<int> (kSampleRate * sustainedPinkSeconds * 2.0);
        juce::AudioBuffer<float> input (2, total);
        fillSustainedPink (input, kSampleRate, sustainedPinkSeconds);
        auto output = renderThroughPlugin (*plugin, input);
        auto outFile = outDir.getChildFile (slug + "_sustained.wav");
        if (writeWav (outFile, output, kSampleRate))
            std::cout << "Wrote " << outFile.getFullPathName() << std::endl;
    }

    plugin->releaseResources();
    return 0;
}

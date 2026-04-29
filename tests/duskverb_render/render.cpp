// DuskVerb render tool.
//
// Loads the built DuskVerb AU bundle via juce::AudioPluginFormatManager
// (the same path used by AudioUnit-hosting DAWs like Logic), applies a named
// factory preset's parameter values, and renders a series of test signals
// through it. Output WAVs go to tests/duskverb_render/output/.
//
// Why hosted (not Processor-link)? An engine-standalone or Processor-link
// test can drift from what the user hears in the DAW because the plugin
// wrapper layer (parameter scaling, channel layout, automation timing) is
// part of the audio result. Hosting via the actual AU bundle eliminates
// that whole class of false negatives.
//
// Usage:
//   duskverb_render <preset_name>
//   duskverb_render "Lush Dark Hall"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdlib>
#include <iostream>
#include <map>

namespace
{
    constexpr double kSampleRate   = 48000.0;
    constexpr int    kBlockSize    = 256;
    constexpr int    kRenderSec    = 6;
    constexpr int    kTotalSamples = static_cast<int> (kSampleRate * kRenderSec);

    // Default AU path: per-user Components folder under $HOME — matches
    // `cmake --build ... --target DuskVerb_AU`'s install destination on
    // macOS and works for any developer (no hard-coded username). The
    // `--au <path>` CLI flag overrides this when the AU lives elsewhere
    // (e.g. /Library/Audio/Plug-Ins/Components for a system install).
    const juce::String kAUPath =
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Plug-Ins/Components/DuskVerb.component")
            .getFullPathName();

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

    // Keys are the human-readable parameter NAMES (matching what the AU host
    // surfaces). The original string IDs from the plugin source are hashed to
    // ints by the AU wrapper, so we match on display name instead.
    PresetParams getLushDarkHall()
    {
        return {
            "Lush Dark Hall",
            {
                // Migrated from 6-AP (algo 1) → FDN (algo 3) on 2026-04-26.
                { "Algorithm",       3.0f / 6.0f},     // FDN — Realistic Space (choice 3 of 0..6)
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
                { "Low Crossover",   550.0f },
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
        return {
            "Cathedral",
            {
                // Migrated from 6-AP (algo 1) → FDN (algo 3) on 2026-04-26.
                { "Algorithm",       3.0f / 6.0f},     // FDN — Realistic Space (choice 3 of 0..6)
                { "Dry/Wet",         1.0f },
                { "Bus Mode",        1.0f },
                { "Pre-Delay",       30.0f },
                { "Pre-Delay Sync",  0.0f },
                { "Decay Time",      6.50f },
                { "Size",            0.95f },
                { "Mod Depth",       0.15f },     // 0.20 → 0.15 (FDN smooth)
                { "Mod Rate",        0.45f },
                { "Treble Multiply", 0.40f },     // 0.55 → 0.40 (darker)
                { "Bass Multiply",   1.30f },
                { "Mid Multiply",    1.20f },
                { "Low Crossover",   750.0f },
                { "High Crossover",  2400.0f },   // 3000 → 2400 (cathedral darkness)
                { "Saturation",      0.15f },
                { "Diffusion",       0.78f },
                { "Early Ref Level", 0.30f },
                { "Early Ref Size",  0.85f },
                { "Lo Cut",          60.0f },
                { "Hi Cut",          8500.0f },   // 10000 → 8500 (224-era cap)
                { "Width",           1.50f },     // 1.35 → 1.50 (max stereo)
                { "Freeze",          0.0f },
                { "Gain Trim",      -3.5f },      // 1 kHz sine calibration round 1
                { "Mono Below",      20.0f },
            }
        };
    }

    PresetParams getBladeRunner224()
    {
        return {
            "Blade Runner 224",
            {
                // Migrated to algorithm 0 (Dattorro figure-8) on 2026-04-26
                // — historically more accurate to the Lexicon 224's actual
                // 2-AP cross-coupled topology than SixAPTank's 6-AP cascade.
                // Validated against Arturia Rev LX-24 BladeRunner preset on
                // 2026-04-27 — second pass after discovering the prior
                // setStateInformation path silently dropped the .aupreset's
                // values, so our "Arturia target" was actually defaults.
                // True target (per AudioUnitSetParameter overrides):
                //   RT60 9.73 s, initial 12 kHz/-15.7 dB, LR mean ~0,
                //   LR stddev 0.028, mid-tail centroid 1700-2700 Hz, hot tail
                //   (-76 dB at 5 s, vs our -99 dB).
                //
                // From the previous (wrong-target) round we keep:
                //   • Width 1.00          — kills the static phasiness
                //   • Mod Depth 0.03      — target stddev is even tighter
                //   • Mod Rate 0.45       — slow drift
                //   • Hi Cut 10 kHz       — preserve bright initial transient
                //   • Early Ref Level 1.0 — max brightness on ER burst
                //   • High Crossover 5kHz — push damping band higher
                //   • Bus Mode 0          — insert mode so dry mixes in
                //   • Dry/Wet 0.412       — matches Arturia's mix
                //
                // Reverting the changes that chased the wrong (defaults)
                // target:
                //   • Decay 4.0 → 9.0 s   — target RT60 9.7 s; bass mult 1.30
                //                              extends the loop ~13 s but the
                //                              tank's loop length and damping
                //                              produce ~10 s measured
                //   • Diffusion 0.50 → 0.85 — smooth sustained tail (like
                //                              Arturia's Lexicon Hall) vs the
                //                              spikier 0.50 we'd dialled in
                { "Algorithm",       0.0f / 6.0f },     // 0 = Vintage Plate (Dattorro)
                { "Dry/Wet",         0.412f },
                { "Bus Mode",        0.0f },
                { "Pre-Delay",       24.0f },     // Lexicon 224 hardware spec for Vangelis cues
                { "Pre-Delay Sync",  0.0f },
                { "Decay Time",      9.00f },
                { "Size",            1.00f },     // max size for fullest low-end body
                { "Mod Depth",       0.03f },
                { "Mod Rate",        0.45f },
                { "Treble Multiply", 0.50f },     // 0.35 was too aggressive — treble decayed
                                                  // 4 dB darker than Arturia by 500 ms.
                                                  // 0.50 keeps the dark mid-tail without
                                                  // killing late-tail HF residue.
                { "Bass Multiply",   2.00f },     // 1.70 → 2.00, more bass dominance
                { "Mid Multiply",    1.00f },
                { "Low Crossover",   800.0f },
                { "High Crossover",  5000.0f },
                { "Saturation",      0.00f },     // Inactive at this preset's signal levels —
                                                  // our soft-clip is on the cross-feed bus with
                                                  // threshold 1.0 - sat*0.6; the bus signal here
                                                  // never reaches the knee even at sat=0.35.
                                                  // Setting to 0 to be honest. To match Arturia's
                                                  // Drive=0.38 audibly we need input-side
                                                  // saturation — a separate DSP task.
                { "Diffusion",       0.85f },
                { "Early Ref Level", 1.00f },
                { "Early Ref Size",  1.00f },
                { "Lo Cut",          60.0f },
                { "Hi Cut",          6500.0f },   // middle ground between 10000 (too bright)
                                                  // and 5000 (too dark) — preserves the 5-6.5 kHz
                                                  // air that Arturia keeps in its mid-tail
                { "Width",           1.00f },     // pure pass-through — kills phasiness
                { "Freeze",          0.0f },
                { "Gain Trim",       7.0f },      // wet-only trim fix recalibration
                { "Mono Below",      20.0f },
            }
        };
    }

    // Compact preset builder. Field order matches FactoryPresets.h exactly so
    // values can be transcribed 1:1 from the source.  Algorithm choice param
    // values are the choice INDEX (0..6); we normalise to N/(N-1) below.
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
                // Divisor must equal (numAlgorithms - 1). With 7 algorithms
                // (Dattorro / 6-AP / QuadTank / FDN / Spring / NonLinear /
                // Shimmer) → divisor = 6. Mismatching the divisor silently
                // misroutes the algorithm index (e.g. /5 with 7 algos sends
                // algoIdx 4 → 4/5=0.8 → Shimmer (idx 6 in 0..6) instead of
                // Spring).
                { "Algorithm",       static_cast<float> (algoIdx) / 6.0f },
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
        if (name == "Vintage Vocal Plate")
            return makePreset (name.toRawUTF8(), 0, 1.0f, true, 18.0f, 3.20f, 0.50f, 0.05f, 0.40f, 0.65f, 0.75f, 800.0f, 0.85f, 0.00f, 0.30f, 90.0f, 14000.0f, 1.10f, 12.5f, 20.0f, 1.10f, 5000.0f, 0.20f);
        if (name == "Modulated Plate")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 8.0f, 2.40f, 0.50f, 0.40f, 1.40f, 0.85f, 1.00f, 1300.0f, 0.80f, 0.00f, 0.45f, 70.0f, 14000.0f, 1.20f, 0.5f, 20.0f, 1.10f, 4500.0f, 0.25f);
        if (name == "Fat Pop Plate")
            return makePreset (name.toRawUTF8(), 0, 1.0f, true, 18.0f, 2.10f, 0.55f, 0.35f, 0.85f, 0.55f, 1.10f, 480.0f, 0.85f, 0.00f, 0.40f, 50.0f, 14000.0f, 1.30f, 13.0f, 20.0f, 1.20f, 4500.0f, 0.30f);
        // Other halls
        if (name == "Smooth Concert Hall")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 28.0f, 2.60f, 0.65f, 0.05f, 0.60f, 0.75f, 1.20f, 900.0f, 0.85f, 0.45f, 0.65f, 60.0f, 13000.0f, 1.25f, -0.5f, 20.0f, 1.00f, 4500.0f, 0.10f);
        if (name == "Vocal Hall")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 22.0f, 3.50f, 0.55f, 0.20f, 0.70f, 0.70f, 1.15f, 1000.0f, 0.78f, 0.45f, 0.55f, 100.0f, 9000.0f, 1.15f, -1.5f, 20.0f, 1.10f, 4000.0f, 0.10f);
        // Chambers
        if (name == "Wood Chamber")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 18.0f, 2.30f, 0.40f, 0.05f, 0.60f, 0.65f, 1.20f, 850.0f, 0.80f, 0.55f, 0.45f, 150.0f, 11500.0f, 1.15f, 0.5f, 20.0f, 1.10f, 4000.0f, 0.20f);
        if (name == "Realistic Chamber")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 14.0f, 1.40f, 0.50f, 0.10f, 0.80f, 0.85f, 1.10f, 1100.0f, 0.85f, 0.65f, 0.50f, 60.0f, 14000.0f, 1.10f, 2.0f, 20.0f, 1.00f, 5000.0f, 0.05f);
        // Rooms
        if (name == "Tight Drum Room")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 4.0f, 0.50f, 0.20f, 0.10f, 0.60f, 0.95f, 0.95f, 1500.0f, 0.65f, 0.65f, 0.30f, 100.0f, 14000.0f, 1.05f, 4.0f, 20.0f, 1.00f, 4500.0f, 0.10f);
        if (name == "Studio Room")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 8.0f, 0.60f, 0.30f, 0.00f, 1.00f, 0.85f, 1.05f, 1300.0f, 0.85f, 0.60f, 0.40f, 80.0f, 9000.0f, 1.05f, 4.5f, 20.0f, 1.00f, 5000.0f, 0.05f);
        if (name == "80s Non-Lin Drum")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 0.0f, 0.30f, 0.15f, 0.20f, 1.00f, 0.95f, 0.85f, 1500.0f, 1.00f, 0.85f, 0.20f, 120.0f, 8000.0f, 1.20f, 4.0f, 20.0f, 0.80f, 3500.0f, 0.40f);
        // Ambient (bus_mode=true in source, mono_below set)
        if (name == "Ambient Swell")
            return makePreset (name.toRawUTF8(), 1, 1.0f, true, 60.0f, 8.00f, 0.92f, 0.28f, 0.40f, 0.60f, 1.50f, 600.0f, 0.80f, 0.10f, 0.75f, 150.0f, 5500.0f, 1.45f, 4.0f, 80.0f, 1.20f, 3500.0f, 0.15f);
        if (name == "Infinite Blackhole")
            return makePreset (name.toRawUTF8(), 1, 1.0f, true, 85.0f, 18.00f, 1.00f, 0.35f, 0.30f, 0.55f, 1.60f, 550.0f, 0.90f, 0.05f, 0.80f, 100.0f, 7500.0f, 1.50f, -1.0f, 100.0f, 1.30f, 3000.0f, 0.25f);
        // New presets (2026-04-26):
        if (name == "Snare Plate XL")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 12.0f, 4.50f, 0.65f, 0.15f, 0.50f, 0.85f, 0.65f, 600.0f, 0.75f, 0.30f, 0.55f, 180.0f, 14000.0f, 1.30f, 1.0f, 20.0f, 1.05f, 5000.0f, 0.20f);
        // Spring engine (algo 4) — Phase A v3.0:
        if (name == "Surf '63 Spring")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true, 0.0f, 1.60f, 0.40f, 0.20f, 1.50f, 1.00f, 0.85f, 1000.0f, 0.45f, 0.10f, 0.30f, 80.0f, 4000.0f, 1.10f, 2.5f, 20.0f, 1.00f, 4000.0f, 0.10f);
        if (name == "Tank Drip")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true, 0.0f, 2.20f, 0.65f, 0.30f, 0.80f, 0.70f, 1.10f, 1000.0f, 0.85f, 0.10f, 0.30f, 100.0f, 3000.0f, 1.20f, 2.5f, 20.0f, 1.00f, 4000.0f, 0.20f);
        // Non-Linear engine (algo 5) — Phase B v3.0:
        if (name == "In The Air Tonight")
            return makePreset (name.toRawUTF8(), 5, 0.216f, false, 0.0f, 2.608f, 0.80f, 0.092f, 0.794f, 0.75f, 1.10f, 500.0f, 0.50f, 0.00f, 0.30f, 60.0f, 10000.0f, 1.30f, 0.0f, 20.0f, 0.75f, 4000.0f, 0.10f);
        // Shimmer engine (algo 6) — v8 Eno/Lanois topology (mirrors FactoryPresets.h):
        // mod_depth = PITCH (0..1 → 0..24 semis), mod_rate Hz → FEEDBACK gain.
        if (name == "Deep Blue Day")
            return makePreset (name.toRawUTF8(), 6, 0.38f, false, 25.0f,  10.30f, 1.00f, 0.50f, 2.395f, 1.00f, 1.10f,  800.0f, 0.85f, 0.20f, 0.50f, 60.0f, 7000.0f, 1.30f,  0.0f, 20.0f, 1.00f, 4000.0f, 0.05f);
        if (name == "Cascading Heaven")
            return makePreset (name.toRawUTF8(), 6, 0.361f, false, 60.0f,  6.00f, 0.85f, 1.00f, 2.705f, 0.95f, 1.10f,  800.0f, 0.85f, 0.20f, 0.50f, 60.0f, 6000.0f, 1.40f, -3.0f, 60.0f, 1.00f, 4000.0f, 0.10f);
        if (name == "Black Hole")
        {
            auto p = makePreset (name.toRawUTF8(), 1, 0.50f, false,  0.0f, 14.00f, 0.95f, 0.35f, 0.60f, 1.00f, 1.10f,  700.0f, 0.85f, 0.05f, 0.70f, 60.0f, 18000.0f, 1.40f, -2.0f, 60.0f, 1.10f, 8000.0f, 0.08f);
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
        if (name == "Bright Studio Hall")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 18.0f, 1.80f, 0.55f, 0.10f, 0.55f, 0.85f, 1.05f, 400.0f, 0.65f, 0.40f, 0.50f, 120.0f, 14000.0f, 1.30f, 1.5f, 20.0f, 1.00f, 5500.0f, 0.05f);
        if (name == "Vocal Booth")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 2.0f, 0.40f, 0.20f, 0.05f, 0.40f, 0.80f, 0.95f, 800.0f, 0.65f, 0.55f, 0.20f, 120.0f, 12000.0f, 1.00f, 4.0f, 20.0f, 1.00f, 4500.0f, 0.05f);
        if (name == "Mobius Pad")
            return makePreset (name.toRawUTF8(), 1, 1.0f, true, 45.0f, 5.50f, 0.90f, 0.40f, 0.35f, 0.45f, 1.50f, 500.0f, 0.85f, 0.20f, 0.85f, 80.0f, 9000.0f, 1.50f, 4.5f, 80.0f, 1.20f, 3200.0f, 0.10f);

        // PCM 90 — Plates (Dattorro, algo 0):
        if (name == "Rich Plate")
            return makePreset (name.toRawUTF8(), 0, 1.0f, true, 0.0f, 1.60f, 0.55f, 0.10f, 0.45f, 0.95f, 1.00f, 600.0f, 0.85f, 0.00f, 0.30f, 80.0f, 14000.0f, 1.10f, 14.5f, 20.0f, 1.00f, 4000.0f, 0.10f);
        if (name == "Gold Plate")
            return makePreset (name.toRawUTF8(), 0, 1.0f, true, 0.0f, 1.96f, 0.357f, 0.12f, 0.35f, 1.00f, 0.55f, 600.0f, 0.80f, 0.00f, 0.00f, 200.0f, 20000.0f, 1.15f, 16.0f, 20.0f, 0.80f, 3000.0f, 0.00f);
        if (name == "Vocal Plate")
            return makePreset (name.toRawUTF8(), 0, 1.0f, true, 4.0f, 0.95f, 0.45f, 0.05f, 0.50f, 0.85f, 1.00f, 700.0f, 0.55f, 0.00f, 0.30f, 100.0f, 11000.0f, 1.10f, 16.5f, 20.0f, 1.00f, 4500.0f, 0.10f);
        // PCM 90 — Halls (SixAPTank / FDN):
        if (name == "Blade Runner Concert")
            return makePreset (name.toRawUTF8(), 1, 1.0f, true, 5.0f, 3.00f, 0.85f, 0.10f, 0.40f, 0.52f, 1.25f, 700.0f, 0.85f, 0.45f, 0.55f, 60.0f, 8000.0f, 1.20f, 8.5f, 20.0f, 1.10f, 4000.0f, 0.10f);
        if (name == "Deep Blue")
            return makePreset (name.toRawUTF8(), 1, 1.0f, true, 10.0f, 3.00f, 0.85f, 0.15f, 0.40f, 0.65f, 1.10f, 600.0f, 0.85f, 0.40f, 0.65f, 60.0f, 8500.0f, 1.30f, 9.0f, 20.0f, 1.10f, 4000.0f, 0.10f);
        if (name == "Bright Hall")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 0.0f, 1.80f, 0.65f, 0.12f, 0.50f, 1.20f, 1.00f, 1000.0f, 0.75f, 0.50f, 0.50f, 80.0f, 18000.0f, 1.20f, 1.5f, 20.0f, 1.00f, 6000.0f, 0.05f);
        if (name == "Utility Hall")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 1.0f, 1.10f, 0.55f, 0.08f, 0.45f, 1.10f, 0.75f, 1000.0f, 0.75f, 0.50f, 0.50f, 100.0f, 8000.0f, 1.10f, 2.5f, 20.0f, 1.00f, 4500.0f, 0.05f);
        // PCM 90 — Rooms (QuadTank / NonLinear):
        if (name == "Ambience")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 1.0f, 0.60f, 0.40f, 0.05f, 0.45f, 1.05f, 0.90f, 900.0f, 0.65f, 0.70f, 0.50f, 100.0f, 14000.0f, 1.20f, 3.5f, 20.0f, 1.05f, 5000.0f, 0.10f);
        if (name == "PCM Drum Room")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 0.0f, 0.60f, 0.35f, 0.10f, 0.50f, 0.90f, 1.10f, 900.0f, 0.70f, 0.75f, 0.40f, 100.0f, 12000.0f, 1.15f, 4.0f, 20.0f, 1.05f, 5000.0f, 0.10f);
        if (name == "1981 Gated Snare")
            return makePreset (name.toRawUTF8(), 5, 1.0f, true, 0.0f, 1.50f, 0.70f, 0.00f, 1.117f, 0.80f, 1.00f, 500.0f, 0.30f, 0.00f, 0.00f, 60.0f, 14000.0f, 1.40f, 0.0f, 100.0f, 0.75f, 4000.0f, 0.10f);
        if (name == "Reverse Taps")
            return makePreset (name.toRawUTF8(), 5, 1.0f, true, 30.0f, 3.00f, 0.85f, 0.49f, 7.52f, 0.70f, 1.00f, 500.0f, 1.00f, 0.00f, 0.30f, 80.0f, 8000.0f, 1.30f, 0.0f, 20.0f, 0.75f, 4000.0f, 0.10f);

        return getLushDarkHall();
    }

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
    {
        file.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (! dynamic_cast<juce::FileOutputStream&> (*stream).openedOk())
        {
            std::cerr << "Failed to open " << file.getFullPathName() << std::endl;
            return false;
        }

        const auto options = juce::AudioFormatWriterOptions{}
            .withSampleRate (sr)
            .withNumChannels (buf.getNumChannels())
            .withBitsPerSample (32)
            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        auto writer = fmt.createWriterFor (stream, options);
        if (writer == nullptr)
        {
            std::cerr << "Failed to create WAV writer for " << file.getFullPathName() << std::endl;
            return false;
        }

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

    // Apply a Valhalla-style .vpreset XML directly: each XML attribute is a
    // parameter name with an already-normalised 0..1 value. Skip the XML
    // metadata attributes (version, encoding, pluginVersion, presetName).
    void applyVpresetXml (juce::AudioPluginInstance& plugin, const juce::File& xmlFile)
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
            p->setValueNotifyingHost (normalised);
            std::cout << "  " << name << " set=" << rawText << "  norm=" << normalised
                      << "  read_back='" << p->getText (p->getValue(), 50) << "'" << std::endl;
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

    void applyPreset (juce::AudioPluginInstance& plugin, const PresetParams& preset)
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
            p->setValueNotifyingHost (normalised);
            const float readBack = p->getValue();
            const juce::String readBackText = p->getText (readBack, 50);
            std::cout << "  " << name << " set=" << raw
                      << "  norm=" << normalised
                      << "  read_back='" << readBackText << "'"
                      << std::endl;
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

    // CLI: render.cpp [<preset_name> | --au <au_path> (--vpreset <xml_path> | --aupreset <plist_path>) [--slug <name>]]
    //   No args     → DuskVerb / Lush Dark Hall (default)
    //   <preset>    → DuskVerb / one of getPresetByName()'s built-in presets
    //   --au + --vpreset  → load arbitrary AU + Valhalla-style XML preset
    //   --au + --aupreset → load arbitrary AU + Apple .aupreset (binary plist
    //                       with embedded JUCE state; works for any JUCE-built
    //                       AU, e.g. Valhalla Shimmer's factory presets)
    juce::String presetName  = "Lush Dark Hall";
    bool         presetExplicit = false;          // user named a preset on the CLI
    juce::String auPathArg   = kAUPath;
    juce::String vpresetPath;
    juce::String aupresetPath;
    juce::String slugArg;
    juce::String outDirArg;
    bool listParamsOnly = false;
    // Dry-passthrough test: override bus_mode=0 + mix=0 on the loaded preset
    // so the engine's wet path is silent and only the dry passes through.
    // Used to verify that gain_trim does not bleed into the dry signal.
    bool dryPassthroughTest = false;
    bool forceGateOff       = false;   // --gate-off : force gate_enabled = 0 after preset apply
    // Per-parameter overrides via --param NAME=VALUE. Stored in declaration
    // order so multiple --param flags compose the way the user wrote them
    // (last write wins). Applied AFTER the preset so they override anything
    // the preset set. Works against any AU — DuskVerb, Valhalla, Arturia, etc.
    std::vector<std::pair<juce::String, juce::String>> paramOverrides;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a = argv[i];
        if (a == "--au" && i + 1 < argc)            auPathArg    = argv[++i];
        else if (a == "--vpreset" && i + 1 < argc)  vpresetPath  = argv[++i];
        else if (a == "--aupreset" && i + 1 < argc) aupresetPath = argv[++i];
        else if (a == "--slug" && i + 1 < argc)     slugArg      = argv[++i];
        else if (a == "--output-dir" && i + 1 < argc) outDirArg  = argv[++i];
        else if (a == "--list-params")              listParamsOnly = true;
        else if (a == "--dry-passthrough-test")     dryPassthroughTest = true;
        else if (a == "--gate-off")                 forceGateOff = true;
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
        else if (! a.startsWith ("--"))
        {
            presetName     = a;
            presetExplicit = true;
        }
    }

    juce::File auFile (auPathArg);
    if (! auFile.exists())
    {
        std::cerr << "AU bundle not found at " << auPathArg << std::endl;
        return 1;
    }

    juce::AudioPluginFormatManager fm;
    // We're linked against juce_audio_processors_headless (no GUI), which
    // disables addDefaultFormats() in favour of the headless-friendly variant.
    juce::addHeadlessDefaultFormatsToManager (fm);

    // Find the AU format among the registered formats (macOS only).
    juce::AudioPluginFormat* auFormat = nullptr;
    for (auto* f : fm.getFormats())
    {
        if (f->getName() == "AudioUnit")
        {
            auFormat = f;
            break;
        }
    }
    if (auFormat == nullptr)
    {
        std::cerr << "AudioUnit format not registered (build is not on macOS?)" << std::endl;
        return 1;
    }

    juce::OwnedArray<juce::PluginDescription> typesFound;
    auFormat->findAllTypesForFile (typesFound, auPathArg);
    if (typesFound.isEmpty())
    {
        std::cerr << "Plugin scan returned no AU types from " << auPathArg << std::endl;
        return 1;
    }
    std::cout << "Found AU type: " << typesFound[0]->name << " (manufacturer: "
              << typesFound[0]->manufacturerName << ")" << std::endl;

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

    auto applyAnyPreset = [&]()
    {
        if (aupresetPath.isNotEmpty())
            applyAuPreset (*plugin, juce::File (aupresetPath));
        else if (vpresetPath.isNotEmpty())
            applyVpresetXml (*plugin, juce::File (vpresetPath));
        else if (haveDuskVerbPreset)
            applyPreset (*plugin, duskVerbPreset);
        // else: arbitrary AU + no preset specifier → leave the plugin in
        // its post-instantiation default state. --param overrides still apply.
    };

    // --param NAME=VALUE overrides. Applied AFTER the preset so they win
    // any conflict. Uses the same value-parsing heuristics as applyPreset:
    // try getValueForText first (handles "On"/"Off", "1.5 kHz", percentages,
    // etc. via the plugin's own formatter); fall back to "treat as already-
    // normalised" for small 0..1 floats when getValueForText returns 0.
    auto applyParamOverrides = [&]()
    {
        if (paramOverrides.empty())
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
            p->setValueNotifyingHost (normalised);
            std::cout << "  --param " << name << " set='" << valueStr
                      << "' norm=" << normalised
                      << " read_back='" << p->getText (p->getValue(), 50) << "'" << std::endl;
        }
    };

    // First pass: apply the preset so any param changes (notably Algorithm)
    // are visible to the plugin BEFORE we re-prepare. This lets the plugin
    // size its per-algorithm DSP buffers correctly during the second
    // prepareToPlay (Arturia Rev LX-24 segfaults if Algorithm changes after
    // prepare).
    applyAnyPreset();
    applyParamOverrides();

    plugin->releaseResources();
    plugin->prepareToPlay (kSampleRate, kBlockSize);

    // Second pass: prepareToPlay can reset some plugins' parameter state
    // back to defaults (Arturia Rev LX-24 does this — verified by dumping
    // post-load param values, which were byte-identical to no-preset state
    // until this re-apply was added). Apply the preset AGAIN so the actually-
    // configured values are what the renderer hears.
    applyAnyPreset();
    applyParamOverrides();

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
    // primes, and any startup transients flush out.
    {
        juce::AudioBuffer<float> preRoll (2, static_cast<int> (kSampleRate * 0.5));
        preRoll.clear();
        renderThroughPlugin (*plugin, preRoll);
    }

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

    const juce::String slug = slugArg.isNotEmpty()
                            ? slugArg
                            : presetName.replace (" ", "");

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
    // impulse tail.
    plugin->reset();

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

    plugin->releaseResources();
    return 0;
}

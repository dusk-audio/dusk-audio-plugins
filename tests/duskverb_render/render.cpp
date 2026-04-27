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

#include <iostream>
#include <map>

namespace
{
    constexpr double kSampleRate   = 48000.0;
    constexpr int    kBlockSize    = 256;
    constexpr int    kRenderSec    = 6;
    constexpr int    kTotalSamples = static_cast<int> (kSampleRate * kRenderSec);

    // Hard-coded path to the built AU. Matches `cmake --build ... --target DuskVerb_AU`
    // install destination on macOS.
    const juce::String kAUPath = "/Users/marckorte/Library/Audio/Plug-Ins/Components/DuskVerb.component";

    // Factory preset definitions — mirror FactoryPresets.h (we don't link
    // against the plugin's source, so we duplicate just the values we need).
    struct PresetParams
    {
        juce::String name;
        std::map<juce::String, float> values;  // parameter ID -> raw value
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
                { "Algorithm",       3.0f / 6.0f},     // FDN — Realistic Space (choice 3 of 0..3)
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
                { "Gain Trim",       0.0f },
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
                { "Algorithm",       3.0f / 6.0f},     // FDN — Realistic Space (choice 3 of 0..3)
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
                { "Gain Trim",       0.0f },
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
                // 2-AP cross-coupled topology than ModernSpace's 6-AP cascade.
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
                { "Gain Trim",      -5.0f },     // -5 dB shaves the tail to match Arturia's level
                { "Mono Below",      20.0f },
            }
        };
    }

    // Compact preset builder. Field order matches FactoryPresets.h exactly so
    // values can be transcribed 1:1 from the source.  Algorithm choice param
    // values are the choice INDEX (0..3); we normalise to N/(N-1) below.
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
                // Divisor must equal (numAlgorithms - 1). Update when adding engines.
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
            return makePreset (name.toRawUTF8(), 0, 1.0f, true, 18.0f, 3.20f, 0.50f, 0.05f, 0.40f, 0.65f, 0.75f,  800.0f, 0.85f, 0.00f, 0.30f,  90.0f, 14000.0f, 1.10f, 0.0f, 20.0f, 1.10f, 5000.0f, 0.20f);
        if (name == "Bright Drum Plate")
            return makePreset (name.toRawUTF8(), 0, 1.0f, true,  6.0f, 1.65f, 0.40f, 0.20f, 0.70f, 1.05f, 0.70f, 1500.0f, 0.90f, 0.00f, 0.25f, 150.0f, 16000.0f, 1.20f, 0.0f, 20.0f, 1.00f, 6000.0f, 0.15f);
        if (name == "Modulated Plate")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true,  8.0f, 2.40f, 0.50f, 0.40f, 1.40f, 0.85f, 1.00f, 1300.0f, 0.80f, 0.00f, 0.45f,  70.0f, 14000.0f, 1.20f, 0.0f, 20.0f, 1.10f, 4500.0f, 0.25f);
        if (name == "Fat Pop Plate")
            return makePreset (name.toRawUTF8(), 0, 1.0f, true, 18.0f, 2.10f, 0.55f, 0.35f, 0.85f, 0.55f, 1.10f,  480.0f, 0.85f, 0.00f, 0.40f,  50.0f, 14000.0f, 1.30f, 0.0f, 20.0f, 1.20f, 4500.0f, 0.30f);
        // Other halls
        if (name == "Smooth Concert Hall")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 28.0f, 2.60f, 0.65f, 0.05f, 0.60f, 0.75f, 1.20f,  900.0f, 0.85f, 0.45f, 0.65f,  60.0f, 13000.0f, 1.25f, 0.0f, 20.0f, 1.00f, 4500.0f, 0.10f);
        if (name == "Vocal Hall")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 22.0f, 3.50f, 0.55f, 0.20f, 0.70f, 0.70f, 1.15f, 1000.0f, 0.78f, 0.45f, 0.55f, 100.0f,  9000.0f, 1.15f, 0.0f, 20.0f, 1.10f, 4000.0f, 0.10f);
        // Chambers
        if (name == "Wood Chamber")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true, 18.0f, 2.30f, 0.40f, 0.05f, 0.60f, 0.65f, 1.20f,  850.0f, 0.80f, 0.55f, 0.45f, 150.0f, 11500.0f, 1.15f, 0.0f, 20.0f, 1.10f, 4000.0f, 0.20f);
        if (name == "Realistic Chamber")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 14.0f, 1.40f, 0.50f, 0.10f, 0.80f, 0.85f, 1.10f, 1100.0f, 0.85f, 0.65f, 0.50f,  60.0f, 14000.0f, 1.10f, 0.0f, 20.0f, 1.00f, 5000.0f, 0.05f);
        // Rooms
        if (name == "Tight Drum Room")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true,  4.0f, 0.50f, 0.20f, 0.10f, 0.60f, 0.95f, 0.95f, 1500.0f, 0.65f, 0.65f, 0.30f, 100.0f, 14000.0f, 1.05f, 0.0f, 20.0f, 1.00f, 4500.0f, 0.10f);
        if (name == "Studio Room")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true,  8.0f, 0.60f, 0.30f, 0.00f, 1.00f, 0.85f, 1.05f, 1300.0f, 0.85f, 0.60f, 0.40f,  80.0f,  9000.0f, 1.05f, 0.0f, 20.0f, 1.00f, 5000.0f, 0.05f);
        if (name == "80s Non-Lin Drum")
            return makePreset (name.toRawUTF8(), 2, 1.0f, true,  0.0f, 0.30f, 0.15f, 0.20f, 1.00f, 0.95f, 0.85f, 1500.0f, 1.00f, 0.85f, 0.20f, 120.0f,  8000.0f, 1.20f, 0.0f, 20.0f, 0.80f, 3500.0f, 0.40f);
        // Ambient (bus_mode=true in source, mono_below set)
        if (name == "Ambient Swell")
            return makePreset (name.toRawUTF8(), 1, 1.0f, true, 60.0f, 8.00f, 0.92f, 0.28f, 0.40f, 0.60f, 1.50f,  600.0f, 0.80f, 0.10f, 0.75f, 150.0f,  5500.0f, 1.45f, 0.0f, 80.0f, 1.20f, 3500.0f, 0.15f);
        if (name == "Infinite Blackhole")
            return makePreset (name.toRawUTF8(), 1, 1.0f, true,  85.0f, 18.00f, 1.00f, 0.35f, 0.30f, 0.55f, 1.60f,  550.0f, 0.90f, 0.05f, 0.80f, 100.0f,  7500.0f, 1.50f, 0.0f, 100.0f, 1.30f, 3000.0f, 0.25f);
        // New presets (2026-04-26):
        if (name == "Snare Plate XL")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 12.0f,  4.50f, 0.65f, 0.15f, 0.50f, 0.85f, 0.65f,  600.0f, 0.75f, 0.30f, 0.55f, 180.0f, 14000.0f, 1.30f, 0.0f,  20.0f, 1.05f, 5000.0f, 0.20f);
        // Spring engine (algo 4) — Phase A v3.0:
        if (name == "Surf '63 Spring")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true,  0.0f,  1.60f, 0.40f, 0.20f, 1.50f, 1.00f, 0.85f, 1000.0f, 0.45f, 0.10f, 0.30f,  80.0f,  4000.0f, 1.10f, 0.0f,  20.0f, 1.00f, 4000.0f, 0.10f);
        if (name == "Tank Drip")
            return makePreset (name.toRawUTF8(), 4, 1.0f, true,  0.0f,  2.20f, 0.65f, 0.30f, 0.80f, 0.70f, 1.10f, 1000.0f, 0.85f, 0.10f, 0.30f, 100.0f,  3000.0f, 1.20f, 0.0f,  20.0f, 1.00f, 4000.0f, 0.20f);
        // Non-Linear engine (algo 5) — Phase B v3.0:
        if (name == "Phil Collins Gated")
            return makePreset (name.toRawUTF8(), 5, 1.0f, true,  0.0f,  0.35f, 0.50f, 0.00f, 0.50f, 0.85f, 1.00f, 1000.0f, 0.15f, 0.00f, 0.30f,  60.0f,  8000.0f, 1.30f, 0.0f,  20.0f, 1.00f, 4000.0f, 0.05f);
        if (name == "Reverse Snare")
            return makePreset (name.toRawUTF8(), 5, 1.0f, true,  0.0f,  0.55f, 0.55f, 0.00f, 0.50f, 0.90f, 0.80f, 1000.0f, 0.50f, 0.00f, 0.30f,  60.0f, 12000.0f, 1.15f, 0.0f,  20.0f, 1.00f, 4000.0f, 0.05f);
        // Shimmer engine (algo 6) — Phase C v3.0:
        if (name == "Eno Choir")
            return makePreset (name.toRawUTF8(), 6, 1.0f, true, 25.0f,  4.00f, 0.80f, 0.50f, 5.00f, 0.55f, 0.95f, 1000.0f, 0.75f, 0.30f, 0.50f,  60.0f, 11000.0f, 1.40f, 0.0f,  20.0f, 1.00f, 4000.0f, 0.08f);
        if (name == "Cascading Heaven")
            return makePreset (name.toRawUTF8(), 6, 1.0f, true, 60.0f,  8.00f, 0.95f, 0.79f, 7.00f, 0.45f, 1.20f, 1000.0f, 0.85f, 0.20f, 0.50f,  60.0f,  9000.0f, 1.50f, 0.0f,  60.0f, 1.10f, 3500.0f, 0.15f);
        if (name == "Bright Studio Hall")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true, 18.0f,  1.80f, 0.55f, 0.10f, 0.55f, 0.85f, 1.05f,  400.0f, 0.65f, 0.40f, 0.50f, 120.0f, 14000.0f, 1.30f, 0.0f,  20.0f, 1.00f, 5500.0f, 0.05f);
        if (name == "Vocal Booth")
            return makePreset (name.toRawUTF8(), 3, 1.0f, true,  2.0f,  0.40f, 0.20f, 0.05f, 0.40f, 0.80f, 0.95f,  800.0f, 0.65f, 0.55f, 0.20f, 120.0f, 12000.0f, 1.00f, 0.0f,  20.0f, 1.00f, 4500.0f, 0.05f);
        if (name == "Mobius Pad")
            return makePreset (name.toRawUTF8(), 1, 1.0f, true, 45.0f,  5.50f, 0.90f, 0.40f, 0.35f, 0.45f, 1.50f,  500.0f, 0.85f, 0.20f, 0.85f,  80.0f,  9000.0f, 1.50f, 0.0f,  80.0f, 1.20f, 3200.0f, 0.10f);
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
    juce::String auPathArg   = kAUPath;
    juce::String vpresetPath;
    juce::String aupresetPath;
    juce::String slugArg;
    bool listParamsOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a = argv[i];
        if (a == "--au" && i + 1 < argc)            auPathArg    = argv[++i];
        else if (a == "--vpreset" && i + 1 < argc)  vpresetPath  = argv[++i];
        else if (a == "--aupreset" && i + 1 < argc) aupresetPath = argv[++i];
        else if (a == "--slug" && i + 1 < argc)     slugArg      = argv[++i];
        else if (a == "--list-params")              listParamsOnly = true;
        else if (! a.startsWith ("--"))             presetName   = a;
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

    auto applyAnyPreset = [&]()
    {
        if (aupresetPath.isNotEmpty())
            applyAuPreset (*plugin, juce::File (aupresetPath));
        else if (vpresetPath.isNotEmpty())
            applyVpresetXml (*plugin, juce::File (vpresetPath));
        else
            applyPreset (*plugin, getPresetByName (presetName));
    };

    // First pass: apply the preset so any param changes (notably Algorithm)
    // are visible to the plugin BEFORE we re-prepare. This lets the plugin
    // size its per-algorithm DSP buffers correctly during the second
    // prepareToPlay (Arturia Rev LX-24 segfaults if Algorithm changes after
    // prepare).
    applyAnyPreset();

    plugin->releaseResources();
    plugin->prepareToPlay (kSampleRate, kBlockSize);

    // Second pass: prepareToPlay can reset some plugins' parameter state
    // back to defaults (Arturia Rev LX-24 does this — verified by dumping
    // post-load param values, which were byte-identical to no-preset state
    // until this re-apply was added). Apply the preset AGAIN so the actually-
    // configured values are what the renderer hears.
    applyAnyPreset();

    // Pre-roll silence so all parameter smoothers settle, predelay buffer
    // primes, and any startup transients flush out.
    {
        juce::AudioBuffer<float> preRoll (2, static_cast<int> (kSampleRate * 0.5));
        preRoll.clear();
        renderThroughPlugin (*plugin, preRoll);
    }

    juce::File outDir = juce::File ("/Users/marckorte/projects/Luna/plugins/tests/duskverb_render/output");
    outDir.createDirectory();

    const juce::String slug = slugArg.isNotEmpty()
                            ? slugArg
                            : presetName.replace (" ", "");

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

    plugin->releaseResources();
    return 0;
}

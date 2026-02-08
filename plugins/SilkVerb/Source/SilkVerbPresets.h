#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace SilkVerbPresets
{

//==============================================================================
struct Preset
{
    juce::String name;
    juce::String category;
    int mode;           // 0=Plate..5=Ambience, 6=BrightHall, 7=ChorusSpace, 8=RandomSpace, 9=DirtyHall
    int color;          // 0=1970s, 1=1980s, 2=Now

    float size;         // 0.0 - 1.0
    float damping;      // 0.0 - 1.0
    float predelay;     // 0 - 100 ms
    float mix;          // 0.0 - 1.0
    float modRate;      // 0.1 - 5.0 Hz
    float modDepth;     // 0.0 - 1.0
    float width;        // 0.0 - 1.0
    float earlyDiff;    // 0.0 - 1.0
    float lateDiff;     // 0.0 - 1.0
    float bassMult;     // 0.5 - 2.0
    float bassFreq;     // 100 - 1000 Hz
    float lowCut;       // 20 - 500 Hz
    float highCut;      // 1000 - 20000 Hz
    bool freeze;        // typically false for presets
    float roomSize = 0.5f;      // 0.0 - 1.0 (default center)
    float earlyLateBal = 0.7f;  // 0.0 = all ER, 1.0 = all late
    float highDecay = 1.0f;     // 0.5 - 2.0
};

//==============================================================================
inline const juce::StringArray Categories = {
    "Vocals",
    "Drums",
    "Guitars",
    "Keys/Pads",
    "Mix",
    "Sound Design"
};

//==============================================================================
inline std::vector<Preset> getFactoryPresets()
{
    std::vector<Preset> presets;

    // ==================== VOCALS ====================

    // Classic plate vocal reverb — smooth, dense, sits behind the voice
    presets.push_back({
        "Vocal Plate",          // name
        "Vocals",               // category
        0,                      // Plate
        2,                      // Now
        0.35f,                  // size — medium decay
        0.55f,                  // damping — slightly dark
        15.0f,                  // predelay — separate from source
        0.25f,                  // mix — subtle blend
        1.2f,                   // modRate
        0.4f,                   // modDepth — gentle chorus
        0.85f,                  // width — wide stereo
        0.75f,                  // earlyDiff — smooth ER
        0.55f,                  // lateDiff — medium density
        1.0f,                   // bassMult — neutral
        400.0f,                 // bassFreq
        80.0f,                  // lowCut — remove mud
        10000.0f,               // highCut — tame air
        false
    });

    // Natural room for intimate vocals
    presets.push_back({
        "Vocal Room",
        "Vocals",
        1,                      // Room
        2,                      // Now
        0.25f,                  // size — small
        0.45f,                  // damping
        8.0f,                   // predelay — close
        0.20f,                  // mix
        0.8f,                   // modRate — subtle
        0.25f,                  // modDepth
        0.70f,                  // width
        0.70f,                  // earlyDiff
        0.45f,                  // lateDiff
        0.9f,                   // bassMult
        350.0f,                 // bassFreq
        100.0f,                 // lowCut
        12000.0f,               // highCut
        false
    });

    // Lush hall for ballad vocals
    presets.push_back({
        "Vocal Hall",
        "Vocals",
        2,                      // Hall
        2,                      // Now
        0.45f,                  // size — medium-long
        0.50f,                  // damping
        25.0f,                  // predelay — good separation
        0.30f,                  // mix
        1.0f,                   // modRate
        0.45f,                  // modDepth
        0.90f,                  // width
        0.75f,                  // earlyDiff
        0.60f,                  // lateDiff
        1.0f,                   // bassMult
        450.0f,                 // bassFreq
        60.0f,                  // lowCut
        9000.0f,                // highCut
        false
    });

    // Chamber for transparent vocal presence
    presets.push_back({
        "Vocal Chamber",
        "Vocals",
        3,                      // Chamber
        2,                      // Now
        0.30f,                  // size
        0.40f,                  // damping — bright
        12.0f,                  // predelay
        0.22f,                  // mix
        1.0f,                   // modRate
        0.30f,                  // modDepth
        0.80f,                  // width
        0.85f,                  // earlyDiff — high density
        0.60f,                  // lateDiff
        1.0f,                   // bassMult
        400.0f,                 // bassFreq
        80.0f,                  // lowCut
        11000.0f,               // highCut
        false
    });

    // ==================== DRUMS ====================

    // Tight room for snare/toms
    presets.push_back({
        "Tight Room",
        "Drums",
        1,                      // Room
        2,                      // Now
        0.15f,                  // size — very short
        0.60f,                  // damping — dark
        0.0f,                   // predelay — immediate
        0.20f,                  // mix
        0.5f,                   // modRate
        0.15f,                  // modDepth — minimal
        0.65f,                  // width
        0.80f,                  // earlyDiff — dense ER
        0.40f,                  // lateDiff
        0.8f,                   // bassMult — reduced bass
        300.0f,                 // bassFreq
        150.0f,                 // lowCut — aggressive LP cut
        8000.0f,                // highCut
        false
    });

    // Classic bright plate for snare
    presets.push_back({
        "Drum Plate",
        "Drums",
        0,                      // Plate
        2,                      // Now
        0.20f,                  // size — short
        0.35f,                  // damping — bright
        0.0f,                   // predelay
        0.18f,                  // mix
        1.5f,                   // modRate
        0.35f,                  // modDepth
        0.75f,                  // width
        0.65f,                  // earlyDiff
        0.50f,                  // lateDiff
        0.7f,                   // bassMult — reduced
        350.0f,                 // bassFreq
        120.0f,                 // lowCut
        14000.0f,               // highCut — let shimmer through
        false
    });

    // Very short ambience for drum bus
    presets.push_back({
        "Drum Ambience",
        "Drums",
        5,                      // Ambience
        2,                      // Now
        0.20f,                  // size
        0.50f,                  // damping
        0.0f,                   // predelay
        0.15f,                  // mix — subtle
        0.8f,                   // modRate
        0.20f,                  // modDepth
        0.60f,                  // width
        0.85f,                  // earlyDiff — dense
        0.35f,                  // lateDiff
        0.7f,                   // bassMult
        250.0f,                 // bassFreq
        100.0f,                 // lowCut
        10000.0f,               // highCut
        false
    });

    // ==================== GUITARS ====================

    // Plate with vintage character for spring emulation
    presets.push_back({
        "Guitar Spring",
        "Guitars",
        0,                      // Plate
        0,                      // 1970s
        0.30f,                  // size
        0.40f,                  // damping
        5.0f,                   // predelay
        0.30f,                  // mix — more prominent
        1.8f,                   // modRate — springy flutter
        0.55f,                  // modDepth — noticeable
        0.60f,                  // width — not too wide
        0.60f,                  // earlyDiff
        0.55f,                  // lateDiff
        1.1f,                   // bassMult — slight bass boost
        500.0f,                 // bassFreq
        60.0f,                  // lowCut
        8000.0f,                // highCut — dark spring character
        false
    });

    // Natural room for clean/acoustic guitar
    presets.push_back({
        "Guitar Room",
        "Guitars",
        1,                      // Room
        2,                      // Now
        0.25f,                  // size
        0.45f,                  // damping
        10.0f,                  // predelay
        0.25f,                  // mix
        0.7f,                   // modRate
        0.25f,                  // modDepth
        0.75f,                  // earlyDiff
        0.45f,                  // lateDiff
        0.70f,                  // width
        1.0f,                   // bassMult
        400.0f,                 // bassFreq
        80.0f,                  // lowCut
        12000.0f,               // highCut
        false
    });

    // Lush ambient wash for post-rock/shoegaze
    presets.push_back({
        "Ambient Wash",
        "Guitars",
        2,                      // Hall
        0,                      // 1970s
        0.70f,                  // size — long tail
        0.55f,                  // damping
        30.0f,                  // predelay — spacious
        0.45f,                  // mix — prominent
        0.9f,                   // modRate
        0.60f,                  // modDepth — lush
        1.0f,                   // width — full stereo
        0.70f,                  // earlyDiff
        0.65f,                  // lateDiff
        1.2f,                   // bassMult — rich lows
        500.0f,                 // bassFreq
        40.0f,                  // lowCut
        8000.0f,                // highCut — dark vintage wash
        false
    });

    // ==================== KEYS / PADS ====================

    // Warm chamber for piano
    presets.push_back({
        "Piano Chamber",
        "Keys/Pads",
        3,                      // Chamber
        2,                      // Now
        0.40f,                  // size
        0.45f,                  // damping
        18.0f,                  // predelay
        0.28f,                  // mix
        0.9f,                   // modRate
        0.35f,                  // modDepth
        0.85f,                  // width
        0.85f,                  // earlyDiff — high density
        0.65f,                  // lateDiff
        1.1f,                   // bassMult — warm bass
        450.0f,                 // bassFreq
        50.0f,                  // lowCut
        14000.0f,               // highCut — open
        false
    });

    // Deep hall for orchestral keys
    presets.push_back({
        "Deep Hall",
        "Keys/Pads",
        2,                      // Hall
        2,                      // Now
        0.60f,                  // size — long
        0.50f,                  // damping
        35.0f,                  // predelay
        0.35f,                  // mix
        0.8f,                   // modRate
        0.50f,                  // modDepth
        0.95f,                  // width
        0.70f,                  // earlyDiff
        0.60f,                  // lateDiff
        1.15f,                  // bassMult — deep
        500.0f,                 // bassFreq
        40.0f,                  // lowCut
        10000.0f,               // highCut
        false
    });

    // Massive cathedral for pad textures
    presets.push_back({
        "Pad Cathedral",
        "Keys/Pads",
        4,                      // Cathedral
        0,                      // 1970s
        0.65f,                  // size — large
        0.55f,                  // damping
        45.0f,                  // predelay — spacious
        0.40f,                  // mix
        0.6f,                   // modRate — slow
        0.55f,                  // modDepth
        1.0f,                   // width
        0.75f,                  // earlyDiff
        0.70f,                  // lateDiff
        1.3f,                   // bassMult — rich
        500.0f,                 // bassFreq
        30.0f,                  // lowCut
        8000.0f,                // highCut — dark cathedral
        false
    });

    // ==================== MIX ====================

    // Very subtle spatial enhancement
    presets.push_back({
        "Subtle Ambience",
        "Mix",
        5,                      // Ambience
        2,                      // Now
        0.15f,                  // size — very short
        0.40f,                  // damping
        5.0f,                   // predelay
        0.12f,                  // mix — barely there
        0.6f,                   // modRate
        0.15f,                  // modDepth
        0.80f,                  // width
        0.80f,                  // earlyDiff
        0.35f,                  // lateDiff
        1.0f,                   // bassMult
        350.0f,                 // bassFreq
        100.0f,                 // lowCut
        14000.0f,               // highCut
        false
    });

    // Room glue for mix bus
    presets.push_back({
        "Mix Glue Room",
        "Mix",
        1,                      // Room
        2,                      // Now
        0.20f,                  // size
        0.50f,                  // damping
        10.0f,                  // predelay
        0.10f,                  // mix — very subtle
        0.7f,                   // modRate
        0.20f,                  // modDepth
        0.70f,                  // width
        0.75f,                  // earlyDiff
        0.45f,                  // lateDiff
        0.9f,                   // bassMult
        400.0f,                 // bassFreq
        80.0f,                  // lowCut
        12000.0f,               // highCut
        false
    });

    // Big cinematic hall
    presets.push_back({
        "Cinematic Hall",
        "Mix",
        2,                      // Hall
        2,                      // Now
        0.75f,                  // size — very long
        0.45f,                  // damping
        40.0f,                  // predelay
        0.35f,                  // mix
        0.7f,                   // modRate
        0.55f,                  // modDepth
        1.0f,                   // width
        0.70f,                  // earlyDiff
        0.65f,                  // lateDiff
        1.2f,                   // bassMult — cinematic bass
        500.0f,                 // bassFreq
        30.0f,                  // lowCut
        9000.0f,                // highCut
        false
    });

    // Massive cathedral for epic mix moments
    presets.push_back({
        "Big Cathedral",
        "Mix",
        4,                      // Cathedral
        2,                      // Now
        0.70f,                  // size
        0.50f,                  // damping
        50.0f,                  // predelay
        0.30f,                  // mix
        0.5f,                   // modRate
        0.50f,                  // modDepth
        1.0f,                   // width
        0.80f,                  // earlyDiff
        0.75f,                  // lateDiff
        1.25f,                  // bassMult
        500.0f,                 // bassFreq
        40.0f,                  // lowCut
        8500.0f,                // highCut
        false
    });

    // ==================== SOUND DESIGN ====================

    // Frozen cathedral — infinite sustain, dark, massive
    presets.push_back({
        "Frozen Cathedral",
        "Sound Design",
        4,                      // Cathedral
        0,                      // 1970s
        0.85f,                  // size — near max
        0.65f,                  // damping — dark
        60.0f,                  // predelay
        0.50f,                  // mix — 50/50
        0.4f,                   // modRate — slow movement
        0.70f,                  // modDepth — wide
        1.0f,                   // width
        0.70f,                  // earlyDiff
        0.80f,                  // lateDiff
        1.4f,                   // bassMult — booming
        600.0f,                 // bassFreq
        20.0f,                  // lowCut — let everything through
        6000.0f,                // highCut — very dark
        true                    // FREEZE ON
    });

    // Dark space — oppressive, claustrophobic
    presets.push_back({
        "Dark Space",
        "Sound Design",
        2,                      // Hall
        0,                      // 1970s
        0.55f,                  // size
        0.75f,                  // damping — very dark
        20.0f,                  // predelay
        0.40f,                  // mix
        0.3f,                   // modRate — slow
        0.45f,                  // modDepth
        0.90f,                  // width
        0.60f,                  // earlyDiff
        0.70f,                  // lateDiff
        1.5f,                   // bassMult — heavy bass
        600.0f,                 // bassFreq
        20.0f,                  // lowCut
        4000.0f,                // highCut — extremely dark
        false
    });

    // Infinite hall — maximum decay, ethereal
    presets.push_back({
        "Infinite Hall",
        "Sound Design",
        2,                      // Hall
        2,                      // Now
        0.95f,                  // size — maximum
        0.40f,                  // damping — bright enough to shimmer
        50.0f,                  // predelay
        0.50f,                  // mix
        0.5f,                   // modRate
        0.65f,                  // modDepth — wide chorus
        1.0f,                   // width
        0.75f,                  // earlyDiff
        0.75f,                  // lateDiff
        1.3f,                   // bassMult
        500.0f,                 // bassFreq
        30.0f,                  // lowCut
        12000.0f,               // highCut — bright infinite
        false
    });

    // ==================== NEW MODES ====================

    // Bright shimmering hall for vocals and synths
    presets.push_back({
        "Shimmer Hall",
        "Vocals",
        6,                      // BrightHall
        2,                      // Now
        0.50f,                  // size
        0.30f,                  // damping — bright
        20.0f,                  // predelay
        0.30f,                  // mix
        0.8f,                   // modRate
        0.50f,                  // modDepth
        0.90f,                  // width
        0.80f,                  // earlyDiff
        0.60f,                  // lateDiff
        1.0f,                   // bassMult
        400.0f,                 // bassFreq
        80.0f,                  // lowCut
        16000.0f,               // highCut — open and bright
        false,
        0.5f,                   // roomSize
        0.75f,                  // earlyLateBal
        1.3f                    // highDecay — enhanced HF sustain
    });

    // Lush chorused space for pads and ambient
    presets.push_back({
        "Lush Chorus",
        "Keys/Pads",
        7,                      // ChorusSpace
        2,                      // Now
        0.55f,                  // size
        0.45f,                  // damping
        25.0f,                  // predelay
        0.40f,                  // mix
        1.5f,                   // modRate — fast for lush movement
        0.75f,                  // modDepth — prominent
        1.0f,                   // width
        0.70f,                  // earlyDiff
        0.65f,                  // lateDiff
        1.15f,                  // bassMult
        500.0f,                 // bassFreq
        40.0f,                  // lowCut
        10000.0f,               // highCut
        false,
        0.55f,                  // roomSize
        0.80f,                  // earlyLateBal — late-heavy
        0.9f                    // highDecay
    });

    // Vintage chorus reverb for guitars
    presets.push_back({
        "Chorus Verb",
        "Guitars",
        7,                      // ChorusSpace
        1,                      // 1980s
        0.45f,                  // size
        0.50f,                  // damping
        15.0f,                  // predelay
        0.35f,                  // mix
        1.2f,                   // modRate
        0.65f,                  // modDepth
        0.85f,                  // width
        0.65f,                  // earlyDiff
        0.55f,                  // lateDiff
        1.1f,                   // bassMult
        450.0f,                 // bassFreq
        60.0f,                  // lowCut
        9000.0f,                // highCut
        false,
        0.5f,                   // roomSize
        0.70f,                  // earlyLateBal
        0.85f                   // highDecay
    });

    // Ever-changing random reverb for sound design
    presets.push_back({
        "Shifting Space",
        "Sound Design",
        8,                      // RandomSpace
        2,                      // Now
        0.60f,                  // size
        0.50f,                  // damping
        20.0f,                  // predelay
        0.40f,                  // mix
        0.6f,                   // modRate — slow wandering
        0.70f,                  // modDepth
        1.0f,                   // width
        0.65f,                  // earlyDiff
        0.60f,                  // lateDiff
        1.1f,                   // bassMult
        500.0f,                 // bassFreq
        30.0f,                  // lowCut
        10000.0f,               // highCut
        false,
        0.6f,                   // roomSize — medium-large
        0.85f,                  // earlyLateBal — late-focused
        0.8f                    // highDecay
    });

    // Vintage random space
    presets.push_back({
        "Drifting Vintage",
        "Sound Design",
        8,                      // RandomSpace
        0,                      // 1970s
        0.50f,                  // size
        0.55f,                  // damping
        15.0f,                  // predelay
        0.35f,                  // mix
        0.4f,                   // modRate
        0.60f,                  // modDepth
        0.90f,                  // width
        0.60f,                  // earlyDiff
        0.55f,                  // lateDiff
        1.2f,                   // bassMult
        500.0f,                 // bassFreq
        40.0f,                  // lowCut
        7000.0f,                // highCut — dark vintage
        false,
        0.5f,                   // roomSize
        0.75f,                  // earlyLateBal
        0.7f                    // highDecay
    });

    // Lo-fi dirty hall for drums
    presets.push_back({
        "Trashy Room",
        "Drums",
        9,                      // DirtyHall
        0,                      // 1970s — extra grit
        0.25f,                  // size — short
        0.55f,                  // damping
        0.0f,                   // predelay
        0.25f,                  // mix
        0.5f,                   // modRate
        0.30f,                  // modDepth
        0.70f,                  // width
        0.70f,                  // earlyDiff
        0.50f,                  // lateDiff
        1.3f,                   // bassMult — boomy
        400.0f,                 // bassFreq
        80.0f,                  // lowCut
        5000.0f,                // highCut — very dark
        false,
        0.35f,                  // roomSize — small
        0.60f,                  // earlyLateBal
        0.5f                    // highDecay — strong HF loss
    });

    // Dark degraded hall for mix
    presets.push_back({
        "Dirty Hall",
        "Mix",
        9,                      // DirtyHall
        1,                      // 1980s
        0.50f,                  // size
        0.50f,                  // damping
        20.0f,                  // predelay
        0.30f,                  // mix
        0.5f,                   // modRate
        0.40f,                  // modDepth
        0.85f,                  // width
        0.65f,                  // earlyDiff
        0.55f,                  // lateDiff
        1.2f,                   // bassMult
        500.0f,                 // bassFreq
        60.0f,                  // lowCut
        6000.0f,                // highCut
        false,
        0.5f,                   // roomSize
        0.70f,                  // earlyLateBal
        0.6f                    // highDecay
    });

    // Small room, long tail — gated character
    presets.push_back({
        "Gated Verb",
        "Drums",
        1,                      // Room
        2,                      // Now
        0.40f,                  // size — medium-long decay
        0.60f,                  // damping
        0.0f,                   // predelay
        0.25f,                  // mix
        0.7f,                   // modRate
        0.25f,                  // modDepth
        0.65f,                  // width
        0.85f,                  // earlyDiff
        0.40f,                  // lateDiff
        0.8f,                   // bassMult — reduced
        300.0f,                 // bassFreq
        120.0f,                 // lowCut
        8000.0f,                // highCut
        false,
        0.15f,                  // roomSize — very small room
        0.50f,                  // earlyLateBal — equal mix
        0.7f                    // highDecay
    });

    return presets;
}

//==============================================================================
inline void applyPreset(juce::AudioProcessorValueTreeState& params, const Preset& preset)
{
    // Mode (10 choices: normalize by 9.0)
    if (auto* p = params.getParameter("mode"))
        p->setValueNotifyingHost(preset.mode / 9.0f);

    // Color (3 choices: normalize by 2.0)
    if (auto* p = params.getParameter("color"))
        p->setValueNotifyingHost(preset.color / 2.0f);

    // Continuous parameters — use convertTo0to1 for ranged params
    if (auto* p = params.getParameter("size"))
        p->setValueNotifyingHost(preset.size);

    if (auto* p = params.getParameter("damping"))
        p->setValueNotifyingHost(preset.damping);

    if (auto* p = params.getParameter("predelay"))
        p->setValueNotifyingHost(params.getParameterRange("predelay").convertTo0to1(preset.predelay));

    if (auto* p = params.getParameter("mix"))
        p->setValueNotifyingHost(preset.mix);

    if (auto* p = params.getParameter("modrate"))
        p->setValueNotifyingHost(params.getParameterRange("modrate").convertTo0to1(preset.modRate));

    if (auto* p = params.getParameter("moddepth"))
        p->setValueNotifyingHost(preset.modDepth);

    if (auto* p = params.getParameter("width"))
        p->setValueNotifyingHost(preset.width);

    if (auto* p = params.getParameter("earlydiff"))
        p->setValueNotifyingHost(preset.earlyDiff);

    if (auto* p = params.getParameter("latediff"))
        p->setValueNotifyingHost(preset.lateDiff);

    if (auto* p = params.getParameter("bassmult"))
        p->setValueNotifyingHost(params.getParameterRange("bassmult").convertTo0to1(preset.bassMult));

    if (auto* p = params.getParameter("bassfreq"))
        p->setValueNotifyingHost(params.getParameterRange("bassfreq").convertTo0to1(preset.bassFreq));

    if (auto* p = params.getParameter("lowcut"))
        p->setValueNotifyingHost(params.getParameterRange("lowcut").convertTo0to1(preset.lowCut));

    if (auto* p = params.getParameter("highcut"))
        p->setValueNotifyingHost(params.getParameterRange("highcut").convertTo0to1(preset.highCut));

    if (auto* p = params.getParameter("freeze"))
        p->setValueNotifyingHost(preset.freeze ? 1.0f : 0.0f);

    // New parameters (with defaults in Preset struct)
    if (auto* p = params.getParameter("roomsize"))
        p->setValueNotifyingHost(preset.roomSize);

    if (auto* p = params.getParameter("erlatebal"))
        p->setValueNotifyingHost(preset.earlyLateBal);

    if (auto* p = params.getParameter("highdecay"))
        p->setValueNotifyingHost(params.getParameterRange("highdecay").convertTo0to1(preset.highDecay));
}

} // namespace SilkVerbPresets

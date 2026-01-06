#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <map>

/**
 * Factory presets for Multi-Comp.
 * TUNING NOTES:
 * - FET Release times tightened (classic FET release is 50ms to 1.1s).
 * - Bus Attacks slowed down to preserve transients.
 * - Opto Peak Reduction increased for realistic gain reduction.
 */
namespace CompressorPresets
{

//==============================================================================
struct Preset
{
    juce::String name;
    juce::String category;
    int mode;                    // 0-6: Opto, FET, VCA, Bus, StudioFET, StudioVCA, Digital

    float threshold = -20.0f;    // dB
    float ratio = 4.0f;
    float attack = 10.0f;        // ms
    float release = 100.0f;      // ms
    float makeup = 0.0f;         // dB
    float mix = 100.0f;          // %
    float sidechainHP = 80.0f;   // Hz
    bool autoMakeup = false;
    int saturationMode = 0;      // 0=Vintage, 1=Modern, 2=Pristine

    // FET-specific
    int fetRatio = 0;            // 0=4:1, 1=8:1, 2=12:1, 3=20:1, 4=All

    // Bus-specific
    int busAttackIndex = 2;      // 0=0.1, 1=0.3, 2=1, 3=3, 4=10, 5=30 ms
    int busReleaseIndex = 2;     // 0=0.1, 1=0.3, 2=0.6, 3=1.2, 4=Auto

    // Opto-specific
    float peakReduction = 0.0f;  // % (0-100 scale)
    bool limitMode = false;
};

//==============================================================================
// Category definitions
inline const juce::StringArray Categories = {
    "Vocals",
    "Drums",
    "Bass",
    "Guitars",
    "Mix Bus",
    "Mastering",
    "Creative"
};

//==============================================================================
// Factory Presets
inline std::vector<Preset> getFactoryPresets()
{
    std::vector<Preset> presets;

    // ==================== VOCALS ====================

    // Classic optical compressor style vocal.
    // Increased reduction to 50% to ensure the user hears the effect immediately.
    presets.push_back({
        "Smooth Opto Vocal",
        "Vocals",
        0,                            // Opto
        -18.0f,                       // Unused
        4.0f,                         // Unused
        10.0f,                        // Unused
        300.0f,                       // Unused
        5.0f,                         // Makeup (opto reduces gain significantly)
        100.0f,                       // Mix
        60.0f,                        // SidechainHP
        false,                        // Manual makeup is more authentic for Opto
        0,                            // Vintage Saturation
        0,                            // Unused
        2,                            // Unused
        2,                            // Unused
        50.0f,                        // PeakRed: 50% (The sweet spot on a real unit)
        false                         // Compress Mode (Limit is too hard for vox)
    });

    // Classic FET vocal presence setting.
    // Fast attack ~0.5ms, fast release ~60ms.
    // Ratio 4:1 is the standard vocal setting, not 8:1 (which is too grabby).
    presets.push_back({
        "Vocal Presence",
        "Vocals",
        1,                            // Vintage FET
        -20.0f,                       // Drive input harder
        4.0f,                         // Unused (controlled by fetRatio)
        0.5f,                         // Attack: ~500µs
        60.0f,                        // Release: 60ms (Fast!)
        4.0f,                         // Makeup
        100.0f,                       // Mix
        100.0f,                       // HPF to prevent popping on plosives
        false,
        0,                            // Vintage
        0,                            // fetRatio: 4:1 (Classic Vocal)
        2,                            // Unused
        2,                            // Unused
        0.0f,
        false
    });

    // [UPDATED] Modern Pop Vocal.
    // Fast attack, Medium release, Auto-makeup for consistent level.
    presets.push_back({
        "Modern Pop Control",
        "Vocals",
        4,                            // Studio FET (Cleaner)
        -15.0f,
        4.0f,
        0.3f,                         // Very fast attack to catch peaks
        120.0f,                       // Medium release
        3.0f,
        100.0f,
        90.0f,
        true,                         // AutoMakeup On
        1,                            // Modern Saturation
        1,                            // fetRatio: 8:1 (Tighter control)
        2,
        2,
        0.0f,
        false
    });

    // ==================== DRUMS ====================

    // Classic Bus Sound.
    // Attack MUST be 30ms (Index 5) to let the kick/snare crack through.
    // Release Auto (Index 4) is the magic glue setting.
    presets.push_back({
        "Classic Drum Glue",
        "Drums",
        3,                            // Bus Compressor
        -15.0f,
        4.0f,                         // Ratio 4:1
        30.0f,                        // Unused
        100.0f,                       // Unused
        3.0f,
        100.0f,
        90.0f,                        // HPF crucial for drum bus to stop kick pumping
        true,
        0,                            // Vintage
        0,
        5,                            // Attack Index: 30ms (Slowest)
        4,                            // Release Index: Auto
        0.0f,
        false
    });

    // "All Buttons In" Room Nuke.
    // Attack needs to be slightly slower than instant to create "movement".
    presets.push_back({
        "Room Nuke (FET All)",
        "Drums",
        1,                            // Vintage FET
        -24.0f,                       // Smash it
        20.0f,
        0.8f,                         // Attack: ~800µs (Knob at 1/Slow) allows explosion
        50.0f,                        // Release: 50ms (Fastest) for max distortion
        12.0f,
        100.0f,
        60.0f,
        false,
        0,                            // Vintage
        4,                            // fetRatio: ALL BUTTONS
        2,
        2,
        0.0f,
        false
    });

    // [UPDATED] Snare Snap.
    // VCA Attack moved to 15ms. 5ms kills the transient.
    presets.push_back({
        "Snare Snap",
        "Drums",
        2,                            // Classic VCA
        -18.0f,
        4.0f,                         // 4:1 is punchier than 6:1 for snare
        15.0f,                        // Attack: 15ms (Lets the 'crack' pass)
        50.0f,                        // Release: 50ms (Quick recovery)
        4.0f,
        100.0f,
        100.0f,                       // High HPF to ignore kick leakage
        false,
        1,                            // Modern Saturation
        0,
        2,
        2,
        0.0f,
        false
    });

    // ==================== BASS ====================

    // FET Bass.
    // Bass needs a slower attack on FET to not distort sub frequencies.
    presets.push_back({
        "Rock Bass Anchor",
        "Bass",
        1,                            // Vintage FET
        -15.0f,
        4.0f,
        0.8f,                         // Attack: ~800µs (Slowest FET attack)
        250.0f,                       // Release: 250ms (Medium to reduce flutter)
        5.0f,
        100.0f,
        40.0f,
        false,
        0,
        0,                            // fetRatio: 4:1
        2,
        2,
        0.0f,
        false
    });

    // Opto Bass.
    // Maximize Peak Reduction for that classic Motown "pinned" feel.
    presets.push_back({
        "Vintage Pinned Bass",
        "Bass",
        0,                            // Opto
        -20.0f,
        4.0f,
        10.0f,
        300.0f,
        6.0f,
        100.0f,
        30.0f,
        false,
        0,
        0,
        2,
        2,
        65.0f,                        // High reduction
        false
    });

    // ==================== GUITARS ====================

    // [UPDATED] Acoustic.
    // Fast attack VCA to tame strumming spikes.
    presets.push_back({
        "Acoustic Strum Tamer",
        "Guitars",
        5,                            // Studio VCA
        -22.0f,
        3.0f,                         // 3:1
        2.0f,                         // Attack: 2ms (Catch the pick spikes)
        100.0f,                       // Release: 100ms
        2.0f,
        100.0f,
        80.0f,                        // Remove body boom
        true,
        2,                            // Pristine
        0,
        2,
        2,
        0.0f,
        false
    });

    // Funky GTR.
    // Using FET with fast release to accentuate the "up" strum.
    presets.push_back({
        "Funk Rhythm Guitar",
        "Guitars",
        1,                            // Vintage FET
        -12.0f,
        4.0f,
        0.3f,                         // Fast attack
        50.0f,                        // Fastest release (pumps with the rhythm)
        4.0f,
        100.0f,
        100.0f,
        false,
        0,
        0,                            // 4:1
        2,
        2,
        0.0f,
        false
    });

    // ==================== MIX BUS ====================

    // The "glue".
    // 10ms Attack / Auto Release / 4:1 / 4dB Gain Reduction is the classic recipe.
    presets.push_back({
        "Console-Style Glue",
        "Mix Bus",
        3,                            // Bus Compressor
        -20.0f,
        4.0f,                         // 4:1
        10.0f,                        // Unused
        100.0f,                       // Unused
        0.0f,
        100.0f,
        60.0f,
        true,
        0,                            // Vintage
        0,
        4,                            // Attack Index: 10ms (Classic Glue)
        4,                            // Release Index: Auto
        0.0f,
        false
    });

    // [UPDATED] Transparent Bus.
    // Lower ratio (1.5:1) for modern mastering/bus styles.
    presets.push_back({
        "Gentle Master",
        "Mix Bus",
        5,                            // Studio VCA
        -24.0f,
        1.5f,                         // 1.5:1 (Subtle)
        30.0f,                        // Attack: 30ms (Transparent)
        100.0f,                       // Release: 100ms
        0.0f,
        100.0f,
        30.0f,
        true,
        2,                            // Pristine
        0,
        2,
        2,
        0.0f,
        false
    });

    // ==================== CREATIVE ====================

    // EDM-style pumping compression
    // Release ~250ms works for 115-130 BPM quarter notes
    presets.push_back({
        "EDM Pump (115-130 BPM)",
        "Creative",
        1,                            // Vintage FET
        -10.0f,
        20.0f,
        0.1f,                         // Super fast attack
        250.0f,                       // Release timed to ~120bpm quarter note
        6.0f,
        100.0f,
        150.0f,                       // Trigger off kick/snare
        false,
        0,
        3,                            // 20:1
        2,
        2,
        0.0f,
        false
    });

    return presets;
}

inline std::vector<Preset> getPresetsByCategory(const juce::String& category)
{
    std::vector<Preset> result;
    auto allPresets = getFactoryPresets();
    for (const auto& preset : allPresets) {
        if (preset.category == category) result.push_back(preset);
    }
    return result;
}

inline void applyPreset(juce::AudioProcessorValueTreeState& params, const Preset& preset)
{
    // Set mode first
    if (auto* modeParam = params.getRawParameterValue("mode"))
        params.getParameter("mode")->setValueNotifyingHost(preset.mode / 6.0f);

    // Common parameters
    if (auto* p = params.getParameter("mix"))
        p->setValueNotifyingHost(preset.mix / 100.0f);

    if (auto* p = params.getParameter("sidechain_hp"))
        p->setValueNotifyingHost(params.getParameterRange("sidechain_hp").convertTo0to1(preset.sidechainHP));

    if (auto* p = params.getParameter("auto_makeup"))
        p->setValueNotifyingHost(preset.autoMakeup ? 1.0f : 0.0f);

    if (auto* p = params.getParameter("saturation_mode"))
        p->setValueNotifyingHost(preset.saturationMode / 2.0f);

    // Mode-specific parameters
    switch (preset.mode)
    {
        case 0: // Opto
            if (auto* p = params.getParameter("opto_peak_reduction"))
                p->setValueNotifyingHost(params.getParameterRange("opto_peak_reduction").convertTo0to1(preset.peakReduction));
            if (auto* p = params.getParameter("opto_gain"))
                p->setValueNotifyingHost(params.getParameterRange("opto_gain").convertTo0to1(preset.makeup));
            if (auto* p = params.getParameter("opto_limit"))
                p->setValueNotifyingHost(preset.limitMode ? 1.0f : 0.0f);
            break;

        case 1: // Vintage FET
        case 4: // Studio FET
            if (auto* p = params.getParameter("fet_input"))
                p->setValueNotifyingHost(params.getParameterRange("fet_input").convertTo0to1(-preset.threshold));
            if (auto* p = params.getParameter("fet_output"))
                p->setValueNotifyingHost(params.getParameterRange("fet_output").convertTo0to1(preset.makeup));
            if (auto* p = params.getParameter("fet_attack"))
                p->setValueNotifyingHost(params.getParameterRange("fet_attack").convertTo0to1(preset.attack));
            if (auto* p = params.getParameter("fet_release"))
                p->setValueNotifyingHost(params.getParameterRange("fet_release").convertTo0to1(preset.release));
            if (auto* p = params.getParameter("fet_ratio"))
                p->setValueNotifyingHost(preset.fetRatio / 4.0f);
            break;

        case 2: // Classic VCA
            if (auto* p = params.getParameter("vca_threshold"))
                p->setValueNotifyingHost(params.getParameterRange("vca_threshold").convertTo0to1(preset.threshold));
            if (auto* p = params.getParameter("vca_ratio"))
                p->setValueNotifyingHost(params.getParameterRange("vca_ratio").convertTo0to1(preset.ratio));
            if (auto* p = params.getParameter("vca_attack"))
                p->setValueNotifyingHost(params.getParameterRange("vca_attack").convertTo0to1(preset.attack));
            if (auto* p = params.getParameter("vca_release"))
                p->setValueNotifyingHost(params.getParameterRange("vca_release").convertTo0to1(preset.release));
            if (auto* p = params.getParameter("vca_output"))
                p->setValueNotifyingHost(params.getParameterRange("vca_output").convertTo0to1(preset.makeup));
            break;

        case 3: // Bus Compressor
            if (auto* p = params.getParameter("bus_threshold"))
                p->setValueNotifyingHost(params.getParameterRange("bus_threshold").convertTo0to1(preset.threshold));
            if (auto* p = params.getParameter("bus_ratio"))
                p->setValueNotifyingHost(params.getParameterRange("bus_ratio").convertTo0to1(preset.ratio));
            if (auto* p = params.getParameter("bus_attack"))
                p->setValueNotifyingHost(preset.busAttackIndex / 5.0f);
            if (auto* p = params.getParameter("bus_release"))
                p->setValueNotifyingHost(preset.busReleaseIndex / 4.0f);
            if (auto* p = params.getParameter("bus_makeup"))
                p->setValueNotifyingHost(params.getParameterRange("bus_makeup").convertTo0to1(preset.makeup));
            break;

        case 5: // Studio VCA
            if (auto* p = params.getParameter("studio_vca_threshold"))
                p->setValueNotifyingHost(params.getParameterRange("studio_vca_threshold").convertTo0to1(preset.threshold));
            if (auto* p = params.getParameter("studio_vca_ratio"))
                p->setValueNotifyingHost(params.getParameterRange("studio_vca_ratio").convertTo0to1(preset.ratio));
            if (auto* p = params.getParameter("studio_vca_attack"))
                p->setValueNotifyingHost(params.getParameterRange("studio_vca_attack").convertTo0to1(preset.attack));
            if (auto* p = params.getParameter("studio_vca_release"))
                p->setValueNotifyingHost(params.getParameterRange("studio_vca_release").convertTo0to1(preset.release));
            if (auto* p = params.getParameter("studio_vca_makeup"))
                p->setValueNotifyingHost(params.getParameterRange("studio_vca_makeup").convertTo0to1(preset.makeup));
            break;

        case 6: // Digital
            if (auto* p = params.getParameter("digital_threshold"))
                p->setValueNotifyingHost(params.getParameterRange("digital_threshold").convertTo0to1(preset.threshold));
            if (auto* p = params.getParameter("digital_ratio"))
                p->setValueNotifyingHost(params.getParameterRange("digital_ratio").convertTo0to1(preset.ratio));
            if (auto* p = params.getParameter("digital_attack"))
                p->setValueNotifyingHost(params.getParameterRange("digital_attack").convertTo0to1(preset.attack));
            if (auto* p = params.getParameter("digital_release"))
                p->setValueNotifyingHost(params.getParameterRange("digital_release").convertTo0to1(preset.release));
            if (auto* p = params.getParameter("digital_makeup"))
                p->setValueNotifyingHost(params.getParameterRange("digital_makeup").convertTo0to1(preset.makeup));
            break;
    }
}

} // namespace CompressorPresets
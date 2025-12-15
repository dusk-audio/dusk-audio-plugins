#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <map>

/**
 * Factory presets for Universal Compressor organized by category.
 * These are professional starting points for various mixing scenarios.
 */
namespace CompressorPresets
{

//==============================================================================
struct Preset
{
    juce::String name;
    juce::String category;
    int mode;                    // 0-6: Opto, FET, VCA, Bus, StudioFET, StudioVCA, Digital

    // Mode-specific parameters vary, but these are the common ones we'll set
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
    int busAttackIndex = 2;      // Attack preset index
    int busReleaseIndex = 2;     // Release preset index

    // Opto-specific
    float peakReduction = 0.0f;  // dB (0-40)
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

    // Smooth vocal leveling with opto - classic LA-2A style
    // Opto uses peakReduction (0-100) as main control, not threshold
    presets.push_back({
        "Smooth Vocal Leveling",     // name
        "Vocals",                     // category
        0,                            // mode: Opto
        -18.0f,                       // threshold (unused for opto)
        4.0f,                         // ratio (unused for opto - program dependent)
        10.0f,                        // attack (unused - opto is fixed 10ms)
        300.0f,                       // release (unused - opto is program dependent)
        6.0f,                         // makeup/gain
        100.0f,                       // mix
        80.0f,                        // sidechainHP
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        35.0f,                        // peakReduction: 35% - gentle vocal leveling
        false                         // limitMode: Compress mode for smoother action
    });

    // Aggressive vocal presence with FET - 1176 style bite
    // FET: threshold is controlled via INPUT gain (drives into fixed threshold)
    // Attack: 20µs-800µs, Release: 50ms-1.1s
    presets.push_back({
        "Vocal Presence (FET)",
        "Vocals",
        1,                            // mode: Vintage FET
        -15.0f,                       // threshold (negative = input drive in dB)
        8.0f,                         // ratio (unused - FET uses ratio buttons)
        0.3f,                         // attack: 300µs (fast for presence)
        200.0f,                       // release: 200ms (medium)
        4.0f,                         // makeup/output gain
        100.0f,                       // mix
        100.0f,                       // sidechainHP
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage
        1,                            // fetRatio: 8:1 (good for vocals)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Clean vocal control with Studio FET - cleaner 1176 character
    presets.push_back({
        "Clean Vocal Control",
        "Vocals",
        4,                            // mode: Studio FET
        -12.0f,                       // threshold (input drive)
        4.0f,                         // ratio (unused)
        0.4f,                         // attack: 400µs (slightly slower)
        300.0f,                       // release: 300ms
        3.0f,                         // makeup
        100.0f,                       // mix
        80.0f,                        // sidechainHP
        true,                         // autoMakeup
        1,                            // saturationMode: Modern (cleaner)
        0,                            // fetRatio: 4:1 (gentle)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Transparent vocal limiting - digital precision
    presets.push_back({
        "Transparent Vocal Limiter",
        "Vocals",
        6,                            // mode: Digital
        -6.0f,                        // threshold: -6dB (catch peaks)
        10.0f,                        // ratio: 10:1 (limiting)
        1.0f,                         // attack: 1ms (fast but not instant)
        100.0f,                       // release: 100ms
        0.0f,                         // makeup (auto handles it)
        100.0f,                       // mix
        60.0f,                        // sidechainHP
        true,                         // autoMakeup: yes for transparent operation
        2,                            // saturationMode: Pristine
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // ==================== DRUMS ====================

    // Punchy drum bus - SSL G-Bus style glue
    // Bus attack: 0=0.1ms, 1=0.3ms, 2=1ms, 3=3ms, 4=10ms, 5=30ms
    // Bus release: 0=100ms, 1=300ms, 2=600ms, 3=1200ms, 4=Auto
    presets.push_back({
        "Punchy Drum Bus",
        "Drums",
        3,                            // mode: Bus Compressor
        -16.0f,                       // threshold
        4.0f,                         // ratio: 4:1
        10.0f,                        // attack (unused - uses index)
        300.0f,                       // release (unused - uses index)
        2.0f,                         // makeup
        100.0f,                       // mix
        60.0f,                        // sidechainHP: filter out sub bass pumping
        true,                         // autoMakeup
        0,                            // saturationMode: Vintage
        0,                            // fetRatio (unused)
        4,                            // busAttackIndex: 10ms (lets transients through)
        4,                            // busReleaseIndex: Auto (program dependent)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Aggressive drums (FET) - 1176 all-in style
    presets.push_back({
        "Aggressive Drums (FET)",
        "Drums",
        1,                            // mode: Vintage FET
        -20.0f,                       // threshold (high input drive for aggression)
        20.0f,                        // ratio (unused)
        0.2f,                         // attack: 200µs (fast grab)
        150.0f,                       // release: 150ms
        6.0f,                         // makeup/output
        100.0f,                       // mix
        80.0f,                        // sidechainHP
        false,                        // autoMakeup (manual control for punch)
        0,                            // saturationMode: Vintage
        3,                            // fetRatio: 20:1 (aggressive limiting)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Parallel drum crush - FET all-buttons for NY compression
    presets.push_back({
        "Parallel Drum Crush",
        "Drums",
        1,                            // mode: Vintage FET
        -24.0f,                       // threshold (heavy drive)
        20.0f,                        // ratio (unused)
        0.1f,                         // attack: 100µs (fastest)
        80.0f,                        // release: 80ms (fast pumping)
        12.0f,                        // makeup/output (heavy makeup for crushed signal)
        35.0f,                        // mix: 35% wet (parallel blend)
        60.0f,                        // sidechainHP
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage (for grit)
        4,                            // fetRatio: All-buttons (maximum crush)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Snare snap - VCA precision for transient control
    presets.push_back({
        "Snare Snap",
        "Drums",
        2,                            // mode: Classic VCA
        -12.0f,                       // threshold
        6.0f,                         // ratio: 6:1
        5.0f,                         // attack: 5ms (let transient through)
        80.0f,                        // release: 80ms (recover before next hit)
        3.0f,                         // makeup
        100.0f,                       // mix
        100.0f,                       // sidechainHP: high to focus on snare body
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Room compression - Opto for smooth room tone
    presets.push_back({
        "Room Compression",
        "Drums",
        0,                            // mode: Opto
        -20.0f,                       // threshold (unused)
        4.0f,                         // ratio (unused)
        20.0f,                        // attack (unused)
        500.0f,                       // release (unused)
        8.0f,                         // makeup/gain (bring up room)
        50.0f,                        // mix: 50% parallel for room blend
        40.0f,                        // sidechainHP: low to let bass through
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        50.0f,                        // peakReduction: 50% (heavy compression on room)
        false                         // limitMode: Compress for smooth pumping
    });

    // ==================== BASS ====================

    // Tight bass control - VCA precision for consistent low end
    // VCA: Fast attack/release, precise control, OverEasy soft knee
    presets.push_back({
        "Tight Bass Control",
        "Bass",
        2,                            // mode: Classic VCA
        -16.0f,                       // threshold
        6.0f,                         // ratio: 6:1 (firm control)
        3.0f,                         // attack: 3ms (fast to catch transients)
        100.0f,                       // release: 100ms (recover between notes)
        2.0f,                         // makeup
        100.0f,                       // mix
        20.0f,                        // sidechainHP: 20Hz (keep sub bass in detection)
        true,                         // autoMakeup
        1,                            // saturationMode: Modern (clean)
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Warm bass leveling - Opto smoothness for organic sound
    // Opto: Program-dependent timing, tube warmth, peakReduction control
    presets.push_back({
        "Warm Bass Leveling",
        "Bass",
        0,                            // mode: Opto
        -18.0f,                       // threshold (unused)
        4.0f,                         // ratio (unused)
        15.0f,                        // attack (unused - opto is fixed)
        400.0f,                       // release (unused)
        3.0f,                         // makeup/gain
        100.0f,                       // mix
        30.0f,                        // sidechainHP: 30Hz (let sub through)
        false,                        // autoMakeup (manual gain staging)
        0,                            // saturationMode: Vintage (warm tube character)
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        25.0f,                        // peakReduction: 25% (moderate leveling)
        false                         // limitMode: Compress for smooth action
    });

    // Punchy bass attack - FET aggression for modern bass punch
    // FET: Attack 20µs-800µs, Release 50ms-1.1s
    presets.push_back({
        "Punchy Bass Attack",
        "Bass",
        1,                            // mode: Vintage FET
        -12.0f,                       // threshold (input drive)
        8.0f,                         // ratio (unused)
        0.5f,                         // attack: 500µs (let initial transient through)
        150.0f,                       // release: 150ms
        4.0f,                         // makeup/output
        100.0f,                       // mix
        40.0f,                        // sidechainHP: 40Hz (focus on attack, not sub)
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage (FET grit)
        1,                            // fetRatio: 8:1 (punchy but controlled)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // ==================== GUITARS ====================

    // Clean guitar sustain - Opto for smooth, natural sustain
    // Opto: Perfect for clean guitar - program-dependent release, tube warmth
    presets.push_back({
        "Clean Guitar Sustain",
        "Guitars",
        0,                            // mode: Opto
        -20.0f,                       // threshold (unused)
        4.0f,                         // ratio (unused)
        10.0f,                        // attack (unused)
        300.0f,                       // release (unused)
        2.0f,                         // makeup/gain
        100.0f,                       // mix
        80.0f,                        // sidechainHP: 80Hz (focus on guitar body)
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage (warm)
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        20.0f,                        // peakReduction: 20% (gentle sustain enhancement)
        false                         // limitMode: Compress for smooth sustain
    });

    // Acoustic guitar smoothing - Studio VCA for transparent control
    // Studio VCA: Modern, clean, RMS detection with soft knee
    presets.push_back({
        "Acoustic Smoothing",
        "Guitars",
        5,                            // mode: Studio VCA
        -18.0f,                       // threshold
        3.0f,                         // ratio: 3:1 (gentle)
        8.0f,                         // attack: 8ms (preserve pick attack)
        200.0f,                       // release: 200ms
        1.0f,                         // makeup
        100.0f,                       // mix
        60.0f,                        // sidechainHP: 60Hz
        true,                         // autoMakeup
        1,                            // saturationMode: Modern (transparent)
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Electric guitar crunch - FET aggression for rock tones
    // FET: Attack 20µs-800µs, adds harmonic grit
    presets.push_back({
        "Electric Crunch",
        "Guitars",
        1,                            // mode: Vintage FET
        -10.0f,                       // threshold (moderate drive)
        8.0f,                         // ratio (unused)
        0.6f,                         // attack: 600µs (let pick attack through)
        100.0f,                       // release: 100ms
        5.0f,                         // makeup/output
        100.0f,                       // mix
        100.0f,                       // sidechainHP: 100Hz (focus on mids)
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage (gritty)
        1,                            // fetRatio: 8:1 (aggressive but musical)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // ==================== MIX BUS ====================

    // SSL-style glue - Classic G-Bus sound
    // Bus attack: 0=0.1ms, 1=0.3ms, 2=1ms, 3=3ms, 4=10ms, 5=30ms
    // Bus release: 0=100ms, 1=300ms, 2=600ms, 3=1200ms, 4=Auto
    presets.push_back({
        "SSL-Style Glue",
        "Mix Bus",
        3,                            // mode: Bus Compressor
        -20.0f,                       // threshold
        4.0f,                         // ratio: 4:1 (classic setting)
        10.0f,                        // attack (unused - uses index)
        100.0f,                       // release (unused - uses index)
        0.0f,                         // makeup (auto handles it)
        100.0f,                       // mix
        60.0f,                        // sidechainHP: 60Hz (reduce bass pumping)
        true,                         // autoMakeup
        0,                            // saturationMode: Vintage (SSL character)
        0,                            // fetRatio (unused)
        3,                            // busAttackIndex: 3ms (classic SSL setting)
        4,                            // busReleaseIndex: Auto (program-dependent)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Gentle bus glue - Subtle cohesion without squashing
    presets.push_back({
        "Gentle Bus Glue",
        "Mix Bus",
        3,                            // mode: Bus Compressor
        -24.0f,                       // threshold (lower = lighter compression)
        2.0f,                         // ratio: 2:1 (gentle)
        30.0f,                        // attack (unused - uses index)
        300.0f,                       // release (unused - uses index)
        0.0f,                         // makeup
        100.0f,                       // mix
        40.0f,                        // sidechainHP: 40Hz
        true,                         // autoMakeup
        1,                            // saturationMode: Modern (cleaner)
        0,                            // fetRatio (unused)
        5,                            // busAttackIndex: 30ms (slowest - preserve transients)
        1,                            // busReleaseIndex: 300ms (medium)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Transparent bus control - Studio VCA for modern transparency
    presets.push_back({
        "Transparent Bus",
        "Mix Bus",
        5,                            // mode: Studio VCA
        -18.0f,                       // threshold
        2.0f,                         // ratio: 2:1 (gentle)
        15.0f,                        // attack: 15ms (preserve transients)
        150.0f,                       // release: 150ms
        0.0f,                         // makeup
        100.0f,                       // mix
        30.0f,                        // sidechainHP: 30Hz
        true,                         // autoMakeup
        2,                            // saturationMode: Pristine (transparent)
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // ==================== MASTERING ====================

    // Mastering glue - Bus compressor for final polish
    // Bus attack: 0=0.1ms, 1=0.3ms, 2=1ms, 3=3ms, 4=10ms, 5=30ms
    // Bus release: 0=100ms, 1=300ms, 2=600ms, 3=1200ms, 4=Auto
    presets.push_back({
        "Mastering Glue",
        "Mastering",
        3,                            // mode: Bus Compressor
        -22.0f,                       // threshold (light touch)
        2.0f,                         // ratio: 2:1 (gentle mastering ratio)
        30.0f,                        // attack (unused - uses index)
        300.0f,                       // release (unused - uses index)
        0.0f,                         // makeup
        100.0f,                       // mix
        30.0f,                        // sidechainHP: 30Hz (protect sub bass)
        true,                         // autoMakeup
        1,                            // saturationMode: Modern (clean)
        0,                            // fetRatio (unused)
        5,                            // busAttackIndex: 30ms (preserve transients for mastering)
        3,                            // busReleaseIndex: 1200ms (slow release for smooth)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Transparent mastering - Digital precision
    presets.push_back({
        "Transparent Master",
        "Mastering",
        6,                            // mode: Digital
        -16.0f,                       // threshold
        1.5f,                         // ratio: 1.5:1 (very gentle)
        10.0f,                        // attack: 10ms (preserve transients)
        200.0f,                       // release: 200ms
        0.0f,                         // makeup
        100.0f,                       // mix
        20.0f,                        // sidechainHP: 20Hz (full range)
        true,                         // autoMakeup
        2,                            // saturationMode: Pristine (transparent)
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Vintage mastering warmth - Opto for smooth analog vibe
    presets.push_back({
        "Vintage Master Warmth",
        "Mastering",
        0,                            // mode: Opto
        -24.0f,                       // threshold (unused)
        3.0f,                         // ratio (unused)
        20.0f,                        // attack (unused)
        400.0f,                       // release (unused)
        1.0f,                         // makeup/gain
        100.0f,                       // mix
        20.0f,                        // sidechainHP: 20Hz
        true,                         // autoMakeup
        0,                            // saturationMode: Vintage (warm tube)
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        15.0f,                        // peakReduction: 15% (subtle mastering leveling)
        false                         // limitMode: Compress for smooth action
    });

    // ==================== CREATIVE ====================

    // Pumping sidechain - EDM-style rhythmic compression
    // FET: Fast attack for instant grab, medium release for pump
    presets.push_back({
        "Pumping Sidechain",
        "Creative",
        1,                            // mode: Vintage FET
        -6.0f,                        // threshold (heavy drive into compressor)
        20.0f,                        // ratio (unused)
        0.02f,                        // attack: 20µs (fastest possible)
        200.0f,                       // release: 200ms (creates the pump)
        8.0f,                         // makeup/output
        100.0f,                       // mix
        200.0f,                       // sidechainHP: 200Hz (trigger on kick)
        false,                        // autoMakeup (manual for control)
        0,                            // saturationMode: Vintage (gritty pump)
        3,                            // fetRatio: 20:1 (hard limiting for pump)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Lo-fi crush - Extreme FET all-buttons destruction
    presets.push_back({
        "Lo-Fi Crush",
        "Creative",
        1,                            // mode: Vintage FET
        -4.0f,                        // threshold (extreme drive)
        20.0f,                        // ratio (unused)
        0.02f,                        // attack: 20µs (fastest)
        30.0f,                        // release: 30ms (fast, pumpy)
        12.0f,                        // makeup/output (loud crushed signal)
        100.0f,                       // mix
        80.0f,                        // sidechainHP
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage (maximum grit)
        4,                            // fetRatio: All-buttons (maximum crush)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    // Telephone effect - Heavy compression + high sidechain HP
    // VCA with extreme settings for lo-fi effect
    presets.push_back({
        "Telephone Effect",
        "Creative",
        2,                            // mode: Classic VCA
        -2.0f,                        // threshold (extreme compression)
        10.0f,                        // ratio: 10:1 (limiting)
        0.5f,                         // attack: 0.5ms (fast)
        50.0f,                        // release: 50ms (quick recovery)
        6.0f,                         // makeup
        100.0f,                       // mix
        300.0f,                       // sidechainHP: 300Hz (removes bass, telephone-like)
        false,                        // autoMakeup
        0,                            // saturationMode: Vintage
        0,                            // fetRatio (unused)
        2,                            // busAttackIndex (unused)
        2,                            // busReleaseIndex (unused)
        0.0f,                         // peakReduction (unused)
        false                         // limitMode (unused)
    });

    return presets;
}

//==============================================================================
// Helper to get presets by category
inline std::vector<Preset> getPresetsByCategory(const juce::String& category)
{
    std::vector<Preset> result;
    auto allPresets = getFactoryPresets();

    for (const auto& preset : allPresets)
    {
        if (preset.category == category)
            result.push_back(preset);
    }

    return result;
}

//==============================================================================
// Apply a preset to the processor's parameters
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

    // Mode-specific parameters based on which mode is selected
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

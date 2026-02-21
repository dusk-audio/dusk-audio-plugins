#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>
#include "EQBand.h"

/**
 * Factory presets for Multi-Q EQ.
 * Organized by use case with professional-quality settings.
 */
namespace MultiQPresets
{

struct BandPreset
{
    bool enabled = true;
    float freq = 1000.0f;     // Hz
    float gain = 0.0f;        // dB
    float q = 0.71f;          // Q factor
    int slope = 2;            // Filter slope (0-5 for 6-48 dB/oct)
};

struct Preset
{
    juce::String name;
    juce::String category;
    int eqType = 0;           // 0=Digital, 1=British, 2=Tube

    // Digital mode: 8 bands
    std::array<BandPreset, 8> bands;

    // Global settings
    float masterGain = 0.0f;
    bool hqEnabled = false;
    int qCoupleMode = 0;      // 0=Off, 1=Proportional, etc.
    int processingMode = 0;   // 0=Stereo, 1=Left, 2=Right, 3=Mid, 4=Side

    // Dynamics (per-band) - simplified: same settings for all bands
    bool dynamicsEnabled = false;
    float dynThreshold = -20.0f;
    float dynAttack = 10.0f;
    float dynRelease = 100.0f;
    float dynRange = 12.0f;
};

// Category definitions
inline const juce::StringArray Categories = {
    "Vocals",
    "Drums",
    "Bass",
    "Guitars",
    "Mix Bus",
    "Mastering",
    "Surgical",
    "Creative"
};

// Factory Presets
inline std::vector<Preset> getFactoryPresets()
{
    std::vector<Preset> presets;

    // ==================== VOCALS ====================

    // Vocal Presence - boost clarity range
    {
        Preset p;
        p.name = "Vocal Presence";
        p.category = "Vocals";
        p.eqType = 0; // Digital
        p.bands[0] = { true, 80.0f, 0.0f, 0.71f, 2 };       // HPF at 80Hz
        p.bands[1] = { true, 200.0f, -2.5f, 1.0f, 0 };      // Low shelf cut - reduce mud
        p.bands[2] = { true, 800.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[3] = { true, 2500.0f, 2.0f, 1.2f, 0 };      // Presence boost
        p.bands[4] = { true, 5000.0f, 1.5f, 0.8f, 0 };      // Air/clarity
        p.bands[5] = { true, 10000.0f, 1.0f, 0.71f, 0 };    // Brilliance
        p.bands[6] = { true, 12000.0f, 0.0f, 0.71f, 0 };    // High shelf (flat)
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.hqEnabled = true;
        p.qCoupleMode = 2; // Light
        presets.push_back(p);
    }

    // Vocal De-Mud - remove boxiness
    {
        Preset p;
        p.name = "Vocal De-Mud";
        p.category = "Vocals";
        p.eqType = 0;
        p.bands[0] = { true, 100.0f, 0.0f, 0.71f, 2 };      // HPF
        p.bands[1] = { true, 250.0f, -3.0f, 1.5f, 0 };      // Low shelf - mud cut
        p.bands[2] = { true, 400.0f, -2.0f, 2.5f, 0 };      // Boxiness notch
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[4] = { true, 3000.0f, 1.0f, 1.0f, 0 };      // Slight presence
        p.bands[5] = { true, 8000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[6] = { true, 12000.0f, 0.0f, 0.71f, 0 };    // Flat
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.qCoupleMode = 3; // Medium
        presets.push_back(p);
    }

    // Broadcast Vocal - radio/podcast style
    {
        Preset p;
        p.name = "Broadcast Vocal";
        p.category = "Vocals";
        p.eqType = 0;
        p.bands[0] = { true, 120.0f, 0.0f, 0.71f, 3 };      // HPF steep
        p.bands[1] = { true, 180.0f, 2.0f, 0.8f, 0 };       // Low warmth
        p.bands[2] = { true, 350.0f, -1.5f, 2.0f, 0 };      // Reduce mud
        p.bands[3] = { true, 2000.0f, 1.5f, 1.5f, 0 };      // Clarity
        p.bands[4] = { true, 4500.0f, 2.5f, 1.0f, 0 };      // Presence
        p.bands[5] = { true, 8000.0f, 1.0f, 0.71f, 0 };     // Air
        p.bands[6] = { true, 10000.0f, 0.0f, 0.71f, 0 };    // Flat
        p.bands[7] = { true, 15000.0f, 0.0f, 0.71f, 2 };    // LPF gentle
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // ==================== DRUMS ====================

    // Punchy Kick
    {
        Preset p;
        p.name = "Punchy Kick";
        p.category = "Drums";
        p.eqType = 0;
        p.bands[0] = { true, 35.0f, 0.0f, 0.71f, 3 };       // HPF - remove sub rumble
        p.bands[1] = { true, 60.0f, 3.0f, 1.2f, 0 };        // Sub punch
        p.bands[2] = { true, 120.0f, 2.0f, 1.5f, 0 };       // Body
        p.bands[3] = { true, 350.0f, -3.0f, 2.0f, 0 };      // Remove mud
        p.bands[4] = { true, 2500.0f, 2.5f, 2.0f, 0 };      // Click/attack
        p.bands[5] = { true, 5000.0f, 1.0f, 1.0f, 0 };      // Beater
        p.bands[6] = { true, 8000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[7] = { true, 12000.0f, 0.0f, 0.71f, 2 };    // LPF
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // Snare Crack
    {
        Preset p;
        p.name = "Snare Crack";
        p.category = "Drums";
        p.eqType = 0;
        p.bands[0] = { true, 80.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 150.0f, 1.0f, 1.0f, 0 };       // Body
        p.bands[2] = { true, 400.0f, -2.0f, 2.5f, 0 };      // Remove box
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[4] = { true, 2000.0f, 3.0f, 1.5f, 0 };      // Crack
        p.bands[5] = { true, 5000.0f, 2.0f, 1.2f, 0 };      // Snare wire
        p.bands[6] = { true, 10000.0f, 1.5f, 0.71f, 0 };    // High shelf air
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        presets.push_back(p);
    }

    // Overhead Clarity
    {
        Preset p;
        p.name = "Overhead Clarity";
        p.category = "Drums";
        p.eqType = 0;
        p.bands[0] = { true, 200.0f, 0.0f, 0.71f, 2 };      // HPF - remove kick bleed
        p.bands[1] = { true, 400.0f, -1.5f, 1.5f, 0 };      // Remove mud
        p.bands[2] = { true, 800.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[3] = { true, 3000.0f, 1.5f, 1.2f, 0 };      // Stick definition
        p.bands[4] = { true, 6000.0f, 2.0f, 0.8f, 0 };      // Cymbal presence
        p.bands[5] = { true, 10000.0f, 2.5f, 0.71f, 0 };    // Air
        p.bands[6] = { true, 14000.0f, 1.0f, 0.71f, 0 };    // Sparkle
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // ==================== BASS ====================

    // Bass Definition
    {
        Preset p;
        p.name = "Bass Definition";
        p.category = "Bass";
        p.eqType = 0;
        p.bands[0] = { true, 30.0f, 0.0f, 0.71f, 3 };       // HPF - sub cleanup
        p.bands[1] = { true, 80.0f, 2.0f, 1.0f, 0 };        // Low-end punch
        p.bands[2] = { true, 200.0f, -1.5f, 1.5f, 0 };      // Reduce mud
        p.bands[3] = { true, 500.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[4] = { true, 1200.0f, 2.5f, 1.5f, 0 };      // Growl/attack
        p.bands[5] = { true, 3000.0f, 1.0f, 1.0f, 0 };      // String noise
        p.bands[6] = { true, 6000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[7] = { true, 8000.0f, 0.0f, 0.71f, 2 };     // LPF
        presets.push_back(p);
    }

    // Sub Bass Enhancement
    {
        Preset p;
        p.name = "Sub Enhancement";
        p.category = "Bass";
        p.eqType = 0;
        p.bands[0] = { true, 25.0f, 0.0f, 0.71f, 2 };       // HPF very low
        p.bands[1] = { true, 50.0f, 4.0f, 1.5f, 0 };        // Sub boost
        p.bands[2] = { true, 100.0f, 1.5f, 1.0f, 0 };       // Low punch
        p.bands[3] = { true, 300.0f, -2.0f, 1.5f, 0 };      // Clean up mud
        p.bands[4] = { true, 700.0f, -1.0f, 1.2f, 0 };      // Reduce honk
        p.bands[5] = { true, 2000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[6] = { true, 5000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[7] = { true, 6000.0f, 0.0f, 0.71f, 3 };     // LPF steep
        presets.push_back(p);
    }

    // ==================== GUITARS ====================

    // Electric Crunch
    {
        Preset p;
        p.name = "Electric Crunch";
        p.category = "Guitars";
        p.eqType = 0;
        p.bands[0] = { true, 80.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 150.0f, 1.0f, 0.8f, 0 };       // Low warmth
        p.bands[2] = { true, 400.0f, -2.0f, 2.0f, 0 };      // Remove mud
        p.bands[3] = { true, 1500.0f, 1.5f, 1.2f, 0 };      // Body
        p.bands[4] = { true, 3000.0f, 2.5f, 1.5f, 0 };      // Crunch/bite
        p.bands[5] = { true, 6000.0f, 1.0f, 1.0f, 0 };      // Presence
        p.bands[6] = { true, 10000.0f, -1.0f, 0.71f, 0 };   // Reduce fizz
        p.bands[7] = { true, 12000.0f, 0.0f, 0.71f, 2 };    // LPF
        presets.push_back(p);
    }

    // Acoustic Sparkle
    {
        Preset p;
        p.name = "Acoustic Sparkle";
        p.category = "Guitars";
        p.eqType = 0;
        p.bands[0] = { true, 100.0f, 0.0f, 0.71f, 2 };      // HPF
        p.bands[1] = { true, 200.0f, -1.5f, 1.2f, 0 };      // Reduce boom
        p.bands[2] = { true, 500.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[3] = { true, 2000.0f, 1.5f, 1.0f, 0 };      // Body
        p.bands[4] = { true, 5000.0f, 2.0f, 0.8f, 0 };      // Pick attack
        p.bands[5] = { true, 8000.0f, 2.5f, 0.71f, 0 };     // Shimmer
        p.bands[6] = { true, 12000.0f, 1.5f, 0.71f, 0 };    // Air
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // ==================== MIX BUS ====================

    // Mix Bus Polish
    {
        Preset p;
        p.name = "Mix Bus Polish";
        p.category = "Mix Bus";
        p.eqType = 0;
        p.bands[0] = { true, 30.0f, 0.0f, 0.71f, 2 };       // HPF gentle
        p.bands[1] = { true, 60.0f, 1.0f, 0.71f, 0 };       // Low shelf warmth
        p.bands[2] = { true, 300.0f, -0.5f, 0.5f, 0 };      // Subtle mud cut
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[4] = { true, 3000.0f, 0.5f, 0.5f, 0 };      // Subtle presence
        p.bands[5] = { true, 8000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[6] = { true, 12000.0f, 1.5f, 0.71f, 0 };    // High shelf air
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.hqEnabled = true;
        p.qCoupleMode = 2; // Light
        presets.push_back(p);
    }

    // Loudness Curve
    {
        Preset p;
        p.name = "Loudness Curve";
        p.category = "Mix Bus";
        p.eqType = 0;
        p.bands[0] = { true, 25.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 80.0f, 2.0f, 0.71f, 0 };       // Low shelf boost
        p.bands[2] = { true, 200.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[3] = { true, 800.0f, -0.5f, 0.5f, 0 };      // Subtle mid cut
        p.bands[4] = { true, 2500.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[5] = { true, 6000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[6] = { true, 10000.0f, 2.0f, 0.71f, 0 };    // High shelf boost
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // ==================== MASTERING ====================

    // Mastering Wide
    {
        Preset p;
        p.name = "Mastering Wide";
        p.category = "Mastering";
        p.eqType = 0;
        p.bands[0] = { true, 25.0f, 0.0f, 0.71f, 3 };       // HPF steep
        p.bands[1] = { true, 50.0f, 0.5f, 0.5f, 0 };        // Subtle sub lift
        p.bands[2] = { true, 250.0f, -0.3f, 0.4f, 0 };      // Very gentle mud cut
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[4] = { true, 4000.0f, 0.3f, 0.4f, 0 };      // Subtle presence
        p.bands[5] = { true, 8000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[6] = { true, 14000.0f, 0.8f, 0.71f, 0 };    // High shelf air
        p.bands[7] = { true, 20000.0f, 0.0f, 0.71f, 1 };    // LPF gentle
        p.hqEnabled = true;
        p.qCoupleMode = 1; // Proportional
        presets.push_back(p);
    }

    // Mastering Surgical
    {
        Preset p;
        p.name = "Mastering Surgical";
        p.category = "Mastering";
        p.eqType = 0;
        p.bands[0] = { true, 28.0f, 0.0f, 0.71f, 4 };       // HPF very steep
        p.bands[1] = { true, 100.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[2] = { true, 300.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[4] = { true, 3000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[5] = { true, 8000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[6] = { true, 12000.0f, 0.0f, 0.71f, 0 };    // Flat
        p.bands[7] = { true, 19500.0f, 0.0f, 0.71f, 2 };    // LPF near Nyquist
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // ==================== SURGICAL ====================

    // Notch Resonance
    {
        Preset p;
        p.name = "Notch Template";
        p.category = "Surgical";
        p.eqType = 0;
        p.bands[0] = { false, 30.0f, 0.0f, 0.71f, 2 };      // HPF off
        p.bands[1] = { false, 100.0f, 0.0f, 0.71f, 0 };     // Off
        p.bands[2] = { true, 400.0f, -6.0f, 8.0f, 0 };      // Narrow notch example
        p.bands[3] = { true, 800.0f, -6.0f, 8.0f, 0 };      // Narrow notch example
        p.bands[4] = { true, 2000.0f, -6.0f, 8.0f, 0 };     // Narrow notch example
        p.bands[5] = { false, 5000.0f, 0.0f, 0.71f, 0 };    // Off
        p.bands[6] = { false, 10000.0f, 0.0f, 0.71f, 0 };   // Off
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.qCoupleMode = 0; // Off for surgical work
        presets.push_back(p);
    }

    // De-Ess
    {
        Preset p;
        p.name = "De-Ess";
        p.category = "Surgical";
        p.eqType = 0;
        p.bands[0] = { false, 30.0f, 0.0f, 0.71f, 2 };      // HPF off
        p.bands[1] = { false, 100.0f, 0.0f, 0.71f, 0 };     // Off
        p.bands[2] = { false, 300.0f, 0.0f, 0.71f, 0 };     // Off
        p.bands[3] = { false, 1000.0f, 0.0f, 0.71f, 0 };    // Off
        p.bands[4] = { true, 5500.0f, -4.0f, 3.0f, 0 };     // S frequency region
        p.bands[5] = { true, 7500.0f, -3.0f, 2.5f, 0 };     // Secondary S region
        p.bands[6] = { false, 10000.0f, 0.0f, 0.71f, 0 };   // Off
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.dynamicsEnabled = true;
        p.dynThreshold = -25.0f;
        p.dynAttack = 1.0f;
        p.dynRelease = 50.0f;
        p.dynRange = 8.0f;
        presets.push_back(p);
    }

    // ==================== CREATIVE ====================

    // Telephone Effect
    {
        Preset p;
        p.name = "Telephone Effect";
        p.category = "Creative";
        p.eqType = 0;
        p.bands[0] = { true, 400.0f, 0.0f, 0.71f, 4 };      // HPF steep
        p.bands[1] = { true, 500.0f, -6.0f, 0.71f, 0 };     // Low cut
        p.bands[2] = { true, 1000.0f, 3.0f, 1.5f, 0 };      // Mid boost
        p.bands[3] = { true, 2000.0f, 4.0f, 1.2f, 0 };      // Presence
        p.bands[4] = { true, 3500.0f, 2.0f, 1.0f, 0 };      // Upper mid
        p.bands[5] = { true, 4500.0f, -6.0f, 0.71f, 0 };    // High cut
        p.bands[6] = { true, 5000.0f, 0.0f, 0.71f, 0 };     // Transition
        p.bands[7] = { true, 5500.0f, 0.0f, 0.71f, 4 };     // LPF steep
        presets.push_back(p);
    }

    // Lo-Fi
    {
        Preset p;
        p.name = "Lo-Fi Warmth";
        p.category = "Creative";
        p.eqType = 0;
        p.bands[0] = { true, 60.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 100.0f, 3.0f, 0.71f, 0 };      // Low shelf boost
        p.bands[2] = { true, 400.0f, 1.0f, 0.8f, 0 };       // Warm mid
        p.bands[3] = { true, 1500.0f, -1.0f, 1.0f, 0 };     // Slight dip
        p.bands[4] = { true, 3000.0f, -2.0f, 1.2f, 0 };     // Reduce harshness
        p.bands[5] = { true, 6000.0f, -3.0f, 0.71f, 0 };    // Roll off highs
        p.bands[6] = { true, 10000.0f, -4.0f, 0.71f, 0 };   // More rolloff
        p.bands[7] = { true, 11000.0f, 0.0f, 0.71f, 3 };    // LPF steep
        presets.push_back(p);
    }

    // Mid-Side Widener (requires M/S mode)
    {
        Preset p;
        p.name = "M/S Width Boost";
        p.category = "Creative";
        p.eqType = 0;
        p.bands[0] = { false, 30.0f, 0.0f, 0.71f, 2 };      // HPF off
        p.bands[1] = { true, 100.0f, -2.0f, 0.71f, 0 };     // Cut sides in low
        p.bands[2] = { true, 300.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[4] = { true, 4000.0f, 2.0f, 0.8f, 0 };      // Boost upper mids
        p.bands[5] = { true, 8000.0f, 3.0f, 0.71f, 0 };     // Boost highs
        p.bands[6] = { true, 12000.0f, 2.0f, 0.71f, 0 };    // Air
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };   // LPF off
        p.processingMode = 4; // Side
        presets.push_back(p);
    }

    return presets;
}

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

inline void applyPreset(juce::AudioProcessorValueTreeState& params, const Preset& preset)
{
    if (auto* p = params.getParameter(ParamIDs::eqType))
        p->setValueNotifyingHost(params.getParameterRange(ParamIDs::eqType).convertTo0to1(static_cast<float>(preset.eqType)));

    for (int i = 0; i < 8; ++i)
    {
        const auto& band = preset.bands[static_cast<size_t>(i)];
        int bandNum = i + 1;

        if (auto* p = params.getParameter(ParamIDs::bandEnabled(bandNum)))
            p->setValueNotifyingHost(band.enabled ? 1.0f : 0.0f);

        if (auto* p = params.getParameter(ParamIDs::bandFreq(bandNum)))
            p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandFreq(bandNum)).convertTo0to1(band.freq));

        if (auto* p = params.getParameter(ParamIDs::bandGain(bandNum)))
            p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandGain(bandNum)).convertTo0to1(band.gain));

        if (auto* p = params.getParameter(ParamIDs::bandQ(bandNum)))
            p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandQ(bandNum)).convertTo0to1(band.q));

        if (auto* p = params.getParameter(ParamIDs::bandSlope(bandNum)))
            p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandSlope(bandNum)).convertTo0to1(static_cast<float>(band.slope)));
    }

    // Global settings
    if (auto* p = params.getParameter(ParamIDs::masterGain))
        p->setValueNotifyingHost(params.getParameterRange(ParamIDs::masterGain).convertTo0to1(preset.masterGain));

    if (auto* p = params.getParameter(ParamIDs::hqEnabled))
        p->setValueNotifyingHost(preset.hqEnabled
            ? params.getParameterRange(ParamIDs::hqEnabled).convertTo0to1(1.0f)  // "2x"
            : params.getParameterRange(ParamIDs::hqEnabled).convertTo0to1(0.0f));  // "Off"

    if (auto* p = params.getParameter(ParamIDs::qCoupleMode))
        p->setValueNotifyingHost(params.getParameterRange(ParamIDs::qCoupleMode).convertTo0to1(static_cast<float>(preset.qCoupleMode)));

    if (auto* p = params.getParameter(ParamIDs::processingMode))
        p->setValueNotifyingHost(params.getParameterRange(ParamIDs::processingMode).convertTo0to1(static_cast<float>(preset.processingMode)));

    for (int i = 1; i <= 8; ++i)
    {
        if (auto* p = params.getParameter(ParamIDs::bandDynEnabled(i)))
            p->setValueNotifyingHost(preset.dynamicsEnabled ? 1.0f : 0.0f);

        if (preset.dynamicsEnabled)
        {
            if (auto* p = params.getParameter(ParamIDs::bandDynThreshold(i)))
                p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandDynThreshold(i)).convertTo0to1(preset.dynThreshold));

            if (auto* p = params.getParameter(ParamIDs::bandDynAttack(i)))
                p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandDynAttack(i)).convertTo0to1(preset.dynAttack));

            if (auto* p = params.getParameter(ParamIDs::bandDynRelease(i)))
                p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandDynRelease(i)).convertTo0to1(preset.dynRelease));

            if (auto* p = params.getParameter(ParamIDs::bandDynRange(i)))
                p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandDynRange(i)).convertTo0to1(preset.dynRange));
        }
    }
}

} // namespace MultiQPresets

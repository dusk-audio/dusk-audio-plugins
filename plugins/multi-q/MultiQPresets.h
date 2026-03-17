#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include "EQBand.h"

/**
 * Factory presets for Multi-Q EQ.
 * Organized by use case with professional-quality settings.
 */
namespace MultiQPresets
{

struct BritishPreset
{
    float hpfFreq = 20.0f;
    bool hpfEnabled = false;
    float lpfFreq = 20000.0f;
    bool lpfEnabled = false;
    float lfGain = 0.0f, lfFreq = 100.0f;
    bool lfBell = false;
    float lmGain = 0.0f, lmFreq = 600.0f, lmQ = 0.7f;
    float hmGain = 0.0f, hmFreq = 2000.0f, hmQ = 0.7f;
    float hfGain = 0.0f, hfFreq = 8000.0f;
    bool hfBell = false;
    int mode = 0;           // 0=Brown(E-Series), 1=Black(G-Series)
    float saturation = 0.0f;
    float inputGain = 0.0f;
    float outputGain = 0.0f;
};

struct TubePreset
{
    float lfBoostGain = 0.0f;
    float lfBoostFreq = 60.0f;
    float lfAttenGain = 0.0f;
    float hfBoostGain = 0.0f;
    float hfBoostFreq = 8000.0f;
    float hfBoostBandwidth = 0.5f;
    float hfAttenGain = 0.0f;
    float hfAttenFreq = 10000.0f;
    float inputGain = 0.0f;
    float outputGain = 0.0f;
    float tubeDrive = 0.3f;
    bool midEnabled = false;
    float midDipFreq = 1000.0f;
    float midDip = 0.0f;
};

struct BandDynamics
{
    bool enabled = false;
    float threshold = -20.0f;
    float attack = 10.0f;
    float release = 100.0f;
    float range = 12.0f;
    float ratio = 4.0f;
};

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
    int eqType = 0;           // 0=Digital, 1=Match, 2=British, 3=Tube

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

    // Mode-specific parameters (optional)
    BritishPreset british;
    bool hasBritish = false;
    TubePreset tube;
    bool hasTube = false;

    // Per-band dynamics (overrides global dynamics when band's enabled flag is true)
    std::array<BandDynamics, 8> bandDynamics{};
    bool hasPerBandDynamics = false;
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

    // Presence Lift
    {
        Preset p;
        p.name = "Presence Lift";
        p.category = "Vocals";
        p.eqType = 0;
        p.bands[0] = { true, 80.0f, 0.0f, 0.71f, 2 };       // HPF at 80Hz
        p.bands[1] = { true, 200.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[2] = { true, 600.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 2500.0f, 2.5f, 0.8f, 0 };       // Presence boost
        p.bands[4] = { true, 3500.0f, 2.0f, 0.9f, 0 };       // Upper presence
        p.bands[5] = { true, 8000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[6] = { true, 12000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };    // LPF off
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // De-Mud
    {
        Preset p;
        p.name = "De-Mud";
        p.category = "Vocals";
        p.eqType = 0;
        p.bands[0] = { true, 80.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 250.0f, -2.5f, 1.2f, 0 };       // Low mud cut
        p.bands[2] = { true, 350.0f, -3.0f, 1.8f, 0 };       // Mud center cut
        p.bands[3] = { true, 800.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[4] = { true, 2000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[5] = { true, 6000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[6] = { true, 12000.0f, 1.5f, 0.71f, 0 };     // Air boost
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };    // LPF off
        presets.push_back(p);
    }

    // Podcast Voice
    {
        Preset p;
        p.name = "Podcast Voice";
        p.category = "Vocals";
        p.eqType = 0;
        p.bands[0] = { true, 100.0f, 0.0f, 0.71f, 3 };      // HPF steep
        p.bands[1] = { true, 200.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[2] = { true, 800.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 2500.0f, 2.0f, 0.8f, 0 };       // Intelligibility
        p.bands[4] = { true, 4000.0f, 1.5f, 0.9f, 0 };       // Clarity
        p.bands[5] = { true, 5000.0f, 1.0f, 1.0f, 0 };       // Upper clarity
        p.bands[6] = { true, 8000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[7] = { true, 12000.0f, 0.0f, 0.71f, 2 };     // LPF
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

    // Kick Drum Focus
    {
        Preset p;
        p.name = "Kick Drum Focus";
        p.category = "Drums";
        p.eqType = 0;
        p.bands[0] = { true, 30.0f, 0.0f, 0.71f, 2 };       // HPF sub cleanup
        p.bands[1] = { true, 60.0f, 3.5f, 1.2f, 0 };         // Sub boost
        p.bands[2] = { true, 150.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 300.0f, -3.5f, 2.0f, 0 };       // Mud cut
        p.bands[4] = { true, 1000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[5] = { true, 3500.0f, 2.5f, 1.5f, 0 };       // Click/attack
        p.bands[6] = { true, 6000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[7] = { true, 10000.0f, 0.0f, 0.71f, 2 };     // LPF
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // Snare Snap
    {
        Preset p;
        p.name = "Snare Snap";
        p.category = "Drums";
        p.eqType = 0;
        p.bands[0] = { true, 100.0f, 0.0f, 0.71f, 2 };      // HPF
        p.bands[1] = { true, 200.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[2] = { true, 400.0f, -2.5f, 2.0f, 0 };       // Box cut
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[4] = { true, 2500.0f, 3.0f, 1.2f, 0 };       // Crack
        p.bands[5] = { true, 4000.0f, 2.0f, 1.0f, 0 };       // Snap
        p.bands[6] = { true, 8000.0f, 1.5f, 0.71f, 0 };      // Air
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };    // LPF off
        presets.push_back(p);
    }

    // Overhead Brightness
    {
        Preset p;
        p.name = "Overhead Brightness";
        p.category = "Drums";
        p.eqType = 0;
        p.bands[0] = { true, 200.0f, 0.0f, 0.71f, 2 };      // HPF remove lows
        p.bands[1] = { true, 400.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[2] = { true, 1000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[3] = { true, 3000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[4] = { true, 6000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[5] = { true, 8000.0f, 2.0f, 0.5f, 0 };       // Brightness shelf
        p.bands[6] = { true, 12000.0f, 2.5f, 0.5f, 0 };      // Air shelf
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };    // LPF off
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

    // Electric Guitar Scoop
    {
        Preset p;
        p.name = "Electric Guitar Scoop";
        p.category = "Guitars";
        p.eqType = 0;
        p.bands[0] = { true, 80.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 150.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[2] = { true, 400.0f, -3.0f, 1.5f, 0 };       // Scoop
        p.bands[3] = { true, 800.0f, -1.0f, 1.0f, 0 };       // Slight dip
        p.bands[4] = { true, 2000.0f, 2.0f, 1.2f, 0 };       // Presence
        p.bands[5] = { true, 4000.0f, 2.5f, 1.0f, 0 };       // Bite
        p.bands[6] = { true, 8000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[7] = { true, 12000.0f, 0.0f, 0.71f, 2 };     // LPF
        presets.push_back(p);
    }

    // Acoustic Guitar Sparkle
    {
        Preset p;
        p.name = "Acoustic Guitar Sparkle";
        p.category = "Guitars";
        p.eqType = 0;
        p.bands[0] = { true, 80.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 200.0f, -1.5f, 1.0f, 0 };       // Reduce boom
        p.bands[2] = { true, 500.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 2000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[4] = { true, 5000.0f, 2.0f, 0.8f, 0 };       // Sparkle
        p.bands[5] = { true, 8000.0f, 1.0f, 0.71f, 0 };      // Shimmer
        p.bands[6] = { true, 12000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };    // LPF off
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // Bass Guitar Punch
    {
        Preset p;
        p.name = "Bass Guitar Punch";
        p.category = "Guitars";
        p.eqType = 0;
        p.bands[0] = { true, 35.0f, 0.0f, 0.71f, 2 };       // HPF sub cleanup
        p.bands[1] = { true, 80.0f, 3.0f, 1.0f, 0 };         // Low punch
        p.bands[2] = { true, 250.0f, -2.5f, 1.5f, 0 };       // Mud cut
        p.bands[3] = { true, 700.0f, 2.0f, 1.2f, 0 };        // Growl
        p.bands[4] = { true, 1500.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[5] = { true, 3000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[6] = { true, 5000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[7] = { true, 8000.0f, 0.0f, 0.71f, 2 };      // LPF
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

    // Gentle Smile
    {
        Preset p;
        p.name = "Gentle Smile";
        p.category = "Mix Bus";
        p.eqType = 0;
        p.bands[0] = { true, 25.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 80.0f, 1.5f, 0.5f, 0 };         // Bass shelf boost
        p.bands[2] = { true, 300.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[4] = { true, 3000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[5] = { true, 6000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[6] = { true, 10000.0f, 1.5f, 0.5f, 0 };      // HF shelf boost
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };    // LPF off
        p.hqEnabled = true;
        p.qCoupleMode = 2; // Light
        presets.push_back(p);
    }

    // Mid Forward
    {
        Preset p;
        p.name = "Mid Forward";
        p.category = "Mix Bus";
        p.eqType = 0;
        p.bands[0] = { true, 30.0f, 0.0f, 0.71f, 2 };       // HPF
        p.bands[1] = { true, 100.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[2] = { true, 500.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 1500.0f, 1.0f, 0.6f, 0 };       // Lower mid boost
        p.bands[4] = { true, 2500.0f, 1.5f, 0.7f, 0 };       // Mid presence
        p.bands[5] = { true, 3500.0f, 1.0f, 0.8f, 0 };       // Upper mid
        p.bands[6] = { true, 8000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };    // LPF off
        p.hqEnabled = true;
        presets.push_back(p);
    }

    // Low End Cleanup
    {
        Preset p;
        p.name = "Low End Cleanup";
        p.category = "Mix Bus";
        p.eqType = 0;
        p.bands[0] = { true, 30.0f, 0.0f, 0.71f, 3 };       // HPF at 30Hz steep
        p.bands[1] = { true, 60.0f, -1.5f, 1.5f, 0 };        // Tighten low end
        p.bands[2] = { true, 200.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 500.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[4] = { true, 1500.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[5] = { true, 4000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[6] = { true, 10000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };    // LPF off
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

    // Mastering Air
    {
        Preset p;
        p.name = "Mastering Air";
        p.category = "Mastering";
        p.eqType = 0;
        p.bands[0] = { true, 25.0f, 0.0f, 0.71f, 3 };       // HPF steep
        p.bands[1] = { true, 100.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[2] = { true, 500.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 1500.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[4] = { true, 4000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[5] = { true, 8000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[6] = { true, 15000.0f, 1.5f, 0.4f, 0 };      // Air shelf
        p.bands[7] = { true, 20000.0f, 0.0f, 0.71f, 1 };     // LPF gentle
        p.hqEnabled = true;
        p.qCoupleMode = 1; // Proportional
        presets.push_back(p);
    }

    // Mastering Warmth
    {
        Preset p;
        p.name = "Mastering Warmth";
        p.category = "Mastering";
        p.eqType = 0;
        p.bands[0] = { true, 25.0f, 0.0f, 0.71f, 3 };       // HPF steep
        p.bands[1] = { true, 100.0f, 1.0f, 0.4f, 0 };        // Warm shelf
        p.bands[2] = { true, 300.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 1000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[4] = { true, 3000.0f, -0.5f, 0.8f, 0 };      // Gentle dip
        p.bands[5] = { true, 6000.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[6] = { true, 12000.0f, 0.0f, 0.71f, 0 };     // Flat
        p.bands[7] = { true, 20000.0f, 0.0f, 0.71f, 1 };     // LPF gentle
        p.hqEnabled = true;
        p.qCoupleMode = 1; // Proportional
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

    // ==================== BRITISH (eqType=2) ====================

    // Console Vocal Chain
    {
        Preset p;
        p.name = "Console Vocal Chain";
        p.category = "Vocals";
        p.eqType = 2;
        p.hasBritish = true;
        p.british.hpfFreq = 80.0f; p.british.hpfEnabled = true;
        p.british.lpfFreq = 12000.0f; p.british.lpfEnabled = true;
        p.british.lfGain = 2.0f; p.british.lfFreq = 100.0f; p.british.lfBell = false;
        p.british.lmGain = -1.5f; p.british.lmFreq = 400.0f; p.british.lmQ = 0.8f;
        p.british.hmGain = 3.0f; p.british.hmFreq = 3000.0f; p.british.hmQ = 1.2f;
        p.british.hfGain = 1.0f; p.british.hfFreq = 8000.0f; p.british.hfBell = false;
        p.british.mode = 0; // E-Series
        p.british.saturation = 0.0f;
        presets.push_back(p);
    }

    // Rock Drums
    {
        Preset p;
        p.name = "Rock Drums";
        p.category = "Drums";
        p.eqType = 2;
        p.hasBritish = true;
        p.british.hpfFreq = 60.0f; p.british.hpfEnabled = true;
        p.british.lfGain = 4.0f; p.british.lfFreq = 100.0f; p.british.lfBell = true;
        p.british.lmGain = -2.0f; p.british.lmFreq = 400.0f; p.british.lmQ = 1.0f;
        p.british.hmGain = 2.0f; p.british.hmFreq = 4000.0f; p.british.hmQ = 1.5f;
        p.british.hfGain = 1.5f; p.british.hfFreq = 10000.0f; p.british.hfBell = false;
        p.british.mode = 1; // G-Series
        presets.push_back(p);
    }

    // Mix Bus Glue
    {
        Preset p;
        p.name = "Mix Bus Glue";
        p.category = "Mix Bus";
        p.eqType = 2;
        p.hasBritish = true;
        p.british.lfGain = 1.0f; p.british.lfFreq = 80.0f; p.british.lfBell = false;
        p.british.hmGain = 0.5f; p.british.hmFreq = 3000.0f; p.british.hmQ = 0.7f;
        p.british.hfGain = 0.5f; p.british.hfFreq = 12000.0f; p.british.hfBell = false;
        p.british.mode = 0;
        p.british.saturation = 30.0f;
        presets.push_back(p);
    }

    // Console Warmth
    {
        Preset p;
        p.name = "Console Warmth";
        p.category = "Creative";
        p.eqType = 2;
        p.hasBritish = true;
        p.british.lfGain = 0.5f; p.british.lfFreq = 100.0f;
        p.british.mode = 0;
        p.british.saturation = 60.0f;
        p.british.inputGain = 3.0f;
        presets.push_back(p);
    }

    // Broadcast Voice (British)
    {
        Preset p;
        p.name = "Broadcast Voice (British)";
        p.category = "Vocals";
        p.eqType = 2;
        p.hasBritish = true;
        p.british.hpfFreq = 120.0f; p.british.hpfEnabled = true;
        p.british.lmGain = -3.0f; p.british.lmFreq = 300.0f; p.british.lmQ = 1.5f;
        p.british.hmGain = 2.0f; p.british.hmFreq = 5000.0f; p.british.hmQ = 1.0f;
        p.british.hfGain = 2.0f; p.british.hfFreq = 8000.0f; p.british.hfBell = false;
        p.british.mode = 1; // G-Series
        presets.push_back(p);
    }

    // ==================== TUBE (eqType=3) ====================

    // Vintage Bass Trick
    {
        Preset p;
        p.name = "Vintage Bass Trick";
        p.category = "Bass";
        p.eqType = 3;
        p.hasTube = true;
        p.tube.lfBoostGain = 6.0f;
        p.tube.lfBoostFreq = 60.0f;
        p.tube.lfAttenGain = 4.0f;
        p.tube.tubeDrive = 0.3f;
        presets.push_back(p);
    }

    // Vintage Air
    {
        Preset p;
        p.name = "Vintage Air";
        p.category = "Mastering";
        p.eqType = 3;
        p.hasTube = true;
        p.tube.hfBoostGain = 4.0f;
        p.tube.hfBoostFreq = 12000.0f;
        p.tube.hfBoostBandwidth = 0.4f;
        p.tube.tubeDrive = 0.3f;
        presets.push_back(p);
    }

    // Warm Vocal (Tube)
    {
        Preset p;
        p.name = "Warm Vocal (Tube)";
        p.category = "Vocals";
        p.eqType = 3;
        p.hasTube = true;
        p.tube.lfBoostGain = 2.0f;
        p.tube.lfBoostFreq = 100.0f;
        p.tube.hfBoostGain = 3.0f;
        p.tube.hfBoostFreq = 8000.0f;
        p.tube.hfBoostBandwidth = 0.5f;
        p.tube.tubeDrive = 0.2f;
        presets.push_back(p);
    }

    // Gentle Master (Tube)
    {
        Preset p;
        p.name = "Gentle Master (Tube)";
        p.category = "Mastering";
        p.eqType = 3;
        p.hasTube = true;
        p.tube.lfBoostGain = 2.0f;
        p.tube.lfBoostFreq = 30.0f;
        p.tube.hfBoostGain = 1.5f;
        p.tube.hfBoostFreq = 16000.0f;
        p.tube.hfBoostBandwidth = 0.6f;
        p.tube.tubeDrive = 0.2f;
        presets.push_back(p);
    }

    // ==================== DYNAMIC EQ (eqType=0 with per-band dynamics) ====================

    // Multiband De-Ess
    {
        Preset p;
        p.name = "Multiband De-Ess";
        p.category = "Surgical";
        p.eqType = 0;
        p.bands[0] = { true, 30.0f, 0.0f, 0.71f, 2 };
        p.bands[1] = { false, 100.0f, 0.0f, 0.71f, 0 };
        p.bands[2] = { false, 300.0f, 0.0f, 0.71f, 0 };
        p.bands[3] = { true, 4000.0f, -2.0f, 2.0f, 0 };
        p.bands[4] = { true, 6000.0f, -4.0f, 3.0f, 0 };
        p.bands[5] = { true, 8000.0f, -3.0f, 2.5f, 0 };
        p.bands[6] = { false, 10000.0f, 0.0f, 0.71f, 0 };
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };
        p.hasPerBandDynamics = true;
        p.bandDynamics[3] = { true, -20.0f, 1.0f, 50.0f, 8.0f, 4.0f };
        p.bandDynamics[4] = { true, -20.0f, 1.0f, 50.0f, 10.0f, 4.0f };
        p.bandDynamics[5] = { true, -22.0f, 1.0f, 50.0f, 8.0f, 4.0f };
        presets.push_back(p);
    }

    // Dynamic Bass Control
    {
        Preset p;
        p.name = "Dynamic Bass Control";
        p.category = "Bass";
        p.eqType = 0;
        p.bands[0] = { true, 30.0f, 0.0f, 0.71f, 2 };
        p.bands[1] = { true, 80.0f, 3.0f, 0.8f, 0 };
        p.bands[7] = { false, 18000.0f, 0.0f, 0.71f, 2 };
        p.hasPerBandDynamics = true;
        p.bandDynamics[1] = { true, -12.0f, 5.0f, 100.0f, 6.0f, 2.0f };
        presets.push_back(p);
    }

    // Resonance Tamer
    {
        Preset p;
        p.name = "Resonance Tamer";
        p.category = "Surgical";
        p.eqType = 0;
        p.bands[2] = { true, 500.0f, -6.0f, 8.0f, 0 };
        p.bands[3] = { true, 1200.0f, -6.0f, 8.0f, 0 };
        p.bands[4] = { true, 3000.0f, -6.0f, 8.0f, 0 };
        p.bands[5] = { true, 5000.0f, -6.0f, 8.0f, 0 };
        p.hasPerBandDynamics = true;
        p.bandDynamics[2] = { true, -15.0f, 2.0f, 80.0f, 12.0f, 8.0f };
        p.bandDynamics[3] = { true, -15.0f, 2.0f, 80.0f, 12.0f, 8.0f };
        p.bandDynamics[4] = { true, -15.0f, 2.0f, 80.0f, 12.0f, 8.0f };
        p.bandDynamics[5] = { true, -15.0f, 2.0f, 80.0f, 12.0f, 8.0f };
        presets.push_back(p);
    }

    // ==================== M/S PRESETS ====================

    // Stereo Width
    {
        Preset p;
        p.name = "Stereo Width";
        p.category = "Creative";
        p.eqType = 0;
        p.processingMode = 4; // Side
        p.bands[6] = { true, 8000.0f, 3.0f, 0.71f, 0 };
        p.bands[5] = { true, 3000.0f, 1.5f, 0.8f, 0 };
        presets.push_back(p);
    }

    // Mid Focus
    {
        Preset p;
        p.name = "Mid Focus";
        p.category = "Creative";
        p.eqType = 0;
        p.processingMode = 3; // Mid
        p.bands[3] = { true, 2000.0f, 2.0f, 0.8f, 0 };
        p.bands[4] = { true, 4000.0f, 2.5f, 1.0f, 0 };
        presets.push_back(p);
    }

    // Telephone
    {
        Preset p;
        p.name = "Telephone";
        p.category = "Creative";
        p.eqType = 0;
        p.bands[0] = { true, 300.0f, 0.0f, 0.71f, 4 };      // HPF steep
        p.bands[1] = { true, 500.0f, -4.0f, 0.71f, 0 };      // Low cut
        p.bands[2] = { true, 1000.0f, 4.0f, 1.5f, 0 };       // Mid boost
        p.bands[3] = { true, 1800.0f, 3.0f, 1.2f, 0 };       // Nasal
        p.bands[4] = { true, 2500.0f, 2.0f, 1.0f, 0 };       // Upper mid
        p.bands[5] = { true, 3000.0f, -3.0f, 0.71f, 0 };     // Rolloff
        p.bands[6] = { true, 3500.0f, -6.0f, 0.71f, 0 };     // Steep rolloff
        p.bands[7] = { true, 3500.0f, 0.0f, 0.71f, 4 };      // LPF steep
        presets.push_back(p);
    }

    // Lo-Fi
    {
        Preset p;
        p.name = "Lo-Fi";
        p.category = "Creative";
        p.eqType = 0;
        p.bands[0] = { true, 200.0f, 0.0f, 0.71f, 2 };      // HPF
        p.bands[1] = { true, 250.0f, 2.0f, 2.5f, 0 };        // Resonant bump at HPF
        p.bands[2] = { true, 600.0f, 0.0f, 0.71f, 0 };       // Flat
        p.bands[3] = { true, 1500.0f, 0.0f, 0.71f, 0 };      // Flat
        p.bands[4] = { true, 3000.0f, -1.0f, 0.71f, 0 };     // Slight dip
        p.bands[5] = { true, 5500.0f, 2.0f, 2.5f, 0 };       // Resonant bump at LPF
        p.bands[6] = { true, 6000.0f, -3.0f, 0.71f, 0 };     // Rolloff
        p.bands[7] = { true, 6000.0f, 0.0f, 0.71f, 3 };      // LPF steep
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

        // Reset per-band FX params to defaults
        if (auto* p = params.getParameter(ParamIDs::bandInvert(i)))
            p->setValueNotifyingHost(0.0f);
        if (auto* p = params.getParameter(ParamIDs::bandPhaseInvert(i)))
            p->setValueNotifyingHost(0.0f);
        if (auto* p = params.getParameter(ParamIDs::bandPan(i)))
            p->setValueNotifyingHost(params.getParameterRange(ParamIDs::bandPan(i)).convertTo0to1(0.0f));
    }

    // Apply British-specific parameters
    if (preset.hasBritish)
    {
        const auto& b = preset.british;
        if (auto* p = params.getParameter(ParamIDs::britishHpfFreq))
            p->setValueNotifyingHost(p->convertTo0to1(b.hpfFreq));
        if (auto* p = params.getParameter(ParamIDs::britishHpfEnabled))
            p->setValueNotifyingHost(b.hpfEnabled ? 1.0f : 0.0f);
        if (auto* p = params.getParameter(ParamIDs::britishLpfFreq))
            p->setValueNotifyingHost(p->convertTo0to1(b.lpfFreq));
        if (auto* p = params.getParameter(ParamIDs::britishLpfEnabled))
            p->setValueNotifyingHost(b.lpfEnabled ? 1.0f : 0.0f);
        if (auto* p = params.getParameter(ParamIDs::britishLfGain))
            p->setValueNotifyingHost(p->convertTo0to1(b.lfGain));
        if (auto* p = params.getParameter(ParamIDs::britishLfFreq))
            p->setValueNotifyingHost(p->convertTo0to1(b.lfFreq));
        if (auto* p = params.getParameter(ParamIDs::britishLfBell))
            p->setValueNotifyingHost(b.lfBell ? 1.0f : 0.0f);
        if (auto* p = params.getParameter(ParamIDs::britishLmGain))
            p->setValueNotifyingHost(p->convertTo0to1(b.lmGain));
        if (auto* p = params.getParameter(ParamIDs::britishLmFreq))
            p->setValueNotifyingHost(p->convertTo0to1(b.lmFreq));
        if (auto* p = params.getParameter(ParamIDs::britishLmQ))
            p->setValueNotifyingHost(p->convertTo0to1(b.lmQ));
        if (auto* p = params.getParameter(ParamIDs::britishHmGain))
            p->setValueNotifyingHost(p->convertTo0to1(b.hmGain));
        if (auto* p = params.getParameter(ParamIDs::britishHmFreq))
            p->setValueNotifyingHost(p->convertTo0to1(b.hmFreq));
        if (auto* p = params.getParameter(ParamIDs::britishHmQ))
            p->setValueNotifyingHost(p->convertTo0to1(b.hmQ));
        if (auto* p = params.getParameter(ParamIDs::britishHfGain))
            p->setValueNotifyingHost(p->convertTo0to1(b.hfGain));
        if (auto* p = params.getParameter(ParamIDs::britishHfFreq))
            p->setValueNotifyingHost(p->convertTo0to1(b.hfFreq));
        if (auto* p = params.getParameter(ParamIDs::britishHfBell))
            p->setValueNotifyingHost(b.hfBell ? 1.0f : 0.0f);
        if (auto* p = params.getParameter(ParamIDs::britishMode))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(b.mode)));
        if (auto* p = params.getParameter(ParamIDs::britishSaturation))
            p->setValueNotifyingHost(p->convertTo0to1(b.saturation));
        if (auto* p = params.getParameter(ParamIDs::britishInputGain))
            p->setValueNotifyingHost(p->convertTo0to1(b.inputGain));
        if (auto* p = params.getParameter(ParamIDs::britishOutputGain))
            p->setValueNotifyingHost(p->convertTo0to1(b.outputGain));
    }

    // Apply Tube-specific parameters
    if (preset.hasTube)
    {
        const auto& t = preset.tube;
        if (auto* p = params.getParameter(ParamIDs::pultecLfBoostGain))
            p->setValueNotifyingHost(p->convertTo0to1(t.lfBoostGain));
        if (auto* p = params.getParameter(ParamIDs::pultecLfBoostFreq))
            p->setValueNotifyingHost(p->convertTo0to1(t.lfBoostFreq));
        if (auto* p = params.getParameter(ParamIDs::pultecLfAttenGain))
            p->setValueNotifyingHost(p->convertTo0to1(t.lfAttenGain));
        if (auto* p = params.getParameter(ParamIDs::pultecHfBoostGain))
            p->setValueNotifyingHost(p->convertTo0to1(t.hfBoostGain));
        if (auto* p = params.getParameter(ParamIDs::pultecHfBoostFreq))
            p->setValueNotifyingHost(p->convertTo0to1(t.hfBoostFreq));
        if (auto* p = params.getParameter(ParamIDs::pultecHfBoostBandwidth))
            p->setValueNotifyingHost(p->convertTo0to1(t.hfBoostBandwidth));
        if (auto* p = params.getParameter(ParamIDs::pultecHfAttenGain))
            p->setValueNotifyingHost(p->convertTo0to1(t.hfAttenGain));
        if (auto* p = params.getParameter(ParamIDs::pultecHfAttenFreq))
            p->setValueNotifyingHost(p->convertTo0to1(t.hfAttenFreq));
        if (auto* p = params.getParameter(ParamIDs::pultecInputGain))
            p->setValueNotifyingHost(p->convertTo0to1(t.inputGain));
        if (auto* p = params.getParameter(ParamIDs::pultecOutputGain))
            p->setValueNotifyingHost(p->convertTo0to1(t.outputGain));
        if (auto* p = params.getParameter(ParamIDs::pultecTubeDrive))
            p->setValueNotifyingHost(p->convertTo0to1(t.tubeDrive));
        if (auto* p = params.getParameter(ParamIDs::pultecMidEnabled))
            p->setValueNotifyingHost(t.midEnabled ? 1.0f : 0.0f);
        if (auto* p = params.getParameter(ParamIDs::pultecMidDipFreq))
            p->setValueNotifyingHost(p->convertTo0to1(t.midDipFreq));
        if (auto* p = params.getParameter(ParamIDs::pultecMidDip))
            p->setValueNotifyingHost(p->convertTo0to1(t.midDip));
    }

    // Apply per-band dynamics
    if (preset.hasPerBandDynamics)
    {
        for (int i = 0; i < 8; ++i)
        {
            const auto& bd = preset.bandDynamics[static_cast<size_t>(i)];
            int bandNum = i + 1;
            if (auto* p = params.getParameter(ParamIDs::bandDynEnabled(bandNum)))
                p->setValueNotifyingHost(bd.enabled ? 1.0f : 0.0f);
            if (bd.enabled)
            {
                if (auto* p = params.getParameter(ParamIDs::bandDynThreshold(bandNum)))
                    p->setValueNotifyingHost(p->convertTo0to1(bd.threshold));
                if (auto* p = params.getParameter(ParamIDs::bandDynAttack(bandNum)))
                    p->setValueNotifyingHost(p->convertTo0to1(bd.attack));
                if (auto* p = params.getParameter(ParamIDs::bandDynRelease(bandNum)))
                    p->setValueNotifyingHost(p->convertTo0to1(bd.release));
                if (auto* p = params.getParameter(ParamIDs::bandDynRange(bandNum)))
                    p->setValueNotifyingHost(p->convertTo0to1(bd.range));
                if (auto* p = params.getParameter(ParamIDs::bandDynRatio(bandNum)))
                    p->setValueNotifyingHost(p->convertTo0to1(bd.ratio));
            }
        }
    }
}

} // namespace MultiQPresets

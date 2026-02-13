#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

/**
 * Factory presets for TapeMachine plugin organized by category.
 * These are professional starting points for various tape emulation scenarios.
 */
namespace TapeMachinePresets
{

//==============================================================================
struct Preset
{
    juce::String name;
    juce::String category;

    // Machine settings
    int tapeMachine = 0;     // 0=Swiss800 (Studer), 1=Classic102 (Ampex)
    int tapeSpeed = 1;       // 0=7.5 IPS, 1=15 IPS, 2=30 IPS
    int tapeType = 0;        // 0=Type456, 1=GP9, 2=Type911, 3=Type250

    // Gain and saturation
    float inputGain = 0.0f;  // -12 to +12 dB (drives saturation)
    float outputGain = 0.0f; // -12 to +12 dB
    float bias = 50.0f;      // 0-100%
    bool autoComp = true;    // Auto gain compensation

    // Tone shaping
    float highpassFreq = 20.0f;   // 20-500 Hz
    float lowpassFreq = 20000.0f; // 1000-20000 Hz

    // Character
    float wowAmount = 7.0f;      // 0-100%
    float flutterAmount = 3.0f;  // 0-100%
    float noiseAmount = 0.0f;    // 0-100%
    bool noiseEnabled = false;
};

//==============================================================================
// Category definitions
inline const juce::StringArray Categories = {
    "Subtle",
    "Warm",
    "Character",
    "Lo-Fi",
    "Mastering"
};

//==============================================================================
// Factory Presets
inline std::vector<Preset> getFactoryPresets()
{
    std::vector<Preset> presets;

    // ==================== SUBTLE ====================
    // Light tape coloring for transparent enhancement

    presets.push_back({
        "Gentle Warmth",          // name
        "Subtle",                 // category
        0,                        // tapeMachine: Swiss800 (Studer - cleaner)
        2,                        // tapeSpeed: 30 IPS (cleanest)
        3,                        // tapeType: Type250 (professional)
        2.0f,                     // inputGain: Light drive
        0.0f,                     // outputGain
        50.0f,                    // bias: Neutral
        true,                     // autoComp
        20.0f,                    // highpassFreq
        20000.0f,                 // lowpassFreq
        3.0f,                     // wowAmount: Minimal
        1.0f,                     // flutterAmount: Minimal
        0.0f,                     // noiseAmount
        false                     // noiseEnabled
    });

    presets.push_back({
        "Transparent Glue",
        "Subtle",
        0,                        // tapeMachine: Swiss800 (clean precision)
        2,                        // tapeSpeed: 30 IPS
        1,                        // tapeType: GP9 (modern formulation)
        3.0f,                     // inputGain: Light saturation
        0.0f,
        55.0f,                    // bias: Slightly hot
        true,
        20.0f,
        18000.0f,                 // lowpassFreq: Gentle HF rolloff
        2.0f,
        1.0f,
        0.0f,
        false
    });

    presets.push_back({
        "Mastering Touch",
        "Subtle",
        0,                        // Swiss800
        2,                        // 30 IPS
        3,                        // Type250
        1.0f,                     // inputGain: Very light
        0.0f,
        50.0f,
        true,
        20.0f,
        20000.0f,
        1.0f,                     // wowAmount: Barely there
        0.5f,                     // flutterAmount: Barely there
        0.0f,
        false
    });

    // ==================== WARM ====================
    // Classic analog warmth and saturation

    presets.push_back({
        "Classic Analog",
        "Warm",
        1,                        // tapeMachine: Classic102 (Ampex - warmer)
        1,                        // tapeSpeed: 15 IPS (classic)
        0,                        // tapeType: Type456 (high output, warm)
        5.0f,                     // inputGain: Moderate drive
        0.0f,
        50.0f,
        true,
        30.0f,                    // highpassFreq: Tighten low end
        16000.0f,                 // lowpassFreq: Smooth highs
        7.0f,
        3.0f,
        5.0f,
        false
    });

    presets.push_back({
        "Vintage Warmth",
        "Warm",
        1,                        // Classic102
        0,                        // 7.5 IPS (more saturation, more wow/flutter character)
        0,                        // Type456
        6.0f,                     // inputGain: Push it
        0.0f,
        45.0f,                    // bias: Slightly under-biased for more harmonics
        true,
        40.0f,
        14000.0f,
        10.0f,                    // wowAmount: More pronounced
        5.0f,
        8.0f,
        false
    });

    presets.push_back({
        "Tube Console",
        "Warm",
        1,                        // Classic102 (Ampex warmth)
        1,                        // 15 IPS
        2,                        // Type911 (German precision with warmth)
        7.0f,                     // inputGain: Solid drive
        0.0f,
        48.0f,
        true,
        25.0f,
        15000.0f,
        5.0f,
        2.0f,
        3.0f,
        false
    });

    // ==================== CHARACTER ====================
    // Distinctive tape sound for creative effect

    presets.push_back({
        "70s Rock",
        "Character",
        1,                        // Classic102 (Ampex character)
        1,                        // 15 IPS
        0,                        // Type456
        8.0f,                     // inputGain: Drive hard
        0.0f,
        42.0f,                    // bias: Under-biased for grit
        true,
        50.0f,                    // highpassFreq: Tighter bass
        12000.0f,                 // lowpassFreq: Darker tone
        12.0f,                    // wowAmount: Noticeable
        6.0f,
        10.0f,
        true                      // noiseEnabled: Part of the vibe
    });

    presets.push_back({
        "Tape Saturation",
        "Character",
        1,                        // Classic102
        1,                        // 15 IPS
        0,                        // Type456
        10.0f,                    // inputGain: Heavy drive
        0.0f,
        40.0f,                    // bias: Under-biased
        true,
        30.0f,
        14000.0f,
        8.0f,
        4.0f,
        5.0f,
        false
    });

    presets.push_back({
        "Cassette Deck",
        "Character",
        1,                        // Classic102
        0,                        // 7.5 IPS (slower = more artifacts)
        2,                        // Type911
        6.0f,
        0.0f,
        55.0f,
        true,
        60.0f,                    // highpassFreq: Less bass
        10000.0f,                 // lowpassFreq: Rolled off highs
        15.0f,                    // wowAmount: Cassette wobble
        8.0f,                     // flutterAmount: More flutter
        15.0f,
        true
    });

    // ==================== LO-FI ====================
    // Degraded, vintage, lo-fi aesthetics

    presets.push_back({
        "Lo-Fi Warble",
        "Lo-Fi",
        1,                        // Classic102
        0,                        // 7.5 IPS
        0,                        // Type456
        8.0f,
        0.0f,
        38.0f,                    // bias: Very under-biased
        true,
        80.0f,                    // highpassFreq: Thin
        8000.0f,                  // lowpassFreq: Very dark
        25.0f,                    // wowAmount: Heavy wobble
        12.0f,                    // flutterAmount: Heavy flutter
        20.0f,
        true
    });

    presets.push_back({
        "Worn Tape",
        "Lo-Fi",
        1,                        // Classic102
        0,                        // 7.5 IPS
        2,                        // Type911
        5.0f,
        0.0f,
        35.0f,
        true,
        100.0f,                   // highpassFreq: Very thin
        6000.0f,                  // lowpassFreq: Very dark
        30.0f,                    // wowAmount: Extreme
        15.0f,                    // flutterAmount: Extreme
        30.0f,
        true
    });

    presets.push_back({
        "Dusty Reel",
        "Lo-Fi",
        1,                        // Classic102
        0,                        // 7.5 IPS
        0,                        // Type456
        4.0f,
        0.0f,
        42.0f,
        true,
        70.0f,
        9000.0f,
        20.0f,
        10.0f,
        40.0f,                    // noiseAmount: Lots of tape hiss
        true
    });

    // ==================== MASTERING ====================
    // Subtle enhancements for final mix

    presets.push_back({
        "Master Bus Glue",
        "Mastering",
        0,                        // Swiss800 (precision)
        2,                        // 30 IPS (cleanest)
        3,                        // Type250 (professional)
        2.0f,                     // inputGain: Very light
        0.0f,
        52.0f,
        true,
        20.0f,
        20000.0f,
        2.0f,                     // wowAmount: Barely perceptible
        1.0f,
        0.0f,
        false
    });

    presets.push_back({
        "Analog Sheen",
        "Mastering",
        0,                        // Swiss800 (precision for mastering)
        2,                        // 30 IPS
        1,                        // GP9
        3.0f,
        0.0f,
        50.0f,
        true,
        20.0f,
        18000.0f,                 // Gentle HF taming
        3.0f,
        1.5f,
        0.0f,
        false
    });

    presets.push_back({
        "Vintage Master",
        "Mastering",
        0,                        // Swiss800
        1,                        // 15 IPS (more character)
        0,                        // Type456
        4.0f,
        0.0f,
        48.0f,
        true,
        25.0f,
        16000.0f,
        5.0f,
        2.0f,
        2.0f,
        false
    });

    return presets;
}

//==============================================================================
// Helper to get presets by category
inline std::vector<Preset> getPresetsByCategory(const juce::String& category)
{
    std::vector<Preset> result;
    for (const auto& preset : getFactoryPresets())
    {
        if (preset.category == category)
            result.push_back(preset);
    }
    return result;
}

//==============================================================================
// Apply preset to APVTS
inline void applyPreset(const Preset& preset, juce::AudioProcessorValueTreeState& params)
{
    // Machine settings
    if (auto* p = params.getParameter("tapeMachine"))
        p->setValueNotifyingHost(params.getParameterRange("tapeMachine").convertTo0to1(static_cast<float>(preset.tapeMachine)));

    if (auto* p = params.getParameter("tapeSpeed"))
        p->setValueNotifyingHost(params.getParameterRange("tapeSpeed").convertTo0to1(static_cast<float>(preset.tapeSpeed)));

    if (auto* p = params.getParameter("tapeType"))
        p->setValueNotifyingHost(params.getParameterRange("tapeType").convertTo0to1(static_cast<float>(preset.tapeType)));

    // Gain
    if (auto* p = params.getParameter("inputGain"))
        p->setValueNotifyingHost(params.getParameterRange("inputGain").convertTo0to1(preset.inputGain));

    if (auto* p = params.getParameter("outputGain"))
        p->setValueNotifyingHost(params.getParameterRange("outputGain").convertTo0to1(preset.outputGain));

    if (auto* p = params.getParameter("bias"))
        p->setValueNotifyingHost(params.getParameterRange("bias").convertTo0to1(preset.bias));

    if (auto* p = params.getParameter("autoComp"))
        p->setValueNotifyingHost(preset.autoComp ? 1.0f : 0.0f);

    // Tone
    if (auto* p = params.getParameter("highpassFreq"))
        p->setValueNotifyingHost(params.getParameterRange("highpassFreq").convertTo0to1(preset.highpassFreq));

    if (auto* p = params.getParameter("lowpassFreq"))
        p->setValueNotifyingHost(params.getParameterRange("lowpassFreq").convertTo0to1(preset.lowpassFreq));

    // Character
    if (auto* p = params.getParameter("wowAmount"))
        p->setValueNotifyingHost(params.getParameterRange("wowAmount").convertTo0to1(preset.wowAmount));

    if (auto* p = params.getParameter("flutterAmount"))
        p->setValueNotifyingHost(params.getParameterRange("flutterAmount").convertTo0to1(preset.flutterAmount));

    if (auto* p = params.getParameter("noiseAmount"))
        p->setValueNotifyingHost(params.getParameterRange("noiseAmount").convertTo0to1(preset.noiseAmount));

    if (auto* p = params.getParameter("noiseEnabled"))
        p->setValueNotifyingHost(preset.noiseEnabled ? 1.0f : 0.0f);
}

} // namespace TapeMachinePresets

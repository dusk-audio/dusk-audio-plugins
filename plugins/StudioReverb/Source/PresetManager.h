/*
  ==============================================================================

    PresetManager.h
    Dragonfly Reverb Preset Management

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class PresetManager
{
public:
    struct Preset
    {
        juce::String name;
        std::map<juce::String, float> parameters;
    };

    struct PresetBank
    {
        juce::String name;
        std::vector<Preset> presets;
    };

    PresetManager();

    // Get presets for a specific reverb type
    std::vector<PresetBank> getPresetsForAlgorithm(int algorithmIndex);

    // Get a specific preset
    Preset getPreset(int algorithmIndex, const juce::String& presetName);

    // Get all preset names for dropdown
    juce::StringArray getPresetNames(int algorithmIndex);

private:
    void initializeHallPresets();
    void initializeRoomPresets();
    void initializePlatePresets();
    void initializeEarlyPresets();

    // Presets organized by algorithm type
    std::array<std::vector<PresetBank>, 4> presetsByAlgorithm;
};
/*
  ==============================================================================

    PresetManager.h - Factory presets and preset management

    Provides a collection of carefully crafted presets inspired by
    classic records and vintage hardware units.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <map>

class PresetManager
{
public:
    PresetManager();
    ~PresetManager() = default;

    // Preset structure
    struct Preset
    {
        juce::String name;
        juce::String category;
        juce::String description;
        std::map<juce::String, float> parameters;
    };

    // Categories
    enum PresetCategory
    {
        Drums = 0,
        Vocals,
        Instruments,
        Ambiences,
        Halls,
        Rooms,
        Plates,
        Chambers,
        Nonlinear,
        Special,
        Vintage,
        Modern,
        NumCategories
    };

    // Initialize factory presets
    void initializeFactoryPresets();

    // Preset management
    int getNumPresets() const { return static_cast<int>(presets.size()); }
    int getNumPresetsInCategory(PresetCategory category) const;

    const Preset* getPreset(int index) const;
    const Preset* getPresetByName(const juce::String& name) const;
    std::vector<const Preset*> getPresetsInCategory(PresetCategory category) const;

    // Apply preset to parameter tree
    void applyPreset(const Preset* preset, juce::AudioProcessorValueTreeState& apvts);
    void applyPresetByIndex(int index, juce::AudioProcessorValueTreeState& apvts);

    // Save/Load user presets
    void saveUserPreset(const juce::String& name, const juce::AudioProcessorValueTreeState& apvts);
    void loadUserPreset(const juce::File& file, juce::AudioProcessorValueTreeState& apvts);

    // Get category name
    static juce::String getCategoryName(PresetCategory category);

private:
    std::vector<Preset> presets;
    std::vector<Preset> userPresets;

    // Factory preset definitions
    void createDrumPresets();
    void createVocalPresets();
    void createInstrumentPresets();
    void createAmbiencePresets();
    void createHallPresets();
    void createRoomPresets();
    void createPlatePresets();
    void createChamberPresets();
    void createNonlinearPresets();
    void createSpecialPresets();
    void createVintagePresets();
    void createModernPresets();

    // Helper to create a preset
    void addPreset(const juce::String& name, PresetCategory category,
                  const juce::String& description,
                  std::initializer_list<std::pair<const char*, float>> params);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
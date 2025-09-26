/*
  ==============================================================================

    PresetManager.cpp
    Dragonfly Reverb Preset Management

  ==============================================================================
*/

#include "PresetManager.h"

PresetManager::PresetManager()
{
    DBG("PresetManager constructor - Initializing presets");
    // Initialize in the correct order matching the reverb type combo box:
    // 0=Room, 1=Hall, 2=Plate, 3=Early Reflections
    initializeRoomPresets();   // Index 0
    initializeHallPresets();   // Index 1
    initializePlatePresets();  // Index 2
    initializeEarlyPresets();  // Index 3

    DBG("PresetManager constructor - Initialized with " << presetsByAlgorithm.size() << " algorithms:");
    for (size_t i = 0; i < presetsByAlgorithm.size(); ++i)
    {
        DBG("  Algorithm " << i << ": " << presetsByAlgorithm[i].size() << " banks");
        if (!presetsByAlgorithm[i].empty())
        {
            DBG("    First bank name: " << presetsByAlgorithm[i][0].name);
            if (!presetsByAlgorithm[i][0].presets.empty())
            {
                DBG("    First preset in first bank: " << presetsByAlgorithm[i][0].presets[0].name);
            }
        }
    }
}

std::vector<PresetManager::PresetBank> PresetManager::getPresetsForAlgorithm(int algorithmIndex)
{
    DBG("PresetManager::getPresetsForAlgorithm - Algorithm Index: " << algorithmIndex);
    DBG("PresetManager::getPresetsForAlgorithm - presetsByAlgorithm size: " << presetsByAlgorithm.size());

    if (algorithmIndex >= 0 && algorithmIndex < 4)
    {
        DBG("PresetManager::getPresetsForAlgorithm - Returning " << presetsByAlgorithm[algorithmIndex].size() << " banks");
        for (size_t i = 0; i < presetsByAlgorithm[algorithmIndex].size(); ++i)
        {
            DBG("  Bank " << i << ": " << presetsByAlgorithm[algorithmIndex][i].name);
        }
        return presetsByAlgorithm[algorithmIndex];
    }
    DBG("PresetManager::getPresetsForAlgorithm - Invalid index, returning empty");
    return {};
}

PresetManager::Preset PresetManager::getPreset(int algorithmIndex, const juce::String& presetName)
{
    auto banks = getPresetsForAlgorithm(algorithmIndex);
    for (const auto& bank : banks)
    {
        for (const auto& preset : bank.presets)
        {
            if (preset.name == presetName)
                return preset;
        }
    }
    return {};
}

juce::StringArray PresetManager::getPresetNames(int algorithmIndex)
{
    DBG("PresetManager::getPresetNames called with algorithmIndex: " << algorithmIndex);
    juce::StringArray names;
    names.add("-- Select Preset --");

    auto banks = getPresetsForAlgorithm(algorithmIndex);
    DBG("PresetManager::getPresetNames - Got " << banks.size() << " banks");

    for (const auto& bank : banks)
    {
        DBG("  Adding presets from bank: " << bank.name);
        for (const auto& preset : bank.presets)
        {
            DBG("    Adding preset: " << preset.name);
            names.add(preset.name);
        }
    }
    DBG("PresetManager::getPresetNames - Total names (including header): " << names.size());
    return names;
}

void PresetManager::initializeRoomPresets()
{
    PresetBank smallRooms;
    smallRooms.name = "Small Rooms";

    // Small Bright Room
    smallRooms.presets.push_back({
        "Small Bright Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 12.0f}, {"width", 90.0f}, {"preDelay", 4.0f}, {"decay", 0.4f},
            {"diffuse", 70.0f}, {"spin", 0.6f}, {"wander", 10.0f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Small Clear Room
    smallRooms.presets.push_back({
        "Small Clear Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 14.0f}, {"width", 100.0f}, {"preDelay", 4.0f}, {"decay", 0.5f},
            {"diffuse", 75.0f}, {"spin", 0.8f}, {"wander", 15.0f},
            {"highCut", 14000.0f}, {"lowCut", 4.0f}
        }
    });

    // Small Dark Room
    smallRooms.presets.push_back({
        "Small Dark Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 15.0f}, {"width", 80.0f}, {"preDelay", 8.0f}, {"decay", 0.6f},
            {"diffuse", 80.0f}, {"spin", 1.0f}, {"wander", 20.0f},
            {"highCut", 8000.0f}, {"lowCut", 4.0f}
        }
    });

    PresetBank mediumRooms;
    mediumRooms.name = "Medium Rooms";

    // Bright Room
    mediumRooms.presets.push_back({
        "Bright Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 15.0f}, {"width", 90.0f}, {"preDelay", 4.0f}, {"decay", 0.6f},
            {"diffuse", 90.0f}, {"spin", 1.0f}, {"wander", 25.0f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Clear Room
    mediumRooms.presets.push_back({
        "Clear Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 15.0f}, {"width", 90.0f}, {"preDelay", 4.0f}, {"decay", 0.6f},
            {"diffuse", 90.0f}, {"spin", 1.0f}, {"wander", 25.0f},
            {"highCut", 13000.0f}, {"lowCut", 4.0f}
        }
    });

    // Dark Room
    mediumRooms.presets.push_back({
        "Dark Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 16.0f}, {"width", 90.0f}, {"preDelay", 4.0f}, {"decay", 0.7f},
            {"diffuse", 50.0f}, {"spin", 1.0f}, {"wander", 25.0f},
            {"highCut", 7300.0f}, {"lowCut", 4.0f}
        }
    });

    // Small Chamber
    mediumRooms.presets.push_back({
        "Small Chamber",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 18.0f}, {"width", 80.0f}, {"preDelay", 10.0f}, {"decay", 1.4f},
            {"diffuse", 80.0f}, {"spin", 2.2f}, {"wander", 14.0f},
            {"highCut", 8500.0f}, {"lowCut", 40.0f}
        }
    });

    PresetBank largeRooms;
    largeRooms.name = "Large Rooms";

    // Recording Studio
    largeRooms.presets.push_back({
        "Recording Studio",
        {
            {"dryLevel", 85.0f}, {"earlyLevel", 8.0f}, {"earlySend", 15.0f}, {"lateLevel", 15.0f},
            {"size", 22.0f}, {"width", 85.0f}, {"preDelay", 5.0f}, {"decay", 0.5f},
            {"diffuse", 70.0f}, {"spin", 1.2f}, {"wander", 20.0f},
            {"highCut", 12000.0f}, {"lowCut", 50.0f}
        }
    });

    // Vocal Booth
    largeRooms.presets.push_back({
        "Vocal Booth",
        {
            {"dryLevel", 90.0f}, {"earlyLevel", 5.0f}, {"earlySend", 10.0f}, {"lateLevel", 10.0f},
            {"size", 12.0f}, {"width", 60.0f}, {"preDelay", 2.0f}, {"decay", 0.2f},
            {"diffuse", 50.0f}, {"spin", 0.8f}, {"wander", 15.0f},
            {"highCut", 10000.0f}, {"lowCut", 100.0f}
        }
    });

    // Large Bright Room
    largeRooms.presets.push_back({
        "Large Bright Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 25.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 0.6f},
            {"diffuse", 80.0f}, {"spin", 1.6f}, {"wander", 30.0f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Large Clear Room
    largeRooms.presets.push_back({
        "Large Clear Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 28.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 0.7f},
            {"diffuse", 80.0f}, {"spin", 1.6f}, {"wander", 20.0f},
            {"highCut", 12000.0f}, {"lowCut", 4.0f}
        }
    });

    // Large Dark Room
    largeRooms.presets.push_back({
        "Large Dark Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 30.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 0.8f},
            {"diffuse", 80.0f}, {"spin", 1.6f}, {"wander", 20.0f},
            {"highCut", 4000.0f}, {"lowCut", 4.0f}
        }
    });

    // Index 0 = Room algorithm
    presetsByAlgorithm[0] = {smallRooms, mediumRooms, largeRooms};
}

void PresetManager::initializeHallPresets()
{
    PresetBank smallHalls;
    smallHalls.name = "Small Halls";

    // Small Bright Hall
    smallHalls.presets.push_back({
        "Small Bright Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 24.0f}, {"width", 80.0f}, {"preDelay", 12.0f}, {"decay", 1.3f},
            {"diffuse", 90.0f}, {"spin", 2.5f}, {"wander", 15.0f},
            {"highCut", 11200.0f}, {"lowCut", 4.0f},
            {"lowCross", 400.0f}, {"highCross", 6250.0f}, {"lowMult", 1.1f}, {"highMult", 0.75f}
        }
    });

    // Small Clear Hall
    smallHalls.presets.push_back({
        "Small Clear Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 24.0f}, {"width", 100.0f}, {"preDelay", 4.0f}, {"decay", 1.3f},
            {"diffuse", 90.0f}, {"spin", 3.3f}, {"wander", 18.0f},
            {"highCut", 7600.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 5500.0f}, {"lowMult", 1.3f}, {"highMult", 0.5f}
        }
    });

    // Small Dark Hall
    smallHalls.presets.push_back({
        "Small Dark Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 24.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 1.5f},
            {"diffuse", 60.0f}, {"spin", 2.5f}, {"wander", 12.0f},
            {"highCut", 5800.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 4000.0f}, {"lowMult", 1.5f}, {"highMult", 0.35f}
        }
    });

    // Gig Venue
    smallHalls.presets.push_back({
        "Gig Venue",
        {
            {"dryLevel", 75.0f}, {"earlyLevel", 15.0f}, {"earlySend", 25.0f}, {"lateLevel", 25.0f},
            {"size", 22.0f}, {"width", 90.0f}, {"preDelay", 8.0f}, {"decay", 1.2f},
            {"diffuse", 85.0f}, {"spin", 2.0f}, {"wander", 22.0f},
            {"highCut", 9000.0f}, {"lowCut", 80.0f},
            {"lowCross", 450.0f}, {"highCross", 5000.0f}, {"lowMult", 1.2f}, {"highMult", 0.6f}
        }
    });

    // Jazz Club
    smallHalls.presets.push_back({
        "Jazz Club",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 12.0f}, {"earlySend", 22.0f}, {"lateLevel", 22.0f},
            {"size", 20.0f}, {"width", 85.0f}, {"preDelay", 6.0f}, {"decay", 1.0f},
            {"diffuse", 75.0f}, {"spin", 1.8f}, {"wander", 16.0f},
            {"highCut", 7500.0f}, {"lowCut", 60.0f},
            {"lowCross", 400.0f}, {"highCross", 4500.0f}, {"lowMult", 1.3f}, {"highMult", 0.4f}
        }
    });

    // Small Chamber
    smallHalls.presets.push_back({
        "Small Chamber",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 18.0f}, {"width", 80.0f}, {"preDelay", 10.0f}, {"decay", 1.4f},
            {"diffuse", 80.0f}, {"spin", 2.2f}, {"wander", 14.0f},
            {"highCut", 8500.0f}, {"lowCut", 40.0f},
            {"lowCross", 400.0f}, {"highCross", 5200.0f}, {"lowMult", 1.1f}, {"highMult", 0.45f}
        }
    });

    PresetBank mediumHalls;
    mediumHalls.name = "Medium Halls";

    // Medium Bright Hall
    mediumHalls.presets.push_back({
        "Medium Bright Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 30.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 1.8f},
            {"diffuse", 90.0f}, {"spin", 3.0f}, {"wander", 16.0f},
            {"highCut", 13000.0f}, {"lowCut", 4.0f},
            {"lowCross", 400.0f}, {"highCross", 6000.0f}, {"lowMult", 1.2f}, {"highMult", 0.7f}
        }
    });

    // Medium Clear Hall
    mediumHalls.presets.push_back({
        "Medium Clear Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 30.0f}, {"width", 100.0f}, {"preDelay", 8.0f}, {"decay", 2.0f},
            {"diffuse", 90.0f}, {"spin", 3.5f}, {"wander", 20.0f},
            {"highCut", 9000.0f}, {"lowCut", 4.0f},
            {"lowCross", 450.0f}, {"highCross", 5000.0f}, {"lowMult", 1.3f}, {"highMult", 0.5f}
        }
    });

    // Medium Dark Hall
    mediumHalls.presets.push_back({
        "Medium Dark Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 30.0f}, {"width", 90.0f}, {"preDelay", 16.0f}, {"decay", 2.2f},
            {"diffuse", 70.0f}, {"spin", 3.0f}, {"wander", 18.0f},
            {"highCut", 5000.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 3500.0f}, {"lowMult", 1.5f}, {"highMult", 0.3f}
        }
    });

    // Concert Hall
    mediumHalls.presets.push_back({
        "Concert Hall",
        {
            {"dryLevel", 75.0f}, {"earlyLevel", 15.0f}, {"earlySend", 25.0f}, {"lateLevel", 25.0f},
            {"size", 32.0f}, {"width", 100.0f}, {"preDelay", 18.0f}, {"decay", 2.3f},
            {"diffuse", 90.0f}, {"spin", 3.2f}, {"wander", 20.0f},
            {"highCut", 11000.0f}, {"lowCut", 60.0f},
            {"lowCross", 350.0f}, {"highCross", 5800.0f}, {"lowMult", 1.3f}, {"highMult", 0.65f}
        }
    });

    // Opera House
    mediumHalls.presets.push_back({
        "Opera House",
        {
            {"dryLevel", 70.0f}, {"earlyLevel", 18.0f}, {"earlySend", 28.0f}, {"lateLevel", 30.0f},
            {"size", 35.0f}, {"width", 100.0f}, {"preDelay", 22.0f}, {"decay", 2.8f},
            {"diffuse", 85.0f}, {"spin", 2.8f}, {"wander", 18.0f},
            {"highCut", 9500.0f}, {"lowCut", 80.0f},
            {"lowCross", 320.0f}, {"highCross", 5200.0f}, {"lowMult", 1.4f}, {"highMult", 0.55f}
        }
    });

    PresetBank largeHalls;
    largeHalls.name = "Large Halls";

    // Large Bright Hall
    largeHalls.presets.push_back({
        "Large Bright Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 40.0f}, {"width", 100.0f}, {"preDelay", 16.0f}, {"decay", 2.5f},
            {"diffuse", 90.0f}, {"spin", 4.0f}, {"wander", 20.0f},
            {"highCut", 15000.0f}, {"lowCut", 4.0f},
            {"lowCross", 350.0f}, {"highCross", 6500.0f}, {"lowMult", 1.2f}, {"highMult", 0.75f}
        }
    });

    // Large Clear Hall
    largeHalls.presets.push_back({
        "Large Clear Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 40.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 3.0f},
            {"diffuse", 90.0f}, {"spin", 4.5f}, {"wander", 25.0f},
            {"highCut", 10000.0f}, {"lowCut", 4.0f},
            {"lowCross", 400.0f}, {"highCross", 5500.0f}, {"lowMult", 1.4f}, {"highMult", 0.5f}
        }
    });

    // Large Dark Hall
    largeHalls.presets.push_back({
        "Large Dark Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 40.0f}, {"width", 100.0f}, {"preDelay", 20.0f}, {"decay", 3.5f},
            {"diffuse", 80.0f}, {"spin", 3.5f}, {"wander", 22.0f},
            {"highCut", 4500.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 3000.0f}, {"lowMult", 1.6f}, {"highMult", 0.25f}
        }
    });

    // Grand Cathedral
    largeHalls.presets.push_back({
        "Grand Cathedral",
        {
            {"dryLevel", 50.0f}, {"earlyLevel", 25.0f}, {"earlySend", 35.0f}, {"lateLevel", 45.0f},
            {"size", 55.0f}, {"width", 100.0f}, {"preDelay", 35.0f}, {"decay", 7.0f},
            {"diffuse", 90.0f}, {"spin", 1.5f}, {"wander", 10.0f},
            {"highCut", 5500.0f}, {"lowCut", 100.0f},
            {"lowCross", 200.0f}, {"highCross", 3000.0f}, {"lowMult", 2.0f}, {"highMult", 0.2f}
        }
    });


    PresetBank churches;
    churches.name = "Churches";

    // Cathedral
    churches.presets.push_back({
        "Cathedral",
        {
            {"dryLevel", 60.0f}, {"earlyLevel", 20.0f}, {"earlySend", 30.0f}, {"lateLevel", 40.0f},
            {"size", 50.0f}, {"width", 100.0f}, {"preDelay", 30.0f}, {"decay", 6.0f},
            {"diffuse", 90.0f}, {"spin", 2.0f}, {"wander", 12.0f},
            {"highCut", 6000.0f}, {"lowCut", 80.0f},
            {"lowCross", 250.0f}, {"highCross", 3500.0f}, {"lowMult", 1.8f}, {"highMult", 0.3f}
        }
    });

    // Index 1 = Hall algorithm
    presetsByAlgorithm[1] = {smallHalls, mediumHalls, largeHalls, churches};
}

void PresetManager::initializePlatePresets()
{
    PresetBank plates;
    plates.name = "Classic Plates";

    // Abrupt Plate - nrevb algorithm
    plates.presets.push_back({
        "Abrupt Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 20.0f}, {"decay", 0.2f},
            {"lowCut", 50.0f}, {"highCut", 10000.0f}, {"dampen", 7000.0f}
        }
    });

    // Bright Plate - nrevb algorithm
    plates.presets.push_back({
        "Bright Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 0.0f}, {"decay", 0.4f},
            {"lowCut", 200.0f}, {"highCut", 16000.0f}, {"dampen", 13000.0f}
        }
    });

    // Clear Plate - nrevb algorithm
    plates.presets.push_back({
        "Clear Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 0.0f}, {"decay", 0.6f},
            {"lowCut", 100.0f}, {"highCut", 13000.0f}, {"dampen", 7000.0f}
        }
    });

    // Dark Plate - nrevb algorithm
    plates.presets.push_back({
        "Dark Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 0.0f}, {"decay", 0.8f},
            {"lowCut", 50.0f}, {"highCut", 7000.0f}, {"dampen", 4000.0f}
        }
    });

    // Foil Tray - nrev algorithm (Simple)
    plates.presets.push_back({
        "Foil Tray",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 50.0f}, {"preDelay", 0.0f}, {"decay", 0.3f},
            {"lowCut", 200.0f}, {"highCut", 16000.0f}, {"dampen", 13000.0f}
        }
    });

    // Metal Roof - nrev algorithm (Simple)
    plates.presets.push_back({
        "Metal Roof",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 120.0f}, {"preDelay", 20.0f}, {"decay", 0.5f},
            {"lowCut", 100.0f}, {"highCut", 13000.0f}, {"dampen", 10000.0f}
        }
    });

    // Narrow Tank - strev algorithm (Tank)
    plates.presets.push_back({
        "Narrow Tank",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 60.0f}, {"preDelay", 10.0f}, {"decay", 0.6f},
            {"lowCut", 50.0f}, {"highCut", 10000.0f}, {"dampen", 7000.0f}
        }
    });

    // Phat Tank - strev algorithm (Tank)
    plates.presets.push_back({
        "Phat Tank",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 150.0f}, {"preDelay", 10.0f}, {"decay", 1.0f},
            {"lowCut", 50.0f}, {"highCut", 10000.0f}, {"dampen", 4000.0f}
        }
    });

    PresetBank specialtyPlates;
    specialtyPlates.name = "Specialty Plates";

    // Vocal Plate
    specialtyPlates.presets.push_back({
        "Vocal Plate",
        {
            {"dryLevel", 75.0f}, {"lateLevel", 25.0f},
            {"width", 90.0f}, {"preDelay", 10.0f}, {"decay", 1.2f},
            {"highCut", 12000.0f}, {"lowCut", 150.0f}
        }
    });

    // Snare Plate
    specialtyPlates.presets.push_back({
        "Snare Plate",
        {
            {"dryLevel", 85.0f}, {"lateLevel", 15.0f},
            {"width", 80.0f}, {"preDelay", 5.0f}, {"decay", 0.6f},
            {"highCut", 15000.0f}, {"lowCut", 200.0f}
        }
    });

    // Kick Plate
    specialtyPlates.presets.push_back({
        "Kick Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 70.0f}, {"preDelay", 8.0f}, {"decay", 0.8f},
            {"highCut", 8000.0f}, {"lowCut", 40.0f}
        }
    });

    // Percussion Plate
    specialtyPlates.presets.push_back({
        "Percussion Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 3.0f}, {"decay", 0.5f},
            {"highCut", 14000.0f}, {"lowCut", 100.0f}
        }
    });

    PresetBank characterPlates;
    characterPlates.name = "Character Plates";

    // Smooth Plate
    characterPlates.presets.push_back({
        "Smooth Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 15.0f}, {"decay", 1.0f},
            {"highCut", 9000.0f}, {"lowCut", 80.0f}
        }
    });

    // Sharp Plate
    characterPlates.presets.push_back({
        "Sharp Plate",
        {
            {"dryLevel", 85.0f}, {"lateLevel", 15.0f},
            {"width", 90.0f}, {"preDelay", 2.0f}, {"decay", 0.3f},
            {"highCut", 16000.0f}, {"lowCut", 300.0f}
        }
    });

    // Echo Plate
    characterPlates.presets.push_back({
        "Echo Plate",
        {
            {"dryLevel", 70.0f}, {"lateLevel", 30.0f},
            {"width", 120.0f}, {"preDelay", 80.0f}, {"decay", 1.5f},
            {"highCut", 11000.0f}, {"lowCut", 60.0f}
        }
    });

    // Strange Plate
    characterPlates.presets.push_back({
        "Strange Plate",
        {
            {"dryLevel", 75.0f}, {"lateLevel", 25.0f},
            {"width", 150.0f}, {"preDelay", 25.0f}, {"decay", 2.0f},
            {"highCut", 6000.0f}, {"lowCut", 250.0f}
        }
    });

    PresetBank lengthPlates;
    lengthPlates.name = "Length Plates";

    // Short Plate
    lengthPlates.presets.push_back({
        "Short Plate",
        {
            {"dryLevel", 85.0f}, {"lateLevel", 15.0f},
            {"width", 90.0f}, {"preDelay", 0.0f}, {"decay", 0.2f},
            {"highCut", 14000.0f}, {"lowCut", 150.0f}
        }
    });

    // Medium Plate
    lengthPlates.presets.push_back({
        "Medium Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 5.0f}, {"decay", 1.0f},
            {"highCut", 12000.0f}, {"lowCut", 100.0f}
        }
    });

    // Long Plate
    lengthPlates.presets.push_back({
        "Long Plate",
        {
            {"dryLevel", 70.0f}, {"lateLevel", 30.0f},
            {"width", 110.0f}, {"preDelay", 20.0f}, {"decay", 2.5f},
            {"highCut", 10000.0f}, {"lowCut", 80.0f}
        }
    });

    PresetBank tanks;
    tanks.name = "Tanks & Foils";

    // Foil Tray
    tanks.presets.push_back({
        "Foil Tray",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 50.0f}, {"preDelay", 0.0f}, {"decay", 0.3f},
            {"highCut", 16000.0f}, {"lowCut", 200.0f}
        }
    });

    // Metal Roof
    tanks.presets.push_back({
        "Metal Roof",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 120.0f}, {"preDelay", 20.0f}, {"decay", 0.5f},
            {"highCut", 13000.0f}, {"lowCut", 100.0f}
        }
    });

    // Narrow Tank
    tanks.presets.push_back({
        "Narrow Tank",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 60.0f}, {"preDelay", 10.0f}, {"decay", 0.6f},
            {"highCut", 10000.0f}, {"lowCut", 50.0f}
        }
    });

    // Phat Tank
    tanks.presets.push_back({
        "Phat Tank",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 150.0f}, {"preDelay", 10.0f}, {"decay", 1.0f},
            {"highCut", 10000.0f}, {"lowCut", 50.0f}
        }
    });

    PresetBank vintage;
    vintage.name = "Vintage Plates";

    // EMT 140 Style
    vintage.presets.push_back({
        "EMT 140 Style",
        {
            {"dryLevel", 75.0f}, {"lateLevel", 25.0f},
            {"width", 100.0f}, {"preDelay", 5.0f}, {"decay", 1.5f},
            {"highCut", 12000.0f}, {"lowCut", 80.0f}
        }
    });

    // EMT 250 Style
    vintage.presets.push_back({
        "EMT 250 Style",
        {
            {"dryLevel", 70.0f}, {"lateLevel", 30.0f},
            {"width", 110.0f}, {"preDelay", 10.0f}, {"decay", 2.0f},
            {"highCut", 10000.0f}, {"lowCut", 100.0f}
        }
    });

    // Index 2 = Plate algorithm
    presetsByAlgorithm[2] = {plates, specialtyPlates, characterPlates, lengthPlates, tanks, vintage};
}

void PresetManager::initializeEarlyPresets()
{
    PresetBank basicSpaces;
    basicSpaces.name = "Basic Spaces";

    // Small Space
    basicSpaces.presets.push_back({
        "Small Space",
        {
            {"dryLevel", 85.0f}, {"earlyLevel", 15.0f},
            {"size", 8.0f}, {"width", 80.0f},
            {"highCut", 14000.0f}, {"lowCut", 40.0f}
        }
    });

    // Medium Space
    basicSpaces.presets.push_back({
        "Medium Space",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 20.0f},
            {"size", 15.0f}, {"width", 90.0f},
            {"highCut", 13000.0f}, {"lowCut", 30.0f}
        }
    });

    // Large Space
    basicSpaces.presets.push_back({
        "Large Space",
        {
            {"dryLevel", 75.0f}, {"earlyLevel", 25.0f},
            {"size", 25.0f}, {"width", 100.0f},
            {"highCut", 12000.0f}, {"lowCut", 25.0f}
        }
    });

    // Huge Space
    basicSpaces.presets.push_back({
        "Huge Space",
        {
            {"dryLevel", 70.0f}, {"earlyLevel", 30.0f},
            {"size", 40.0f}, {"width", 120.0f},
            {"highCut", 11000.0f}, {"lowCut", 20.0f}
        }
    });

    PresetBank ambienceSpaces;
    ambienceSpaces.name = "Ambience Spaces";

    // Tight Ambience
    ambienceSpaces.presets.push_back({
        "Tight Ambience",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 20.0f},
            {"size", 12.0f}, {"width", 70.0f},
            {"highCut", 13000.0f}, {"lowCut", 50.0f}
        }
    });

    // Wide Ambience
    ambienceSpaces.presets.push_back({
        "Wide Ambience",
        {
            {"dryLevel", 75.0f}, {"earlyLevel", 25.0f},
            {"size", 20.0f}, {"width", 110.0f},
            {"highCut", 12000.0f}, {"lowCut", 30.0f}
        }
    });

    // Very Wide
    ambienceSpaces.presets.push_back({
        "Very Wide",
        {
            {"dryLevel", 70.0f}, {"earlyLevel", 30.0f},
            {"size", 30.0f}, {"width", 150.0f},
            {"highCut", 11000.0f}, {"lowCut", 25.0f}
        }
    });

    PresetBank vintageSpaces;
    vintageSpaces.name = "Vintage Spaces";

    // Abrupt Echo
    vintageSpaces.presets.push_back({
        "Abrupt Echo",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 20.0f},
            {"size", 20.0f}, {"width", 100.0f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Backstage Pass
    vintageSpaces.presets.push_back({
        "Backstage Pass",
        {
            {"dryLevel", 75.0f}, {"earlyLevel", 25.0f},
            {"size", 15.0f}, {"width", 80.0f},
            {"highCut", 12000.0f}, {"lowCut", 50.0f}
        }
    });

    // Concert Venue
    vintageSpaces.presets.push_back({
        "Concert Venue",
        {
            {"dryLevel", 70.0f}, {"earlyLevel", 30.0f},
            {"size", 30.0f}, {"width", 100.0f},
            {"highCut", 14000.0f}, {"lowCut", 40.0f}
        }
    });

    // Damaged Goods
    vintageSpaces.presets.push_back({
        "Damaged Goods",
        {
            {"dryLevel", 85.0f}, {"earlyLevel", 15.0f},
            {"size", 10.0f}, {"width", 60.0f},
            {"highCut", 8000.0f}, {"lowCut", 100.0f}
        }
    });

    PresetBank spaces;
    spaces.name = "Spaces";

    // Elevator Pitch
    spaces.presets.push_back({
        "Elevator Pitch",
        {
            {"dryLevel", 85.0f}, {"earlyLevel", 15.0f},
            {"size", 8.0f}, {"width", 70.0f},
            {"highCut", 10000.0f}, {"lowCut", 80.0f}
        }
    });

    // Floor Thirteen
    spaces.presets.push_back({
        "Floor Thirteen",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 20.0f},
            {"size", 13.0f}, {"width", 90.0f},
            {"highCut", 11000.0f}, {"lowCut", 60.0f}
        }
    });

    // Garage Band
    spaces.presets.push_back({
        "Garage Band",
        {
            {"dryLevel", 75.0f}, {"earlyLevel", 25.0f},
            {"size", 18.0f}, {"width", 100.0f},
            {"highCut", 9000.0f}, {"lowCut", 100.0f}
        }
    });

    // Home Studio
    spaces.presets.push_back({
        "Home Studio",
        {
            {"dryLevel", 90.0f}, {"earlyLevel", 10.0f},
            {"size", 12.0f}, {"width", 85.0f},
            {"highCut", 13000.0f}, {"lowCut", 40.0f}
        }
    });

    PresetBank slaps;
    slaps.name = "Slap Delays";

    // Tight Slap
    slaps.presets.push_back({
        "Tight Slap",
        {
            {"dryLevel", 85.0f}, {"earlyLevel", 15.0f},
            {"size", 5.0f}, {"width", 100.0f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Medium Slap
    slaps.presets.push_back({
        "Medium Slap",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 20.0f},
            {"size", 10.0f}, {"width", 100.0f},
            {"highCut", 14000.0f}, {"lowCut", 4.0f}
        }
    });

    // Wide Slap
    slaps.presets.push_back({
        "Wide Slap",
        {
            {"dryLevel", 75.0f}, {"earlyLevel", 25.0f},
            {"size", 15.0f}, {"width", 120.0f},
            {"highCut", 12000.0f}, {"lowCut", 4.0f}
        }
    });

    // Index 3 = Early Reflections algorithm
    presetsByAlgorithm[3] = {basicSpaces, ambienceSpaces, vintageSpaces, spaces, slaps};
}
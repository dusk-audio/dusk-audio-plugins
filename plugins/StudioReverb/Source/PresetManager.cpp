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
            {"size", 12.0f}, {"width", 90.0f}, {"preDelay", 4.0f}, {"decay", 0.2f},
            {"diffuse", 60.0f}, {"spin", 0.4f}, {"wander", 0.4f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Small Clear Room
    smallRooms.presets.push_back({
        "Small Clear Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 14.0f}, {"width", 100.0f}, {"preDelay", 4.0f}, {"decay", 0.3f},
            {"diffuse", 65.0f}, {"spin", 0.6f}, {"wander", 0.3f},
            {"highCut", 14000.0f}, {"lowCut", 4.0f}
        }
    });

    // Small Dark Room
    smallRooms.presets.push_back({
        "Small Dark Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 15.0f}, {"width", 80.0f}, {"preDelay", 8.0f}, {"decay", 0.4f},
            {"diffuse", 70.0f}, {"spin", 0.8f}, {"wander", 0.2f},
            {"highCut", 8000.0f}, {"lowCut", 4.0f}
        }
    });

    PresetBank mediumRooms;
    mediumRooms.name = "Medium Rooms";

    // Medium Bright Room
    mediumRooms.presets.push_back({
        "Medium Bright Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 18.0f}, {"width", 100.0f}, {"preDelay", 8.0f}, {"decay", 0.4f},
            {"diffuse", 70.0f}, {"spin", 0.8f}, {"wander", 0.4f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Medium Clear Room
    mediumRooms.presets.push_back({
        "Medium Clear Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 18.0f}, {"width", 100.0f}, {"preDelay", 8.0f}, {"decay", 0.4f},
            {"diffuse", 70.0f}, {"spin", 0.8f}, {"wander", 0.4f},
            {"highCut", 10000.0f}, {"lowCut", 4.0f}
        }
    });

    // Medium Dark Room
    mediumRooms.presets.push_back({
        "Medium Dark Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 20.0f}, {"width", 90.0f}, {"preDelay", 12.0f}, {"decay", 0.6f},
            {"diffuse", 75.0f}, {"spin", 1.2f}, {"wander", 0.2f},
            {"highCut", 6000.0f}, {"lowCut", 4.0f}
        }
    });

    PresetBank largeRooms;
    largeRooms.name = "Large Rooms";

    // Large Bright Room
    largeRooms.presets.push_back({
        "Large Bright Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 25.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 0.6f},
            {"diffuse", 80.0f}, {"spin", 1.6f}, {"wander", 0.3f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Large Clear Room
    largeRooms.presets.push_back({
        "Large Clear Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 28.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 0.7f},
            {"diffuse", 80.0f}, {"spin", 1.6f}, {"wander", 0.2f},
            {"highCut", 12000.0f}, {"lowCut", 4.0f}
        }
    });

    // Large Dark Room
    largeRooms.presets.push_back({
        "Large Dark Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 30.0f}, {"width", 100.0f}, {"preDelay", 12.0f}, {"decay", 0.8f},
            {"diffuse", 80.0f}, {"spin", 1.6f}, {"wander", 0.2f},
            {"highCut", 4000.0f}, {"lowCut", 4.0f}
        }
    });

    // Index 0 = Room algorithm
    presetsByAlgorithm[0] = {smallRooms, mediumRooms, largeRooms};
}

void PresetManager::initializeHallPresets()
{
    PresetBank rooms;
    rooms.name = "Rooms";

    // Bright Room
    rooms.presets.push_back({
        "Bright Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 15.0f}, {"width", 90.0f}, {"preDelay", 4.0f}, {"decay", 0.6f},
            {"diffuse", 90.0f}, {"spin", 1.0f}, {"wander", 0.25f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 7900.0f}, {"lowMult", 0.8f}, {"highMult", 0.75f}
        }
    });

    // Clear Room
    rooms.presets.push_back({
        "Clear Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 15.0f}, {"width", 90.0f}, {"preDelay", 4.0f}, {"decay", 0.6f},
            {"diffuse", 90.0f}, {"spin", 1.0f}, {"wander", 0.25f},
            {"highCut", 13000.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 5800.0f}, {"lowMult", 0.9f}, {"highMult", 0.5f}
        }
    });

    // Dark Room
    rooms.presets.push_back({
        "Dark Room",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 16.0f}, {"width", 90.0f}, {"preDelay", 4.0f}, {"decay", 0.7f},
            {"diffuse", 50.0f}, {"spin", 1.0f}, {"wander", 0.25f},
            {"highCut", 7300.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 4900.0f}, {"lowMult", 1.2f}, {"highMult", 0.35f}
        }
    });

    // Small Chamber
    rooms.presets.push_back({
        "Small Chamber",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 16.0f}, {"width", 80.0f}, {"preDelay", 8.0f}, {"decay", 0.8f},
            {"diffuse", 70.0f}, {"spin", 1.2f}, {"wander", 0.1f},
            {"highCut", 8200.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 5500.0f}, {"lowMult", 1.1f}, {"highMult", 0.35f}
        }
    });

    // Large Chamber
    rooms.presets.push_back({
        "Large Chamber",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 20.0f}, {"width", 80.0f}, {"preDelay", 8.0f}, {"decay", 1.0f},
            {"diffuse", 90.0f}, {"spin", 1.8f}, {"wander", 0.12f},
            {"highCut", 7000.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 4900.0f}, {"lowMult", 1.3f}, {"highMult", 0.25f}
        }
    });

    PresetBank smallHalls;
    smallHalls.name = "Small Halls";

    // Small Bright Hall
    smallHalls.presets.push_back({
        "Small Bright Hall",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 10.0f}, {"earlySend", 20.0f}, {"lateLevel", 20.0f},
            {"size", 24.0f}, {"width", 80.0f}, {"preDelay", 12.0f}, {"decay", 1.3f},
            {"diffuse", 90.0f}, {"spin", 2.5f}, {"wander", 0.13f},
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
            {"diffuse", 90.0f}, {"spin", 3.3f}, {"wander", 0.15f},
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
            {"diffuse", 60.0f}, {"spin", 2.5f}, {"wander", 0.1f},
            {"highCut", 5800.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 4000.0f}, {"lowMult", 1.5f}, {"highMult", 0.35f}
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
            {"diffuse", 90.0f}, {"spin", 3.0f}, {"wander", 0.15f},
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
            {"diffuse", 90.0f}, {"spin", 3.5f}, {"wander", 0.2f},
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
            {"diffuse", 70.0f}, {"spin", 3.0f}, {"wander", 0.15f},
            {"highCut", 5000.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 3500.0f}, {"lowMult", 1.5f}, {"highMult", 0.3f}
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
            {"diffuse", 90.0f}, {"spin", 4.0f}, {"wander", 0.2f},
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
            {"diffuse", 90.0f}, {"spin", 4.5f}, {"wander", 0.25f},
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
            {"diffuse", 80.0f}, {"spin", 3.5f}, {"wander", 0.2f},
            {"highCut", 4500.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 3000.0f}, {"lowMult", 1.6f}, {"highMult", 0.25f}
        }
    });

    PresetBank churches;
    churches.name = "Churches & Cathedrals";

    // Small Church
    churches.presets.push_back({
        "Small Church",
        {
            {"dryLevel", 70.0f}, {"earlyLevel", 15.0f}, {"earlySend", 25.0f}, {"lateLevel", 30.0f},
            {"size", 35.0f}, {"width", 100.0f}, {"preDelay", 20.0f}, {"decay", 3.5f},
            {"diffuse", 85.0f}, {"spin", 2.5f}, {"wander", 0.15f},
            {"highCut", 8000.0f}, {"lowCut", 50.0f},
            {"lowCross", 300.0f}, {"highCross", 4000.0f}, {"lowMult", 1.5f}, {"highMult", 0.4f}
        }
    });

    // Cathedral
    churches.presets.push_back({
        "Cathedral",
        {
            {"dryLevel", 60.0f}, {"earlyLevel", 20.0f}, {"earlySend", 30.0f}, {"lateLevel", 40.0f},
            {"size", 50.0f}, {"width", 100.0f}, {"preDelay", 30.0f}, {"decay", 6.0f},
            {"diffuse", 90.0f}, {"spin", 2.0f}, {"wander", 0.1f},
            {"highCut", 6000.0f}, {"lowCut", 80.0f},
            {"lowCross", 250.0f}, {"highCross", 3500.0f}, {"lowMult", 1.8f}, {"highMult", 0.3f}
        }
    });

    // Index 1 = Hall algorithm
    presetsByAlgorithm[1] = {rooms, smallHalls, mediumHalls, largeHalls, churches};
}

void PresetManager::initializePlatePresets()
{
    PresetBank plates;
    plates.name = "Classic Plates";

    // Abrupt Plate
    plates.presets.push_back({
        "Abrupt Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 20.0f}, {"decay", 0.2f},
            {"highCut", 10000.0f}, {"lowCut", 50.0f}
        }
    });

    // Bright Plate
    plates.presets.push_back({
        "Bright Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 0.0f}, {"decay", 0.4f},
            {"highCut", 16000.0f}, {"lowCut", 200.0f}
        }
    });

    // Clear Plate
    plates.presets.push_back({
        "Clear Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 0.0f}, {"decay", 0.6f},
            {"highCut", 13000.0f}, {"lowCut", 100.0f}
        }
    });

    // Dark Plate
    plates.presets.push_back({
        "Dark Plate",
        {
            {"dryLevel", 80.0f}, {"lateLevel", 20.0f},
            {"width", 100.0f}, {"preDelay", 0.0f}, {"decay", 0.8f},
            {"highCut", 7000.0f}, {"lowCut", 50.0f}
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
    presetsByAlgorithm[2] = {plates, tanks, vintage};
}

void PresetManager::initializeEarlyPresets()
{
    PresetBank ambiences;
    ambiences.name = "Ambiences";

    // Abrupt Echo
    ambiences.presets.push_back({
        "Abrupt Echo",
        {
            {"dryLevel", 80.0f}, {"earlyLevel", 20.0f},
            {"size", 20.0f}, {"width", 100.0f},
            {"highCut", 16000.0f}, {"lowCut", 4.0f}
        }
    });

    // Backstage Pass
    ambiences.presets.push_back({
        "Backstage Pass",
        {
            {"dryLevel", 75.0f}, {"earlyLevel", 25.0f},
            {"size", 15.0f}, {"width", 80.0f},
            {"highCut", 12000.0f}, {"lowCut", 50.0f}
        }
    });

    // Concert Venue
    ambiences.presets.push_back({
        "Concert Venue",
        {
            {"dryLevel", 70.0f}, {"earlyLevel", 30.0f},
            {"size", 30.0f}, {"width", 100.0f},
            {"highCut", 14000.0f}, {"lowCut", 40.0f}
        }
    });

    // Damaged Goods
    ambiences.presets.push_back({
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
    presetsByAlgorithm[3] = {ambiences, spaces, slaps};
}
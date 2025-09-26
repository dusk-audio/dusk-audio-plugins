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
            {"diffuse", 90.0f}, {"spin", 2.5f}, {"wander", 13.0f},
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
            {"diffuse", 90.0f}, {"spin", 3.3f}, {"wander", 15.0f},
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
            {"diffuse", 60.0f}, {"spin", 2.5f}, {"wander", 10.0f},
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
            {"diffuse", 90.0f}, {"spin", 3.0f}, {"wander", 15.0f},
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
            {"diffuse", 70.0f}, {"spin", 3.0f}, {"wander", 15.0f},
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
            {"diffuse", 80.0f}, {"spin", 3.5f}, {"wander", 20.0f},
            {"highCut", 4500.0f}, {"lowCut", 4.0f},
            {"lowCross", 500.0f}, {"highCross", 3000.0f}, {"lowMult", 1.6f}, {"highMult", 0.25f}
        }
    });

    PresetBank churches;
    churches.name = "Churches";

    // Small Church
    churches.presets.push_back({
        "Small Church",
        {
            {"dryLevel", 70.0f}, {"earlyLevel", 15.0f}, {"earlySend", 25.0f}, {"lateLevel", 30.0f},
            {"size", 35.0f}, {"width", 100.0f}, {"preDelay", 20.0f}, {"decay", 3.5f},
            {"diffuse", 85.0f}, {"spin", 2.5f}, {"wander", 15.0f},
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
            {"diffuse", 90.0f}, {"spin", 2.0f}, {"wander", 10.0f},
            {"highCut", 6000.0f}, {"lowCut", 80.0f},
            {"lowCross", 250.0f}, {"highCross", 3500.0f}, {"lowMult", 1.8f}, {"highMult", 0.3f}
        }
    });

    // Index 1 = Hall algorithm
    presetsByAlgorithm[1] = {smallHalls, mediumHalls, largeHalls, churches};
}
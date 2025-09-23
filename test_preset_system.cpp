// Simple test to verify preset system indexing
#include <iostream>
#include <array>
#include <vector>
#include <string>
#include <map>

struct Preset {
    std::string name;
    std::map<std::string, float> parameters;
};

struct PresetBank {
    std::string name;
    std::vector<Preset> presets;
};

int main() {
    std::array<std::vector<PresetBank>, 4> presetsByAlgorithm;

    // Simulate initialization like in PresetManager
    PresetBank roomBank;
    roomBank.name = "Room Presets";
    roomBank.presets.push_back({"Small Room", {}});
    roomBank.presets.push_back({"Medium Room", {}});
    presetsByAlgorithm[0] = {roomBank};

    PresetBank hallBank;
    hallBank.name = "Hall Presets";
    hallBank.presets.push_back({"Small Hall", {}});
    hallBank.presets.push_back({"Concert Hall", {}});
    presetsByAlgorithm[1] = {hallBank};

    PresetBank plateBank;
    plateBank.name = "Plate Presets";
    plateBank.presets.push_back({"EMT 140", {}});
    plateBank.presets.push_back({"Vintage Plate", {}});
    presetsByAlgorithm[2] = {plateBank};

    PresetBank earlyBank;
    earlyBank.name = "Early Presets";
    earlyBank.presets.push_back({"Ambience", {}});
    earlyBank.presets.push_back({"Slap", {}});
    presetsByAlgorithm[3] = {earlyBank};

    // Test retrieval
    const char* algoNames[] = {"Room", "Hall", "Plate", "Early"};

    for (int i = 0; i < 4; ++i) {
        std::cout << "\nAlgorithm " << i << " (" << algoNames[i] << "):" << std::endl;
        for (const auto& bank : presetsByAlgorithm[i]) {
            std::cout << "  Bank: " << bank.name << std::endl;
            for (const auto& preset : bank.presets) {
                std::cout << "    - " << preset.name << std::endl;
            }
        }
    }

    return 0;
}
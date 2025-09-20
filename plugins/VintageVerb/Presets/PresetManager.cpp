/*
  ==============================================================================

    PresetManager.cpp - Factory preset definitions

  ==============================================================================
*/

#include "PresetManager.h"

PresetManager::PresetManager()
{
    initializeFactoryPresets();
}

void PresetManager::initializeFactoryPresets()
{
    presets.clear();

    createDrumPresets();
    createVocalPresets();
    createInstrumentPresets();
    createAmbiencePresets();
    createHallPresets();
    createRoomPresets();
    createPlatePresets();
    createChamberPresets();
    createNonlinearPresets();
    createSpecialPresets();
    createVintagePresets();
    createModernPresets();
}

void PresetManager::createDrumPresets()
{
    addPreset("80s Gated Drums", Drums,
        "Classic gated reverb for drums",
        {
            {"mix", 0.35f},
            {"size", 0.25f},
            {"attack", 0.0f},
            {"damping", 0.7f},
            {"predelay", 0.01f},
            {"width", 1.0f},
            {"modulation", 0.1f},
            {"density", 0.8f},
            {"diffusion", 0.5f},
            {"reverbMode", 14.0f},  // Nonlin
            {"colorMode", 1.0f},     // 1980s
        });

    addPreset("Snare Room", Drums,
        "Small room for snare drums",
        {
            {"mix", 0.25f},
            {"size", 0.15f},
            {"attack", 0.0f},
            {"damping", 0.4f},
            {"predelay", 0.005f},
            {"width", 0.8f},
            {"modulation", 0.05f},
            {"density", 0.7f},
            {"diffusion", 0.6f},
            {"reverbMode", 3.0f},    // Room
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Kick Chamber", Drums,
        "Tight chamber for kick drums",
        {
            {"mix", 0.2f},
            {"size", 0.1f},
            {"attack", 0.0f},
            {"damping", 0.6f},
            {"predelay", 0.0f},
            {"width", 0.5f},
            {"modulation", 0.0f},
            {"density", 0.9f},
            {"diffusion", 0.3f},
            {"reverbMode", 4.0f},    // Chamber
            {"colorMode", 0.0f},     // 1970s
        });

    addPreset("Toms Plate", Drums,
        "Bright plate for toms",
        {
            {"mix", 0.3f},
            {"size", 0.35f},
            {"attack", 0.05f},
            {"damping", 0.3f},
            {"predelay", 0.01f},
            {"width", 1.2f},
            {"modulation", 0.2f},
            {"density", 0.85f},
            {"diffusion", 0.8f},
            {"reverbMode", 2.0f},    // Plate
            {"colorMode", 1.0f},     // 1980s
        });
}

void PresetManager::createVocalPresets()
{
    addPreset("Lead Vocal Plate", Vocals,
        "Classic plate for lead vocals",
        {
            {"mix", 0.2f},
            {"size", 0.4f},
            {"attack", 0.1f},
            {"damping", 0.35f},
            {"predelay", 0.02f},
            {"width", 0.9f},
            {"modulation", 0.15f},
            {"density", 0.75f},
            {"diffusion", 0.85f},
            {"reverbMode", 11.0f},   // SmoothPlate
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Backing Vocals Hall", Vocals,
        "Wide hall for backing vocals",
        {
            {"mix", 0.35f},
            {"size", 0.6f},
            {"attack", 0.2f},
            {"damping", 0.4f},
            {"predelay", 0.03f},
            {"width", 1.5f},
            {"modulation", 0.25f},
            {"density", 0.6f},
            {"diffusion", 0.9f},
            {"reverbMode", 0.0f},    // ConcertHall
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Vintage Vocal Chamber", Vocals,
        "1960s chamber sound",
        {
            {"mix", 0.25f},
            {"size", 0.3f},
            {"attack", 0.05f},
            {"damping", 0.5f},
            {"predelay", 0.015f},
            {"width", 0.7f},
            {"modulation", 0.1f},
            {"density", 0.65f},
            {"diffusion", 0.7f},
            {"reverbMode", 20.0f},   // Chamber1979
            {"colorMode", 0.0f},     // 1970s
        });

    addPreset("Whisper Room", Vocals,
        "Intimate room for quiet vocals",
        {
            {"mix", 0.15f},
            {"size", 0.2f},
            {"attack", 0.0f},
            {"damping", 0.45f},
            {"predelay", 0.008f},
            {"width", 0.6f},
            {"modulation", 0.08f},
            {"density", 0.5f},
            {"diffusion", 0.6f},
            {"reverbMode", 12.0f},   // SmoothRoom
            {"colorMode", 2.0f},     // Now
        });
}

void PresetManager::createInstrumentPresets()
{
    addPreset("Piano Hall", Instruments,
        "Concert hall for piano",
        {
            {"mix", 0.3f},
            {"size", 0.7f},
            {"attack", 0.15f},
            {"damping", 0.3f},
            {"predelay", 0.025f},
            {"width", 1.3f},
            {"modulation", 0.2f},
            {"density", 0.7f},
            {"diffusion", 0.95f},
            {"reverbMode", 1.0f},    // BrightHall
            {"colorMode", 2.0f},     // Now
        });

    addPreset("String Ensemble", Instruments,
        "Lush space for strings",
        {
            {"mix", 0.4f},
            {"size", 0.8f},
            {"attack", 0.3f},
            {"damping", 0.25f},
            {"predelay", 0.04f},
            {"width", 1.6f},
            {"modulation", 0.35f},
            {"density", 0.8f},
            {"diffusion", 1.0f},
            {"reverbMode", 6.0f},    // ChorusSpace
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Guitar Amp Spring", Instruments,
        "Spring reverb simulation",
        {
            {"mix", 0.25f},
            {"size", 0.15f},
            {"attack", 0.0f},
            {"damping", 0.6f},
            {"predelay", 0.003f},
            {"width", 0.4f},
            {"modulation", 0.4f},
            {"density", 0.3f},
            {"diffusion", 0.4f},
            {"reverbMode", 10.0f},   // DirtyPlate
            {"colorMode", 0.0f},     // 1970s
        });

    addPreset("Brass Section", Instruments,
        "Bright room for brass",
        {
            {"mix", 0.22f},
            {"size", 0.35f},
            {"attack", 0.02f},
            {"damping", 0.35f},
            {"predelay", 0.012f},
            {"width", 1.1f},
            {"modulation", 0.12f},
            {"density", 0.6f},
            {"diffusion", 0.75f},
            {"reverbMode", 3.0f},    // Room
            {"colorMode", 1.0f},     // 1980s
        });
}

void PresetManager::createAmbiencePresets()
{
    addPreset("Subtle Ambience", Ambiences,
        "Barely there room tone",
        {
            {"mix", 0.1f},
            {"size", 0.05f},
            {"attack", 0.0f},
            {"damping", 0.5f},
            {"predelay", 0.001f},
            {"width", 0.8f},
            {"modulation", 0.02f},
            {"density", 0.4f},
            {"diffusion", 0.5f},
            {"reverbMode", 7.0f},    // Ambience
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Wide Ambience", Ambiences,
        "Spacious ambient field",
        {
            {"mix", 0.25f},
            {"size", 0.5f},
            {"attack", 0.4f},
            {"damping", 0.2f},
            {"predelay", 0.05f},
            {"width", 2.0f},
            {"modulation", 0.3f},
            {"density", 0.3f},
            {"diffusion", 0.9f},
            {"reverbMode", 5.0f},    // RandomSpace
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Dark Ambience", Ambiences,
        "Moody ambient space",
        {
            {"mix", 0.3f},
            {"size", 0.6f},
            {"attack", 0.5f},
            {"damping", 0.7f},
            {"predelay", 0.03f},
            {"width", 1.4f},
            {"modulation", 0.4f},
            {"density", 0.5f},
            {"diffusion", 0.8f},
            {"reverbMode", 9.0f},    // DirtyHall
            {"colorMode", 0.0f},     // 1970s
        });
}

void PresetManager::createHallPresets()
{
    addPreset("Concert Hall", Halls,
        "Large concert hall",
        {
            {"mix", 0.35f},
            {"size", 0.85f},
            {"attack", 0.25f},
            {"damping", 0.3f},
            {"predelay", 0.04f},
            {"width", 1.4f},
            {"modulation", 0.2f},
            {"density", 0.75f},
            {"diffusion", 0.95f},
            {"reverbMode", 0.0f},    // ConcertHall
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Cathedral", Halls,
        "Massive cathedral space",
        {
            {"mix", 0.4f},
            {"size", 1.0f},
            {"attack", 0.4f},
            {"damping", 0.25f},
            {"predelay", 0.06f},
            {"width", 1.6f},
            {"modulation", 0.15f},
            {"density", 0.85f},
            {"diffusion", 1.0f},
            {"reverbMode", 18.0f},   // Cathedral
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Palace Hall", Halls,
        "Royal palace ballroom",
        {
            {"mix", 0.38f},
            {"size", 0.9f},
            {"attack", 0.3f},
            {"damping", 0.28f},
            {"predelay", 0.05f},
            {"width", 1.5f},
            {"modulation", 0.18f},
            {"density", 0.8f},
            {"diffusion", 0.97f},
            {"reverbMode", 19.0f},   // Palace
            {"colorMode", 2.0f},     // Now
        });

    addPreset("1984 Digital Hall", Halls,
        "Classic 1980s digital hall",
        {
            {"mix", 0.32f},
            {"size", 0.75f},
            {"attack", 0.2f},
            {"damping", 0.35f},
            {"predelay", 0.035f},
            {"width", 1.3f},
            {"modulation", 0.25f},
            {"density", 0.7f},
            {"diffusion", 0.88f},
            {"reverbMode", 21.0f},   // Hall1984
            {"colorMode", 1.0f},     // 1980s
        });
}

void PresetManager::createRoomPresets()
{
    addPreset("Studio Live Room", Rooms,
        "Natural studio room",
        {
            {"mix", 0.2f},
            {"size", 0.25f},
            {"attack", 0.0f},
            {"damping", 0.4f},
            {"predelay", 0.008f},
            {"width", 1.0f},
            {"modulation", 0.05f},
            {"density", 0.6f},
            {"diffusion", 0.7f},
            {"reverbMode", 3.0f},    // Room
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Wood Room", Rooms,
        "Warm wooden room",
        {
            {"mix", 0.22f},
            {"size", 0.3f},
            {"attack", 0.05f},
            {"damping", 0.45f},
            {"predelay", 0.01f},
            {"width", 0.9f},
            {"modulation", 0.08f},
            {"density", 0.55f},
            {"diffusion", 0.65f},
            {"reverbMode", 12.0f},   // SmoothRoom
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Concrete Room", Rooms,
        "Hard reflective room",
        {
            {"mix", 0.25f},
            {"size", 0.28f},
            {"attack", 0.0f},
            {"damping", 0.2f},
            {"predelay", 0.006f},
            {"width", 1.1f},
            {"modulation", 0.03f},
            {"density", 0.7f},
            {"diffusion", 0.5f},
            {"reverbMode", 3.0f},    // Room
            {"colorMode", 2.0f},     // Now
        });
}

void PresetManager::createPlatePresets()
{
    addPreset("EMT 140 Plate", Plates,
        "Classic EMT plate emulation",
        {
            {"mix", 0.28f},
            {"size", 0.45f},
            {"attack", 0.08f},
            {"damping", 0.32f},
            {"predelay", 0.015f},
            {"width", 1.0f},
            {"modulation", 0.18f},
            {"density", 0.8f},
            {"diffusion", 0.9f},
            {"reverbMode", 2.0f},    // Plate
            {"colorMode", 0.0f},     // 1970s
        });

    addPreset("Smooth Plate", Plates,
        "Modern smooth plate",
        {
            {"mix", 0.25f},
            {"size", 0.4f},
            {"attack", 0.1f},
            {"damping", 0.35f},
            {"predelay", 0.02f},
            {"width", 0.95f},
            {"modulation", 0.15f},
            {"density", 0.75f},
            {"diffusion", 0.95f},
            {"reverbMode", 11.0f},   // SmoothPlate
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Dirty Plate", Plates,
        "Gritty vintage plate",
        {
            {"mix", 0.3f},
            {"size", 0.38f},
            {"attack", 0.05f},
            {"damping", 0.5f},
            {"predelay", 0.012f},
            {"width", 0.85f},
            {"modulation", 0.25f},
            {"density", 0.65f},
            {"diffusion", 0.75f},
            {"reverbMode", 10.0f},   // DirtyPlate
            {"colorMode", 0.0f},     // 1970s
        });
}

void PresetManager::createChamberPresets()
{
    addPreset("Echo Chamber", Chambers,
        "Classic echo chamber",
        {
            {"mix", 0.26f},
            {"size", 0.32f},
            {"attack", 0.06f},
            {"damping", 0.42f},
            {"predelay", 0.018f},
            {"width", 0.8f},
            {"modulation", 0.12f},
            {"density", 0.68f},
            {"diffusion", 0.72f},
            {"reverbMode", 4.0f},    // Chamber
            {"colorMode", 0.0f},     // 1970s
        });

    addPreset("1979 Chamber", Chambers,
        "Late 70s digital chamber",
        {
            {"mix", 0.28f},
            {"size", 0.35f},
            {"attack", 0.07f},
            {"damping", 0.38f},
            {"predelay", 0.02f},
            {"width", 0.9f},
            {"modulation", 0.15f},
            {"density", 0.7f},
            {"diffusion", 0.78f},
            {"reverbMode", 20.0f},   // Chamber1979
            {"colorMode", 0.0f},     // 1970s
        });

    addPreset("Stone Chamber", Chambers,
        "Hard stone chamber",
        {
            {"mix", 0.24f},
            {"size", 0.3f},
            {"attack", 0.03f},
            {"damping", 0.25f},
            {"predelay", 0.014f},
            {"width", 0.75f},
            {"modulation", 0.08f},
            {"density", 0.72f},
            {"diffusion", 0.6f},
            {"reverbMode", 4.0f},    // Chamber
            {"colorMode", 2.0f},     // Now
        });
}

void PresetManager::createNonlinearPresets()
{
    addPreset("Gate Reverb", Nonlinear,
        "Classic gated reverb",
        {
            {"mix", 0.4f},
            {"size", 0.2f},
            {"attack", 0.0f},
            {"damping", 0.8f},
            {"predelay", 0.005f},
            {"width", 1.2f},
            {"modulation", 0.05f},
            {"density", 0.9f},
            {"diffusion", 0.4f},
            {"reverbMode", 14.0f},   // Nonlin
            {"colorMode", 1.0f},     // 1980s
        });

    addPreset("Reverse Reverb", Nonlinear,
        "Backwards reverb effect",
        {
            {"mix", 0.35f},
            {"size", 0.25f},
            {"attack", 0.8f},
            {"damping", 0.6f},
            {"predelay", 0.0f},
            {"width", 1.4f},
            {"modulation", 0.3f},
            {"density", 0.85f},
            {"diffusion", 0.5f},
            {"reverbMode", 14.0f},   // Nonlin
            {"colorMode", 1.0f},     // 1980s
        });

    addPreset("Bloom Reverb", Nonlinear,
        "Expanding bloom effect",
        {
            {"mix", 0.32f},
            {"size", 0.4f},
            {"attack", 0.9f},
            {"damping", 0.35f},
            {"predelay", 0.02f},
            {"width", 1.6f},
            {"modulation", 0.35f},
            {"density", 0.7f},
            {"diffusion", 0.85f},
            {"reverbMode", 13.0f},   // SmoothRandom
            {"colorMode", 2.0f},     // Now
        });
}

void PresetManager::createSpecialPresets()
{
    addPreset("Chaotic Space", Special,
        "Unpredictable chaotic reverb",
        {
            {"mix", 0.35f},
            {"size", 0.6f},
            {"attack", 0.15f},
            {"damping", 0.4f},
            {"predelay", 0.025f},
            {"width", 1.5f},
            {"modulation", 0.5f},
            {"density", 0.6f},
            {"diffusion", 0.7f},
            {"reverbMode", 15.0f},   // ChaoticHall
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Sanctuary", Special,
        "Sacred space reverb",
        {
            {"mix", 0.38f},
            {"size", 0.8f},
            {"attack", 0.35f},
            {"damping", 0.28f},
            {"predelay", 0.045f},
            {"width", 1.7f},
            {"modulation", 0.12f},
            {"density", 0.82f},
            {"diffusion", 0.98f},
            {"reverbMode", 8.0f},    // Sanctuary
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Random Space", Special,
        "Randomized reflections",
        {
            {"mix", 0.3f},
            {"size", 0.5f},
            {"attack", 0.2f},
            {"damping", 0.45f},
            {"predelay", 0.03f},
            {"width", 1.3f},
            {"modulation", 0.4f},
            {"density", 0.5f},
            {"diffusion", 0.6f},
            {"reverbMode", 5.0f},    // RandomSpace
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Chorus Space", Special,
        "Chorused reverb tails",
        {
            {"mix", 0.32f},
            {"size", 0.55f},
            {"attack", 0.18f},
            {"damping", 0.32f},
            {"predelay", 0.028f},
            {"width", 1.4f},
            {"modulation", 0.6f},
            {"density", 0.65f},
            {"diffusion", 0.8f},
            {"reverbMode", 6.0f},    // ChorusSpace
            {"colorMode", 2.0f},     // Now
        });
}

void PresetManager::createVintagePresets()
{
    addPreset("70s Tape Echo", Vintage,
        "Tape echo chamber",
        {
            {"mix", 0.35f},
            {"size", 0.3f},
            {"attack", 0.08f},
            {"damping", 0.6f},
            {"predelay", 0.025f},
            {"width", 0.7f},
            {"modulation", 0.3f},
            {"density", 0.45f},
            {"diffusion", 0.55f},
            {"reverbMode", 9.0f},    // DirtyHall
            {"colorMode", 0.0f},     // 1970s
            {"bassFreq", 200.0f},
            {"bassMul", 1.3f},
            {"highFreq", 4000.0f},
            {"highMul", 0.6f},
        });

    addPreset("80s Digital", Vintage,
        "Early digital reverb",
        {
            {"mix", 0.3f},
            {"size", 0.5f},
            {"attack", 0.15f},
            {"damping", 0.35f},
            {"predelay", 0.03f},
            {"width", 1.2f},
            {"modulation", 0.25f},
            {"density", 0.7f},
            {"diffusion", 0.75f},
            {"reverbMode", 21.0f},   // Hall1984
            {"colorMode", 1.0f},     // 1980s
            {"highFreq", 8000.0f},
            {"highMul", 1.2f},
        });

    addPreset("Abbey Road Chamber", Vintage,
        "Famous studio chamber",
        {
            {"mix", 0.28f},
            {"size", 0.34f},
            {"attack", 0.06f},
            {"damping", 0.48f},
            {"predelay", 0.018f},
            {"width", 0.85f},
            {"modulation", 0.14f},
            {"density", 0.66f},
            {"diffusion", 0.74f},
            {"reverbMode", 20.0f},   // Chamber1979
            {"colorMode", 0.0f},     // 1970s
        });

    addPreset("Lexicon 224", Vintage,
        "Classic Lexicon sound",
        {
            {"mix", 0.32f},
            {"size", 0.65f},
            {"attack", 0.2f},
            {"damping", 0.32f},
            {"predelay", 0.035f},
            {"width", 1.3f},
            {"modulation", 0.22f},
            {"density", 0.72f},
            {"diffusion", 0.88f},
            {"reverbMode", 1.0f},    // BrightHall
            {"colorMode", 1.0f},     // 1980s
        });
}

void PresetManager::createModernPresets()
{
    addPreset("Clean Hall", Modern,
        "Pristine modern hall",
        {
            {"mix", 0.28f},
            {"size", 0.7f},
            {"attack", 0.2f},
            {"damping", 0.28f},
            {"predelay", 0.04f},
            {"width", 1.4f},
            {"modulation", 0.15f},
            {"density", 0.78f},
            {"diffusion", 0.96f},
            {"reverbMode", 0.0f},    // ConcertHall
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Studio Plate", Modern,
        "Modern studio plate",
        {
            {"mix", 0.22f},
            {"size", 0.38f},
            {"attack", 0.1f},
            {"damping", 0.34f},
            {"predelay", 0.022f},
            {"width", 1.0f},
            {"modulation", 0.12f},
            {"density", 0.76f},
            {"diffusion", 0.92f},
            {"reverbMode", 11.0f},   // SmoothPlate
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Ambient Pad", Modern,
        "Lush ambient texture",
        {
            {"mix", 0.4f},
            {"size", 0.85f},
            {"attack", 0.5f},
            {"damping", 0.22f},
            {"predelay", 0.06f},
            {"width", 1.8f},
            {"modulation", 0.45f},
            {"density", 0.5f},
            {"diffusion", 0.95f},
            {"reverbMode", 6.0f},    // ChorusSpace
            {"colorMode", 2.0f},     // Now
        });

    addPreset("Transparent Room", Modern,
        "Clear natural room",
        {
            {"mix", 0.18f},
            {"size", 0.22f},
            {"attack", 0.0f},
            {"damping", 0.38f},
            {"predelay", 0.01f},
            {"width", 1.0f},
            {"modulation", 0.04f},
            {"density", 0.62f},
            {"diffusion", 0.78f},
            {"reverbMode", 12.0f},   // SmoothRoom
            {"colorMode", 2.0f},     // Now
        });
}

void PresetManager::addPreset(const juce::String& name, PresetCategory category,
                             const juce::String& description,
                             std::initializer_list<std::pair<const char*, float>> params)
{
    Preset preset;
    preset.name = name;
    preset.category = getCategoryName(category);
    preset.description = description;

    // Add default parameters first
    preset.parameters["mix"] = 0.5f;
    preset.parameters["size"] = 0.5f;
    preset.parameters["attack"] = 0.1f;
    preset.parameters["damping"] = 0.5f;
    preset.parameters["predelay"] = 0.02f;
    preset.parameters["width"] = 1.0f;
    preset.parameters["modulation"] = 0.2f;
    preset.parameters["bassFreq"] = 150.0f;
    preset.parameters["bassMul"] = 1.0f;
    preset.parameters["highFreq"] = 6000.0f;
    preset.parameters["highMul"] = 1.0f;
    preset.parameters["density"] = 0.7f;
    preset.parameters["diffusion"] = 0.8f;
    preset.parameters["shape"] = 0.5f;
    preset.parameters["spread"] = 1.0f;
    preset.parameters["reverbMode"] = 0.0f;
    preset.parameters["colorMode"] = 2.0f;
    preset.parameters["routingMode"] = 1.0f;  // Parallel
    preset.parameters["engineMix"] = 0.5f;
    preset.parameters["hpfFreq"] = 20.0f;
    preset.parameters["lpfFreq"] = 20000.0f;
    preset.parameters["tiltGain"] = 0.0f;
    preset.parameters["inputGain"] = 0.0f;
    preset.parameters["outputGain"] = 0.0f;

    // Override with specified parameters
    for (const auto& param : params)
    {
        preset.parameters[param.first] = param.second;
    }

    presets.push_back(preset);
}

int PresetManager::getNumPresetsInCategory(PresetCategory category) const
{
    juce::String categoryName = getCategoryName(category);
    int count = 0;

    for (const auto& preset : presets)
    {
        if (preset.category == categoryName)
            count++;
    }

    return count;
}

const PresetManager::Preset* PresetManager::getPreset(int index) const
{
    if (index >= 0 && index < static_cast<int>(presets.size()))
        return &presets[static_cast<size_t>(index)];
    return nullptr;
}

const PresetManager::Preset* PresetManager::getPresetByName(const juce::String& name) const
{
    for (const auto& preset : presets)
    {
        if (preset.name == name)
            return &preset;
    }
    return nullptr;
}

std::vector<const PresetManager::Preset*> PresetManager::getPresetsInCategory(PresetCategory category) const
{
    std::vector<const Preset*> categoryPresets;
    juce::String categoryName = getCategoryName(category);

    for (const auto& preset : presets)
    {
        if (preset.category == categoryName)
            categoryPresets.push_back(&preset);
    }

    return categoryPresets;
}

void PresetManager::applyPreset(const Preset* preset, juce::AudioProcessorValueTreeState& apvts)
{
    if (!preset) return;

    for (const auto& param : preset->parameters)
    {
        if (auto* parameter = apvts.getParameter(param.first))
        {
            parameter->setValueNotifyingHost(parameter->convertTo0to1(param.second));
        }
    }
}

void PresetManager::applyPresetByIndex(int index, juce::AudioProcessorValueTreeState& apvts)
{
    applyPreset(getPreset(index), apvts);
}

void PresetManager::saveUserPreset(const juce::String& name, const juce::AudioProcessorValueTreeState& apvts)
{
    Preset preset;
    preset.name = name;
    preset.category = "User";
    preset.description = "User preset";

    // Save all parameters
    for (auto* param : apvts.processor.getParameters())
    {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
        {
            preset.parameters[p->paramID] = p->getValue();
        }
    }

    userPresets.push_back(preset);

    // TODO: Save to file
}

void PresetManager::loadUserPreset(const juce::File& file, juce::AudioProcessorValueTreeState& apvts)
{
    // TODO: Implement file loading
}

juce::String PresetManager::getCategoryName(PresetCategory category)
{
    switch (category)
    {
        case Drums:         return "Drums";
        case Vocals:        return "Vocals";
        case Instruments:   return "Instruments";
        case Ambiences:     return "Ambiences";
        case Halls:         return "Halls";
        case Rooms:         return "Rooms";
        case Plates:        return "Plates";
        case Chambers:      return "Chambers";
        case Nonlinear:     return "Nonlinear";
        case Special:       return "Special";
        case Vintage:       return "Vintage";
        case Modern:        return "Modern";
        default:            return "Unknown";
    }
}
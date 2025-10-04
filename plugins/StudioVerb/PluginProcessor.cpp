/*
  ==============================================================================

    Studio Verb - Professional Reverb Plugin
    Copyright (c) 2024 Luna CO. Audio

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ReverbEngineEnhanced.h"  // Task 3: Using enhanced engine

//==============================================================================
StudioVerbAudioProcessor::StudioVerbAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       parameters(*this, nullptr, "Parameters", createParameterLayout())
{
    // Initialize with default values first - lightweight operations only
    currentAlgorithm.store(Room);
    currentSize.store(0.5f);
    currentDamp.store(0.5f);
    currentPredelay.store(20.0f);
    manualPredelay.store(20.0f);  // Initialize to match currentPredelay
    currentMix.store(0.3f);
    currentWidth.store(1.0f);

    // Don't create the reverb engine in constructor - defer to prepareToPlay
    // This avoids heavy initialization during plugin scanning
    reverbEngine = nullptr;

    // Initialize factory presets - lightweight
    initializePresets();

    // Add parameter listeners
    parameters.addParameterListener(ALGORITHM_ID, this);
    parameters.addParameterListener(SIZE_ID, this);
    parameters.addParameterListener(DAMP_ID, this);
    parameters.addParameterListener(PREDELAY_ID, this);
    parameters.addParameterListener(MIX_ID, this);
    parameters.addParameterListener(WIDTH_ID, this);
    parameters.addParameterListener(LOW_RT60_ID, this);
    parameters.addParameterListener(MID_RT60_ID, this);
    parameters.addParameterListener(HIGH_RT60_ID, this);
    parameters.addParameterListener(INFINITE_ID, this);
    parameters.addParameterListener(OVERSAMPLING_ID, this);
    parameters.addParameterListener(ROOM_SHAPE_ID, this);
    parameters.addParameterListener(VINTAGE_ID, this);
    parameters.addParameterListener(PREDELAY_BEATS_ID, this);
}

StudioVerbAudioProcessor::~StudioVerbAudioProcessor()
{
    parameters.removeParameterListener(ALGORITHM_ID, this);
    parameters.removeParameterListener(SIZE_ID, this);
    parameters.removeParameterListener(DAMP_ID, this);
    parameters.removeParameterListener(PREDELAY_ID, this);
    parameters.removeParameterListener(MIX_ID, this);
    parameters.removeParameterListener(WIDTH_ID, this);
    parameters.removeParameterListener(LOW_RT60_ID, this);
    parameters.removeParameterListener(MID_RT60_ID, this);
    parameters.removeParameterListener(HIGH_RT60_ID, this);
    parameters.removeParameterListener(INFINITE_ID, this);
    parameters.removeParameterListener(OVERSAMPLING_ID, this);
    parameters.removeParameterListener(ROOM_SHAPE_ID, this);
    parameters.removeParameterListener(VINTAGE_ID, this);
    parameters.removeParameterListener(PREDELAY_BEATS_ID, this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout StudioVerbAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Algorithm selector
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ALGORITHM_ID,
        "Algorithm",
        juce::StringArray { "Room", "Hall", "Plate", "Early Reflections", "Gated", "Reverse" },
        0));

    // Size parameter (0-1)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        SIZE_ID,
        "Size",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        "",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2); },
        [](const juce::String& text) { return text.getFloatValue(); }));

    // Damping parameter (0-1)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        DAMP_ID,
        "Damping",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        "",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2); },
        [](const juce::String& text) { return text.getFloatValue(); }));

    // Predelay parameter (0-200ms)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PREDELAY_ID,
        "Predelay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        0.0f,
        "ms",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ms"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    // Mix parameter (0-1)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        MIX_ID,
        "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        "",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(static_cast<int>(value * 100)) + "%"; },
        [](const juce::String& text) { return text.getFloatValue() / 100.0f; }));

    // Width parameter (0-1)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        WIDTH_ID,
        "Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        "",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(static_cast<int>(value * 100)) + "%"; },
        [](const juce::String& text) { return text.getFloatValue() / 100.0f; }));

    // Advanced RT60 parameters
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        LOW_RT60_ID,
        "Low RT60",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.1f),
        2.0f,
        "s",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " s"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        MID_RT60_ID,
        "Mid RT60",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.1f),
        2.0f,
        "s",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " s"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        HIGH_RT60_ID,
        "High RT60",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.1f),
        1.5f,
        "s",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " s"; },
        [](const juce::String& text) { return text.getFloatValue(); }));

    // Infinite decay mode
    layout.add(std::make_unique<juce::AudioParameterBool>(
        INFINITE_ID,
        "Infinite",
        false));

    // Oversampling factor (1x, 2x, 4x)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        OVERSAMPLING_ID,
        "Oversampling",
        juce::StringArray { "Off", "2x", "4x" },
        0));

    // Room shape parameter
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ROOM_SHAPE_ID,
        "Room Shape",
        juce::StringArray { "Studio Room", "Small Room", "Large Hall", "Cathedral", "Chamber", "Warehouse", "Booth", "Tunnel" },
        0));

    // Vintage/warmth parameter
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        VINTAGE_ID,
        "Vintage",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        "",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(static_cast<int>(value * 100)) + "%"; },
        [](const juce::String& text) { return text.getFloatValue() / 100.0f; }));

    // Tempo-synced predelay
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        PREDELAY_BEATS_ID,
        "Predelay Sync",
        juce::StringArray { "Off", "1/16", "1/8", "1/4", "1/2" },
        0));

    return layout;
}

//==============================================================================
void StudioVerbAudioProcessor::initializePresets()
{
    // Room presets
    factoryPresets.push_back({ "Small Office", Room, 0.3f, 0.6f, 10.0f, 0.3f });
    factoryPresets.push_back({ "Living Room", Room, 0.5f, 0.4f, 20.0f, 0.35f });
    factoryPresets.push_back({ "Conference Room", Room, 0.7f, 0.5f, 15.0f, 0.4f });
    factoryPresets.push_back({ "Studio Live", Room, 0.6f, 0.3f, 12.0f, 0.25f });
    factoryPresets.push_back({ "Drum Room", Room, 0.4f, 0.7f, 5.0f, 0.5f });

    // Hall presets
    factoryPresets.push_back({ "Small Hall", Hall, 0.6f, 0.4f, 25.0f, 0.4f });
    factoryPresets.push_back({ "Concert Hall", Hall, 0.8f, 0.3f, 35.0f, 0.45f });
    factoryPresets.push_back({ "Cathedral", Hall, 0.9f, 0.2f, 50.0f, 0.5f });
    factoryPresets.push_back({ "Theater", Hall, 0.7f, 0.3f, 30.0f, 0.35f });
    factoryPresets.push_back({ "Arena", Hall, 0.85f, 0.25f, 40.0f, 0.4f });

    // Plate presets
    factoryPresets.push_back({ "Bright Plate", Plate, 0.4f, 0.1f, 5.0f, 0.4f });
    factoryPresets.push_back({ "Vintage Plate", Plate, 0.6f, 0.3f, 0.0f, 0.45f });
    factoryPresets.push_back({ "Shimmer Plate", Plate, 0.5f, 0.2f, 10.0f, 0.5f });
    factoryPresets.push_back({ "Dark Plate", Plate, 0.7f, 0.6f, 8.0f, 0.35f });
    factoryPresets.push_back({ "Studio Plate", Plate, 0.55f, 0.25f, 12.0f, 0.3f });

    // Early Reflections presets
    factoryPresets.push_back({ "Tight Slap", EarlyReflections, 0.2f, 0.0f, 0.0f, 0.6f });
    factoryPresets.push_back({ "Medium Bounce", EarlyReflections, 0.4f, 0.0f, 20.0f, 0.5f });
    factoryPresets.push_back({ "Distant Echo", EarlyReflections, 0.6f, 0.0f, 50.0f, 0.4f });
    factoryPresets.push_back({ "Ambience", EarlyReflections, 0.5f, 0.0f, 30.0f, 0.3f });
    factoryPresets.push_back({ "Pre-Verb", EarlyReflections, 0.3f, 0.0f, 15.0f, 0.7f });

    // Showcase presets highlighting new features
    factoryPresets.push_back({ "Lush Hall", Hall, 0.85f, 0.35f, 40.0f, 0.5f });  // Uses 32-channel FDN modulation
    factoryPresets.push_back({ "Infinite Pad", Hall, 0.9f, 0.1f, 60.0f, 0.6f });  // For infinite mode
    factoryPresets.push_back({ "Bright Dattorro", Plate, 0.6f, 0.1f, 15.0f, 0.45f });  // Bright plate with output diffusion
    factoryPresets.push_back({ "Dark Dattorro", Plate, 0.7f, 0.8f, 20.0f, 0.4f });  // Dark plate with heavy damping
    factoryPresets.push_back({ "Crystal Cathedral", Hall, 0.95f, 0.15f, 75.0f, 0.55f });  // Multiband RT60 showcase
}

//==============================================================================
void StudioVerbAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // Thread safety: Lock to prevent artifacts during audio processing
    const juce::ScopedLock sl(processLock);

    // Input validation and bounds checking
    if (parameterID == ALGORITHM_ID)
    {
        int algorithmInt = static_cast<int>(newValue);
        algorithmInt = juce::jlimit(0, static_cast<int>(NumAlgorithms) - 1, algorithmInt);
        currentAlgorithm.store(static_cast<Algorithm>(algorithmInt));

        if (reverbEngine)
            reverbEngine->setAlgorithm(algorithmInt);
    }
    else if (parameterID == SIZE_ID)
    {
        float clampedSize = juce::jlimit(0.0f, 1.0f, newValue);
        currentSize.store(clampedSize);

        if (reverbEngine)
            reverbEngine->setSize(clampedSize);
    }
    else if (parameterID == DAMP_ID)
    {
        float clampedDamp = juce::jlimit(0.0f, 1.0f, newValue);
        currentDamp.store(clampedDamp);

        if (reverbEngine)
            reverbEngine->setDamping(clampedDamp);
    }
    else if (parameterID == PREDELAY_ID)
    {
        float clampedPredelay = juce::jlimit(0.0f, 200.0f, newValue);
        currentPredelay.store(clampedPredelay);

        // Save as manual predelay (preserves user setting when tempo sync is off)
        manualPredelay.store(clampedPredelay);

        // Only apply if tempo sync is off
        if (currentPredelayBeats.load() == 0 && reverbEngine)
            reverbEngine->setPredelay(clampedPredelay);
    }
    else if (parameterID == MIX_ID)
    {
        float clampedMix = juce::jlimit(0.0f, 1.0f, newValue);
        currentMix.store(clampedMix);

        if (reverbEngine)
            reverbEngine->setMix(clampedMix);
    }
    else if (parameterID == WIDTH_ID)
    {
        float clampedWidth = juce::jlimit(0.0f, 1.0f, newValue);
        currentWidth.store(clampedWidth);

        if (reverbEngine)
            reverbEngine->setWidth(clampedWidth);
    }
    else if (parameterID == LOW_RT60_ID)
    {
        float clampedRT60 = juce::jlimit(0.1f, 10.0f, newValue);
        currentLowRT60.store(clampedRT60);

        if (reverbEngine)
            reverbEngine->setLowDecayTime(clampedRT60);
    }
    else if (parameterID == MID_RT60_ID)
    {
        float clampedRT60 = juce::jlimit(0.1f, 10.0f, newValue);
        currentMidRT60.store(clampedRT60);

        if (reverbEngine)
            reverbEngine->setMidDecayTime(clampedRT60);
    }
    else if (parameterID == HIGH_RT60_ID)
    {
        float clampedRT60 = juce::jlimit(0.1f, 10.0f, newValue);
        currentHighRT60.store(clampedRT60);

        if (reverbEngine)
            reverbEngine->setHighDecayTime(clampedRT60);
    }
    else if (parameterID == INFINITE_ID)
    {
        bool infinite = (newValue >= 0.5f);
        currentInfinite.store(infinite);

        if (reverbEngine)
            reverbEngine->setInfiniteDecay(infinite);
    }
    else if (parameterID == OVERSAMPLING_ID)
    {
        int oversamplingChoice = static_cast<int>(newValue);
        currentOversampling.store(oversamplingChoice);

        if (reverbEngine)
        {
            // 0 = Off, 1 = 2x, 2 = 4x
            reverbEngine->setOversamplingEnabled(oversamplingChoice > 0);

            // Convert choice to actual factor
            int factor = (oversamplingChoice == 0) ? 1 : (oversamplingChoice == 1) ? 2 : 4;
            reverbEngine->setOversamplingFactor(factor);

            // Update latency reporting
            setLatencySamples(reverbEngine->getOversamplingLatency());
        }
    }
    else if (parameterID == ROOM_SHAPE_ID)
    {
        int shape = static_cast<int>(newValue);
        currentRoomShape.store(shape);

        if (reverbEngine)
            reverbEngine->setRoomShape(shape);
    }
    else if (parameterID == VINTAGE_ID)
    {
        float clampedVintage = juce::jlimit(0.0f, 1.0f, newValue);
        currentVintage.store(clampedVintage);

        if (reverbEngine)
            reverbEngine->setVintage(clampedVintage);
    }
    else if (parameterID == PREDELAY_BEATS_ID)
    {
        int beatChoice = static_cast<int>(newValue);
        int previousBeatChoice = currentPredelayBeats.load();
        currentPredelayBeats.store(beatChoice);

        if (beatChoice > 0 && reverbEngine)
        {
            // Save manual predelay before switching to tempo sync
            if (previousBeatChoice == 0)
                manualPredelay.store(currentPredelay.load());

            // Get tempo from host
            if (auto* playHead = getPlayHead())
            {
                auto positionInfo = playHead->getPosition();
                if (positionInfo && positionInfo->getBpm().hasValue())
                {
                    double bpm = *positionInfo->getBpm();
                    float msPerBeat = 60000.0f / static_cast<float>(bpm);  // Corrected variable name

                    // Calculate predelay in ms based on beat division
                    float beats = 0.0f;
                    switch (beatChoice)
                    {
                        case 1: beats = 0.0625f; break;  // 1/16
                        case 2: beats = 0.125f; break;   // 1/8
                        case 3: beats = 0.25f; break;    // 1/4
                        case 4: beats = 0.5f; break;     // 1/2
                    }

                    float predelayMs = beats * msPerBeat;
                    reverbEngine->setPredelay(predelayMs);
                    // Don't overwrite currentPredelay - keep manual value intact via manualPredelay
                }
                else
                {
                    DBG("StudioVerb: Tempo sync requires host tempo information (not available)");
                }
            }
            else
            {
                DBG("StudioVerb: No AudioPlayHead available for tempo sync");
            }
        }
        else if (beatChoice == 0)
        {
            // Restore manual predelay when switching back to manual mode
            if (reverbEngine)
            {
                float restoredPredelay = manualPredelay.load();
                currentPredelay.store(restoredPredelay);
                reverbEngine->setPredelay(restoredPredelay);

                // Update the parameter to reflect restored value
                if (auto* param = parameters.getParameter(PREDELAY_ID))
                    param->setValueNotifyingHost(restoredPredelay / 200.0f);
            }
        }
    }
}

//==============================================================================
// Task 4: Extended to handle user presets
void StudioVerbAudioProcessor::loadPreset(int presetIndex)
{
    const Preset* preset = nullptr;
    int factoryCount = static_cast<int>(factoryPresets.size());

    if (presetIndex >= 0 && presetIndex < factoryCount)
    {
        preset = &factoryPresets[presetIndex];
    }
    else
    {
        int userIndex = presetIndex - factoryCount;
        if (userIndex >= 0 && userIndex < static_cast<int>(userPresets.size()))
        {
            preset = &userPresets[userIndex];
        }
    }

    if (preset != nullptr)
    {
        // Update basic parameters
        if (auto* param = parameters.getParameter(ALGORITHM_ID))
            param->setValueNotifyingHost(static_cast<float>(preset->algorithm) / (NumAlgorithms - 1));

        if (auto* param = parameters.getParameter(SIZE_ID))
            param->setValueNotifyingHost(preset->size);

        if (auto* param = parameters.getParameter(DAMP_ID))
            param->setValueNotifyingHost(preset->damp);

        if (auto* param = parameters.getParameter(PREDELAY_ID))
            param->setValueNotifyingHost(preset->predelay / 200.0f);

        if (auto* param = parameters.getParameter(MIX_ID))
            param->setValueNotifyingHost(preset->mix);

        if (auto* param = parameters.getParameter(WIDTH_ID))
            param->setValueNotifyingHost(preset->width);

        // Update advanced parameters
        if (auto* param = parameters.getParameter(LOW_RT60_ID))
            param->setValueNotifyingHost((preset->lowRT60 - 0.1f) / 9.9f);

        if (auto* param = parameters.getParameter(MID_RT60_ID))
            param->setValueNotifyingHost((preset->midRT60 - 0.1f) / 9.9f);

        if (auto* param = parameters.getParameter(HIGH_RT60_ID))
            param->setValueNotifyingHost((preset->highRT60 - 0.1f) / 9.9f);

        if (auto* param = parameters.getParameter(INFINITE_ID))
            param->setValueNotifyingHost(preset->infinite ? 1.0f : 0.0f);

        if (auto* param = parameters.getParameter(OVERSAMPLING_ID))
            param->setValueNotifyingHost(static_cast<float>(preset->oversampling) / 2.0f);

        if (auto* param = parameters.getParameter(ROOM_SHAPE_ID))
            param->setValueNotifyingHost(static_cast<float>(preset->roomShape) / 7.0f);

        if (auto* param = parameters.getParameter(PREDELAY_BEATS_ID))
            param->setValueNotifyingHost(static_cast<float>(preset->predelayBeats) / 4.0f);

        if (auto* param = parameters.getParameter(VINTAGE_ID))
            param->setValueNotifyingHost(preset->vintage);

        currentPresetIndex = presetIndex;
    }
}

//==============================================================================
juce::StringArray StudioVerbAudioProcessor::getPresetNamesForAlgorithm(Algorithm algo) const
{
    juce::StringArray names;

    for (const auto& preset : factoryPresets)
    {
        if (preset.algorithm == algo)
            names.add(preset.name);
    }

    return names;
}

//==============================================================================
const juce::String StudioVerbAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool StudioVerbAudioProcessor::acceptsMidi() const
{
    return false;
}

bool StudioVerbAudioProcessor::producesMidi() const
{
    return false;
}

bool StudioVerbAudioProcessor::isMidiEffect() const
{
    return false;
}

// Task 7: Improved latency reporting
double StudioVerbAudioProcessor::getTailLengthSeconds() const
{
    // Return infinity if infinite mode is enabled
    if (currentInfinite.load())
        return std::numeric_limits<double>::infinity();

    if (reverbEngine && getSampleRate() > 0)
        return static_cast<double>(reverbEngine->getMaxTailSamples()) / getSampleRate();

    return 5.0; // Fallback
}

//==============================================================================
// Task 4: Extended for user preset support
int StudioVerbAudioProcessor::getNumPrograms()
{
    return static_cast<int>(factoryPresets.size() + userPresets.size());
}

int StudioVerbAudioProcessor::getCurrentProgram()
{
    return currentPresetIndex;
}

void StudioVerbAudioProcessor::setCurrentProgram(int index)
{
    loadPreset(index);
}

const juce::String StudioVerbAudioProcessor::getProgramName(int index)
{
    int factoryCount = static_cast<int>(factoryPresets.size());

    if (index >= 0 && index < factoryCount)
        return factoryPresets[index].name;

    int userIndex = index - factoryCount;
    if (userIndex >= 0 && userIndex < static_cast<int>(userPresets.size()))
        return userPresets[userIndex].name;

    return {};
}

void StudioVerbAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    int factoryCount = static_cast<int>(factoryPresets.size());
    int userIndex = index - factoryCount;

    if (userIndex >= 0 && userIndex < static_cast<int>(userPresets.size()))
    {
        userPresets[userIndex].name = newName;
    }
}

// Task 4: Save user preset
void StudioVerbAudioProcessor::saveUserPreset(const juce::String& name)
{
    // Validate preset name
    if (name.isEmpty())
    {
        DBG("Warning: Cannot save preset with empty name");
        return;
    }

    // Limit number of user presets to prevent excessive memory usage
    constexpr size_t maxUserPresets = 100;
    if (userPresets.size() >= maxUserPresets)
    {
        DBG("Warning: Maximum number of user presets (" << maxUserPresets << ") reached");
        return;
    }

    try
    {
        Preset preset;
        preset.name = name;
        preset.algorithm = currentAlgorithm.load();
        preset.size = currentSize.load();
        preset.damp = currentDamp.load();
        preset.predelay = currentPredelay.load();
        preset.mix = currentMix.load();
        preset.width = currentWidth.load();
        preset.lowRT60 = currentLowRT60.load();
        preset.midRT60 = currentMidRT60.load();
        preset.highRT60 = currentHighRT60.load();
        preset.infinite = currentInfinite.load();
        preset.oversampling = currentOversampling.load();
        preset.roomShape = currentRoomShape.load();
        preset.predelayBeats = currentPredelayBeats.load();
        preset.vintage = currentVintage.load();

        userPresets.push_back(preset);

        // Store in parameters state
        auto userPresetsNode = parameters.state.getOrCreateChildWithName("UserPresets", nullptr);
        juce::ValueTree presetNode("Preset");
        presetNode.setProperty("name", name, nullptr);
        presetNode.setProperty("algorithm", static_cast<int>(preset.algorithm), nullptr);
        presetNode.setProperty("size", preset.size, nullptr);
        presetNode.setProperty("damp", preset.damp, nullptr);
        presetNode.setProperty("predelay", preset.predelay, nullptr);
        presetNode.setProperty("mix", preset.mix, nullptr);
        presetNode.setProperty("width", preset.width, nullptr);
        presetNode.setProperty("lowRT60", preset.lowRT60, nullptr);
        presetNode.setProperty("midRT60", preset.midRT60, nullptr);
        presetNode.setProperty("highRT60", preset.highRT60, nullptr);
        presetNode.setProperty("infinite", preset.infinite, nullptr);
        presetNode.setProperty("oversampling", preset.oversampling, nullptr);
        presetNode.setProperty("roomShape", preset.roomShape, nullptr);
        presetNode.setProperty("predelayBeats", preset.predelayBeats, nullptr);
        presetNode.setProperty("vintage", preset.vintage, nullptr);
        userPresetsNode.appendChild(presetNode, nullptr);
    }
    catch (const std::exception& e)
    {
        DBG("Error saving user preset: " << e.what());
    }
}

// Task 4: Delete user preset
void StudioVerbAudioProcessor::deleteUserPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(userPresets.size()))
    {
        DBG("Warning: Invalid preset index for deletion: " << index);
        return;
    }

    try
    {
        userPresets.erase(userPresets.begin() + index);

        // Update parameters state
        auto userPresetsNode = parameters.state.getChildWithName("UserPresets");
        if (userPresetsNode.isValid() && index < userPresetsNode.getNumChildren())
        {
            userPresetsNode.removeChild(index, nullptr);
        }
        else
        {
            DBG("Warning: Preset tree inconsistency during deletion");
        }
    }
    catch (const std::exception& e)
    {
        DBG("Error deleting user preset: " << e.what());
    }
}

//==============================================================================
void StudioVerbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Validate spec to prevent crashes
    if (sampleRate <= 0.0 || samplesPerBlock <= 0)
    {
        DBG("StudioVerb: Invalid prepare spec - sampleRate=" << sampleRate
            << " samplesPerBlock=" << samplesPerBlock);
        return;
    }

    // Lazy initialization - create reverb engine on first prepareToPlay
    // This avoids heavy initialization during plugin scanning
    if (!reverbEngine)
    {
        DBG("StudioVerb: Creating reverb engine in prepareToPlay");
        reverbEngine = std::make_unique<ReverbEngineEnhanced>();
    }

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    reverbEngine->prepare(spec);

    // Reset to clear any previous state and prevent artifacts
    reverbEngine->reset();

    // Apply current parameters including advanced controls
    reverbEngine->setAlgorithm(static_cast<int>(currentAlgorithm.load()));
    reverbEngine->setSize(currentSize.load());
    reverbEngine->setDamping(currentDamp.load());
    reverbEngine->setPredelay(currentPredelay.load());
    reverbEngine->setMix(currentMix.load());
    reverbEngine->setWidth(currentWidth.load());

    // Apply advanced parameters
    reverbEngine->setLowDecayTime(currentLowRT60.load());
    reverbEngine->setMidDecayTime(currentMidRT60.load());
    reverbEngine->setHighDecayTime(currentHighRT60.load());
    reverbEngine->setInfiniteDecay(currentInfinite.load());

    int oversamplingChoice = currentOversampling.load();
    reverbEngine->setOversamplingEnabled(oversamplingChoice > 0);
    int factor = (oversamplingChoice == 0) ? 1 : (oversamplingChoice == 1) ? 2 : 4;
    reverbEngine->setOversamplingFactor(factor);

    // Apply room shape and vintage
    reverbEngine->setRoomShape(currentRoomShape.load());
    reverbEngine->setVintage(currentVintage.load());

    // Report latency from oversampling (use accurate method)
    setLatencySamples(reverbEngine->getOversamplingLatency());
}

void StudioVerbAudioProcessor::releaseResources()
{
    // Clear reverb state when stopping playback
    if (reverbEngine)
        reverbEngine->reset();
}

//==============================================================================
bool StudioVerbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Support only stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Check input
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono())
        return false;

    return true;
}

//==============================================================================
void StudioVerbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    juce::ScopedNoDenormals noDenormals;

    // Thread safety: Try to acquire lock, but don't block audio thread
    const juce::ScopedTryLock sl(processLock);
    if (!sl.isLocked())
        return; // Skip processing if we can't get the lock immediately

    // Critical buffer validation to prevent crashes
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
    {
        DBG("StudioVerb: Invalid buffer - channels=" << buffer.getNumChannels()
            << " samples=" << buffer.getNumSamples());
        return;
    }

    // Validate reverb engine exists
    if (!reverbEngine)
    {
        DBG("StudioVerb: Reverb engine is null!");
        return;
    }

    // Ensure we have at least 2 channels for stereo processing
    if (buffer.getNumChannels() < 2)
    {
        DBG("StudioVerb: Insufficient channels for stereo processing");
        return;
    }

    // Handle mono input by duplicating to stereo
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() >= 2)
    {
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    }

    // Process with reverb engine (lock already acquired above)
    if (reverbEngine)
    {
        reverbEngine->process(buffer);
    }
}

//==============================================================================
bool StudioVerbAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* StudioVerbAudioProcessor::createEditor()
{
    return new StudioVerbAudioProcessorEditor(*this);
}

//==============================================================================
void StudioVerbAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void StudioVerbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));

            // Task 4: Restore user presets
            userPresets.clear();
            auto userPresetsNode = parameters.state.getChildWithName("UserPresets");
            if (userPresetsNode.isValid())
            {
                for (int i = 0; i < userPresetsNode.getNumChildren(); ++i)
                {
                    auto presetNode = userPresetsNode.getChild(i);
                    Preset preset;
                    preset.name = presetNode.getProperty("name", "User Preset");
                    preset.algorithm = static_cast<Algorithm>(static_cast<int>(presetNode.getProperty("algorithm", 0)));
                    preset.size = presetNode.getProperty("size", 0.5f);
                    preset.damp = presetNode.getProperty("damp", 0.5f);
                    preset.predelay = presetNode.getProperty("predelay", 0.0f);
                    preset.mix = presetNode.getProperty("mix", 0.5f);
                    preset.width = presetNode.getProperty("width", 0.5f);
                    preset.lowRT60 = presetNode.getProperty("lowRT60", 2.0f);
                    preset.midRT60 = presetNode.getProperty("midRT60", 2.0f);
                    preset.highRT60 = presetNode.getProperty("highRT60", 1.5f);
                    preset.infinite = presetNode.getProperty("infinite", false);
                    preset.oversampling = presetNode.getProperty("oversampling", 0);
                    preset.roomShape = presetNode.getProperty("roomShape", 0);
                    preset.predelayBeats = presetNode.getProperty("predelayBeats", 0);
                    preset.vintage = presetNode.getProperty("vintage", 0.0f);
                    userPresets.push_back(preset);
                }
            }
        }
    }
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StudioVerbAudioProcessor();
}
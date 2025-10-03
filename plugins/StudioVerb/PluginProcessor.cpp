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
    reverbEngine = std::make_unique<ReverbEngineEnhanced>();  // Task 3: Using enhanced engine

    initializePresets();

    // Add parameter listeners (Task 10: Added width)
    parameters.addParameterListener(ALGORITHM_ID, this);
    parameters.addParameterListener(SIZE_ID, this);
    parameters.addParameterListener(DAMP_ID, this);
    parameters.addParameterListener(PREDELAY_ID, this);
    parameters.addParameterListener(MIX_ID, this);
    parameters.addParameterListener(WIDTH_ID, this);

    // Load default values
    auto* algorithmParam = parameters.getRawParameterValue(ALGORITHM_ID);
    auto* sizeParam = parameters.getRawParameterValue(SIZE_ID);
    auto* dampParam = parameters.getRawParameterValue(DAMP_ID);
    auto* predelayParam = parameters.getRawParameterValue(PREDELAY_ID);
    auto* mixParam = parameters.getRawParameterValue(MIX_ID);
    auto* widthParam = parameters.getRawParameterValue(WIDTH_ID);

    currentAlgorithm.store(static_cast<Algorithm>(static_cast<int>(*algorithmParam)));
    currentSize.store(*sizeParam);
    currentDamp.store(*dampParam);
    currentPredelay.store(*predelayParam);
    currentMix.store(*mixParam);
    currentWidth.store(*widthParam);
}

StudioVerbAudioProcessor::~StudioVerbAudioProcessor()
{
    parameters.removeParameterListener(ALGORITHM_ID, this);
    parameters.removeParameterListener(SIZE_ID, this);
    parameters.removeParameterListener(DAMP_ID, this);
    parameters.removeParameterListener(PREDELAY_ID, this);
    parameters.removeParameterListener(MIX_ID, this);
    parameters.removeParameterListener(WIDTH_ID, this);  // Task 10
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout StudioVerbAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Algorithm selector
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ALGORITHM_ID,
        "Algorithm",
        juce::StringArray { "Room", "Hall", "Plate", "Early Reflections" },
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

    // Task 10: Width parameter (0-1)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        WIDTH_ID,
        "Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        "",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(static_cast<int>(value * 100)) + "%"; },
        [](const juce::String& text) { return text.getFloatValue() / 100.0f; }));

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

        if (reverbEngine)
            reverbEngine->setPredelay(clampedPredelay);
    }
    else if (parameterID == MIX_ID)
    {
        float clampedMix = juce::jlimit(0.0f, 1.0f, newValue);
        currentMix.store(clampedMix);

        if (reverbEngine)
            reverbEngine->setMix(clampedMix);
    }
    else if (parameterID == WIDTH_ID)  // Task 10
    {
        float clampedWidth = juce::jlimit(0.0f, 1.0f, newValue);
        currentWidth.store(clampedWidth);

        if (reverbEngine)
            reverbEngine->setWidth(clampedWidth);
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
        // Update parameters
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
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    reverbEngine->prepare(spec);

    // Apply current parameters (Task 10: Added width)
    reverbEngine->setAlgorithm(static_cast<int>(currentAlgorithm.load()));
    reverbEngine->setSize(currentSize.load());
    reverbEngine->setDamping(currentDamp.load());
    reverbEngine->setPredelay(currentPredelay.load());
    reverbEngine->setMix(currentMix.load());
    reverbEngine->setWidth(currentWidth.load());
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